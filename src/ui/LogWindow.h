#pragma once

#include <wx/wx.h>
#include <wx/log.h>
#include <wx/textctrl.h>

/*
  LogWindow - simple frame containing a wxTextCtrl and an optional
  wxLogTextCtrl target that routes wxLog messages into the text control.

  Notes:
  - Call AttachAsGlobalLogTarget() to route global wxLog messages to this window.
  - Call DetachGlobalLogTarget() (or let the destructor do it) to restore the previous target.
*/
class LogWindow : public wxFrame
{
public:
    // parent may be null; pass your main frame pointer to have the log window owned
    LogWindow(wxWindow* parent = nullptr,
              wxWindowID id = wxID_ANY,
              const wxString& title = "Developer Console");

    ~LogWindow() override;

    // Expose the text control (if you want to write directly)
    wxTextCtrl* GetTextCtrl() const { return m_textCtrl; }

    // Attach this window as the global wxLog target (uses wxLogTextCtrl).
    // Returns true if attached successfully. Safe to call multiple times.
    bool AttachAsGlobalLogTarget();

    // Detach and restore previous target (if any). Call on shutdown.
    void DetachGlobalLogTarget();

    // Utility actions
    void Clear();
    bool SaveToFile(const wxString& path);

private:
    wxTextCtrl* m_textCtrl = nullptr;

    // previous global wxLog target (to restore on detach)
    wxLog* m_prevLogTarget = nullptr;

    // pointer to the wxLogTextCtrl instance we allocate (null if not attached)
    wxLog* m_ownedLogTarget = nullptr;

    void OnClear(wxCommandEvent& evt);
    void OnSave(wxCommandEvent& evt);

    wxDECLARE_EVENT_TABLE();
};
