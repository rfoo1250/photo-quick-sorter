#include "ui/PhotoQuickSorterFrame.h"
#include "panels/MainMenuPanel.h"
#include "panels/SortPhotosPanel.h"
#include "utils/logging.h"
#include "ui/Theme.h"
#include <memory>

#if DEBUG_MODE
#include "ui/LogWindow.h"
#endif

PhotoQuickSorterFrame::PhotoQuickSorterFrame(const wxString &title)
    : wxFrame(nullptr, wxID_ANY, title, wxDefaultPosition, wxSize(960, 720))
{
    LOG_DEBUG("PhotoQuickSorterFrame ctor");

    folderLocations.Clear(); // optional safety

    m_mainMenuPanel = new MainMenuPanel(this);
    m_sortPhotosPanel = new SortPhotosPanel(this);

    wxBoxSizer *frameSizer = new wxBoxSizer(wxVERTICAL);
    frameSizer->Add(m_mainMenuPanel, 1, wxEXPAND);
    frameSizer->Add(m_sortPhotosPanel, 1, wxEXPAND);

    SetSizer(frameSizer);
    SetBackgroundColour(Theme::BgPanel);
    Centre();

    // hide the sort panel at startup so only main menu shows
    m_sortPhotosPanel->Hide();

#if DEBUG_MODE
    // Create and show the developer console and route global wxLog() output into it.
    m_logWindow = std::make_unique<LogWindow>(this);
    // If you prefer it hidden by default, remove the next line
    m_logWindow->Show();

    if (!m_logWindow->AttachAsGlobalLogTarget())
    {
        LOG_WARN("Failed to attach LogWindow as global log target");
    }
    else
    {
        LOG_INFO("Developer console (LogWindow) attached");
    }
#else
    // Suppress all wxLog output in release — no log window, no popup dialogs.
    // User-facing errors are reported via wxMessageBox directly where they occur.
    wxLog::EnableLogging(false);
#endif

    Bind(wxEVT_SIZE, &PhotoQuickSorterFrame::OnSize, this);
    Layout();
}

void PhotoQuickSorterFrame::ShowMainMenuPanel()
{
    if (m_sortPhotosPanel) m_sortPhotosPanel->Hide();
    if (m_mainMenuPanel) {
        m_mainMenuPanel->Show();
        m_mainMenuPanel->RefreshPreview();
    }
    Layout();
}

void PhotoQuickSorterFrame::ShowSortPhotosPanel()
{
    if (m_mainMenuPanel) m_mainMenuPanel->Hide();
    if (m_sortPhotosPanel) {
        m_sortPhotosPanel->RefreshData();
        m_sortPhotosPanel->Show();
    }
    Layout();
}

void PhotoQuickSorterFrame::OnSize(wxSizeEvent& evt)
{
    LOG_DEBUG("Frame resize: %dx%d", evt.GetSize().x, evt.GetSize().y);
    evt.Skip();
}

PhotoQuickSorterFrame::~PhotoQuickSorterFrame()
{
#if DEBUG_MODE
    if (m_logWindow) {
        // Detach and destroy the developer console cleanly
        m_logWindow->DetachGlobalLogTarget(); // LogWindow destructor also does this; explicit is fine
        m_logWindow->Destroy();
        m_logWindow.reset();
    }
#endif
}
