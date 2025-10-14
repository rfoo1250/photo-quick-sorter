#include "SortPhotosPanel.h"
#include "PhotoQuickSorterFrame.h"

SortPhotosPanel::SortPhotosPanel(wxWindow* parent)
    : wxPanel(parent)
{
    auto* frame = dynamic_cast<PhotoQuickSorterFrame*>(GetParent());
    wxString info;

    if (frame) {
        const auto& folders = frame->folderLocations;
        info = "Sorting from:\n" +
               folders.baseFolder + "\n " +
               folders.folder1 + "\n " +
               folders.folder2;
    } else {
        info = "No folder data available.";
    }

    wxStaticText* label = new wxStaticText(this, wxID_ANY, info, wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER);
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(label, 1, wxALIGN_CENTER | wxALL, 10);
    SetSizer(sizer);
}
