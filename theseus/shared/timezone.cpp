// timezone.cpp: the 75 time zones the clock settings list, in the order
// of the 5960 retail XBE's table at 0x000201f0 (0x1c bytes a zone). Each
// row: display name, DST flag, bias, standard and daylight bias in
// minutes, standard name and change date (month, day, weekday, hour), then
// the daylight name and change date. Generated from the binary's table;
// offsets, names and change rules are facts, so any correct copy matches.

#include "std.h"
#include "timezone.h"

#define D(m, d, w, h) { m, d, w, h }
#define NO_DATE       { 0, 0, 0, 0 }

const struct TZINFO g_timezoneinfo[] =
{
	{ _T("GMT-12"), 0, 720, 0, 0, L"IDLW", NO_DATE, NULL, NO_DATE },
	{ _T("GMT-11"), 0, 660, 0, 0, L"NT", NO_DATE, NULL, NO_DATE },
	{ _T("GMT-10"), 0, 600, 0, 0, L"HST", NO_DATE, NULL, NO_DATE },
	{ _T("GMT-09YDT"), 1, 540, 0, -60, L"YST", D(10, 5, 0, 2), L"YDT", D(4, 1, 0, 2) },
	{ _T("GMT-09PDT"), 1, 480, 0, -60, L"PST", D(10, 5, 0, 2), L"PDT", D(4, 1, 0, 2) },
	{ _T("GMT-07MST"), 0, 420, 0, 0, L"MST", NO_DATE, NULL, NO_DATE },
	{ _T("GMT-07MDT"), 1, 420, 0, -60, L"MST", D(10, 5, 0, 2), L"MST", D(4, 1, 0, 2) },
	{ _T("GMT-06CAST"), 0, 360, 0, 0, L"CAST", NO_DATE, NULL, NO_DATE },
	{ _T("GMT-06CDT"), 1, 360, 0, -60, L"CST", D(10, 5, 0, 2), L"CDT", D(4, 1, 0, 2) },
	{ _T("GMT-06MDT"), 1, 360, 0, -60, L"MST", D(10, 5, 0, 2), L"MDT", D(4, 1, 0, 2) },
	{ _T("GMT-06CCST"), 0, 360, 0, 0, L"CCST", NO_DATE, NULL, NO_DATE },
	{ _T("GMT-05EDT"), 1, 300, 0, -60, L"EST", D(10, 5, 0, 2), L"EDT", D(4, 1, 0, 2) },
	{ _T("GMT-05EST"), 0, 300, 0, 0, L"EST", NO_DATE, NULL, NO_DATE },
	{ _T("GMT-05SPST"), 0, 300, 0, 0, L"SPST", NO_DATE, NULL, NO_DATE },
	{ _T("GMT-04ADT"), 1, 240, 0, -60, L"AST", D(10, 5, 0, 2), L"ADT", D(4, 1, 0, 2) },
	{ _T("GMT-04SWST"), 0, 240, 0, 0, L"SWST", NO_DATE, NULL, NO_DATE },
	{ _T("GMT-04PSDT"), 1, 240, 0, -60, L"PSST", D(3, 2, 6, 0), L"PSDT", D(10, 2, 6, 0) },
	{ _T("GMT-03:30"), 1, 210, 0, -60, L"NST", D(10, 5, 0, 2), L"NDT", D(4, 1, 0, 2) },
	{ _T("GMT-03ESDT"), 1, 180, 0, -60, L"ESST", D(2, 2, 0, 2), L"ESDT", D(10, 3, 0, 2) },
	{ _T("GMT-03SEST"), 0, 180, 0, 0, L"SEST", NO_DATE, NULL, NO_DATE },
	{ _T("GMT-03GDT"), 1, 180, 0, -60, L"GST", D(10, 5, 0, 2), L"GDT", D(4, 1, 0, 2) },
	{ _T("GMT-02"), 1, 120, 0, -60, L"MAST", D(9, 5, 0, 2), L"MADT", D(3, 5, 0, 2) },
	{ _T("GMT-01ADT"), 1, 60, 0, -60, L"AST", D(10, 5, 0, 3), L"ADT", D(3, 5, 0, 2) },
	{ _T("GMT-01WAT"), 0, 60, 0, 0, L"WAT", NO_DATE, NULL, NO_DATE },
	{ _T("GMT+00GST"), 0, 0, 0, 0, L"GST", NO_DATE, NULL, NO_DATE },
	{ _T("GMT+00BST"), 1, 0, 0, -60, L"GMT", D(10, 5, 0, 2), L"BST", D(3, 5, 0, 1) },
	{ _T("GMT+01WEDT"), 1, -60, 0, -60, L"WEST", D(10, 5, 0, 3), L"WEDT", D(3, 5, 0, 2) },
	{ _T("GMT+01CEDT"), 1, -60, 0, -60, L"CEST", D(10, 5, 0, 3), L"CEDT", D(3, 5, 0, 2) },
	{ _T("GMT+01RDT"), 1, -60, 0, -60, L"RST", D(10, 5, 0, 3), L"RDT", D(3, 5, 0, 2) },
	{ _T("GMT+01SCDT"), 1, -60, 0, -60, L"SCST", D(10, 5, 0, 3), L"SCDT", D(3, 5, 0, 2) },
	{ _T("GMT+01WDST"), 0, -60, 0, 0, L"WAST", NO_DATE, NULL, NO_DATE },
	{ _T("GMT+02GTDT"), 1, -120, 0, -60, L"GTST", D(10, 5, 0, 3), L"GTDT", D(3, 5, 0, 2) },
	{ _T("GMT+02EEDT"), 1, -120, 0, -60, L"EEST", D(9, 5, 0, 1), L"EEDT", D(3, 5, 0, 0) },
	{ _T("GMT+02EDT"), 1, -120, 0, -60, L"EST", D(9, 5, 3, 2), L"EDT", D(5, 1, 5, 2) },
	{ _T("GMT+02SAST"), 0, -120, 0, 0, L"SAST", NO_DATE, NULL, NO_DATE },
	{ _T("GMT+02FLDT"), 1, -120, 0, -60, L"FLST", D(10, 5, 0, 4), L"FLDT", D(3, 5, 0, 3) },
	{ _T("GMT+02JST"), 0, -120, 0, 0, L"JST", NO_DATE, NULL, NO_DATE },
	{ _T("GMT+03ADT"), 1, -180, 0, -60, L"AST", D(10, 1, 0, 4), L"ADT", D(4, 1, 0, 3) },
	{ _T("GMT+03AST"), 0, -180, 0, 0, L"AST", NO_DATE, NULL, NO_DATE },
	{ _T("GMT+03RDT"), 1, -180, 0, -60, L"RST", D(10, 5, 0, 3), L"RDT", D(3, 5, 0, 2) },
	{ _T("GMT+03EAST"), 0, -180, 0, 0, L"EAST", NO_DATE, NULL, NO_DATE },
	{ _T("GMT+03:30"), 1, -210, 0, -60, L"IST", D(9, 4, 2, 2), L"IDT", D(3, 1, 0, 2) },
	{ _T("GMT+04AST"), 0, -240, 0, 0, L"AST", NO_DATE, NULL, NO_DATE },
	{ _T("GMT+04CDT"), 1, -240, 0, -60, L"CST", D(10, 5, 0, 3), L"CDT", D(3, 5, 0, 2) },
	{ _T("GMT+04:30"), 0, -270, 0, 0, L"AST", NO_DATE, NULL, NO_DATE },
	{ _T("GMT+05EDT"), 1, -300, 0, -60, L"EST", D(10, 5, 0, 3), L"EDT", D(3, 5, 0, 2) },
	{ _T("GMT+05WAST"), 0, -300, 0, 0, L"WAST", NO_DATE, NULL, NO_DATE },
	{ _T("GMT+05:30"), 0, -330, 0, 0, L"IST", NO_DATE, NULL, NO_DATE },
	{ _T("GMT+05:45"), 0, -345, 0, 0, L"NST", NO_DATE, NULL, NO_DATE },
	{ _T("GMT+06NCDT"), 1, -360, 0, -60, L"NCST", D(10, 5, 0, 3), L"NCDT", D(3, 5, 0, 2) },
	{ _T("GMT+06CAST"), 0, -360, 0, 0, L"CAST", NO_DATE, NULL, NO_DATE },
	{ _T("GMT+06SRST"), 0, -360, 0, 0, L"SRST", NO_DATE, NULL, NO_DATE },
	{ _T("GMT+06:30"), 0, -390, 0, 0, L"MST", NO_DATE, NULL, NO_DATE },
	{ _T("GMT+07SAST"), 0, -420, 0, 0, L"SAST", NO_DATE, NULL, NO_DATE },
	{ _T("GMT+07NADT"), 1, -420, 0, -60, L"NAST", D(10, 5, 0, 3), L"NADT", D(3, 5, 0, 2) },
	{ _T("GMT+08CST"), 0, -480, 0, 0, L"CST", NO_DATE, NULL, NO_DATE },
	{ _T("GMT+08NEDT"), 1, -480, 0, -60, L"NEST", D(10, 5, 0, 3), L"NEDT", D(3, 5, 0, 2) },
	{ _T("GMT+08MPST"), 0, -480, 0, 0, L"MPST", NO_DATE, NULL, NO_DATE },
	{ _T("GMT+08AWST"), 0, -480, 0, 0, L"AWST", NO_DATE, NULL, NO_DATE },
	{ _T("GMT+08TST"), 0, -480, 0, 0, L"TST", NO_DATE, NULL, NO_DATE },
	{ _T("GMT+09TST"), 0, -540, 0, 0, L"TST", NO_DATE, NULL, NO_DATE },
	{ _T("GMT+09KST"), 0, -540, 0, 0, L"KST", NO_DATE, NULL, NO_DATE },
	{ _T("GMT+09YDT"), 1, -540, 0, -60, L"YST", D(10, 5, 0, 3), L"YDT", D(3, 5, 0, 2) },
	{ _T("GMT+09:30ACDT"), 1, -570, 0, -60, L"ACST", D(3, 5, 0, 2), L"ACDT", D(10, 5, 0, 2) },
	{ _T("GMT+09:30ACST"), 0, -570, 0, 0, L"ACST", NO_DATE, NULL, NO_DATE },
	{ _T("GMT+10AEST"), 0, -600, 0, 0, L"AEST", NO_DATE, NULL, NO_DATE },
	{ _T("GMT+10AEDT"), 1, -600, 0, -60, L"AEST", D(3, 5, 0, 2), L"AEDT", D(10, 5, 0, 2) },
	{ _T("GMT+10WPST"), 0, -600, 0, 0, L"WPST", NO_DATE, NULL, NO_DATE },
	{ _T("GMT+10TDT"), 1, -600, 0, -60, L"TST", D(3, 5, 0, 2), L"TDT", D(10, 1, 0, 2) },
	{ _T("GMT+10VDT"), 1, -600, 0, -60, L"VST", D(10, 5, 0, 3), L"VDT", D(3, 5, 0, 2) },
	{ _T("GMT+11"), 0, -660, 0, 0, L"CPST", NO_DATE, NULL, NO_DATE },
	{ _T("GMT+12NZDT"), 1, -720, 0, -60, L"NZST", D(3, 3, 0, 2), L"NZDT", D(10, 1, 0, 2) },
	{ _T("GMT+12FST"), 0, -720, 0, 0, L"FST", NO_DATE, NULL, NO_DATE },
	{ _T("GMT+13"), 0, -780, 0, 0, L"TST", NO_DATE, NULL, NO_DATE },
	{ _T("GMT+14"), 0, -840, 0, 0, L"KST", NO_DATE, NULL, NO_DATE },
};

const int g_timezoneCount = sizeof(g_timezoneinfo) / sizeof(g_timezoneinfo[0]);
