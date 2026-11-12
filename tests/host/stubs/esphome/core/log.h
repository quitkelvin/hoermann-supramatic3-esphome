#pragma once
#include <cstdio>
#define ESPHOME_LOG_LEVEL_VERY_VERBOSE 7
#define ESPHOME_LOGLEVEL 3
extern bool g_log_enabled;
#define UAPB_LOG_(lvl, tag, ...) do { if (g_log_enabled) { printf("  [%s] %s: ", lvl, tag); printf(__VA_ARGS__); printf("\n"); } } while (0)
#define ESP_LOGE(tag, ...) UAPB_LOG_("E", tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...) UAPB_LOG_("W", tag, __VA_ARGS__)
#define ESP_LOGI(tag, ...) UAPB_LOG_("I", tag, __VA_ARGS__)
#define ESP_LOGD(tag, ...) do {} while (0)
#define ESP_LOGV(tag, ...) do {} while (0)
#define ESP_LOGVV(tag, ...) do {} while (0)
#define ESP_LOGCONFIG(tag, ...) do {} while (0)
