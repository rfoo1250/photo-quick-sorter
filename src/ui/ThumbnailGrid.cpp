#include "ui/ThumbnailGrid.h"
#include <wx/filename.h>
#include <wx/filefn.h>
#include <algorithm>
#include <thread>

static const int THUMB_PADDING = 12;

// Create a solid-gray bitmap used as a loading placeholder.
static wxBitmap MakePlaceholder(int size)
{
    wxImage img(size, size, true); // true = zero-fill (black)
    img.SetRGB(wxRect(0, 0, size, size), 180, 180, 180);
    return wxBitmap(img);
}

// ─────────────────────────────────────────────
// Construction / destruction
// ─────────────────────────────────────────────

ThumbnailGrid::ThumbnailGrid(wxWindow* parent, const wxString& emptyHint)
    : wxScrolledWindow(parent, wxID_ANY,
                       wxDefaultPosition, wxDefaultSize, wxVSCROLL)
    , m_emptyHint(emptyHint)
{
    SetScrollRate(0, 20);
    Bind(wxEVT_SIZE, &ThumbnailGrid::OnSize, this);
    Rebuild(); // show initial hint
}

ThumbnailGrid::~ThumbnailGrid()
{
    // Signal any background thread to stop — it will exit at its next
    // cancellation check and will not call CallAfter on a dead 'this'.
    if (m_cancelToken)
        m_cancelToken->store(true, std::memory_order_relaxed);

    // Drop any already-queued CallAfter lambdas for this handler so they
    // are never executed after 'this' is destroyed.
    DeletePendingEvents();
}

// ─────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────

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

// ─────────────────────────────────────────────
// Private
// ─────────────────────────────────────────────

void ThumbnailGrid::CancelPendingLoad()
{
    if (m_cancelToken)
        m_cancelToken->store(true, std::memory_order_relaxed);
    // Reset our shared_ptr. The thread holds its own copy, keeping the
    // atomic alive until the thread exits naturally.
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
    // Cancel any in-flight background load before touching child widgets.
    CancelPendingLoad();

    Freeze();
    DestroyChildren();
    m_thumbWidgets.clear();

    // ── Empty state ──────────────────────────────────────────────────────
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

    // ── Compute thumb size ───────────────────────────────────────────────
    int scrollW = GetClientSize().x;
    if (scrollW < 60) scrollW = 400;
    int thumbSize = (int)((scrollW * 0.92 - THUMB_PADDING * 4) / 3);
    if (thumbSize < 60) thumbSize = 60;

    // ── Build placeholder grid (shown immediately) ───────────────────────
    wxBitmap placeholder = MakePlaceholder(thumbSize);
    wxFlexGridSizer* grid = new wxFlexGridSizer(0, 3, THUMB_PADDING, THUMB_PADDING);
    m_thumbWidgets.reserve(m_paths.size());

    for (const wxString& path : m_paths) {
        wxStaticBitmap* thumb = new wxStaticBitmap(this, wxID_ANY, placeholder);
        thumb->SetMinSize(wxSize(thumbSize, thumbSize));
        m_thumbWidgets.push_back(thumb); // store by index for background updates

        wxStaticText* lbl = new wxStaticText(this, wxID_ANY,
            wxFileName(path).GetFullName(),
            wxDefaultPosition, wxSize(thumbSize, -1),
            wxALIGN_CENTER | wxST_ELLIPSIZE_END);

        wxBoxSizer* cell = new wxBoxSizer(wxVERTICAL);
        cell->Add(thumb, 0, wxALIGN_CENTER | wxBOTTOM, 3);
        cell->Add(lbl,   0, wxALIGN_CENTER);
        grid->Add(cell, 0, wxALIGN_TOP | wxALIGN_CENTER_HORIZONTAL | wxALL, 4);
    }

    SetSizer(grid);
    FitInside();
    Thaw(); // ← placeholder grid is visible to the user NOW, before any I/O

    // ── Kick off background loading ──────────────────────────────────────
    m_cancelToken = std::make_shared<std::atomic<bool>>(false);

    // Capture everything by value — no references into member state.
    std::vector<wxString>              paths = m_paths; // deep copy
    int                                tSize = thumbSize;
    std::shared_ptr<std::atomic<bool>> token = m_cancelToken;

    std::thread([this, paths = std::move(paths), tSize, token]()
    {
        for (size_t i = 0; i < paths.size(); ++i)
        {
            if (token->load(std::memory_order_relaxed)) return;

            wxImage img;
            if (wxFileExists(paths[i]))
            {
                // Hint to the JPEG decoder to use DCT scaling: libjpeg will
                // decode at 1/8, 1/4, or 1/2 resolution (whichever covers
                // tSize), dramatically reducing CPU and I/O for camera photos.
                // Non-JPEG handlers silently ignore these options.
                img.SetOption(wxIMAGE_OPTION_MAX_WIDTH,  tSize);
                img.SetOption(wxIMAGE_OPTION_MAX_HEIGHT, tSize);
                img.LoadFile(paths[i], wxBITMAP_TYPE_ANY);
            }

            if (img.IsOk())
            {
                double sx = (double)tSize / img.GetWidth();
                double sy = (double)tSize / img.GetHeight();
                double s  = std::min(sx, sy);
                img = img.Scale((int)(img.GetWidth()  * s),
                                (int)(img.GetHeight() * s),
                                wxIMAGE_QUALITY_NORMAL);
            }

            if (token->load(std::memory_order_relaxed)) return;

            // Move the wxImage into a shared_ptr — cheap to capture in the lambda.
            // wxBitmap construction MUST happen on the main thread (GDI object).
            auto imgPtr = std::make_shared<wxImage>(std::move(img));

            // CallAfter is thread-safe: posts a wxCallAfterEvent to the app queue.
            CallAfter([this, token, i, imgPtr]()
            {
                if (token->load(std::memory_order_relaxed)) return;
                if (i >= m_thumbWidgets.size() || !m_thumbWidgets[i]) return;
                if (imgPtr->IsOk())
                    m_thumbWidgets[i]->SetBitmap(wxBitmap(*imgPtr));
            });
        }
    }).detach();
}
