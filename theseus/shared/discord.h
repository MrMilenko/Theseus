// discord.h: Discord webhook relay. POSTs title-launch events to a
// user-configured relay endpoint. Reads enable / IP / port from
// Config.ini. Theseus-original, opt-in.

#pragma once

void InitDiscordConfig();
bool SendDiscordRelay(const char* title_id_hex, const char* relay_ip, int port);
bool SendDiscordRelayFromConfig(const char* title_id_hex);

bool IsDiscordRelayEnabled();
const char* GetDiscordRelayIP();
int GetDiscordRelayPort();

extern bool g_DiscordEnabled;
extern char g_DiscordIP[64];
extern int g_DiscordPort;

