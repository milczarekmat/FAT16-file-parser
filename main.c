#include "file_reader.h"

int main() {
    file_open(NULL, NULL);
    file_close(NULL);
    file_read(NULL, 1, 1, NULL);
    dir_open(NULL, NULL);
    dir_read(NULL, NULL);
    dir_close(NULL);
    fat_open(NULL, 0);
    disk_open_from_file(NULL);
    return 0;
}
