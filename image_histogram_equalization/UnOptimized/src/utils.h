
#include <string>
    // Constant array of input sizes in bytes
    const int inputSizeOptions[] = {
        64,       // 64 Bytes
        128,      // 128 Bytes
        256,      // 256 Bytes
        512,      // 512 Bytes
        1024,     // 1 KB (1024 Bytes)
        2048,     // 2 KB
        4096,     // 4 KB
        8192,     // 8 KB
        16384,    // 16 KB
        32768,    // 32 KB
        65536,    // 64 KB
        131072,   // 128 KB
        262144,   // 256 KB
        524288,   // 512 KB
        1048576,  // 1 MB
        2097152,  // 2 MB
        4194304,  // 4 MB
        8388608   // 8 MB
    };

    // Corresponding array of strings for input sizes
    const std::string inputSizeStrings[] = {
        "64 B",       // 64 Bytes
        "128 B",      // 128 Bytes
        "256 B",      // 256 Bytes
        "512 B",      // 512 Bytes
        "1 KB",       // 1 KB (1024 Bytes)
        "2 KB",       // 2 KB
        "4 KB",       // 4 KB
        "8 KB",       // 8 KB
        "16 KB",      // 16 KB
        "32 KB",      // 32 KB
        "64 KB",      // 64 KB
        "128 KB",     // 128 KB
        "256 KB",     // 256 KB
        "512 KB",     // 512 KB
        "1 MB",       // 1 MB
        "2 MB",       // 2 MB
        "4 MB",       // 4 MB
        "8 MB"        // 8 MB
    };

int is_file_empty(FILE *file) {
    fseek(file, 0, SEEK_END); // Move file pointer to the end
    if (ftell(file) == 0) {   // Check position of file pointer
        return 1;             // File is empty
    }
    return 0;                 // File is not empty
}
