// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Utils.h"
#include "PixelStreamingSettings.h"
#include "Tickable.h"
#include "PlayerId.h"
#include "CanvasItem.h"
#include "IPixelStreamingSessions.h"
#include "Containers/UnrealString.h"
#include "IPixelStreamingStatsConsumer.h"
#include "UnrealEngine.h"

// ----------------- FStatData -----------------

struct FStatData
{
public:
	FStatData(FName InStatName, double InStatValue, int InNDecimalPlacesToPrint, bool bInSmooth = false)
		: StatName(InStatName)
		, StatValue(InStatValue)
		, NDecimalPlacesToPrint(InNDecimalPlacesToPrint)
		, bSmooth(bInSmooth) {}

	bool operator==(const FStatData& Other) const
	{
		return Equals(Other);
	}

	bool Equals(const FStatData& Other) const
	{
		return StatName == Other.StatName;
	}

	FName StatName;
	double StatValue;
	int NDecimalPlacesToPrint;
	bool bSmooth;
};

FORCEINLINE uint32 GetTypeHash(const FStatData& Obj)
{
	// From UnrealString.h
	return GetTypeHash(Obj.StatName);
}

// ----------------- FRenderableStat -----------------

struct FRenderableStat
{
	FStatData Stat;
	FCanvasTextItem CanvasItem;
};

// ----------------- FPixelStreamingPeerStats -----------------

// Pixel Streaming stats that are associated with a specific peer.
class FPixelStreamingPeerStats
{

public:
	FPixelStreamingPeerStats(FPlayerId InAssociatedPlayer)
		: AssociatedPlayer(InAssociatedPlayer)
		, PlayerIdCanvasItem(FVector2D(0, 0), FText::FromString(FString::Printf(TEXT("[Peer Stats(%s)]"), *AssociatedPlayer)), FSlateFontInfo(FSlateFontInfo(UEngine::GetSmallFont(), 12)), FLinearColor(0, 1, 0))
	{
		PlayerIdCanvasItem.EnableShadow(FLinearColor::Black);
	};

	void StoreStat(FStatData StatToStore);
	bool StoreStat_GameThread(FStatData StatToStore);

private:
	int DisplayId = 0;
	FPlayerId AssociatedPlayer;

public:
	TMap<FName, FRenderableStat> StoredStats;
	TMap<FName, TArray<TWeakPtr<IPixelStreamingStatsConsumer>>> SingleStatConsumers;
	FCanvasTextItem PlayerIdCanvasItem;
};

// ----------------- FPixelStreamingStats -----------------

// Stats about Pixel Streaming that can displayed either in the in-application HUD, in the log, or simply reported to some subscriber.
class FPixelStreamingStats : FTickableGameObject
{
public:
	static constexpr double SmoothingPeriod = 3.0 * 60.0;
	static constexpr double SmoothingFactor = 10.0 / 100.0;
	static FPixelStreamingStats* Get();

	FPixelStreamingStats(IPixelStreamingSessions* Sessions);
	void QueryPeerStat(FPlayerId PlayerId, FName StatToQuery, TFunction<void(bool, double)> QueryCallback) const;
	bool QueryPeerStat_GameThread(FPlayerId PlayerId, FName StatToQuery, double& OutStatValue) const;
	void RemovePeersStats(FPlayerId PlayerId);
	void StorePeerStat(FPlayerId PlayerId, FStatData Stat);
	void StoreApplicationStat(FStatData PeerStat);
	void Tick(float DeltaTime);
	void AddOnPeerStatChangedCallback(FPlayerId PlayerId, FName StatToListenOn, TWeakPtr<IPixelStreamingStatsConsumer> Callback);
	void AddOnAnyStatChangedCallback(TWeakPtr<IPixelStreamingStatsConsumer> Callback);
	void RemoveOnAnyStatChangedCallback(TWeakPtr<IPixelStreamingStatsConsumer> Callback);
	int32 OnRenderStats(UWorld* World, FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const FVector* ViewLocation, const FRotator* ViewRotation);
	bool OnToggleStats(UWorld* World, FCommonViewportClient* ViewportClient, const TCHAR* Stream);

	FORCEINLINE TStatId GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(PixelStreamingStats, STATGROUP_Tickables); }

private:
	void PollPixelStreamingSettings();
	void RemovePeerStat_GameThread(FPlayerId PlayerId);
	bool StorePeerStat_GameThread(FPlayerId PlayerId, FStatData Stat);
	bool StoreApplicationStat_GameThread(FStatData Stat);
	void AddOnPeerStatChangedCallback_GameThread(FPlayerId PlayerId, FName StatToListenOn, TWeakPtr<IPixelStreamingStatsConsumer> Callback);
	void AddOnAnyStatChangedCallback_GameThread(TWeakPtr<IPixelStreamingStatsConsumer> Callback);
	void RemoveOnAnyStatChangedCallback_GameThread(TWeakPtr<IPixelStreamingStatsConsumer> Callback);
	void FireStatChanged_GameThread(FPlayerId PlayerId, FName StatName, float StatValue);

private:
	static FPixelStreamingStats* Instance;

	IPixelStreamingSessions* Sessions;

	bool bRegisterEngineStats = false;
	FName PixelStreamingStatName = FName(TEXT("STAT_PixelStreaming"));
	FName PixelStreamingStatCategory = FName(TEXT("STATCAT_PixelStreaming"));
	FText PixelStreamingStatDescription = FText::FromString(FString(TEXT("Pixel Streaming stats for all connected peers.")));

	TMap<FPlayerId, FPixelStreamingPeerStats> PeerStats;
	TMap<FName, FRenderableStat> ApplicationStats;
	TArray<TWeakPtr<IPixelStreamingStatsConsumer>> AllStatsConsumers;

	int64 LastTimeSettingsPolledCycles = 0;
};
