#pragma once

#include <wx/string.h>
#include <wx/datetime.h>
#include <wx/arrstr.h>
#include <vector>
#include <cstddef>

struct ImageInfo {
    wxString path;
    wxUint64 size = 0;
    wxDateTime modificationTime;

    ImageInfo() = default;
    ImageInfo(const wxString& p, wxUint64 s, const wxDateTime& t)
        : path(p), size(s), modificationTime(t) {}
};

class ImageRepository {
public:
    ImageRepository() = default;

    // Scans folder for image files (uses safe recursive walker).
    // filespec: semicolon-separated patterns (e.g. "*.jpg;*.png"). If empty, defaults to common types.
    // recursive: whether to descend into subfolders.
    bool BuildFromFolder(const wxString& baseFolder,
                         const wxString& filespec = wxEmptyString,
                         bool recursive = true);

    // Persistence (TSV with path \t size \t iso-datetime)
    bool SaveToFile(const wxString& savePath) const;
    bool LoadFromFile(const wxString& loadPath);

    // Basic operations
    void Clear();
    size_t GetCount() const { return m_images.size(); }
    const ImageInfo* GetAt(size_t idx) const;
    ImageInfo* GetAt(size_t idx);

    void Add(const ImageInfo& info);
    bool RemoveAt(size_t idx);
    ptrdiff_t FindByPath(const wxString& path) const; // -1 if not found

private:
    std::vector<ImageInfo> m_images;

    // helper: splits a semicolon-separated filespec into individual patterns
    static wxArrayString SplitFileSpec(const wxString& filespec);

    // helper: recursively collect files (pattern supports single pattern)
    static void CollectFilesRecursive(const wxString& folder,
                                      wxArrayString& outFiles,
                                      const wxString& singlePattern,
                                      bool recursive);
};
