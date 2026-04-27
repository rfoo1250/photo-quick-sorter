#include "app/PhotoQuickSorterApp.h"
#include "ui/PhotoQuickSorterFrame.h"
#include "ui/Theme.h"

wxIMPLEMENT_APP(PhotoQuickSorterApp);

bool PhotoQuickSorterApp::OnInit() {
    wxInitAllImageHandlers();
    auto* frame = new PhotoQuickSorterFrame("Photo Quick Sorter");
    frame->SetFont(Theme::FontBody());
    frame->Show(true);
    return true;
}
