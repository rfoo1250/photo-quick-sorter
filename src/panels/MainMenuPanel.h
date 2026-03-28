#pragma once
#include <wx/wx.h>

enum {
    ID_BROWSE_BASE = wxID_HIGHEST + 1,
    ID_BROWSE_1,
    ID_BROWSE_2,
    ID_TO_SORT_PANEL
};

class PhotoQuickSorterFrame;

class MainMenuPanel : public wxPanel {
public:
    explicit MainMenuPanel(wxWindow* parent);
private:
    void OnBrowseFolder(wxCommandEvent& event);
    void OnStartSorting(wxCommandEvent& event);
    wxTextCtrl* m_baseFolderText = nullptr;
    wxTextCtrl* m_folder1Text = nullptr;
    wxTextCtrl* m_folder2Text = nullptr;
};
