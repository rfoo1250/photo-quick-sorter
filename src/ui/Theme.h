#pragma once
#include <wx/colour.h>
#include <wx/font.h>

namespace Theme {

// ── Backgrounds ──────────────────────────────────────────────────────────────
inline const wxColour BgPanel     { 248, 248, 246 };  // warm off-white
inline const wxColour BgImageView {  24,  24,  24 };  // dark canvas behind photo
inline const wxColour BgToast     {  40,  40,  40 };  // toast notification

// ── Text ─────────────────────────────────────────────────────────────────────
inline const wxColour TextPrimary   {  20,  20,  20 };
inline const wxColour TextSecondary {  90,  90,  90 };
inline const wxColour TextHint      { 140, 140, 140 };
inline const wxColour TextOnDark    { 255, 255, 255 };

// ── Accent ───────────────────────────────────────────────────────────────────
inline const wxColour Accent     {  60, 110, 200 };
inline const wxColour AccentText { 255, 255, 255 };

// ── Semantic states ───────────────────────────────────────────────────────────
inline const wxColour Danger     { 200,  55,  55 };
inline const wxColour DangerText { 255, 255, 255 };

// ── Spacing ───────────────────────────────────────────────────────────────────
constexpr int PadSmall  =  6;
constexpr int PadMedium = 10;
constexpr int PadLarge  = 16;

// ── Fonts ─────────────────────────────────────────────────────────────────────
inline wxFont MakeFont(int ptSize,
                       wxFontWeight weight = wxFONTWEIGHT_NORMAL,
                       wxFontStyle  style  = wxFONTSTYLE_NORMAL)
{
    return wxFont(ptSize, wxFONTFAMILY_DEFAULT, style, weight, false, "Segoe UI");
}

inline wxFont FontBody()  { return MakeFont(10); }
inline wxFont FontSmall() { return MakeFont(9); }
inline wxFont FontHint()  { return MakeFont(9); }
inline wxFont FontDone()  { return MakeFont(16, wxFONTWEIGHT_BOLD); }

} // namespace Theme
