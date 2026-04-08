#include "app/PhotoQuickSorterApp.h"
#include "ui/PhotoQuickSorterFrame.h"
#include "utils/logging.h"

#ifdef __WXMSW__
#include <mfapi.h>
#endif

wxIMPLEMENT_APP(PhotoQuickSorterApp);

bool PhotoQuickSorterApp::OnInit() {
    wxInitAllImageHandlers();

#ifdef __WXMSW__
    HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
    if (SUCCEEDED(hr)) {
        m_mediaFoundationStarted = true;
        LOG_INFO("Media Foundation initialized");
    } else {
        LOG_ERROR("MFStartup failed: hr=0x%08X", (unsigned)hr);
    }
#endif

    auto* frame = new PhotoQuickSorterFrame("Photo Quick Sorter");
    frame->Show(true);
    return true;
}

int PhotoQuickSorterApp::OnExit()
{
#ifdef __WXMSW__
    if (m_mediaFoundationStarted) {
        HRESULT hr = MFShutdown();
        if (FAILED(hr))
            LOG_ERROR("MFShutdown failed: hr=0x%08X", (unsigned)hr);
        else
            LOG_INFO("Media Foundation shut down");
        m_mediaFoundationStarted = false;
    }
#endif

    return wxApp::OnExit();
}
