#pragma once

#include <cstdint>
#include <algorithm>

namespace aqua {

struct Color {
    uint8_t r{0};
    uint8_t g{0};
    uint8_t b{0};
    uint8_t a{255};

    constexpr Color() = default;
    constexpr Color(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha = 255)
        : r(red), g(green), b(blue), a(alpha) {}

    static constexpr Color rgb(uint8_t r, uint8_t g, uint8_t b) {
        return Color(r, g, b, 255);
    }

    static constexpr Color rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        return Color(r, g, b, a);
    }

    static constexpr Color hex(uint32_t hex_val, uint8_t a = 255) {
        return Color((hex_val >> 16) & 0xFF, (hex_val >> 8) & 0xFF, hex_val & 0xFF, a);
    }

    // Konwersja na 32-bitową liczbę (Little-endian RGBA: R, G, B, A w bajtach 0, 1, 2, 3)
    constexpr uint32_t to_u32() const {
        return static_cast<uint32_t>(r) |
               (static_cast<uint32_t>(g) << 8) |
               (static_cast<uint32_t>(b) << 16) |
               (static_cast<uint32_t>(a) << 24);
    }

    // Blendowanie dwóch kolorów (this over dst)
    Color blend_over(Color dst) const {
        if (a == 255) return *this;
        if (a == 0) return dst;

        uint32_t alpha = a;
        uint32_t inv_alpha = 255 - alpha;

        uint8_t out_r = static_cast<uint8_t>((r * alpha + dst.r * inv_alpha) / 255);
        uint8_t out_g = static_cast<uint8_t>((g * alpha + dst.g * inv_alpha) / 255);
        uint8_t out_b = static_cast<uint8_t>((b * alpha + dst.b * inv_alpha) / 255);
        uint8_t out_a = static_cast<uint8_t>(std::min(255u, alpha + (dst.a * inv_alpha) / 255));

        return Color(out_r, out_g, out_b, out_a);
    }

    // Paleta kolorów macOS Sequoia / Aqua
    static inline const Color Clear{0, 0, 0, 0};
    static inline const Color Black{0, 0, 0, 255};
    static inline const Color White{255, 255, 255, 255};
    
    // Tła okien i paneli
    static inline const Color MacDarkBg{32, 32, 34, 255};          // Ciemne tło okna kalkulatora/terminala
    static inline const Color MacDisplayBg{24, 24, 26, 255};       // Ciemniejszy wyświetlacz
    static inline const Color MacWindowBg{242, 242, 247, 255};     // Jasne tło okna macOS
    static inline const Color MacTitleBarBg{235, 235, 240, 255};   // Pasek tytułowy
    static inline const Color MacBorder{50, 50, 54, 255};          // Subtelna ramka ciemna
    static inline const Color MacLightBorder{210, 210, 215, 255};  // Subtelna ramka jasna

    // Przyciski i akcenty macOS
    static inline const Color MacOrange{255, 159, 10, 255};        // Pomarańczowy operator (#FF9F0A)
    static inline const Color MacOrangePressed{204, 125, 5, 255};  // Wciśnięty pomarańcz
    static inline const Color MacBtnDark{58, 58, 60, 255};         // Ciemnoszary przycisk numeryczny (#3A3A3C)
    static inline const Color MacBtnDarkPressed{80, 80, 84, 255};  // Wciśnięty ciemnoszary
    static inline const Color MacBtnLight{165, 165, 165, 255};     // Jasnoszary funkcyjny (#A5A5A5)
    static inline const Color MacBtnLightPressed{210, 210, 210, 255};

    static inline const Color MacBlue{0, 122, 255, 255};           // Niebieski akcent macOS (#007AFF)
    static inline const Color MacGreen{52, 199, 89, 255};          // Zielony (#34C759)
    static inline const Color MacRed{255, 59, 48, 255};            // Czerwony (#FF3B30)
    static inline const Color MacPurple{175, 82, 222, 255};        // Fioletowy (#AF52DE)
};

} // namespace aqua
