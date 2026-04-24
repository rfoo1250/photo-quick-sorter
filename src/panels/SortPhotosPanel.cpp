#include "panels/SortPhotosPanel.h"
#include "ui/PhotoQuickSorterFrame.h"
#include "ui/ThumbnailGrid.h"
#include "utils/logging.h"
#include "utils/MediaUtils.h"
#include <wx/filename.h>
#include <wx/filefn.h>
#include <wx/clipbrd.h>
#include <wx/dir.h>
#include <wx/textfile.h>
#include <wx/stdpaths.h>
#include <algorithm>
#include <cmath>

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

constexpr double ZOOM_MIN = 1.0;
constexpr double ZOOM_MAX = 8.0;
constexpr double ZOOM_STEP = 1.2;
constexpr double ZOOM_EPSILON = 0.0001;

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

bool NearlyEqual(double lhs, double rhs)
{
    return std::abs(lhs - rhs) < ZOOM_EPSILON;
}

wxSize CalculateRenderedSize(const wxSize& source, const wxSize& viewport, double zoom)
{
    if (source.x <= 0 || source.y <= 0 || viewport.x <= 0 || viewport.y <= 0)
        return wxSize(1, 1);

    const double baseScale = std::min(1.0,
        std::min((double)viewport.x / source.x, (double)viewport.y / source.y));
    const double renderScale = std::max(0.01, baseScale * zoom);

    return wxSize(
        std::max(1, (int)std::lround(source.x * renderScale)),
        std::max(1, (int)std::lround(source.y * renderScale)));
}

wxPoint2DDouble ClampImageOffsetToViewport(const wxSize& viewport,
                                           const wxSize& rendered,
                                           const wxPoint2DDouble& desired)
{
    wxPoint2DDouble clamped = desired;

    if (rendered.x <= viewport.x) {
        clamped.m_x = (viewport.x - rendered.x) / 2.0;
    } else {
        clamped.m_x = std::clamp(clamped.m_x, (double)viewport.x - rendered.x, 0.0);
    }

    if (rendered.y <= viewport.y) {
        clamped.m_y = (viewport.y - rendered.y) / 2.0;
    } else {
        clamped.m_y = std::clamp(clamped.m_y, (double)viewport.y - rendered.y, 0.0);
    }

    return clamped;
}

wxRect BuildImageRect(const wxSize& rendered, const wxPoint2DDouble& offset)
{
    return wxRect(
        (int)std::lround(offset.m_x),
        (int)std::lround(offset.m_y),
        rendered.x,
        rendered.y);
}

}

void SortPhotosPanel::TryQuitToMenu()
{
    static const int ID_SAVE_QUIT    = wxID_HIGHEST + 200;
    static const int ID_DISCARD_QUIT = wxID_HIGHEST + 201;

    wxDialog dlg(this, wxID_ANY, "Return to Main Menu?",
                 wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE);

    wxStaticText* msg = new wxStaticText(&dlg, wxID_ANY,
        "Return to the main menu?\n"
        "Do you want to save your sort progress?\n"
        "Saved progress can be resumed next time.");

    wxButton* saveBtn    = new wxButton(&dlg, ID_SAVE_QUIT,    "Yes, save progress");
    wxButton* discardBtn = new wxButton(&dlg, ID_DISCARD_QUIT, "Yes, do not save progress");
    wxButton* cancelBtn  = new wxButton(&dlg, wxID_CANCEL,     "No");
    saveBtn->SetDefault();

    wxBitmap icEnter = LoadKeycap("enter.png");
    wxBitmap icEsc   = LoadKeycap("esc.png");
    if (icEnter.IsOk()) { saveBtn->SetBitmap(icEnter);  saveBtn->SetBitmapPosition(wxLEFT);  }
    if (icEsc.IsOk())   { cancelBtn->SetBitmap(icEsc);  cancelBtn->SetBitmapPosition(wxLEFT); }

    saveBtn->Bind(wxEVT_BUTTON,    [&](wxCommandEvent&) { dlg.EndModal(ID_SAVE_QUIT);    });
    discardBtn->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) { dlg.EndModal(ID_DISCARD_QUIT); });
    cancelBtn->Bind(wxEVT_BUTTON,  [&](wxCommandEvent&) { dlg.EndModal(wxID_CANCEL);     });

    wxBoxSizer* btnRow = new wxBoxSizer(wxHORIZONTAL);
    btnRow->Add(saveBtn,    0, wxALL, 6);
    btnRow->Add(discardBtn, 0, wxALL, 6);
    btnRow->Add(cancelBtn,  0, wxALL, 6);

    wxBoxSizer* vSizer = new wxBoxSizer(wxVERTICAL);
    vSizer->Add(msg,    0, wxALL, 16);
    vSizer->Add(btnRow, 0, wxALIGN_CENTER | wxBOTTOM, 10);
    dlg.SetSizerAndFit(vSizer);
    dlg.Centre();

    const int choice = dlg.ShowModal();
    if (choice == wxID_CANCEL) return;

    auto* frame = dynamic_cast<PhotoQuickSorterFrame*>(GetParent());
    if (!frame) return;

    if (choice == ID_DISCARD_QUIT) {
        const wxString base = frame->folderLocations.baseFolder;
        const wxString names[] = {
            "_pending_folder1.txt", "_pending_folder2.txt", "_pending_deletes.txt"
        };
        for (const auto& name : names) {
            wxString p = base + wxFILE_SEP_PATH + name;
            if (wxFileExists(p)) wxRemoveFile(p);
        }
    }
    // ID_SAVE_QUIT: pending files are already up to date from PersistList calls

    frame->ShowMainMenuPanel();
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
    m_keyUpFilter = new KeyUpFilter(this);
    wxEvtHandler::AddFilter(m_keyUpFilter);
    BuildSortingUI();
    Bind(wxEVT_SIZE, &SortPhotosPanel::OnSize, this);
    wxGetTopLevelParent(this)->Bind(wxEVT_CHAR_HOOK, &SortPhotosPanel::OnKeyDown, this);
}

void SortPhotosPanel::BuildSortingUI()
{
    m_imageViewport = new wxPanel(this, wxID_ANY);
    m_imageBitmap = new wxStaticBitmap(m_imageViewport, wxID_ANY, wxNullBitmap);
    m_imageBitmap->Bind(wxEVT_MOUSEWHEEL, &SortPhotosPanel::OnImageMouseWheel, this);
    m_imageBitmap->Bind(wxEVT_LEFT_DOWN, &SortPhotosPanel::OnImageLeftDown, this);
    m_imageBitmap->Bind(wxEVT_LEFT_UP, &SortPhotosPanel::OnImageLeftUp, this);
    m_imageBitmap->Bind(wxEVT_MOTION, &SortPhotosPanel::OnImageMouseMove, this);
    m_imageBitmap->Bind(wxEVT_MOUSE_CAPTURE_LOST, &SortPhotosPanel::OnImageCaptureLost, this);

    m_zoomInBtn = new wxButton(m_imageViewport, wxID_ANY, "");
    m_zoomOutBtn = new wxButton(m_imageViewport, wxID_ANY, "");
    m_zoomInShiftHint = new wxStaticBitmap(m_imageViewport, wxID_ANY, wxNullBitmap);
    m_zoomOutShiftHint = new wxStaticBitmap(m_imageViewport, wxID_ANY, wxNullBitmap);
    m_zoomInBtn->Bind(wxEVT_BUTTON, &SortPhotosPanel::OnZoomIn, this);
    m_zoomOutBtn->Bind(wxEVT_BUTTON, &SortPhotosPanel::OnZoomOut, this);
    m_zoomInBtn->SetToolTip("Zoom in (Shift + = / +)");
    m_zoomOutBtn->SetToolTip("Zoom out (Shift + - / -)");

    m_openVideoBtn = new wxButton(this, wxID_ANY, "Open in Player");
    m_openVideoBtn->Hide();
    m_openVideoBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        auto* frame = dynamic_cast<PhotoQuickSorterFrame*>(GetParent());
        if (!frame) return;
        const ImageInfo* info = frame->imageRepo.GetAt(m_currentIndex);
        if (info) wxLaunchDefaultApplication(info->path);
    });

    m_imageNameLabel = new wxStaticText(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER | wxST_ELLIPSIZE_END);
    m_imageNameLabel->SetToolTip("Copy path");
    m_imageNameLabel->SetCursor(wxCursor(wxCURSOR_HAND));
    m_copyPathTimer = new wxTimer();
    m_copyPathTimer->Bind(wxEVT_TIMER, [this](wxTimerEvent&) {
        if (m_imageNameLabel) m_imageNameLabel->SetLabel(wxFileName(m_activeImagePath).GetFullName());
    });
    m_imageNameLabel->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) {
        if (m_activeImagePath.IsEmpty()) return;
        if (wxTheClipboard->Open()) {
            wxTheClipboard->SetData(new wxTextDataObject(m_activeImagePath));
            wxTheClipboard->Close();
            LOG_DEBUG("Copied path to clipboard: %s", m_activeImagePath);
        }
        m_imageNameLabel->SetLabel("Copied path!");
        m_copyPathTimer->Stop();
        m_copyPathTimer->StartOnce(2000);
    });
    m_progressLabel  = new wxStaticText(this, wxID_ANY, "0/0", wxDefaultPosition, wxSize(60, -1), wxALIGN_RIGHT);
    m_progressBar    = new wxGauge(this, wxID_ANY, 1, wxDefaultPosition, wxDefaultSize, wxGA_HORIZONTAL | wxGA_SMOOTH);

    m_quitBtn    = new wxButton(this, wxID_ANY, "Quit");
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
    wxBitmap icC     = LoadKeycap("c.png");
    wxBitmap icG     = LoadKeycap("g.png");
    wxBitmap icEsc   = LoadKeycap("esc.png");
    wxBitmap icEnter = LoadKeycap("enter.png");
    wxBitmap icShift = LoadKeycap("shift.png");
    wxBitmap icPlus  = LoadKeycap("equals-plus.png");
    wxBitmap icMinus = LoadKeycap("minus.png");

    if (icEsc.IsOk())   { m_quitBtn->SetBitmap(icEsc);      m_quitBtn->SetBitmapPosition(wxLEFT);    }
    if (icLeft.IsOk())  { m_folder1Btn->SetBitmap(icLeft);  m_folder1Btn->SetBitmapPosition(wxLEFT);  }
    if (icRight.IsOk()) { m_folder2Btn->SetBitmap(icRight); m_folder2Btn->SetBitmapPosition(wxRIGHT); }
    if (icUp.IsOk())    { m_saveBtn->SetBitmap(icUp);       m_saveBtn->SetBitmapPosition(wxTOP);      }
    if (icDown.IsOk())  { m_deleteBtn->SetBitmap(icDown);   m_deleteBtn->SetBitmapPosition(wxBOTTOM); }
    if (icZ.IsOk())     { m_undoBtn->SetBitmap(icZ);        m_undoBtn->SetBitmapPosition(wxLEFT);     }
    if (icG.IsOk())     { m_ruleOfThirdsBtn->SetBitmap(icG); m_ruleOfThirdsBtn->SetBitmapPosition(wxLEFT); }
    if (icEnter.IsOk()) { m_openVideoBtn->SetBitmap(icEnter); m_openVideoBtn->SetBitmapPosition(wxLEFT); }
    if (icShift.IsOk()) {
        m_zoomInShiftHint->SetBitmap(icShift);
        m_zoomOutShiftHint->SetBitmap(icShift);
        m_zoomInShiftHint->SetToolTip("Shift + =");
        m_zoomOutShiftHint->SetToolTip("Shift + -");
    }
    if (icPlus.IsOk())  { m_zoomInBtn->SetBitmap(icPlus);   m_zoomInBtn->SetBitmapPosition(wxLEFT);   }
    if (icMinus.IsOk()) { m_zoomOutBtn->SetBitmap(icMinus); m_zoomOutBtn->SetBitmapPosition(wxLEFT);  }
    m_ruleOfThirdsBtn->SetToolTip("Toggle a rule-of-thirds grid on the current image");

    m_copyNameBtn = new wxButton(this, wxID_ANY, "Copy name");
    m_copyNameBtn->SetToolTip("Copy just the name of the file");
    if (icC.IsOk())     { m_copyNameBtn->SetBitmap(icC);   m_copyNameBtn->SetBitmapPosition(wxLEFT);  }
    m_copyNameTimer = new wxTimer();
    m_copyNameTimer->Bind(wxEVT_TIMER, [this](wxTimerEvent&) {
        if (m_copyNameBtn) m_copyNameBtn->SetLabel("Copy name");
    });
    m_copyNameBtn->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
        m_btnMousePressTime = wxGetUTCTimeMillis(); e.Skip();
    });
    m_copyNameBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if ((wxGetUTCTimeMillis() - m_btnMousePressTime).GetValue() >= 2000) {
            LOG_DEBUG("Copy name button held >2s, treating as misclick");
            return;
        }
        if (m_activeImagePath.IsEmpty()) return;
        wxString stem = wxFileName(m_activeImagePath).GetName();
        if (wxTheClipboard->Open()) {
            wxTheClipboard->SetData(new wxTextDataObject(stem));
            wxTheClipboard->Close();
            LOG_DEBUG("Copied name to clipboard: %s", stem);
        }
        m_copyNameBtn->SetLabel("Copied!");
        m_copyNameTimer->Stop();
        m_copyNameTimer->StartOnce(2000);
    });

    m_fileMetaLabel = new wxStaticText(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT);
    m_fileMetaLabel->SetForegroundColour(wxColour(100, 100, 100));

    m_helpLabel = new wxStaticText(this, wxID_ANY,
        "Hold button/key >2s: ignored\n"
        "Click filename: copy full path\n"
        "Scroll on image: zoom\n"
        "Drag on image: pan");
    {
        wxFont f = m_helpLabel->GetFont();
        f.SetPointSize(f.GetPointSize() - 1);
        m_helpLabel->SetFont(f);
    }
    m_helpLabel->SetForegroundColour(wxColour(130, 130, 130));

    wxFlexGridSizer* grid = new wxFlexGridSizer(5, 3, 0, 0);
    grid->AddGrowableRow(2);
    grid->AddGrowableCol(1);

    // Row 0: quit | save | copy-name
    grid->Add(m_quitBtn,     0, wxALL | wxALIGN_CENTER_VERTICAL, 8);
    grid->Add(m_saveBtn,     0, wxALL | wxALIGN_CENTER, 8);
    grid->Add(m_copyNameBtn, 0, wxALL | wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL, 8);

    // Row 1: rule-of-thirds | spacer | undo
    grid->Add(m_ruleOfThirdsBtn, 0, wxALL | wxALIGN_CENTER_VERTICAL, 8);
    grid->Add(0, 0);
    grid->Add(m_undoBtn,     0, wxALL | wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL, 8);

    // Row 2 (growable): folder1 | image | folder2
    wxBoxSizer* imageCell = new wxBoxSizer(wxVERTICAL);
    imageCell->Add(m_imageNameLabel, 0, wxALIGN_CENTER | wxBOTTOM, 4);
    imageCell->Add(m_imageViewport,  1, wxEXPAND);

    grid->Add(m_folder1Btn, 0, wxALL | wxALIGN_CENTER_VERTICAL, 8);
    grid->Add(imageCell,    1, wxEXPAND, 10);
    grid->Add(m_folder2Btn, 0, wxALL | wxALIGN_CENTER_VERTICAL, 8);

    // Row 3: "Open in Player" — collapses to zero height for non-video images
    grid->Add(0, 0);
    grid->Add(m_openVideoBtn, 0, wxALIGN_CENTER | wxTOP | wxBOTTOM, 4);
    grid->Add(0, 0);

    // Row 4: help text | delete | file metadata
    grid->Add(m_helpLabel,     0, wxALL | wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL, 8);
    grid->Add(m_deleteBtn,     0, wxALL | wxALIGN_CENTER, 8);
    grid->Add(m_fileMetaLabel, 0, wxALL | wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL, 8);

    wxBoxSizer* progressRow = new wxBoxSizer(wxHORIZONTAL);
    progressRow->Add(m_progressLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    progressRow->Add(m_progressBar,   1, wxALIGN_CENTER_VERTICAL);

    wxBoxSizer* vSizer = new wxBoxSizer(wxVERTICAL);
    vSizer->Add(progressRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);
    vSizer->Add(grid, 1, wxEXPAND | wxALL, 8);
    SetSizer(vSizer);

    auto bindActionBtn = [this](wxButton* btn, auto handler) {
        btn->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
            m_btnMousePressTime = wxGetUTCTimeMillis(); e.Skip();
        });
        btn->Bind(wxEVT_BUTTON, [this, handler](wxCommandEvent&) {
            if ((wxGetUTCTimeMillis() - m_btnMousePressTime).GetValue() >= 2000) {
                LOG_DEBUG("Button held >2s, treating as misclick");
                return;
            }
            wxCommandEvent d; (this->*handler)(d);
        });
    };
    bindActionBtn(m_folder1Btn,     &SortPhotosPanel::OnFolder1);
    bindActionBtn(m_folder2Btn,     &SortPhotosPanel::OnFolder2);
    bindActionBtn(m_saveBtn,        &SortPhotosPanel::OnSave);
    bindActionBtn(m_deleteBtn,      &SortPhotosPanel::OnDelete);
    bindActionBtn(m_undoBtn,        &SortPhotosPanel::OnUndo);
    bindActionBtn(m_ruleOfThirdsBtn,&SortPhotosPanel::OnToggleRuleOfThirds);

    m_quitBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { TryQuitToMenu(); });

    UpdateGridToggleButton();
    UpdateZoomButtons();

    SetButtonsEnabled(false);
}

SortPhotosPanel::~SortPhotosPanel()
{
    wxEvtHandler::RemoveFilter(m_keyUpFilter);
    delete m_keyUpFilter;
    m_keyUpFilter = nullptr;
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
    m_activeImagePath.clear();
    ResetImageZoom();
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

    auto loadTxt = [](const wxString& path, wxString& destDir, std::vector<wxString>& out) {
        if (!wxFileExists(path)) return;
        wxTextFile f;
        if (!f.Open(path)) return;
        for (size_t i = 0; i < f.GetLineCount(); ++i) {
            wxString line = f.GetLine(i).Trim(true).Trim(false);
            if (line.IsEmpty()) continue;
            if (line.StartsWith("DEST:")) {
                destDir = line.Mid(5);
                continue;
            }
            if (wxFileExists(line))
                out.push_back(line);
        }
        f.Close();
    };

    wxString savedDest1, savedDest2, unused;
    std::vector<wxString> tmp1, tmp2, tmpd;
    loadTxt(f1txt,  savedDest1, tmp1);
    loadTxt(f2txt,  savedDest2, tmp2);
    loadTxt(deltxt, unused,     tmpd);

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
        // Check whether the user entered different destination folders this time.
        // If so, warn and override with the saved ones so files go to the right place.
        bool mismatch1 = !savedDest1.IsEmpty() && savedDest1 != frame->folderLocations.folder1;
        bool mismatch2 = !savedDest2.IsEmpty() && savedDest2 != frame->folderLocations.folder2;

        if (mismatch1 || mismatch2) {
            auto descEntered = [](const wxString& s) -> wxString {
                return s.IsEmpty() ? wxString("(none entered this session)")
                                   : wxString::Format("\"%s\"", s);
            };

            wxString msg = "The previous session used different destination folders.\n"
                           "The original destinations will be used to avoid misplacing files:\n\n";
            if (mismatch1)
                msg += wxString::Format("  Folder 1: \"%s\"\n  (this session: %s)\n\n",
                                        savedDest1, descEntered(frame->folderLocations.folder1));
            if (mismatch2)
                msg += wxString::Format("  Folder 2: \"%s\"\n  (this session: %s)\n\n",
                                        savedDest2, descEntered(frame->folderLocations.folder2));
            wxMessageBox(msg, "Destination Folders Changed", wxOK | wxICON_WARNING, this);

            if (mismatch1) frame->folderLocations.folder1 = savedDest1;
            if (mismatch2) frame->folderLocations.folder2 = savedDest2;
        }

        m_folder1List = std::move(tmp1);
        m_folder2List = std::move(tmp2);
        m_deleteList  = std::move(tmpd);
        // Rebuild action history (interleaving lost, but only the lists matter for review)
        for (const auto& p : m_folder1List) m_actionHistory.push_back({p, SortAction::MoveToFolder1});
        for (const auto& p : m_folder2List) m_actionHistory.push_back({p, SortAction::MoveToFolder2});
        for (const auto& p : m_deleteList)  m_actionHistory.push_back({p, SortAction::Delete});

        // Advance m_currentIndex past the already-processed images so sorting
        // resumes at the correct position (e.g. 17/30, not 0/13).
        // Images are always acted upon sequentially, so the processed ones
        // occupy the first N positions in imageRepo.
        for (const auto& action : m_actionHistory) {
            if (frame->imageRepo.FindByPath(action.imagePath) >= 0)
                ++m_currentIndex;
        }

        LOG_INFO("Resumed previous session: %zu F1, %zu F2, %zu delete, resuming at %zu/%zu",
                 m_folder1List.size(), m_folder2List.size(), m_deleteList.size(),
                 m_currentIndex, frame->imageRepo.GetCount());
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
    if (m_imageViewport) {
        wxSize viewport = m_imageViewport->GetClientSize();
        if (viewport.x > 1 && viewport.y > 1)
            return viewport;
    }

    wxSize panel = GetClientSize();
    int maxW = std::max(1, (int)(panel.x * 0.75));
    int maxH = std::max(1, (int)(panel.y * 0.75));
    return wxSize(maxW, maxH);
}

void SortPhotosPanel::InvalidateImageRenderCache()
{
    m_cachedImagePath.clear();
    m_cachedImageBounds = wxDefaultSize;
    m_cachedZoomFactor = 1.0;
    m_cachedImageOffset = wxPoint2DDouble(0.0, 0.0);
    m_cachedSourceImage = wxNullImage;
    m_cachedPlainBitmap = wxNullBitmap;
    m_cachedGridBitmap = wxNullBitmap;
    m_visibleImageRect = wxRect();
}

wxBitmap SortPhotosPanel::GetOrCreateImageBitmap(const wxString& path, const wxSize& available, bool withGrid)
{
    if (available.x <= 0 || available.y <= 0)
        return wxNullBitmap;

    if (m_cachedImagePath != path) {
        InvalidateImageRenderCache();
        m_cachedImagePath = path;
    }

    if (!m_cachedSourceImage.IsOk()) {
        m_cachedSourceImage.LoadFile(path, wxBITMAP_TYPE_ANY);
        if (!m_cachedSourceImage.IsOk()) {
            LOG_WARN("Could not load image: %s", path);
            return wxNullBitmap;
        }
    }

    const wxSize sourceSize(m_cachedSourceImage.GetWidth(), m_cachedSourceImage.GetHeight());
    const wxSize renderedSize = CalculateRenderedSize(sourceSize, available, m_imageZoom);
    const wxPoint2DDouble clampedOffset = ClampImageOffsetToViewport(available, renderedSize, m_imageOffset);
    m_imageOffset = clampedOffset;

    wxRect imageRect = BuildImageRect(renderedSize, clampedOffset);
    wxRect viewportRect(wxPoint(0, 0), available);
    wxRect visibleRect = imageRect;
    visibleRect.Intersect(viewportRect);
    m_visibleImageRect = visibleRect;

    const bool renderMatches = m_cachedImageBounds == available &&
                               NearlyEqual(m_cachedZoomFactor, m_imageZoom) &&
                               NearlyEqual(m_cachedImageOffset.m_x, clampedOffset.m_x) &&
                               NearlyEqual(m_cachedImageOffset.m_y, clampedOffset.m_y);
    if (!renderMatches) {
        m_cachedImageBounds = available;
        m_cachedZoomFactor = m_imageZoom;
        m_cachedImageOffset = clampedOffset;
        m_cachedPlainBitmap = wxNullBitmap;
        m_cachedGridBitmap = wxNullBitmap;
    }

    auto renderViewportFrame = [&](bool drawGrid) -> wxBitmap {
        wxBitmap frame(std::max(1, available.x), std::max(1, available.y), 32);
        wxMemoryDC dc(frame);

        wxColour bg = m_imageViewport ? m_imageViewport->GetBackgroundColour() : wxNullColour;
        if (!bg.IsOk()) bg = GetBackgroundColour();
        if (!bg.IsOk()) bg = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE);
        dc.SetBackground(wxBrush(bg));
        dc.Clear();

        if (!visibleRect.IsEmpty()) {
            int drawX = visibleRect.x;
            int drawY = visibleRect.y;
            int drawW = visibleRect.width;
            int drawH = visibleRect.height;

            wxImage drawImage;
            if (visibleRect == imageRect) {
                drawImage = m_cachedSourceImage.Copy();
            } else {
                const double scaleX = (double)sourceSize.x / renderedSize.x;
                const double scaleY = (double)sourceSize.y / renderedSize.y;

                const int srcX = std::clamp((int)std::floor((visibleRect.x - imageRect.x) * scaleX), 0, sourceSize.x - 1);
                const int srcY = std::clamp((int)std::floor((visibleRect.y - imageRect.y) * scaleY), 0, sourceSize.y - 1);
                const int srcRight = std::clamp((int)std::ceil((visibleRect.GetRight() + 1 - imageRect.x) * scaleX), srcX + 1, sourceSize.x);
                const int srcBottom = std::clamp((int)std::ceil((visibleRect.GetBottom() + 1 - imageRect.y) * scaleY), srcY + 1, sourceSize.y);

                drawImage = m_cachedSourceImage.GetSubImage(
                    wxRect(srcX, srcY, srcRight - srcX, srcBottom - srcY));
            }

            if (drawImage.IsOk() &&
                (drawImage.GetWidth() != drawW || drawImage.GetHeight() != drawH)) {
                drawImage = drawImage.Scale(drawW, drawH, wxIMAGE_QUALITY_HIGH);
            }

            if (drawGrid && IsAtOriginalZoom() && visibleRect == imageRect && drawImage.IsOk()) {
                drawImage = ApplyRuleOfThirdsOverlay(drawImage);
            }

            if (drawImage.IsOk()) {
                dc.DrawBitmap(wxBitmap(drawImage), drawX, drawY, false);
            }
        }

        dc.SelectObject(wxNullBitmap);
        return frame;
    };

    const bool allowGrid = withGrid && IsAtOriginalZoom();

    if (!m_cachedPlainBitmap.IsOk())
        m_cachedPlainBitmap = renderViewportFrame(false);

    if (!allowGrid)
        return m_cachedPlainBitmap;

    if (!m_cachedGridBitmap.IsOk())
        m_cachedGridBitmap = renderViewportFrame(true);

    return m_cachedGridBitmap;
}

void SortPhotosPanel::UpdateZoomButtons()
{
    if (!m_zoomInBtn || !m_zoomOutBtn) return;

    const bool hasImage = m_imageBitmap && m_imageBitmap->GetBitmap().IsOk();
    const bool sortingReady = m_folder1Btn && m_folder1Btn->IsEnabled();
    const bool showZoom = !m_inReview && !m_currentIsVideo && hasImage && sortingReady;
    m_zoomInBtn->Show(showZoom);
    m_zoomOutBtn->Show(showZoom);
    m_zoomInBtn->Enable(showZoom && m_imageZoom < ZOOM_MAX);
    m_zoomOutBtn->Enable(showZoom && !IsAtOriginalZoom());
    if (m_zoomInShiftHint)
        m_zoomInShiftHint->Show(showZoom && m_zoomInShiftHint->GetBitmap().IsOk());
    if (m_zoomOutShiftHint)
        m_zoomOutShiftHint->Show(showZoom && m_zoomOutShiftHint->GetBitmap().IsOk());
}

void SortPhotosPanel::UpdateImageViewportLayout()
{
    if (!m_imageViewport) return;

    wxSize viewport = m_imageViewport->GetClientSize();
    const int margin = 8;
    const int gap = 6;

    int zoomButtonsX = margin;
    const bool hasShiftHints = m_zoomInShiftHint && m_zoomOutShiftHint &&
                               m_zoomInShiftHint->GetBitmap().IsOk() &&
                               m_zoomOutShiftHint->GetBitmap().IsOk();
    if (hasShiftHints) {
        wxSize shiftInSize = m_zoomInShiftHint->GetBestSize();
        wxSize shiftOutSize = m_zoomOutShiftHint->GetBestSize();
        const int shiftX = margin;
        const int shiftGap = 6;
        const int zoomRow1Y = margin;
        const int zoomRow2Y = margin + std::max(shiftInSize.y, m_zoomInBtn ? m_zoomInBtn->GetBestSize().y : 0) + gap;

        m_zoomInShiftHint->SetSize(shiftX, zoomRow1Y, shiftInSize.x, shiftInSize.y);
        m_zoomOutShiftHint->SetSize(shiftX, zoomRow2Y, shiftOutSize.x, shiftOutSize.y);
        if (m_zoomInShiftHint->IsShown()) m_zoomInShiftHint->Raise();
        if (m_zoomOutShiftHint->IsShown()) m_zoomOutShiftHint->Raise();

        zoomButtonsX = shiftX + std::max(shiftInSize.x, shiftOutSize.x) + shiftGap;
    }

    if (m_zoomInBtn && m_zoomOutBtn) {
        wxSize plusSize = m_zoomInBtn->GetBestSize();
        wxSize minusSize = m_zoomOutBtn->GetBestSize();
        m_zoomInBtn->SetSize(zoomButtonsX, margin, plusSize.x, plusSize.y);
        m_zoomOutBtn->SetSize(zoomButtonsX, margin + plusSize.y + gap, minusSize.x, minusSize.y);
        if (m_zoomInBtn->IsShown()) m_zoomInBtn->Raise();
        if (m_zoomOutBtn->IsShown()) m_zoomOutBtn->Raise();
    }

    if (!m_imageBitmap) return;

    m_imageBitmap->SetPosition(wxPoint(0, 0));
    m_imageBitmap->SetSize(viewport);
}

bool SortPhotosPanel::IsAtOriginalZoom() const
{
    return NearlyEqual(m_imageZoom, 1.0);
}

void SortPhotosPanel::EndImageDrag()
{
    m_isDraggingImage = false;
    if (m_imageBitmap && m_imageBitmap->HasCapture())
        m_imageBitmap->ReleaseMouse();
}

void SortPhotosPanel::ResetImageZoom()
{
    EndImageDrag();
    if (!IsAtOriginalZoom())
        LOG_DEBUG("ResetImageZoom: was %.2f -> 1.0", m_imageZoom);
    m_imageZoom = 1.0;
    m_imageOffset = wxPoint2DDouble(0.0, 0.0);
    m_visibleImageRect = wxRect();
}

void SortPhotosPanel::ApplyZoomStep(int direction, const wxPoint* anchor)
{
    if (m_inReview || m_currentIsVideo) return;
    if (!m_cachedSourceImage.IsOk()) return;

    const wxSize viewport = GetImageDisplayBounds();
    const wxSize sourceSize(m_cachedSourceImage.GetWidth(), m_cachedSourceImage.GetHeight());
    const wxSize oldRenderedSize = CalculateRenderedSize(sourceSize, viewport, m_imageZoom);
    const wxPoint2DDouble oldOffset = ClampImageOffsetToViewport(viewport, oldRenderedSize, m_imageOffset);

    wxPoint anchorPoint = anchor ? *anchor : wxPoint(viewport.x / 2, viewport.y / 2);
    anchorPoint.x = std::clamp(anchorPoint.x, 0, std::max(0, viewport.x));
    anchorPoint.y = std::clamp(anchorPoint.y, 0, std::max(0, viewport.y));

    const double previousZoom = m_imageZoom;
    double newZoom = previousZoom;
    if (direction > 0)
        newZoom = std::min(ZOOM_MAX, previousZoom * ZOOM_STEP);
    else if (direction < 0)
        newZoom = std::max(ZOOM_MIN, previousZoom / ZOOM_STEP);

    if (NearlyEqual(newZoom, 1.0))
        newZoom = 1.0;

    if (NearlyEqual(newZoom, previousZoom))
        return;

    const double relX = oldRenderedSize.x > 0
        ? std::clamp((anchorPoint.x - oldOffset.m_x) / oldRenderedSize.x, 0.0, 1.0)
        : 0.5;
    const double relY = oldRenderedSize.y > 0
        ? std::clamp((anchorPoint.y - oldOffset.m_y) / oldRenderedSize.y, 0.0, 1.0)
        : 0.5;

    EndImageDrag();
    m_imageZoom = newZoom;

    if (IsAtOriginalZoom()) {
        m_imageOffset = wxPoint2DDouble(0.0, 0.0);
    } else {
        const wxSize newRenderedSize = CalculateRenderedSize(sourceSize, viewport, m_imageZoom);
        wxPoint2DDouble desiredOffset(
            anchorPoint.x - relX * newRenderedSize.x,
            anchorPoint.y - relY * newRenderedSize.y);
        m_imageOffset = ClampImageOffsetToViewport(viewport, newRenderedSize, desiredOffset);
    }

    if (!IsAtOriginalZoom() && m_showRuleOfThirds)
        m_showRuleOfThirds = false;

    LOG_DEBUG("ApplyZoomStep: %.2f -> %.2f (dir=%d)", previousZoom, m_imageZoom, direction);
    LoadCurrentImage();
}

bool SortPhotosPanel::RefreshCurrentImageBitmap()
{
    if (!m_imageBitmap || m_currentIsVideo || m_activeImagePath.IsEmpty())
        return false;

    wxSize available = GetImageDisplayBounds();
    if (available.x <= 0 || available.y <= 0) {
        Layout();
        UpdateImageViewportLayout();
        available = GetImageDisplayBounds();
    }

    if (available.x <= 0 || available.y <= 0) {
        LOG_WARN("RefreshCurrentImageBitmap: viewport still invalid after Layout (%dx%d)", available.x, available.y);
        return false;
    }

    wxBitmap bitmap = GetOrCreateImageBitmap(m_activeImagePath, available, m_showRuleOfThirds);
    if (!bitmap.IsOk()) {
        InvalidateImageRenderCache();
        bitmap = GetOrCreateImageBitmap(m_activeImagePath, available, m_showRuleOfThirds);
    }

    if (!bitmap.IsOk())
        return false;

    m_imageBitmap->SetBitmap(bitmap);
    UpdateImageViewportLayout();
    return true;
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

    {
        wxFileName fn(info->path);
        wxString type = fn.GetExt().Upper();
        wxULongLong bytes = fn.GetSize();
        wxString sizeStr;
        if (bytes == wxInvalidSize) {
            sizeStr = "Unknown size";
        } else if (bytes.GetValue() >= 1024ULL * 1024 * 1024) {
            sizeStr = wxString::Format("%.1f GB", bytes.GetValue() / (1024.0 * 1024 * 1024));
        } else if (bytes.GetValue() >= 1024ULL * 1024) {
            sizeStr = wxString::Format("%.1f MB", bytes.GetValue() / (1024.0 * 1024));
        } else if (bytes.GetValue() >= 1024ULL) {
            sizeStr = wxString::Format("%.1f KB", bytes.GetValue() / 1024.0);
        } else {
            sizeStr = wxString::Format("%llu B", bytes.GetValue());
        }
        wxDateTime dtCreate;
        wxString dateStr;
        if (fn.GetTimes(nullptr, nullptr, &dtCreate) && dtCreate.IsValid())
            dateStr = dtCreate.Format("%Y-%m-%d %H:%M");
        else
            dateStr = "Unknown";
        m_fileMetaLabel->SetLabel(wxString::Format("%s\n%s\n%s", type, sizeStr, dateStr));
    }

    if (m_activeImagePath != info->path) {
        m_activeImagePath = info->path;
        ResetImageZoom();
        InvalidateImageRenderCache();
    }

    m_currentIsVideo = MediaUtils::IsVideoFile(info->path);
    m_imageBitmap->Show();
    Layout();

    if (m_currentIsVideo) {
        InvalidateImageRenderCache();
        m_visibleImageRect = wxRect();
        // ── Video: show shell thumbnail + "Open in Player" button ────────────
        m_openVideoBtn->Show();
        wxSize viewport = GetImageDisplayBounds();
        int maxW = std::max(1, viewport.x);
        int maxH = std::max(1, viewport.y);

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

            wxBitmap frameBmp(std::max(1, viewport.x), std::max(1, viewport.y), 32);
            wxMemoryDC dc(frameBmp);
            wxColour bg = m_imageViewport ? m_imageViewport->GetBackgroundColour() : wxNullColour;
            if (!bg.IsOk()) bg = GetBackgroundColour();
            if (!bg.IsOk()) bg = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE);
            dc.SetBackground(wxBrush(bg));
            dc.Clear();

            const wxPoint origin(
                (viewport.x - img.GetWidth()) / 2,
                (viewport.y - img.GetHeight()) / 2);
            dc.DrawBitmap(wxBitmap(img), origin.x, origin.y, false);
            dc.SelectObject(wxNullBitmap);

            m_visibleImageRect = wxRect(origin, wxSize(img.GetWidth(), img.GetHeight()));
            m_imageBitmap->SetBitmap(frameBmp);
        } else {
            m_imageBitmap->SetBitmap(wxNullBitmap);
        }
    } else {
        // ── Image: scale and display with wxStaticBitmap ─────────────────────
        m_openVideoBtn->Hide();
        RefreshCurrentImageBitmap();
    }

    SetButtonsEnabled(true);
    UpdateGridToggleButton();
    UpdateZoomButtons();
    Layout();
    UpdateImageViewportLayout();
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
            PersistList(m_folder1List, "_pending_folder1.txt", frame->folderLocations.folder1);
            break;
        case SortAction::MoveToFolder2:
            m_folder2List.push_back(info->path);
            PersistList(m_folder2List, "_pending_folder2.txt", frame->folderLocations.folder2);
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

void SortPhotosPanel::PersistList(const std::vector<wxString>& list, const wxString& filename,
                                   const wxString& destDir)
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
    if (!destDir.IsEmpty())
        f.AddLine("DEST:" + destDir);
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

    auto* frame = dynamic_cast<PhotoQuickSorterFrame*>(GetParent());

    switch (undone.type) {
        case SortAction::MoveToFolder1:
            removeFrom(m_folder1List);
            PersistList(m_folder1List, "_pending_folder1.txt",
                        frame ? frame->folderLocations.folder1 : wxString());
            break;
        case SortAction::MoveToFolder2:
            removeFrom(m_folder2List);
            PersistList(m_folder2List, "_pending_folder2.txt",
                        frame ? frame->folderLocations.folder2 : wxString());
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

    m_heldKey = 0;
    m_btnMousePressTime = 0;
    m_inReview = true;

    // Destroy sorting UI and null all sorting-UI pointers
    DestroyChildren();
    m_imageViewport  = nullptr;
    m_imageBitmap    = nullptr;
    m_openVideoBtn   = nullptr;
    m_progressBar    = nullptr;
    m_progressLabel  = nullptr;
    m_imageNameLabel = nullptr;
    m_fileMetaLabel  = nullptr;
    m_helpLabel      = nullptr;
    m_quitBtn     = nullptr;
    m_folder1Btn  = nullptr;
    m_folder2Btn  = nullptr;
    m_saveBtn     = nullptr;
    m_deleteBtn   = nullptr;
    m_undoBtn     = nullptr;
    m_ruleOfThirdsBtn = nullptr;
    m_zoomInBtn   = nullptr;
    m_zoomOutBtn  = nullptr;
    m_zoomInShiftHint = nullptr;
    m_zoomOutShiftHint = nullptr;
    if (m_copyNameTimer) { m_copyNameTimer->Stop(); delete m_copyNameTimer; m_copyNameTimer = nullptr; }
    if (m_copyPathTimer) { m_copyPathTimer->Stop(); delete m_copyPathTimer; m_copyPathTimer = nullptr; }
    m_copyNameBtn = nullptr;
    m_activeImagePath.clear();
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
    thumbGrid->SetRemoveCallback([this, frame, list, title, step](const wxString& path) {
        // Remove from the active review list
        auto it = std::find(list->begin(), list->end(), path);
        if (it != list->end()) list->erase(it);

        // Keep action history consistent
        m_actionHistory.erase(
            std::remove_if(m_actionHistory.begin(), m_actionHistory.end(),
                [&](const PendingAction& a) { return a.imagePath == path; }),
            m_actionHistory.end());

        // Persist updated lists
        PersistList(m_folder1List, "_pending_folder1.txt", frame->folderLocations.folder1);
        PersistList(m_folder2List, "_pending_folder2.txt", frame->folderLocations.folder2);
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

wxString SortPhotosPanel::ExecuteFileMove(const wxString& srcPath, const wxString& destFolder)
{
    if (!wxFileExists(srcPath)) {
        LOG_WARN("ExecuteFileMove: source no longer exists: %s", srcPath);
        return wxString(); // silent skip — file already gone
    }

    if (!wxDir::Exists(destFolder)) {
        if (!wxFileName::Mkdir(destFolder, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL)) {
            LOG_ERROR("ExecuteFileMove: could not create folder: %s", destFolder);
            return wxString::Format("'%s' could not be moved — destination folder could not be created",
                                   wxFileName(srcPath).GetFullName());
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
        return wxString::Format("'%s' could not be moved (%s)",
                                srcFn.GetFullName(), wxSysErrorMsg());
    }
    if (!wxRemoveFile(srcPath)) {
        LOG_WARN("ExecuteFileMove: copy ok but source not deleted: %s", srcPath);
        return wxString::Format("'%s' was copied but the original could not be removed (%s)",
                                srcFn.GetFullName(), wxSysErrorMsg());
    }

    LOG_DEBUG("ExecuteFileMove: moved %s -> %s", srcPath, dest);
    return wxString();
}

void SortPhotosPanel::ExecuteConfirmedActions()
{
    auto* frame = dynamic_cast<PhotoQuickSorterFrame*>(GetParent());
    if (!frame) return;

    std::vector<wxString> failures;

    for (const auto& action : m_actionHistory) {
        switch (action.type) {
            case SortAction::MoveToFolder1:
                if (m_folder1Confirmed) {
                    wxString err = ExecuteFileMove(action.imagePath, frame->folderLocations.folder1);
                    if (!err.IsEmpty()) failures.push_back(err);
                }
                break;
            case SortAction::MoveToFolder2:
                if (m_folder2Confirmed) {
                    wxString err = ExecuteFileMove(action.imagePath, frame->folderLocations.folder2);
                    if (!err.IsEmpty()) failures.push_back(err);
                }
                break;
            case SortAction::Delete:
                if (m_deleteConfirmed && wxFileExists(action.imagePath)) {
                    if (!wxRemoveFile(action.imagePath)) {
                        LOG_WARN("ExecuteConfirmedActions: delete failed: %s", action.imagePath);
                        failures.push_back(wxString::Format("'%s' could not be deleted (%s)",
                            wxFileName(action.imagePath).GetFullName(), wxSysErrorMsg()));
                    }
                }
                break;
            case SortAction::Save:
                break;
        }
    }

    if (!failures.empty()) {
        wxString msg = "The following files could not be processed:\n\n";
        for (const auto& f : failures)
            msg += "\u2022 " + f + "\n";
        wxMessageBox(msg, "Some files could not be moved", wxOK | wxICON_WARNING, this);
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
    if (!m_ruleOfThirdsBtn || !m_ruleOfThirdsBtn->IsEnabled() || !IsAtOriginalZoom())
        return;

    const bool previousState = m_showRuleOfThirds;
    m_showRuleOfThirds = !m_showRuleOfThirds;
    InvalidateImageRenderCache();

    if (!RefreshCurrentImageBitmap()) {
        m_showRuleOfThirds = previousState;
        InvalidateImageRenderCache();
        RefreshCurrentImageBitmap();
    }

    UpdateGridToggleButton();
}

void SortPhotosPanel::OnZoomIn(wxCommandEvent& WXUNUSED(evt))
{
    ApplyZoomStep(1);
}

void SortPhotosPanel::OnZoomOut(wxCommandEvent& WXUNUSED(evt))
{
    ApplyZoomStep(-1);
}

void SortPhotosPanel::OnImageMouseWheel(wxMouseEvent& evt)
{
    if (m_inReview || m_currentIsVideo) {
        evt.Skip();
        return;
    }

    if (!m_visibleImageRect.Contains(evt.GetPosition())) {
        evt.Skip();
        return;
    }

    const int rotation = evt.GetWheelRotation();
    if (rotation > 0)
        ApplyZoomStep(1, &evt.GetPosition());
    else if (rotation < 0)
        ApplyZoomStep(-1, &evt.GetPosition());
    else
        evt.Skip();
}

void SortPhotosPanel::OnImageLeftDown(wxMouseEvent& evt)
{
    if (m_inReview || m_currentIsVideo || IsAtOriginalZoom() ||
        !m_imageBitmap || !m_imageBitmap->GetBitmap().IsOk() ||
        !m_visibleImageRect.Contains(evt.GetPosition())) {
        evt.Skip();
        return;
    }

    m_isDraggingImage = true;
    m_imageDragStart = evt.GetPosition();
    m_imageDragStartOffset = m_imageOffset;
    if (!m_imageBitmap->HasCapture())
        m_imageBitmap->CaptureMouse();
}

void SortPhotosPanel::OnImageLeftUp(wxMouseEvent& evt)
{
    if (!m_isDraggingImage) {
        evt.Skip();
        return;
    }

    EndImageDrag();
}

void SortPhotosPanel::OnImageMouseMove(wxMouseEvent& evt)
{
    if (!m_isDraggingImage) {
        evt.Skip();
        return;
    }

    if (!evt.LeftIsDown()) {
        EndImageDrag();
        evt.Skip();
        return;
    }

    if (!m_cachedSourceImage.IsOk()) {
        EndImageDrag();
        return;
    }

    const wxSize viewport = GetImageDisplayBounds();
    const wxSize sourceSize(m_cachedSourceImage.GetWidth(), m_cachedSourceImage.GetHeight());
    const wxSize renderedSize = CalculateRenderedSize(sourceSize, viewport, m_imageZoom);
    const wxPoint delta = evt.GetPosition() - m_imageDragStart;
    const wxPoint2DDouble desiredOffset(
        m_imageDragStartOffset.m_x + delta.x,
        m_imageDragStartOffset.m_y + delta.y);
    const wxPoint2DDouble clampedOffset =
        ClampImageOffsetToViewport(viewport, renderedSize, desiredOffset);

    if (NearlyEqual(clampedOffset.m_x, m_imageOffset.m_x) &&
        NearlyEqual(clampedOffset.m_y, m_imageOffset.m_y))
        return;

    m_imageOffset = clampedOffset;
    RefreshCurrentImageBitmap();
}

void SortPhotosPanel::OnImageCaptureLost(wxMouseCaptureLostEvent& WXUNUSED(evt))
{
    m_isDraggingImage = false;
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
    if (!enabled) ResetImageZoom();
    UpdateZoomButtons();
    UpdateGridToggleButton();
}

void SortPhotosPanel::UpdateGridToggleButton()
{
    if (!m_ruleOfThirdsBtn) return;

    const bool sortingReady = m_folder1Btn && m_folder1Btn->IsEnabled();
    const bool hasImage = !m_activeImagePath.IsEmpty() &&
                          (m_cachedSourceImage.IsOk() ||
                           (m_imageBitmap && m_imageBitmap->GetBitmap().IsOk()));
    const bool allowGrid = sortingReady && hasImage && !m_currentIsVideo && IsAtOriginalZoom();

    m_ruleOfThirdsBtn->Enable(allowGrid);
    m_ruleOfThirdsBtn->SetLabel(m_showRuleOfThirds ? "Grid On" : "Grid Off");
    if (!allowGrid) {
        m_ruleOfThirdsBtn->SetBackgroundColour(wxNullColour);
        m_ruleOfThirdsBtn->SetForegroundColour(wxNullColour);
        m_ruleOfThirdsBtn->Refresh();
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
    m_heldKey = 0;
    m_btnMousePressTime = 0;
    m_inReview = true;
    DestroyChildren();
    m_imageViewport  = nullptr;
    m_imageBitmap    = nullptr;
    m_openVideoBtn   = nullptr;
    m_progressBar    = nullptr;
    m_progressLabel  = nullptr;
    m_imageNameLabel = nullptr;
    m_fileMetaLabel  = nullptr;
    m_helpLabel      = nullptr;
    m_quitBtn = nullptr;
    m_folder1Btn = m_folder2Btn = m_saveBtn = m_deleteBtn = m_undoBtn = m_ruleOfThirdsBtn = nullptr;
    m_zoomInBtn = m_zoomOutBtn = nullptr;
    m_zoomInShiftHint = m_zoomOutShiftHint = nullptr;
    if (m_copyNameTimer) { m_copyNameTimer->Stop(); delete m_copyNameTimer; m_copyNameTimer = nullptr; }
    if (m_copyPathTimer) { m_copyPathTimer->Stop(); delete m_copyPathTimer; m_copyPathTimer = nullptr; }
    m_copyNameBtn = nullptr;
    m_activeImagePath.clear();
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
    else
        UpdateImageViewportLayout();
}

void SortPhotosPanel::OnKeyDown(wxKeyEvent& evt)
{
    if (!IsShown()) {
        evt.Skip();
        return;
    }

    wxTopLevelWindow* tlw = wxDynamicCast(wxGetTopLevelParent(this), wxTopLevelWindow);
    if (tlw && !tlw->IsActive()) {
        evt.Skip();
        return;
    }

    const int key = evt.GetKeyCode();
    const wxChar unicode = (wxChar)evt.GetUnicodeKey();
    const bool zoomInKey = key == WXK_ADD || key == WXK_NUMPAD_ADD ||
                           unicode == '+' || (key == '=' && evt.ShiftDown());
    const bool zoomOutKey = key == WXK_SUBTRACT || key == WXK_NUMPAD_SUBTRACT ||
                            key == '-' || unicode == '-' || unicode == '_' ||
                            (key == '-' && evt.ShiftDown());

    if (zoomInKey || zoomOutKey) {
        if (!m_inReview && !m_currentIsVideo && m_imageBitmap && m_imageBitmap->GetBitmap().IsOk())
            ApplyZoomStep(zoomInKey ? 1 : -1);
        return;
    }

    if (m_inReview) {
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

    // Normalize lowercase to uppercase for action keys
    const int normKey = (key >= 'a' && key <= 'z') ? key - 32 : key;

    if (key == WXK_ESCAPE) {
        TryQuitToMenu();
        return;
    }

    if (!m_folder1Btn || !m_folder1Btn->IsEnabled()) {
        if (normKey == 'G') {
            if (m_ruleOfThirdsBtn && m_ruleOfThirdsBtn->IsEnabled() && m_heldKey != 'G') {
                m_heldKey = 'G';
                m_keyPressTime = wxGetUTCTimeMillis();
                SetButtonVisualPressed(m_ruleOfThirdsBtn, true);
            }
            return;
        }
        evt.Skip();
        return;
    }

    // Arm the button on first press; ignore auto-repeat (same key already held)
    if (m_heldKey == normKey) return;

    // If a different key is somehow already held, release it cleanly first
    if (m_heldKey != 0) {
        SetButtonVisualPressed(KeyToButton(m_heldKey), false);
        m_heldKey = 0;
    }

    switch (normKey) {
        case WXK_LEFT:
        case WXK_RIGHT:
        case WXK_UP:
        case WXK_DOWN:
        case 'Z':
        case 'C':
        case 'G':
            if (KeyToButton(normKey) && KeyToButton(normKey)->IsEnabled()) {
                m_heldKey = normKey;
                m_keyPressTime = wxGetUTCTimeMillis();
                SetButtonVisualPressed(KeyToButton(normKey), true);
            }
            break;
        case WXK_RETURN:
        case WXK_NUMPAD_ENTER:
            if (m_currentIsVideo && m_openVideoBtn && m_openVideoBtn->IsShown()) {
                m_heldKey = normKey;
                m_keyPressTime = wxGetUTCTimeMillis();
                SetButtonVisualPressed(m_openVideoBtn, true);
            } else {
                evt.Skip();
            }
            break;
        default:
            evt.Skip();
            break;
    }
}

// ─────────────────────────────────────────────
// Key/button hold tracking
// ─────────────────────────────────────────────

int SortPhotosPanel::KeyUpFilter::FilterEvent(wxEvent& event)
{
    if (event.GetEventType() == wxEVT_KEY_UP)
        m_owner->HandleKeyUp(static_cast<wxKeyEvent&>(event));
    return Event_Skip;
}

void SortPhotosPanel::HandleKeyUp(wxKeyEvent& evt)
{
    if (!IsShown() || m_inReview || m_heldKey == 0) return;

    const int key = evt.GetKeyCode();
    const int normKey = (key >= 'a' && key <= 'z') ? key - 32 : key;
    if (normKey != m_heldKey) return;

    SetButtonVisualPressed(KeyToButton(m_heldKey), false);

    const wxLongLong elapsed = wxGetUTCTimeMillis() - m_keyPressTime;
    const int actionKey = m_heldKey;
    m_heldKey = 0;

    if (elapsed.GetValue() >= 2000) {
        LOG_DEBUG("Key held %lld ms (>2s), treating as misclick", elapsed.GetValue());
        return;
    }

    ExecuteKeyAction(actionKey);
}

wxButton* SortPhotosPanel::KeyToButton(int key) const
{
    switch (key) {
        case WXK_LEFT:         return m_folder1Btn;
        case WXK_RIGHT:        return m_folder2Btn;
        case WXK_UP:           return m_saveBtn;
        case WXK_DOWN:         return m_deleteBtn;
        case 'Z':              return m_undoBtn;
        case 'C':              return m_copyNameBtn;
        case 'G':              return m_ruleOfThirdsBtn;
        case WXK_RETURN:
        case WXK_NUMPAD_ENTER: return m_openVideoBtn;
        default:               return nullptr;
    }
}

void SortPhotosPanel::SetButtonVisualPressed(wxButton* btn, bool pressed)
{
    if (!btn) return;
    if (pressed) {
        btn->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT));
        btn->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHTTEXT));
    } else {
        btn->SetBackgroundColour(wxNullColour);
        btn->SetForegroundColour(wxNullColour);
    }
    btn->Refresh();
}

void SortPhotosPanel::ExecuteKeyAction(int key)
{
    wxCommandEvent dummy;
    switch (key) {
        case WXK_LEFT:  if (m_folder1Btn && m_folder1Btn->IsEnabled())       OnFolder1(dummy); break;
        case WXK_RIGHT: if (m_folder2Btn && m_folder2Btn->IsEnabled())       OnFolder2(dummy); break;
        case WXK_UP:    if (m_saveBtn    && m_saveBtn->IsEnabled())          OnSave(dummy);    break;
        case WXK_DOWN:  if (m_deleteBtn  && m_deleteBtn->IsEnabled())        OnDelete(dummy);  break;
        case 'Z':       if (m_undoBtn    && m_undoBtn->IsEnabled())          OnUndo(dummy);    break;
        case 'C':
            if (m_copyNameBtn && m_copyNameBtn->IsEnabled() && !m_activeImagePath.IsEmpty()) {
                wxString stem = wxFileName(m_activeImagePath).GetName();
                if (wxTheClipboard->Open()) {
                    wxTheClipboard->SetData(new wxTextDataObject(stem));
                    wxTheClipboard->Close();
                    LOG_DEBUG("Copied name to clipboard (keybind): %s", stem);
                }
                m_copyNameBtn->SetLabel("Copied!");
                m_copyNameTimer->Stop();
                m_copyNameTimer->StartOnce(2000);
            }
            break;
        case 'G':
            if (m_ruleOfThirdsBtn && m_ruleOfThirdsBtn->IsEnabled())
                OnToggleRuleOfThirds(dummy);
            break;
        case WXK_RETURN:
        case WXK_NUMPAD_ENTER:
            if (m_currentIsVideo && m_openVideoBtn && m_openVideoBtn->IsShown()) {
                auto* frame = dynamic_cast<PhotoQuickSorterFrame*>(GetParent());
                if (!frame) break;
                const ImageInfo* info = frame->imageRepo.GetAt(m_currentIndex);
                if (info) wxLaunchDefaultApplication(info->path);
            }
            break;
    }
}
