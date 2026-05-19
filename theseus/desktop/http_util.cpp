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
}

static size_t WriteToString(void* ptr, size_t size, size_t nmemb, void* user) {
    ((std::string*)user)->append((char*)ptr, size * nmemb);
    return size * nmemb;
}

static size_t WriteToFile(void* ptr, size_t size, size_t nmemb, void* user) {
    return fwrite(ptr, size, nmemb, (FILE*)user);
}

// Build a curl_slist from our HttpHeaders. Caller frees with curl_slist_free_all.
static curl_slist* BuildSlist(const HttpHeaders& headers) {
    curl_slist* list = nullptr;
    for (const HttpHeader& h : headers) {
        std::string line = h.name + ": " + h.value;
        list = curl_slist_append(list, line.c_str());
    }
    return list;
}

HttpResponse Http_Get(const std::string& url, const HttpHeaders& headers) {
    EnsureGlobalInit();
    HttpResponse out;
    CURL* h = curl_easy_init();
    if (!h) return out;
    ApplyCommonOpts(h, url.c_str());
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, WriteToString);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, &out.body);
    curl_slist* slist = BuildSlist(headers);
    if (slist) curl_easy_setopt(h, CURLOPT_HTTPHEADER, slist);
    if (curl_easy_perform(h) == CURLE_OK) {
        curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &out.status);
    }
    if (slist) curl_slist_free_all(slist);
    curl_easy_cleanup(h);
    return out;
}

HttpResponse Http_Post(const std::string& url,
                       const std::string& body,
                       const HttpHeaders& headers) {
    EnsureGlobalInit();
    HttpResponse out;
    CURL* h = curl_easy_init();
    if (!h) return out;
    ApplyCommonOpts(h, url.c_str());
    curl_easy_setopt(h, CURLOPT_POST, 1L);
    curl_easy_setopt(h, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE, (long)body.size());
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, WriteToString);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, &out.body);
    curl_slist* slist = BuildSlist(headers);
    if (slist) curl_easy_setopt(h, CURLOPT_HTTPHEADER, slist);
    if (curl_easy_perform(h) == CURLE_OK) {
        curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &out.status);
    }
    if (slist) curl_slist_free_all(slist);
    curl_easy_cleanup(h);
    return out;
}

std::string Http_GetToString(const std::string& url) {
    HttpResponse r = Http_Get(url);
    if (!r.ok()) return "";
    return r.body;
}

bool Http_GetToFile(const std::string& url, const std::string& outPath) {
    EnsureGlobalInit();
    FILE* fp = fopen(outPath.c_str(), "wb");
    if (!fp) return false;
    CURL* h = curl_easy_init();
    if (!h) { fclose(fp); remove(outPath.c_str()); return false; }
    ApplyCommonOpts(h, url.c_str());
    curl_easy_setopt(h, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, WriteToFile);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, fp);
    CURLcode rc = curl_easy_perform(h);
    curl_easy_cleanup(h);
    fclose(fp);
    if (rc != CURLE_OK) { remove(outPath.c_str()); return false; }
    return true;
}
