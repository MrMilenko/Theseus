#pragma once

// Shared libcurl wrappers. One boilerplate, two output flavors.

#include <string>

// GET url into a string. Empty on failure or non-2xx.
std::string Http_GetToString(const std::string& url);

// GET url straight to disk. Removes partial file on failure.
bool Http_GetToFile(const std::string& url, const std::string& outPath);
