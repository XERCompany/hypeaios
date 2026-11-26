#include "kernel/hello/hello.h"

#include "hcpp/ecdsa.hpp"
#include "hcpp/memory.hpp"
#include "hcpp/rng.hpp"
#include "hcpp/sha256.hpp"
#include "kernel/hello/hellostore.h"
#include "kernel/serial.hpp"

namespace hype::hello {
    namespace {

        constexpr hcpp::u32 pbkdf2_iterations = 10000;
        constexpr hcpp::u32 lockout_threshold = 5;
        constexpr hcpp::u32 flag_locked = 1U;
        constexpr hcpp::size_t key_public_size = 64;
        constexpr hcpp::size_t key_signature_size = 64;

        bool secrets_equal(const hcpp::u8* left, const hcpp::u8* right,
                           hcpp::size_t size) noexcept {
            hcpp::u8 diff = 0;
            for (hcpp::size_t i = 0; i < size; ++i) diff |= left[i] ^ right[i];
            return diff == 0;
        }

        bool derive_verifier(HelloMethod method, const void* secret,
                             hcpp::size_t secret_size, const hcpp::u8 salt[16],
                             hcpp::u8 out[32]) noexcept {
            switch (method) {
                case HelloMethod::Password:
                    hcpp::pbkdf2_sha256(secret, secret_size, salt, 16,
                                        pbkdf2_iterations, out);
                    return true;
                case HelloMethod::PinCode: {
                    hcpp::u8 device[16];
                    if (!store::device_secret(device)) return false;
                    hcpp::u8 combined[32];
                    hcpp::copy(combined, salt, 16);
                    hcpp::copy(combined + 16, device, 16);
                    hcpp::pbkdf2_sha256(secret, secret_size, combined,
                                        sizeof(combined), pbkdf2_iterations, out);
                    return true;
                }
                case HelloMethod::Key:
                case HelloMethod::Face:
                    return false;
            }
            return false;
        }

        bool derive_challenge(hcpp::u32 uid, hcpp::u32 counter,
                              hcpp::u8 out[32]) noexcept {
            hcpp::u8 device[16];
            if (!store::device_secret(device)) return false;
            hcpp::u8 block[24];
            for (hcpp::u8 i = 0; i < 4; ++i) {
                block[i] = static_cast<hcpp::u8>(uid >> (i * 8));
                block[4 + i] = static_cast<hcpp::u8>(counter >> (i * 8));
            }
            hcpp::copy(block + 8, device, sizeof(device));
            hcpp::sha256(block, sizeof(block), out);
            return true;
        }

        void write_auth_marker(HelloMethod method, bool ok) noexcept {
            serial::write("hello auth=");
            serial::write(hello_method_name(method));
            serial::write_char(' ');
            serial::write_line(ok ? "ok" : "fail");
        }

    }  // namespace

    hcpp::u32 hello_register_user(hcpp::StringView name) noexcept {
        if (name.empty()) return 0;
        return store::insert_user(name.data(), name.size());
    }

    hcpp::u32 hello_find_user(hcpp::StringView name) noexcept {
        if (name.empty()) return 0;
        return store::find_user(name.data(), name.size());
    }

    hcpp::u32 hello_user_count() noexcept { return store::user_count(); }

    hcpp::u32 hello_credential_count() noexcept {
        return store::credential_count();
    }

    bool hello_enroll(hcpp::u32 uid, HelloMethod method, const void* secret,
                      hcpp::size_t secret_size) noexcept {
        if (uid == 0 || secret == nullptr || secret_size == 0) return false;
        if (method == HelloMethod::Face) return false;

        HelloCredential credential{};
        credential.uid = uid;
        credential.method = method;
        hcpp::rng_fill(credential.salt, sizeof(credential.salt));
        if (method == HelloMethod::Key) {
            if (secret_size != key_public_size) return false;
            hcpp::sha256(secret, secret_size, credential.verifier);
            hcpp::copy(credential.wrapped_key, secret, key_public_size);
        } else {
            if (!derive_verifier(method, secret, secret_size, credential.salt,
                                 credential.verifier))
                return false;
            hcpp::rng_fill(credential.wrapped_key, sizeof(credential.wrapped_key));
        }
        credential.fail_count = 0;
        credential.flags = 0;
        return store::save_credential(credential);
    }

    HelloResult hello_auth(hcpp::u32 uid, HelloMethod method, const void* secret,
                           hcpp::size_t secret_size) noexcept {
        if (method == HelloMethod::Face) return HelloResult::NoCamera;
        if (uid == 0 || secret == nullptr || secret_size == 0) {
            return HelloResult::BadSecret;
        }
        HelloCredential credential{};
        if (!store::load_credential(uid, method, credential)) {
            return HelloResult::NotEnrolled;
        }
        if ((credential.flags & flag_locked) != 0) return HelloResult::LockedOut;

        bool secret_ok = false;
        if (method == HelloMethod::Key) {
            if (secret_size != key_signature_size) return HelloResult::BadSecret;
            hcpp::u32 counter = 0;
            if (!store::key_counter_next(counter)) return HelloResult::BadSecret;
            hcpp::u8 challenge[32];
            if (!derive_challenge(uid, counter, challenge)) {
                return HelloResult::BadSecret;
            }
            const auto* signature = static_cast<const hcpp::u8*>(secret);
            secret_ok = hcpp::ecdsa_p256_verify(credential.wrapped_key, challenge,
                                                signature, signature + 32);
        } else {
            hcpp::u8 verifier[32];
            if (!derive_verifier(method, secret, secret_size, credential.salt,
                                 verifier))
                return HelloResult::BadSecret;
            secret_ok =
                secrets_equal(verifier, credential.verifier, sizeof(verifier));
        }

        if (secret_ok) {
            if (credential.fail_count != 0 || (credential.flags & flag_locked) != 0) {
                store::update_failures(uid, method, 0, 0);
            }
            return HelloResult::Ok;
        }

        ++credential.fail_count;
        if (credential.fail_count >= lockout_threshold) {
            credential.flags |= flag_locked;
        }
        store::update_failures(uid, method, credential.fail_count,
                               credential.flags);
        return (credential.flags & flag_locked) != 0 ? HelloResult::LockedOut
                                                     : HelloResult::BadSecret;
    }

    hcpp::u32 hello_methods(hcpp::u32 uid) noexcept {
        return store::methods_mask(uid);
    }

    bool hello_reset_failures(hcpp::u32 uid, HelloMethod method) noexcept {
        return store::update_failures(uid, method, 0, 0);
    }

    const char* hello_method_name(HelloMethod method) noexcept {
        switch (method) {
            case HelloMethod::Key: return "key";
            case HelloMethod::Password: return "password";
            case HelloMethod::PinCode: return "pincode";
            case HelloMethod::Face: return "face";
        }
        return "unknown";
    }

    bool hello_selftest() noexcept {
        bool ok = true;
        const hcpp::u32 uid = hello_register_user("hello-selftest");
        if (uid == 0) {
            serial::write_line("hello selftest=failed register-user");
            return false;
        }
        serial::write("hello user=hello-selftest uid=");
        serial::write_dec32(uid);
        serial::write("\r\n");

        const char password[] = "hype-pass-1234";
        const bool pw_enrolled =
            hello_enroll(uid, HelloMethod::Password, password, sizeof(password) - 1);
        ok &= pw_enrolled;
        serial::write("hello enroll=password ");
        serial::write_line(pw_enrolled ? "ok" : "fail");

        const HelloResult pw_ok =
            hello_auth(uid, HelloMethod::Password, password, sizeof(password) - 1);
        ok &= pw_ok == HelloResult::Ok;
        write_auth_marker(HelloMethod::Password, pw_ok == HelloResult::Ok);

        const char wrong_password[] = "not-the-password";
        const HelloResult pw_bad = hello_auth(uid, HelloMethod::Password,
                                              wrong_password,
                                              sizeof(wrong_password) - 1);
        ok &= pw_bad == HelloResult::BadSecret;
        write_auth_marker(HelloMethod::Password, pw_bad == HelloResult::Ok);

        const char pin[] = "1337";
        const bool pin_enrolled =
            hello_enroll(uid, HelloMethod::PinCode, pin, sizeof(pin) - 1);
        ok &= pin_enrolled;
        serial::write("hello enroll=pincode ");
        serial::write_line(pin_enrolled ? "ok" : "fail");

        const HelloResult pin_ok =
            hello_auth(uid, HelloMethod::PinCode, pin, sizeof(pin) - 1);
        ok &= pin_ok == HelloResult::Ok;
        write_auth_marker(HelloMethod::PinCode, pin_ok == HelloResult::Ok);

        const char wrong_pin[] = "9999";
        HelloResult pin_result = HelloResult::Ok;
        for (hcpp::u32 attempt = 0; attempt < 5; ++attempt) {
            pin_result = hello_auth(uid, HelloMethod::PinCode, wrong_pin,
                                    sizeof(wrong_pin) - 1);
        }
        ok &= pin_result == HelloResult::LockedOut;
        write_auth_marker(HelloMethod::PinCode, false);
        const HelloResult pin_locked =
            hello_auth(uid, HelloMethod::PinCode, pin, sizeof(pin) - 1);
        ok &= pin_locked == HelloResult::LockedOut;
        serial::write("hello lockout=pincode ");
        serial::write_line(pin_locked == HelloResult::LockedOut ? "ok" : "fail");

        const hcpp::u8 key_secret[32] = {
            0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
            0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00,
            0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0,
            0x0F, 0xED, 0xCB, 0xA9, 0x87, 0x65, 0x43, 0x21,
        };
        const hcpp::u8 key_nonce[32] = {
            0x51, 0x62, 0x73, 0x84, 0x95, 0xA6, 0xB7, 0xC8,
            0xD9, 0xEA, 0xFB, 0x0C, 0x1D, 0x2E, 0x3F, 0x40,
            0x52, 0x63, 0x74, 0x85, 0x96, 0xA7, 0xB8, 0xC9,
            0xDA, 0xEB, 0xFC, 0x0D, 0x1E, 0x2F, 0x30, 0x41,
        };
        hcpp::u8 key_public[64];
        const bool key_enrolled =
            hcpp::ecdsa_p256_pubkey(key_secret, key_public) &&
            hello_enroll(uid, HelloMethod::Key, key_public, sizeof(key_public));
        ok &= key_enrolled;
        serial::write("hello enroll=key ");
        serial::write_line(key_enrolled ? "ok" : "fail");

        hcpp::u32 counter = 0;
        hcpp::u8 challenge[32];
        hcpp::u8 signature[64];
        const bool signed_challenge =
            store::key_counter_peek(counter) &&
            derive_challenge(uid, counter, challenge) &&
            hcpp::ecdsa_p256_sign(key_secret, key_nonce, challenge, signature,
                                  signature + 32);
        const HelloResult key_auth =
            signed_challenge ? hello_auth(uid, HelloMethod::Key, signature,
                                          sizeof(signature))
                             : HelloResult::BadSecret;
        ok &= key_auth == HelloResult::Ok;
        write_auth_marker(HelloMethod::Key, key_auth == HelloResult::Ok);

        signature[0] ^= 0xA5;
        const HelloResult key_bad =
            hello_auth(uid, HelloMethod::Key, signature, sizeof(signature));
        ok &= key_bad == HelloResult::BadSecret;
        write_auth_marker(HelloMethod::Key, key_bad == HelloResult::Ok);

        const HelloResult face = hello_auth(uid, HelloMethod::Face, "t", 1);
        ok &= face == HelloResult::NoCamera;
        serial::write_line(face == HelloResult::NoCamera
                               ? "face=camera-missing"
                               : "face=unexpected");

        const hcpp::u32 mask = hello_methods(uid);
        ok &= mask == ((1U << static_cast<hcpp::u8>(HelloMethod::Key)) |
                       (1U << static_cast<hcpp::u8>(HelloMethod::Password)) |
                       (1U << static_cast<hcpp::u8>(HelloMethod::PinCode)));

        serial::write("users=");
        serial::write_dec32(hello_user_count());
        serial::write(" credentials=");
        serial::write_dec32(hello_credential_count());
        serial::write("\r\n");
        return ok;
    }

}  // namespace hype::hello
