// Host BadgeOTA: "no update available". It is the only user of Update.h and the
// partition APIs, so it is replaced whole. Same header as ota/BadgeOTA.cpp.

#include "ota/BadgeOTA.h"

namespace ota {
namespace {
const char* kNoNetwork = "no network on the host";
}

void prepareFirmwareInstallHeap() {}
void begin() {}
void tick() {}

CheckResult checkNow(bool) { return CheckResult::kNetworkError; }
bool beginCheckAsync(bool) { return false; }
bool isCheckingAsync() { return false; }
bool updateAvailable() { return false; }

const char* latestKnownTag() { return ""; }
const char* latestKnownAssetUrl() { return ""; }
size_t latestKnownAssetSize() { return 0; }
time_t lastCheckEpoch() { return 0; }
const char* lastErrorMessage() { return kNoNetwork; }

InstallResult installCached(InstallProgressCb, void*) { return InstallResult::kNoAssetCached; }
void markCurrentAppValidIfPending() {}
bool runningPendingVerify() { return false; }

size_t ffatPartitionBytes() { return 0x600000; }
size_t ffatVolumeBytes() { return 0x600000; }
bool ffatExpansionAvailable() { return false; }
bool ffatUsesExpandedPartitionLayout() { return false; }
bool canOfferLayoutMigration() { return false; }
bool layoutJustChanged() { return false; }
void acknowledgeLayoutChange() {}
bool justRebootedFromLayoutMigration() { return false; }
void acknowledgeMigrationBoot() {}
void reformatFfatAndReboot() {}
MigrationResult migrateToExpandedLayout() { return MigrationResult::kDisabled; }
bool migrationAssetPresent() { return false; }

}  // namespace ota
