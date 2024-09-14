#include "up3d.h"
#include <linux/ktime.h>

u64 get_current_timestamp() {
    ktime_t now = ktime_get_real(); // 获取当前时间
    u64 timestamp = ktime_to_ns(now); // 将ktime_t转换为纳秒
    return timestamp;
}