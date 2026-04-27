#pragma once
#include <wx/wx.h>
#include <wx/scrolwin.h>
#include <vector>
#include <memory>
#include <atomic>
#include <functional>

// Scrollable 3-column thumbnail grid.
//
// Plain usage (preview only):
//   auto* grid = new ThumbnailGrid(parent, "Select a folder.");
//   grid->SetImages(paths);
//
// Interactive usage (review steps — each cell has a hover "Undo" overlay):
//   grid->SetRemoveCallback([](const wxString& removedPath) { ... });
//   grid->SetImages(paths);  // call AFTER SetRemoveCallback
//
// Images load on a background thread; gray placeholders appear immediately.
class ThumbnailGrid : public wxScrolledWindow {
public:
    explicit ThumbnailGrid(wxWindow* parent,
                           const wxString& emptyHint = "No images.");
    ~ThumbnailGrid();

    bool AcceptsFocusFromKeyboard() const override { return false; }

    // Replace displayed images. Empty vector shows the empty hint.
    void SetImages(const std::vector<wxString>& paths);

    // Change the text shown when the images list is empty.
    void SetEmptyHint(const wxString& hint);

    // Enable per-item removal. When set, each cell gets a hover "Undo" overlay.
    // The callback is called (on the main thread) with the removed path BEFORE
    // the grid rebuilds itself. Call this before SetImages.
    void SetRemoveCallback(std::function<void(const wxString&)> cb);

private:
    void Rebuild();
    void CancelPendingLoad();
    void OnItemRemove(const wxString& path);
    void OnSize(wxSizeEvent& evt);
    void OpenInspect(const wxString& path, const wxBitmap& preview);

    std::vector<wxString>  m_paths;
    wxString               m_emptyHint;

    std::shared_ptr<std::atomic<bool>> m_cancelToken;

    // Per-slot update function: called on the main thread when a background-loaded
    // bitmap is ready. Replaces the placeholder with the real thumbnail.
    std::vector<std::function<void(const wxBitmap&)>> m_thumbUpdaters;

    std::function<void(const wxString&)> m_removeCallback;

    // Inspection overlay — nullptr when no image is being inspected.
    wxWindow*              m_inspectOverlay = nullptr;
    // Lifetime guard: reset in destructor so overlay callbacks become no-ops.
    std::shared_ptr<bool>  m_alive = std::make_shared<bool>(true);
};
