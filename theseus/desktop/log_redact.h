// log_redact.h: make a log safe to paste into a public report.
#pragma once
#include <string>

// The home folder becomes ~ and IP addresses become [ip] (loopback and
// 0.0.0.0 stay, they say nothing about anyone).
std::string Log_Redact(const std::string& text, const char* home);
