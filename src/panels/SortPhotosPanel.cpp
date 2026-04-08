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
#include <shobjidl.h>
#endif

// ─────────────────────────────────────────────
// Video thumbnail helper (Windows Shell cache)
// ─────────────────────────────────────────────

#ifdef __WXMSW__
// Called on the main thread — COM is initialised by wxWidgets (OleInitialize).
// Uses GetDIBits with an explicit top-down, 32-bit format so we get a
// normalised left→right, top→bottom BGRA buffer regardless of how the Shell
// chose to store the HBITMAP internally (stride, orientation, DDB vs DIB).
static wxBitmap GetVideoThumbnailBitmap(const wxString& path, int size)
{
    IShellItemImageFactory* pFactory = nullptr;
    if (FAILED(SHCreateItemFromParsingName(path.wc_str(), nullptr,
                                            IID_PPV_ARGS(&pFactory))))
        return wxNullBitmap;

    SIZE sz = { size, size };
    HBITMAP hBmp = nullptr;
    HRESULT hr = pFactory->GetImage(sz, SIIGBF_BIGGERSIZEOK, &hBmp);
    pFactory->Release();
    if (FAILED(hr) || !hBmp) return wxNullBitmap;

    // Query dimensions only (works for both DDB and DIB section).
    BITMAP bm = {};
    if (!GetObject(hBmp, sizeof(BITMAP), &bm) || bm.bmWidth <= 0 || bm.bmHeight == 0) {
        DeleteObject(hBmp);
        return wxNullBitmap;
    }
    int w = bm.bmWidth, h = abs(bm.bmHeight);

    // Ask GetDIBits for a top-down (biHeight < 0), 32-bit BGRA buffer.
    // This normalises any internal orientation/stride the Shell may have used.
    BITMAPINFOHEADER bih = {};
    bih.biSize        = sizeof(BITMAPINFOHEADER);
    bih.biWidth       = w;
    bih.biHeight      = -h; // negative = top-down output
    bih.biPlanes      = 1;
    bih.biBitCount    = 32;
    bih.biCompression = BI_RGB;

    std::vector<BYTE> pixels(w * h * 4);
    HDC hdc = ::GetDC(nullptr);
    int rows = ::GetDIBits(hdc, hBmp, 0, h, pixels.data(),
                            reinterpret_cast<BITMAPINFO*>(&bih), DIB_RGB_COLORS);
    ::ReleaseDC(nullptr, hdc);
    ::DeleteObject(hBmp);

    if (rows <= 0) return wxNullBitmap;

    // Convert BGRA → RGB for wxImage (stride is exactly w*4 because we asked for it).
    wxImage img(w, h, false);
    unsigned char* rgb = img.GetData();
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int si = (y * w + x) * 4, di = y * w + x;
            rgb[di*3+0] = pixels[si+2]; // R
            rgb[di*3+1] = pixels[si+1]; // G
            rgb[di*3+2] = pixels[si+0]; // B
        }
    }

    if (w > size || h > size) {
        double s = std::min((double)size / w, (double)size / h);
        img = img.Scale(std::max(1, (int)(w * s)),
                        std::max(1, (int)(h * s)),
                        wxIMAGE_QUALITY_NORMAL);
    }
    return wxBitmap(img);
}
#endif // __WXMSW__

// ─────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────

namespace {

void BlendImagePixel(wxImage& img, int x, int y,
                     unsigned char r, unsigned char g, unsigned char b,
                     unsigned char alpha)
{
    if (x < 0 || y < 0 || x >= img.GetWidth() || y >= img.GetHeight())
        return;

    unsigned char* data = img.GetData();
    if (!data) return;

    const int idx = (y * img.GetWidth() + x) * 3;
    const int invAlpha = 255 - alpha;

    data[idx + 0] = static_cast<unsigned char>((data[idx + 0] * invAlpha + r * alpha) / 255);
    data[idx + 1] = static_cast<unsigned char>((data[idx + 1] * invAlpha + g * alpha) / 255);
    data[idx + 2] = static_cast<unsigned char>((data[idx + 2] * invAlpha + b * alpha) / 255);
}

}

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
    BuildSortingUI();
    Bind(wxEVT_SIZE, &SortPhotosPanel::OnSize, this);
    wxGetTopLevelParent(this)->Bind(wxEVT_CHAR_HOOK, &SortPhotosPanel::OnKeyDown, this);
}

void SortPhotosPanel::BuildSortingUI()
{
    m_imageBitmap = new wxStaticBitmap(this, wxID_ANY, wxNullBitmap);

    m_openVideoBtn = new wxButton(this, wxID_ANY, "Open in Player");
    m_openVideoBtn->Hide();
    m_openVideoBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        auto* frame = dynamic_cast<PhotoQuickSorterFrame*>(GetParent());
        if (!frame) return;
        const ImageInfo* info = frame->imageRepo.GetAt(m_currentIndex);
        if (info) wxLaunchDefaultApplication(info->path);
    });

    m_imageNameLabel = new wxStaticText(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER | wxST_ELLIPSIZE_END);
    m_progressLabel  = new wxStaticText(this, wxID_ANY, "0/0", wxDefaultPosition, wxSize(60, -1), wxALIGN_RIGHT);
    m_progressBar    = new wxGauge(this, wxID_ANY, 1, wxDefaultPosition, wxDefaultSize, wxGA_HORIZONTAL | wxGA_SMOOTH);

    m_folder1Btn = new wxButton(this, ID_SORT_FOLDER1, "Folder 1");
    m_folder2Btn = new wxButton(this, ID_SORT_FOLDER2, "Folder 2");
    m_saveBtn    = new wxButton(this, ID_SORT_SAVE,    "Save");
    m_deleteBtn  = new wxButton(this, ID_SORT_DELETE,  "Delete");
    m_undoBtn    = new wxButton(this, ID_SORT_UNDO,    "Undo");
    m_ruleOfThirdsBtn = new wxButton(this, wxID_ANY, "Grid Off");

    wxBitmap icLeft  = LoadKeycap("cursor-left.png");
    wxBitmap icRight = LoadKeycap("cursor-right.png");
    wxBitmap icUp    = LoadKeycap("cursor-up.png");
    wxBitmap icDown  = LoadKeycap("cursor-down.png");
    wxBitmap icZ     = LoadKeycap("z.png");
    wxBitmap icG     = LoadKeycap("g.png");

    if (icLeft.IsOk())  { m_folder1Btn->SetBitmap(icLeft);  m_folder1Btn->SetBitmapPosition(wxLEFT);  }
    if (icRight.IsOk()) { m_folder2Btn->SetBitmap(icRight); m_folder2Btn->SetBitmapPosition(wxRIGHT); }
    if (icUp.IsOk())    { m_saveBtn->SetBitmap(icUp);       m_saveBtn->SetBitmapPosition(wxTOP);      }
    if (icDown.IsOk())  { m_deleteBtn->SetBitmap(icDown);   m_deleteBtn->SetBitmapPosition(wxBOTTOM); }
    if (icZ.IsOk())     { m_undoBtn->SetBitmap(icZ);        m_undoBtn->SetBitmapPosition(wxLEFT);     }
    if (icG.IsOk())     { m_ruleOfThirdsBtn->SetBitmap(icG); m_ruleOfThirdsBtn->SetBitmapPosition(wxLEFT); }
    m_ruleOfThirdsBtn->SetToolTip("Toggle a rule-of-thirds grid on the current image");


    wxFlexGridSizer* grid = new wxFlexGridSizer(3, 3, 0, 0);
    grid->AddGrowableRow(1);
    grid->AddGrowableCol(1);

    grid->Add(m_undoBtn,    0, wxALL | wxALIGN_CENTER_VERTICAL, 8);
    grid->Add(m_saveBtn,    0, wxALL | wxALIGN_CENTER, 8);
    grid->Add(0, 0);

    wxBoxSizer* imageCell = new wxBoxSizer(wxVERTICAL);
    imageCell->Add(m_imageNameLabel, 0, wxALIGN_CENTER | wxBOTTOM, 4);
    imageCell->Add(m_imageBitmap,    0, wxALIGN_CENTER);
    imageCell->Add(m_openVideoBtn,   0, wxALIGN_CENTER | wxTOP, 8);

    grid->Add(m_folder1Btn, 0, wxALL | wxALIGN_CENTER_VERTICAL, 8);
    grid->Add(imageCell,    1, wxEXPAND, 10);
    grid->Add(m_folder2Btn, 0, wxALL | wxALIGN_CENTER_VERTICAL, 8);

    grid->Add(m_ruleOfThirdsBtn, 0, wxALL | wxALIGN_CENTER_VERTICAL, 8);
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
    m_ruleOfThirdsBtn->Bind(wxEVT_BUTTON, &SortPhotosPanel::OnToggleRuleOfThirds, this);

    UpdateGridToggleButton();

    SetButtonsEnabled(false);
}

SortPhotosPanel::~SortPhotosPanel()
{
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
    m_actionHistory.clear();
    m_folder1List.clear();
    m_folder2List.clear();
    m_deleteList.clear();
    InvalidateImageRenderCache();

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

        LOG_INFO("Resumed previous session: %zu F1, %zu F2, %zu delete, %zu image(s) remaining",
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

wxImage SortPhotosPanel::ApplyRuleOfThirdsOverlay(wxImage img) const
{
    if (!img.IsOk()) return img;

    const int w = img.GetWidth();
    const int h = img.GetHeight();
    if (w < 3 || h < 3) return img;

    const int thickness = std::max(1, std::min(w, h) / 320);
    const int xLines[] = { w / 3, (2 * w) / 3 };
    const int yLines[] = { h / 3, (2 * h) / 3 };

    for (int lineX : xLines) {
        for (int offset = 0; offset < thickness; ++offset) {
            const int x = lineX + offset - thickness / 2;
            for (int y = 0; y < h; ++y) {
                BlendImagePixel(img, x, y, 255, 255, 255, 128);
            }
        }
    }

    for (int lineY : yLines) {
        for (int offset = 0; offset < thickness; ++offset) {
            const int y = lineY + offset - thickness / 2;
            for (int x = 0; x < w; ++x) {
                BlendImagePixel(img, x, y, 255, 255, 255, 128);
            }
        }
    }

    return img;
}

wxSize SortPhotosPanel::GetImageDisplayBounds() const
{
    wxSize panel = GetClientSize();
    int btnW = m_folder1Btn ? (m_folder1Btn->GetSize().x + 16) : 0;
    int maxW = std::max(1, panel.x - 2 * btnW);
    int maxH = std::max(1, (int)(panel.y * 0.75));
    return wxSize(maxW, maxH);
}

void SortPhotosPanel::InvalidateImageRenderCache()
{
    m_cachedImagePath.clear();
    m_cachedImageBounds = wxDefaultSize;
    m_cachedPlainBitmap = wxNullBitmap;
    m_cachedGridBitmap = wxNullBitmap;
}

wxBitmap SortPhotosPanel::GetOrCreateImageBitmap(const wxString& path, const wxSize& available, bool withGrid)
{
    const bool cacheMatches = m_cachedImagePath == path && m_cachedImageBounds == available;
    if (!cacheMatches)
        InvalidateImageRenderCache();

    if (m_cachedImagePath.IsEmpty()) {
        m_cachedImagePath = path;
        m_cachedImageBounds = available;
    }

    if (!m_cachedPlainBitmap.IsOk()) {
        wxImage img(path, wxBITMAP_TYPE_ANY);
        if (!img.IsOk()) {
            LOG_WARN("Could not load image: %s", path);
            return wxNullBitmap;
        }

        if (available.x > 1 && available.y > 1) {
            double scaleX = (double)available.x / img.GetWidth();
            double scaleY = (double)available.y / img.GetHeight();
            double scale  = std::min(scaleX, scaleY);
            if (scale < 1.0)
                img = img.Scale(std::max(1, (int)(img.GetWidth() * scale)),
                                std::max(1, (int)(img.GetHeight() * scale)),
                                wxIMAGE_QUALITY_HIGH);
        }

        m_cachedPlainBitmap = wxBitmap(img);
    }

    if (!withGrid)
        return m_cachedPlainBitmap;

    if (!m_cachedGridBitmap.IsOk()) {
        wxImage gridImg = m_cachedPlainBitmap.ConvertToImage();
        if (!gridImg.IsOk())
            return wxNullBitmap;
        m_cachedGridBitmap = wxBitmap(ApplyRuleOfThirdsOverlay(gridImg));
    }

    return m_cachedGridBitmap;
}

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
    m_imageBitmap->Show();

    if (m_currentIsVideo) {
        InvalidateImageRenderCache();
        // ── Video: show shell thumbnail + "Open in Player" button ────────────
        m_openVideoBtn->Show();
        Layout();

        wxSize panel = GetClientSize();
        int btnW = m_folder1Btn->GetSize().x + 16;
        int maxW = std::max(1, panel.x - 2 * btnW);
        int maxH = std::max(1, (int)(panel.y * 0.70)); // leave room for the button below

        // Request at the larger dimension so the shell gives us enough pixels,
        // then scale to fit within maxW x maxH (same aspect-ratio logic as images).
        int requestSize = std::max(64, std::max(maxW, maxH));

        wxBitmap thumb;
#ifdef __WXMSW__
        thumb = GetVideoThumbnailBitmap(info->path, requestSize);
#endif
        if (thumb.IsOk()) {
            wxImage img = thumb.ConvertToImage();
            double scaleX = (double)maxW / img.GetWidth();
            double scaleY = (double)maxH / img.GetHeight();
            double scale  = std::min(scaleX, scaleY);
            if (scale < 1.0)
                img = img.Scale(std::max(1, (int)(img.GetWidth()  * scale)),
                                std::max(1, (int)(img.GetHeight() * scale)),
                                wxIMAGE_QUALITY_HIGH);
            m_imageBitmap->SetBitmap(wxBitmap(img));
        } else {
            m_imageBitmap->SetBitmap(wxNullBitmap);
        }
    } else {
        // ── Image: scale and display with wxStaticBitmap ─────────────────────
        m_openVideoBtn->Hide();
        Layout();
        wxBitmap bitmap = GetOrCreateImageBitmap(info->path, GetImageDisplayBounds(), m_showRuleOfThirds);
        m_imageBitmap->SetBitmap(bitmap);
    }

    SetButtonsEnabled(true);
    UpdateGridToggleButton();
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
    DestroyChildren();
    m_imageBitmap    = nullptr;
    m_openVideoBtn   = nullptr;
    m_progressBar    = nullptr;
    m_progressLabel  = nullptr;
    m_imageNameLabel = nullptr;
    m_folder1Btn  = nullptr;
    m_folder2Btn  = nullptr;
    m_saveBtn     = nullptr;
    m_deleteBtn   = nullptr;
    m_undoBtn     = nullptr;
    m_ruleOfThirdsBtn = nullptr;
    InvalidateImageRenderCache();

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

    std::vector<wxString>* list = nullptr;
    wxString titleText, confirmLabel, skipLabel;
    ReviewStep step = m_currentReviewStep;

    switch (step) {
        case ReviewStep::Folder1:
            list         = &m_folder1List;
            titleText    = wxString::Format("-> Folder 1 - %zu file(s) to move", m_folder1List.size());
            confirmLabel = "Confirm Move";
            skipLabel    = "Skip (keep in base folder)";
            break;
        case ReviewStep::Folder2:
            list         = &m_folder2List;
            titleText    = wxString::Format("-> Folder 2 - %zu file(s) to move", m_folder2List.size());
            confirmLabel = "Confirm Move";
            skipLabel    = "Skip (keep in base folder)";
            break;
        case ReviewStep::Delete:
            list         = &m_deleteList;
            titleText    = wxString::Format("Delete - %zu file(s)", m_deleteList.size());
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

    // Scrollable thumbnail grid — interactive: hover any image to undo it
    ThumbnailGrid* thumbGrid = new ThumbnailGrid(this);
    thumbGrid->SetRemoveCallback([this, list, title, step](const wxString& path) {
        // Remove from the active review list
        auto it = std::find(list->begin(), list->end(), path);
        if (it != list->end()) list->erase(it);

        // Keep action history consistent
        m_actionHistory.erase(
            std::remove_if(m_actionHistory.begin(), m_actionHistory.end(),
                [&](const PendingAction& a) { return a.imagePath == path; }),
            m_actionHistory.end());

        // Persist updated lists
        PersistList(m_folder1List, "_pending_folder1.txt");
        PersistList(m_folder2List, "_pending_folder2.txt");
        PersistList(m_deleteList,  "_pending_deletes.txt");

        // Update the title label to reflect the new count
        wxString newTitle;
        switch (step) {
            case ReviewStep::Folder1:
                newTitle = wxString::Format("-> Folder 1 - %zu file(s) to move", list->size()); break;
            case ReviewStep::Folder2:
                newTitle = wxString::Format("-> Folder 2 - %zu file(s) to move", list->size()); break;
            case ReviewStep::Delete:
                newTitle = wxString::Format("Delete - %zu file(s)", list->size()); break;
        }
        title->SetLabel(newTitle);
    });
    thumbGrid->SetImages(std::vector<wxString>(list->begin(), list->end()));
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

void SortPhotosPanel::OnToggleRuleOfThirds(wxCommandEvent& WXUNUSED(evt))
{
    if (!m_ruleOfThirdsBtn || !m_ruleOfThirdsBtn->IsEnabled())
        return;

    m_showRuleOfThirds = !m_showRuleOfThirds;
    UpdateGridToggleButton();
    LoadCurrentImage();
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
    if (m_ruleOfThirdsBtn) m_ruleOfThirdsBtn->Enable(enabled && !m_currentIsVideo);
    UpdateGridToggleButton();
}

void SortPhotosPanel::UpdateGridToggleButton()
{
    if (!m_ruleOfThirdsBtn) return;

    m_ruleOfThirdsBtn->SetLabel(m_showRuleOfThirds ? "Grid On" : "Grid Off");
    if (!m_ruleOfThirdsBtn->IsEnabled()) {
        m_ruleOfThirdsBtn->SetBackgroundColour(wxNullColour);
        m_ruleOfThirdsBtn->SetForegroundColour(wxNullColour);
        return;
    }

    if (m_showRuleOfThirds) {
        m_ruleOfThirdsBtn->SetBackgroundColour(wxColour(70, 120, 70));
        m_ruleOfThirdsBtn->SetForegroundColour(*wxWHITE);
    } else {
        m_ruleOfThirdsBtn->SetBackgroundColour(wxNullColour);
        m_ruleOfThirdsBtn->SetForegroundColour(wxNullColour);
    }
    m_ruleOfThirdsBtn->Refresh();
}

void SortPhotosPanel::ShowDoneState()
{
    m_inReview = true;
    DestroyChildren();
    m_imageBitmap    = nullptr;
    m_openVideoBtn   = nullptr;
    m_progressBar    = nullptr;
    m_progressLabel  = nullptr;
    m_imageNameLabel = nullptr;
    m_folder1Btn = m_folder2Btn = m_saveBtn = m_deleteBtn = m_undoBtn = m_ruleOfThirdsBtn = nullptr;
    InvalidateImageRenderCache();

    Freeze();
    wxBoxSizer* vSizer = new wxBoxSizer(wxVERTICAL);
    vSizer->AddStretchSpacer();

    wxStaticText* msg = new wxStaticText(this, wxID_ANY, "All Done!\nAll images were saved.",
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
        if (m_ruleOfThirdsBtn && m_ruleOfThirdsBtn->IsEnabled()) {
            const int key = evt.GetKeyCode();
            if (key == 'G' || key == 'g') {
                wxCommandEvent dummy;
                OnToggleRuleOfThirds(dummy);
                return;
            }
        }
        evt.Skip();
        return;
    }

    wxCommandEvent dummy;
    switch (evt.GetKeyCode()) {
        case WXK_LEFT:  OnFolder1(dummy); break;
        case WXK_RIGHT: OnFolder2(dummy); break;
        case WXK_UP:    OnSave(dummy);    break;
        case WXK_DOWN:  OnDelete(dummy);  break;
        case 'Z':
        case 'z':       OnUndo(dummy);    break;
        case 'G':
        case 'g':       OnToggleRuleOfThirds(dummy); break;
        default:        evt.Skip();       break;
    }
}
