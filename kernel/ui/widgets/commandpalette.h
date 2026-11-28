#pragma once

// CommandPalette — centered launcher panel (design spec
// docs/research/hypeui-design.md §5 `command`; the `command` primitive
// IS the launcher body). A search input row sits on top of a
// fuzzy-filtered result list: apps, shell actions and system commands
// the host seeds via add_item(). Fuzzy matching is subsequence-based,
// scored with a consecutive-hit bonus (plus a word-boundary bonus) so
// tight runs outrank scattered hits.
//
// States: open / closed (the host owns visibility — usually through
// OverlayHost, which centers the panel and paints the scrim). Per-row:
// selected (accent wash + 2px accent bar), icon slot (single glyph or
// the app-monogram fallback), right-aligned dim hint. Keyboard contract:
// Up/Down move the selection, Enter commits through on_accept, Esc is
// left to the host (OverlayHost closes the modal). Pointer: hover
// selects, release on a row commits, press outside the panel asks the
// host to dismiss via on_dismiss.

#include "hcpp/fonts/font.hpp"
#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"
#include "kernel/ui/geometry.h"
#include "kernel/ui/widget.h"

namespace hype::ui {

    class Canvas;

    // Pointer event kinds routed to CommandPalette::handle_mouse.
    enum class PaletteMouse : hcpp::u8 { Move, Down, Up };

    struct PaletteItem {
        hcpp::StringView icon;   // single glyph for the 16px slot
        hcpp::StringView label;
        hcpp::StringView hint;   // right-aligned dim text (kind/shortcut)
        hcpp::u32 id = 0;
    };

    class CommandPalette;
    using PaletteCallback = void (*)(CommandPalette& palette,
                                     hcpp::u32 id, void* context) noexcept;
    using PaletteDismiss = void (*)(CommandPalette& palette,
                                    void* context) noexcept;

    class CommandPalette : public Widget {
    public:
        static constexpr hcpp::size_t max_items = 40;
        static constexpr hcpp::size_t max_rows = 8;
        static constexpr hcpp::size_t query_cap = 47;
        static constexpr hcpp::size_t no_row =
            static_cast<hcpp::size_t>(-1);
        static constexpr hcpp::i32 panel_width = 520;
        static constexpr hcpp::i32 input_height = 40;
        static constexpr hcpp::i32 row_height = 30;
        static constexpr hcpp::i32 pad = 10;
        static constexpr hcpp::i32 list_gap = 8;

        CommandPalette() noexcept = default;

        // Item strings are borrowed: labels/icons/hints must outlive the
        // open palette (hxrt names and string literals both qualify).
        bool add_item(hcpp::StringView icon, hcpp::StringView label,
                      hcpp::StringView hint, hcpp::u32 id) noexcept;
        void clear_items() noexcept;
        hcpp::size_t item_count() const noexcept;

        // Open-time reset: clears the query and rebuilds the full result
        // list (call before overlays.open every time the palette shows).
        void reset() noexcept;
        hcpp::StringView query() const noexcept;
        hcpp::size_t query_length() const noexcept;

        hcpp::size_t result_count() const noexcept;
        const PaletteItem& result_item(hcpp::size_t row) const noexcept;
        hcpp::size_t selected() const noexcept;

        // Subsequence match; <0 = no match, higher = better. Consecutive
        // hits and word/camel boundaries earn bonuses.
        static hcpp::i32 fuzzy_score(hcpp::StringView query,
                                     hcpp::StringView text) noexcept;

        void set_on_accept(PaletteCallback callback, void* context) noexcept;
        void set_on_dismiss(PaletteDismiss callback, void* context) noexcept;
        // Commits the selected row through on_accept. Returns false when
        // the result list is empty.
        bool accept() noexcept;

        // Returns true when visual state changed (repaint wanted).
        bool handle_mouse(Point point, PaletteMouse event) noexcept;
        bool handle_key(char ascii) noexcept override;

        void set_font(const hcpp::fonts::FontFace* font) noexcept;

        Size measure(Size available) noexcept override;
        void paint(Canvas& canvas) noexcept override;

        // Sub-rects (bounds-relative geometry for tests/hosts).
        Rect input_rect() const noexcept;
        Rect row_rect(hcpp::size_t row) const noexcept;

    private:
        void rebuild() noexcept;
        bool move_selection(hcpp::i32 delta) noexcept;
        bool push_char(char c) noexcept;
        bool pop_char() noexcept;
        const hcpp::fonts::FontFace* resolved_font() const noexcept;

        PaletteItem items_[max_items] = {};
        // Filtered+ranked view: index into items_ ordered by score.
        hcpp::u8 result_idx_[max_items] = {};
        hcpp::size_t count_ = 0;
        hcpp::size_t result_count_ = 0;
        hcpp::size_t selected_ = 0;
        char query_[query_cap + 1] = {};
        hcpp::u8 query_len_ = 0;
        PaletteCallback on_accept_ = nullptr;
        void* accept_ctx_ = nullptr;
        PaletteDismiss on_dismiss_ = nullptr;
        void* dismiss_ctx_ = nullptr;
        const hcpp::fonts::FontFace* font_ = nullptr;
    };

}  // namespace hype::ui
