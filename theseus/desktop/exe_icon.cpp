// exe_icon.cpp: reads icons out of .ico files and Windows programs.
//
// An .ico is a list of pictures, each a PNG or a bottom-up bitmap with an
// AND mask under it. A program keeps the same pictures as RT_ICON resources
// and the list as an RT_GROUP_ICON, so we walk the PE resource tree, take
// the first group (the one Explorer shows) and decode its biggest picture.

#include "exe_icon.h"
#include "icon_compress.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <stdint.h>
#include <vector>

#include "stb_image.h"
#include "host.h"

namespace {

const int kMaxDim = 1024;
const uint32_t kRtIcon = 3, kRtGroupIcon = 14;

uint16_t U16(const unsigned char* p) { return (uint16_t)(p[0] | p[1] << 8); }
uint32_t U32(const unsigned char* p) { return (uint32_t)p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

// A file read in pieces, so a 2 GB game exe never lands in memory whole.
struct Reader {
	FILE* f;
	uint64_t size;
	bool Read(uint64_t off, void* dst, size_t n) const {
		if (off > size || n > size - off) return false;
		if (Host_Seek(f, (int64_t)off, SEEK_SET) < 0) return false;
		return fread(dst, 1, n, f) == n;
	}
};

// One bitmap picture (no file header), bottom-up, with its AND mask.
unsigned char* DecodeDib(const unsigned char* p, size_t size, int* outW, int* outH) {
	if (size < 40) return NULL;
	uint32_t hdr = U32(p);
	int32_t w = (int32_t)U32(p + 4), h2 = (int32_t)U32(p + 8);
	int bpp = U16(p + 14);
	uint32_t comp = U32(p + 16), used = U32(p + 32);
	if (hdr < 40 || hdr > size || w <= 0 || h2 <= 0 || w > kMaxDim) return NULL;
	int h = h2 / 2;
	if (h <= 0 || h > kMaxDim) return NULL;
	if (comp != 0 && !(comp == 3 && bpp == 32)) return NULL;
	if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 24 && bpp != 32) return NULL;

	size_t colors = bpp <= 8 ? (used ? used : (size_t)1 << bpp) : 0;
	if (colors > 256) return NULL;
	size_t palOff = hdr + (comp == 3 && hdr == 40 ? 12 : 0);
	size_t pixOff = palOff + colors * 4;
	size_t stride = (((size_t)w * bpp + 31) / 32) * 4;
	size_t maskStride = (((size_t)w + 31) / 32) * 4;
	if (pixOff + stride * h > size) return NULL;
	const unsigned char* pal = p + palOff;
	const unsigned char* pix = p + pixOff;
	// Some 32-bit pictures skip the mask; alpha carries it then.
	const unsigned char* mask = pixOff + stride * h + maskStride * h <= size ? pix + stride * h : NULL;

	unsigned char* out = (unsigned char*)malloc((size_t)w * h * 4);
	if (!out) return NULL;
	bool anyAlpha = false;
	for (int y = 0; y < h; y++) {
		const unsigned char* row = pix + stride * (h - 1 - y);
		unsigned char* o = out + (size_t)y * w * 4;
		for (int x = 0; x < w; x++, o += 4) {
			if (bpp == 32) {
				o[0] = row[x * 4 + 2]; o[1] = row[x * 4 + 1]; o[2] = row[x * 4 + 0]; o[3] = row[x * 4 + 3];
				if (o[3]) anyAlpha = true;
				continue;
			}
			if (bpp == 24) {
				o[0] = row[x * 3 + 2]; o[1] = row[x * 3 + 1]; o[2] = row[x * 3 + 0];
			} else {
				unsigned idx;
				if (bpp == 8)      idx = row[x];
				else if (bpp == 4) idx = (row[x / 2] >> (x & 1 ? 0 : 4)) & 15;
				else               idx = (row[x / 8] >> (7 - (x & 7))) & 1;
				if (idx >= colors) idx = 0;
				o[0] = pal[idx * 4 + 2]; o[1] = pal[idx * 4 + 1]; o[2] = pal[idx * 4 + 0];
			}
			o[3] = 255;
		}
	}
	if (mask && (bpp != 32 || !anyAlpha)) {
		for (int y = 0; y < h; y++) {
			const unsigned char* row = mask + maskStride * (h - 1 - y);
			for (int x = 0; x < w; x++) {
				bool clear = (row[x / 8] >> (7 - (x & 7))) & 1;
				out[((size_t)y * w + x) * 4 + 3] = clear ? 0 : 255;
			}
		}
	} else if (bpp == 32 && !anyAlpha) {
		for (size_t i = 0; i < (size_t)w * h; i++) out[i * 4 + 3] = 255;
	}
	*outW = w;
	*outH = h;
	return out;
}

// One picture from an icon list: PNG or bitmap.
unsigned char* DecodeImage(const unsigned char* p, size_t size, int* w, int* h) {
	static const unsigned char kPng[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
	if (size >= 8 && memcmp(p, kPng, 8) == 0) {
		int ch = 0;
		unsigned char* px = stbi_load_from_memory(p, (int)size, w, h, &ch, 4);
		if (!px) return NULL;
		// Hand back malloc'd memory like the bitmap path does.
		size_t n = (size_t)*w * *h * 4;
		unsigned char* out = (unsigned char*)malloc(n);
		if (out) memcpy(out, px, n);
		stbi_image_free(px);
		return out;
	}
	return DecodeDib(p, size, w, h);
}

// Keeps the biggest picture seen so far, deeper color breaking a tie.
struct Best {
	unsigned char* px = NULL;
	int w = 0, h = 0, rank = 0;
	void Offer(const unsigned char* data, size_t size, int bits) {
		int w2 = 0, h2 = 0;
		unsigned char* p = DecodeImage(data, size, &w2, &h2);
		if (!p) return;
		int rank2 = w2 * h2 * 64 + (bits > 63 ? 63 : bits);
		if (rank2 <= rank) { free(p); return; }
		free(px);
		px = p; w = w2; h = h2; rank = rank2;
	}
};

// A resource directory entry by id; the first entry when id is 0.
bool FindEntry(const Reader& r, uint64_t base, uint32_t dirOff, uint32_t id, uint32_t* entryData) {
	unsigned char d[16];
	if (!r.Read(base + dirOff, d, 16)) return false;
	unsigned count = (unsigned)U16(d + 12) + U16(d + 14);
	if (count > 4096) return false;
	for (unsigned i = 0; i < count; i++) {
		unsigned char e[8];
		if (!r.Read(base + dirOff + 16 + i * 8, e, 8)) return false;
		uint32_t name = U32(e);
		if (id == 0 || (!(name & 0x80000000u) && name == id)) {
			*entryData = U32(e + 4);
			return true;
		}
	}
	return false;
}

struct Pe {
	Reader r;
	uint64_t rsrc;          // file offset of the resource tree
	struct Sec { uint32_t va, vsize, raw, rawSize; };
	std::vector<Sec> secs;

	bool RvaToOffset(uint32_t rva, uint64_t* off) const {
		for (size_t i = 0; i < secs.size(); i++) {
			uint32_t span = secs[i].vsize > secs[i].rawSize ? secs[i].vsize : secs[i].rawSize;
			if (rva >= secs[i].va && rva - secs[i].va < span) {
				*off = (uint64_t)secs[i].raw + (rva - secs[i].va);
				return true;
			}
		}
		return false;
	}

	bool Open() {
		unsigned char mz[64];
		if (!r.Read(0, mz, 64) || mz[0] != 'M' || mz[1] != 'Z') return false;
		uint32_t peOff = U32(mz + 0x3c);
		unsigned char coff[24];
		if (!r.Read(peOff, coff, 24) || memcmp(coff, "PE\0\0", 4) != 0) return false;
		unsigned nsec = U16(coff + 6), optSize = U16(coff + 20);
		if (nsec == 0 || nsec > 96 || optSize < 96) return false;
		std::vector<unsigned char> opt(optSize);
		if (!r.Read(peOff + 24, &opt[0], optSize)) return false;
		uint16_t magic = U16(&opt[0]);
		size_t ddOff = magic == 0x20b ? 112 : magic == 0x10b ? 96 : 0;
		if (!ddOff || ddOff + 3 * 8 > optSize || U32(&opt[ddOff - 4]) < 3) return false;
		uint32_t rsrcRva = U32(&opt[ddOff + 2 * 8]);
		if (!rsrcRva) return false;
		for (unsigned i = 0; i < nsec; i++) {
			unsigned char s[40];
			if (!r.Read(peOff + 24 + optSize + i * 40, s, 40)) return false;
			Sec sec = { U32(s + 12), U32(s + 8), U32(s + 20), U32(s + 16) };
			secs.push_back(sec);
		}
		return RvaToOffset(rsrcRva, &rsrc);
	}

	// The data of resource type/id, first language. id 0 takes the first one.
	bool Resource(uint32_t type, uint32_t id, std::vector<unsigned char>& out) const {
		uint32_t e;
		if (!FindEntry(r, rsrc, 0, type, &e) || !(e & 0x80000000u)) return false;
		if (!FindEntry(r, rsrc, e & 0x7fffffffu, id, &e) || !(e & 0x80000000u)) return false;
		if (!FindEntry(r, rsrc, e & 0x7fffffffu, 0, &e) || (e & 0x80000000u)) return false;
		unsigned char data[8];
		if (!r.Read(rsrc + e, data, 8)) return false;
		uint32_t size = U32(data + 4);
		uint64_t off;
		if (size == 0 || size > (8u << 20) || !RvaToOffset(U32(data), &off)) return false;
		out.resize(size);
		return r.Read(off, &out[0], size);
	}
};

unsigned char* FromPe(FILE* f, uint64_t size, int* w, int* h) {
	Pe pe;
	pe.r.f = f;
	pe.r.size = size;
	std::vector<unsigned char> group;
	if (!pe.Open() || !pe.Resource(kRtGroupIcon, 0, group) || group.size() < 6) return NULL;
	unsigned count = U16(&group[4]);
	Best best;
	for (unsigned i = 0; i < count && 6 + (i + 1) * 14 <= group.size(); i++) {
		const unsigned char* e = &group[6 + i * 14];
		std::vector<unsigned char> img;
		if (pe.Resource(kRtIcon, U16(e + 12), img))
			best.Offer(&img[0], img.size(), U16(e + 6));
	}
	*w = best.w;
	*h = best.h;
	return best.px;
}

bool HasExt(const char* path, const char* ext) {
	size_t n = strlen(path), e = strlen(ext);
	return n > e && strcasecmp(path + n - e, ext) == 0;
}

} // namespace

bool ExeIcon_IsIconSource(const char* path) {
	return path && (HasExt(path, ".ico") || HasExt(path, ".exe") || HasExt(path, ".dll"));
}

unsigned char* ExeIcon_FromIco(const unsigned char* data, size_t size, int* w, int* h) {
	if (size < 6 || U16(data) != 0 || (U16(data + 2) != 1 && U16(data + 2) != 2)) return NULL;
	unsigned count = U16(data + 4);
	Best best;
	for (unsigned i = 0; i < count && 6 + (i + 1) * 16 <= size; i++) {
		const unsigned char* e = data + 6 + i * 16;
		uint32_t len = U32(e + 8), off = U32(e + 12);
		if (off < 6 || off > size || len > size - off) continue;
		best.Offer(data + off, len, U16(e + 6));
	}
	*w = best.w;
	*h = best.h;
	return best.px;
}

unsigned char* ExeIcon_Load(const char* path, int* w, int* h) {
	*w = *h = 0;
	FILE* f = fopen(path, "rb");
	if (!f) return NULL;
	unsigned char* px = NULL;
	HostFileInfo info;
	unsigned char magic[2];
	if (Host_StatOpenFile(f, &info) && fread(magic, 1, 2, f) == 2) {
		if (magic[0] == 'M' && magic[1] == 'Z') {
			px = FromPe(f, info.size, w, h);
		} else if (info.size >= 6 && info.size <= (16u << 20)) {
			std::vector<unsigned char> data((size_t)info.size);
			if (Host_Seek(f, 0, SEEK_SET) == 0 && fread(&data[0], 1, data.size(), f) == data.size())
				px = ExeIcon_FromIco(&data[0], data.size(), w, h);
		}
	}
	fclose(f);
	return px;
}

bool ExeIcon_Save(const char* path, const char* dst) {
	int w = 0, h = 0;
	unsigned char* px = ExeIcon_Load(path, &w, &h);
	if (!px) return false;
	bool ok = Icons_FitPixels(px, w, h, dst, 256, 90);
	free(px);
	return ok;
}
