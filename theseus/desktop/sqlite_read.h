// sqlite_read.h: reads one table out of an SQLite database file, enough for
// the stores that keep their library in one (Lutris, itch.io). Read only,
// plain file reads, no SQLite library. Committed pages in a -wal file next
// to it count, so a store that's running still reads current.

#pragma once

#include <map>
#include <string>
#include <vector>

typedef std::map<std::string, std::string> SqliteRow;

// Every row of table, column name to value as text. NULL and columns a row
// doesn't have come back empty. False when the file or table can't be read.
bool Sqlite_ReadTable(const char* dbPath, const char* table, std::vector<SqliteRow>& rows);
