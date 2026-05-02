// xbox_hdd.h: Xbox HDD image parser public API. Reads xemu's qcow2
// virtual hard drive images to access FATX partitions; primary
// consumer is the savegame grid (TDATA / UDATA) integration.
// Companion to desktop/xbox_hdd.cpp.
//
// Usage:
//   XboxHDD hdd;
//   if (hdd.Open("path/to/xbox_hdd.qcow2")) {
//       auto titles = hdd.EnumerateTitles();
//       for (auto& t : titles) {
//           printf("%s: %s\n", t.titleID, t.titleName);
//       }
//   }
//   hdd.Close();

#ifndef XBOX_HDD_H
#define XBOX_HDD_H

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>

// ============================================================================
// qcow2 reader
// ============================================================================

struct Qcow2Header {
    uint32_t magic;             // 0x514649FB
    uint32_t version;           // 2 or 3
    uint32_t cluster_bits;      // log2(cluster_size), typically 16
    uint64_t disk_size;         // virtual disk size
    uint32_t l1_size;           // L1 table entry count
    uint64_t l1_table_offset;   // physical offset of L1 table
};

class Qcow2Reader {
public:
    Qcow2Reader();
    ~Qcow2Reader();

    bool Open(const char* path);
    bool OpenReadWrite(const char* path);
    void Close();
    bool IsOpen() const { return m_file != nullptr; }
    bool IsWritable() const { return m_writable; }

    // Read from virtual address space (resolves through L1/L2 tables)
    size_t ReadVirtual(uint64_t virtualOffset, void* buffer, size_t size);

    // Write to virtual address space (only works on already-allocated clusters)
    // Returns true if all bytes were written successfully
    bool WriteVirtual(uint64_t virtualOffset, const void* data, size_t size);

    uint64_t GetDiskSize() const { return m_header.disk_size; }
    uint32_t GetClusterSize() const { return 1u << m_header.cluster_bits; }

private:
    // Resolve virtual offset to physical file offset (0 if unallocated)
    uint64_t ResolveVirtual(uint64_t virtualOffset, uint32_t* outL1Idx = nullptr,
                             uint32_t* outL2Idx = nullptr, uint32_t* outInCluster = nullptr);

    // Allocate a new cluster at end of qcow2 file (returns physical offset)
    uint64_t AllocCluster();

    // Update refcount for a physical cluster (increment by 1)
    bool UpdateRefcount(uint64_t physOffset);

    // Ensure L2 table exists for given L1 index (allocates if needed)
    uint64_t EnsureL2Table(uint32_t l1Idx);

    FILE* m_file;
    Qcow2Header m_header;
    std::vector<uint64_t> m_l1Table;
    uint32_t m_clusterSize;
    uint32_t m_l2Bits;
    uint32_t m_l2Entries;
    bool m_writable;
};

// ============================================================================
// FATX filesystem
// ============================================================================

#define FATX_MAGIC      0x58544146  // "FATX" as little-endian uint32
#define FATX_HEADER_SIZE 0x1000     // 4096 bytes

// Directory entry flags
#define FATX_ATTR_READONLY  0x01
#define FATX_ATTR_HIDDEN    0x02
#define FATX_ATTR_SYSTEM    0x04
#define FATX_ATTR_DIRECTORY 0x10
#define FATX_ATTR_ARCHIVE   0x20

// FAT special values (16-bit)
#define FATX_FAT16_FREE     0x0000
#define FATX_FAT16_RESERVED 0xFFF8
#define FATX_FAT16_EOF      0xFFFF

// FAT special values (32-bit)
#define FATX_FAT32_FREE     0x00000000
#define FATX_FAT32_RESERVED 0xFFFFFFF8
#define FATX_FAT32_EOF      0xFFFFFFFF

// Directory entry start offset within each cluster (observed in xemu images)
#define FATX_DIR_ENTRY_START 192

struct FATXVolumeHeader {
    uint32_t magic;             // FATX_MAGIC
    uint32_t volumeID;
    uint32_t sectorsPerCluster; // cluster_size = this * 512
    uint16_t fatCopies;
    uint16_t reserved;
};

struct FATXDirEntry {
    char     name[43];          // null-terminated (from length-prefixed on-disk format)
    uint8_t  attributes;
    uint32_t firstCluster;
    uint32_t fileSize;
    uint16_t modifiedTime;
    uint16_t modifiedDate;
    uint16_t createdTime;
    uint16_t createdDate;

    bool IsDirectory() const { return (attributes & FATX_ATTR_DIRECTORY) != 0; }
    bool IsFile() const { return !IsDirectory(); }
};

struct FATXPartition {
    uint64_t offset;            // byte offset in the virtual disk
    uint64_t size;              // partition size in bytes
    uint32_t clusterSize;       // bytes per cluster
    uint32_t totalClusters;
    uint32_t fatEntrySize;      // 2 or 4
    uint64_t fatOffset;         // absolute offset of FAT in virtual disk
    uint64_t dataOffset;        // absolute offset of first data cluster
    uint32_t volumeID;
    bool     valid;
};

class FATXReader {
public:
    FATXReader();
    ~FATXReader();

    // Initialize with a qcow2 reader and partition offset
    bool Mount(Qcow2Reader* qcow2, uint64_t partitionOffset, uint64_t partitionSize);
    void Unmount();
    bool IsMounted() const { return m_mounted; }

    // Read directory entries from a cluster
    std::vector<FATXDirEntry> ReadDirectory(uint32_t cluster);

    // Read root directory (cluster 1)
    std::vector<FATXDirEntry> ReadRootDirectory() { return ReadDirectory(1); }

    // Read file data (follows FAT chain)
    std::vector<uint8_t> ReadFile(uint32_t firstCluster, uint32_t fileSize);

    // Follow a FAT chain from a starting cluster
    std::vector<uint32_t> GetClusterChain(uint32_t startCluster);

    // Get partition info
    const FATXPartition& GetPartition() const { return m_partition; }

    // Write support
    uint32_t AllocateCluster();                         // find free FAT entry, mark as EOF
    bool WriteFATEntry(uint32_t cluster, uint32_t val); // update FAT in memory
    bool FreeClusterChain(uint32_t startCluster);       // mark chain as free
    bool FlushFAT();                                    // write FAT back to disk

    // Create a file in a directory (allocates clusters, writes data, creates dir entry)
    bool CreateFile(uint32_t parentDirCluster, const char* name,
                    const void* data, uint32_t size);

    // Create a subdirectory
    bool CreateDirectory(uint32_t parentDirCluster, const char* name);

    // Delete a file or directory entry (frees clusters, marks entry as deleted)
    bool DeleteEntry(uint32_t parentDirCluster, const char* name);

    // Rename a file or directory entry
    bool RenameEntry(uint32_t parentDirCluster, const char* oldName, const char* newName);

    // Write raw data to a cluster
    bool WriteCluster(uint32_t cluster, const void* data, uint32_t size);

private:
    uint64_t ClusterToOffset(uint32_t cluster) const;
    uint32_t ReadFATEntry(uint32_t cluster);

    // Recursively free all clusters used by a directory's contents
    void DeleteDirectoryContents(uint32_t dirCluster);

    // Find a free slot in a directory cluster, returns byte offset within cluster or -1
    int FindFreeDirSlot(uint32_t dirCluster);

    // Write a directory entry at a specific position
    bool WriteDirEntry(uint32_t dirCluster, int slotOffset, const FATXDirEntry& entry);

    Qcow2Reader* m_qcow2;
    FATXPartition m_partition;
    std::vector<uint8_t> m_fatData;
    bool m_mounted;
};

// ============================================================================
// Xbox HDD (combines qcow2 + FATX into a usable interface)
// ============================================================================

// Standard Xbox HDD partition table (from mborgerson/fatx)
// Drive letters per the Xbox kernel, NOT alphabetical by offset
struct XboxPartitionDef {
    uint64_t offset;
    uint64_t size;      // 0 = extends to end of disk
    char     letter;
};
static const XboxPartitionDef XBOX_PARTITION_TABLE[] = {
    { 0x00080000,    0x2EE00000,  'X' },   // Cache 1 (750 MB)
    { 0x2EE80000,    0x2EE00000,  'Y' },   // Cache 2 (750 MB)
    { 0x5DC80000,    0x2EE00000,  'Z' },   // Cache 3 (750 MB)
    { 0x8CA80000,    0x1F400000,  'C' },   // System   (500 MB)
    { 0xABE80000,    0x1312D6000, 'E' },   // Data     (~4.8 GB)
};
static const int XBOX_PARTITION_COUNT = sizeof(XBOX_PARTITION_TABLE) / sizeof(XBOX_PARTITION_TABLE[0]);

// Title info extracted from TDATA/UDATA
struct XboxTitleInfo {
    char     titleID[9];        // 8-char hex title ID + null
    char     titleName[128];    // UTF-8 display name (converted from UTF-16LE TitleMeta.xbx)
    uint32_t tdataCluster;      // cluster of TDATA/{titleID} directory
    uint32_t udataCluster;      // cluster of UDATA/{titleID} directory
    bool     hasTitleImage;     // TitleImage.xbx exists
    bool     hasSaveImage;      // SaveImage.xbx exists
    int      saveCount;         // number of save subdirectories
};

class XboxHDD {
public:
    XboxHDD();
    ~XboxHDD();

    // Create a new qcow2 HDD image with formatted FATX partitions
    static bool Create(const char* qcow2Path, uint64_t diskSize = 8ULL * 1024 * 1024 * 1024);

    // Open a qcow2 HDD image (read-only or read-write)
    bool Open(const char* qcow2Path);
    bool OpenReadWrite(const char* qcow2Path);
    void Close();
    bool IsOpen() const { return m_qcow2.IsOpen(); }
    bool IsWritable() const { return m_qcow2.IsWritable(); }

    // Scan all partitions for FATX volumes
    int ScanPartitions();

    // Find the partition containing TDATA/UDATA (the E: data partition)
    // Returns partition index or -1 if not found
    int FindDataPartition();

    // Enumerate all title IDs found in TDATA and UDATA
    std::vector<XboxTitleInfo> EnumerateTitles();

    // Read a file from a mounted partition
    std::vector<uint8_t> ReadFile(int partitionIndex, uint32_t firstCluster, uint32_t fileSize);

    // Read TitleMeta.xbx and return the title name as UTF-8
    std::string ReadTitleName(int partitionIndex, uint32_t metaCluster, uint32_t metaSize);

    // Access partitions
    int GetPartitionCount() const { return (int)m_partitions.size(); }
    FATXReader* GetPartition(int index);
    uint64_t GetQcow2DiskSize() const { return m_qcow2.GetDiskSize(); }

private:
    Qcow2Reader m_qcow2;
    std::vector<FATXReader*> m_partitions;
    int m_dataPartitionIndex;   // cached index of the E: partition
};

#endif // XBOX_HDD_H
