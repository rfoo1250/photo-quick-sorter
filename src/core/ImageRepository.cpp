#include "core/ImageRepository.h"

#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/textfile.h>
#include <wx/log.h>
#include <wx/filefn.h> // wxFileExists
#include <algorithm>

wxArrayString ImageRepository::SplitFileSpec(const wxString& filespec)
{
    wxArrayString parts;
    if (filespec.IsEmpty()) return parts;

    wxString spec = filespec;
    // split by ';'
    wxString item;
    size_t start = 0;
    for (size_t i = 0; i <= spec.length(); ++i) {
        if (i == spec.length() || spec[i] == ';') {
            item = spec.SubString(start, i == spec.length() ? i - 1 : (long)i - 1).Trim(true).Trim(false);
            if (!item.IsEmpty()) parts.Add(item);
            start = i + 1;
        }
    }
    return parts;
}

// Collect files matching singlePattern in folder; if recursive==true descend into subfolders
void ImageRepository::CollectFilesRecursive(const wxString& folder,
                                           wxArrayString& outFiles,
                                           const wxString& singlePattern,
                                           bool recursive)
{
    wxDir dir(folder);
    if (!dir.IsOpened()) return;

    wxString filename;
    bool hasFile = dir.GetFirst(&filename, singlePattern, wxDIR_FILES);
    while (hasFile) {
        outFiles.Add(folder + wxFILE_SEP_PATH + filename);
        hasFile = dir.GetNext(&filename);
    }

    if (recursive) {
        wxString sub;
        bool hasSub = dir.GetFirst(&sub, wxEmptyString, wxDIR_DIRS);
        while (hasSub) {
            // skip "." and ".."
            if (sub != "." && sub != "..") {
                wxString subPath = folder + wxFILE_SEP_PATH + sub;
                CollectFilesRecursive(subPath, outFiles, singlePattern, recursive);
            }
            hasSub = dir.GetNext(&sub);
        }
    }
}

bool ImageRepository::BuildFromFolder(const wxString& baseFolder,
                                      const wxString& filespec,
                                      bool recursive)
{
    Clear();

    if (baseFolder.IsEmpty()) {
        wxLogError("BuildFromFolder: baseFolder is empty");
        return false;
    }

    if (!wxDir::Exists(baseFolder)) {
        wxLogError("BuildFromFolder: baseFolder does not exist: %s", baseFolder);
        return false;
    }

    // default filespec
    wxString spec = filespec.IsEmpty() ? "*.jpg;*.jpeg;*.png;*.webp;*.heic" : filespec;

    // split into patterns and collect
    wxArrayString patterns = SplitFileSpec(spec);
    wxArrayString allFiles;
    if (patterns.IsEmpty()) {
        // fallback single pattern
        CollectFilesRecursive(baseFolder, allFiles, wxT("*.*"), recursive);
    } else {
        for (const auto& p : patterns) {
            CollectFilesRecursive(baseFolder, allFiles, p, recursive);
        }
    }

    // Remove duplicates and sort for deterministic order
    std::sort(allFiles.begin(), allFiles.end());
    allFiles.erase(std::unique(allFiles.begin(), allFiles.end()), allFiles.end());

    for (const auto& f : allFiles) {
        wxFileName fn(f);
        wxULongLong ull = fn.GetSize(); // returns wxULongLong
        wxUint64 size = static_cast<wxUint64>(ull.GetValue());

        wxDateTime modTime;
        // GetTimes returns bool; we'll try to get mtime; if not available leave invalid
        fn.GetTimes(nullptr, &modTime, nullptr);
        m_images.emplace_back(f, size, modTime);
    }

    wxLogMessage("ImageRepository: scanned '%s', found %zu image(s)", baseFolder, m_images.size());
    return true;
}

void ImageRepository::Clear()
{
    m_images.clear();
}

bool ImageRepository::SaveToFile(const wxString& savePath) const
{
    if (savePath.IsEmpty()) {
        wxLogError("SaveToFile: savePath is empty");
        return false;
    }

    wxTextFile file;
    if (wxFileExists(savePath)) {
        if (!file.Open(savePath)) {
            wxLogError("SaveToFile: failed to open file for writing: %s", savePath);
            return false;
        }
        file.Clear();
    } else {
        if (!file.Create(savePath)) {
            wxLogError("SaveToFile: failed to create file: %s", savePath);
            return false;
        }
    }

    for (const auto& info : m_images) {
        wxString t = info.modificationTime.IsValid() ? info.modificationTime.FormatISOCombined(' ') : "";
        // Escape tabs/newlines in path if you expect them; for now assume safe paths.
        wxString line = info.path + "\t" + wxString::Format("%llu", (unsigned long long)info.size) + "\t" + t;
        file.AddLine(line);
    }

    if (!file.Write()) {
        wxLogError("SaveToFile: write failed: %s", savePath);
        file.Close();
        return false;
    }

    file.Close();
    return true;
}

bool ImageRepository::LoadFromFile(const wxString& loadPath)
{
    Clear();

    if (!wxFileExists(loadPath)) {
        wxLogWarning("LoadFromFile: file does not exist: %s", loadPath);
        return false;
    }

    wxTextFile file;
    if (!file.Open(loadPath)) {
        wxLogError("LoadFromFile: cannot open file: %s", loadPath);
        return false;
    }

    for (size_t i = 0; i < file.GetLineCount(); ++i) {
        wxString line = file.GetLine(i);
        if (line.IsEmpty()) continue;

        wxArrayString parts = wxSplit(line, '\t');
        if (parts.size() < 1) continue;

        wxString path = parts[0];
        wxUint64 size = 0;
        if (parts.size() >= 2 && !parts[1].IsEmpty()) {
            unsigned long long tmp = 0;
            if (parts[1].ToULongLong(&tmp)) size = static_cast<wxUint64>(tmp);
        }

        wxDateTime modTime;
        if (parts.size() >= 3 && !parts[2].IsEmpty()) {
            // Try parsing ISO combined; different wx versions have different overloads.
            // Try the common parse functions; if none succeed, leave modTime invalid.
            modTime.ParseISOCombined(parts[2]);
            // if parsing fails, modTime remains invalid — acceptable
        }
        m_images.emplace_back(path, size, modTime);
    }

    file.Close();
    wxLogMessage("ImageRepository: loaded %zu image(s) from '%s'", m_images.size(), loadPath);
    return true;
}

const ImageInfo* ImageRepository::GetAt(size_t idx) const
{
    if (idx >= m_images.size()) return nullptr;
    return &m_images[idx];
}
ImageInfo* ImageRepository::GetAt(size_t idx)
{
    if (idx >= m_images.size()) return nullptr;
    return &m_images[idx];
}

void ImageRepository::Add(const ImageInfo& info)
{
    m_images.push_back(info);
}

bool ImageRepository::RemoveAt(size_t idx)
{
    if (idx >= m_images.size()) return false;
    m_images.erase(m_images.begin() + idx);
    return true;
}

ptrdiff_t ImageRepository::FindByPath(const wxString& path) const
{
    for (size_t i = 0; i < m_images.size(); ++i) {
        if (m_images[i].path == path) return static_cast<ptrdiff_t>(i);
    }
    return -1;
}
