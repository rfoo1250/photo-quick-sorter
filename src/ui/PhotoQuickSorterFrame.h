#pragma once

#include <memory>
#include <wx/wx.h>

#include "core/FolderLocations.h"
#include "core/ImageRepository.h"
#include "utils/logging.h"
#if DEBUG_MODE
#  include "ui/LogWindow.h"
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
    ImageRepository imageRepo;

    void ShowMainMenuPanel();
    void ShowSortPhotosPanel();

private:
    void OnSize(wxSizeEvent& evt);
    MainMenuPanel* m_mainMenuPanel = nullptr;
    SortPhotosPanel* m_sortPhotosPanel = nullptr;

    #if DEBUG_MODE
        std::unique_ptr<LogWindow> m_logWindow;
    #endif
};
