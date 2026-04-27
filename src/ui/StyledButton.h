#pragma once
#include <wx/wx.h>
#include <wx/dcbuffer.h>
#include <algorithm>

// Owner-drawn panel-based button with a solid custom background color.
// Native wxButton ignores SetBackgroundColour on Windows with visual styles
// enabled, so this class provides reliable custom coloring for CTAs.
// Fires wxEVT_BUTTON on click — existing Bind(wxEVT_BUTTON, ...) calls work.
class StyledButton : public wxPanel {
public:
    StyledButton(wxWindow* parent, wxWindowID id, const wxString& label,
                 const wxColour& bg, const wxColour& fg)
        : wxPanel(parent, id, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
        , m_bg(bg)
        , m_bgHover(Lighten(bg, 18))
        , m_fg(fg)
        , m_text(label)
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetCursor(wxCursor(wxCURSOR_HAND));
        SetMinSize(wxSize(-1, 40));

        Bind(wxEVT_PAINT,        &StyledButton::OnPaint, this);
        Bind(wxEVT_LEFT_UP,      &StyledButton::OnClick, this);
        Bind(wxEVT_ENTER_WINDOW, &StyledButton::OnEnter, this);
        Bind(wxEVT_LEAVE_WINDOW, &StyledButton::OnLeave, this);
        Bind(wxEVT_SET_FOCUS,    [this](wxFocusEvent& e) { Refresh(); e.Skip(); });
        Bind(wxEVT_KILL_FOCUS,   [this](wxFocusEvent& e) { Refresh(); e.Skip(); });
        Bind(wxEVT_KEY_DOWN,     &StyledButton::OnKeyDown, this);
    }

    void SetLabel(const wxString& label) override { m_text = label; Refresh(); }
    wxString GetLabel() const override { return m_text; }

    void SetBitmap(const wxBitmap& bmp) { m_icon = bmp; Refresh(); }

private:
    static wxColour Lighten(const wxColour& c, int v)
    {
        return wxColour(
            (unsigned char)std::min(255, (int)c.Red()   + v),
            (unsigned char)std::min(255, (int)c.Green() + v),
            (unsigned char)std::min(255, (int)c.Blue()  + v));
    }

    void OnPaint(wxPaintEvent&)
    {
        wxAutoBufferedPaintDC dc(this);
        wxSize sz = GetClientSize();
        dc.SetBackground(wxBrush(m_hovered ? m_bgHover : m_bg));
        dc.Clear();
        dc.SetFont(GetFont());
        dc.SetTextForeground(m_fg);
        wxSize tsz = dc.GetTextExtent(m_text);
        if (m_icon.IsOk()) {
            const int gap   = 6;
            int iconW = m_icon.GetWidth(), iconH = m_icon.GetHeight();
            int totalW = iconW + gap + tsz.x;
            int startX = (sz.x - totalW) / 2;
            dc.DrawBitmap(m_icon, startX, (sz.y - iconH) / 2, true);
            dc.DrawText(m_text, startX + iconW + gap, (sz.y - tsz.y) / 2);
        } else {
            dc.DrawText(m_text, (sz.x - tsz.x) / 2, (sz.y - tsz.y) / 2);
        }
        if (HasFocus()) {
            dc.SetPen(wxPen(*wxWHITE, 1, wxPENSTYLE_DOT));
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            dc.DrawRectangle(4, 4, sz.x - 8, sz.y - 8);
        }
    }

    void FireClick()
    {
        wxCommandEvent evt(wxEVT_BUTTON, GetId());
        evt.SetEventObject(this);
        ProcessWindowEvent(evt);
    }

    void OnClick(wxMouseEvent& e)
    {
        FireClick();
        e.Skip();
    }

    void OnKeyDown(wxKeyEvent& e)
    {
        int key = e.GetKeyCode();
        if (key == WXK_RETURN || key == WXK_SPACE)
            FireClick();
        else
            e.Skip();
    }

    void OnEnter(wxMouseEvent& e) { m_hovered = true;  Refresh(); e.Skip(); }
    void OnLeave(wxMouseEvent& e) { m_hovered = false; Refresh(); e.Skip(); }

    wxColour m_bg, m_bgHover, m_fg;
    wxString m_text;
    wxBitmap m_icon;
    bool     m_hovered = false;
};
