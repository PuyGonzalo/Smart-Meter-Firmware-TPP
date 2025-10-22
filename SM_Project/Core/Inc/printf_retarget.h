#define ENABLE_DEBUG

#ifdef ENABLE_DEBUG
#define DEBUG_PRINTF(format, ...) printf(format "\r\n", ##__VA_ARGS__)
#endif