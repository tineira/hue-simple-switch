#pragma once

// Host stub of the ESP-IDF NVS iterator API used by recipes.h. Finds nothing.

#include <stddef.h>

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NVS_NOT_FOUND 0x1102
#define NVS_KEY_NAME_MAX_SIZE 16
#define NVS_DEFAULT_PART_NAME "nvs"

typedef enum { NVS_TYPE_BLOB = 0x42 } nvs_type_t;
typedef struct nvs_opaque_iterator_t *nvs_iterator_t;
typedef struct {
  char namespace_name[16];
  char key[NVS_KEY_NAME_MAX_SIZE];
  nvs_type_t type;
} nvs_entry_info_t;

inline esp_err_t nvs_entry_find(const char *, const char *, nvs_type_t, nvs_iterator_t *it) {
  *it = nullptr;
  return ESP_ERR_NVS_NOT_FOUND;
}
inline esp_err_t nvs_entry_next(nvs_iterator_t *it) {
  *it = nullptr;
  return ESP_ERR_NVS_NOT_FOUND;
}
inline esp_err_t nvs_entry_info(nvs_iterator_t, nvs_entry_info_t *) { return ESP_ERR_NVS_NOT_FOUND; }
inline void nvs_release_iterator(nvs_iterator_t) {}
