
#include "HAL/IPlatformFileManagedStorageWrapper.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY(LogPlatformFileManagedStorage);

static FAutoConsoleCommand PersistentStorageCategoryStatsCommand
(
	TEXT("PersistentStorageCategoryStats"),
	TEXT("Get the stat of each persistent storage stats\n"),
	FConsoleCommandDelegate::CreateStatic([]()
{
	for (auto& CategoryStat : FPersistentStorageManager::Get().GenerateCategoryStats())
	{
		UE_LOG(LogPlatformFileManagedStorage, Display, TEXT("%s"), *CategoryStat.Print());
	}
})
);