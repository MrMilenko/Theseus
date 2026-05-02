// xbox_hdd.cpp: read-only parser for Xbox HDD images (qcow2 + FATX).
// Two modes of operation:
//   1. qcow2 mode: reads xemu's virtual HDD image file, parses FATX from it.
//   2. Host filesystem mode: reads TDATA / UDATA from a directory on the host.
//      (e.g., the user extracts their Xbox HDD contents to a folder)
//
// The XboxHDD class abstracts both. SavedGameGrid doesn't care where the
// data comes from; it just calls EnumerateTitles() and gets back title info.

#include "xbox_hdd.h"

#include <algorithm>
#include <cstdlib>
#include <ctime>

// Case-insensitive strcmp: MSVC has _stricmp, POSIX has strcasecmp
#if defined(_MSC_VER)
    #define fatx_stricmp _stricmp
#else
    #include <strings.h>
    #define fatx_stricmp strcasecmp
#endif

// ============================================================================
// Byte-swap helpers (qcow2 is big-endian)
// ============================================================================

static inline uint32_t bswap32(uint32_t v) {
    return ((v & 0xFF000000) >> 24) |
           ((v & 0x00FF0000) >> 8)  |
           ((v & 0x0000FF00) << 8)  |
           ((v & 0x000000FF) << 24);
}

static inline uint64_t bswap64(uint64_t v) {
    return ((uint64_t)bswap32((uint32_t)v) << 32) |
           (uint64_t)bswap32((uint32_t)(v >> 32));
}

// ============================================================================
// Qcow2Reader
// ============================================================================

Qcow2Reader::Qcow2Reader()
    : m_file(nullptr), m_clusterSize(0), m_l2Bits(0), m_l2Entries(0), m_writable(false)
{
    memset(&m_header, 0, sizeof(m_header));
}

Qcow2Reader::~Qcow2Reader() {
    Close();
}

bool Qcow2Reader::OpenReadWrite(const char* path) {
    Close();
    m_file = fopen(path, "r+b");
    if (!m_file) {
        fprintf(stderr, "[Qcow2] fopen r+b failed: '%s' (errno %d), trying read-only\n", path, errno);
        return Open(path);
    }
    m_writable = true;

    // Parse header (same as Open)
    uint8_t hdr[104];
    if (fread(hdr, 1, sizeof(hdr), m_file) != sizeof(hdr)) { Close(); return false; }
    uint32_t magic; memcpy(&magic, hdr + 0, 4); magic = bswap32(magic);
    if (magic != 0x514649FB) { Close(); return false; }
    m_header.magic = magic;
    memcpy(&m_header.version, hdr + 4, 4); m_header.version = bswap32(m_header.version);
    memcpy(&m_header.cluster_bits, hdr + 20, 4); m_header.cluster_bits = bswap32(m_header.cluster_bits);
    memcpy(&m_header.disk_size, hdr + 24, 8); m_header.disk_size = bswap64(m_header.disk_size);
    memcpy(&m_header.l1_size, hdr + 36, 4); m_header.l1_size = bswap32(m_header.l1_size);
    memcpy(&m_header.l1_table_offset, hdr + 40, 8); m_header.l1_table_offset = bswap64(m_header.l1_table_offset);
    m_clusterSize = 1u << m_header.cluster_bits;
    m_l2Bits = m_header.cluster_bits - 3;
    m_l2Entries = m_clusterSize / 8;
    if (m_header.l1_size == 0 || m_header.l1_size > 65536) { Close(); return false; }
    m_l1Table.resize(m_header.l1_size);
    fseek(m_file, (long)m_header.l1_table_offset, SEEK_SET);
    for (uint32_t i = 0; i < m_header.l1_size; i++) {
        uint64_t entry; if (fread(&entry, 8, 1, m_file) != 1) { Close(); return false; }
        m_l1Table[i] = bswap64(entry);
    }
    return true;
}

bool Qcow2Reader::Open(const char* path) {
    Close();

    m_file = fopen(path, "rb");
    if (!m_file) {
        fprintf(stderr, "[Qcow2] fopen failed: '%s' (errno %d)\n", path, errno);
        return false;
    }
    m_writable = false;

    // Read and parse header (big-endian)
    uint8_t hdr[104];
    if (fread(hdr, 1, sizeof(hdr), m_file) != sizeof(hdr)) {
        Close();
        return false;
    }

    // Unpack fields manually (big-endian)
    uint32_t magic;
    memcpy(&magic, hdr + 0, 4);
    magic = bswap32(magic);
    if (magic != 0x514649FB) {  // "QFI\xFB"
        Close();
        return false;
    }

    m_header.magic = magic;
    memcpy(&m_header.version, hdr + 4, 4);
    m_header.version = bswap32(m_header.version);

    memcpy(&m_header.cluster_bits, hdr + 20, 4);
    m_header.cluster_bits = bswap32(m_header.cluster_bits);

    memcpy(&m_header.disk_size, hdr + 24, 8);
    m_header.disk_size = bswap64(m_header.disk_size);

    memcpy(&m_header.l1_size, hdr + 36, 4);
    m_header.l1_size = bswap32(m_header.l1_size);

    memcpy(&m_header.l1_table_offset, hdr + 40, 8);
    m_header.l1_table_offset = bswap64(m_header.l1_table_offset);

    m_clusterSize = 1u << m_header.cluster_bits;
    m_l2Bits = m_header.cluster_bits - 3;  // 8 bytes per L2 entry
    m_l2Entries = m_clusterSize / 8;

    // Read L1 table
    if (m_header.l1_size == 0 || m_header.l1_size > 65536) {
        Close();
        return false;
    }

    m_l1Table.resize(m_header.l1_size);
    fseek(m_file, (long)m_header.l1_table_offset, SEEK_SET);
    for (uint32_t i = 0; i < m_header.l1_size; i++) {
        uint64_t entry;
        if (fread(&entry, 8, 1, m_file) != 1) {
            Close();
            return false;
        }
        m_l1Table[i] = bswap64(entry);
    }

    return true;
}

void Qcow2Reader::Close() {
    if (m_file) {
        fclose(m_file);
        m_file = nullptr;
    }
    m_l1Table.clear();
    m_writable = false;
    memset(&m_header, 0, sizeof(m_header));
}

uint64_t Qcow2Reader::ResolveVirtual(uint64_t virtualOffset,
                                       uint32_t* outL1Idx, uint32_t* outL2Idx,
                                       uint32_t* outInCluster) {
    uint32_t l1Idx = (uint32_t)(virtualOffset >> (m_l2Bits + m_header.cluster_bits));
    uint32_t l2Idx = (uint32_t)((virtualOffset >> m_header.cluster_bits) & ((1u << m_l2Bits) - 1));
    uint32_t inCluster = (uint32_t)(virtualOffset & (m_clusterSize - 1));

    if (outL1Idx) *outL1Idx = l1Idx;
    if (outL2Idx) *outL2Idx = l2Idx;
    if (outInCluster) *outInCluster = inCluster;

    if (l1Idx >= m_l1Table.size() || m_l1Table[l1Idx] == 0)
        return 0;

    uint64_t l2Phys = m_l1Table[l1Idx] & 0x00FFFFFFFFFFFE00ULL;
    fseek(m_file, (long)(l2Phys + l2Idx * 8), SEEK_SET);
    uint64_t l2Entry;
    if (fread(&l2Entry, 8, 1, m_file) != 1)
        return 0;
    l2Entry = bswap64(l2Entry);
    uint64_t clusterPhys = l2Entry & 0x00FFFFFFFFFFFE00ULL;
    return clusterPhys;  // 0 if unallocated
}

// Update the refcount for a physical cluster offset. Increments refcount by 1.
bool Qcow2Reader::UpdateRefcount(uint64_t physOffset) {
    if (!m_file || !m_writable)
        return false;

    uint32_t clusterIdx = (uint32_t)(physOffset / m_clusterSize);

    // Read refcount table offset from header (stored at offset 48, big-endian)
    uint8_t hdrBuf[8];
    fseek(m_file, 48, SEEK_SET);
    if (fread(hdrBuf, 1, 8, m_file) != 8) return false;
    uint64_t refcountTableOffset;
    memcpy(&refcountTableOffset, hdrBuf, 8);
    refcountTableOffset = bswap64(refcountTableOffset);

    // 16-bit refcounts (refcount_order=4, default for qcow2 v3)
    // Each refcount block holds clusterSize/2 entries
    uint32_t entriesPerBlock = m_clusterSize / 2;
    uint32_t blockIdx = clusterIdx / entriesPerBlock;
    uint32_t entryIdx = clusterIdx % entriesPerBlock;

    // Read the refcount table entry for this block
    fseek(m_file, (long)(refcountTableOffset + blockIdx * 8), SEEK_SET);
    uint64_t blockOffset;
    if (fread(&blockOffset, 8, 1, m_file) != 1) return false;
    blockOffset = bswap64(blockOffset);

    if (blockOffset == 0) {
        // Need to allocate a new refcount block. This is the tricky recursive case.
        // For simplicity, append a zeroed cluster and update the refcount table.
        fseek(m_file, 0, SEEK_END);
        long endPos = ftell(m_file);
        blockOffset = ((uint64_t)endPos + m_clusterSize - 1) & ~((uint64_t)m_clusterSize - 1);

        std::vector<uint8_t> zeros(m_clusterSize, 0);
        fseek(m_file, (long)blockOffset, SEEK_SET);
        fwrite(zeros.data(), 1, m_clusterSize, m_file);

        // Write the block offset to the refcount table (big-endian)
        uint64_t beBlockOffset = bswap64(blockOffset);
        fseek(m_file, (long)(refcountTableOffset + blockIdx * 8), SEEK_SET);
        fwrite(&beBlockOffset, 8, 1, m_file);

        // Set refcount=1 for the refcount block itself (big-endian)
        uint32_t selfIdx = (uint32_t)(blockOffset / m_clusterSize);
        if (selfIdx / entriesPerBlock == blockIdx) {
            uint32_t selfEntry = selfIdx % entriesPerBlock;
            uint16_t rc = 0x0100;  // 1 in BE16
            fseek(m_file, (long)(blockOffset + selfEntry * 2), SEEK_SET);
            fwrite(&rc, 2, 1, m_file);
        }
    }

    // Read current refcount (big-endian 16-bit), increment, write back
    fseek(m_file, (long)(blockOffset + entryIdx * 2), SEEK_SET);
    uint16_t refcountBE = 0;
    fread(&refcountBE, 2, 1, m_file);
    // Swap to native, increment, swap back
    uint16_t refcount = (uint16_t)((refcountBE >> 8) | (refcountBE << 8));
    refcount++;
    refcountBE = (uint16_t)((refcount >> 8) | (refcount << 8));
    fseek(m_file, (long)(blockOffset + entryIdx * 2), SEEK_SET);
    fwrite(&refcountBE, 2, 1, m_file);

    return true;
}

// Allocate a new qcow2 cluster at the end of the physical file.
// Returns the physical offset of the new cluster, or 0 on failure.
uint64_t Qcow2Reader::AllocCluster() {
    if (!m_file || !m_writable)
        return 0;

    // Find current end of file
    fseek(m_file, 0, SEEK_END);
    long endPos = ftell(m_file);

    // Align to cluster boundary
    uint64_t newCluster = ((uint64_t)endPos + m_clusterSize - 1) & ~((uint64_t)m_clusterSize - 1);

    // Extend file: write zeros for the new cluster
    std::vector<uint8_t> zeros(m_clusterSize, 0);
    fseek(m_file, (long)newCluster, SEEK_SET);
    if (fwrite(zeros.data(), 1, m_clusterSize, m_file) != m_clusterSize)
        return 0;

    // Update refcount for the new cluster
    UpdateRefcount(newCluster);

    return newCluster;
}

// Ensure an L2 table exists for the given L1 index, allocating if needed.
// Returns the physical offset of the L2 table, or 0 on failure.
uint64_t Qcow2Reader::EnsureL2Table(uint32_t l1Idx) {
    if (l1Idx >= m_l1Table.size())
        return 0;

    if (m_l1Table[l1Idx] != 0)
        return m_l1Table[l1Idx] & 0x00FFFFFFFFFFFE00ULL;

    // Allocate a new L2 table cluster (zeroed = all entries unallocated)
    uint64_t l2Phys = AllocCluster();
    if (l2Phys == 0)
        return 0;

    // Update L1 table entry in memory and on disk
    // Set OFLAG_COPIED bit (bit 63) to indicate this L2 table is owned by us
    m_l1Table[l1Idx] = l2Phys | (1ULL << 63);
    uint64_t l1Entry = bswap64(m_l1Table[l1Idx]);
    fseek(m_file, (long)(m_header.l1_table_offset + l1Idx * 8), SEEK_SET);
    fwrite(&l1Entry, 8, 1, m_file);
    fflush(m_file);

    fprintf(stderr, "[Qcow2] Allocated L2 table for L1[%u] at physical 0x%llX\n",
            l1Idx, (unsigned long long)l2Phys);
    return l2Phys;
}

bool Qcow2Reader::WriteVirtual(uint64_t virtualOffset, const void* data, size_t size) {
    if (!m_file || !m_writable || virtualOffset >= m_header.disk_size)
        return false;

    const uint8_t* src = (const uint8_t*)data;
    uint64_t pos = virtualOffset;
    size_t remaining = size;

    while (remaining > 0) {
        uint32_t l1Idx, l2Idx, inCluster;
        uint64_t clusterPhys = ResolveVirtual(pos, &l1Idx, &l2Idx, &inCluster);
        size_t chunk = std::min(remaining, (size_t)(m_clusterSize - inCluster));

        if (clusterPhys == 0) {
            // Cluster not allocated. Allocate it now.
            uint64_t l2Phys = EnsureL2Table(l1Idx);
            if (l2Phys == 0) {
                fprintf(stderr, "[Qcow2] Failed to allocate L2 table\n");
                return false;
            }

            // Allocate a new data cluster
            clusterPhys = AllocCluster();
            if (clusterPhys == 0) {
                fprintf(stderr, "[Qcow2] Failed to allocate data cluster\n");
                return false;
            }

            // Update L2 entry to point to new cluster (with OFLAG_COPIED bit 63 set)
            uint64_t l2Entry = bswap64(clusterPhys | (1ULL << 63));
            fseek(m_file, (long)(l2Phys + l2Idx * 8), SEEK_SET);
            fwrite(&l2Entry, 8, 1, m_file);

            // cluster allocated silently (was debug spam)
        }

        fseek(m_file, (long)(clusterPhys + inCluster), SEEK_SET);
        if (fwrite(src, 1, chunk, m_file) != chunk)
            return false;

        src += chunk;
        pos += chunk;
        remaining -= chunk;
    }

    fflush(m_file);
    return true;
}

size_t Qcow2Reader::ReadVirtual(uint64_t virtualOffset, void* buffer, size_t size) {
    if (!m_file || virtualOffset >= m_header.disk_size)
        return 0;

    // Clamp to disk size
    if (virtualOffset + size > m_header.disk_size)
        size = (size_t)(m_header.disk_size - virtualOffset);

    uint8_t* out = (uint8_t*)buffer;
    size_t totalRead = 0;
    uint64_t pos = virtualOffset;
    size_t remaining = size;

    while (remaining > 0) {
        uint32_t l1Idx = (uint32_t)(pos >> (m_l2Bits + m_header.cluster_bits));
        uint32_t l2Idx = (uint32_t)((pos >> m_header.cluster_bits) & ((1u << m_l2Bits) - 1));
        uint32_t inCluster = (uint32_t)(pos & (m_clusterSize - 1));
        size_t chunk = std::min(remaining, (size_t)(m_clusterSize - inCluster));

        if (l1Idx >= m_l1Table.size() || m_l1Table[l1Idx] == 0) {
            // Unallocated L1 entry: reads as zeros
            memset(out, 0, chunk);
        } else {
            // Read L2 entry
            uint64_t l2Phys = m_l1Table[l1Idx] & 0x00FFFFFFFFFFFE00ULL;
            fseek(m_file, (long)(l2Phys + l2Idx * 8), SEEK_SET);
            uint64_t l2Entry;
            if (fread(&l2Entry, 8, 1, m_file) != 1) {
                memset(out, 0, chunk);
            } else {
                l2Entry = bswap64(l2Entry);
                uint64_t clusterPhys = l2Entry & 0x00FFFFFFFFFFFE00ULL;

                if (clusterPhys == 0) {
                    // Unallocated cluster: reads as zeros
                    memset(out, 0, chunk);
                } else {
                    // Read from physical cluster
                    fseek(m_file, (long)(clusterPhys + inCluster), SEEK_SET);
                    size_t rd = fread(out, 1, chunk, m_file);
                    if (rd < chunk)
                        memset(out + rd, 0, chunk - rd);
                }
            }
        }

        out += chunk;
        pos += chunk;
        remaining -= chunk;
        totalRead += chunk;
    }

    return totalRead;
}

// ============================================================================
// FATXReader
// ============================================================================

FATXReader::FATXReader()
    : m_qcow2(nullptr), m_mounted(false)
{
    memset(&m_partition, 0, sizeof(m_partition));
}

FATXReader::~FATXReader() {
    Unmount();
}

bool FATXReader::Mount(Qcow2Reader* qcow2, uint64_t partitionOffset, uint64_t partitionSize) {
    Unmount();

    if (!qcow2 || !qcow2->IsOpen())
        return false;

    m_qcow2 = qcow2;

    // Read FATX volume header
    FATXVolumeHeader hdr;
    if (m_qcow2->ReadVirtual(partitionOffset, &hdr, sizeof(hdr)) != sizeof(hdr))
        return false;

    // Check magic (little-endian on disk)
    if (hdr.magic != FATX_MAGIC)
        return false;

    m_partition.offset = partitionOffset;
    m_partition.size = partitionSize;
    m_partition.clusterSize = hdr.sectorsPerCluster * 512;
    m_partition.volumeID = hdr.volumeID;

    if (m_partition.clusterSize == 0 || m_partition.clusterSize > (1 << 20))
        return false;  // sanity check

    // Calculate FAT size and cluster count using mborgerson/fatx algorithm:
    //   fat_entries = partition_size / bytes_per_cluster + RESERVED (1)
    //   FAT16 if fat_entries < 0xFFF0, else FAT32
    //   fat_size = fat_entries * entry_size, rounded up to 4096
    //   num_clusters = (partition_size - fat_size - superblock) / bytes_per_cluster + RESERVED
    uint32_t fatEntries = (uint32_t)(partitionSize / m_partition.clusterSize) + 1;

    if (fatEntries < 0xFFF0) {
        m_partition.fatEntrySize = 2;
    } else {
        m_partition.fatEntrySize = 4;
    }

    uint32_t fatSize = fatEntries * m_partition.fatEntrySize;
    if (fatSize % 4096)
        fatSize += 4096 - (fatSize % 4096);

    // FAT starts immediately after the superblock (4096 bytes)
    m_partition.fatOffset = partitionOffset + FATX_HEADER_SIZE;

    // Data clusters start after FAT
    m_partition.dataOffset = m_partition.fatOffset + fatSize;

    // Actual usable cluster count
    m_partition.totalClusters = (uint32_t)((partitionSize - fatSize - FATX_HEADER_SIZE)
                                           / m_partition.clusterSize) + 1;
    m_partition.valid = true;

    fprintf(stderr, "[FATX] Mounted: offset=0x%llX clusters=%u fatSize=%u dataOffset=0x%llX\n",
            (unsigned long long)partitionOffset, m_partition.totalClusters,
            fatSize, (unsigned long long)m_partition.dataOffset);

    // Read the full FAT into memory
    m_fatData.resize(fatSize);
    m_qcow2->ReadVirtual(m_partition.fatOffset, m_fatData.data(), fatSize);

    // Debug: check FAT after load
    {
        uint32_t freeCount = 0, ffCount = 0, usedCount = 0;
        for (uint32_t i = 2; i < m_partition.totalClusters; i++) {
            uint32_t val = ReadFATEntry(i);
            if (val == 0) freeCount++;
            else if (val >= 0xFFF0) ffCount++;
            else usedCount++;
        }
        if (freeCount == 0 && usedCount > 0)
            fprintf(stderr, "[FATX] WARNING: partition appears full (%u chain, %u EOF)\n", usedCount, ffCount);
    }

    m_mounted = true;
    return true;
}

void FATXReader::Unmount() {
    m_qcow2 = nullptr;
    m_fatData.clear();
    memset(&m_partition, 0, sizeof(m_partition));
    m_mounted = false;
}

uint64_t FATXReader::ClusterToOffset(uint32_t cluster) const {
    // Cluster numbering starts at 1
    return m_partition.dataOffset + (uint64_t)(cluster - 1) * m_partition.clusterSize;
}

uint32_t FATXReader::ReadFATEntry(uint32_t cluster) {
    uint32_t offset = cluster * m_partition.fatEntrySize;
    if (offset + m_partition.fatEntrySize > m_fatData.size())
        return (m_partition.fatEntrySize == 2) ? FATX_FAT16_EOF : FATX_FAT32_EOF;

    if (m_partition.fatEntrySize == 2) {
        uint16_t val;
        memcpy(&val, m_fatData.data() + offset, 2);
        return val;
    } else {
        uint32_t val;
        memcpy(&val, m_fatData.data() + offset, 4);
        return val;
    }
}

std::vector<uint32_t> FATXReader::GetClusterChain(uint32_t startCluster) {
    std::vector<uint32_t> chain;
    uint32_t current = startCluster;
    uint32_t eof = (m_partition.fatEntrySize == 2) ? FATX_FAT16_EOF : FATX_FAT32_EOF;

    while (current != 0 && current < m_partition.totalClusters) {
        chain.push_back(current);
        uint32_t next = ReadFATEntry(current);
        if (next >= (eof & 0xFFFFFFF0))  // any special value = end
            break;
        current = next;
        if (chain.size() > 100000)  // safety limit
            break;
    }

    return chain;
}

std::vector<FATXDirEntry> FATXReader::ReadDirectory(uint32_t cluster) {
    std::vector<FATXDirEntry> entries;

    if (!m_mounted || cluster == 0)
        return entries;

    // Follow the FAT chain to read multi-cluster directories
    auto chain = GetClusterChain(cluster);
    std::vector<uint8_t> data;
    for (uint32_t cl : chain) {
        size_t prevSize = data.size();
        data.resize(prevSize + m_partition.clusterSize);
        m_qcow2->ReadVirtual(ClusterToOffset(cl), data.data() + prevSize, m_partition.clusterSize);
    }

    // Parse directory entries from all clusters
    uint32_t entryCount = (uint32_t)(data.size() / 64);

    for (uint32_t i = 0; i < entryCount; i++) {
        const uint8_t* raw = data.data() + i * 64;
        uint8_t nameLen = raw[0];

        // Skip unused entries
        if (nameLen == 0xFF || nameLen == 0x00 || nameLen == 0xE5)
            continue;

        // Valid name length: 1-42
        if (nameLen < 1 || nameLen > 42)
            continue;

        uint8_t attr = raw[1];

        // Validate: name should be printable ASCII
        bool validName = true;
        for (int j = 0; j < nameLen; j++) {
            if (raw[2 + j] < 0x20 || raw[2 + j] > 0x7E) {
                validName = false;
                break;
            }
        }
        if (!validName)
            continue;

        FATXDirEntry entry;
        memset(&entry, 0, sizeof(entry));
        memcpy(entry.name, raw + 2, nameLen);
        entry.name[nameLen] = '\0';
        entry.attributes = attr;
        memcpy(&entry.firstCluster, raw + 44, 4);
        memcpy(&entry.fileSize, raw + 48, 4);
        memcpy(&entry.modifiedTime, raw + 52, 2);
        memcpy(&entry.modifiedDate, raw + 54, 2);
        memcpy(&entry.createdTime, raw + 56, 2);
        memcpy(&entry.createdDate, raw + 58, 2);

        // Sanity check cluster number
        if (entry.firstCluster > 0 && entry.firstCluster < m_partition.totalClusters)
            entries.push_back(entry);
    }

    return entries;
}

std::vector<uint8_t> FATXReader::ReadFile(uint32_t firstCluster, uint32_t fileSize) {
    std::vector<uint8_t> result;

    if (!m_mounted || firstCluster == 0 || fileSize == 0)
        return result;

    auto chain = GetClusterChain(firstCluster);
    result.reserve(fileSize);

    uint32_t remaining = fileSize;
    for (uint32_t cluster : chain) {
        uint32_t toRead = std::min(remaining, m_partition.clusterSize);
        size_t prevSize = result.size();
        result.resize(prevSize + toRead);
        m_qcow2->ReadVirtual(ClusterToOffset(cluster), result.data() + prevSize, toRead);
        remaining -= toRead;
        if (remaining == 0)
            break;
    }

    return result;
}

// ============================================================================
// FATXReader — Write Support
// ============================================================================

bool FATXReader::WriteFATEntry(uint32_t cluster, uint32_t val) {
    uint32_t offset = cluster * m_partition.fatEntrySize;
    if (offset + m_partition.fatEntrySize > m_fatData.size())
        return false;
    if (m_partition.fatEntrySize == 2) {
        uint16_t v16 = (uint16_t)val;
        memcpy(m_fatData.data() + offset, &v16, 2);
    } else {
        memcpy(m_fatData.data() + offset, &val, 4);
    }
    return true;
}

uint32_t FATXReader::AllocateCluster() {
    uint32_t eof = (m_partition.fatEntrySize == 2) ? FATX_FAT16_EOF : FATX_FAT32_EOF;
    // Start searching from cluster 2 (cluster 1 is root dir)
    for (uint32_t i = 2; i < m_partition.totalClusters; i++) {
        if (ReadFATEntry(i) == 0) {
            WriteFATEntry(i, eof);
            return i;
        }
    }
    // Debug: dump FAT entries around where free should be
    fprintf(stderr, "[FATX] AllocateCluster: no free clusters in %u entries! FAT[0..7]:",
            m_partition.totalClusters);
    for (int i = 0; i < 8 && i < (int)m_partition.totalClusters; i++)
        fprintf(stderr, " 0x%X", ReadFATEntry(i));
    fprintf(stderr, "\n");
    fprintf(stderr, "[FATX] FAT[258..265]:");
    for (int i = 258; i < 266 && i < (int)m_partition.totalClusters; i++)
        fprintf(stderr, " 0x%X", ReadFATEntry(i));
    fprintf(stderr, "\n");
    fprintf(stderr, "[FATX] FAT data size: %zu bytes, fatEntrySize: %u\n",
            m_fatData.size(), m_partition.fatEntrySize);
    // Check raw bytes at entry 260
    uint32_t rawOff = 260 * m_partition.fatEntrySize;
    if (rawOff + 16 <= m_fatData.size()) {
        fprintf(stderr, "[FATX] Raw bytes at entry 260: ");
        for (int j = 0; j < 16; j++) fprintf(stderr, "%02X ", m_fatData[rawOff + j]);
        fprintf(stderr, "\n");
    }
    return 0;  // disk full
}

bool FATXReader::FreeClusterChain(uint32_t startCluster) {
    uint32_t eof = (m_partition.fatEntrySize == 2) ? FATX_FAT16_EOF : FATX_FAT32_EOF;
    uint32_t current = startCluster;
    while (current != 0 && current < m_partition.totalClusters) {
        uint32_t next = ReadFATEntry(current);
        WriteFATEntry(current, 0);
        if (next >= (eof & 0xFFFFFFF0))
            break;
        current = next;
    }
    return true;
}

bool FATXReader::FlushFAT() {
    if (!m_mounted || !m_qcow2 || !m_qcow2->IsWritable())
        return false;
    return m_qcow2->WriteVirtual(m_partition.fatOffset, m_fatData.data(), m_fatData.size());
}

bool FATXReader::WriteCluster(uint32_t cluster, const void* data, uint32_t size) {
    if (!m_mounted || !m_qcow2 || !m_qcow2->IsWritable())
        return false;
    uint32_t toWrite = std::min(size, m_partition.clusterSize);
    return m_qcow2->WriteVirtual(ClusterToOffset(cluster), data, toWrite);
}

int FATXReader::FindFreeDirSlot(uint32_t dirCluster) {
    // Read the directory cluster
    std::vector<uint8_t> data(m_partition.clusterSize);
    m_qcow2->ReadVirtual(ClusterToOffset(dirCluster), data.data(), m_partition.clusterSize);

    uint32_t entryCount = m_partition.clusterSize / 64;
    for (uint32_t i = 0; i < entryCount; i++) {
        uint8_t nameLen = data[i * 64];
        if (nameLen == 0xFF || nameLen == 0x00 || nameLen == 0xE5) {
            return (int)(i * 64);
        }
    }
    return -1;  // directory cluster is full
}

bool FATXReader::WriteDirEntry(uint32_t dirCluster, int slotOffset, const FATXDirEntry& entry) {
    if (!m_qcow2 || !m_qcow2->IsWritable())
        return false;

    // Build the 64-byte on-disk directory entry
    uint8_t raw[64];
    memset(raw, 0xFF, 64);
    uint8_t nameLen = (uint8_t)strlen(entry.name);
    raw[0] = nameLen;
    raw[1] = entry.attributes;
    memcpy(raw + 2, entry.name, nameLen);
    // Pad remaining name bytes with 0xFF
    for (int i = nameLen; i < 42; i++)
        raw[2 + i] = 0xFF;
    memcpy(raw + 44, &entry.firstCluster, 4);
    memcpy(raw + 48, &entry.fileSize, 4);
    memcpy(raw + 52, &entry.modifiedTime, 2);
    memcpy(raw + 54, &entry.modifiedDate, 2);
    memcpy(raw + 56, &entry.createdTime, 2);
    memcpy(raw + 58, &entry.createdDate, 2);
    // Access time at offset 60-63
    memcpy(raw + 60, &entry.createdTime, 2);
    memcpy(raw + 62, &entry.createdDate, 2);

    uint64_t writeOffset = ClusterToOffset(dirCluster) + slotOffset;
    return m_qcow2->WriteVirtual(writeOffset, raw, 64);
}

// Get current DOS-style date/time
static void GetFATXTimestamp(uint16_t& outTime, uint16_t& outDate) {
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    // FATX epoch is year 2000 (not 1980 like DOS FAT)
    outTime = (uint16_t)((t->tm_hour << 11) | (t->tm_min << 5) | (t->tm_sec / 2));
    outDate = (uint16_t)(((t->tm_year + 1900 - 2000) << 9) | ((t->tm_mon + 1) << 5) | t->tm_mday);
}

bool FATXReader::CreateFile(uint32_t parentDirCluster, const char* name,
                             const void* data, uint32_t size) {
    if (!m_mounted || !m_qcow2 || !m_qcow2->IsWritable())
        return false;

    // Validate name
    size_t nameLen = strlen(name);
    if (nameLen < 1 || nameLen > 42)
        return false;

    // Check for duplicate name
    auto existing = ReadDirectory(parentDirCluster);
    for (const auto& e : existing) {
        if (fatx_stricmp(e.name, name) == 0) {
            fprintf(stderr, "[FATX] CreateFile: '%s' already exists, skipping\n", name);
            return false;
        }
    }

    // Allocate clusters for file data
    uint32_t clustersNeeded = (size + m_partition.clusterSize - 1) / m_partition.clusterSize;
    if (clustersNeeded == 0) clustersNeeded = 1;

    fprintf(stderr, "[FATX] CreateFile: '%s' (%u bytes, %u clusters needed)\n",
            name, size, clustersNeeded);

    std::vector<uint32_t> chain;
    for (uint32_t i = 0; i < clustersNeeded; i++) {
        uint32_t c = AllocateCluster();
        if (c == 0) {
            fprintf(stderr, "[FATX] CreateFile: disk full after %zu clusters\n", chain.size());
            for (uint32_t prev : chain) WriteFATEntry(prev, 0);
            return false;
        }
        chain.push_back(c);
    }

    // Link the chain
    uint32_t eof = (m_partition.fatEntrySize == 2) ? FATX_FAT16_EOF : FATX_FAT32_EOF;
    for (size_t i = 0; i < chain.size() - 1; i++)
        WriteFATEntry(chain[i], chain[i + 1]);
    WriteFATEntry(chain.back(), eof);

    // Write file data
    const uint8_t* src = (const uint8_t*)data;
    uint32_t remaining = size;
    for (uint32_t cluster : chain) {
        uint32_t toWrite = std::min(remaining, m_partition.clusterSize);
        if (!WriteCluster(cluster, src, toWrite)) {
            fprintf(stderr, "[FATX] CreateFile: WriteCluster failed at cluster %u\n", cluster);
            return false;
        }
        src += toWrite;
        remaining -= toWrite;
    }

    // Find a free slot in the parent directory
    int slot = FindFreeDirSlot(parentDirCluster);
    if (slot < 0) {
        fprintf(stderr, "[FATX] Directory cluster full, cannot create '%s'\n", name);
        return false;
    }

    // Create directory entry
    FATXDirEntry entry;
    memset(&entry, 0, sizeof(entry));
    strncpy(entry.name, name, 42);
    entry.attributes = FATX_ATTR_ARCHIVE;
    entry.firstCluster = chain[0];
    entry.fileSize = size;
    GetFATXTimestamp(entry.createdTime, entry.createdDate);
    entry.modifiedTime = entry.createdTime;
    entry.modifiedDate = entry.createdDate;

    if (!WriteDirEntry(parentDirCluster, slot, entry))
        return false;

    return FlushFAT();
}

bool FATXReader::CreateDirectory(uint32_t parentDirCluster, const char* name) {
    if (!m_mounted || !m_qcow2 || !m_qcow2->IsWritable())
        return false;

    size_t nameLen = strlen(name);
    if (nameLen < 1 || nameLen > 42)
        return false;

    // Check for duplicate name
    auto existing = ReadDirectory(parentDirCluster);
    for (const auto& e : existing) {
        if (fatx_stricmp(e.name, name) == 0) {
            fprintf(stderr, "[FATX] CreateDirectory: '%s' already exists, skipping\n", name);
            return false;
        }
    }

    // Allocate one cluster for the new directory
    uint32_t dirCluster = AllocateCluster();
    if (dirCluster == 0) return false;

    // Zero-fill the new directory cluster (all 0xFF = end-of-dir markers)
    std::vector<uint8_t> emptyDir(m_partition.clusterSize, 0xFF);
    if (!WriteCluster(dirCluster, emptyDir.data(), m_partition.clusterSize)) {
        WriteFATEntry(dirCluster, 0);
        return false;
    }

    // Find free slot in parent
    int slot = FindFreeDirSlot(parentDirCluster);
    if (slot < 0) {
        WriteFATEntry(dirCluster, 0);
        return false;
    }

    // Create directory entry
    FATXDirEntry entry;
    memset(&entry, 0, sizeof(entry));
    strncpy(entry.name, name, 42);
    entry.attributes = FATX_ATTR_DIRECTORY;
    entry.firstCluster = dirCluster;
    entry.fileSize = 0;
    GetFATXTimestamp(entry.createdTime, entry.createdDate);
    entry.modifiedTime = entry.createdTime;
    entry.modifiedDate = entry.createdDate;

    if (!WriteDirEntry(parentDirCluster, slot, entry))
        return false;

    return FlushFAT();
}

// Recursively free all clusters used by a directory and its contents
void FATXReader::DeleteDirectoryContents(uint32_t dirCluster) {
    auto entries = ReadDirectory(dirCluster);
    for (const auto& e : entries) {
        if (e.firstCluster == 0 || e.firstCluster >= m_partition.totalClusters)
            continue;
        if (e.IsDirectory()) {
            // Recurse into subdirectory first
            DeleteDirectoryContents(e.firstCluster);
        }
        // Free this entry's cluster chain
        FreeClusterChain(e.firstCluster);
    }
}

bool FATXReader::DeleteEntry(uint32_t parentDirCluster, const char* name) {
    if (!m_mounted || !m_qcow2 || !m_qcow2->IsWritable())
        return false;

    // Read directory to find the entry
    std::vector<uint8_t> data(m_partition.clusterSize);
    m_qcow2->ReadVirtual(ClusterToOffset(parentDirCluster), data.data(), m_partition.clusterSize);

    uint32_t entryCount = m_partition.clusterSize / 64;
    for (uint32_t i = 0; i < entryCount; i++) {
        const uint8_t* raw = data.data() + i * 64;
        uint8_t nameLen = raw[0];
        if (nameLen < 1 || nameLen > 42) continue;

        char entryName[43];
        memcpy(entryName, raw + 2, nameLen);
        entryName[nameLen] = '\0';

        if (fatx_stricmp(entryName, name) == 0) {
            uint8_t attr = raw[1];
            uint32_t firstCluster;
            memcpy(&firstCluster, raw + 44, 4);

            if (firstCluster > 0 && firstCluster < m_partition.totalClusters) {
                // If it's a directory, recursively free all contents first
                if (attr & FATX_ATTR_DIRECTORY)
                    DeleteDirectoryContents(firstCluster);

                // Free the entry's own cluster chain
                FreeClusterChain(firstCluster);
            }

            // Mark entry as deleted (0xE5)
            uint8_t marker = 0xE5;
            uint64_t entryOffset = ClusterToOffset(parentDirCluster) + i * 64;
            m_qcow2->WriteVirtual(entryOffset, &marker, 1);

            return FlushFAT();
        }
    }
    return false;  // entry not found
}

bool FATXReader::RenameEntry(uint32_t parentDirCluster, const char* oldName, const char* newName) {
    if (!m_mounted || !m_qcow2 || !m_qcow2->IsWritable())
        return false;

    size_t newLen = strlen(newName);
    if (newLen < 1 || newLen > 42)
        return false;

    std::vector<uint8_t> data(m_partition.clusterSize);
    m_qcow2->ReadVirtual(ClusterToOffset(parentDirCluster), data.data(), m_partition.clusterSize);

    uint32_t entryCount = m_partition.clusterSize / 64;
    for (uint32_t i = 0; i < entryCount; i++) {
        uint8_t* raw = data.data() + i * 64;
        uint8_t nameLen = raw[0];
        if (nameLen < 1 || nameLen > 42) continue;

        char entryName[43];
        memcpy(entryName, raw + 2, nameLen);
        entryName[nameLen] = '\0';

        if (fatx_stricmp(entryName, oldName) == 0) {
            // Update name length and name bytes
            raw[0] = (uint8_t)newLen;
            memcpy(raw + 2, newName, newLen);
            // Pad remaining with 0xFF
            for (size_t j = newLen; j < 42; j++)
                raw[2 + j] = 0xFF;

            // Write the modified entry back
            uint64_t entryOffset = ClusterToOffset(parentDirCluster) + i * 64;
            return m_qcow2->WriteVirtual(entryOffset, raw, 64);
        }
    }
    return false;
}

// ============================================================================
// XboxHDD
// ============================================================================

bool XboxHDD::Create(const char* qcow2Path, uint64_t diskSize) {
    FILE* f = fopen(qcow2Path, "wb");
    if (!f) {
        fprintf(stderr, "[HDD] Create: cannot open '%s' for writing\n", qcow2Path);
        return false;
    }

    // qcow2 v3 header
    uint32_t clusterBits = 16;
    uint32_t clusterSize = 1u << clusterBits;   // 65536
    uint32_t l2Entries = clusterSize / 8;        // 8192
    uint32_t l1Size = (uint32_t)((diskSize + ((uint64_t)l2Entries * clusterSize - 1))
                       / ((uint64_t)l2Entries * clusterSize));

    // Layout:
    //   0x00000: header (1 cluster)
    //   0x10000: refcount table (1 cluster)
    //   0x20000: refcount block 0 (1 cluster)
    //   0x30000: L1 table (1 cluster)
    //   0x40000: first available cluster for data
    uint64_t refcountTableOffset = clusterSize;
    uint64_t refcountBlockOffset = clusterSize * 2;
    uint64_t l1TableOffset = clusterSize * 3;
    uint32_t headerClusters = 4;  // header + rctable + rcblock + L1

    // Write header (big-endian)
    uint8_t hdr[104];
    memset(hdr, 0, sizeof(hdr));

    // Magic
    uint32_t magic = bswap32(0x514649FB);
    memcpy(hdr + 0, &magic, 4);
    // Version = 3
    uint32_t ver = bswap32(3);
    memcpy(hdr + 4, &ver, 4);
    // Backing file offset = 0
    // Backing file size = 0
    // Cluster bits
    uint32_t cb = bswap32(clusterBits);
    memcpy(hdr + 20, &cb, 4);
    // Disk size
    uint64_t ds = bswap64(diskSize);
    memcpy(hdr + 24, &ds, 8);
    // Encryption = 0
    // L1 size
    uint32_t l1s = bswap32(l1Size);
    memcpy(hdr + 36, &l1s, 4);
    // L1 table offset
    uint64_t l1o = bswap64(l1TableOffset);
    memcpy(hdr + 40, &l1o, 8);
    // Refcount table offset
    uint64_t rto = bswap64(refcountTableOffset);
    memcpy(hdr + 48, &rto, 8);
    // Refcount table clusters = 1
    uint32_t rtc = bswap32(1);
    memcpy(hdr + 56, &rtc, 4);
    // Number of snapshots = 0 (offset 60)
    // Snapshots offset = 0 (offset 64)
    // v3 fields:
    // Incompatible features = 0 (offset 72)
    // Compatible features = 0 (offset 80)
    // Autoclear features = 0 (offset 88)
    // Refcount order = 4 (16-bit refcounts)
    uint32_t rco = bswap32(4);
    memcpy(hdr + 96, &rco, 4);
    // Header length = 104
    uint32_t hl = bswap32(104);
    memcpy(hdr + 100, &hl, 4);

    // Write first cluster: header padded to cluster size
    std::vector<uint8_t> cluster(clusterSize, 0);
    memcpy(cluster.data(), hdr, sizeof(hdr));
    fwrite(cluster.data(), 1, clusterSize, f);

    // Refcount table: one entry pointing to refcount block 0
    memset(cluster.data(), 0, clusterSize);
    uint64_t rcbOffset = bswap64(refcountBlockOffset);
    memcpy(cluster.data(), &rcbOffset, 8);
    fwrite(cluster.data(), 1, clusterSize, f);

    // Refcount block 0: mark the first headerClusters as refcount=1
    // qcow2 stores ALL numbers big-endian, including 16-bit refcounts
    memset(cluster.data(), 0, clusterSize);
    for (uint32_t i = 0; i < headerClusters; i++) {
        uint16_t rc = 0x0100;  // 1 in big-endian 16-bit
        memcpy(cluster.data() + i * 2, &rc, 2);
    }
    fwrite(cluster.data(), 1, clusterSize, f);

    // L1 table: all zeros (no data allocated yet)
    memset(cluster.data(), 0, clusterSize);
    fwrite(cluster.data(), 1, clusterSize, f);

    fclose(f);

    fprintf(stderr, "[HDD] Created qcow2: %s (%llu MB virtual, %u-byte clusters, L1=%u entries)\n",
            qcow2Path, (unsigned long long)(diskSize / (1024*1024)), clusterSize, l1Size);

    // Now open it read-write and format FATX partitions
    XboxHDD hdd;
    if (!hdd.m_qcow2.OpenReadWrite(qcow2Path)) {
        fprintf(stderr, "[HDD] Create: failed to reopen for formatting\n");
        return false;
    }

    // Format each partition with FATX
    for (int i = 0; i < XBOX_PARTITION_COUNT; i++) {
        uint64_t partOff = XBOX_PARTITION_TABLE[i].offset;
        uint64_t partSize = XBOX_PARTITION_TABLE[i].size;
        if (partSize == 0) partSize = diskSize - partOff;
        if (partOff + partSize > diskSize) continue;

        char letter = XBOX_PARTITION_TABLE[i].letter;

        // Write FATX superblock (4096 bytes)
        std::vector<uint8_t> superblock(4096, 0);
        uint32_t fatxMagic = FATX_MAGIC;
        memcpy(superblock.data(), &fatxMagic, 4);
        // Volume ID (random-ish)
        uint32_t volId = (uint32_t)(partOff ^ 0xDEADBEEF);
        memcpy(superblock.data() + 4, &volId, 4);
        // Sectors per cluster = 32 (16KB clusters)
        uint32_t spc = 32;
        memcpy(superblock.data() + 8, &spc, 4);
        // FAT copies = 1
        uint16_t fatCopies = 1;
        memcpy(superblock.data() + 12, &fatCopies, 2);
        // Rest is 0xFF
        memset(superblock.data() + 16, 0xFF, 4096 - 16);

        hdd.m_qcow2.WriteVirtual(partOff, superblock.data(), 4096);

        // Calculate FAT size (same algorithm as Mount)
        uint32_t bytesPerCluster = spc * 512;
        uint32_t fatEntries = (uint32_t)(partSize / bytesPerCluster) + 1;
        uint32_t fatEntrySize = (fatEntries < 0xFFF0) ? 2 : 4;
        uint32_t fatSize = fatEntries * fatEntrySize;
        if (fatSize % 4096) fatSize += 4096 - (fatSize % 4096);

        // Write FAT: entry 0 = media descriptor, entry 1 = EOF (root dir), rest = free
        uint64_t fatOffset = partOff + 4096;
        std::vector<uint8_t> fat(fatSize, 0);
        if (fatEntrySize == 2) {
            uint16_t mediaDesc = 0xFFF8;
            uint16_t eofMark = 0xFFFF;
            memcpy(fat.data(), &mediaDesc, 2);
            memcpy(fat.data() + 2, &eofMark, 2);
        } else {
            uint32_t mediaDesc = 0xFFFFFFF8;
            uint32_t eofMark = 0xFFFFFFFF;
            memcpy(fat.data(), &mediaDesc, 4);
            memcpy(fat.data() + 4, &eofMark, 4);
        }
        hdd.m_qcow2.WriteVirtual(fatOffset, fat.data(), fatSize);

        // Write empty root directory (cluster 1): all 0xFF = end-of-dir
        uint64_t dataOffset = fatOffset + fatSize;
        std::vector<uint8_t> rootDir(bytesPerCluster, 0xFF);
        hdd.m_qcow2.WriteVirtual(dataOffset, rootDir.data(), bytesPerCluster);

        fprintf(stderr, "[HDD] Formatted %c: at 0x%llX (%llu MB, FAT%d, %u clusters)\n",
                letter, (unsigned long long)partOff,
                (unsigned long long)(partSize / (1024*1024)),
                fatEntrySize == 2 ? 16 : 32, fatEntries);
    }

    hdd.m_qcow2.Close();
    return true;
}

XboxHDD::XboxHDD()
    : m_dataPartitionIndex(-1)
{
}

XboxHDD::~XboxHDD() {
    Close();
}

bool XboxHDD::Open(const char* qcow2Path) {
    Close();
    if (!m_qcow2.Open(qcow2Path))
        return false;
    ScanPartitions();
    m_dataPartitionIndex = FindDataPartition();
    return true;
}

bool XboxHDD::OpenReadWrite(const char* qcow2Path) {
    Close();
    if (!m_qcow2.OpenReadWrite(qcow2Path))
        return false;
    ScanPartitions();
    m_dataPartitionIndex = FindDataPartition();
    fprintf(stderr, "[HDD] Opened read-write: %s\n", m_qcow2.IsWritable() ? "yes" : "no (fallback to read-only)");
    return true;
}

void XboxHDD::Close() {
    for (auto* p : m_partitions)
        delete p;
    m_partitions.clear();
    m_dataPartitionIndex = -1;
    m_qcow2.Close();
}

int XboxHDD::ScanPartitions() {
    if (!m_qcow2.IsOpen())
        return 0;

    uint64_t diskSize = m_qcow2.GetDiskSize();
    fprintf(stderr, "[HDD] Scanning %llu byte virtual disk for FATX partitions\n",
            (unsigned long long)diskSize);

    for (int i = 0; i < XBOX_PARTITION_COUNT; i++) {
        uint64_t offset = XBOX_PARTITION_TABLE[i].offset;
        uint64_t partSize = XBOX_PARTITION_TABLE[i].size;
        if (partSize == 0)
            partSize = diskSize - offset;  // extends to end

        if (offset >= diskSize)
            continue;
        if (offset + partSize > diskSize)
            partSize = diskSize - offset;

        // Check for FATX magic
        uint32_t magic = 0;
        m_qcow2.ReadVirtual(offset, &magic, 4);
        fprintf(stderr, "[HDD]   %c: at 0x%08llX (%llu MB): %s\n",
                XBOX_PARTITION_TABLE[i].letter,
                (unsigned long long)offset,
                (unsigned long long)(partSize / (1024*1024)),
                (magic == FATX_MAGIC) ? "FATX" : "not FATX");
        if (magic != FATX_MAGIC)
            continue;

        FATXReader* reader = new FATXReader();
        if (reader->Mount(&m_qcow2, offset, partSize)) {
            m_partitions.push_back(reader);
        } else {
            delete reader;
        }
    }

    return (int)m_partitions.size();
}

int XboxHDD::FindDataPartition() {
    // The data partition contains TDATA and UDATA directories at root
    for (int i = 0; i < (int)m_partitions.size(); i++) {
        auto entries = m_partitions[i]->ReadRootDirectory();
        for (const auto& e : entries) {
            if (e.IsDirectory() && (fatx_stricmp(e.name, "TDATA") == 0 ||
                                    fatx_stricmp(e.name, "UDATA") == 0)) {
                return i;
            }
        }
    }
    return -1;
}

FATXReader* XboxHDD::GetPartition(int index) {
    if (index < 0 || index >= (int)m_partitions.size())
        return nullptr;
    return m_partitions[index];
}

std::vector<XboxTitleInfo> XboxHDD::EnumerateTitles() {
    std::vector<XboxTitleInfo> titles;

    if (m_dataPartitionIndex < 0)
        return titles;

    FATXReader* part = m_partitions[m_dataPartitionIndex];
    auto rootEntries = part->ReadRootDirectory();

    // Find TDATA and UDATA clusters
    uint32_t tdataCluster = 0, udataCluster = 0;
    for (const auto& e : rootEntries) {
        if (e.IsDirectory()) {
            if (fatx_stricmp(e.name, "TDATA") == 0) tdataCluster = e.firstCluster;
            if (fatx_stricmp(e.name, "UDATA") == 0) udataCluster = e.firstCluster;
        }
    }

    // Build title list from UDATA (has per-game save info)
    if (udataCluster > 0) {
        auto udataEntries = part->ReadDirectory(udataCluster);
        for (const auto& e : udataEntries) {
            if (!e.IsDirectory())
                continue;

            XboxTitleInfo info;
            memset(&info, 0, sizeof(info));
            strncpy(info.titleID, e.name, 8);
            info.titleID[8] = '\0';
            info.udataCluster = e.firstCluster;

            // Scan UDATA/{titleID}/ for metadata files
            auto titleEntries = part->ReadDirectory(e.firstCluster);
            for (const auto& te : titleEntries) {
                if (te.IsFile() && fatx_stricmp(te.name, "TitleMeta.xbx") == 0) {
                    // Read title name
                    std::string name = ReadTitleName(m_dataPartitionIndex,
                                                     te.firstCluster, te.fileSize);
                    if (!name.empty())
                        strncpy(info.titleName, name.c_str(), sizeof(info.titleName) - 1);
                }
                if (te.IsFile() && fatx_stricmp(te.name, "TitleImage.xbx") == 0)
                    info.hasTitleImage = true;
                if (te.IsFile() && fatx_stricmp(te.name, "SaveImage.xbx") == 0)
                    info.hasSaveImage = true;
                if (te.IsDirectory())
                    info.saveCount++;
            }

            titles.push_back(info);
        }
    }

    // Cross-reference with TDATA to find titles that have TDATA but no UDATA
    if (tdataCluster > 0) {
        auto tdataEntries = part->ReadDirectory(tdataCluster);
        for (const auto& e : tdataEntries) {
            if (!e.IsDirectory())
                continue;

            // Check if we already have this title from UDATA
            bool found = false;
            for (auto& t : titles) {
                if (fatx_stricmp(t.titleID, e.name) == 0) {
                    t.tdataCluster = e.firstCluster;
                    found = true;
                    break;
                }
            }

            if (!found) {
                // Title only in TDATA, no saves
                XboxTitleInfo info;
                memset(&info, 0, sizeof(info));
                strncpy(info.titleID, e.name, 8);
                info.titleID[8] = '\0';
                info.tdataCluster = e.firstCluster;
                titles.push_back(info);
            }
        }
    }

    return titles;
}

std::vector<uint8_t> XboxHDD::ReadFile(int partitionIndex, uint32_t firstCluster, uint32_t fileSize) {
    FATXReader* part = GetPartition(partitionIndex);
    if (!part)
        return {};
    return part->ReadFile(firstCluster, fileSize);
}

std::string XboxHDD::ReadTitleName(int partitionIndex, uint32_t metaCluster, uint32_t metaSize) {
    if (metaSize == 0 || metaSize > 4096)
        return "";

    auto data = ReadFile(partitionIndex, metaCluster, metaSize);
    if (data.empty())
        return "";

    // TitleMeta.xbx is UTF-16LE INI format: "TitleName=Game Title\r\n"
    // Convert entire file to UTF-8 first, then parse (skip BOM if present)
    std::string fullText;
    size_t start = 0;
    if (data.size() >= 2 && data[0] == 0xFF && data[1] == 0xFE)
        start = 2;
    for (size_t i = start; i + 1 < data.size(); i += 2) {
        uint16_t ch = data[i] | (data[i + 1] << 8);
        if (ch == 0 || ch == 0xFFFF) break;
        if (ch < 0x80) {
            fullText += (char)ch;
        } else if (ch < 0x800) {
            fullText += (char)(0xC0 | (ch >> 6));
            fullText += (char)(0x80 | (ch & 0x3F));
        } else {
            fullText += (char)(0xE0 | (ch >> 12));
            fullText += (char)(0x80 | ((ch >> 6) & 0x3F));
            fullText += (char)(0x80 | (ch & 0x3F));
        }
    }

    // Find "TitleName=" and extract the value
    const char* key = "TitleName=";
    size_t pos = fullText.find(key);
    if (pos == std::string::npos) return fullText;  // no key found, return raw
    pos += strlen(key);
    size_t end = fullText.find_first_of("\r\n", pos);
    if (end == std::string::npos) end = fullText.size();
    return fullText.substr(pos, end - pos);
}
