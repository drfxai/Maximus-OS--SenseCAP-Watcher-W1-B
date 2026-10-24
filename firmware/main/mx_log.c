#include "mx_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_core_dump.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"

#define RING (48 * 1024)
#define TAIL 3072           /* the last lines before a restart */
#define MAGIC 0x4D584C47u   /* "MXLG" */

/* The live log lives in PSRAM. Only its tail survives a restart, in RTC
   memory: noinit PSRAM exists on the original ESP32 only, and on this chip
   the whole 48 KB went to internal RAM in 2.3.0 and 2.3.1, leaving the
   display no DMA memory (a white, then a black screen). */
static char *s_data;
static uint32_t s_head;     /* counts every byte ever written */

typedef struct {
    uint32_t magic, head;
    char data[TAIL];
} tail_t;
static RTC_NOINIT_ATTR tail_t s_tail;

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static vprintf_like_t s_orig;
static char *s_prev, *s_crash;
static size_t s_prev_len;

static void put(const char *s, size_t n)
{
    portENTER_CRITICAL_SAFE(&s_mux);
    for (size_t i = 0; i < n; i++) {
        s_data[(s_head + i) % RING] = s[i];
        s_tail.data[(s_tail.head + i) % TAIL] = s[i];
    }
    s_head += n;
    s_tail.head += n;
    portEXIT_CRITICAL_SAFE(&s_mux);
}

static int tee(const char *fmt, va_list ap)
{
    char b[160];        /* small: this runs on every task's stack, some only 3 KB */
    va_list cp;
    va_copy(cp, ap);
    int n = vsnprintf(b, sizeof b, fmt, cp);
    va_end(cp);
    if (n > 0) put(b, n < (int)sizeof b ? (size_t)n : sizeof b - 1);
    return s_orig ? s_orig(fmt, ap) : n;
}

static void read_crash(void)
{
    esp_core_dump_summary_t *sum = heap_caps_calloc(1, sizeof *sum, MALLOC_CAP_SPIRAM);
    if (!sum) return;
    if (esp_core_dump_image_check() == ESP_OK && esp_core_dump_get_summary(sum) == ESP_OK) {
        enum { CAP = 768 };
        s_crash = heap_caps_malloc(CAP, MALLOC_CAP_SPIRAM);
        if (s_crash) {
            char reason[160] = "";
            esp_core_dump_get_panic_reason(reason, sizeof reason);
            int n = snprintf(s_crash, CAP, "%s%sCrash in task \"%s\" at PC 0x%08lx (cause %lu, address 0x%08lx), build %.8s.\nBacktrace:",
                             reason, reason[0] ? "\n" : "", sum->exc_task, (unsigned long)sum->exc_pc,
                             (unsigned long)sum->ex_info.exc_cause, (unsigned long)sum->ex_info.exc_vaddr,
                             (const char *)sum->app_elf_sha256);
            for (uint32_t i = 0; i < sum->exc_bt_info.depth && i < 16 && n < CAP - 16; i++)
                n += snprintf(s_crash + n, CAP - n, " 0x%08lx", (unsigned long)sum->exc_bt_info.bt[i]);
            if (sum->exc_bt_info.corrupted && n < CAP - 16) snprintf(s_crash + n, CAP - n, " (corrupted)");
        }
    }
    free(sum);
}

void mx_log_init(void)
{
    esp_reset_reason_t why = esp_reset_reason();
    if (s_tail.magic == MAGIC && why != ESP_RST_POWERON && why != ESP_RST_BROWNOUT) {
        size_t n = s_tail.head < TAIL ? s_tail.head : TAIL;
        s_prev = heap_caps_malloc(n + 1, MALLOC_CAP_SPIRAM);
        if (s_prev) {
            for (size_t i = 0; i < n; i++) s_prev[i] = s_tail.data[(s_tail.head - n + i) % TAIL];
            s_prev[n] = 0;
            s_prev_len = n;
        }
    }
    s_tail.magic = MAGIC;
    s_tail.head = 0;
    if (why == ESP_RST_PANIC || why == ESP_RST_INT_WDT || why == ESP_RST_TASK_WDT || why == ESP_RST_WDT) read_crash();
    s_data = heap_caps_malloc(RING, MALLOC_CAP_SPIRAM);
    if (s_data) s_orig = esp_log_set_vprintf(tee);   /* else no live log; the serial port still has it all */
}

uint32_t mx_log_head(void) { return s_head; }

size_t mx_log_read(uint32_t *pos, char *dst, size_t cap)
{
    if (!s_data) return 0;
    portENTER_CRITICAL_SAFE(&s_mux);
    uint32_t head = s_head;
    uint32_t p = *pos;
    if (p > head || head - p > RING) p = head > RING ? head - RING : 0;
    size_t n = head - p;
    if (n > cap) n = cap;
    portEXIT_CRITICAL_SAFE(&s_mux);
    /* the copy runs outside the lock; a writer lapping us only garbles text, never memory */
    for (size_t i = 0; i < n; i++) dst[i] = s_data[(p + i) % RING];
    *pos = p + n;
    return n;
}

const char *mx_log_prev(size_t *len)
{
    if (len) *len = s_prev_len;
    return s_prev;
}

const char *mx_log_crash(void) { return s_crash; }
