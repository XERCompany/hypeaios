#include "kernel/hello/hello.h"

#include "hcpp/rng.hpp"
#include "hcpp/sha256.hpp"
#include "kernel/hello/hellostore.h"
#include "kernel/serial.hpp"

namespace hype::hello {
    namespace {

        constexpr hcpp::u32 pbkdf2_iterations = 10000;
        constexpr hcpp::u32 lockout_threshold = 5;
        constexpr hcpp::u32 flag_locked = 1U;

        bool secrets_equal(const hcpp::u8* left, const hcpp::u8* right,
                           hcpp::size_t size) noexcept {
            hcpp::u8 diff = 0;
            for (hcpp::size_t i = 0; i < size; ++i) diff |= left[i] ^ right[i];
            return diff == 0;
        }

        // Password: verifier = PBKDF2(secret, salt).
        // PinCode:  verifier = PBKDF2(pin, salt ‖ device_secret) — device-bound,
        //           so the short secret cannot be brute-forced off-device.
        // Key:      verifier = SHA-256(key blob) — placeholder until the FIDO2
        //           challenge-response path (real ECDSA verify) lands.
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
                    __builtin_memcpy(combined, salt, 16);
                    __builtin_memcpy(combined + 16, device, 16);
                    hcpp::pbkdf2_sha256(secret, secret_size, combined,
                                        sizeof(combined), pbkdf2_iterations, out);
                    return true;
                }
                case HelloMethod::Key:
                    hcpp::sha256(secret, secret_size, out);
                    return true;
                case HelloMethod::Face:
                    return false;
            }
            return false;
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
        if (method == HelloMethod::Face) return false;  // no camera provider

        HelloCredential credential{};
        credential.uid = uid;
        credential.method = method;
        hcpp::rng_fill(credential.salt, sizeof(credential.salt));
        if (!derive_verifier(method, secret, secret_size, credential.salt,
                             credential.verifier))
            return false;
        // Wrapped session-key blob: random placeholder until a real
        // authentication-key wrap (Hello Key protector) is implemented.
        hcpp::rng_fill(credential.wrapped_key, sizeof(credential.wrapped_key));
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

        hcpp::u8 verifier[32];
        if (!derive_verifier(method, secret, secret_size, credential.salt,
                             verifier))
            return HelloResult::BadSecret;

        if (secrets_equal(verifier, credential.verifier, sizeof(verifier))) {
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

        // Fail-count path: wrong pins until lockout at the threshold.
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

        // Match Face: honest failure — no camera provider on QEMU.
        const HelloResult face = hello_auth(uid, HelloMethod::Face, "t", 1);
        ok &= face == HelloResult::NoCamera;
        serial::write_line(face == HelloResult::NoCamera
                               ? "face=camera-missing"
                               : "face=unexpected");

        const hcpp::u32 mask = hello_methods(uid);
        ok &= mask == ((1U << static_cast<hcpp::u8>(HelloMethod::Password)) |
                       (1U << static_cast<hcpp::u8>(HelloMethod::PinCode)));

        serial::write("users=");
        serial::write_dec32(hello_user_count());
        serial::write(" credentials=");
        serial::write_dec32(hello_credential_count());
        serial::write("\r\n");
        return ok;
    }

}  // namespace hype::hello
