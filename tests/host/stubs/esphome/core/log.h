#pragma once
// Host stub: every log line goes to the test harness, which records it.
void host_log(char level, const char *tag, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
#define ESP_LOGE(tag, ...) host_log('E', tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...) host_log('W', tag, __VA_ARGS__)
#define ESP_LOGI(tag, ...) host_log('I', tag, __VA_ARGS__)
#define ESP_LOGD(tag, ...) host_log('D', tag, __VA_ARGS__)
#define ESP_LOGV(tag, ...) host_log('V', tag, __VA_ARGS__)
#define ESP_LOGVV(tag, ...) host_log('v', tag, __VA_ARGS__)
#define ESP_LOGCONFIG(tag, ...) host_log('C', tag, __VA_ARGS__)
