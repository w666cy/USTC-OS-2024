# Lab5 FAT文件系统实现 report

**PB22111664 王晨烨**

## 1 实验目的

- 熟悉文件系统的基本功能与工作原理（理论基础）
- 熟悉FAT16的存储结构，利用FUSE实现一个FAT文件系统：
  - 根目录下，文件与目录的读操作、创建操作（仅根目录文件系统，基础）
  - 非根目录下的文件与目录的读、创建操作（进阶）
  - 文件与目录的删除操作（进阶）
  - 文件的写操作（进阶）

## 2 实验环境

- VMware / VirtualBox
- OS: Ubuntu 22.04 LTS
- Linux内核版本: 5.9.0+
- libfuse3

## 3 实验内容

### 3.1.1 读目录

​	我们读取目录内容的方式是调用`fill_entries_in_sectors()`函数，从一个起始扇区开始，读取给定数量的扇区。本部分内容只要求实现根目录的读取，由于根目录在FAT文件系统的磁盘上有专门的分区，因此访问文件系统的元数据`meta`（FAT16结构体）就能获得根目录的扇区信息，根目录的起始扇区为`root_sec`，扇区数目为`root_sectors`.

```c
    if(path_is_root(path)) {
        sector_t first_sec = meta.root_sec; 
        size_t nsec = meta.root_sectors; 
        fill_entries_in_sectors(first_sec, nsec, filler, buf);
        return 0;
    }
```



### 3.1.2  读取目录名和文件名

​	我们的读目录功能实际上是要实现类似`ls`的命令，因此我们实际要读取的内容是这个目录下所有的目录名和文件名，为此我们要找到这个目录的存储扇区中的所有有效目录项。从目录的起始扇区`first_sec`开始，循环遍历`nsec`个扇区；对于每个扇区`sec`，我们要获得其具体的内容才能从中找到有效目录项，所以先调用`sector_read()`函数将扇区的内容读到缓存`sector_buffer[]`中，然后在缓存中不断向后偏移，读取一个目录项大小`DIR_ENTRY_SIZE`的内容到`entry`，直到遍历完一个扇区大小`meta.sector_size`的内容；对于这样得到的一个目录项，调用`de_is_valid()`判断其是否有效，如果有效则将该目录项对应的文件名部分转化为我们需要的文件名格式；如果遇到空闲目录项则直接返回，因为我们创建目录项的方式保证了空闲目录项之后不会再有有效目录项。

```c
int fill_entries_in_sectors(sector_t first_sec, size_t nsec, fuse_fill_dir_t filler, void* buf) {
    char sector_buffer[MAX_LOGICAL_SECTOR_SIZE];
    char name[MAX_NAME_LEN];
    for(size_t i=0; i < nsec; i++) {
        sector_t sec = first_sec + i;
        int ret = sector_read(sec, sector_buffer);
        if(ret < 0) {
            return -EIO;
        }
        for(size_t off = 0; off < meta.sector_size; off += DIR_ENTRY_SIZE) {
            DIR_ENTRY* entry = (DIR_ENTRY*)(sector_buffer + off);
            if(de_is_valid(entry)) {
                int ret = to_longname(entry->DIR_Name, name, MAX_NAME_LEN);
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
```



### 3.2.1 寻找目录和文件

​	我们对目录或文件进行操作的第一步是要找到对应目录项的位置，即所在扇区和在扇区中的偏移。实现的原理基本和3.1.2读取目录项类似，不同的是找到一个有效目录项时需要将其文件名与我们寻找的文件名进行比对，如果相同则记录目录项内容和位置；如果找到的是一个空闲目录项，说明我们寻找的文件不存在，将该空闲目录项的位置记录下来，后续需要使用。

```c
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
```



### 3.2.2 读取数据

​	要求从给定簇`clus`的`offset`位置开始读取数据，而我们读内容的方式`sector_read()`是以扇区为单位进行，所以调用`cluster_first_sector()`获得给定簇的起始扇区，再由`offset`计算读取位置所在的扇区及在扇区中的偏移，然后就开始以扇区为单位将数据复制到`data`中。

```c
int read_from_cluster_at_offset(cluster_t clus, off_t offset, char* data, size_t size) {
    assert(offset + size <= meta.cluster_size);
    char sector_buffer[PHYSICAL_SECTOR_SIZE];

    uint32_t sec = cluster_first_sector(clus) + offset / meta.sector_size; 
    size_t sec_off = offset % meta.sector_size; 
    size_t pos = 0;
  
    while(pos < size) {
        int ret = sector_read(sec, sector_buffer);
        if(ret < 0) {
            return -EIO;
        }
      
        memcpy(data + pos, sector_buffer + sec_off, meta.sector_size - sec_off);
        pos += meta.sector_size - sec_off;
        sec_off = 0;
        sec++;
    }
  
    return pos;
}
```



### 3.3.1 创建目录项

​	如果我们要创建的目录或文件不存在，我们在3.2.1中会得到一个记录空闲目录项位置的`slot`，根据创建文件的参数设置`slot`目录项的目录属性，最后调用`dir_entry_write()`将`slot`的目录项内容写到记录的空闲位置。

```c
    memcpy(dir->DIR_Name, shortname, 11);
    dir->DIR_Attr = attr;
    dir->DIR_FstClusHI = 0;
    dir->DIR_FstClusLO = first_clus;
    dir->DIR_FileSize = file_size;
```



### 3.3.2 目录项写回

​	同`sector_read()`一样，我们写内容的方式`sector_write()`也是以扇区为单位进行，所以为了写入目录项，要先将目录项所在的扇区内容读到缓存中进行修改，然后写回。

```c
int dir_entry_write(DirEntrySlot slot) {
    char sector_buffer[MAX_LOGICAL_SECTOR_SIZE];
    int ret = sector_read(slot.sector, sector_buffer); 
    if(ret < 0) {
        return -EIO;
    }
    memcpy(sector_buffer + slot.offset, &(slot.dir), sizeof(DIR_ENTRY));
    ret = sector_write(slot.sector, sector_buffer);
    if(ret < 0) {
        return ret;
    }
    return 0;
}
```



### 3.4.1 读FAT表项

​	读取FAT表项的值同样需要知道表项所在的扇区及在扇区中的偏移，FAT表在磁盘上有专门的分区，可由元数据获得扇区信息，簇号在FAT表中顺序排列，由簇号获得表项在FAT表中的偏移。

```c
cluster_t read_fat_entry(cluster_t clus) {
    char sector_buffer[MAX_LOGICAL_SECTOR_SIZE];
    size_t fat_entry_offset = clus * 2;
    sector_t fat_sector = meta.fat_sec + fat_entry_offset / meta.sector_size;
    size_t fat_sector_offset = fat_entry_offset % meta.sector_size;
    
    int ret = sector_read(fat_sector, sector_buffer);
    if (ret < 0) {
        return -EIO;
    }

    cluster_t fat_entry;
    memcpy(&fat_entry, sector_buffer + fat_sector_offset, 16);

    return fat_entry;
}
```



### 3.4.2 读取文件

​	我们读取数据（3.2.2）的方式以簇为单位进行，因此在找到文件目录项后，先判读读取的数据是不是在文件的第一个簇中，是则直接读取；不是则根据读取位置的偏移找到第一个要读取的簇，然后根据要读取数据的大小一个簇一个簇地读取。

```c
		 while (size > 0 && is_cluster_inuse(clus)) {
        size_t bytes_to_read = min(size, meta.cluster_size - offset);

        int ret = read_from_cluster_at_offset(clus, offset, buffer + p, bytes_to_read);
        if (ret <= 0) {
            break;
        }

        p += ret;
        size -= ret;

        offset = 0;
        clus = read_fat_entry(clus);
    }
```



### 3.5.1 寻找目录项

​	3.2.1中寻找目录项是在给定的扇区中搜索，该部分从根目录开始，按照文件路径一层层向下搜索上一层找到的目录项的簇中的扇区。在根目录扇区成功搜索到第一层目录项的情况下，能继续向下搜索的条件是未达到最后一层且该层目录项不是文件，此时我们从该目录项对应目录的第一个簇开始搜索，依链接顺序遍历它的簇直到

1. `state < 0`，搜索结果异常，下面的结果处理会直接返回异常；
2. `state == FIND_EXIST`，找到下一层的目录项，如果下一层为最后一层则直接返回，若不是最后一层且目录项为目录，此时`slot`已经正确记录了目录的内容，将簇号设置为`slot`目录项的的第一个簇，开始下一层的遍历；
3. `state == FIND_EMPTY`，搜索失败；
4. 遍历完目录项的所有簇，应有`state`等于`FIND_FULL`，搜索失败。

```c
    cluster_t clus = slot->dir.DIR_FstClusLO;
    *remains = next_level;
    while (true) {
        size_t len = strcspn(*remains, "/"); 
        
        while (is_cluster_inuse(clus)) {
            first_sec = cluster_first_sector(clus);
            nsec = meta.sec_per_clus;
            state = find_entry_in_sectors(*remains, len, first_sec, nsec, slot);

            if (state < 0 || state == FIND_EXIST || state == FIND_EMPTY)
                break;
            clus = read_fat_entry(clus);
        }

        const char* next_level = *remains + len;
        next_level += strspn(next_level, "/");

        if(state < 0 || *next_level == '\0') {
            return state;
        }
        if(state != FIND_EXIST) {
            return -ENOENT;
        }
        if(!attr_is_directory(slot->dir.DIR_Attr)) {
            return -ENOTDIR;
        }

        *remains = next_level;
        clus = slot->dir.DIR_FstClusLO;
    }
    return -EUCLEAN;
```



### 3.6.1 创建目录

​	创建一个目录首先要找到一个空闲目录项，此外目录创建时应带有指向父目录和自身的两个目录项，所以还需要为创建的目录项分配一个簇用来存放父目录项和自身目录项，最后根据3.3.1进行创建，目录项属性要设置为目录。

```c
   	ret = find_empty_slot(path, &slot, &filename);
    if(ret < 0)
        return ret;

		ret = alloc_one_cluster(&dir_clus);
    if (ret < 0)
        return ret;
    
    char shortname[11];
    ret = to_shortname(filename, MAX_NAME_LEN, shortname);
    if(ret < 0)
        return ret;

    ret = dir_entry_create(slot, shortname, ATTR_DIRECTORY, dir_clus, 0);
    if(ret < 0)
        return ret;
```



### 3.6.2 写FAT表项

​	在3.4.1读的基础上增加写回操作。

```c
    for(size_t i = 0; i < meta.fats; i++) {
        sector_t clus_sector = meta.fat_sec + i * meta.sec_per_fat + clus_sec;
        int ret = sector_read(clus_sector, sector_buffer);
        if(ret < 0) 
            return -EIO;
        memcpy(sector_buffer + sec_off, &data, sizeof(cluster_t));
        ret = sector_write(clus_sector, sector_buffer);
        if (ret < 0)
            return -EIO;
    }
```



### 3.6.3 分配一个空闲簇

​	遍历簇号，如果其对应的FAT表项值为空闲，则将表项值修改为文件结束并清空簇的内容。

```c
int alloc_one_cluster(cluster_t* clus) {
    for (cluster_t i = CLUSTER_MIN; i <= CLUSTER_MAX; i++) {
        cluster_t entry = read_fat_entry(i);
        if (entry == CLUSTER_FREE) {
            *clus = i;
          
            int ret = write_fat_entry(i, CLUSTER_END);
            if (ret < 0) {
                return ret;
            }
            
            ret = cluster_clear(i);
            if (ret < 0) {
                return ret;
            }
          
            return 0;
        }
    }
    return -ENOSPC;
}
```



### 3.7.1 删除文件

​	找到文件的目录项，确认属性为文件，释放文件的簇（修改FAT表项为空闲），标记文件为删除，写回目录项（修改了文件名）。

```c
int fat16_unlink(const char *path) {
    printf("unlink(path='%s')\n", path);
    DirEntrySlot slot;
    DIR_ENTRY* dir = &(slot.dir);
    int result;

    result = find_entry(path, &slot);
    if (result != 0)
        return result;

    if (attr_is_directory(dir->DIR_Attr))
        return -EISDIR;

    result = free_clusters(dir->DIR_FstClusLO);
    if (result != 0)
        return result;

    dir->DIR_Name[0] = NAME_DELETED;

    result = dir_entry_write(slot);
    if (result != 0) 
        return result;

    return 0;
}
```



### 3.7.2 删除目录

​	基本框架同3.7.1，但在释放目录的簇之前，需要检查目录是否为空，即是否只有父目录项和自身目录项，检查目录内容的框架同3.1.2。

```c
    char sector_buffer[MAX_LOGICAL_SECTOR_SIZE];
    cluster_t clus = dir->DIR_FstClusLO;
    bool find_free = false;
    while (is_cluster_inuse(clus)) {
        find_free = false;
        sector_t first_sec = cluster_first_sector(clus);
        for (size_t i = 0; i < meta.sec_per_clus; i++) {
            sector_t sec = first_sec + i;
            int ret = sector_read(sec, sector_buffer);
            if (ret < 0)
                return -EIO;
            for (size_t off = 0; off < meta.sector_size; off += DIR_ENTRY_SIZE) {
                DIR_ENTRY* entry = (DIR_ENTRY*)(sector_buffer + off);
                if (de_is_valid(entry) && !de_is_dot(entry)) 
                    return -ENOTEMPTY;
                if (de_is_free(entry)) {
                    find_free = true;
                    break;
                }
            }
            if (find_free)
                break;
        }
        clus = read_fat_entry(clus);
    }
```



## 4 实验结果

​	上面的内容成功通过了自动测试程序前7个任务点的检查，最后1个任务点的代码因为存在还未解决的导致簇环的bug目前未在报告中给出。



## 5 实验总结

​	通过本次实验，我对FAT文件系统的工作原理有了深刻的理解；此外，大量的函数定义和调用极大地锻炼了我的代码能力。
