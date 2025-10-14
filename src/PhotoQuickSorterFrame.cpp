#include "PhotoQuickSorterFrame.h"
#include "MainMenuPanel.h"
#include "SortPhotosPanel.h"

PhotoQuickSorterFrame::PhotoQuickSorterFrame(const wxString& title)
    : wxFrame(nullptr, wxID_ANY, title, wxDefaultPosition, wxSize(500, 500))
{
    folderLocations.Clear(); // optional safety
    
    m_mainMenuPanel = new MainMenuPanel(this);
    m_sortPhotosPanel = new SortPhotosPanel(this);

    wxBoxSizer* frameSizer = new wxBoxSizer(wxVERTICAL);
    frameSizer->Add(m_mainMenuPanel, 1, wxEXPAND);
    frameSizer->Add(m_sortPhotosPanel, 1, wxEXPAND);

    SetSizer(frameSizer);
    m_sortPhotosPanel->Hide();
    Layout();
}

void PhotoQuickSorterFrame::ShowMainMenuPanel() {
    m_sortPhotosPanel->Hide();
    m_mainMenuPanel->Show();
    Layout();
}

void PhotoQuickSorterFrame::ShowSortPhotosPanel() {
    m_mainMenuPanel->Hide();
    m_sortPhotosPanel->Show();
    Layout();
}
