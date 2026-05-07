#include "http_util.h"
#include <curl/curl.h>
#include <stdio.h>
#include <mutex>

static std::once_flag s_initOnce;
static void EnsureGlobalInit() {
    std::call_once(s_initOnce, []{ curl_global_init(CURL_GLOBAL_DEFAULT); });
}

static void ApplyCommonOpts(CURL* h, const char* url) {
    curl_easy_setopt(h, CURLOPT_URL, url);
    curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(h, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(h, CURLOPT_USERAGENT, "Theseus-Dashboard/1.0");
    curl_easy_setopt(h, CURLOPT_FAILONERROR, 1L);
}

static size_t WriteToString(void* ptr, size_t size, size_t nmemb, void* user) {
    ((std::string*)user)->append((char*)ptr, size * nmemb);
    return size * nmemb;
}

std::string Http_GetToString(const std::string& url) {
    EnsureGlobalInit();
    std::string out;
    CURL* h = curl_easy_init();
    if (!h) return out;
    ApplyCommonOpts(h, url.c_str());
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, WriteToString);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, &out);
    if (curl_easy_perform(h) != CURLE_OK) out.clear();
    curl_easy_cleanup(h);
    return out;
}

bool Http_GetToFile(const std::string& url, const std::string& outPath) {
    EnsureGlobalInit();
    FILE* fp = fopen(outPath.c_str(), "wb");
    if (!fp) return false;
    CURL* h = curl_easy_init();
    if (!h) { fclose(fp); remove(outPath.c_str()); return false; }
    ApplyCommonOpts(h, url.c_str());
    curl_easy_setopt(h, CURLOPT_WRITEDATA, fp);
    CURLcode rc = curl_easy_perform(h);
    curl_easy_cleanup(h);
    fclose(fp);
    if (rc != CURLE_OK) { remove(outPath.c_str()); return false; }
    return true;
}
