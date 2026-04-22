#include "ui/ThumbnailGrid.h"
#include "utils/MediaUtils.h"
#include "utils/logging.h"
#include <wx/filename.h>
#include <wx/filefn.h>
#include <wx/statbmp.h>
#include <wx/dcbuffer.h>
#include <wx/graphics.h>
#include <algorithm>
#include <cmath>
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
// ImageInspectOverlay — full-screen dimmed overlay for inspecting an image.
// Created as a direct child of the TLW so it floats above all content.
// Animates in over ~160ms (scale 0.85→1.0, opacity 0→1, background dim 0→180).
// Dismissed by clicking outside the image or pressing Escape.
// Loads the image at full resolution on a background thread; shows the
// thumbnail-sized preview immediately while the full-res loads.
// ─────────────────────────────────────────────────────────────────────────────

class ImageInspectOverlay : public wxPanel {
public:
    static constexpr int    ANIM_FRAMES = 8;
    static constexpr int    ANIM_MS     = 20;
    static constexpr double ZOOM_MIN    = 1.0;
    static constexpr double ZOOM_MAX    = 8.0;
    static constexpr double ZOOM_STEP   = 1.2;
    static constexpr double ZOOM_EPS    = 0.0001;

    ImageInspectOverlay(wxWindow* tlw, const wxString& path,
                        const wxBitmap& preview, std::shared_ptr<bool> ownerAlive)
        : wxPanel(tlw, wxID_ANY, wxPoint(0,0), tlw->GetClientSize(), wxNO_BORDER)
        , m_path(path), m_ownerAlive(std::move(ownerAlive))
        , m_cancelToken(std::make_shared<std::atomic<bool>>(false))
        , m_previewBitmap(preview)
    {
        Hide();
        m_dimmedBackground = CaptureAndDim(tlw);

        m_zoomInBtn  = new wxButton(this, wxID_ANY, "+", wxDefaultPosition, wxSize(36,36));
        m_zoomOutBtn = new wxButton(this, wxID_ANY, "-", wxDefaultPosition, wxSize(36,36));
        m_zoomInBtn ->SetToolTip("Zoom in");
        m_zoomOutBtn->SetToolTip("Zoom out");
        m_zoomInBtn ->Hide();
        m_zoomOutBtn->Hide();
        m_zoomInBtn ->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ApplyZoomStep( 1); });
        m_zoomOutBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ApplyZoomStep(-1); });
        PositionZoomButtons();

        SetBackgroundStyle(wxBG_STYLE_PAINT);
        Bind(wxEVT_PAINT,              &ImageInspectOverlay::OnPaint,       this);
        Bind(wxEVT_LEFT_DOWN,          &ImageInspectOverlay::OnLeftDown,    this);
        Bind(wxEVT_LEFT_UP,            &ImageInspectOverlay::OnLeftUp,      this);
        Bind(wxEVT_MOTION,             &ImageInspectOverlay::OnMouseMove,   this);
        Bind(wxEVT_MOUSEWHEEL,         &ImageInspectOverlay::OnMouseWheel,  this);
        Bind(wxEVT_MOUSE_CAPTURE_LOST, &ImageInspectOverlay::OnCaptureLost, this);
        tlw->Bind(wxEVT_CHAR_HOOK, &ImageInspectOverlay::OnKeyHook, this);
        tlw->Bind(wxEVT_SIZE,      &ImageInspectOverlay::OnTLWSize, this);

        m_animTimer.Bind(wxEVT_TIMER, &ImageInspectOverlay::OnAnimTimer, this);
        Show();
        Raise();
        SetFocus();
        m_animTimer.Start(ANIM_MS);
        StartFullResLoad();
    }

    ~ImageInspectOverlay()
    {
        m_cancelToken->store(true, std::memory_order_relaxed);
        m_animTimer.Stop();
        if (wxWindow* tlw = GetParent()) {
            tlw->Unbind(wxEVT_CHAR_HOOK, &ImageInspectOverlay::OnKeyHook, this);
            tlw->Unbind(wxEVT_SIZE,      &ImageInspectOverlay::OnTLWSize, this);
        }
        if (auto p = m_ownerAlive.lock()) m_onNullify();
    }

    void SetNullifyCallback(std::function<void()> cb) { m_onNullify = std::move(cb); }
    void Dismiss() { Destroy(); }

private:
    // ── Static helpers ────────────────────────────────────────────────────────

    static wxBitmap CaptureAndDim(wxWindow* win)
    {
#ifdef __WXMSW__
        wxSize sz = win->GetClientSize();
        if (sz.x <= 0 || sz.y <= 0) return wxNullBitmap;
        wxBitmap bmp(sz);
        {
            wxMemoryDC memDC(bmp);
            HDC  hdc  = memDC.GetHDC();
            HWND hwnd = (HWND)win->GetHWND();
#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif
            if (!PrintWindow(hwnd, hdc, PW_CLIENTONLY | PW_RENDERFULLCONTENT))
                PrintWindow(hwnd, hdc, PW_CLIENTONLY);
        }
        wxImage img = bmp.ConvertToImage();
        if (!img.IsOk()) return wxNullBitmap;
        unsigned char* data = img.GetData();
        const int n = img.GetWidth() * img.GetHeight() * 3;
        for (int i = 0; i < n; ++i)
            data[i] = (unsigned char)(data[i] * 0.35f);
        return wxBitmap(img);
#else
        return wxNullBitmap;
#endif
    }

    static wxBitmap ScaleToFit(const wxBitmap& src, const wxSize& area,
                               wxImageResizeQuality quality)
    {
        if (!src.IsOk() || area.x <= 0 || area.y <= 0) return src;
        const int bw = src.GetWidth(), bh = src.GetHeight();
        if (bw <= 0 || bh <= 0) return src;
        const int maxW = std::max(1, (int)(area.x * 0.9));
        const int maxH = std::max(1, (int)(area.y * 0.9));
        const double s = std::min((double)maxW / bw, (double)maxH / bh);
        const int dw = std::max(1, (int)(bw * s)), dh = std::max(1, (int)(bh * s));
        if (dw == bw && dh == bh) return src;
        return wxBitmap(src.ConvertToImage().Scale(dw, dh, quality));
    }

    static wxRect CenteredRect(const wxSize& sz, int bw, int bh)
    {
        return wxRect((sz.x - bw) / 2, (sz.y - bh) / 2, bw, bh);
    }

    void PositionZoomButtons()
    {
        const int margin = 12, gap = 6, sz = 36;
        if (m_zoomInBtn)  { m_zoomInBtn ->SetSize(margin, margin,        sz, sz); m_zoomInBtn ->Raise(); }
        if (m_zoomOutBtn) { m_zoomOutBtn->SetSize(margin, margin+sz+gap, sz, sz); m_zoomOutBtn->Raise(); }
    }

    void UpdateZoomButtons()
    {
        if (m_zoomInBtn)  m_zoomInBtn ->Enable(m_zoom < ZOOM_MAX - ZOOM_EPS);
        if (m_zoomOutBtn) m_zoomOutBtn->Enable(m_zoom > ZOOM_MIN + ZOOM_EPS);
    }

    static wxPoint2DDouble ClampOffset(const wxSize& vp, const wxSize& r, wxPoint2DDouble off)
    {
        off.m_x = (r.x <= vp.x) ? (vp.x - r.x) / 2.0
                                 : std::clamp(off.m_x, (double)vp.x - r.x, 0.0);
        off.m_y = (r.y <= vp.y) ? (vp.y - r.y) / 2.0
                                 : std::clamp(off.m_y, (double)vp.y - r.y, 0.0);
        return off;
    }

    void EndDrag()
    {
        if (m_isDragging) { m_isDragging = false; if (HasCapture()) ReleaseMouse(); }
    }

    void ApplyZoomStep(int dir, const wxPoint* anchor = nullptr)
    {
        if (!m_sourceImage.IsOk()) return;
        const wxSize vp = GetClientSize();

        double nz = (dir > 0) ? std::min(ZOOM_MAX, m_zoom * ZOOM_STEP)
                               : std::max(ZOOM_MIN, m_zoom / ZOOM_STEP);
        if (std::abs(nz - ZOOM_MIN) < ZOOM_EPS) nz = ZOOM_MIN;
        if (std::abs(nz - m_zoom)   < ZOOM_EPS) return;

        // Remember which image-relative point should stay fixed under the anchor.
        const wxPoint ap = anchor ? *anchor : wxPoint(vp.x / 2, vp.y / 2);
        const wxSize oldR = m_displayBitmap.IsOk()
            ? wxSize(m_displayBitmap.GetWidth(), m_displayBitmap.GetHeight())
            : wxSize(1, 1);
        const double relX = oldR.x > 0 ? std::clamp((ap.x - m_offset.m_x) / oldR.x, 0.0, 1.0) : 0.5;
        const double relY = oldR.y > 0 ? std::clamp((ap.y - m_offset.m_y) / oldR.y, 0.0, 1.0) : 0.5;

        EndDrag();
        m_zoom = nz;
        m_displayBitmap = wxNullBitmap;
        RebuildDisplayBitmap(vp);

        if (m_displayBitmap.IsOk()) {
            const wxSize newR = {m_displayBitmap.GetWidth(), m_displayBitmap.GetHeight()};
            m_offset = ClampOffset(vp, newR, {ap.x - relX * newR.x, ap.y - relY * newR.y});
        }

        UpdateZoomButtons();
        Refresh();
    }

    // Current image rect (for dismiss hit-test and drag bounds).
    // Re-clamps m_offset so the rect always matches exactly what OnPaint draws.
    wxRect GetImageRect(const wxSize& sz) const
    {
        if (m_displayBitmap.IsOk()) {
            const wxSize r = {m_displayBitmap.GetWidth(), m_displayBitmap.GetHeight()};
            const wxPoint2DDouble off = ClampOffset(sz, r, m_offset);
            return { (int)std::lround(off.m_x), (int)std::lround(off.m_y), r.x, r.y };
        }
        if (m_scaledPreview.IsOk())
            return CenteredRect(sz, m_scaledPreview.GetWidth(), m_scaledPreview.GetHeight());
        return {};
    }

    // ── Events ────────────────────────────────────────────────────────────────

    void OnAnimTimer(wxTimerEvent&)
    {
        if (++m_animFrame >= ANIM_FRAMES) m_animTimer.Stop();
        Refresh();
    }

    void OnPaint(wxPaintEvent&)
    {
        wxAutoBufferedPaintDC dc(this);
        const wxSize sz = GetClientSize();

        if (m_dimmedBackground.IsOk()) dc.DrawBitmap(m_dimmedBackground, 0, 0, false);
        else { dc.SetBackground(wxBrush(wxColour(15,15,15))); dc.Clear(); }

        // Lazily scale preview bitmap on first paint
        if (!m_scaledPreview.IsOk() && !m_displayBitmap.IsOk() && m_previewBitmap.IsOk())
            m_scaledPreview = ScaleToFit(m_previewBitmap, sz, wxIMAGE_QUALITY_NORMAL);

        const wxBitmap& draw = m_displayBitmap.IsOk() ? m_displayBitmap : m_scaledPreview;
        if (!draw.IsOk()) return;

        const int bw = draw.GetWidth(), bh = draw.GetHeight();

        if (m_animFrame < ANIM_FRAMES) {
            // Animate using centered rect (offset not yet set during animation)
            const wxRect r  = CenteredRect(sz, bw, bh);
            const float  t  = (float)m_animFrame / ANIM_FRAMES;
            const double sc = 0.85 + 0.15 * t;
            const int dw = std::max(1,(int)(bw*sc)), dh = std::max(1,(int)(bh*sc));
            dc.DrawBitmap(wxBitmap(draw.ConvertToImage().Scale(dw, dh, wxIMAGE_QUALITY_NORMAL)),
                          r.x + (bw-dw)/2, r.y + (bh-dh)/2, false);
        } else if (m_displayBitmap.IsOk()) {
            const wxSize r = {bw, bh};
            const wxPoint2DDouble off = ClampOffset(sz, r, m_offset);
            dc.DrawBitmap(m_displayBitmap,
                          (int)std::lround(off.m_x), (int)std::lround(off.m_y), false);
        } else {
            // Full-res still loading — preview centered
            const wxRect r = CenteredRect(sz, bw, bh);
            dc.DrawBitmap(draw, r.x, r.y, false);
        }
    }

    void OnLeftDown(wxMouseEvent& evt)
    {
        if (m_animFrame < ANIM_FRAMES) { Dismiss(); return; }
        // Clicks on child controls (e.g. a disabled zoom button) fall through to
        // this panel on Windows — don't treat them as dismiss clicks.
        wxWindow* hit = wxFindWindowAtPoint(ClientToScreen(evt.GetPosition()));
        if (hit && hit != this) return;

        const wxPoint pos = evt.GetPosition();
        const wxSize  vp  = GetClientSize();
        const wxRect  img = GetImageRect(vp);

        if (m_zoom > ZOOM_MIN + ZOOM_EPS && img.Contains(pos)) {
            // Start pan drag
            m_isDragging   = true;
            m_dragStart    = pos;
            m_dragStartOff = m_offset;
            CaptureMouse();
        } else if (!img.Contains(pos)) {
            Dismiss();
        }
    }

    void OnLeftUp(wxMouseEvent& evt)
    {
        if (m_isDragging) EndDrag();
        else evt.Skip();
    }

    void OnMouseMove(wxMouseEvent& evt)
    {
        if (!m_isDragging) { evt.Skip(); return; }
        if (!evt.LeftIsDown()) { EndDrag(); return; }

        const wxSize  vp    = GetClientSize();
        const wxSize  r     = { m_displayBitmap.GetWidth(), m_displayBitmap.GetHeight() };
        const wxPoint delta = evt.GetPosition() - m_dragStart;
        m_offset = ClampOffset(vp, r, { m_dragStartOff.m_x + delta.x,
                                        m_dragStartOff.m_y + delta.y });
        Refresh();
    }

    void OnCaptureLost(wxMouseCaptureLostEvent&) { m_isDragging = false; }

    void OnMouseWheel(wxMouseEvent& evt)
    {
        if (!m_sourceImage.IsOk()) { evt.Skip(); return; }
        wxPoint pos = evt.GetPosition();
        const int rot = evt.GetWheelRotation();
        if      (rot > 0) ApplyZoomStep( 1, &pos);
        else if (rot < 0) ApplyZoomStep(-1, &pos);
        else evt.Skip();
    }

    void OnKeyHook(wxKeyEvent& evt)
    {
        if (evt.GetKeyCode() == WXK_ESCAPE) Dismiss();
        else evt.Skip();
    }

    void OnTLWSize(wxSizeEvent& evt)
    {
        evt.Skip();
        if (wxWindow* tlw = GetParent()) {
            SetSize(tlw->GetClientSize());
            const wxSize newVP = tlw->GetClientSize();
            m_dimmedBackground = wxNullBitmap;
            m_scaledPreview    = wxNullBitmap;
            m_displayBitmap    = wxNullBitmap;
            EndDrag();
            if (m_sourceImage.IsOk()) {
                RebuildDisplayBitmap(newVP);
                if (m_displayBitmap.IsOk()) {
                    const wxSize r = {m_displayBitmap.GetWidth(), m_displayBitmap.GetHeight()};
                    m_offset = ClampOffset(newVP, r, {(newVP.x - r.x) / 2.0, (newVP.y - r.y) / 2.0});
                }
            }
            PositionZoomButtons();
            Refresh();
        }
    }

    // ── Full-res background load ──────────────────────────────────────────────

    void RebuildDisplayBitmap(const wxSize& vpSize)
    {
        if (!m_sourceImage.IsOk() || vpSize.x <= 0 || vpSize.y <= 0) return;
        const int srcW = m_sourceImage.GetWidth(), srcH = m_sourceImage.GetHeight();
        // base: largest scale that fits 90% of the viewport at zoom=1
        const double base = std::min(1.0, std::min((double)(vpSize.x * 0.9) / srcW,
                                                   (double)(vpSize.y * 0.9) / srcH));
        const double scale = base * m_zoom;
        const int dw = std::max(1, (int)std::round(srcW * scale));
        const int dh = std::max(1, (int)std::round(srcH * scale));
        m_displayBitmap = wxBitmap(m_sourceImage.Scale(dw, dh, wxIMAGE_QUALITY_HIGH));
    }

    void StartFullResLoad()
    {
        auto     token = m_cancelToken;
        wxString path  = m_path;
        wxSize   vp    = GetClientSize();
        if (vp.x <= 0 || vp.y <= 0)
            if (wxWindow* tlw = GetParent()) vp = tlw->GetClientSize();

        std::thread([this, path, token, vp]() {
            if (token->load(std::memory_order_relaxed)) return;
            wxImage img;
            img.LoadFile(path, wxBITMAP_TYPE_ANY);
            if (!img.IsOk() || token->load(std::memory_order_relaxed)) return;
            auto imgPtr = std::make_shared<wxImage>(std::move(img));
            CallAfter([this, token, imgPtr, vp]() {
                if (token->load(std::memory_order_relaxed)) return;
                if (!imgPtr->IsOk()) return;
                m_sourceImage = std::move(*imgPtr);
                RebuildDisplayBitmap(vp);
                if (m_displayBitmap.IsOk()) {
                    const wxSize r = {m_displayBitmap.GetWidth(), m_displayBitmap.GetHeight()};
                    m_offset = ClampOffset(vp, r, {(vp.x - r.x) / 2.0, (vp.y - r.y) / 2.0});
                }
                if (m_zoomInBtn)  m_zoomInBtn ->Show();
                if (m_zoomOutBtn) m_zoomOutBtn->Show();
                PositionZoomButtons();
                UpdateZoomButtons();
                Refresh();
                LOG_DEBUG("Inspect: full-res loaded (%dx%d) for %s",
                          m_sourceImage.GetWidth(), m_sourceImage.GetHeight(), m_path);
            });
        }).detach();
    }

    // ── Members ───────────────────────────────────────────────────────────────

    wxString  m_path;
    std::weak_ptr<bool>   m_ownerAlive;
    std::function<void()> m_onNullify;
    wxBitmap  m_dimmedBackground;
    wxBitmap  m_previewBitmap;   // original thumbnail bitmap (not scaled)
    wxBitmap  m_scaledPreview;   // preview scaled to fit viewport (lazy)
    wxImage   m_sourceImage;     // full-res source (retained for resize)
    wxBitmap  m_displayBitmap;   // full-res scaled to fit viewport
    wxTimer   m_animTimer;
    int       m_animFrame = 0;
    std::shared_ptr<std::atomic<bool>> m_cancelToken;

    double          m_zoom         = ZOOM_MIN;
    wxPoint2DDouble m_offset       = wxPoint2DDouble(0.0, 0.0);
    bool            m_isDragging   = false;
    wxPoint         m_dragStart;
    wxPoint2DDouble m_dragStartOff = wxPoint2DDouble(0.0, 0.0);

    wxButton* m_zoomInBtn  = nullptr;
    wxButton* m_zoomOutBtn = nullptr;
};

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

    wxBitmap GetBitmap() const { return m_bmp; }
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
    using InspectCb = std::function<void(const wxString&, const wxBitmap&)>;

    RemovableThumb(wxWindow* parent, int thumbSize, const wxString& path,
                   std::function<void(const wxString&)> onRemove,
                   InspectCb onInspect = nullptr)
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

        // Image thumbnails (not video) get a hand cursor and click-to-inspect.
        if (onInspect && !MediaUtils::IsVideoFile(path)) {
            m_thumb->SetCursor(wxCursor(wxCURSOR_HAND));
            m_thumb->Bind(wxEVT_LEFT_DOWN, [this, onInspect](wxMouseEvent&) {
                onInspect(m_path, m_normalBmp.IsOk() ? m_normalBmp : m_thumb->GetBitmap());
            });
        }

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
    m_alive.reset(); // invalidate any weak_ptr guards held by overlay callbacks
    if (m_cancelToken)
        m_cancelToken->store(true, std::memory_order_relaxed);
    DeletePendingEvents();
    if (m_inspectOverlay) {
        m_inspectOverlay->Destroy();
        m_inspectOverlay = nullptr;
    }
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
    // Dismiss any open inspection overlay when the grid is being rebuilt.
    if (m_inspectOverlay) {
        m_inspectOverlay->Destroy();
        m_inspectOverlay = nullptr;
    }
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
            RemovableThumb::InspectCb inspectCb;
            if (!MediaUtils::IsVideoFile(path))
                inspectCb = [this](const wxString& p, const wxBitmap& bmp) { OpenInspect(p, bmp); };
            RemovableThumb* cell = new RemovableThumb(this, thumbSize, path,
                [this](const wxString& p) { OnItemRemove(p); },
                std::move(inspectCb));
            m_thumbUpdaters.push_back([cell](const wxBitmap& b) {
                cell->SetThumbBitmap(b);
            });
            grid->Add(cell, 0, wxALIGN_TOP | wxALIGN_CENTER_HORIZONTAL | wxALL, 4);
        }
    } else {
        // Plain mode: static cells
        wxBitmap placeholder = MakePlaceholder(thumbSize);
        for (const wxString& path : m_paths) {
            ThumbDisplay* thumb = new ThumbDisplay(this, thumbSize);
            thumb->SetBitmap(placeholder);
            m_thumbUpdaters.push_back([thumb](const wxBitmap& b) {
                thumb->SetBitmap(b);
            });

            // Images (not videos) are inspectable: hand cursor + click to inspect.
            if (!MediaUtils::IsVideoFile(path)) {
                thumb->SetCursor(wxCursor(wxCURSOR_HAND));
                thumb->Bind(wxEVT_LEFT_DOWN, [this, thumb, path](wxMouseEvent&) {
                    OpenInspect(path, thumb->GetBitmap());
                });
            }

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

void ThumbnailGrid::OpenInspect(const wxString& path, const wxBitmap& preview)
{
    if (m_inspectOverlay) return; // already open

    wxWindow* tlw = wxGetTopLevelParent(this);
    if (!tlw) return;

    auto* overlay = new ImageInspectOverlay(tlw, path, preview, m_alive);
    m_inspectOverlay = overlay;

    std::weak_ptr<bool> weakAlive = m_alive;
    overlay->SetNullifyCallback([this, weakAlive]() {
        if (weakAlive.lock())
            m_inspectOverlay = nullptr;
    });

    LOG_DEBUG("Inspect: opened for %s", path);
}
