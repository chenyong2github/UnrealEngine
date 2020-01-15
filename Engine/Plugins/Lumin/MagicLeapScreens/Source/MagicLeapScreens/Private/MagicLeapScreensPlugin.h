// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/Texture2D.h"
#include "Containers/Array.h"
#include "Misc/Optional.h"
#include "IMagicLeapPlugin.h"
#include "IMagicLeapScreensPlugin.h"
#include "Lumin/CAPIShims/LuminAPIImage.h"
#include "Lumin/CAPIShims/LuminAPIScreens.h"
#include "MagicLeapPluginUtil.h"
#include "MagicLeapScreensTypes.h"

class FScreensRunnable;
struct FScreensTask;

class FMagicLeapScreensPlugin : public IMagicLeapScreensPlugin
{
public:
	void StartupModule() override;
	void ShutdownModule() override;
	bool IsSupportedFormat(EPixelFormat InPixelFormat);

#if WITH_MLSDK
	UTexture2D* MLImageToUTexture2D(const MLImage& Source);
	void MLWatchHistoryEntryToUnreal(const MLScreensWatchHistoryEntry& InEntry, FMagicLeapScreensWatchHistoryEntry& OutEntry);
	bool MLScreenInfoToUnreal(const MLScreensScreenInfoEx& InInfo, FMagicLeapScreenTransform& OutTransform, const FTransform& TrackingToWorld, const float WorldToMetersScale);
	bool UTexture2DToMLImage(const UTexture2D& Source, MLImage& Target);
#endif // WITH_MLSDK

	bool RemoveWatchHistoryEntry(const FGuid& ID);
	bool ClearWatchHistory();
	bool Tick(float DeltaTime);

	void GetWatchHistoryEntriesAsync(const TOptional<FMagicLeapScreensHistoryRequestResultDelegate>& OptionalResultDelegate);
	void AddToWatchHistoryAsync(const FMagicLeapScreensWatchHistoryEntry& NewEntry, const TOptional<FMagicLeapScreensEntryRequestResultDelegate>& OptionalResultDelegate);
	void UpdateWatchHistoryEntryAsync(const FMagicLeapScreensWatchHistoryEntry& UpdateEntry, const TOptional<FMagicLeapScreensEntryRequestResultDelegate>& OptionalResultDelegate);
	void UpdateScreenTransformAsync(const FMagicLeapScreenTransform& ScreenTransform, const FMagicLeapScreenTransformRequestResultDelegate& ResultDelegate);

	bool GetScreenTransform(FMagicLeapScreenTransform& ScreensTransforms);
	bool GetScreensTransforms(TArray<FMagicLeapScreenTransform>& ScreensTransforms);

	FScreensTask AddToWatchHistory(const FMagicLeapScreensWatchHistoryEntry& WatchHistoryEntry);
	FScreensTask GetWatchHistoryEntries();
	FScreensTask UpdateWatchHistoryEntry(const FMagicLeapScreensWatchHistoryEntry& WatchHistoryEntry);
	FScreensTask UpdateScreensTransform(const FMagicLeapScreenTransform& ScreenTransform);
	class FScreensRunnable* GetRunnable() const;

private:
	class FScreensRunnable* Runnable;
	TArray<uint8> PixelDataMemPool;
#if WITH_MLSDK
	bool ShouldUseDefaultThumbnail(const FMagicLeapScreensWatchHistoryEntry& Entry, MLImage& MLImage);
	MLImage DefaultThumbnail;
	FCriticalSection CriticalSection;
#endif // WITH_MLSDK
	FMagicLeapAPISetup APISetup;

	FTickerDelegate TickDelegate;
	FDelegateHandle TickDelegateHandle;
};

inline FMagicLeapScreensPlugin& GetMagicLeapScreensPlugin()
{
	return FModuleManager::Get().GetModuleChecked<FMagicLeapScreensPlugin>("MagicLeapScreens");
}
