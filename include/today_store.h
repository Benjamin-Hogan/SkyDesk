// today_store.h - Today's Sky on the device (docs/11-today.md -> Architecture).
// Owns the spotter's mutex, restores it at boot (NVS blob + SD hash set), and drains its
// outbox to the SD card from the net task between jobs. The UI reads snapshots.
#pragma once

#include "spotter.h"

// setup(), after sdInit() and before netTaskStart(): restore today's summary.
void todayInit();

// Core 1, every UI tick right after trackerUpdate().
void todayUpdate(const Traffic &t, bool newPoll, bool trafficUp, uint32_t nowMs);

// Net task, between jobs (never during a radar convert): CSV lines, type lookups,
// the hash set (every 5 min when changed) and the NVS summary (at most once a minute).
void todayService();

// UI snapshots (core 1; copied under the mutex into the CALLER's buffer - a stack
// copy costs no heap: the task stacks are allocated anyway).
uint32_t todayVersion();
void todaySnapshot(TodaySummary &out, uint16_t &nearby);
uint16_t todayOverhead();
bool todayLastPass(PassRec &out);
bool todayClockValid();
