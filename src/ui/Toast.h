#pragma once
#include <wx/wx.h>

// Borderless floating toast that slides up from the bottom of its parent window,
// then slides back down when Dismiss() is called.
//
// Usage:
//   m_toast = new Toast(wxGetTopLevelParent(this));
//   m_toast->ShowMessage("Loading...");
//   // ... do work ...
//   m_toast->Dismiss();

class Toast : public wxFrame {
public:
    explicit Toast(wxWindow* parent);

    // Slide in from below the parent with the given message.
    // Blocks briefly (~100ms) while animating, using wxSafeYield so the UI stays painted.
    void ShowMessage(const wxString& message);

    // Slide back down and hide.
    // Safe to call even if not currently shown.
    void Dismiss();

private:
    wxStaticText* m_label = nullptr;

    // Animate window position from fromY to toY.
    // If hideAfter is true, calls Show(false) at the end.
    void Animate(int fromY, int toY, bool hideAfter);
};
