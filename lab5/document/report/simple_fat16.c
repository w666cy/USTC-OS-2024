#include <time.h>
#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/timeb.h>

#include "fat16.h"
#include "fat16_utils.h"

/* FAT16 volume data with a file handler of the FAT16 image file */
// 存储文件系统所需要的元数据的数据结构
// "抄袭"了https://elixir.bootlin.com/linux/latest/source/fs/fat/inode.c#L44
typedef struct {
	uint32_t sector_size;           // 逻辑扇区大小（字节）
	uint32_t sec_per_clus;          // 每簇扇区数
	uint32_t reserved;              // 保留扇区数
	uint32_t fats;                  // FAT表的数量
	uint32_t dir_entries;           // 根目录项数量
	uint32_t sectors;               // 文件系统总扇区数
	uint32_t sec_per_fat;            // 每个FAT表所占扇区数

    sector_t fat_sec;               // FAT表开始扇区
    sector_t root_sec;              // 根目录区域开始扇区
    uint32_t root_sectors;          // 根目录区域扇区数
    sector_t data_sec;              // 数据区域开始扇区
    
    uint32_t clusters;              // 文件系统簇数
    uint32_t cluster_size;          // 簇大小（字节）

    uid_t fs_uid;               // 可忽略，挂载FAT的用户ID，所有文件的拥有者都显示为该用户
    gid_t fs_gid;               // 可忽略，挂载FAT的组ID，所有文件的用户组都显示为该组
    struct timespec atime;      // 访问时间
    struct timespec mtime;      // 修改时间
    struct timespec ctime;      // 创建时间
} FAT16;

FAT16 meta;

sector_t cluster_first_sector(cluster_t clus) {
    assert(is_cluster_inuse(clus));
    return ((clus - 2) * meta.sec_per_clus) + meta.data_sec;
}

cluster_t sector_cluster(sector_t sec) {
    if(sec < meta.data_sec) {
        return 0;
    }
    cluster_t clus = 2 + (sec - meta.data_sec) / meta.sec_per_clus;
    assert(is_cluster_inuse(clus));
    return clus;
}

cluster_t read_fat_entry(cluster_t clus)
{
    char sector_buffer[MAX_LOGICAL_SECTOR_SIZE];
    /**
     * TODO: 4.1 读取FAT表项 [约5行代码]
     * Hint: 你需要读取FAT表中clus对应的表项，然后返回该表项的值。
     *       表项在哪个扇区？在扇区中的偏移量是多少？表项的大小是多少？
     */
    // ================== Your code here =================
    
    size_t fat_entry_offset = clus * 2; // FAT 表项在 FAT 表中的偏移量
    sector_t fat_sector = meta.fat_sec + fat_entry_offset / meta.sector_size; // FAT 表项所在的扇区
    size_t fat_sector_offset = fat_entry_offset % meta.sector_size; // FAT 表项在扇区中的偏移量
    
    // 读取 FAT 表所在的扇区
    int ret = sector_read(fat_sector, sector_buffer);
    if (ret < 0) {
        return -EIO;
    }

    // 从扇区缓冲区中读取 FAT 表项的值
    cluster_t fat_entry;
    memcpy(&fat_entry, sector_buffer + fat_sector_offset, 16);

    return fat_entry;

    // ===================================================
    // return CLUSTER_END; // TODO: 记得删除或者修改这一行
}

typedef struct {
    DIR_ENTRY dir;
    sector_t sector;
    size_t offset;
} DirEntrySlot;

/**
 * @brief 寻找对应的目录项，从 name 开始的 len 字节为要搜索的文件/目录名
 * 
 * @param name 指向文件名的指针，不一定以'\0'结尾，因此需要len参数
 * @param len 文件名的长度
 * @param from_sector 开始搜索的扇区
 * @param sectors_count 需要搜索的扇区数
 * @param slot 输出参数，存放找到的目录项和位置
 * @return long 找到entry时返回 FIND_EXIST，找到空槽返回 FIND_EMPTY，扇区均满了返回 FIND_FULL，错误返回错误代码的负值
 */
int find_entry_in_sectors(const char* name, size_t len, 
            sector_t from_sector, size_t sectors_count, 
            DirEntrySlot* slot) {
    char buffer[MAX_LOGICAL_SECTOR_SIZE];

    /**
     * TODO: 2.1 在指定扇区中找到文件名为 name 的目录项 [约20行代码]  
     * Hint: 你可以参考 fill_entries_in_sectors 函数的实现。（非常类似！）
     *       区别在于，你需要找到对应目录项并给slot赋值，然后返回正确的返回值，而不是调用 filler 函数。
     *       你可以使用 check_name 函数来检查目录项是否符合你要找的文件名。
     *       注意：找到entry时返回 FIND_EXIST，找到空槽返回 FIND_EMPTY，所有扇区都满了返回 FIND_FULL。
     */
    // ================== Your code here =================

    for(size_t i=0; i < sectors_count; i++) {
        sector_t sec = from_sector + i;
        int ret = sector_read(sec, buffer);
        if(ret < 0) {
            return -EIO;
        }
        for(size_t off = 0; off < meta.sector_size; off += DIR_ENTRY_SIZE) {
            DIR_ENTRY* entry = (DIR_ENTRY*)(buffer + off);
            if(de_is_valid(entry)) {
                if(check_name(name, len, entry)) {
                    slot->dir = *entry;
                    slot->sector = sec;
                    slot->offset = off;
                    return FIND_EXIST;
                }
            }
            if(de_is_free(entry)) {
                slot->sector = sec;
                slot->offset = off;
                return FIND_EMPTY;
            }
        }
    }
    
    // ===================================================
    return FIND_FULL;
}

/**
 * @brief 找到path所对应路径的目录项，如果最后一级路径不存在，则找到能创建最后一级文件/目录的空目录项。
 * 
 * @param path 要查找的路径
 * @param slot 输出参数，存放找到的目录项和位置
 * @param remains 输出参数，指向未找到的路径部分。如路径为 "/a/b/c"，a存在，b不存在，remains会指指向 "b/c"。
 * @return int 找到entry时返回 FIND_EXIST，找到空槽返回 FIND_EMPTY，扇区均满了返回 FIND_FULL，
 *             错误返回错误代码的负值，可能的错误参见brief部分。
 */
int find_entry_internal(const char* path, DirEntrySlot* slot, const char** remains) {
    *remains = path;
    *remains += strspn(*remains, "/");    // 跳过开头的'/'
    
    // 根目录
    sector_t first_sec = meta.root_sec;
    size_t nsec = meta.root_sectors;
    size_t len = strcspn(*remains, "/"); // 目前要搜索的文件名长度
    int state = find_entry_in_sectors(*remains, len, first_sec, nsec, slot);    // 请补全 find_entry_in_sectors 函数

    // 找到下一层名字开头
    const char* next_level = *remains + len;
    next_level += strspn(next_level, "/");

    // 以下是根目录搜索的结果判断和错误处理
    if(state < 0 || *next_level == '\0') {   // 出错，或者只有一层已经找到了，直接返回结果
        return state;
    }
    if(state != FIND_EXIST) {   // 不是最后一层，且没找到
        return -ENOENT;
    }
    if(!attr_is_directory(slot->dir.DIR_Attr)) { // 不是最后一级，且不是目录
        return -ENOTDIR;
    }

    cluster_t clus = slot->dir.DIR_FstClusLO;  // 文件首个簇的簇号
    *remains = next_level;
    while (true) {
        size_t len = strcspn(*remains, "/"); // 目前要搜索的文件名长度
        /**
         * TODO: 5.1 查找非根目录的路径的目录项 [约10行代码]
         * Hint: 补全以下代码，实现对非根目录的路径的查找。提示，你要写一个 while 循环，依次搜索当前目录对应的簇里的目录项。
         */
        // ================== Your code here =================
        
        while (is_cluster_inuse(clus)) {
            first_sec = cluster_first_sector(clus);  // 转换簇号到扇区号
            nsec = meta.sec_per_clus;  // 每个簇的扇区数
            state = find_entry_in_sectors(*remains, len, first_sec, nsec, slot);

            if (state < 0 || state == FIND_EXIST || state == FIND_EMPTY)
                break;
            clus = read_fat_entry(clus);
        }
        
        // ===================================================

        // 此时，slot 中存放了下一层的目录项

        const char* next_level = *remains + len;
        next_level += strspn(next_level, "/");

        if(state < 0 || *next_level == '\0') {   // 出错或者是最后一层，直接返回结果
            return state;
        }
        if(state != FIND_EXIST) {
            return -ENOENT;
        }
        if(!attr_is_directory(slot->dir.DIR_Attr)) {
            return -ENOTDIR;
        }

        *remains = next_level;  // remains 指向下一层的名字开头
        clus = slot->dir.DIR_FstClusLO; // 切换到下一个簇
    }
    return -EUCLEAN;
}


/**
 * @brief 将 path 对应的目录项写入 slot 中，slot 实际上记录了目录项本身，以及目录项的位置（扇区号和在扇区中的偏移）。
 *        该函数的主体实现在 find_entry_internal 函数中。你不需要修改这个函数。
 * @param path 
 * @param slot 
 * @return int 如果找到目录项，返回0；如果文件不存在，返回-ENOENT；如果出现其它错误，返回错误代码的负值。
 */
int find_entry(const char* path, DirEntrySlot* slot) {
    const char* remains = NULL;
    int ret = find_entry_internal(path, slot, &remains); // 请查看并补全 find_entry_internal 函数
    if(ret < 0) {
        return ret;
    }
    if(ret == FIND_EXIST) {
        return 0;
    }
    return -ENOENT;
}

/**
 * @brief 找到一个能创建 path 对应文件/目录的空槽（空目录项），如果文件/目录已经存在，则返回错误。
 * 
 * @param path 
 * @param slot 
 * @param last_name 
 * @return int 如果找到空槽，返回0；如果文件已经存在，返回-EEXIST；如果目录已满，返回-ENOSPC。
 */
int find_empty_slot(const char* path, DirEntrySlot *slot, const char** last_name) {
    int ret = find_entry_internal(path, slot, last_name);
    if(ret < 0) {
        return ret;
    }
    if(ret == FIND_EXIST) { // 文件已经存在
        return -EEXIST;
    }
    if(ret == FIND_FULL) { // 所有槽都已经满了
        return -ENOSPC;
    }
    return 0;
}

mode_t get_mode_from_attr(uint8_t attr) {
    mode_t mode = 0;
    mode |= attr_is_readonly(attr) ? S_IRUGO : S_NORMAL;
    mode |= attr_is_directory(attr) ? S_IFDIR : S_IFREG;
    return mode;
}

// ===========================文件系统接口实现===============================

/**
 * @brief 文件系统初始化，无需修改。但你可以阅读这个函数来了解如何使用 sector_read 来读出文件系统元数据信息。
 * 
 * @param conn 
 * @return void* 
 */
void *fat16_init(struct fuse_conn_info * conn, struct fuse_config *config) {
    /* Reads the BPB */
    BPB_BS bpb;
    sector_read(0, &bpb);
    meta.sector_size = bpb.BPB_BytsPerSec;
    meta.sec_per_clus = bpb.BPB_SecPerClus;
    meta.reserved = bpb.BPB_RsvdSecCnt;
    meta.fats = bpb.BPB_NumFATS;
    meta.dir_entries = bpb.BPB_RootEntCnt;
    meta.sectors = bpb.BPB_TotSec16 != 0 ? bpb.BPB_TotSec16 : bpb.BPB_TotSec32;
    meta.sec_per_fat = bpb.BPB_FATSz16;


    meta.fat_sec = meta.reserved;
    meta.root_sec = meta.fat_sec + (meta.fats * meta.sec_per_fat);
    meta.root_sectors = (meta.dir_entries * DIR_ENTRY_SIZE) / meta.sector_size;
    meta.data_sec = meta.root_sec + meta.root_sectors;
    meta.clusters = (meta.sectors - meta.data_sec) / meta.sec_per_clus;
    meta.cluster_size = meta.sec_per_clus * meta.sector_size;

    meta.fs_uid = getuid();
    meta.fs_gid = getgid();

    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    meta.atime = meta.mtime = meta.ctime = now;
    return NULL;
}

/**
 * @brief 释放文件系统，无需修改
 * 
 * @param data 
 */
void fat16_destroy(void *data) { }

/**
 * @brief 获取path对应的文件的属性，无需修改。
 * 
 * @param path    要获取属性的文件路径
 * @param stbuf   输出参数，需要填充的属性结构体
 * @return int    成功返回0，失败返回POSIX错误代码的负值
 */
int fat16_getattr(const char* path, struct stat* stbuf, struct fuse_file_info* fi) {
    printf("getattr(path='%s')\n", path);
    // 清空所有属性
    memset(stbuf, 0, sizeof(struct stat));

    // 这些属性被忽略
    stbuf->st_dev = 0;
    stbuf->st_ino = 0;
    stbuf->st_nlink = 0;
    stbuf->st_rdev = 0;

    // 这些属性被提前计算好，不会改变
    stbuf->st_uid = meta.fs_uid;
    stbuf->st_gid = meta.fs_gid;
    stbuf->st_blksize = meta.cluster_size;

    // 这些属性需要根据文件设置
    // st_mode, st_size, st_blocks, a/m/ctim
    if (path_is_root(path)) {
        stbuf->st_mode = S_IFDIR | S_NORMAL;
        stbuf->st_size = 0;
        stbuf->st_blocks = 0;
        stbuf->st_atim = meta.atime;
        stbuf->st_mtim = meta.mtime;
        stbuf->st_ctim = meta.ctime;
        return 0;
    }

    DirEntrySlot slot;
    DIR_ENTRY* dir = &(slot.dir);
    int ret = find_entry(path, &slot);
    if(ret < 0) {
        return ret;
    }
    stbuf->st_mode = get_mode_from_attr(dir->DIR_Attr);
    stbuf->st_size = dir->DIR_FileSize;
    stbuf->st_blocks = dir->DIR_FileSize / PHYSICAL_SECTOR_SIZE;
    
    time_fat_to_unix(&stbuf->st_atim, dir->DIR_LstAccDate, 0, 0);
    time_fat_to_unix(&stbuf->st_mtim, dir->DIR_WrtDate, dir->DIR_WrtTime, 0);
    time_fat_to_unix(&stbuf->st_ctim, dir->DIR_CrtDate, dir->DIR_CrtTime, dir->DIR_CrtTimeTenth);
    return 0;
}

/**
 * @brief 读取扇区号为 first_sec 开始的 nsec 个扇区的目录项，并使用 filler 函数将目录项填充到 buffer 中
 *        调用 filler 的方式为 filler(buffer, 完整的文件名/目录名, NULL, 0, 0)
 *        例如，filler(buffer, "file1.txt", NULL, 0, 0) 表示该目录中有文件 file1.txt
 * @param first_sec 开始的扇区号
 * @param nsec      扇区数
 * @param filler    用于填充结果的函数
 * @param buf       结果缓冲区
 * @return int      成功返回0，失败返回POSIX错误代码的负值
 */
int fill_entries_in_sectors(sector_t first_sec, size_t nsec, fuse_fill_dir_t filler, void* buf) {
    char sector_buffer[MAX_LOGICAL_SECTOR_SIZE];
    char name[MAX_NAME_LEN];
    for(size_t i=0; i < nsec; i++) {
        /**
         * TODO: 1.2 读取扇区中的目录项 [3行代码]
         * Hint：（为降低难度，我们实现了大部分代码，你只需要修改和补全以下带 TODO 的几行。）
         *       你需要补全这个循环。这个循环读取每个扇区的内容，然后遍历扇区中的目录项，并正确调用 filler 函数。
         *       你可以参考以下步骤（大部分已经实现）：
         *          1. 使用 sector_read 读取第i个扇区内容到 sector_buffer 中（扇区号是什么？）
         *          2. 遍历 sector_buffer 中的每个目录项，如果是有效的目录项，转换文件名并调用 filler 函数。（每个目录项多长？）
         *             可能用到的函数（请自行阅读函数实现来了解用法）：
         *              de_is_valid: 判断目录项是否有效。
         *              to_longname: 将 FAT 短文件名转换为长文件名。
         *              filler: 用于填充结果的函数。
         *          3. 如果遇到空目录项，说明该扇区的目录项已经读取完，无需继续遍历。（为什么？）
         *             可能用到的函数：
         *              de_is_free: 判断目录项是否为空。
         *       为降低难度，我们实现了大部分代码，你只需要修改和补全以下带 TODO 的几行。
         */

        sector_t sec = first_sec + i; // TODO: 请填写正确的扇区号
        int ret = sector_read(sec, sector_buffer);
        if(ret < 0) {
            return -EIO;
        }
        for(size_t off = 0; off < meta.sector_size; off += DIR_ENTRY_SIZE) { // TODO: 请补全循环条件（每个扇区多大？目录项多大？）
            DIR_ENTRY* entry = (DIR_ENTRY*)(sector_buffer + off);
            if(de_is_valid(entry)) {
                int ret = to_longname(entry->DIR_Name, name, MAX_NAME_LEN); // TODO: 请调用 to_longname 函数，将 entry->DIR_Name 转换为长文件名，结果存放在 name 中。
                if(ret < 0) {
                    return ret;
                }
                printf("fill: '%s'", name);
                filler(buf, name, NULL, 0, 0);
            }
            if(de_is_free(entry)) {
                return 0;
            }
        }
    }
    return 0;
}

/**
 * @brief 读取path对应的目录，结果通过filler函数写入buffer中
 * 
 * @param path    要读取目录的路径
 * @param buf  结果缓冲区
 * @param filler  用于填充结果的函数，本次实验按filler(buffer, 文件名, NULL, 0, 0)的方式调用即可。
 *                你也可以参考<fuse.h>第58行附近的函数声明和注释来获得更多信息。
 * @param offset  忽略
 * @param fi      忽略
 * @return int    成功返回0，失败返回POSIX错误代码的负值
 */
int fat16_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, 
                    struct fuse_file_info *fi, enum fuse_readdir_flags flags) {
    printf("readdir(path='%s')\n", path);

    if(path_is_root(path)) {
        /**
         * TODO: 1.1 读取根目录区域 [2行代码]
         * Hint: 请正确修改以下两行，删掉 _placeholder_() 并替换为正确的数值。提示，使用 meta 中的成员变量。
         *       （注：_placeholder_() 在之后的 TODO 中也会出现。它只是个占位符，相当于你要填的空，你应该删除它，修改成正确的值。）
         */
        sector_t first_sec = meta.root_sec; // TODO: 请填写正确的根目录区域开始扇区号，你可以参考 meta 的定义。
        size_t nsec = meta.root_sectors; // TODO: 请填写正确的根目录区域扇区数
        fill_entries_in_sectors(first_sec, nsec, filler, buf);
        return 0;
    }

    // 不是根目录的情况，需要找到第一个 clus
    cluster_t clus = CLUSTER_END;
    DirEntrySlot slot;
    int ret = find_entry(path, &slot);
    if(ret < 0) {
        return ret;
    }

    DIR_ENTRY* dir = &(slot.dir);
    clus = dir->DIR_FstClusLO;    // 不是根目录
    if(!attr_is_directory(dir->DIR_Attr)) {
        return -ENOTDIR;
    }

    // 改成 if root else 的结构
    char sector_buffer[MAX_LOGICAL_SECTOR_SIZE];
    char name[MAX_NAME_LEN];
    while (is_cluster_inuse(clus)) {
        sector_t first_sec = cluster_first_sector(clus);
        size_t nsec = meta.sec_per_clus;
        fill_entries_in_sectors(first_sec, nsec, filler, buf);

        clus = read_fat_entry(clus);
    }
    
    return 0;
}

/**
 * @brief 从簇号为 clus 的簇的 offset 处开始读取 size 字节的数据到 data 中，并返回实际读取的字节数。
 * 
 * @param clus 簇号
 * @param offset 开始读取的偏移
 * @param data 输出缓冲区
 * @param size 要读取数据的长度
 * @return int 成功返回实际读取数据的长度，错误返回 POSIX 错误代码的负值
 */
int read_from_cluster_at_offset(cluster_t clus, off_t offset, char* data, size_t size) {
    assert(offset + size <= meta.cluster_size);  // offset + size 必须小于簇大小
    char sector_buffer[PHYSICAL_SECTOR_SIZE];
    /**
     * TODO: 2.2 从簇中读取数据 [约5行代码]
     * Hint: 步骤如下: 
     *       1. 计算 offset 对应的扇区号和扇区内偏移量。你可以用 cluster_first_sector 函数找到簇的第一个扇区。
     *          但 offset 可能超过一个扇区，所以你要计算出实际的扇区号和扇区内偏移量。
     *       2. 读取扇区（使用 sector_read）
     *       3. 将扇区正确位置的内容移动至 data 中正确位置
     *       你只需要补全以下 TODO 部分。主要是计算扇区号和扇区内偏移量；以及使用 memcpy 将数据移动到 data 中。
     */
    uint32_t sec = cluster_first_sector(clus) + offset / meta.sector_size; // TODO: 请填写正确的扇区号。
    size_t sec_off = offset % meta.sector_size; // TODO: 请填写正确的扇区内偏移量。
    size_t pos = 0; // 实际已经读取的字节数
    while(pos < size) { // 还没有读取完毕
        int ret = sector_read(sec, sector_buffer);
        if(ret < 0) {
            return ret;
        }
        // Hint: 使用 memcpy 挪数据，从 sec_off 开始挪。挪到 data 中哪个位置？挪多少？挪完后记得更新 pos （约3行代码）
        // ================== Your code here =================
        
        memcpy(data + pos, sector_buffer + sec_off, meta.sector_size - sec_off);
        pos += meta.sector_size - sec_off;
        
        // ===================================================
        sec_off = 0;
        sec ++ ;
    }
    return pos;
    // return size;
}

/**
 * @brief 从path对应的文件的 offset 字节处开始读取 size 字节的数据到 buffer 中，并返回实际读取的字节数。
 * Hint: 文件大小属性是 Dir.DIR_FileSize。
 * 
 * @param path    要读取文件的路径
 * @param buffer  结果缓冲区
 * @param size    需要读取的数据长度
 * @param offset  要读取的数据所在偏移量
 * @param fi      忽略
 * @return int    成功返回实际读写的字符数，失败返回0。
 */
int fat16_read(const char *path, char *buffer, size_t size, off_t offset,
               struct fuse_file_info *fi) {
    printf("read(path='%s', offset=%ld, size=%lu)\n", path, offset, size);
    if(path_is_root(path)) {
        return -EISDIR;
    }

    DirEntrySlot slot;
    DIR_ENTRY* dir = &(slot.dir);
    int ret = find_entry(path, &slot);  // 寻找文件对应的目录项，请查看并补全 find_entry 函数
    if(ret < 0) { // 寻找目录项出错
        return ret;
    }
    if(attr_is_directory(dir->DIR_Attr)) { // 找到的是目录
        return -EISDIR;
    }
    if(offset > dir->DIR_FileSize) { // 要读取的偏移量超过文件大小
        return -EINVAL;
    }
    size = min(size, dir->DIR_FileSize - offset);  // 读取的数据长度不能超过文件大小


    if(offset + size <= meta.cluster_size) {    // 文件在一个簇内的情况
        cluster_t clus = dir->DIR_FstClusLO;
        int ret = read_from_cluster_at_offset(clus, offset, buffer, size); // 请补全该函数
        return ret;
    }

    // 读取跨簇的文件
    cluster_t clus = dir->DIR_FstClusLO;
    size_t p = 0;   // 实际读取的字节数
    while(offset >= meta.cluster_size) {    // 移动到正确的簇号
        if(!is_cluster_inuse(clus)) {
            return -EUCLEAN;
        }
        offset -= meta.cluster_size;    // 如果 offset 大于一个簇的大小，需要移动到下一个簇，offset 减去一个簇的大小
        clus = read_fat_entry(clus);    // 请查看并补全 read_fat_entry 函数
    }

    /**
     * TODO: 4.2 读取多个簇中的数据 [约10行代码]
     * Hint: 你需要写一个循环，读取当前簇中的正确数据。步骤如下
     *       0. 写一个 while 循环，确认还有数据要读取，且簇号 clus 有效。
     *            1. 你可以使用 is_cluster_inuse() 函数
     *       1. 计算你要读取的（从offset开始的）数据长度。这有两种情况：
     *            1. 要读到簇的结尾，长度是？（注意 offset）
     *            2. 剩余的数据不需要到簇的结尾，长度是？
     *       2. 使用 read_from_cluster_at_offset 函数读取数据。注意检查返回值。
     *       3. 更新 p （已读字节数）、offset （下一个簇偏移）、clus（下一个簇号）。
     *          实际上，offset肯定是0，因为除了第一个簇，后面的簇都是从头开始读取。
     *          clus 则需要读取 FAT 表，别忘了你已经实现了 read_fat_entry 函数。
     */
    // ================== Your code here =================
    
    while (size > 0 && is_cluster_inuse(clus)) { // 确认还有数据要读取，且簇号有效
        size_t bytes_to_read = min(size, meta.cluster_size - offset); // 计算要读取的数据长度

        int ret = read_from_cluster_at_offset(clus, offset, buffer + p, bytes_to_read); // 读取数据到 buffer 中
        if (ret <= 0) { // 读取失败或已到达文件结尾
            break;
        }

        p += ret; // 更新已读字节数
        size -= ret; // 更新剩余要读取的字节数

        offset = 0; // 下一个簇的偏移量为0
        clus = read_fat_entry(clus); // 获取下一个簇号
    }
    
    // ===================================================
    return p;
}

int dir_entry_write(DirEntrySlot slot) {
    /**
     * TODO: 3.2 写入目录项 [3行代码]
     * Hint: （你只需补全带 TODO 的几行）
     *       使用 sector_write 将目录项写入文件系统。
     *       sector_write 只能写入一个扇区，但一个目录项只占用一个扇区的一部分。
     *       为了不覆盖其他目录项，你需要先读取该扇区，修改目录项对应的位置，然后再将整个扇区写回。
     */

    char sector_buffer[MAX_LOGICAL_SECTOR_SIZE];
    int ret = sector_read(slot.sector, sector_buffer); // TODO: 使用 sector_read 读取扇区
    if(ret < 0) {
        return ret;
    }
    memcpy(sector_buffer + slot.offset, &(slot.dir), sizeof(DIR_ENTRY)); // TODO: 使用 memcpy 将 slot.dir 里的目录项写入 buffer 中的正确位置
    ret = sector_write(slot.sector, sector_buffer); // TODO: 使用 sector_write 写回扇区
    if(ret < 0) {
        return ret;
    }
    return 0;
}

int dir_entry_create(DirEntrySlot slot, const char *shortname, 
            attr_t attr, cluster_t first_clus, size_t file_size) {
    DIR_ENTRY* dir = &(slot.dir);
    memset(dir, 0, sizeof(DIR_ENTRY));
    
    /**
     * TODO: 3.1 创建目录项 [约5行代码]
     * Hint: 请给目录项的 DIR_Name、Dir_Attr、DIR_FstClusHI、DIR_FstClusLO、DIR_FileSize 设置正确的值。
     *       你可以用 memcpy 函数来设置 DIR_Name。
     *       DIR_FstClusHI 在我们的系统中永远为 0。
     */

    // ================== Your code here =================
    
    memcpy(dir->DIR_Name, shortname, 11); // 设置目录项的文件名，shortname 是一个长度为 11 的字符串
    dir->DIR_Attr = attr;
    dir->DIR_FstClusHI = 0;
    dir->DIR_FstClusLO = first_clus;
    dir->DIR_FileSize = file_size;
    
    // ===================================================
    
    // 设置文件时间戳，无需修改
    struct timespec ts;
    int ret = clock_gettime(CLOCK_REALTIME, &ts);
    if(ret < 0) {
        return -errno;
    }
    time_unix_to_fat(&ts, &(dir->DIR_CrtDate), &(dir->DIR_CrtTime), &(dir->DIR_CrtTimeTenth));
    time_unix_to_fat(&ts, &(dir->DIR_WrtDate), &(dir->DIR_WrtTime), NULL);
    time_unix_to_fat(&ts, &(dir->DIR_LstAccDate), NULL, NULL);

    ret = dir_entry_write(slot);    // 请实现该函数
    if(ret < 0) {
        return ret;
    }
    return 0;
}


/**
 * @brief 在 path 对应的路径创建新文件（一个目录项对应一个文件，创建文件实际上就是创建目录项）
 * 
 * @param path    要创建的文件路径
 * @param mode    要创建文件的类型，本次实验可忽略，默认所有创建的文件都为普通文件
 * @param devNum  忽略，要创建文件的设备的设备号
 * @return int    成功返回0，失败返回POSIX错误代码的负值
 */
int fat16_mknod(const char *path, mode_t mode, dev_t dev) {
    printf("mknod(path='%s', mode=%03o, dev=%lu)\n", path, mode, dev);
    DirEntrySlot slot;
    const char* filename = NULL;
    int ret = find_empty_slot(path, &slot, &filename);  // 找一个空的目录项，如果正确实现了 find_entry_internal 函数，这里不需要修改
    if(ret < 0) {
        return ret;
    }

    char shortname[11];
    ret = to_shortname(filename, MAX_NAME_LEN, shortname); // 将长文件名转换为短文件名
    if(ret < 0) {
        return ret;
    }
    ret = dir_entry_create(slot, shortname, ATTR_REGULAR, 0, 0); // 创建目录项，请查看并补全 dir_entry_create 函数
    if(ret < 0) {
        return ret;
    }
    return 0;
}

/**
 * @brief 将data写入簇号为clusterN的簇对应的FAT表项，注意要对文件系统中所有FAT表都进行相同的写入。
 * 
 * @param clus  要写入表项的簇号
 * @param data      要写入表项的数据，如下一个簇号，CLUSTER_END（文件末尾），或者0（释放该簇）等等
 * @return int      成功返回0
 */
int write_fat_entry(cluster_t clus, cluster_t data) {
    char sector_buffer[MAX_LOGICAL_SECTOR_SIZE];
    size_t clus_off = clus * sizeof(cluster_t);
    sector_t clus_sec = clus_off / meta.sector_size;
    size_t sec_off = clus_off % meta.sector_size;
    for(size_t i = 0; i < meta.fats; i++) {
        /**
         * TODO: 6.2 修改FAT表项 [约10行代码，核心代码约4行]
         * Hint: 修改第 i 个 FAT 表中，clus_sec 扇区中，sec_off 偏移处的表项，使其值为 data
         *         1. 计算第 i 个 FAT 表所在扇区，进一步计算clus应的FAT表项所在扇区
         *         2. 读取该扇区并在对应位置修改数据
         *         3. 将该扇区写回
         */
        // ================== Your code here =================
        
        sector_t clus_sector = meta.fat_sec + i * meta.sec_per_fat + clus_sec;
        int ret = sector_read(clus_sector, sector_buffer);
        if(ret < 0) {
            return -EIO;
        }
        memcpy(sector_buffer + sec_off, &data, sizeof(cluster_t));
        ret = sector_write(clus_sector, sector_buffer);
        if (ret < 0)
            return ret;
        
        // ===================================================
    }
    return 0;
}

int free_clusters(cluster_t clus) {
    while(is_cluster_inuse(clus)) {
        cluster_t next = read_fat_entry(clus);
        int ret = write_fat_entry(clus, CLUSTER_FREE);
        if(ret < 0) {
            return ret;
        }
        clus = next;
    }
    return 0;
}


static const char ZERO_SECTOR[PHYSICAL_SECTOR_SIZE] = {0};
int cluster_clear(cluster_t clus) {
    sector_t first_sec = cluster_first_sector(clus);
    for(size_t i = 0; i < meta.sec_per_clus; i++) {
        sector_t sec = first_sec + i;
        int ret = sector_write(sec, ZERO_SECTOR);
        if(ret < 0) {
            return ret;
        }
    }
    return 0;
}

/**
 * @brief 分配一个空闲簇，将簇号写入 clus 中
 * 
 * @param clus 输出参数，用于保存分配的簇号
 * @return int 成功返回0，失败返回错误代码负值
 */
int alloc_one_cluster(cluster_t* clus) {
    /**
     * TODO: 6.3 分配一个空闲簇 [约15行代码]
     * Hint: 步骤如下
     *       1. 扫描FAT表，找到1个空闲的簇。
     *         1.1 找不到空簇，分配失败，返回 -ENOSPC。
     *       2. 修改空簇对应的FAT表项，将其指向 CLUSTER_END。同时清零该簇（使用 cluster_clear 函数）。
     *       3. 将 first_clus 设置为第一个簇的簇号，释放 clusters。
     */

    // ================== Your code here =================
    
    // 扫描 FAT 表，找到一个空闲的簇
    for (cluster_t i = 2; i < meta.clusters; i++) {
        cluster_t entry = read_fat_entry(i);
        if (entry == CLUSTER_FREE) {
            *clus = i;
            // 修改 FAT 表项，将其指向 CLUSTER_END
            int ret = write_fat_entry(i, CLUSTER_END);
            if (ret < 0) {
                return ret;
            }
            // 清零该簇
            ret = cluster_clear(i);
            if (ret < 0) {
                return ret;
            }
            return 0;
        }
    }
    // 找不到空闲的簇，分配失败
    return -ENOSPC;
    
    // ===================================================
    // return -ENOTSUP;    // TODO 请删除这一行或者修改为正确的返回值
}

/**
 * @brief 分配n个空闲簇，分配过程中将n个簇通过FAT表项连在一起，然后返回第一个簇的簇号。
 *        最后一个簇的FAT表项将会指向0xFFFF（即文件中止）。
 * @param n         要分配的簇数
 * @param first_clus 输出参数，用于保存第一个簇的簇号
 * @return int      成功返回0，失败返回错误代码负值
 */
int alloc_clusters(size_t n, cluster_t* first_clus) {
    if (n == 0)
        return CLUSTER_END;

    // 用于保存找到的n个空闲簇，另外在末尾加上CLUSTER_END，共n+1个簇号
    cluster_t *clusters = malloc((n + 1) * sizeof(cluster_t));
    size_t allocated = 0; // 已找到的空闲簇个数

    /**
     * TODO: 8.3 分配 n 个空闲簇
     * Hint: 步骤如下
     *       1. 扫描FAT表，找到n个空闲的簇，存入cluster数组。注意此时不需要修改对应的FAT表项。
     *         1.1 找不到n个簇，分配失败，记得 free(clusters)，返回 -ENOSPC。
     *       2. 修改clusters中存储的N个簇对应的FAT表项，将每个簇与下一个簇连接在一起。同时清零每一个新分配的簇。
     *         2.1 记得将最后一个簇连接至 CLUSTER_END。
     *       3. 将 first_clus 设置为第一个簇的簇号，释放 clusters。
     */


    // ================== Your code here =================
    
    for (cluster_t clus = CLUSTER_MIN; ((clus <= CLUSTER_MAX) && (allocated < n)); clus++) {
        if (read_fat_entry(clus) == CLUSTER_FREE) {
            printf("free cluster : %x\n", clus);
            clusters[allocated++] = clus;
        }
    }

    if (allocated < n) {
        free(clusters);
        return -ENOSPC;
    }

    clusters[n] = CLUSTER_END;

    for (size_t i = 0; i < n; i++) {
        write_fat_entry(clusters[i], clusters[i + 1]);
        cluster_clear(clusters[i]);
    }
    
    *first_clus = clusters[0];

    // ===================================================

    free(clusters);
    return 0;
}


/**
 * @brief 创建path对应的文件夹
 * 
 * @param path 创建的文件夹路径
 * @param mode 文件模式，本次实验可忽略，默认都为普通文件夹
 * @return int 成功:0， 失败: POSIX错误代码的负值
 */
int fat16_mkdir(const char *path, mode_t mode) {
    printf("mkdir(path='%s', mode=%03o)\n", path, mode);
    DirEntrySlot slot = {{}, 0, 0};
    const char* filename = NULL;
    cluster_t dir_clus = 0; // 新建目录的簇号
    int ret = 0;

    /**
     * TODO: 6.1 创建目录（文件夹） [约15行代码，核心代码约4行]
     * Hint: 参考 mknod，区别在于，这次你应该分配一个空闲簇，然后在这个簇中创建 . 和 .. 目录项。
     *       请调用 alloc_one_cluster(&dir_clus); 函数来分配簇。然后实现 alloc_one_cluster 函数。
     */

    // ================== Your code here =================

    ret = alloc_one_cluster(&dir_clus);
    if (ret < 0)
        return ret;
    // slot.dir.DIR_FstClusLO = dir_clus;
    
    ret = find_empty_slot(path, &slot, &filename);
    if(ret < 0) {
        return ret;
    }

    char shortname[11];
    ret = to_shortname(filename, MAX_NAME_LEN, shortname);
    if(ret < 0) {
        return ret;
    }
    ret = dir_entry_create(slot, shortname, ATTR_DIRECTORY, dir_clus, 0);
    if(ret < 0) {
        return ret;
    }
    
    // ===================================================

    // 设置 . 和 .. 目录项
    const char DOT_NAME[] =    ".          ";
    const char DOTDOT_NAME[] = "..         ";
    sector_t sec = cluster_first_sector(dir_clus);
    DirEntrySlot dot_slot = {.sector=sec, .offset=0};
    ret = dir_entry_create(dot_slot, DOT_NAME, ATTR_DIRECTORY, dir_clus, 0);
    if(ret < 0) {
        return ret;
    }
    DirEntrySlot dotdot_slot = {.sector=sec, .offset=DIR_ENTRY_SIZE};
    ret = dir_entry_create(dotdot_slot, DOTDOT_NAME, ATTR_DIRECTORY, sector_cluster(slot.sector), 0);
    if(ret < 0) {
        return ret;
    }
    return 0;
}

/**
 * @brief 删除path对应的文件
 * 
 * @param path  要删除的文件路径
 * @return int  成功返回0，失败返回POSIX错误代码的负值
 */
int fat16_unlink(const char *path) {
    printf("unlink(path='%s')\n", path);
    DirEntrySlot slot;
    DIR_ENTRY* dir = &(slot.dir);

    /**
     * TODO: 7.1 删除文件 [约15行代码，核心代码约5行]
     * Hint: 和创建文件类似，删除文件实际上就是删除目录项。步骤如下：
     *       找到目录项；确认目录项是个文件；释放占用的簇；修改目录项为删除；写回目录项；
     *       可能使用的函数：find_entry, attr_is_directory, free_clusters, dir_entry_write
     *       记得检查调用函数后的返回值。
     */
    // ================== Your code here =================
    
    int result;

    // 查找文件的目录项
    result = find_entry(path, &slot);
    if (result != 0) {
        return result;  // 如果没有找到或有其他错误，返回错误代码
    }

    // 检查目录项是否确实是文件
    if (attr_is_directory(dir->DIR_Attr)) {
        return -EISDIR;  // 不能删除目录
    }

    // 释放分配给文件的簇
    result = free_clusters(dir->DIR_FstClusLO);
    if (result != 0) {
        return result;  // 释放簇时出错
    }

    // 将目录项标记为已删除
    dir->DIR_Name[0] = NAME_DELETED;  // 在FAT16中，0xE5标记目录项为已删除

    // 将更新的目录项写回磁盘
    result = dir_entry_write(slot);
    if (result != 0) {
        return result;  // 写入目录项时出错
    }

    return 0;  // 成功
    
    // ===================================================
    // return -ENOTSUP; // TODO: 请修改返回值
}

/**
 * @brief 删除path对应的文件夹
 * 
 * @param path 要删除的文件夹路径
 * @return int 成功:0， 失败: POSIX错误代码的负值
 */
int fat16_rmdir(const char *path) {
    printf("rmdir(path='%s')\n", path);
    if(path_is_root(path)) {  // 根目录无法删除
        return -EBUSY;
    }

    /**
     * TODO: 7.2 删除目录（文件夹）[约50行代码]
     * Hint: 总体思路类似 readdir + unlink。具体如下：
     *       删除过程和删除文件类似，但注意，请**检查目录是否为空**，如果目录不为空，**不能删除**，且返回 -ENOTEMPTY。
     *       检查目录是否为空的过程和 readdir 类似，但是你需要检查每个目录项是否为空，而不是填充结果。
     */

    // ================== Your code here =================
    
    DirEntrySlot slot;
    DIR_ENTRY *dir = &(slot.dir);
    int result;

    // 查找目录的目录项
    result = find_entry(path, &slot);
    if (result != 0) {
        return result;  // 如果没有找到或有其他错误，返回错误代码
    }

    // 确认找到的是一个目录而不是文件
    if (!attr_is_directory(dir->DIR_Attr)) {
        return -ENOTDIR;  // 不是目录
    }

    // 检查目录是否为空（仅包含"."和".."）
    char sector_buffer[MAX_LOGICAL_SECTOR_SIZE];
    cluster_t clus = dir->DIR_FstClusLO;
    while (is_cluster_inuse(clus)) {
        sector_t first_sec = cluster_first_sector(clus);
        for (size_t i = 0; i < meta.sec_per_clus; i++) {
            sector_t sec = first_sec + i;
            int ret = sector_read(sec, sector_buffer);
            if (ret < 0)
                return -EIO;
            for (size_t off = 0; off < meta.sector_size; off += DIR_ENTRY_SIZE) {
                DIR_ENTRY* entry = (DIR_ENTRY*)(sector_buffer + off);
                if (de_is_valid(entry) && !de_is_dot(entry)) {
                    printf("error dir: '%s'\n", entry->DIR_Name);
                    return -ENOTEMPTY;
                }
                if (de_is_free(entry))
                    break;
            }
        }
        clus = read_fat_entry(clus);
    }

    printf("hello!\n");
    

    // 释放目录占用的簇
    result = free_clusters(dir->DIR_FstClusLO);
    if (result != 0) {
        return result;  // 释放簇时出错
    }

    // 将目录项标记为已删除
    dir->DIR_Name[0] = NAME_DELETED;

    // 将更新的目录项写回磁盘
    result = dir_entry_write(slot);
    if (result != 0) {
        return result;  // 写入目录项时出错
    }

    // sector_read(slot.sector, sector_buffer);
    // memset(sector_buffer + slot.offset, NAME_DELETED, 1);
    // sector_write(slot.sector, sector_buffer);

    return 0;  // 成功
    
    // ===================================================
    // return -ENOTSUP; // TODO: 请修改返回值
}

/**
 * @brief 修改path对应文件的时间戳，本次实验不做要求，可忽略该函数
 * 
 * @param path  要修改时间戳的文件路径
 * @param tv    时间戳
 * @return int 
 */
int fat16_utimens(const char *path, const struct timespec tv[2], struct fuse_file_info* fi) {
    printf("utimens(path='%s', tv=[%ld.%09ld, %ld.%09ld])\n", path, 
                tv[0].tv_sec, tv[0].tv_nsec, tv[1].tv_sec, tv[1].tv_nsec);
    DirEntrySlot slot;
    DIR_ENTRY* dir = &(slot.dir);
    int ret = find_entry(path, &slot);
    if(ret < 0) {
        return ret;
    }

    time_unix_to_fat(&tv[1], &(dir->DIR_WrtDate), &(dir->DIR_WrtTime), NULL);
    time_unix_to_fat(&tv[0], &(dir->DIR_LstAccDate), NULL, NULL);
    ret = dir_entry_write(slot);
    if(ret < 0) {
        return ret;
    }
    
    return 0;
}

/**
 * @brief 将data中的数据写入编号为clusterN的簇的offset位置。
 *        注意size+offset <= 簇大小
 * 
 * @param clus      要写入数据的块号
 * @param data      要写入的数据
 * @param size      要写入数据的大小（字节）
 * @param offset    要写入簇的偏移量
 * @return ssize_t  成功写入的字节数，失败返回错误代码负值。可能部分成功，此时仅返回成功写入的字节数，不提供错误原因（POSIX标准）。
 */
ssize_t write_to_cluster_at_offset(cluster_t clus, off_t offset, const char* data, size_t size) {
    assert(offset + size <= meta.cluster_size);  // offset + size 必须小于簇大小
    char sector_buffer[PHYSICAL_SECTOR_SIZE];

    /**
     * TODO: 8.2 写入数据到簇中 [约20行代码]
     * Hint: 参考 read_from_cluster_at_offset 函数。每个扇区实际上都要 读 -> 修改 -> 写。
     */

    // ================== Your code here =================
    
    uint32_t sec = cluster_first_sector(clus) + offset / meta.sector_size;
    size_t sec_off = offset % meta.sector_size;
    size_t pos = 0;
    while(pos < size) { // 还没有读取完毕
        size_t len = min(size - pos, meta.sector_size - sec_off);
        int ret = sector_read(sec, sector_buffer);
        if(ret < 0) {
            return ret;
        }
        memcpy(sector_buffer + sec_off, data + pos, len);
        ret = sector_write(sec, sector_buffer);
        if(ret < 0) {
            return ret;
        }
        pos += len;
        sec_off = 0;
        sec ++;
    }
    return pos;
    
    // ===================================================
    //return -ENOTSUP; // TODO: 请修改返回值
}

/**
 * @brief 将长度为size的数据data写入path对应的文件的offset位置。注意当写入数据量超过文件本身大小时，
 *        需要扩展文件的大小，必要时需要分配新的簇。
 * 
 * @param path    要写入的文件的路径
 * @param data    要写入的数据
 * @param size    要写入数据的长度
 * @param offset  文件中要写入数据的偏移量（字节）
 * @param fi      本次实验可忽略该参数
 * @return int    成功返回写入的字节数，失败返回POSIX错误代码的负值。
 */
int fat16_write(const char *path, const char *data, size_t size, off_t offset,
                struct fuse_file_info *fi) {
    printf("write(path='%s', offset=%ld, size=%lu)\n", path, offset, size);
    if(path_is_root(path)) {
        return -EISDIR;
    }

    DirEntrySlot slot;
    DIR_ENTRY* dir = &(slot.dir);
    int ret = find_entry(path, &slot);
    if(ret < 0) {
        return ret;
    }
    if(attr_is_directory(dir->DIR_Attr)) {
        return -EISDIR;
    }
    if(offset > dir->DIR_FileSize) {
        return -EINVAL;
    }
    if(size == 0) {
        return 0;
    }

    /**
     * TODO: 8.1 写入数据到文件中,必要时分配新簇。[约50行代码]
     * Hint: 参考实验文档中的实现思路。
     * 
     */
    // ================== Your code here =================
    
    size_t clus_num = 0;
    cluster_t last_clus = CLUSTER_END;
    cluster_t clus = dir->DIR_FstClusLO;
    while (is_cluster_inuse(clus)) {
        printf("cluster %x is in use.\n", clus);
        clus_num++;
        last_clus = clus;
        clus = read_fat_entry(clus);
    }
    // for (cluster_t clus = dir->DIR_FstClusLO; is_cluster_inuse(clus); clus = read_fat_entry(clus)) {
    //     printf("cluster %x is in use.\n", clus);
    //     clus_num++;
    //     last_clus = clus;
    // }
    
    if (size + offset > dir->DIR_FileSize) {
        dir->DIR_FileSize = size + offset;
        size_t alloc_num = (size + offset) / meta.cluster_size + 1 - clus_num;
        cluster_t first_clus;
        int ret = alloc_clusters(alloc_num, &first_clus);
        if (ret < 0)
            return ret;
        if (last_clus == CLUSTER_END)
            dir->DIR_FstClusLO = first_clus;
        else
            write_fat_entry(last_clus, first_clus);
    }

    clus = dir->DIR_FstClusLO;
    size_t pos = 0;
    while(pos < size) {
        if (offset > meta.cluster_size) {
            offset -= meta.cluster_size;
            clus = read_fat_entry(clus);
            continue;
        }
        else {
            ret = write_to_cluster_at_offset(clus, offset, data + pos, min(meta.cluster_size - offset, size - pos));
            if (ret < 0)
                return ret;
            pos += ret;
            offset = 0;
            clus = read_fat_entry(clus);
        }
    }

    ret = dir_entry_write(slot);
    if (ret < 0)
        return ret;

    return pos;

    // ===================================================
    // return -ENOTSUP; // TODO: 请修改返回值
}

/**
 * @brief 将path对应的文件大小改为size，注意size可以大于小于或等于原文件大小。
 *        若size大于原文件大小，需要将拓展的部分全部置为0，如有需要，需要分配新簇。
 *        若size小于原文件大小，将从末尾截断文件，若有簇不再被使用，应该释放对应的簇。
 *        若size等于原文件大小，什么都不需要做。
 * 
 * @param path 需要更改大小的文件路径 
 * @param size 新的文件大小
 * @return int 成功返回0，失败返回POSIX错误代码的负值。
 */
int fat16_truncate(const char *path, off_t size, struct fuse_file_info* fi) {
    printf("truncate(path='%s', size=%lu)\n", path, size);
    if(path_is_root(path)) {
        return -EISDIR;
    }

    DirEntrySlot slot;
    DIR_ENTRY* dir = &(slot.dir);
    int ret = find_entry(path, &slot);
    if(ret < 0) {
        return ret;
    }
    if(attr_is_directory(dir->DIR_Attr)) {
        return -EISDIR;
    }

    size_t old_size = dir->DIR_FileSize;
    if(old_size == size) {
        return 0;
    } else if(size > old_size) {
        size_t need_clus = (size + meta.cluster_size - 1) / meta.cluster_size;
        cluster_t clus = dir->DIR_FstClusLO;
        cluster_t last_clus = 0;
        while(is_cluster_inuse(clus)) {
            last_clus = clus;
            need_clus --;
            clus = read_fat_entry(clus);
        }

        cluster_t new;
        int ret = alloc_clusters(need_clus, &new);
        if(ret < 0) {
            return ret;
        }
        ret = write_fat_entry(last_clus, new);

        if(last_clus == 0) {
            dir->DIR_FstClusLO = new;
        }
    } else if(size < old_size) {
        size_t need_clus = (size + meta.cluster_size - 1) / meta.cluster_size;
        cluster_t clus = dir->DIR_FstClusLO;
        cluster_t last_clus = 0;
        while(need_clus > 0) {
            need_clus --;
            last_clus = clus;
            clus = read_fat_entry(clus);
        }
        if(last_clus == 0) {
            dir->DIR_FstClusLO = CLUSTER_FREE;
        } else {
            int ret;
            ret = free_clusters(clus);
            if(ret < 0) {
                return ret;
            }
            ret = write_fat_entry(last_clus, CLUSTER_END);
            if(ret < 0) {
                return ret;
            }
        }
    }

    dir->DIR_FileSize = size;
    dir_entry_write(slot);

    return 0;
}


struct fuse_operations fat16_oper = {
    .init = fat16_init,         // 文件系统初始化
    .destroy = fat16_destroy,   // 文件系统注销
    .getattr = fat16_getattr,   // 获取文件属性

    .readdir = fat16_readdir,   // 读取目录
    .read = fat16_read,         // 读取文件

    .mknod = fat16_mknod,       // 创建文件
    .unlink = fat16_unlink,     // 删除文件
    .utimens = fat16_utimens,   // 修改文件时间戳

    .mkdir = fat16_mkdir,       // 创建目录
    .rmdir = fat16_rmdir,       // 删除目录

    .write = fat16_write,       // 写文件
    .truncate = fat16_truncate  // 修改文件大小
};
