#pragma once
#include <wx/wx.h>

class PhotoQuickSorterApp : public wxApp {
public:
    bool OnInit() override;
    int OnExit() override;

private:
#ifdef __WXMSW__
    bool m_mediaFoundationStarted = false;
#endif
};
