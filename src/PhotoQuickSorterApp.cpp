#include "PhotoQuickSorterApp.h"
#include "PhotoQuickSorterFrame.h"

wxIMPLEMENT_APP(PhotoQuickSorterApp);

bool PhotoQuickSorterApp::OnInit() {
    auto* frame = new PhotoQuickSorterFrame("Photo Quick Sorter");
    frame->Show(true);
    return true;
}
