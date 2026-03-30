#pragma once
#include <wx/wx.h>
#include <wx/scrolwin.h>
#include <wx/statbmp.h>
#include <vector>
#include <memory>
#include <atomic>

// Scrollable 3-column thumbnail grid.
//
// Usage:
//   auto* grid = new ThumbnailGrid(parent, "Select a folder to see images.");
//   grid->SetImages(paths);   // pass empty vector to show the hint
//
// Placeholder cells appear immediately; images load on a background thread
// and pop in one by one. Automatically recomputes thumb size on window resize.
class ThumbnailGrid : public wxScrolledWindow {
public:
    explicit ThumbnailGrid(wxWindow* parent,
                           const wxString& emptyHint = "No images.");
    ~ThumbnailGrid();

    // Replace displayed images. Empty vector shows the empty hint.
    void SetImages(const std::vector<wxString>& paths);

    // Change the text shown when images list is empty.
    void SetEmptyHint(const wxString& hint);

private:
    void Rebuild();
    void CancelPendingLoad();
    void OnSize(wxSizeEvent& evt);

    std::vector<wxString>  m_paths;
    wxString               m_emptyHint;

    // Cancellation token shared with the background thread.
    // Replaced on every Rebuild(); the thread holds its own shared_ptr copy.
    std::shared_ptr<std::atomic<bool>> m_cancelToken;

    // Raw pointers to the per-slot wxStaticBitmap placeholders (owned by wx).
    // Index matches m_paths. Cleared and repopulated on every Rebuild().
    std::vector<wxStaticBitmap*> m_thumbWidgets;
};
