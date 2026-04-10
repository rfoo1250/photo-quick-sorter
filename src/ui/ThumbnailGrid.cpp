#include "ui/ThumbnailGrid.h"
#include "utils/MediaUtils.h"
#include "utils/logging.h"
#include <wx/filename.h>
#include <wx/filefn.h>
#include <wx/statbmp.h>
#include <wx/dcbuffer.h>
#include <algorithm>
#include <thread>

#ifdef __WXMSW__
#include <shobjidl.h>
#endif

static const int THUMB_PADDING = 12;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static wxBitmap MakePlaceholder(int size)
{
    wxImage img(size, size, true);
    img.SetRGB(wxRect(0, 0, size, size), 180, 180, 180);
    return wxBitmap(img);
}

static wxBitmap MakeDim(const wxBitmap& src)
{
    if (!src.IsOk()) return wxNullBitmap;
    wxImage img = src.ConvertToImage();
    unsigned char* data = img.GetData();
    int n = img.GetWidth() * img.GetHeight() * 3;
    for (int i = 0; i < n; ++i)
        data[i] = (unsigned char)(data[i] * 0.45f);
    return wxBitmap(img);
}

// ─────────────────────────────────────────────────────────────────────────────
// Windows Shell thumbnail helper (background-thread safe with COM initialized)
// ─────────────────────────────────────────────────────────────────────────────

#ifdef __WXMSW__
// Returns a wxImage extracted from the Windows Shell thumbnail cache for any
// file type (video, image, document…).  Returns wxNullImage on failure.
// Must be called from a thread where CoInitializeEx has already been called.
// Uses GetDIBits with biHeight<0 (top-down) so stride and orientation are
// normalised regardless of how the Shell stored the HBITMAP internally.
static wxImage GetShellThumbnailImage(const wxString& path, int size)
{
    IShellItemImageFactory* pFactory = nullptr;
    HRESULT hr = SHCreateItemFromParsingName(path.wc_str(), nullptr,
                                              IID_PPV_ARGS(&pFactory));
    if (FAILED(hr)) return wxNullImage;

    SIZE sz = { size, size };
    HBITMAP hBmp = nullptr;
    hr = pFactory->GetImage(sz, SIIGBF_BIGGERSIZEOK, &hBmp);
    pFactory->Release();
    if (FAILED(hr) || !hBmp) return wxNullImage;

    BITMAP bm = {};
    if (!GetObject(hBmp, sizeof(BITMAP), &bm) || bm.bmWidth <= 0 || bm.bmHeight == 0) {
        DeleteObject(hBmp);
        return wxNullImage;
    }
    int w = bm.bmWidth, h = abs(bm.bmHeight);

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

    if (rows <= 0) return wxNullImage;

    wxImage img(w, h, false);
    unsigned char* rgb = img.GetData();
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int si = (y * w + x) * 4, di = y * w + x;
            rgb[di * 3 + 0] = pixels[si + 2]; // R
            rgb[di * 3 + 1] = pixels[si + 1]; // G
            rgb[di * 3 + 2] = pixels[si + 0]; // B
        }
    }

    // Scale down to fit the requested thumb size (preserving aspect ratio).
    if (w > size || h > size) {
        double s = std::min((double)size / w, (double)size / h);
        img = img.Scale(std::max(1, (int)(w * s)),
                        std::max(1, (int)(h * s)),
                        wxIMAGE_QUALITY_NORMAL);
    }
    return img;
}
#endif // __WXMSW__

// ─────────────────────────────────────────────────────────────────────────────
// ThumbDisplay — custom-drawn panel that owns its own bitmap painting.
// Replaces wxStaticBitmap to avoid a Windows-specific issue: STM_SETIMAGE
// returns the old HBITMAP which wxWidgets wraps in a temporary wxBitmap and
// immediately destroys, freeing the handle that our wxBitmap member still
// references. The control then renders blank even though IsOk() returns true.
// ─────────────────────────────────────────────────────────────────────────────

class ThumbDisplay : public wxPanel {
public:
    ThumbDisplay(wxWindow* parent, int size)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(size, size))
    {
        SetMinSize(wxSize(size, size));
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        Bind(wxEVT_PAINT, &ThumbDisplay::OnPaint, this);
    }

    void SetBitmap(const wxBitmap& bmp)
    {
        m_bmp = bmp;
        Refresh();
    }

    bool HasBitmap() const { return m_bmp.IsOk(); }

private:
    void OnPaint(wxPaintEvent&)
    {
        wxAutoBufferedPaintDC dc(this);
        wxSize sz = GetClientSize();
        dc.SetBackground(wxBrush(GetBackgroundColour()));
        dc.Clear();
        if (m_bmp.IsOk()) {
            int x = (sz.x - m_bmp.GetWidth())  / 2;
            int y = (sz.y - m_bmp.GetHeight()) / 2;
            dc.DrawBitmap(m_bmp, x, y, false);
        }
    }

    wxBitmap m_bmp;
};

// ─────────────────────────────────────────────────────────────────────────────
// RemovableThumb — a single interactive thumbnail cell.
// Used by ThumbnailGrid when a remove callback is set.
// ─────────────────────────────────────────────────────────────────────────────

class RemovableThumb : public wxPanel {
public:
    RemovableThumb(wxWindow* parent, int thumbSize, const wxString& path,
                   std::function<void(const wxString&)> onRemove)
        : wxPanel(parent, wxID_ANY)
        , m_path(path)
        , m_onRemove(std::move(onRemove))
        , m_hoverTimer(this)
    {
        m_thumb = new ThumbDisplay(this, thumbSize);
        m_thumb->SetBitmap(MakePlaceholder(thumbSize));

        m_label = new wxStaticText(this, wxID_ANY,
            wxFileName(path).GetFullName(),
            wxDefaultPosition, wxSize(thumbSize, -1),
            wxALIGN_CENTER | wxST_ELLIPSIZE_END);

        wxBoxSizer* sz = new wxBoxSizer(wxVERTICAL);
        sz->Add(m_thumb, 0, wxALIGN_CENTER | wxBOTTOM, 3);
        sz->Add(m_label, 0, wxALIGN_CENTER);
        SetSizer(sz);

        // Undo button: child of this panel but NOT in the sizer.
        // Positioned manually over the image on hover.
        m_undoBtn = new wxButton(this, wxID_ANY, "Undo");
        m_undoBtn->SetBackgroundColour(wxColour(255, 255, 255));
        m_undoBtn->Hide();

        m_undoBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            m_onRemove(m_path);
        });

        // Timer-based hover polling: when hovered, poll every 40 ms to detect
        // when the cursor truly leaves the cell. This bypasses the unreliable
        // Win32 WM_MOUSELEAVE tracking chain that breaks when m_undoBtn
        // is shown/hidden (which can cancel the child's TrackMouseEvent).
        m_hoverTimer.Bind(wxEVT_TIMER, &RemovableThumb::OnHoverTimer, this);

        // Bind hover on this panel and its layout children so any motion
        // inside the cell triggers enter; OnLeave does a bounds check so
        // moving between children doesn't flicker.
        BindHover(this);
        BindHover(m_thumb);
        BindHover(m_label);
        BindHover(m_undoBtn);
    }

    // Called on the main thread when the background thread finishes loading.
    void SetThumbBitmap(const wxBitmap& bmp)
    {
        m_normalBmp = bmp;
        m_dimBmp    = MakeDim(bmp);
        LOG_DEBUG("Thumb loaded: %s", wxFileName(m_path).GetFullName());
        m_thumb->SetBitmap((m_hovered && m_dimBmp.IsOk()) ? m_dimBmp : m_normalBmp);
    }

private:
    void BindHover(wxWindow* w)
    {
        w->Bind(wxEVT_ENTER_WINDOW, &RemovableThumb::OnEnter, this);
        w->Bind(wxEVT_LEAVE_WINDOW, &RemovableThumb::OnLeave, this);
    }

    void OnHoverTimer(wxTimerEvent&)
    {
        wxPoint pos = ScreenToClient(wxGetMousePosition());
        if (GetClientRect().Contains(pos)) {
            SetHovered(true);
            m_hoverTimer.StartOnce(40);
        } else {
            SetHovered(false);
        }
    }

    void OnEnter(wxMouseEvent& evt)
    {
        evt.Skip();
        wxPoint pos = ScreenToClient(wxGetMousePosition());
        if (GetClientRect().Contains(pos)) {
            SetHovered(true);
            if (!m_hoverTimer.IsRunning())
                m_hoverTimer.StartOnce(40);
        }
    }

    void OnLeave(wxMouseEvent& evt)
    {
        evt.Skip();
        wxPoint pos = ScreenToClient(wxGetMousePosition());
        if (!GetClientRect().Contains(pos)) {
            m_hoverTimer.Stop();
            SetHovered(false);
        } else if (!m_hoverTimer.IsRunning()) {
            m_hoverTimer.StartOnce(40);
        }
    }

    void SetHovered(bool hovered)
    {
        if (m_hovered == hovered) return;
        m_hovered = hovered;
        LOG_DEBUG("Thumb hover: %s -> %s", wxFileName(m_path).GetFullName(),
            hovered ? "on" : "off");

        if (hovered) {
            if (m_dimBmp.IsOk())
                m_thumb->SetBitmap(m_dimBmp);

            // Position the undo button centred over the image widget.
            wxPoint tPos  = m_thumb->GetPosition();
            wxSize  tSize = m_thumb->GetSize();
            wxSize  bSize = m_undoBtn->GetBestSize();
            m_undoBtn->SetSize(
                tPos.x + (tSize.x - bSize.x) / 2,
                tPos.y + (tSize.y - bSize.y) / 2,
                bSize.x, bSize.y);
            m_undoBtn->Show();
            m_undoBtn->Raise();
        } else {
            // Restore bitmap BEFORE hiding the button so that when Win32 repaints
            // the area the button was covering, it reveals the normal image
            // rather than a still-dim or blank frame.
            if (m_normalBmp.IsOk())
                m_thumb->SetBitmap(m_normalBmp);
            m_undoBtn->Hide();
        }
    }

    ThumbDisplay*   m_thumb   = nullptr;
    wxStaticText*   m_label   = nullptr;
    wxButton*       m_undoBtn = nullptr;
    wxBitmap        m_normalBmp;
    wxBitmap        m_dimBmp;
    bool            m_hovered = false;
    wxTimer         m_hoverTimer;
    wxString        m_path;
    std::function<void(const wxString&)> m_onRemove;
};

// ─────────────────────────────────────────────────────────────────────────────
// ThumbnailGrid
// ─────────────────────────────────────────────────────────────────────────────

ThumbnailGrid::ThumbnailGrid(wxWindow* parent, const wxString& emptyHint)
    : wxScrolledWindow(parent, wxID_ANY,
                       wxDefaultPosition, wxDefaultSize, wxVSCROLL)
    , m_emptyHint(emptyHint)
{
    SetScrollRate(0, 20);
    Bind(wxEVT_SIZE, &ThumbnailGrid::OnSize, this);
    Rebuild();
}

ThumbnailGrid::~ThumbnailGrid()
{
    if (m_cancelToken)
        m_cancelToken->store(true, std::memory_order_relaxed);
    DeletePendingEvents();
}

void ThumbnailGrid::SetImages(const std::vector<wxString>& paths)
{
    m_paths = paths;
    Rebuild();
}

void ThumbnailGrid::SetEmptyHint(const wxString& hint)
{
    m_emptyHint = hint;
    if (m_paths.empty())
        Rebuild();
}

void ThumbnailGrid::SetRemoveCallback(std::function<void(const wxString&)> cb)
{
    m_removeCallback = std::move(cb);
}

void ThumbnailGrid::OnItemRemove(const wxString& path)
{
    auto it = std::find(m_paths.begin(), m_paths.end(), path);
    if (it != m_paths.end()) m_paths.erase(it);

    // Notify external observer synchronously (title update, list persistence…).
    if (m_removeCallback) m_removeCallback(path);

    // Defer the rebuild so we're fully out of the button-click call stack
    // before DestroyChildren() invalidates the RemovableThumb and its button.
    CallAfter([this]() { Rebuild(); });
}

void ThumbnailGrid::CancelPendingLoad()
{
    if (m_cancelToken)
        m_cancelToken->store(true, std::memory_order_relaxed);
    m_cancelToken.reset();
}

void ThumbnailGrid::OnSize(wxSizeEvent& evt)
{
    evt.Skip();
    if (!m_paths.empty())
        Rebuild();
}

void ThumbnailGrid::Rebuild()
{
    CancelPendingLoad();

    Freeze();
    DestroyChildren();
    m_thumbUpdaters.clear();

    // ── Empty state ──────────────────────────────────────────────────────────
    if (m_paths.empty()) {
        wxStaticText* lbl = new wxStaticText(this, wxID_ANY, m_emptyHint,
                                             wxDefaultPosition, wxDefaultSize,
                                             wxALIGN_CENTER);
        wxBoxSizer* s = new wxBoxSizer(wxVERTICAL);
        s->Add(lbl, 0, wxALIGN_CENTER | wxALL, 20);
        SetSizer(s);
        FitInside();
        Thaw();
        return;
    }

    // ── Compute thumb size ───────────────────────────────────────────────────
    int scrollW = GetClientSize().x;
    if (scrollW < 60) scrollW = 400;
    int thumbSize = (int)((scrollW * 0.92 - THUMB_PADDING * 4) / 3);
    if (thumbSize < 60) thumbSize = 60;

    // ── Build grid with placeholders ─────────────────────────────────────────
    wxFlexGridSizer* grid = new wxFlexGridSizer(0, 3, THUMB_PADDING, THUMB_PADDING);
    m_thumbUpdaters.reserve(m_paths.size());

    if (m_removeCallback) {
        // Interactive mode: RemovableThumb cells with hover overlay
        for (const wxString& path : m_paths) {
            RemovableThumb* cell = new RemovableThumb(this, thumbSize, path,
                [this](const wxString& p) { OnItemRemove(p); });
            m_thumbUpdaters.push_back([cell](const wxBitmap& b) {
                cell->SetThumbBitmap(b);
            });
            grid->Add(cell, 0, wxALIGN_TOP | wxALIGN_CENTER_HORIZONTAL | wxALL, 4);
        }
    } else {
        // Plain mode: static cells
        wxBitmap placeholder = MakePlaceholder(thumbSize);
        for (const wxString& path : m_paths) {
            wxStaticBitmap* thumb = new wxStaticBitmap(this, wxID_ANY, placeholder);
            thumb->SetMinSize(wxSize(thumbSize, thumbSize));
            m_thumbUpdaters.push_back([thumb](const wxBitmap& b) {
                thumb->SetBitmap(b);
            });

            wxStaticText* lbl = new wxStaticText(this, wxID_ANY,
                wxFileName(path).GetFullName(),
                wxDefaultPosition, wxSize(thumbSize, -1),
                wxALIGN_CENTER | wxST_ELLIPSIZE_END);

            wxBoxSizer* cell = new wxBoxSizer(wxVERTICAL);
            cell->Add(thumb, 0, wxALIGN_CENTER | wxBOTTOM, 3);
            cell->Add(lbl,   0, wxALIGN_CENTER);
            grid->Add(cell, 0, wxALIGN_TOP | wxALIGN_CENTER_HORIZONTAL | wxALL, 4);
        }
    }

    SetSizer(grid);
    FitInside();
    Thaw(); // placeholders visible NOW

    // ── Background loading ────────────────────────────────────────────────────
    m_cancelToken = std::make_shared<std::atomic<bool>>(false);

    std::vector<wxString>              paths = m_paths;
    int                                tSize = thumbSize;
    std::shared_ptr<std::atomic<bool>> token = m_cancelToken;

    std::thread([this, paths = std::move(paths), tSize, token]()
    {
#ifdef __WXMSW__
        // COM is required for the Windows Shell thumbnail API (IShellItemImageFactory).
        HRESULT comHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        bool comInited = SUCCEEDED(comHr) || comHr == RPC_E_CHANGED_MODE;
#endif

        for (size_t i = 0; i < paths.size(); ++i)
        {
            if (token->load(std::memory_order_relaxed)) break;

            wxImage img;

            if (MediaUtils::IsVideoFile(paths[i])) {
                // ── Video: extract thumbnail via Windows Shell cache ──────────
#ifdef __WXMSW__
                img = GetShellThumbnailImage(paths[i], tSize);
#endif
                // On non-Windows or if shell extraction fails, img stays invalid
                // and the gray placeholder remains.
            } else if (wxFileExists(paths[i])) {
                // ── Image: load and scale with wxImage ───────────────────────
                img.SetOption(wxIMAGE_OPTION_MAX_WIDTH,  tSize);
                img.SetOption(wxIMAGE_OPTION_MAX_HEIGHT, tSize);
                img.LoadFile(paths[i], wxBITMAP_TYPE_ANY);
            }

            if (img.IsOk())
            {
                double sx = (double)tSize / img.GetWidth();
                double sy = (double)tSize / img.GetHeight();
                double s  = std::min(sx, sy);
                if (s < 1.0)
                    img = img.Scale((int)(img.GetWidth()  * s),
                                    (int)(img.GetHeight() * s),
                                    wxIMAGE_QUALITY_NORMAL);
            }

            if (token->load(std::memory_order_relaxed)) break;

            auto imgPtr = std::make_shared<wxImage>(std::move(img));
            CallAfter([this, token, i, imgPtr]()
            {
                if (token->load(std::memory_order_relaxed)) return;
                if (i >= m_thumbUpdaters.size() || !m_thumbUpdaters[i]) return;
                if (imgPtr->IsOk())
                    m_thumbUpdaters[i](wxBitmap(*imgPtr));
            });
        }

#ifdef __WXMSW__
        if (comInited) CoUninitialize();
#endif
    }).detach();
}
