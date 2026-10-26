#ifndef ENCRYPTED_FILE_FORMAT_H
#define ENCRYPTED_FILE_FORMAT_H

#include <string>

// File extensions are ASCII and must select the same format in both editors.
inline bool has_txx_extension(const std::string &path)
{
    if (path.size() < 4) return false;
    const auto pos = path.size() - 4;
    return path[pos] == '.' &&
           (path[pos + 1] == 't' || path[pos + 1] == 'T') &&
           (path[pos + 2] == 'x' || path[pos + 2] == 'X') &&
           (path[pos + 3] == 'x' || path[pos + 3] == 'X');
}

#endif
