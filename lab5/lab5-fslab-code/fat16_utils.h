#ifndef FAT16_UTILS_H
#define FAT16_UTILS_H
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include "fat16.h"

#define ATTR_CONTAINS(attr, attr_name) ((attr & attr_name) != 0)

/**
 * @brief 这个函数不会被用到，只是为了让没有实现的部分也能通过编译。
 *        所有的这个函数调用都在 TODO 中，你需要替换成正确的数值或者表达式。
 *        这个函数被运行时，程序会直接退出。 
 */
int64_t _placeholder_() {
    printf("_placeholder_ 函数被调用，这意味着代码运行到了你未实现的部分。\n");
    printf("为防止程序出错，文件系统会立即退出。\n");
    exit(-1);
}

/**
 * @brief 簇号是否是合法的（表示正在使用的）数据簇号（在CLUSTER_MIN和CLUSTER_MAX之间）
 * 
 * @param clus 簇号
 * @return int        
 */
bool is_cluster_inuse(cluster_t clus) {
    return CLUSTER_MIN <= clus && clus <= CLUSTER_MAX;
}

bool attr_is_readonly(attr_t attr) {
    return (attr & ATTR_READONLY) != 0;
}

bool attr_is_directory(attr_t attr) {
    return (attr & ATTR_DIRECTORY) != 0;
}

bool attr_is_lfn(attr_t attr) {
    return attr == ATTR_LFN;
}

bool de_is_free(DIR_ENTRY* dir) {
    return dir->DIR_Name[0] == NAME_FREE;
}

bool de_is_deleted(DIR_ENTRY* dir) {
    return dir->DIR_Name[0] == NAME_DELETED;
}

bool de_is_valid(DIR_ENTRY* dir) {
    const uint8_t* name = dir->DIR_Name;
    attr_t attr = dir->DIR_Attr;
    return !attr_is_lfn(attr) && name[0] != NAME_DELETED && name[0] != NAME_FREE;
}

bool de_is_dot(DIR_ENTRY* dir) {
    if(attr_is_lfn(dir->DIR_Attr)) {
        return false;
    }
    const char* name = (const char *)dir->DIR_Name;
    attr_t attr = dir->DIR_Attr;
    const char DOT_NAME[] =    ".          ";
    const char DOTDOT_NAME[] = "..         ";
    return strncmp(name, DOT_NAME, FAT_NAME_LEN) == 0 || strncmp(name, DOTDOT_NAME, FAT_NAME_LEN) == 0;
}

bool path_is_root(const char* path) {
    path += strspn(path, "/");
    return *path == '\0';
}

// 抄袭https://elixir.bootlin.com/linux/latest/source/fs/fat/namei_msdos.c#L19
const char INVALID_CHARS[] = "*?<>|\"+=,; :\\";
int to_shortname(const char* name, size_t len, char* res) {
    bool has_ext = false;
    size_t base_len = len;
    for(size_t i = 0; i < len; i++) {
        if(name[i] == '\0') {
            len = i;
            base_len = min(base_len, len);
            break;
        }
        if(strchr(INVALID_CHARS, name[i]) != NULL) {
            return -EINVAL;
        }
        if(name[i] == '.' && i != 0) {
            has_ext = true;
            base_len = i;
        }
    }

    // 转换文件名
    memset(res, ' ', FAT_NAME_LEN);
    for(size_t i = 0; i < base_len && i < FAT_NAME_BASE_LEN; i++) {
        res[i] = toupper(name[i]);
    }
    // 0xe5用来代表删除，如果首个字母为0xe5,需要转换为0x05
    res[0] = (res[0] == 0xe5) ? 0x05 : res[0];

    // 转换拓展名
    if(has_ext) {
        const char* ext = name + base_len + 1;
        size_t ext_len = len - base_len - 1;
        for(size_t i = 0; i < ext_len && i < FAT_NAME_EXT_LEN; i++) {
            res[FAT_NAME_BASE_LEN + i] = toupper(ext[i]);
        }
    }
	return 0;
}

/**
 * @brief 将 FAT 格式的文件名转换为正常文件名
 * 
 * @param fat_name FAT格式的文件名
 * @param res 用于存放结果的缓冲区
 * @param len res 缓冲区的大小，用于确保名字不会超出这个长度
 * @return int 
 */
int to_longname(const uint8_t fat_name[11], char* res, size_t len) {
    len --;  // last char for '\0' 
    size_t i = 0;
    while(i < len && i < FAT_NAME_BASE_LEN) {
        if(fat_name[i] == ' ') {
            break;
        }
        res[i] = tolower(fat_name[i]);
        i++;
    }

    if(fat_name[FAT_NAME_BASE_LEN] != ' ') {
        res[i++] = '.';
        for(size_t j = FAT_NAME_BASE_LEN; i < len && j < FAT_NAME_LEN; j++) {
            if(fat_name[j] == ' ') {
                break;
            }
            res[i] = tolower(fat_name[j]);
            i++;
        }
    }

    res[i] = '\0';
    return i;
}

 
bool check_name(const char* name, size_t len, const DIR_ENTRY* dir) {
    char fatname[11];
    to_shortname(name, len, fatname);
    return strncmp(fatname, (const char *)dir->DIR_Name, FAT_NAME_LEN) == 0;
}


void time_fat_to_unix(struct timespec* ts, uint16_t date, uint16_t time, uint16_t acc_time) {
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = (date >> 9) + 80;           // 7位年份
    t.tm_mon  = ((date >> 5) & 0xf) - 1;     // 4位月份
    t.tm_mday = date & 0x1f;              // 5位日期

    t.tm_hour = time >> 11;                 // 5位小时
    t.tm_min  = (time >> 5) & 0x3f;          // 6位分钟
    t.tm_sec  = (time & 0x1f) * 2;           // 5位秒，需要乘2，精确到2秒

    ts->tv_sec = mktime(&t) + (acc_time / 100);
    ts->tv_nsec = (acc_time % 100) * 10000000;
}

void time_unix_to_fat(const struct timespec* ts, uint16_t* date, uint16_t* time, uint8_t* acc_time) {
    struct tm* t = gmtime(&(ts->tv_sec));
    *date = 0;
    *date |= ((t->tm_year - 80) << 9);
    *date |= ((t->tm_mon + 1) << 5);
    *date |= t->tm_mday;
    
    if(time != NULL) {
        *time = 0;
        *time |= (t->tm_hour << 11);
        *time |= (t->tm_min << 5);
        *time |= (t->tm_sec / 2);
    }

    if(acc_time != NULL) {
        *acc_time = (t->tm_sec % 2) * 100;
        *acc_time += ts->tv_nsec / 10000000;
    }
}

#endif // FAT16_UTILS_H