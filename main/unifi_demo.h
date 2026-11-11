#pragma once

#include "unifi_model.h"

/* Stable clock for an explicitly selected demo before network time is known. */
#define UNIFI_DEMO_EPOCH INT64_C(1794067200)

/* Shared synthetic data for firmware and the native LVGL preview. No I/O. */
void unifi_demo_snapshot(unifi_snapshot_t *snapshot, int64_t now,
                         uint32_t client_offset, bool vpn_only);
void unifi_demo_alerts(unifi_alerts_t *alerts, int64_t now);
