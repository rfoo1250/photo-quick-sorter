#pragma once

#include <wx/wx.h>

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

    // Attach this window as the global wxLog target (uses wxLogTextCtrl)
    // Returns true if attached successfully.
    bool AttachAsGlobalLogTarget();

    // Detach and restore previous target (if any). Call on shutdown.
    void DetachGlobalLogTarget();

    // Utility actions
    void Clear();
    bool SaveToFile(const wxString& path);

private:
    wxTextCtrl* m_textCtrl = nullptr;
    wxLog* m_prevLogTarget = nullptr; // previous global wxLog, to restore on detach

    void OnClear(wxCommandEvent& evt);
    void OnSave(wxCommandEvent& evt);

    wxDECLARE_EVENT_TABLE();
};
