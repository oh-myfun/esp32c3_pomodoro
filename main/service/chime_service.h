#pragma once

/* 整点/半点报时检测服务。
 * 由 UIUpdate 任务每秒调用一次。
 * 内部用绝对分钟 ID 去重，确保每个目标分钟只响一次。 */

void chime_service_init(void);
void chime_service_tick(void);
