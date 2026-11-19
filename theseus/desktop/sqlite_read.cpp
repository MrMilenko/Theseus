// sqlite_read.cpp: walks an SQLite table's b-tree straight off the file.
// Follows the published file format: 100 byte header, table b-tree pages,
// overflow chains, records of varint serial types. Anything that doesn't
// look right ends the read instead of guessing.

#include "sqlite_read.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <stdint.h>

namespace {

uint32_t Be16(const uint8_t* p) { return (uint32_t)p[0] << 8 | p[1]; }
uint32_t Be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

// Bytes used, 0 when it runs off the end.
int Varint(const uint8_t* p, const uint8_t* end, uint64_t* out)
{
	uint64_t v = 0;
	for (int i = 0; i < 9; i++) {
		if (p + i >= end) return 0;
		if (i == 8) { *out = (v << 8) | p[i]; return 9; }
		v = (v << 7) | (p[i] & 0x7f);
		if (!(p[i] & 0x80)) { *out = v; return i + 1; }
	}
	return 0;
}

struct Db {
	FILE* f = NULL;
	uint32_t pageSize = 0, usable = 0;
	std::map<uint32_t, std::vector<uint8_t> > wal;   // committed pages from the -wal file
	int pagesRead = 0;

	~Db() { if (f) fclose(f); }

	bool Page(uint32_t n, std::vector<uint8_t>& out)
	{
		// A loop in a corrupt file shouldn't spin forever.
		if (n == 0 || ++pagesRead > 2000000) return false;
		std::map<uint32_t, std::vector<uint8_t> >::iterator w = wal.find(n);
		if (w != wal.end()) { out = w->second; return true; }
		out.resize(pageSize);
		if (fseek(f, (long)(n - 1) * (long)pageSize, SEEK_SET) != 0) return false;
		return fread(&out[0], 1, pageSize, f) == pageSize;
	}
};

// The WAL checksum: pairs of 32 bit words, big or little endian as the magic says.
void WalSum(const uint8_t* p, size_t n, bool big, uint32_t* s0, uint32_t* s1)
{
	for (size_t i = 0; i + 8 <= n; i += 8) {
		uint32_t a = big ? Be32(p + i) : (uint32_t)p[i] | p[i + 1] << 8 | p[i + 2] << 16 | (uint32_t)p[i + 3] << 24;
		uint32_t b = big ? Be32(p + i + 4) : (uint32_t)p[i + 4] | p[i + 5] << 8 | p[i + 6] << 16 | (uint32_t)p[i + 7] << 24;
		*s0 += a + *s1;
		*s1 += b + *s0;
	}
}

void LoadWal(Db& db, const std::string& path)
{
	FILE* f = fopen(path.c_str(), "rb");
	if (!f) return;
	uint8_t hdr[32];
	if (fread(hdr, 1, 32, f) != 32) { fclose(f); return; }
	uint32_t magic = Be32(hdr);
	bool big = magic == 0x377f0683;
	if ((magic & 0xfffffffe) != 0x377f0682 || Be32(hdr + 8) != db.pageSize) { fclose(f); return; }
	uint32_t s0 = 0, s1 = 0;
	WalSum(hdr, 24, big, &s0, &s1);
	if (s0 != Be32(hdr + 24) || s1 != Be32(hdr + 28)) { fclose(f); return; }

	std::map<uint32_t, std::vector<uint8_t> > pending;
	std::vector<uint8_t> frame(24 + db.pageSize);
	while (fread(&frame[0], 1, frame.size(), f) == frame.size()) {
		// A frame from an older generation, or a torn write, ends the log.
		if (memcmp(&frame[8], hdr + 16, 8) != 0) break;
		WalSum(&frame[0], 8, big, &s0, &s1);
		WalSum(&frame[24], db.pageSize, big, &s0, &s1);
		if (s0 != Be32(&frame[16]) || s1 != Be32(&frame[20])) break;
		pending[Be32(&frame[0])].assign(frame.begin() + 24, frame.end());
		if (Be32(&frame[4]) != 0) {   // a commit
			for (std::map<uint32_t, std::vector<uint8_t> >::iterator it = pending.begin(); it != pending.end(); ++it)
				db.wal[it->first].swap(it->second);
			pending.clear();
		}
	}
	fclose(f);
}

// The cell's whole payload, overflow pages and all.
bool Payload(Db& db, const uint8_t* p, const uint8_t* end, uint64_t size, std::vector<uint8_t>& out)
{
	if (size > (16u << 20)) return false;
	uint64_t U = db.usable, X = U - 35, M = ((U - 12) * 32 / 255) - 23;
	uint64_t local = size;
	if (size > X) {
		uint64_t K = M + ((size - M) % (U - 4));
		local = K <= X ? K : M;
	}
	if (p + local > end) return false;
	out.assign(p, p + local);
	if (local == size) return true;
	if (p + local + 4 > end) return false;
	uint32_t next = Be32(p + local);
	std::vector<uint8_t> page;
	while (out.size() < size) {
		if (!db.Page(next, page)) return false;
		size_t take = (size_t)(size - out.size());
		if (take > U - 4) take = (size_t)(U - 4);
		out.insert(out.end(), page.begin() + 4, page.begin() + 4 + take);
		next = Be32(&page[0]);
	}
	return true;
}

struct Cell { int64_t rowid; std::vector<uint8_t> payload; };

bool Walk(Db& db, uint32_t pageNo, int depth, std::vector<Cell>& out)
{
	if (depth > 32) return false;
	std::vector<uint8_t> page;
	if (!db.Page(pageNo, page)) return false;
	size_t hdr = pageNo == 1 ? 100 : 0;
	const uint8_t* base = &page[0];
	const uint8_t* end = base + db.usable;
	uint8_t type = base[hdr];
	uint32_t cells = Be16(base + hdr + 3);
	if (hdr + 12 + cells * 2 > db.usable) return false;
	if (type == 0x0d) {
		for (uint32_t i = 0; i < cells; i++) {
			size_t at = Be16(base + hdr + 8 + i * 2);
			if (at >= db.usable) return false;
			const uint8_t* p = base + at;
			uint64_t size, rowid;
			int n = Varint(p, end, &size);
			if (!n) return false;
			p += n;
			n = Varint(p, end, &rowid);
			if (!n) return false;
			p += n;
			Cell c;
			c.rowid = (int64_t)rowid;
			if (!Payload(db, p, end, size, c.payload)) return false;
			out.push_back(c);
		}
		return true;
	}
	if (type != 0x05) return false;
	for (uint32_t i = 0; i < cells; i++) {
		size_t at = Be16(base + hdr + 12 + i * 2);
		if (at + 4 > db.usable) return false;
		if (!Walk(db, Be32(base + at), depth + 1, out)) return false;
	}
	return Walk(db, Be32(base + hdr + 8), depth + 1, out);
}

// One record's values as text, NULLs empty.
bool Record(const std::vector<uint8_t>& rec, std::vector<std::string>& vals, std::vector<bool>& null)
{
	vals.clear();
	null.clear();
	if (rec.empty()) return false;
	const uint8_t* p = &rec[0];
	const uint8_t* end = p + rec.size();
	uint64_t hsize;
	int n = Varint(p, end, &hsize);
	if (!n || hsize > rec.size()) return false;
	const uint8_t* h = p + n;
	const uint8_t* hend = p + hsize;
	const uint8_t* body = hend;
	while (h < hend) {
		uint64_t t;
		n = Varint(h, hend, &t);
		if (!n) return false;
		h += n;
		std::string v;
		size_t len = 0;
		bool isNull = t == 0;
		if (t >= 1 && t <= 6) {
			static const size_t kLen[] = { 0, 1, 2, 3, 4, 6, 8 };
			len = kLen[t];
			if (body + len > end) return false;
			int64_t x = (body[0] & 0x80) ? -1 : 0;
			for (size_t i = 0; i < len; i++) x = (int64_t)((uint64_t)x << 8 | body[i]);
			char buf[32];
			snprintf(buf, sizeof(buf), "%lld", (long long)x);
			v = buf;
		} else if (t == 7) {
			len = 8;
			if (body + len > end) return false;
			uint64_t bits = 0;
			for (int i = 0; i < 8; i++) bits = bits << 8 | body[i];
			double d;
			memcpy(&d, &bits, 8);
			char buf[64];
			snprintf(buf, sizeof(buf), "%.17g", d);
			v = buf;
		} else if (t == 8 || t == 9) {
			v = t == 8 ? "0" : "1";
		} else if (t >= 12) {
			len = (size_t)((t - 12) / 2);
			if (body + len > end) return false;
			v.assign((const char*)body, len);
		}
		body += len;
		vals.push_back(v);
		null.push_back(isNull);
	}
	return true;
}

std::string Upper(const std::string& s)
{
	std::string o = s;
	for (size_t i = 0; i < o.size(); i++) o[i] = (char)toupper((unsigned char)o[i]);
	return o;
}

std::string Unquote(const std::string& s)
{
	if (s.size() >= 2 && (s[0] == '"' || s[0] == '`' || s[0] == '[' || s[0] == '\''))
		return s.substr(1, s.size() - 2);
	return s;
}

// Column names from CREATE TABLE, and which one is the rowid in disguise.
bool Columns(const std::string& sql, std::vector<std::string>& cols, int* rowidCol)
{
	cols.clear();
	*rowidCol = -1;
	std::string up = Upper(sql);
	if (up.find("WITHOUT ROWID") != std::string::npos) return false;
	size_t open = sql.find('('), close = sql.rfind(')');
	if (open == std::string::npos || close == std::string::npos || close < open) return false;
	std::vector<std::string> defs;
	std::string cur;
	int depth = 0;
	char quote = 0;
	for (size_t i = open + 1; i < close; i++) {
		char c = sql[i];
		if (quote) { if (c == quote) quote = 0; cur += c; continue; }
		if (c == '"' || c == '`' || c == '\'') quote = c;
		else if (c == '[') quote = ']';
		else if (c == '(') depth++;
		else if (c == ')') depth--;
		else if (c == ',' && depth == 0) { defs.push_back(cur); cur.clear(); continue; }
		cur += c;
	}
	defs.push_back(cur);

	std::vector<std::string> types;
	std::string pkCol;
	for (size_t d = 0; d < defs.size(); d++) {
		std::string def = defs[d];
		size_t s = def.find_first_not_of(" \t\r\n");
		if (s == std::string::npos) continue;
		def = def.substr(s);
		std::string upDef = Upper(def);
		bool constraint = false;
		static const char* kTableBits[] = { "CONSTRAINT", "PRIMARY", "UNIQUE", "CHECK", "FOREIGN" };
		for (size_t k = 0; k < 5; k++)
			if (upDef.compare(0, strlen(kTableBits[k]), kTableBits[k]) == 0 &&
			    (upDef.size() == strlen(kTableBits[k]) || !isalnum((unsigned char)upDef[strlen(kTableBits[k])])))
				constraint = true;
		if (constraint) {
			// PRIMARY KEY ("id") on one INTEGER column makes it the rowid too.
			size_t pk = upDef.find("PRIMARY KEY");
			size_t a = def.find('(', pk == std::string::npos ? 0 : pk), b = def.find(')', a);
			if (pk != std::string::npos && a != std::string::npos && b != std::string::npos) {
				std::string inner = def.substr(a + 1, b - a - 1);
				if (inner.find(',') == std::string::npos) {
					size_t f = inner.find_first_not_of(" \t"), l = inner.find_last_not_of(" \t");
					if (f != std::string::npos) pkCol = Unquote(inner.substr(f, l - f + 1));
				}
			}
			continue;
		}
		std::string name;
		size_t rest;
		if (def[0] == '"' || def[0] == '`' || def[0] == '[') {
			char q = def[0] == '[' ? ']' : def[0];
			size_t e = def.find(q, 1);
			if (e == std::string::npos) return false;
			name = def.substr(1, e - 1);
			rest = e + 1;
		} else {
			size_t e = def.find_first_of(" \t\r\n");
			name = def.substr(0, e);
			rest = e == std::string::npos ? def.size() : e;
		}
		std::string tail = Upper(def.substr(rest));
		size_t t = tail.find_first_not_of(" \t\r\n");
		std::string type = t == std::string::npos ? "" : tail.substr(t, tail.find_first_of(" \t\r\n(", t) - t);
		if (type == "INTEGER" && tail.find("PRIMARY KEY") != std::string::npos) *rowidCol = (int)cols.size();
		cols.push_back(name);
		types.push_back(type);
	}
	if (*rowidCol < 0 && !pkCol.empty())
		for (size_t i = 0; i < cols.size(); i++)
			if (strcasecmp(cols[i].c_str(), pkCol.c_str()) == 0 && types[i] == "INTEGER") *rowidCol = (int)i;
	return !cols.empty();
}

} // namespace

bool Sqlite_ReadTable(const char* dbPath, const char* table, std::vector<SqliteRow>& rows)
{
	rows.clear();
	Db db;
	db.f = fopen(dbPath, "rb");
	if (!db.f) return false;
	uint8_t hdr[100];
	if (fread(hdr, 1, 100, db.f) != 100 || memcmp(hdr, "SQLite format 3", 16) != 0) return false;
	db.pageSize = Be16(hdr + 16);
	if (db.pageSize == 1) db.pageSize = 65536;
	if (db.pageSize < 512 || (db.pageSize & (db.pageSize - 1))) return false;
	if (hdr[20] >= db.pageSize - 480) return false;
	db.usable = db.pageSize - hdr[20];
	if (Be32(hdr + 56) > 1) return false;   // UTF-16 text, not something we'll meet
	LoadWal(db, std::string(dbPath) + "-wal");

	std::vector<Cell> master;
	if (!Walk(db, 1, 0, master)) return false;
	uint32_t root = 0;
	std::string sql;
	std::vector<std::string> vals;
	std::vector<bool> null;
	for (size_t i = 0; i < master.size(); i++) {
		if (!Record(master[i].payload, vals, null) || vals.size() < 5) continue;
		if (vals[0] != "table" || strcasecmp(vals[1].c_str(), table) != 0) continue;
		root = (uint32_t)strtoul(vals[3].c_str(), NULL, 10);
		sql = vals[4];
		break;
	}
	std::vector<std::string> cols;
	int rowidCol;
	if (!root || !Columns(sql, cols, &rowidCol)) return false;

	std::vector<Cell> cells;
	if (!Walk(db, root, 0, cells)) return false;
	for (size_t i = 0; i < cells.size(); i++) {
		if (!Record(cells[i].payload, vals, null)) continue;
		SqliteRow row;
		for (size_t c = 0; c < cols.size(); c++) {
			if ((int)c == rowidCol) {
				char buf[32];
				snprintf(buf, sizeof(buf), "%lld", (long long)cells[i].rowid);
				row[cols[c]] = buf;
			} else {
				row[cols[c]] = c < vals.size() ? vals[c] : "";
			}
		}
		rows.push_back(row);
	}
	return true;
}
