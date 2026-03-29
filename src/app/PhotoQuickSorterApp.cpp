#include "app/PhotoQuickSorterApp.h"
#include "ui/PhotoQuickSorterFrame.h"

wxIMPLEMENT_APP(PhotoQuickSorterApp);

bool PhotoQuickSorterApp::OnInit() {
    wxInitAllImageHandlers();
    auto* frame = new PhotoQuickSorterFrame("Photo Quick Sorter");
    frame->Show(true);
    return true;
}
