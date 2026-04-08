#include "panels/SortPhotosPanel.h"
#include "ui/PhotoQuickSorterFrame.h"
#include "ui/ThumbnailGrid.h"
#include "utils/logging.h"
#include "utils/MediaUtils.h"
#include <wx/filename.h>
#include <wx/filefn.h>
#include <wx/dir.h>
#include <wx/textfile.h>
#include <wx/stdpaths.h>
#include <algorithm>

#ifdef __WXMSW__
#include <mfplay.h>
#include <propvarutil.h>
#endif

// ─────────────────────────────────────────────
// MFPlay callback (fires on UI thread)
// ─────────────────────────────────────────────

#ifdef __WXMSW__
class MFPlayCallback : public IMFPMediaPlayerCallback {
public:
    explicit MFPlayCallback(SortPhotosPanel* owner) : m_owner(owner) {}

    ULONG   STDMETHODCALLTYPE AddRef()  override { return ++m_ref; }
    ULONG   STDMETHODCALLTYPE Release() override {
        ULONG r = --m_ref; if (r == 0) delete this; return r;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == __uuidof(IMFPMediaPlayerCallback))
            { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }

    void STDMETHODCALLTYPE OnMediaPlayerEvent(MFP_EVENT_HEADER* pHdr) override {
        if (!m_owner || !pHdr) return;
        if (FAILED(pHdr->hrEvent)) {
            m_owner->OnVideoPlayerError(pHdr->eEventType, pHdr->hrEvent);
            return;
        }
        switch (pHdr->eEventType) {
            case MFP_EVENT_TYPE_MEDIAITEM_CREATED:
                m_owner->OnVideoItemCreated(MFP_GET_MEDIAITEM_CREATED_EVENT(pHdr));
                break;
            case MFP_EVENT_TYPE_MEDIAITEM_SET:
                m_owner->OnVideoItemReady();
                break;
            case MFP_EVENT_TYPE_PLAYBACK_ENDED:
                m_owner->OnVideoPlaybackEnded();
                break;
            default:
                break;
        }
    }

private:
    SortPhotosPanel*   m_owner;
    std::atomic<ULONG> m_ref{1};
};
#endif // __WXMSW__

// ─────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────

wxBitmap SortPhotosPanel::LoadKeycap(const wxString& filename, int size) const
{
    wxFileName exe(wxStandardPaths::Get().GetExecutablePath());
    exe.Normalize();
    exe.SetFullName("");
    exe.RemoveLastDir();
    exe.RemoveLastDir();
    wxString path = exe.GetFullPath() + "assets/single-keys-blank/200dpi/" + filename;
    if (!wxFileExists(path)) return wxNullBitmap;
    wxImage img(path, wxBITMAP_TYPE_PNG);
    if (!img.IsOk()) return wxNullBitmap;
    img = img.Scale(size, size, wxIMAGE_QUALITY_HIGH);
    return wxBitmap(img);
}

// ─────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────

SortPhotosPanel::SortPhotosPanel(wxWindow* parent)
    : wxPanel(parent)
{
    m_posTimer = new wxTimer(this, ID_VIDEO_POS_TIMER);
    Bind(wxEVT_TIMER, &SortPhotosPanel::OnPosTimer, this, ID_VIDEO_POS_TIMER);
    BuildSortingUI();
    Bind(wxEVT_SIZE, &SortPhotosPanel::OnSize, this);
    wxGetTopLevelParent(this)->Bind(wxEVT_CHAR_HOOK, &SortPhotosPanel::OnKeyDown, this);
}

void SortPhotosPanel::BuildSortingUI()
{
    m_imageBitmap = new wxStaticBitmap(this, wxID_ANY, wxNullBitmap);

    // Video render target + controls (hidden until a video loads)
    m_videoPanel = new wxPanel(this, wxID_ANY);
    m_videoPanel->SetBackgroundColour(*wxBLACK);
    m_videoPanel->Hide();
    m_videoPanel->Bind(wxEVT_PAINT, &SortPhotosPanel::OnVideoPanelPaint, this);
    m_videoPanel->Bind(wxEVT_SIZE,  &SortPhotosPanel::OnVideoPanelSize,  this);

    m_playPauseBtn = new wxButton(this, wxID_ANY, "Play");
    m_seekSlider   = new wxSlider(this, wxID_ANY, 0, 0, 1000);
    m_posLabel     = new wxStaticText(this, wxID_ANY, "0:00 / 0:00",
                                      wxDefaultPosition, wxSize(90, -1));
    m_playPauseBtn->Hide();
    m_seekSlider->Hide();
    m_posLabel->Hide();

    m_playPauseBtn->Bind(wxEVT_BUTTON, &SortPhotosPanel::OnPlayPause, this);
    m_seekSlider  ->Bind(wxEVT_SCROLL_THUMBTRACK,   &SortPhotosPanel::OnSeekTrack,   this);
    m_seekSlider  ->Bind(wxEVT_SCROLL_THUMBRELEASE, &SortPhotosPanel::OnSeekRelease, this);
    m_seekSlider  ->Bind(wxEVT_SCROLL_CHANGED,      &SortPhotosPanel::OnSeekChanged, this);
    m_imageNameLabel = new wxStaticText(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER | wxST_ELLIPSIZE_END);
    m_progressLabel  = new wxStaticText(this, wxID_ANY, "0/0", wxDefaultPosition, wxSize(60, -1), wxALIGN_RIGHT);
    m_progressBar    = new wxGauge(this, wxID_ANY, 1, wxDefaultPosition, wxDefaultSize, wxGA_HORIZONTAL | wxGA_SMOOTH);

    m_folder1Btn = new wxButton(this, ID_SORT_FOLDER1, "Folder 1");
    m_folder2Btn = new wxButton(this, ID_SORT_FOLDER2, "Folder 2");
    m_saveBtn    = new wxButton(this, ID_SORT_SAVE,    "Save");
    m_deleteBtn  = new wxButton(this, ID_SORT_DELETE,  "Delete");
    m_undoBtn    = new wxButton(this, ID_SORT_UNDO,    "Undo");

    wxBitmap icLeft  = LoadKeycap("cursor-left.png");
    wxBitmap icRight = LoadKeycap("cursor-right.png");
    wxBitmap icUp    = LoadKeycap("cursor-up.png");
    wxBitmap icDown  = LoadKeycap("cursor-down.png");
    wxBitmap icZ     = LoadKeycap("z.png");

    if (icLeft.IsOk())  { m_folder1Btn->SetBitmap(icLeft);  m_folder1Btn->SetBitmapPosition(wxLEFT);  }
    if (icRight.IsOk()) { m_folder2Btn->SetBitmap(icRight); m_folder2Btn->SetBitmapPosition(wxRIGHT); }
    if (icUp.IsOk())    { m_saveBtn->SetBitmap(icUp);       m_saveBtn->SetBitmapPosition(wxTOP);      }
    if (icDown.IsOk())  { m_deleteBtn->SetBitmap(icDown);   m_deleteBtn->SetBitmapPosition(wxBOTTOM); }
    if (icZ.IsOk())     { m_undoBtn->SetBitmap(icZ);        m_undoBtn->SetBitmapPosition(wxLEFT);     }


    wxFlexGridSizer* grid = new wxFlexGridSizer(3, 3, 0, 0);
    grid->AddGrowableRow(1);
    grid->AddGrowableCol(1);

    grid->Add(m_undoBtn,    0, wxALL | wxALIGN_CENTER_VERTICAL, 8);
    grid->Add(m_saveBtn,    0, wxALL | wxALIGN_CENTER, 8);
    grid->Add(0, 0);

    wxBoxSizer* imageCell = new wxBoxSizer(wxVERTICAL);
    imageCell->Add(m_imageNameLabel, 0, wxALIGN_CENTER | wxBOTTOM, 4);
    imageCell->Add(m_imageBitmap,    0, wxALIGN_CENTER);
    imageCell->Add(m_videoPanel,     1, wxEXPAND);

    wxBoxSizer* videoCtrlRow = new wxBoxSizer(wxHORIZONTAL);
    videoCtrlRow->Add(m_playPauseBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    videoCtrlRow->Add(m_seekSlider,   1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    videoCtrlRow->Add(m_posLabel,     0, wxALIGN_CENTER_VERTICAL);
    imageCell->Add(videoCtrlRow, 0, wxEXPAND | wxTOP, 4);

    grid->Add(m_folder1Btn, 0, wxALL | wxALIGN_CENTER_VERTICAL, 8);
    grid->Add(imageCell,    1, wxEXPAND, 10);
    grid->Add(m_folder2Btn, 0, wxALL | wxALIGN_CENTER_VERTICAL, 8);

    grid->Add(0, 0);
    grid->Add(m_deleteBtn,  0, wxALL | wxALIGN_CENTER, 8);
    grid->Add(0, 0);

    wxBoxSizer* progressRow = new wxBoxSizer(wxHORIZONTAL);
    progressRow->Add(m_progressLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    progressRow->Add(m_progressBar,   1, wxALIGN_CENTER_VERTICAL);

    wxBoxSizer* vSizer = new wxBoxSizer(wxVERTICAL);
    vSizer->Add(progressRow, 0, wxEXPAND | wxALL, 8);
    vSizer->Add(grid, 1, wxEXPAND | wxALL, 5);
    SetSizer(vSizer);

    m_folder1Btn->Bind(wxEVT_BUTTON, &SortPhotosPanel::OnFolder1, this);
    m_folder2Btn->Bind(wxEVT_BUTTON, &SortPhotosPanel::OnFolder2, this);
    m_saveBtn   ->Bind(wxEVT_BUTTON, &SortPhotosPanel::OnSave,    this);
    m_deleteBtn ->Bind(wxEVT_BUTTON, &SortPhotosPanel::OnDelete,  this);
    m_undoBtn   ->Bind(wxEVT_BUTTON, &SortPhotosPanel::OnUndo,    this);

    SetButtonsEnabled(false);
}

SortPhotosPanel::~SortPhotosPanel()
{
    ShutdownPlayer(); // must happen before HWND is destroyed
    delete m_posTimer;
    m_posTimer = nullptr;
    wxWindow* tlw = wxGetTopLevelParent(this);
    if (tlw) tlw->Unbind(wxEVT_CHAR_HOOK, &SortPhotosPanel::OnKeyDown, this);
}

// ─────────────────────────────────────────────
// Session entry / reset
// ─────────────────────────────────────────────

void SortPhotosPanel::RefreshData()
{
    auto* frame = dynamic_cast<PhotoQuickSorterFrame*>(GetParent());
    if (!frame) return;

    LOG_DEBUG("RefreshData - Base: %s | Folder1: %s | Folder2: %s",
              frame->folderLocations.baseFolder,
              frame->folderLocations.folder1,
              frame->folderLocations.folder2);

    // If the sorting UI was destroyed by a previous review/done state, rebuild it
    if (!m_imageBitmap) {
        Freeze();
        DestroyChildren();
        BuildSortingUI();
        Layout();
        Thaw();
    }

    // Reset session state
    m_currentIndex    = 0;
    m_currentIsVideo  = false;
    m_inReview        = false;
    m_folder1Shown    = false;
    m_folder2Shown    = false;
    m_deleteShown     = false;
    m_folder1Confirmed = false;
    m_folder2Confirmed = false;
    m_deleteConfirmed  = false;
    m_videoReady      = false;
    m_videoOpening    = false;
    m_seekInProgress  = false;
    m_updatingSeekUi  = false;
    m_actionHistory.clear();
    m_folder1List.clear();
    m_folder2List.clear();
    m_deleteList.clear();

    StopActiveVideo(false);

    // Crash recovery: check for leftover txt files from a previous session
    CheckForPendingSession();

    // If recovery populated lists and there's nothing left to sort, go straight to review
    bool hasPending = !m_folder1List.empty() || !m_folder2List.empty() || !m_deleteList.empty();
    if (hasPending && frame->imageRepo.GetCount() == 0) {
        m_inReview = true;
        AdvanceReviewStep();
        return;
    }

    LoadCurrentImage();
}

// ─────────────────────────────────────────────
// Crash recovery
// ─────────────────────────────────────────────

void SortPhotosPanel::CheckForPendingSession()
{
    auto* frame = dynamic_cast<PhotoQuickSorterFrame*>(GetParent());
    if (!frame) return;

    const wxString base = frame->folderLocations.baseFolder;
    wxString f1txt  = base + wxFILE_SEP_PATH + "_pending_folder1.txt";
    wxString f2txt  = base + wxFILE_SEP_PATH + "_pending_folder2.txt";
    wxString deltxt = base + wxFILE_SEP_PATH + "_pending_deletes.txt";

    auto loadTxt = [](const wxString& path, std::vector<wxString>& out) {
        if (!wxFileExists(path)) return;
        wxTextFile f;
        if (!f.Open(path)) return;
        for (size_t i = 0; i < f.GetLineCount(); ++i) {
            wxString line = f.GetLine(i).Trim(true).Trim(false);
            if (!line.IsEmpty() && wxFileExists(line))
                out.push_back(line);
        }
        f.Close();
    };

    std::vector<wxString> tmp1, tmp2, tmpd;
    loadTxt(f1txt,  tmp1);
    loadTxt(f2txt,  tmp2);
    loadTxt(deltxt, tmpd);

    size_t total = tmp1.size() + tmp2.size() + tmpd.size();
    if (total == 0) return;

    int answer = wxMessageBox(
        wxString::Format(
            "A previous session left %zu unexecuted action(s) for this folder.\n"
            "Resume the review?",
            total),
        "Resume Previous Session",
        wxYES_NO | wxICON_QUESTION, this);

    if (answer == wxYES) {
        m_folder1List = std::move(tmp1);
        m_folder2List = std::move(tmp2);
        m_deleteList  = std::move(tmpd);
        // Rebuild action history (interleaving lost, but only the lists matter for review)
        for (const auto& p : m_folder1List) m_actionHistory.push_back({p, SortAction::MoveToFolder1});
        for (const auto& p : m_folder2List) m_actionHistory.push_back({p, SortAction::MoveToFolder2});
        for (const auto& p : m_deleteList)  m_actionHistory.push_back({p, SortAction::Delete});

        // Remove already-acted-upon images from imageRepo so sorting resumes
        // from the first unprocessed image (m_currentIndex stays 0 after RefreshData reset).
        // Collect indices descending so RemoveAt doesn't invalidate later indices.
        std::vector<size_t> toRemove;
        for (const auto& action : m_actionHistory) {
            ptrdiff_t idx = frame->imageRepo.FindByPath(action.imagePath);
            if (idx >= 0) toRemove.push_back(static_cast<size_t>(idx));
        }
        std::sort(toRemove.begin(), toRemove.end(), std::greater<size_t>());
        toRemove.erase(std::unique(toRemove.begin(), toRemove.end()), toRemove.end());
        for (size_t idx : toRemove)
            frame->imageRepo.RemoveAt(idx);

        LOG_INFO("Resumed previous session: %zu F1, %zu F2, %zu delete, %zu media file(s) remaining",
                 m_folder1List.size(), m_folder2List.size(), m_deleteList.size(),
                 frame->imageRepo.GetCount());
    } else {
        if (wxFileExists(f1txt))  wxRemoveFile(f1txt);
        if (wxFileExists(f2txt))  wxRemoveFile(f2txt);
        if (wxFileExists(deltxt)) wxRemoveFile(deltxt);
        LOG_INFO("Discarded previous session pending actions");
    }
}

// ─────────────────────────────────────────────
// Image display
// ─────────────────────────────────────────────

void SortPhotosPanel::LoadCurrentImage()
{
    auto* frame = dynamic_cast<PhotoQuickSorterFrame*>(GetParent());
    if (!frame) return;

    const ImageInfo* info = frame->imageRepo.GetAt(m_currentIndex);
    if (!info) {
        OnAllImagesActedUpon();
        return;
    }

    size_t total = frame->imageRepo.GetCount();
    m_progressBar->SetRange((int)total);
    m_progressBar->SetValue((int)m_currentIndex);
    m_progressLabel->SetLabel(wxString::Format("%zu/%zu", m_currentIndex, total));
    m_imageNameLabel->SetLabel(wxFileName(info->path).GetFullName());

    m_currentIsVideo = MediaUtils::IsVideoFile(info->path);
    StopActiveVideo(false);

    if (m_currentIsVideo) {
        m_imageBitmap->Hide();
        m_videoPanel->Show();
        ResetVideoControls("Loading...", true, false, false, "Loading");
        Layout();
        m_videoPanel->Refresh();
        m_videoPanel->Update();
        OpenCurrentVideo(info->path);
    } else {
        m_videoPanel->Hide();
        ResetVideoControls("0:00 / 0:00", false, false, false, "Play");
        m_videoPanel->Refresh();
        m_imageBitmap->Show();

        wxImage img(info->path, wxBITMAP_TYPE_ANY);
        if (img.IsOk()) {
            Layout();
            wxSize panel = GetClientSize();
            int btnW = m_folder1Btn->GetSize().x + 16;
            int maxW = panel.x - 2 * btnW;
            int maxH = (int)(panel.y * 0.75);
            wxSize available(std::max(maxW, 1), std::max(maxH, 1));
            if (available.x > 1 && available.y > 1) {
                double scaleX = (double)available.x / img.GetWidth();
                double scaleY = (double)available.y / img.GetHeight();
                double scale  = std::min(scaleX, scaleY);
                if (scale < 1.0)
                    img = img.Scale((int)(img.GetWidth() * scale),
                                    (int)(img.GetHeight() * scale),
                                    wxIMAGE_QUALITY_HIGH);
            }
            m_imageBitmap->SetBitmap(wxBitmap(img));
        } else {
            m_imageBitmap->SetBitmap(wxNullBitmap);
            LOG_WARN("Could not load image: %s", info->path);
        }
    }

    SetButtonsEnabled(true);
    Layout();
}

// ─────────────────────────────────────────────
// Action recording
// ─────────────────────────────────────────────

void SortPhotosPanel::RecordAction(SortAction type)
{
    auto* frame = dynamic_cast<PhotoQuickSorterFrame*>(GetParent());
    if (!frame) return;

    const ImageInfo* info = frame->imageRepo.GetAt(m_currentIndex);
    if (!info) return;

    m_actionHistory.push_back({info->path, type});

    switch (type) {
        case SortAction::MoveToFolder1:
            m_folder1List.push_back(info->path);
            PersistList(m_folder1List, "_pending_folder1.txt");
            break;
        case SortAction::MoveToFolder2:
            m_folder2List.push_back(info->path);
            PersistList(m_folder2List, "_pending_folder2.txt");
            break;
        case SortAction::Delete:
            m_deleteList.push_back(info->path);
            PersistList(m_deleteList, "_pending_deletes.txt");
            break;
        case SortAction::Save:
            break; // no persistence needed
    }

    LOG_DEBUG("RecordAction: %s -> action %d (history size: %zu)",
              info->path, (int)type, m_actionHistory.size());

    ++m_currentIndex;
    LoadCurrentImage();
}

void SortPhotosPanel::PersistList(const std::vector<wxString>& list, const wxString& filename)
{
    auto* frame = dynamic_cast<PhotoQuickSorterFrame*>(GetParent());
    if (!frame) return;

    wxString path = frame->folderLocations.baseFolder + wxFILE_SEP_PATH + filename;

    if (list.empty()) {
        if (wxFileExists(path)) wxRemoveFile(path);
        return;
    }

    wxTextFile f;
    if (wxFileExists(path)) {
        if (!f.Open(path)) return;
        f.Clear();
    } else {
        if (!f.Create(path)) return;
    }
    for (const auto& entry : list) f.AddLine(entry);
    f.Write();
    f.Close();
}

// ─────────────────────────────────────────────
// Undo
// ─────────────────────────────────────────────

void SortPhotosPanel::OnUndo(wxCommandEvent& WXUNUSED(evt))
{
    if (m_actionHistory.empty()) return;

    PendingAction undone = m_actionHistory.back();
    m_actionHistory.pop_back();
    --m_currentIndex;

    auto removeFrom = [&](std::vector<wxString>& list) {
        auto it = std::find(list.begin(), list.end(), undone.imagePath);
        if (it != list.end()) list.erase(it);
    };

    switch (undone.type) {
        case SortAction::MoveToFolder1:
            removeFrom(m_folder1List);
            PersistList(m_folder1List, "_pending_folder1.txt");
            break;
        case SortAction::MoveToFolder2:
            removeFrom(m_folder2List);
            PersistList(m_folder2List, "_pending_folder2.txt");
            break;
        case SortAction::Delete:
            removeFrom(m_deleteList);
            PersistList(m_deleteList, "_pending_deletes.txt");
            break;
        case SortAction::Save:
            break;
    }

    LOG_DEBUG("Undo: restored %s (history size now: %zu)", undone.imagePath, m_actionHistory.size());
    LoadCurrentImage();
}

// ─────────────────────────────────────────────
// End-of-session review
// ─────────────────────────────────────────────

void SortPhotosPanel::OnAllImagesActedUpon()
{
    bool hasPending = !m_folder1List.empty() || !m_folder2List.empty() || !m_deleteList.empty();
    if (!hasPending) {
        ShowDoneState();
        return;
    }

    m_inReview = true;

    // Destroy sorting UI and null all sorting-UI pointers
    ShutdownPlayer(); // must happen before DestroyChildren destroys m_videoPanel's HWND
    DestroyChildren();
    m_imageBitmap    = nullptr;
    m_videoPanel     = nullptr;
    m_playPauseBtn   = nullptr;
    m_seekSlider     = nullptr;
    m_posLabel       = nullptr;
    m_progressBar    = nullptr;
    m_progressLabel  = nullptr;
    m_imageNameLabel = nullptr;
    m_folder1Btn  = nullptr;
    m_folder2Btn  = nullptr;
    m_saveBtn     = nullptr;
    m_deleteBtn   = nullptr;
    m_undoBtn     = nullptr;

    AdvanceReviewStep();
}

void SortPhotosPanel::AdvanceReviewStep()
{
    // Try each step in order; skip if already shown or list is empty
    if (!m_folder1Shown && !m_folder1List.empty()) {
        m_folder1Shown      = true;
        m_currentReviewStep = ReviewStep::Folder1;
        ShowReviewStep();
        return;
    }
    if (!m_folder2Shown && !m_folder2List.empty()) {
        m_folder2Shown      = true;
        m_currentReviewStep = ReviewStep::Folder2;
        ShowReviewStep();
        return;
    }
    if (!m_deleteShown && !m_deleteList.empty()) {
        m_deleteShown       = true;
        m_currentReviewStep = ReviewStep::Delete;
        ShowReviewStep();
        return;
    }

    // All steps shown — execute and return to main menu
    ExecuteConfirmedActions();
    auto* frame = dynamic_cast<PhotoQuickSorterFrame*>(GetParent());
    if (frame) frame->ShowMainMenuPanel();
}

void SortPhotosPanel::ShowReviewStep()
{
    auto* frame = dynamic_cast<PhotoQuickSorterFrame*>(GetParent());
    if (!frame) return;

    const std::vector<wxString>* list = nullptr;
    wxString titleText, confirmLabel, skipLabel;

    switch (m_currentReviewStep) {
        case ReviewStep::Folder1:
            list         = &m_folder1List;
            titleText    = wxString::Format("-> Folder 1 - %zu media file(s) to move", m_folder1List.size());
            confirmLabel = "Confirm Move";
            skipLabel    = "Skip (keep in base folder)";
            break;
        case ReviewStep::Folder2:
            list         = &m_folder2List;
            titleText    = wxString::Format("-> Folder 2 - %zu media file(s) to move", m_folder2List.size());
            confirmLabel = "Confirm Move";
            skipLabel    = "Skip (keep in base folder)";
            break;
        case ReviewStep::Delete:
            list         = &m_deleteList;
            titleText    = wxString::Format("Delete - %zu media file(s)", m_deleteList.size());
            confirmLabel = "Confirm Delete";
            skipLabel    = "Keep All";
            break;
    }

    Freeze();
    DestroyChildren(); // safe on subsequent steps too

    wxBoxSizer* vSizer = new wxBoxSizer(wxVERTICAL);

    wxStaticText* title = new wxStaticText(this, wxID_ANY, titleText,
                                           wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER);
    vSizer->Add(title, 0, wxALIGN_CENTER | wxALL, 8);

    // Scrollable thumbnail grid (same component as MainMenu preview)
    ThumbnailGrid* thumbGrid = new ThumbnailGrid(this);
    thumbGrid->SetMedia(std::vector<wxString>(list->begin(), list->end()));
    vSizer->Add(thumbGrid, 1, wxEXPAND | wxALL, 6);

    // Buttons
    wxButton* confirmBtn = new wxButton(this, ID_SORT_STEP_CONFIRM, confirmLabel);
    wxButton* skipBtn    = new wxButton(this, ID_SORT_STEP_SKIP,    skipLabel);


    wxBitmap icEnter = LoadKeycap("enter.png");
    if (icEnter.IsOk()) { confirmBtn->SetBitmap(icEnter); confirmBtn->SetBitmapPosition(wxLEFT); }
    confirmBtn->SetDefault();

    wxBitmap icEsc = LoadKeycap("esc.png");
    if (icEsc.IsOk()) { skipBtn->SetBitmap(icEsc); skipBtn->SetBitmapPosition(wxLEFT); }

    if (m_currentReviewStep == ReviewStep::Delete) {
        confirmBtn->SetBackgroundColour(wxColour(200, 60, 60));
        confirmBtn->SetForegroundColour(*wxWHITE);
    }

    wxBoxSizer* btnRow = new wxBoxSizer(wxHORIZONTAL);
    btnRow->Add(confirmBtn, 0, wxALL, 8);
    btnRow->Add(skipBtn,    0, wxALL, 8);
    vSizer->Add(btnRow, 0, wxALIGN_CENTER | wxBOTTOM, 10);

    confirmBtn->Bind(wxEVT_BUTTON, &SortPhotosPanel::OnStepConfirm, this);
    skipBtn   ->Bind(wxEVT_BUTTON, &SortPhotosPanel::OnStepSkip,    this);

    SetSizer(vSizer);
    Thaw();
    Layout();
    confirmBtn->SetFocus();
}

void SortPhotosPanel::OnStepConfirm(wxCommandEvent& WXUNUSED(evt))
{
    switch (m_currentReviewStep) {
        case ReviewStep::Folder1: m_folder1Confirmed = true; break;
        case ReviewStep::Folder2: m_folder2Confirmed = true; break;
        case ReviewStep::Delete:  m_deleteConfirmed  = true; break;
    }
    AdvanceReviewStep();
}

void SortPhotosPanel::OnStepSkip(wxCommandEvent& WXUNUSED(evt))
{
    // confirmed flag stays false — AdvanceReviewStep will skip execution for this category
    AdvanceReviewStep();
}

// ─────────────────────────────────────────────
// Execution
// ─────────────────────────────────────────────

void SortPhotosPanel::ExecuteFileMove(const wxString& srcPath, const wxString& destFolder)
{
    if (!wxFileExists(srcPath)) {
        LOG_WARN("ExecuteFileMove: source no longer exists: %s", srcPath);
        return;
    }

    if (!wxDir::Exists(destFolder)) {
        if (!wxFileName::Mkdir(destFolder, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL)) {
            LOG_ERROR("ExecuteFileMove: could not create folder: %s", destFolder);
            return;
        }
    }

    wxFileName srcFn(srcPath);
    wxString dest = destFolder + wxFILE_SEP_PATH + srcFn.GetFullName();

    int suffix = 1;
    while (wxFileExists(dest)) {
        dest = destFolder + wxFILE_SEP_PATH
             + srcFn.GetName() + wxString::Format("_%d", suffix++)
             + "." + srcFn.GetExt();
    }

    if (!wxCopyFile(srcPath, dest)) {
        LOG_ERROR("ExecuteFileMove: copy failed: %s -> %s", srcPath, dest);
        return;
    }
    if (!wxRemoveFile(srcPath))
        LOG_WARN("ExecuteFileMove: copy ok but source not deleted: %s", srcPath);

    LOG_DEBUG("ExecuteFileMove: moved %s -> %s", srcPath, dest);
}

void SortPhotosPanel::ExecuteConfirmedActions()
{
    auto* frame = dynamic_cast<PhotoQuickSorterFrame*>(GetParent());
    if (!frame) return;

    for (const auto& action : m_actionHistory) {
        switch (action.type) {
            case SortAction::MoveToFolder1:
                if (m_folder1Confirmed)
                    ExecuteFileMove(action.imagePath, frame->folderLocations.folder1);
                break;
            case SortAction::MoveToFolder2:
                if (m_folder2Confirmed)
                    ExecuteFileMove(action.imagePath, frame->folderLocations.folder2);
                break;
            case SortAction::Delete:
                if (m_deleteConfirmed && wxFileExists(action.imagePath))
                    wxRemoveFile(action.imagePath);
                break;
            case SortAction::Save:
                break;
        }
    }

    // Clean up all persistence files
    const wxString base = frame->folderLocations.baseFolder;
    auto tryRemove = [&](const wxString& name) {
        wxString p = base + wxFILE_SEP_PATH + name;
        if (wxFileExists(p)) wxRemoveFile(p);
    };
    tryRemove("_pending_folder1.txt");
    tryRemove("_pending_folder2.txt");
    tryRemove("_pending_deletes.txt");

    m_folder1List.clear();
    m_folder2List.clear();
    m_deleteList.clear();
    m_actionHistory.clear();

    LOG_INFO("ExecuteConfirmedActions complete");
}

// ─────────────────────────────────────────────
// Button handlers (sorting phase)
// ─────────────────────────────────────────────

void SortPhotosPanel::OnFolder1(wxCommandEvent& WXUNUSED(evt))
{
    auto* frame = dynamic_cast<PhotoQuickSorterFrame*>(GetParent());
    if (!frame) return;
    if (frame->folderLocations.folder1.IsEmpty()) {
        wxMessageBox("Folder 1 path is not set.", "No Folder Set", wxOK | wxICON_WARNING, this);
        return;
    }
    RecordAction(SortAction::MoveToFolder1);
}

void SortPhotosPanel::OnFolder2(wxCommandEvent& WXUNUSED(evt))
{
    auto* frame = dynamic_cast<PhotoQuickSorterFrame*>(GetParent());
    if (!frame) return;
    if (frame->folderLocations.folder2.IsEmpty()) {
        wxMessageBox("Folder 2 path is not set.", "No Folder Set", wxOK | wxICON_WARNING, this);
        return;
    }
    RecordAction(SortAction::MoveToFolder2);
}

void SortPhotosPanel::OnSave(wxCommandEvent& WXUNUSED(evt))
{
    RecordAction(SortAction::Save);
}

void SortPhotosPanel::OnDelete(wxCommandEvent& WXUNUSED(evt))
{
    RecordAction(SortAction::Delete);
}

// ─────────────────────────────────────────────
// Utility
// ─────────────────────────────────────────────

void SortPhotosPanel::SetButtonsEnabled(bool enabled)
{
    if (m_folder1Btn) m_folder1Btn->Enable(enabled);
    if (m_folder2Btn) m_folder2Btn->Enable(enabled);
    if (m_saveBtn)    m_saveBtn   ->Enable(enabled);
    if (m_deleteBtn)  m_deleteBtn ->Enable(enabled);
    if (m_undoBtn)    m_undoBtn   ->Enable(enabled);
}

// ─────────────────────────────────────────────
// Video helpers
// ─────────────────────────────────────────────

bool SortPhotosPanel::EnsurePlayerCreated()
{
#ifdef __WXMSW__
    if (m_player) return true;
    if (!m_videoPanel || !m_videoPanel->GetHWND()) {
        LOG_ERROR("EnsurePlayerCreated: video panel HWND is not ready");
        return false;
    }

    auto* cb = new MFPlayCallback(this); // ref=1
    HRESULT hr = MFPCreateMediaPlayer(
        nullptr,
        FALSE,
        MFP_OPTION_NONE,
        cb,
        (HWND)m_videoPanel->GetHWND(),
        &m_player);
    cb->Release(); // player keeps its own ref

    if (FAILED(hr)) {
        m_player = nullptr;
        LOG_ERROR("MFPCreateMediaPlayer failed: hr=0x%08X", (unsigned)hr);
        return false;
    }

    return true;
#else
    return false;
#endif
}

void SortPhotosPanel::OpenCurrentVideo(const wxString& path)
{
#ifdef __WXMSW__
    if (!m_currentIsVideo || m_inReview) return;

    if (!EnsurePlayerCreated()) {
        ShowVideoErrorState("Unavailable");
        return;
    }

    m_videoOpening = true;
    m_videoReady   = false;
    m_seekInProgress = false;
    m_videoDuration = 0;
    m_pendingSetToken = 0;
    const std::uint64_t token = ++m_videoRequestToken;

    HRESULT hr = m_player->CreateMediaItemFromURL(
        path.wc_str(),
        FALSE, // async open
        static_cast<DWORD_PTR>(token),
        nullptr);
    if (FAILED(hr)) {
        LOG_ERROR("CreateMediaItemFromURL failed for '%s': hr=0x%08X",
                  path, (unsigned)hr);
        ShowVideoErrorState("Unavailable");
        return;
    }
#else
    wxUnusedVar(path);
    ShowVideoErrorState("Unsupported");
#endif
}

void SortPhotosPanel::StopActiveVideo(bool releasePlayer)
{
#ifdef __WXMSW__
    ++m_videoRequestToken; // invalidate any in-flight async callbacks
    m_pendingSetToken = 0;
#endif
    m_videoOpening   = false;
    m_videoReady     = false;
    m_seekInProgress = false;
    m_updatingSeekUi = false;
    m_videoDuration  = 0;

#ifdef __WXMSW__
    if (m_posTimer && m_posTimer->IsRunning())
        m_posTimer->Stop();

    if (m_player) {
        m_player->Stop();
        if (releasePlayer) {
            m_player->Shutdown();
            m_player->Release();
            m_player = nullptr;
        }
    }
#endif
}

void SortPhotosPanel::ShutdownPlayer()
{
    StopActiveVideo(true);
}

void SortPhotosPanel::ResetVideoControls(const wxString& statusText,
                                         bool showControls,
                                         bool enablePlay,
                                         bool enableSeek,
                                         const wxString& playLabel)
{
    if (m_playPauseBtn) {
        if (showControls) m_playPauseBtn->Show();
        else m_playPauseBtn->Hide();
        m_playPauseBtn->SetLabel(playLabel);
        m_playPauseBtn->Enable(showControls && enablePlay);
    }

    if (m_seekSlider) {
        if (showControls) m_seekSlider->Show();
        else m_seekSlider->Hide();
        m_updatingSeekUi = true;
        m_seekSlider->SetValue(0);
        m_updatingSeekUi = false;
        m_seekSlider->Enable(showControls && enableSeek);
    }

    if (m_posLabel) {
        if (showControls) m_posLabel->Show();
        else m_posLabel->Hide();
        m_posLabel->SetLabel(statusText);
    }
}

void SortPhotosPanel::ShowVideoErrorState(const wxString& statusText)
{
    m_videoOpening = false;
    m_videoReady   = false;
    m_seekInProgress = false;
    m_pendingSetToken = 0;
    if (m_posTimer && m_posTimer->IsRunning())
        m_posTimer->Stop();

    ResetVideoControls(statusText, true, false, false, "Play");
    if (m_videoPanel) {
        m_videoPanel->Show();
        m_videoPanel->Refresh();
    }
    Layout();
}

void SortPhotosPanel::UpdateVideoSurface()
{
#ifdef __WXMSW__
    if (!m_player || !m_videoPanel || !m_currentIsVideo || !m_videoReady || m_inReview)
        return;
    m_player->UpdateVideo();
#endif
}

#ifdef __WXMSW__
void SortPhotosPanel::OnVideoItemCreated(MFP_MEDIAITEM_CREATED_EVENT* evt)
{
    if (!evt || !evt->pMediaItem || !m_player || !m_currentIsVideo || m_inReview)
        return;

    DWORD_PTR userData = 0;
    HRESULT hr = evt->pMediaItem->GetUserData(&userData);
    if (FAILED(hr)) {
        LOG_ERROR("GetUserData failed: hr=0x%08X", (unsigned)hr);
        ShowVideoErrorState("Unavailable");
        return;
    }

    const std::uint64_t token = static_cast<std::uint64_t>(userData);
    if (!m_videoOpening || token != m_videoRequestToken)
        return;

    BOOL hasVideo = FALSE;
    BOOL isSelected = FALSE;
    hr = evt->pMediaItem->HasVideo(&hasVideo, &isSelected);
    if (FAILED(hr)) {
        LOG_ERROR("HasVideo failed: hr=0x%08X", (unsigned)hr);
        ShowVideoErrorState("Unavailable");
        return;
    }

    if (!(hasVideo && isSelected)) {
        LOG_WARN("Selected media item does not expose a playable video stream");
        ShowVideoErrorState("No video");
        return;
    }

    m_pendingSetToken = token;
    hr = m_player->SetMediaItem(evt->pMediaItem);
    if (FAILED(hr)) {
        LOG_ERROR("SetMediaItem failed: hr=0x%08X", (unsigned)hr);
        ShowVideoErrorState("Unavailable");
    }
}

void SortPhotosPanel::OnVideoPlayerError(MFP_EVENT_TYPE eventType, HRESULT hr)
{
    if (!m_currentIsVideo || !m_videoPanel || m_inReview)
        return;

    LOG_ERROR("MFPlay event failed: type=%d hr=0x%08X", (int)eventType, (unsigned)hr);

    if (m_videoOpening || m_videoReady)
        ShowVideoErrorState("Unavailable");
}
#endif

void SortPhotosPanel::OnVideoItemReady()
{
#ifdef __WXMSW__
    if (!m_player || !m_currentIsVideo || !m_videoPanel || m_inReview)
        return;
    if (!m_videoOpening || m_pendingSetToken == 0 || m_pendingSetToken != m_videoRequestToken)
        return;

    m_videoOpening = false;
    m_videoReady   = true;
    m_seekInProgress = false;
    m_videoDuration = 0;

    PROPVARIANT dur = {};
    if (SUCCEEDED(m_player->GetDuration(MFP_POSITIONTYPE_100NS, &dur)))
        m_videoDuration = dur.hVal.QuadPart;
    PropVariantClear(&dur);

    long totalSec = (long)(m_videoDuration / 10'000'000LL);
    ResetVideoControls(
        wxString::Format("0:00 / %ld:%02ld", totalSec / 60, totalSec % 60),
        true,
        true,
        m_videoDuration > 0,
        "Play");

    HRESULT hr = m_player->Play();
    if (FAILED(hr)) {
        LOG_ERROR("IMFPMediaPlayer::Play failed: hr=0x%08X", (unsigned)hr);
        ShowVideoErrorState("Unavailable");
        return;
    }

    if (m_posTimer)
        m_posTimer->Start(250);
    if (m_playPauseBtn)
        m_playPauseBtn->SetLabel("Pause");

    UpdateVideoSurface();
    m_videoPanel->Refresh();
#endif
}

void SortPhotosPanel::OnVideoPlaybackEnded()
{
    if (!m_currentIsVideo || !m_videoReady) return;
    if (m_posTimer) m_posTimer->Stop();
    if (m_playPauseBtn) m_playPauseBtn->SetLabel("Play");
    if (m_seekSlider && m_videoDuration > 0) {
        m_updatingSeekUi = true;
        m_seekSlider->SetValue(1000);
        m_updatingSeekUi = false;
    }
    UpdateVideoSurface();
}

void SortPhotosPanel::OnPosTimer(wxTimerEvent&)
{
#ifdef __WXMSW__
    if (!m_player || !m_videoReady || m_videoDuration <= 0 || m_seekInProgress) return;
    PROPVARIANT pos = {};
    if (SUCCEEDED(m_player->GetPosition(MFP_POSITIONTYPE_100NS, &pos))) {
        long long p = pos.hVal.QuadPart;
        PropVariantClear(&pos);
        if (m_seekSlider) {
            m_updatingSeekUi = true;
            m_seekSlider->SetValue((int)(p * 1000 / m_videoDuration));
            m_updatingSeekUi = false;
        }
        long cur = (long)(p / 10'000'000LL);
        long tot = (long)(m_videoDuration / 10'000'000LL);
        if (m_posLabel) {
            m_posLabel->SetLabel(wxString::Format("%ld:%02ld / %ld:%02ld",
                cur / 60, cur % 60, tot / 60, tot % 60));
        }
    }
#endif
}

void SortPhotosPanel::OnPlayPause(wxCommandEvent&)
{
#ifdef __WXMSW__
    if (!m_player || !m_videoReady || m_videoOpening) return;
    MFP_MEDIAPLAYER_STATE state = MFP_MEDIAPLAYER_STATE_EMPTY;
    m_player->GetState(&state);
    if (state == MFP_MEDIAPLAYER_STATE_PLAYING) {
        m_player->Pause();
        if (m_posTimer) m_posTimer->Stop();
        if (m_playPauseBtn) m_playPauseBtn->SetLabel("Play");
        UpdateVideoSurface();
    } else {
        HRESULT hr = m_player->Play();
        if (FAILED(hr)) {
            LOG_ERROR("Play failed from OnPlayPause: hr=0x%08X", (unsigned)hr);
            ShowVideoErrorState("Unavailable");
            return;
        }
        if (m_posTimer) m_posTimer->Start(250);
        if (m_playPauseBtn) m_playPauseBtn->SetLabel("Pause");
        UpdateVideoSurface();
    }
#endif
}

void SortPhotosPanel::OnSeekTrack(wxScrollEvent& evt)
{
    evt.Skip();
    if (!m_player || !m_videoReady || m_videoDuration <= 0 || m_updatingSeekUi) return;
    m_seekInProgress = true;
    long long target = (long long)m_seekSlider->GetValue() * m_videoDuration / 1000;
    long cur = (long)(target / 10'000'000LL);
    long tot = (long)(m_videoDuration / 10'000'000LL);
    if (m_posLabel) {
        m_posLabel->SetLabel(wxString::Format("%ld:%02ld / %ld:%02ld",
            cur / 60, cur % 60, tot / 60, tot % 60));
    }
}

void SortPhotosPanel::OnSeekRelease(wxScrollEvent& evt)
{
    evt.Skip();
    m_seekInProgress = false;
}

void SortPhotosPanel::OnSeekChanged(wxScrollEvent& evt)
{
    evt.Skip();
#ifdef __WXMSW__
    if (!m_player || !m_videoReady || m_videoDuration <= 0 || m_updatingSeekUi) return;
    m_seekInProgress = false;
    long long target = (long long)m_seekSlider->GetValue() * m_videoDuration / 1000;
    PROPVARIANT pos = {};
    pos.vt = VT_I8;
    pos.hVal.QuadPart = target;
    HRESULT hr = m_player->SetPosition(MFP_POSITIONTYPE_100NS, &pos);
    PropVariantClear(&pos);
    if (FAILED(hr)) {
        LOG_ERROR("SetPosition failed: hr=0x%08X", (unsigned)hr);
        ShowVideoErrorState("Unavailable");
        return;
    }
    long cur = (long)(target / 10'000'000LL);
    long tot = (long)(m_videoDuration / 10'000'000LL);
    if (m_posLabel) {
        m_posLabel->SetLabel(wxString::Format("%ld:%02ld / %ld:%02ld",
            cur / 60, cur % 60, tot / 60, tot % 60));
    }
    UpdateVideoSurface();
#endif
}

void SortPhotosPanel::OnVideoPanelPaint(wxPaintEvent&)
{
    if (!m_videoPanel) return;
    wxPaintDC dc(m_videoPanel);
    if (!m_player || !m_currentIsVideo || !m_videoReady) {
        dc.SetBackground(wxBrush(*wxBLACK));
        dc.Clear();
        return;
    }
    UpdateVideoSurface();
}

void SortPhotosPanel::OnVideoPanelSize(wxSizeEvent& evt)
{
    evt.Skip();
    UpdateVideoSurface();
}

void SortPhotosPanel::ShowDoneState()
{
    m_inReview = true;
    ShutdownPlayer(); // must happen before DestroyChildren destroys m_videoPanel's HWND
    DestroyChildren();
    m_imageBitmap    = nullptr;
    m_videoPanel     = nullptr;
    m_playPauseBtn   = nullptr;
    m_seekSlider     = nullptr;
    m_posLabel       = nullptr;
    m_progressBar    = nullptr;
    m_progressLabel  = nullptr;
    m_imageNameLabel = nullptr;
    m_folder1Btn = m_folder2Btn = m_saveBtn = m_deleteBtn = m_undoBtn = nullptr;

    Freeze();
    wxBoxSizer* vSizer = new wxBoxSizer(wxVERTICAL);
    vSizer->AddStretchSpacer();

    wxStaticText* msg = new wxStaticText(this, wxID_ANY, "All Done!\nAll media were processed.",
                                          wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER);
    wxFont f = msg->GetFont();
    f.SetPointSize(f.GetPointSize() + 6);
    f.SetWeight(wxFONTWEIGHT_BOLD);
    msg->SetFont(f);
    vSizer->Add(msg, 0, wxALIGN_CENTER | wxALL, 20);

    vSizer->AddStretchSpacer();

    wxButton* backBtn = new wxButton(this, wxID_ANY, "Back to Menu");
    wxBitmap icEnter = LoadKeycap("enter.png");
    if (icEnter.IsOk()) { backBtn->SetBitmap(icEnter); backBtn->SetBitmapPosition(wxLEFT); }
    backBtn->SetDefault();
    vSizer->Add(backBtn, 0, wxALIGN_CENTER | wxBOTTOM, 30);

    backBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        auto* frame = dynamic_cast<PhotoQuickSorterFrame*>(GetParent());
        if (frame) frame->ShowMainMenuPanel();
    });

    SetSizer(vSizer);
    Thaw();
    Layout();
    backBtn->SetFocus();
}

void SortPhotosPanel::OnSize(wxSizeEvent& evt)
{
    evt.Skip();
    if (m_inReview) return;
    if (m_currentIsVideo) {
        UpdateVideoSurface();
        return;
    }
    auto* frame = dynamic_cast<PhotoQuickSorterFrame*>(GetParent());
    if (frame && frame->imageRepo.GetCount() > 0 && IsShown() && m_imageBitmap)
        LoadCurrentImage();
}

void SortPhotosPanel::OnKeyDown(wxKeyEvent& evt)
{
    if (m_inReview) {
        const int key = evt.GetKeyCode();
        if (key == WXK_ESCAPE) {
            wxCommandEvent dummy;
            OnStepSkip(dummy);
        } else if (key == WXK_RETURN || key == WXK_NUMPAD_ENTER) {
            bool isDoneState = m_folder1List.empty() && m_folder2List.empty() && m_deleteList.empty();
            if (isDoneState) {
                auto* frame = dynamic_cast<PhotoQuickSorterFrame*>(GetParent());
                if (frame) frame->ShowMainMenuPanel();
            } else {
                wxCommandEvent dummy;
                OnStepConfirm(dummy);
            }
        } else {
            evt.Skip();
        }
        return;
    }

    if (!m_folder1Btn || !m_folder1Btn->IsEnabled()) {
        evt.Skip();
        return;
    }
    wxCommandEvent dummy;
    switch (evt.GetKeyCode()) {
        case WXK_LEFT:  OnFolder1(dummy); break;
        case WXK_RIGHT: OnFolder2(dummy); break;
        case WXK_UP:    OnSave(dummy);    break;
        case WXK_DOWN:  OnDelete(dummy);  break;
        case 'Z':       OnUndo(dummy);    break;
        default:        evt.Skip();       break;
    }
}
