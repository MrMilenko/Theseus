# File Utilities

The path and file helpers every part of the 5960 retail binary leans on: joining a folder and a
name, resolving script URLs against the current folder, turning a URL into a file path, and
counting blocks. Theseus's version is `theseus/shared/file_util.cpp`, compiled by both builds.

## Functions

| Address | Name | What it does |
|---------|------|--------------|
| 0x0002def0 | (create file) | `CreateFileA` on a narrowed path; the helpers below open files through it |
| 0x0002df60 | DoesFileExist | Narrows the path into 260 chars and returns whether `GetFileAttributesExA` succeeds |
| 0x0002dfc0 | MakePath | Copies the folder (unless it is already the output), adds `\` when the last character isn't one, appends the name |
| 0x0002e010 | CleanFilePath | Skips a leading `file:` (`_wcsnicmp`, 5), turns `/` into `\`, stops at `?`, narrows each character |
| 0x0002e060 | MakeAbsoluteURL2 | Resolves a URL against a base |
| 0x0002e170 | UpdateCurDirFromFile | Resolves a URL and keeps it as the current folder when absolute |
| 0x0002e200 | MakeAbsoluteURL | Current folder against the app folder, then the URL against that |
| 0x0002e260 | FindFilePath | The resolved path if a file is there, else app folder + name |
| 0x0002e2d0 | GetDirectoryBlocks | Recursive block count with a cancel event |
| 0x0002e520 | GetFileSize | Size of a file opened with all share flags and backup semantics, 0 if it won't open |

`0x0017a480` holds the current folder and `0x0017e828` points at the app folder.

## MakeAbsoluteURL2

Registers: `EAX` is the URL, `ECX` the base, `ESI` the output.

```
if (wcschr(url, ':') || (url[0] == '\\' && url[1] == '\\') || !base ||
    (!wcschr(base, ':') && !wcschr(base, '/')))
    { wcscpy(out, url); return; }
wcscpy(out, base);
p = wcsrchr(out, '/'); if (!p) p = wcsrchr(out, '\\');
while (url starts with "../" or "..\\") {
    url += 3; *p = 0;
    p = last '/' or '\\' in out; if none { p = out + wcslen(out) - 1; break; }
}
*p = '/'; wcscpy(p + 1, url);
```

A base with a drive but no separator at all (`Q:`) leaves `p` null, and the binary writes through
it. Theseus puts the URL over the last character in that case, as it does when `../` runs out of
folders.

## GetDirectoryBlocks

Returns -1 straight away when the cancel event is signaled. Builds `<dir>\*.*`, narrows it and calls
`FindFirstFileA`. When that fails the result is 1 if `GetLastError()` is `ERROR_FILE_NOT_FOUND` (an
empty folder still takes its own block) and 0 otherwise. The count starts at 1 for the folder; each
file adds `(size + block - 1) / block`; with recursion on, each subfolder whose name doesn't start
with `.` adds its own count. The cancel event is checked before every entry and stops the walk with
-1. The handle is closed with `CloseHandle`.

## CActiveFile

A file pulled into memory by URL: URL at +4, content at +8, size at +0xc, write time at +0x10 and
an in-XIP flag at +0x18.

| Address | Name | What it does |
|---------|------|--------------|
| 0x0002e560 | Reset | Frees the URL and the content (`LocalFree`) and zeroes size and time |
| 0x0002e5a0 | Fetch | Resets and copies the URL. Empty succeeds with nothing. `string:` (any case) makes the rest the content. A relative URL is resolved with MakeAbsoluteURL and tried in the XIPs; failing that, with the search flag set and no such file, it becomes app folder + URL. Then the XIPs again with the final URL, then FetchFile |
| 0x0002e750 | FetchFile | Opens for sequential reading; with the try flag set, gives up unless size + 1 MB of physical memory is free; reads it all, writes two zero bytes after it and takes the write time |
| 0x0002e840 | MakeUnicode | Content starting FF FE has the mark moved off; anything else is widened byte by byte (stopping at a zero byte), terminated, and the size becomes `(bytes + 1) * 2` |

Theseus also counts a host path starting with `/` as absolute on desktop, closes the file on the
memory check, and frees the new buffer when GetFileTime fails (5960 frees the old content there).
