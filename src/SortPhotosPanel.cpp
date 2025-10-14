#include "SortPhotosPanel.h"
#include "PhotoQuickSorterFrame.h"

SortPhotosPanel::SortPhotosPanel(wxWindow* parent)
    : wxPanel(parent)
{
    m_infoLabel = new wxStaticText(this, wxID_ANY, "Sorting Screen", wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER);
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(m_infoLabel, 1, wxALIGN_CENTER | wxALL, 10);
    SetSizer(sizer);
}

void SortPhotosPanel::RefreshData() {
    auto* frame = dynamic_cast<PhotoQuickSorterFrame*>(GetParent());
    if (!frame) return;

    const auto& folders = frame->folderLocations;
    wxString info = wxString::Format("Sorting from:\n%s\n %s\n %s",
                                     folders.baseFolder,
                                     folders.folder1,
                                     folders.folder2);

    m_infoLabel->SetLabel(info);
    Layout();  // reflow in case label size changes
}