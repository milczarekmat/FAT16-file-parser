#ifndef FAT_PROJEKT_FILE_READER_H
#define FAT_PROJEKT_FILE_READER_H

#define BYTES_PER_SECTOR 512
#define FAT_DELETED ((char)0xE5)
#define SIZE_OF_DIRECTORY_ENTRY 32
#define SIZE_OF_FILENAME 8
#define SIZE_OF_EXTENSION 3
#define LAST_CLUSTER 0xfff8

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <math.h>

typedef uint32_t lba_t; // sektory
typedef uint32_t cluster_t; // klastry

struct date_t{
    unsigned int day;
    unsigned int month;
    unsigned int year;
};

struct time_t{
    unsigned int hour;
    unsigned int minute;
    unsigned int second;
};

enum attribs_t{
    FAT_ATTRIB_READ_ONLY = 0x01,
    FAT_ATTRIB_HIDDEN = 0x02,
    FAT_ATTRIB_SYSTEM = 0x04,
    FAT_ATTRIB_VOLUME_LABEL = 0x08,
    FAT_ATTRIB_DIRECTORY = 0x10,
    FAT_ATTRIB_ARCHIVE = 0x20,
};

struct disk_t{
    FILE* fp;
    lba_t disk_size;
};

struct fat_super_t {
    uint8_t __jump_code[3];
    char oem_name[8];

    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t fat_count;
    uint16_t root_dir_capacity;
    uint16_t logical_sectors16;
    uint8_t __reserved;
    uint16_t sectors_per_fat;

    uint32_t __reserved2;

    uint32_t hidden_sectors;
    uint32_t logical_sectors32;

    uint16_t __reserved3;
    uint8_t __reserved4;

    uint32_t serial_number;

    char label[11];
    char fsid[8];

    uint8_t __boot_code[448];
    uint16_t validate_num; // 55 aa
} __attribute__(( packed ));

struct volume_t{
    struct disk_t* disk;
    struct fat_super_t* psuper;
    lba_t volume_size;
    lba_t user_space;
    cluster_t total_clusters;
    lba_t volume_start;
    lba_t* fat_positions;
    lba_t dir_position;
    lba_t sectors_per_dir;
    cluster_t data_cluster_2; //2-indeks dla pierwszego niezarezerwowanego klastra z danymi
    uint32_t bytes_per_cluster;
};

struct dir_entry_t{
    char filename[SIZE_OF_FILENAME]; // bez rozszerzenia pliku
    char ext[SIZE_OF_EXTENSION]; // rozszerzenie
    uint8_t attrib;
    uint8_t reserved;
    uint8_t creation_tenths;
    uint16_t time_format;
    uint16_t date_format;
    uint16_t last_accessed_date;
    uint16_t high_cluster_index; // w fat 12 i 16 zawsze równe zero
    uint16_t last_modification[2];
    uint16_t low_cluster_index;
    uint32_t size;
    //todo last change
    char name[12];
    struct date_t creation_date;
    struct time_t creation_time;
    uint8_t is_archived : 1; // - wartość atrybutu: plik zarchiwizowany (0 lub 1),
    uint8_t is_readonly : 1; // - wartość atrybutu: plik tylko do odczytu (0 lub 1),
    uint8_t is_system : 1; // - wartość atrybutu: plik jest systemowy (0 lub 1),
    uint8_t is_hidden : 1; // - wartość atrybutu: plik jest ukryty (0 lub 1),
    uint8_t is_directory : 1; //- wartość atrybutu: katalog (0 lub 1).
    uint8_t is_volume_label : 1; //- wartość atrybutu: katalog (0 lub 1).
}__attribute__(( packed ));

struct entry_formatted_t{
    char name[12]; // - nazwa pliku/katalogu bez nadmiarowych spacji oraz z kropką separującą nazwę od rozszerzenia,
    struct date_t creation_date;
    struct time_t creation_time;
    uint8_t is_archived : 1; // - wartość atrybutu: plik zarchiwizowany (0 lub 1),
    uint8_t is_readonly : 1; // - wartość atrybutu: plik tylko do odczytu (0 lub 1),
    uint8_t is_system : 1; // - wartość atrybutu: plik jest systemowy (0 lub 1),
    uint8_t is_hidden : 1; // - wartość atrybutu: plik jest ukryty (0 lub 1),
    uint8_t is_directory : 1; //- wartość atrybutu: katalog (0 lub 1).
    uint8_t is_volume_label : 1; //- wartość atrybutu: katalog (0 lub 1).
}__attribute__(( packed ));

void fill_entry_structure(struct dir_entry_t *entry, struct entry_formatted_t* entry_formatted);

struct file_t{
    struct volume_t* volume;
    char filename[11];
    cluster_t first_cluster_index;
    uint32_t clusters_size_in_bytes;
    int32_t current_position;
    int16_t current_position_in_cluster;
    cluster_t current_cluster;
    uint16_t *clusters;
    size_t clusters_number;
    uint32_t file_size;
};

struct dir_t{
    struct volume_t* volume;
    char name[11];
    cluster_t first_cluster_index;
    uint32_t clusters_size_in_bytes;
    int32_t current_position;
    int16_t current_position_in_cluster;
    cluster_t current_cluster;
    uint16_t *clusters;
    size_t clusters_number;
    uint32_t directory_size;
};

void get_chain_fat16(struct file_t* file, const void* const buffer,
                     uint16_t first_cluster);
uint16_t* get_fat_table(struct volume_t* volume);

struct disk_t* disk_open_from_file(const char* volume_file_name);
int disk_read(struct disk_t* pdisk, int32_t first_sector, void* buffer, int32_t sectors_to_read);
int disk_close(struct disk_t* pdisk);

struct volume_t* fat_open(struct disk_t* pdisk, uint32_t first_sector);
int fat_close(struct volume_t* pvolume);
int check_if_fats_table_are_the_same(struct disk_t* pdisk, struct volume_t* volume);

struct file_t* find_file_entry(struct volume_t* volume, const char* filename);
struct file_t* file_open(struct volume_t* pvolume, const char* file_name);
int file_close(struct file_t* stream);
size_t file_read(void *ptr, size_t size, size_t nmemb, struct file_t *stream);
int32_t file_seek(struct file_t* stream, int32_t offset, int whence);


struct dir_t* dir_open(struct volume_t* pvolume, const char* dir_path);
int dir_read(struct dir_t* pdir, struct dir_entry_t* pentry);
int dir_close(struct dir_t* pdir);

#endif
