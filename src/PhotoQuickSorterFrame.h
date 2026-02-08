#pragma once

#include <memory>
#include <wx/wx.h>

#include "FolderLocations.h"
// #include "ImageRepository.h"
#include "logging.h"
#if DEBUG_MODE
#  include "LogWindow.h"
#endif

// encompasses the main frame of the application, which contains the main menu and the sorting panel
class MainMenuPanel;
class SortPhotosPanel;

class PhotoQuickSorterFrame : public wxFrame {
public:
    // Keep your chosen constructor signature; using title as you had
    PhotoQuickSorterFrame(const wxString& title);
    ~PhotoQuickSorterFrame() override;

    FolderLocations folderLocations;
    // ImageRepository imageRepo;

    void ShowMainMenuPanel();
    void ShowSortPhotosPanel();

private:
    MainMenuPanel* m_mainMenuPanel = nullptr;
    SortPhotosPanel* m_sortPhotosPanel = nullptr;

    #if DEBUG_MODE
        std::unique_ptr<LogWindow> m_logWindow;
    #endif
};
