// log_redact.h: make a log safe to paste into a public report.
#pragma once
#include <string>

// The home folder becomes ~, IP addresses become [ip] (loopback and 0.0.0.0
// stay, they say nothing about anyone) and sign-in tokens become [redacted].
std::string Log_Redact(const std::string& text, const char* home);

// Access tokens and API keys in URLs, auth headers and JSON become [redacted].
std::string Log_RedactSecrets(const std::string& text);
