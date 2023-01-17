#include "file_reader.h"

struct disk_t* disk_open_from_file(const char* volume_file_name){
    if (!volume_file_name){
        errno = EFAULT;
        return NULL;
    }
    struct disk_t* new_disk = malloc(sizeof(struct disk_t));
    if (!new_disk){
        errno = ENOMEM;
        return NULL;
    }
    new_disk->fp = fopen(volume_file_name, "rb");
    if (!new_disk->fp){
        free(new_disk);
        errno = ENOENT;
        return NULL;
    }
    fseek(new_disk->fp, 0, SEEK_END);
    new_disk->disk_size = ftell(new_disk->fp)/BYTES_PER_SECTOR;
    return new_disk;
}

int disk_read(struct disk_t* pdisk, int32_t first_sector, void* buffer, int32_t sectors_to_read){
    if (!pdisk || !buffer){
        errno = EFAULT;
        return -1;
    }
    if (!pdisk->fp){
        errno = EFAULT;
        return -1;
    }
    if ((lba_t)first_sector + sectors_to_read > pdisk->disk_size){
        errno = ERANGE;
        return -1;
    }
    fseek(pdisk->fp, first_sector * BYTES_PER_SECTOR, SEEK_SET);
    return fread(buffer, BYTES_PER_SECTOR, sectors_to_read, pdisk->fp);
}

int disk_close(struct disk_t* pdisk){
    if (!pdisk){
        errno = EFAULT;
        return -1;
    }
    if (!pdisk->fp){
        errno = EFAULT;
        free(pdisk);
        return -1;
    }
    fclose(pdisk->fp);
    free(pdisk);
    return 0;
}

struct volume_t* fat_open(struct disk_t* pdisk, uint32_t first_sector){
    if (!pdisk){
        errno = EFAULT;
        return NULL;
    }
    struct volume_t* volume = malloc(sizeof(struct volume_t));
    if (!volume){
        errno = ENOMEM;
        return NULL;
    }
    volume->psuper = NULL;
    volume->fat_positions = NULL;
    volume->disk = pdisk;

    volume->psuper = malloc(sizeof(struct fat_super_t));
    if (!volume->psuper){
        errno = ENOMEM;
        fat_close(volume);
        return NULL;
    }


    if (disk_read(pdisk, volume->volume_start, volume->psuper, sizeof(struct fat_super_t)/BYTES_PER_SECTOR) == -1){
        return NULL;
    }

    if (volume->psuper->validate_num != 0xAA55){
        errno = EINVAL;
        fat_close(volume);
        return NULL;
    }

    volume->volume_start = first_sector;
    volume->volume_size = volume->psuper->logical_sectors16 == 0 ?
                          volume->psuper->logical_sectors32 : volume->psuper->logical_sectors16;
    volume->dir_position = volume->volume_start  + volume->psuper->reserved_sectors +
                           volume->psuper->fat_count * volume->psuper->sectors_per_fat;
    assert(volume->volume_size <= pdisk->disk_size);
    volume->sectors_per_dir = (volume->psuper->root_dir_capacity * SIZE_OF_DIRECTORY_ENTRY) / volume->psuper->bytes_per_sector;
    volume->data_cluster_2 = volume->dir_position + volume->sectors_per_dir;
    volume->bytes_per_cluster = volume->psuper->sectors_per_cluster * volume->psuper->bytes_per_sector;
    if ((volume->psuper->root_dir_capacity * SIZE_OF_DIRECTORY_ENTRY) % volume->psuper->bytes_per_sector != 0){
        volume->sectors_per_dir++;
    }

    volume->fat_positions = calloc(volume->psuper->fat_count, sizeof(lba_t));
    if (!volume->fat_positions){
        errno = ENOMEM;
        fat_close(volume);
        return NULL;
    }
    for (int i=0; i<volume->psuper->fat_count; i++){
        volume->fat_positions[i] = volume->volume_start + volume->psuper->reserved_sectors + (i*volume->psuper->sectors_per_fat);
    }

    int check = check_if_fats_table_are_the_same(pdisk, volume);
    if (check == -1 || check == -2){
        return NULL;
    }
    else if (check == 1){
        errno = EINVAL;
        return NULL;
    }

    return volume;
}

int check_if_fats_table_are_the_same(struct disk_t* pdisk, struct volume_t* volume){
    uint8_t* first_fat_table = malloc(volume->psuper->sectors_per_fat * volume->psuper->bytes_per_sector);
    uint8_t* second_fat_table = malloc(volume->psuper->sectors_per_fat * volume->psuper->bytes_per_sector);
    if (!first_fat_table || !second_fat_table){
        errno = ENOMEM;
        fat_close(volume);
        return -1;
    }
    for (int i=0; i<volume->psuper->fat_count-1; i++){
        int check1 = disk_read(pdisk, volume->fat_positions[i], first_fat_table, volume->psuper->sectors_per_fat);
        int check2 = disk_read(pdisk, volume->fat_positions[i+1], second_fat_table, volume->psuper->sectors_per_fat);
        if (check1 == -1 || check2 == -1){
            fat_close(volume);
            free(first_fat_table);
            free(second_fat_table);
            return -2;
        }
        if (memcmp(first_fat_table, second_fat_table,
                   volume->psuper->sectors_per_fat * volume->psuper->bytes_per_sector)){
            fat_close(volume);
            free(first_fat_table);
            free(second_fat_table);
            return 1;
        }
    }
    free(first_fat_table);
    free(second_fat_table);
    return 0;
}

int fat_close(struct volume_t* pvolume){
    if (!pvolume){
        errno = EFAULT;
        return -1;
    }
    if (pvolume->fat_positions){
        free(pvolume->fat_positions);
    }
    if (pvolume->psuper){
        free(pvolume->psuper);
    }
    free(pvolume);
    return 0;
}

void fill_entry_structure(struct dir_entry_t *entry){
    (entry->attrib & FAT_ATTRIB_DIRECTORY) != 0 ? (entry->is_directory = 1) : (entry->is_directory = 0);
    (entry->attrib & FAT_ATTRIB_HIDDEN) != 0 ? (entry->is_hidden = 1) : (entry->is_hidden = 0);
    (entry->attrib & FAT_ATTRIB_READ_ONLY) != 0 ? (entry->is_readonly = 1) : (entry->is_readonly = 0);
    (entry->attrib & FAT_ATTRIB_SYSTEM) != 0 ? (entry->is_system = 1) : (entry->is_system = 0);
    (entry->attrib & FAT_ATTRIB_ARCHIVE) != 0 ? (entry->is_archived = 1) : (entry->is_archived = 0);
    (entry->attrib & FAT_ATTRIB_VOLUME_LABEL) != 0 ? (entry->is_volume_label = 1) : (entry->is_volume_label = 0);
    size_t length = 0;
    for ( ; length < SIZE_OF_FILENAME; length++){
        if (entry->filename[length] == ' '){
            break;
        }
    }
    strncpy(entry->name, entry->filename, length);
    entry->name[length] = '\0';
    if (entry->ext[0] != ' ') {
        entry->name[length] = '.';
        entry->name[length + 1] = '\0';
        length = 0;
        for (; length < SIZE_OF_EXTENSION; length++) {
            if (entry->ext[length] == ' ') {
                break;
            }
        }
        strncat(entry->name, entry->ext, length);
    }
}

struct file_t* find_file_entry(struct volume_t* volume, const char* filename){
    uint8_t* dir_structure = malloc(volume->sectors_per_dir * volume->psuper->bytes_per_sector);
    if (!dir_structure){
        errno = ENOMEM;
        return NULL;
    }

    if (disk_read(volume->disk, volume->dir_position, dir_structure, volume->sectors_per_dir) == -1){
        free(dir_structure);
        return NULL;
    }

    struct dir_entry_t* entry = malloc(sizeof(struct dir_entry_t));
    if (!entry){
        errno = ENOMEM;
        free(dir_structure);
        return NULL;
    }
    for (int i=0; i<volume->psuper->root_dir_capacity; i++){
        memcpy(entry, dir_structure + i * SIZE_OF_DIRECTORY_ENTRY, SIZE_OF_DIRECTORY_ENTRY);
        fill_entry_structure(entry);
        if (!strcmp(filename, entry->name)){
            if (entry->is_directory || entry->is_volume_label){
                errno = EISDIR;
                free(dir_structure);
                free(entry);
                return NULL;
            }

            struct file_t* file = malloc(sizeof(struct file_t));
            if (!file){
                errno = ENOMEM;
                free(dir_structure);
                free(entry);
                return NULL;
            }

            file->first_cluster_index = entry->low_cluster_index;
            strncpy(file->filename, entry->name, 12);
            file->current_position = 0;
            file->current_cluster = 0;
            file->current_position_in_cluster = 0;
            file->volume = volume;
            file->file_size = entry->size;
            free(dir_structure);
            free(entry);
            return file;
        }
    }
    errno = ENOENT;
    free(dir_structure);
    free(entry);
    return NULL;
}

void get_chain_fat16(struct file_t* file, const void* const buffer,
                     uint16_t first_cluster){
    if (!buffer || first_cluster == 0 || first_cluster == 1){
        return;
    }

    uint16_t terminator = ((uint16_t*)buffer)[1];
    file->clusters_number = 0;
    file->clusters = NULL;
    uint16_t* fat_table = (uint16_t*)buffer;
    file->first_cluster_index = first_cluster; //todo git?
    uint16_t current_cluster = first_cluster;
    if (current_cluster >= LAST_CLUSTER || current_cluster == 0){
        return;
    }
    while (fat_table[current_cluster] != terminator){
        uint16_t* temp = realloc(file->clusters, (file->clusters_number + 1) * sizeof(uint16_t));
        if (!temp){
            free(file->clusters);
            errno = ENOMEM;
            return;
        }
        file->clusters = temp;
        file->clusters[file->clusters_number] = current_cluster;

        current_cluster = fat_table[current_cluster];
        if (current_cluster >= LAST_CLUSTER || current_cluster == 0){
            file->clusters_number++;
            break;
        }
        file->clusters_number++;
    }
    file->clusters_size_in_bytes = file->clusters_number *
                                   (file->volume->psuper->sectors_per_cluster * file->volume->psuper->bytes_per_sector);
}

uint16_t* get_fat_table(struct volume_t* volume){
    uint16_t* fat_table = malloc(volume->psuper->sectors_per_fat *
                                 volume->psuper->bytes_per_sector);
    if (!fat_table){
        errno = ENOMEM;
        return NULL;
    }
    if (disk_read(volume->disk, volume->fat_positions[0], fat_table,
                  volume->psuper->sectors_per_fat) == -1){
        return NULL;
    }

    return fat_table;
}

struct file_t* file_open(struct volume_t* pvolume, const char* file_name){
    if (!pvolume || !file_name){
        errno = EFAULT;
        return NULL;
    }

    struct file_t* file = find_file_entry(pvolume, file_name);
    if (!file){
        return NULL;
    }

    uint16_t *fat_table = get_fat_table(pvolume);
    if (!fat_table){
        return NULL;
    }
    get_chain_fat16(file, fat_table, file->first_cluster_index);

    free(fat_table);
    return file;
}

int file_close(struct file_t* stream){
    if (!stream){
        errno = EFAULT;
        return -1;
    }
    if (stream->clusters){
        free(stream->clusters);
    }
    free(stream);
    return 0;
}

size_t file_read(void *ptr, size_t size, size_t nmemb, struct file_t *stream){
    if (!ptr || !stream){
        errno = EFAULT;
        return -1;
    }

    char* cluster_data = malloc(stream->volume->bytes_per_cluster);
    if (!cluster_data){
        errno = ENOMEM;
        return -1;
    }

    int readed_elements = 0;
    int byte_offset = 0;
    uint32_t bytes_to_read = size * nmemb > stream->file_size ? stream->file_size : size * nmemb;
    uint16_t cluster_to_read = ceil((double)bytes_to_read / (double)stream->volume->bytes_per_cluster);
    if (stream->current_position >= (int32_t )stream->file_size){
        free(cluster_data);
        return 0;
    }
    for (int i=0; i < cluster_to_read; i++) {
        cluster_t cluster_real_index = stream->clusters[stream->current_cluster] - 2;
        lba_t file_current_position = stream->volume->data_cluster_2 +
                                      cluster_real_index * stream->volume->psuper->sectors_per_cluster;
        if (disk_read(stream->volume->disk, file_current_position, cluster_data,
                      stream->volume->psuper->sectors_per_cluster) == -1){
            return -1;
        }
        uint16_t remaining_elements_in_cluster = i == cluster_to_read - 1 ?
                                                 (bytes_to_read % (uint32_t)stream->volume->bytes_per_cluster)/size :
                                                 (stream->volume->bytes_per_cluster - stream->current_position_in_cluster)/size;
        if (remaining_elements_in_cluster == 0){
            remaining_elements_in_cluster = stream->volume->bytes_per_cluster/size;
        }
        uint16_t remaining_bytes_in_file = stream->file_size - stream->current_position;
        if (size > remaining_bytes_in_file){ //sytuacja, gdy zostaje mniej miejsca w pliku niz size do odczytania
            memcpy((uint8_t*)ptr + readed_elements * size, cluster_data + stream->current_position_in_cluster, remaining_bytes_in_file);
            remaining_elements_in_cluster = 0;
        }
        uint16_t remaining_bytes_in_current_cluster = stream->volume->bytes_per_cluster - stream->current_position_in_cluster;
        if (size > remaining_bytes_in_current_cluster){
            memcpy((uint8_t*)ptr + readed_elements * size, cluster_data + stream->current_position_in_cluster, remaining_bytes_in_current_cluster);
            byte_offset = remaining_bytes_in_current_cluster;
            file_seek(stream, remaining_bytes_in_current_cluster, SEEK_CUR);
            cluster_real_index = stream->clusters[stream->current_cluster] - 2;
            file_current_position = stream->volume->data_cluster_2 +
                                    cluster_real_index * stream->volume->psuper->sectors_per_cluster;
            if (disk_read(stream->volume->disk, file_current_position, cluster_data,
                          stream->volume->psuper->sectors_per_cluster) == -1){
                return -1;
            }
        }
        for (size_t j=0; j < remaining_elements_in_cluster; j++) {
            memcpy((uint8_t*)ptr + readed_elements * size +byte_offset, cluster_data + stream->current_position_in_cluster, size-byte_offset);
            file_seek(stream, size-byte_offset, SEEK_CUR);
            readed_elements++;
        }
    }

    free(cluster_data);
    return readed_elements;
}

int32_t file_seek(struct file_t* stream, int32_t offset, int whence){
    if (!stream){
        errno = EFAULT;
        return -1;
    }
    if (whence == SEEK_SET){
        stream->current_position = 0;
        stream->current_cluster = 0;
        stream->current_position_in_cluster = 0;
    }
    else if (whence == SEEK_END){
        stream->current_position = stream->file_size;
        stream->current_cluster = stream->clusters_number;
        stream->current_position_in_cluster = stream->current_position % stream->volume->bytes_per_cluster;
    }
    else if (whence != SEEK_CUR){
        errno = EINVAL;
        return -1;
    }

    if (stream->current_position + offset < 0 || stream->current_position + offset >= (int32_t)stream->clusters_size_in_bytes){
        errno = ENXIO;
        return -1;
    }
    stream->current_position += offset;
    stream->current_cluster = stream->current_position / stream->volume->bytes_per_cluster;
    stream->current_position_in_cluster = stream->current_position % stream->volume->bytes_per_cluster;

    return 0;
}

struct dir_t* dir_open(struct volume_t* pvolume, const char* dir_path){
    if (!pvolume){
        errno = EFAULT;
        return NULL;
    }
    if (!dir_path){
        errno = ENOENT;
        return NULL;
    }

    if (strcmp("\\", dir_path) != 0){
        errno = ENOENT;
        return NULL;
    }
    struct dir_t* dir = malloc(sizeof(struct dir_t));
    if (!dir){
        errno = ENOMEM;
    }
    strcpy(dir->name, "\\");
    dir->volume = pvolume;
    dir->founded_elements = 0;

    return dir;
}

int dir_read(struct dir_t* pdir, struct dir_entry_t* pentry){
    if (!pdir || !pentry){
        errno = EFAULT;
        return -1;
    }
    uint8_t* root = malloc(pdir->volume->sectors_per_dir * pdir->volume->psuper->bytes_per_sector);
    if (!root){
        errno = ENOMEM;
        return -1;
    }
    if (disk_read(pdir->volume->disk, pdir->volume->dir_position, root, pdir->volume->sectors_per_dir) == -1){
        free(root);
        return -1;
    }
    unsigned int element_number = 0;
    for (int i=0; i<pdir->volume->psuper->root_dir_capacity; i++){
        memcpy(pentry, root + i*SIZE_OF_DIRECTORY_ENTRY, SIZE_OF_DIRECTORY_ENTRY);
        if (pentry->filename[0] == FAT_DELETED || pentry->filename[0] == (char)0x00){
            continue;
        }
        fill_entry_structure(pentry);
        if (pentry->is_volume_label){
            continue;
        }
        element_number++;
        if (element_number <= pdir->founded_elements){
            continue;
        }
        pdir->founded_elements++;
        free(root);
        return 0;
    }

    free(root);
    return 1;
}

int dir_close(struct dir_t* pdir){
    if (!pdir){
        errno = EFAULT;
        return -1;
    }
    free(pdir);
    return 0;
}
