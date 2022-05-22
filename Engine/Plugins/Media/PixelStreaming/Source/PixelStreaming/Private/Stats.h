// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Tickable.h"
#include "PixelStreamingPlayerId.h"
#include "CanvasItem.h"
#include "UnrealEngine.h"
#include "ConsoleSettings.h"
#include "DebugGraph.h"

class IPixelStreamingStatsConsumer;
struct FPixelStreamingFrameMetadata;

namespace UE::PixelStreaming
{
	// ----------------- FStatData -----------------
	class FPlayerSessions;

	struct FStatData
	{
	public:
		FStatData(FName InStatName, double InStatValue, int InNDecimalPlacesToPrint, bool bInSmooth = false)
			: StatName(InStatName)
			, StatValue(InStatValue)
			, NDecimalPlacesToPrint(InNDecimalPlacesToPrint)
			, bSmooth(bInSmooth)
			 {
			 }

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
		double LastEMA = 0;
		int NumSamples = 0;
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

	// ----------------- FPeerStats -----------------

	// Pixel Streaming stats that are associated with a specific peer.
	class FPeerStats
	{

	public:
		FPeerStats(FPixelStreamingPlayerId InAssociatedPlayer)
			: AssociatedPlayer(InAssociatedPlayer)
			, PlayerIdCanvasItem(FVector2D(0, 0), FText::FromString(FString::Printf(TEXT("[Peer Stats(%s)]"), *AssociatedPlayer)), FSlateFontInfo(FSlateFontInfo(UEngine::GetSmallFont(), 12)), FLinearColor(0, 1, 0))
		{
			PlayerIdCanvasItem.EnableShadow(FLinearColor::Black);
		};

		void StoreStat(FStatData StatToStore);
		bool StoreStat_GameThread(FStatData StatToStore);

	private:
		int DisplayId = 0;
		FPixelStreamingPlayerId AssociatedPlayer;

	public:
		TMap<FName, FRenderableStat> StoredStats;
		TMap<FName, TArray<TWeakPtr<IPixelStreamingStatsConsumer>>> SingleStatConsumers;
		FCanvasTextItem PlayerIdCanvasItem;
	};

	// ----------------- FStats -----------------

	// Stats about Pixel Streaming that can displayed either in the in-application HUD, in the log, or simply reported to some subscriber.
	class FStats : FTickableGameObject
	{
	public:
		static constexpr double SmoothingPeriod = 3.0 * 60.0;
		static constexpr double SmoothingFactor = 10.0 / 100.0;
		static FStats* Get();

		void AddSessions(FPlayerSessions* InSessions);
		void RemoveSessions(FPlayerSessions* InSessions);

		FStats(const FStats&) = delete;
		void QueryPeerStat(FPixelStreamingPlayerId PlayerId, FName StatToQuery, TFunction<void(bool, double)> QueryCallback) const;
		bool QueryPeerStat_GameThread(FPixelStreamingPlayerId PlayerId, FName StatToQuery, double& OutStatValue) const;
		void RemovePeersStats(FPixelStreamingPlayerId PlayerId);
		void StorePeerStat(FPixelStreamingPlayerId PlayerId, FStatData Stat);
		void StoreApplicationStat(FStatData PeerStat);
		void Tick(float DeltaTime);
		void AddOnPeerStatChangedCallback(FPixelStreamingPlayerId PlayerId, FName StatToListenOn, TWeakPtr<IPixelStreamingStatsConsumer> Callback);

		bool OnToggleStats(UWorld* World, FCommonViewportClient* ViewportClient, const TCHAR* Stream);
		int32 OnRenderStats(UWorld* World, FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const FVector* ViewLocation, const FRotator* ViewRotation);

		bool OnToggleGraphs(UWorld* World, FCommonViewportClient* ViewportClient, const TCHAR* Stream);
		int32 OnRenderGraphs(UWorld* World, FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const FVector* ViewLocation, const FRotator* ViewRotation);

		FORCEINLINE TStatId GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(PixelStreamingStats, STATGROUP_Tickables); }

		void GraphValue(FName InName, float Value, int InSamples, float InMinRange, float InMaxRange, float InRefValue = 0.0f);
		
		double AddTimeDeltaStat(uint64 Cycles1, uint64 Cycles2, const FString& Label);
		void AddFrameTimingStats(const FPixelStreamingFrameMetadata& FrameMetadata);

		void AddCanvasTile(FName Name, const FCanvasTileItem& Tile);

	private:
		FStats();
		void RegisterEngineHooks();
		void PollPixelStreamingSettings();
		void RemovePeerStat_GameThread(FPixelStreamingPlayerId PlayerId);
		bool StorePeerStat_GameThread(FPixelStreamingPlayerId PlayerId, FStatData Stat);
		bool StoreApplicationStat_GameThread(FStatData Stat);
		void AddOnPeerStatChangedCallback_GameThread(FPixelStreamingPlayerId PlayerId, FName StatToListenOn, TWeakPtr<IPixelStreamingStatsConsumer> Callback);
		void FireStatChanged_GameThread(FPixelStreamingPlayerId PlayerId, FName StatName, float StatValue);
		void UpdateConsoleAutoComplete_GameThread(TArray<FAutoCompleteCommand>& AutoCompleteList);

	private:
		static FStats* Instance;

		TArray<FPlayerSessions*> SessionsList;

		bool bRegisterEngineStats = false;

		TMap<FPixelStreamingPlayerId, FPeerStats> PeerStats;
		TMap<FName, FRenderableStat> ApplicationStats;

		int64 LastTimeSettingsPolledCycles = 0;

		TMap<FName, FDebugGraph> Graphs;
		TMap<FName, FCanvasTileItem> Tiles;
	};
} // namespace UE::PixelStreaming
