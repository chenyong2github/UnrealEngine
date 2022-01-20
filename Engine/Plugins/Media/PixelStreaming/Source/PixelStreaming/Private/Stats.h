// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Settings.h"
#include "Tickable.h"
#include "PixelStreamingPlayerId.h"
#include "CanvasItem.h"
#include "IPixelStreamingSessions.h"
#include "Containers/UnrealString.h"
#include "IPixelStreamingStatsConsumer.h"
#include "UnrealEngine.h"

namespace UE {
	namespace PixelStreaming {
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

			FStats(IPixelStreamingSessions* Sessions);
			void QueryPeerStat(FPixelStreamingPlayerId PlayerId, FName StatToQuery, TFunction<void(bool, double)> QueryCallback) const;
			bool QueryPeerStat_GameThread(FPixelStreamingPlayerId PlayerId, FName StatToQuery, double& OutStatValue) const;
			void RemovePeersStats(FPixelStreamingPlayerId PlayerId);
			void StorePeerStat(FPixelStreamingPlayerId PlayerId, FStatData Stat);
			void StoreApplicationStat(FStatData PeerStat);
			void Tick(float DeltaTime);
			void AddOnPeerStatChangedCallback(FPixelStreamingPlayerId PlayerId, FName StatToListenOn, TWeakPtr<IPixelStreamingStatsConsumer> Callback);
			int32 OnRenderStats(UWorld* World, FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const FVector* ViewLocation, const FRotator* ViewRotation);
			bool OnToggleStats(UWorld* World, FCommonViewportClient* ViewportClient, const TCHAR* Stream);

			FORCEINLINE TStatId GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(PixelStreamingStats, STATGROUP_Tickables); }

		private:
			void PollPixelStreamingSettings();
			void RemovePeerStat_GameThread(FPixelStreamingPlayerId PlayerId);
			bool StorePeerStat_GameThread(FPixelStreamingPlayerId PlayerId, FStatData Stat);
			bool StoreApplicationStat_GameThread(FStatData Stat);
			void AddOnPeerStatChangedCallback_GameThread(FPixelStreamingPlayerId PlayerId, FName StatToListenOn, TWeakPtr<IPixelStreamingStatsConsumer> Callback);
			void FireStatChanged_GameThread(FPixelStreamingPlayerId PlayerId, FName StatName, float StatValue);

		private:
			static FStats* Instance;

			IPixelStreamingSessions* Sessions;

			bool bRegisterEngineStats = false;
			FName PixelStreamingStatName = FName(TEXT("STAT_PixelStreaming"));
			FName PixelStreamingStatCategory = FName(TEXT("STATCAT_PixelStreaming"));
			FText PixelStreamingStatDescription = FText::FromString(FString(TEXT("Pixel Streaming stats for all connected peers.")));

			TMap<FPixelStreamingPlayerId, FPeerStats> PeerStats;
			TMap<FName, FRenderableStat> ApplicationStats;

			int64 LastTimeSettingsPolledCycles = 0;
		};
	}
}
