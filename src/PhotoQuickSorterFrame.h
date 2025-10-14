#pragma once

#include "../FolderLocations.h"
#include <wx/wx.h>

class MainMenuPanel;
class SortPhotosPanel;

class PhotoQuickSorterFrame : public wxFrame {
public:
    PhotoQuickSorterFrame(const wxString& title);

    FolderLocations folderLocations;

    void ShowMainMenuPanel();
    void ShowSortPhotosPanel();
private:
    MainMenuPanel* m_mainMenuPanel;
    SortPhotosPanel* m_sortPhotosPanel;

};
