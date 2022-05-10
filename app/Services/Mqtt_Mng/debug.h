#ifndef USER_DEBUG_H_
#define USER_DEBUG_H_

/* text format */
#define NORMAL  "\e[21m"
#define BOLD    "\e[1m"

/* colors  */
#define BLANK   "\e[0m"
#define RED     "\e[31m"
#define GREEN   "\e[32m"
#define YELLOW  "\e[33m"
#define MAGENTA "\e[35m"
#define CYAN    "\e[36m"

/* format + colors */
#define NORMAL_BLANK    "\e[21;0m"
#define BOLD_RED        "\e[1;31m"
#define BOLD_GREEN      "\e[1;32m"
#define BOLD_YELLOW     "\e[1;33m"
#define BOLD_MAGENTA    "\e[1;35m"
#define BOLD_CYAN       "\e[1;36m"

#if defined(GLOBAL_DEBUG_ON)
    #define MQTT_DEBUG_ON
#endif

/* use the do-while(0) trick to split #define on multiple lines */

#if defined(MQTT_DEBUG_ON)

    #define DEBUG_INFO(format, ...) do {\
        os_printf(BOLD_GREEN "[DEBUG]\t| ");\
        os_printf(format, ##__VA_ARGS__);\
        os_printf(NORMAL_BLANK);\
        os_printf("\n");\
    }while(0)

    #define FOTA_INFO(format, ...) do {\
        os_printf(BOLD_CYAN "[FOTA]\t| ");\
        os_printf(format, ##__VA_ARGS__);\
        os_printf(NORMAL_BLANK);\
        os_printf("\n");\
    }while(0)

    #define ERROR_INFO(format, ...) do {\
        os_printf(BOLD_RED "[ERROR]\t| ");\
        os_printf(format, ##__VA_ARGS__);\
        os_printf(NORMAL_BLANK);\
        os_printf("\n");\
    } while(0)

    #define MQTT_INFO(format, ...) do {\
        os_printf(BOLD_MAGENTA "[MQTT]\t| ");\
        os_printf(format, ##__VA_ARGS__);\
        os_printf(NORMAL_BLANK);\
        os_printf("\n");\
    } while(0)

    #define BUTTON_INFO(format, ...) do {\
        os_printf(BOLD_YELLOW "[BUTTON]\t| ");\
        os_printf(format, ##__VA_ARGS__);\
        os_printf(NORMAL_BLANK);\
        os_printf("\n");\
    } while(0)

#else

    int no_printf(const char *format, ...) __attribute__((format (printf, 1, 2)));

    #define DEBUG_INFO(format, ...) do { \
        static const char flash_str[] ICACHE_RODATA_ATTR STORE_ATTR = format;  \
        no_printf(flash_str, ##__VA_ARGS__);    \
    } while(0)

    #define FOTA_INFO(format, ...) do { \
        static const char flash_str[] ICACHE_RODATA_ATTR STORE_ATTR = format;  \
        no_printf(flash_str, ##__VA_ARGS__);    \
    } while(0)

    #define ERROR_INFO(format, ...) do { \
        static const char flash_str[] ICACHE_RODATA_ATTR STORE_ATTR = format;  \
        no_printf(flash_str, ##__VA_ARGS__);    \
    } while(0)

    #define MQTT_INFO(format, ...) do { \
        static const char flash_str[] ICACHE_RODATA_ATTR STORE_ATTR = format;  \
        no_printf(flash_str, ##__VA_ARGS__);    \
    } while(0)

    #define BUTTON_INFO(format, ...) do { \
        static const char flash_str[] ICACHE_RODATA_ATTR STORE_ATTR = format;  \
        no_printf(flash_str, ##__VA_ARGS__); \
    } while(0)
#endif

#endif /* USER_DEBUG_H_ */
