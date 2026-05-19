#pragma once

// Shared libcurl wrappers.

#include <string>
#include <vector>

struct HttpHeader { std::string name, value; };
using HttpHeaders = std::vector<HttpHeader>;

struct HttpResponse {
    long status = 0;     // 0 on transport failure
    std::string body;
    bool ok() const { return status >= 200 && status < 300; }
};

// String/file GET shims. Empty/false on failure.
std::string Http_GetToString(const std::string& url);
bool        Http_GetToFile(const std::string& url, const std::string& outPath);

// Header + status-aware GET/POST. Non-2xx is NOT a failure; caller decides.
HttpResponse Http_Get(const std::string& url, const HttpHeaders& headers = {});
HttpResponse Http_Post(const std::string& url,
                       const std::string& body = "",
                       const HttpHeaders& headers = {});
