#include "ui/LogWindow.h"
#include <wx/filedlg.h>
#include <wx/stdpaths.h>
#include <wx/filefn.h>
#include <wx/textfile.h>
#include <wx/sizer.h>

enum {
    ID_LOG_CLEAR = wxID_HIGHEST + 1,
    ID_LOG_SAVE
};

wxBEGIN_EVENT_TABLE(LogWindow, wxFrame)
    EVT_BUTTON(ID_LOG_CLEAR, LogWindow::OnClear)
    EVT_BUTTON(ID_LOG_SAVE, LogWindow::OnSave)
wxEND_EVENT_TABLE()

LogWindow::LogWindow(wxWindow* parent, wxWindowID id, const wxString& title)
    : wxFrame(parent, id, title, wxDefaultPosition, wxSize(700, 300), wxDEFAULT_FRAME_STYLE | wxFRAME_NO_TASKBAR),
      m_textCtrl(nullptr),
      m_prevLogTarget(nullptr),
      m_ownedLogTarget(nullptr)
{
    // Create controls: a toolbar-like horizontal sizer with Clear / Save, and the log text area below
    wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);

    wxBoxSizer* buttonSizer = new wxBoxSizer(wxHORIZONTAL);
    wxButton* clearBtn = new wxButton(this, ID_LOG_CLEAR, "Clear");
    wxButton* saveBtn = new wxButton(this, ID_LOG_SAVE, "Save...");
    buttonSizer->Add(clearBtn, 0, wxALL, 4);
    buttonSizer->Add(saveBtn, 0, wxALL, 4);
    buttonSizer->AddStretchSpacer();

    topSizer->Add(buttonSizer, 0, wxEXPAND);

    // The text control: multiline, read-only, with vertical scroll
    m_textCtrl = new wxTextCtrl(this, wxID_ANY, wxEmptyString,
                                wxDefaultPosition, wxDefaultSize,
                                wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2 | wxHSCROLL);

    topSizer->Add(m_textCtrl, 1, wxEXPAND | wxALL, 4);

    SetSizer(topSizer);
    Layout();
    CentreOnParent();
}

LogWindow::~LogWindow()
{
    // If still attached, detach and restore previous log target to avoid dangling pointer
    DetachGlobalLogTarget();
}

bool LogWindow::AttachAsGlobalLogTarget()
{
    if (!m_textCtrl) return false;

    // If already attached, do nothing
    if (m_ownedLogTarget != nullptr) return true;

    // create a wxLogTextCtrl that writes into our text control
    // In modern wxWidgets wxLogTextCtrl is provided by <wx/log.h>
    wxLog* newTarget = new wxLogTextCtrl(m_textCtrl);

    // SetActiveTarget returns the previous target; store it to restore later
    m_prevLogTarget = wxLog::SetActiveTarget(newTarget);

    // Remember the pointer we allocated so we can delete it later.
    m_ownedLogTarget = newTarget;
    return true;
}

void LogWindow::DetachGlobalLogTarget()
{
    // Only detach if we previously attached and still own the target
    if (m_ownedLogTarget != nullptr)
    {
        // Restore previous target (may be nullptr) and avoid deleting a target we don't own
        wxLog::SetActiveTarget(m_prevLogTarget);

        // delete the wxLogTextCtrl we created
        delete m_ownedLogTarget;
        m_ownedLogTarget = nullptr;
        m_prevLogTarget = nullptr;
    }
}

void LogWindow::Clear()
{
    if (m_textCtrl) m_textCtrl->Clear();
}

bool LogWindow::SaveToFile(const wxString& path)
{
    if (!m_textCtrl) return false;
    wxString contents = m_textCtrl->GetValue();
    if (contents.IsEmpty()) return false;

    wxTextFile file;
    if (wxFileExists(path)) {
        if (!file.Open(path)) return false;
        file.Clear();
    } else {
        if (!file.Create(path)) return false;
    }

    wxArrayString lines = wxSplit(contents, '\n', '\0');
    for (const auto& line : lines) {
        file.AddLine(line);
    }
    bool ok = file.Write();
    file.Close();
    return ok;
}

void LogWindow::OnClear(wxCommandEvent& WXUNUSED(evt))
{
    Clear();
}

void LogWindow::OnSave(wxCommandEvent& WXUNUSED(evt))
{
    wxString defaultPath = wxStandardPaths::Get().GetDocumentsDir() + wxFILE_SEP_PATH + "app_log.txt";
    wxFileDialog dlg(this, "Save logs to file", wxEmptyString, defaultPath,
                     "Text files (*.txt)|*.txt|All files (*.*)|*.*",
                     wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dlg.ShowModal() == wxID_OK) {
        if (!SaveToFile(dlg.GetPath())) {
            wxLogWarning("Failed to save log file: %s", dlg.GetPath());
        }
    }
}
