// Copyright Epic Games, Inc. All Rights Reserved.

#include "Stats.h"
#include "PixelStreamingPrivate.h"
#include "Engine/Engine.h"
#include "CoreGlobals.h"
#include "Async/Async.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Stats/Stats.h"
#include "PixelStreamingDelegates.h"

UE::PixelStreaming::FStats* UE::PixelStreaming::FStats::Instance = nullptr;

UE::PixelStreaming::FStats* UE::PixelStreaming::FStats::Get()
{
	return UE::PixelStreaming::FStats::Instance;
}

UE::PixelStreaming::FStats::FStats(UE::PixelStreaming::IPixelStreamingSessions* InSessions)
	: Sessions(InSessions)
{
	checkf(UE::PixelStreaming::FStats::Instance == nullptr, TEXT("There should only ever been one PixelStreaming stats object."));
	checkf(Sessions, TEXT("To make stats object the sessions object must not be nullptr."));
	UE::PixelStreaming::FStats::Instance = this;
}

void UE::PixelStreaming::FStats::QueryPeerStat(FPixelStreamingPlayerId PlayerId, FName StatToQuery, TFunction<void(bool, double)> QueryCallback) const
{
	if (IsInGameThread())
	{
		double OutValue = 0.0;
		bool bHadStat = QueryPeerStat_GameThread(PlayerId, StatToQuery, OutValue);
		QueryCallback(bHadStat, OutValue);
	}
	else
	{
		AsyncTask(ENamedThreads::GameThread, [this, PlayerId, StatToQuery, QueryCallback]() {
			double OutValue = 0.0;
			bool bHadStat = QueryPeerStat_GameThread(PlayerId, StatToQuery, OutValue);
			QueryCallback(bHadStat, OutValue);
		});
	}
}

void UE::PixelStreaming::FStats::RemovePeersStats(FPixelStreamingPlayerId PlayerId)
{
	if (IsInGameThread())
	{
		RemovePeerStat_GameThread(PlayerId);
	}
	else
	{
		AsyncTask(ENamedThreads::GameThread, [this, PlayerId]() { RemovePeerStat_GameThread(PlayerId); });
	}
}

void UE::PixelStreaming::FStats::StorePeerStat(FPixelStreamingPlayerId PlayerId, UE::PixelStreaming::FStatData Stat)
{
	if (IsInGameThread())
	{
		//todo: return bool
		bool bUpdated = StorePeerStat_GameThread(PlayerId, Stat);
		if (bUpdated)
		{
			FireStatChanged_GameThread(PlayerId, Stat.StatName, Stat.StatValue);
		}
	}
	else
	{
		AsyncTask(ENamedThreads::GameThread, [this, PlayerId, Stat]() {
			bool bUpdated = StorePeerStat_GameThread(PlayerId, Stat);
			if (bUpdated)
			{
				FireStatChanged_GameThread(PlayerId, Stat.StatName, Stat.StatValue);
			}
		});
	}
}

void UE::PixelStreaming::FStats::StoreApplicationStat(UE::PixelStreaming::FStatData Stat)
{
	if (IsInGameThread())
	{
		bool bUpdated = StoreApplicationStat_GameThread(Stat);
		if (bUpdated)
		{
			FireStatChanged_GameThread(FPixelStreamingPlayerId(TEXT("Application")), Stat.StatName, Stat.StatValue);
		}
	}
	else
	{
		AsyncTask(ENamedThreads::GameThread, [this, Stat]() {
			bool bUpdated = StoreApplicationStat_GameThread(Stat);
			if (bUpdated)
			{
				FireStatChanged_GameThread(FPixelStreamingPlayerId(TEXT("Application")), Stat.StatName, Stat.StatValue);
			}
		});
	}
}

void UE::PixelStreaming::FStats::FireStatChanged_GameThread(FPixelStreamingPlayerId PlayerId, FName StatName, float StatValue)
{
	checkf(IsInGameThread(), TEXT("This method must be called from the game thread"));

	if (IsEngineExitRequested())
	{
		return;
	}

	if (UPixelStreamingDelegates* Delegates = UPixelStreamingDelegates::GetPixelStreamingDelegates())
	{
		Delegates->OnStatChangedNative.Broadcast(PlayerId, StatName, StatValue);
	}
}

void UE::PixelStreaming::FStats::RemovePeerStat_GameThread(FPixelStreamingPlayerId PlayerId)
{
	checkf(IsInGameThread(), TEXT("This method must be called from the game thread"));

	if (IsEngineExitRequested())
	{
		return;
	}

	PeerStats.Remove(PlayerId);

	if (PlayerId == SFU_PLAYER_ID)
	{
		TArray<FPixelStreamingPlayerId> ToRemove;

		for (auto& Entry : PeerStats)
		{
			FPixelStreamingPlayerId PeerId = Entry.Key;
			if (PeerId.Contains(TEXT("Simulcast"), ESearchCase::IgnoreCase, ESearchDir::FromStart))
			{
				ToRemove.Add(PeerId);
			}
		}

		for (FPixelStreamingPlayerId SimulcastLayerId : ToRemove)
		{
			PeerStats.Remove(SimulcastLayerId);
		}
	}
}

bool UE::PixelStreaming::FStats::QueryPeerStat_GameThread(FPixelStreamingPlayerId PlayerId, FName StatToQuery, double& OutValue) const
{
	verifyf(IsInGameThread(), TEXT("This method must be called from the game thread"));

	const UE::PixelStreaming::FPeerStats* SinglePeerStats = PeerStats.Find(PlayerId);
	if (!SinglePeerStats)
	{
		return false;
	}

	const UE::PixelStreaming::FRenderableStat* StoredStat = SinglePeerStats->StoredStats.Find(StatToQuery);
	if (!StoredStat)
	{
		return false;
	}

	OutValue = StoredStat->Stat.StatValue;
	return true;
}

bool UE::PixelStreaming::FStats::StorePeerStat_GameThread(FPixelStreamingPlayerId PlayerId, UE::PixelStreaming::FStatData Stat)
{
	checkf(IsInGameThread(), TEXT("This method must be called from the game thread"));

	if (IsEngineExitRequested())
	{
		return false;
	}

	if (!PeerStats.Contains(PlayerId))
	{
		PeerStats.Add(PlayerId, UE::PixelStreaming::FPeerStats(PlayerId));
		return true;
	}
	return PeerStats[PlayerId].StoreStat_GameThread(Stat);
}

bool UE::PixelStreaming::FStats::StoreApplicationStat_GameThread(UE::PixelStreaming::FStatData Stat)
{
	checkf(IsInGameThread(), TEXT("This method must be called from the game thread"));

	if (IsEngineExitRequested())
	{
		return false;
	}

	bool bUpdated = false;

	if (ApplicationStats.Contains(Stat.StatName))
	{
		UE::PixelStreaming::FRenderableStat* RenderableStat = ApplicationStats.Find(Stat.StatName);

		if (Stat.bSmooth && RenderableStat->Stat.StatValue != 0)
		{
			double CurValue = RenderableStat->Stat.StatValue;
			double PercentageDrift = FMath::Abs(CurValue - Stat.StatValue) / CurValue;
			if (PercentageDrift > UE::PixelStreaming::FStats::SmoothingFactor)
			{
				RenderableStat->Stat.StatValue = Stat.StatValue;
				bUpdated = true;
			}
			else
			{
				return false;
			}
		}
		else
		{
			bUpdated = RenderableStat->Stat.StatValue != Stat.StatValue;
			RenderableStat->Stat.StatValue = Stat.StatValue;
		}

		if (bUpdated)
		{
			FText TextToDisplay = FText::FromString(FString::Printf(TEXT("%s: %.*f"), *Stat.StatName.ToString(), Stat.NDecimalPlacesToPrint, RenderableStat->Stat.StatValue));
			RenderableStat->CanvasItem.Text = TextToDisplay;
		}
	}
	else
	{
		FText TextToDisplay = FText::FromString(FString::Printf(TEXT("%s: %.*f"), *Stat.StatName.ToString(), Stat.NDecimalPlacesToPrint, Stat.StatValue));

		UE::PixelStreaming::FRenderableStat RenderableStat{
			Stat,
			FCanvasTextItem(FVector2D(0, 0), TextToDisplay, FSlateFontInfo(FSlateFontInfo(UEngine::GetSmallFont(), 12)), FLinearColor(0, 1, 0))
		};

		RenderableStat.CanvasItem.EnableShadow(FLinearColor::Black);

		ApplicationStats.Add(RenderableStat.Stat.StatName, RenderableStat);
		bUpdated = true;
	}
	return bUpdated;
}

void UE::PixelStreaming::FStats::AddOnPeerStatChangedCallback_GameThread(FPixelStreamingPlayerId PlayerId, FName StatToListenOn, TWeakPtr<IPixelStreamingStatsConsumer> Callback)
{
	checkf(IsInGameThread(), TEXT("This method was not called from the game thread."));

	if (IsEngineExitRequested())
	{
		return;
	}

	UE::PixelStreaming::FPeerStats* SinglePeerStats = PeerStats.Find(PlayerId);
	if (!SinglePeerStats)
	{
		PeerStats.Add(PlayerId, UE::PixelStreaming::FPeerStats(PlayerId));
		SinglePeerStats = PeerStats.Find(PlayerId);
	}

	TArray<TWeakPtr<IPixelStreamingStatsConsumer>>* Callbacks = SinglePeerStats->SingleStatConsumers.Find(StatToListenOn);

	if (!Callbacks)
	{
		SinglePeerStats->SingleStatConsumers.Add(StatToListenOn, TArray<TWeakPtr<IPixelStreamingStatsConsumer>>());
		Callbacks = SinglePeerStats->SingleStatConsumers.Find(StatToListenOn);
	}

	Callbacks->Add(Callback);
}

int32 UE::PixelStreaming::FStats::OnRenderStats(UWorld* World, FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const FVector* ViewLocation, const FRotator* ViewRotation)
{
	if (GAreScreenMessagesEnabled)
	{
		Y += 250;
		// Draw each peer's stats in a column, so we must recall where Y starts for each column
		int32 YStart = Y;

		// --------- Draw stats for this Pixel Streaming instance ----------

		for (auto& ApplicationStatEntry : ApplicationStats)
		{
			UE::PixelStreaming::FRenderableStat& StatToDraw = ApplicationStatEntry.Value;
			StatToDraw.CanvasItem.Position.X = X;
			StatToDraw.CanvasItem.Position.Y = Y;
			Canvas->DrawItem(StatToDraw.CanvasItem);
			Y += StatToDraw.CanvasItem.DrawnSize.Y;
		}

		// --------- Draw stats for each peer ----------

		// increment X now we are done drawing application stats
		X += 435;

		// TMap<FPixelStreamingPlayerId, UE::PixelStreaming::FPeerStats> PeerStats;
		for (auto& EachPeerEntry : PeerStats)
		{
			UE::PixelStreaming::FPeerStats& SinglePeerStats = EachPeerEntry.Value;
			if (SinglePeerStats.StoredStats.Num() == 0)
			{
				continue;
			}

			// Reset Y for each peer as each peer gets it own column
			Y = YStart;

			SinglePeerStats.PlayerIdCanvasItem.Position.X = X;
			SinglePeerStats.PlayerIdCanvasItem.Position.Y = Y;
			Canvas->DrawItem(SinglePeerStats.PlayerIdCanvasItem);
			Y += SinglePeerStats.PlayerIdCanvasItem.DrawnSize.Y;

			for (auto& NameStatKeyVal : SinglePeerStats.StoredStats)
			{
				UE::PixelStreaming::FRenderableStat& Stat = NameStatKeyVal.Value;
				Stat.CanvasItem.Position.X = X;
				Stat.CanvasItem.Position.Y = Y;
				Canvas->DrawItem(Stat.CanvasItem);
				Y += Stat.CanvasItem.DrawnSize.Y;
			}

			// Each peer's stats gets its own column
			X += 250;
		}
	}
	return Y;
}

bool UE::PixelStreaming::FStats::OnToggleStats(UWorld* World, FCommonViewportClient* ViewportClient, const TCHAR* Stream)
{
	return false;
}

void UE::PixelStreaming::FStats::PollPixelStreamingSettings()
{
	double DeltaSeconds = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - LastTimeSettingsPolledCycles);
	if (DeltaSeconds > 1)
	{
		StoreApplicationStat(UE::PixelStreaming::FStatData(FName(TEXT("PixelStreaming.Encoder.MinQP")), UE::PixelStreaming::Settings::CVarPixelStreamingEncoderMinQP.GetValueOnAnyThread(), 0));
		StoreApplicationStat(UE::PixelStreaming::FStatData(FName(TEXT("PixelStreaming.Encoder.MaxQP")), UE::PixelStreaming::Settings::CVarPixelStreamingEncoderMaxQP.GetValueOnAnyThread(), 0));
		StoreApplicationStat(UE::PixelStreaming::FStatData(FName(TEXT("PixelStreaming.Encoder.KeyframeInterval (frames)")), UE::PixelStreaming::Settings::CVarPixelStreamingEncoderKeyframeInterval.GetValueOnAnyThread(), 0));
		StoreApplicationStat(UE::PixelStreaming::FStatData(FName(TEXT("PixelStreaming.WebRTC.Fps")), UE::PixelStreaming::Settings::CVarPixelStreamingWebRTCFps.GetValueOnAnyThread(), 0));
		StoreApplicationStat(UE::PixelStreaming::FStatData(FName(TEXT("PixelStreaming.WebRTC.StartBitrate")), UE::PixelStreaming::Settings::CVarPixelStreamingWebRTCStartBitrate.GetValueOnAnyThread(), 0));
		StoreApplicationStat(UE::PixelStreaming::FStatData(FName(TEXT("PixelStreaming.WebRTC.MinBitrate")), UE::PixelStreaming::Settings::CVarPixelStreamingWebRTCMinBitrate.GetValueOnAnyThread(), 0));
		StoreApplicationStat(UE::PixelStreaming::FStatData(FName(TEXT("PixelStreaming.WebRTC.MaxBitrate")), UE::PixelStreaming::Settings::CVarPixelStreamingWebRTCMaxBitrate.GetValueOnAnyThread(), 0));

		LastTimeSettingsPolledCycles = FPlatformTime::Cycles64();
	}
}

void UE::PixelStreaming::FStats::AddOnPeerStatChangedCallback(FPixelStreamingPlayerId PlayerId, FName StatToListenOn, TWeakPtr<IPixelStreamingStatsConsumer> Callback)
{
	if (IsInGameThread())
	{
		AddOnPeerStatChangedCallback_GameThread(PlayerId, StatToListenOn, Callback);
	}
	else
	{
		AsyncTask(ENamedThreads::GameThread, [this, PlayerId, StatToListenOn, Callback]() { AddOnPeerStatChangedCallback_GameThread(PlayerId, StatToListenOn, Callback); });
	}
}

void UE::PixelStreaming::FStats::Tick(float DeltaTime)
{
	// Note (Luke): If we want more metrics from WebRTC there is also the histogram counts.
	// For example:
	// RTC_HISTOGRAM_COUNTS("WebRTC.Video.NacksSent", nacks_sent, 1, 100000, 100);
	// webrtc::metrics::Histogram* Hist1 = webrtc::metrics::HistogramFactoryGetCounts("WebRTC.Video.NacksSent", 0, 100000, 100);
	// Will require calling webrtc::metrics::Enable();

	Sessions->PollWebRTCStats();
	PollPixelStreamingSettings();

	if (!GEngine)
	{
		return;
	}

	// Check if user has enabled on screen stats
	if (!bRegisterEngineStats && UE::PixelStreaming::Settings::CVarPixelStreamingOnScreenStats.GetValueOnGameThread())
	{
		GAreScreenMessagesEnabled = true;
		UEngine::FEngineStatRender RenderFunc = UEngine::FEngineStatRender::CreateRaw(this, &UE::PixelStreaming::FStats::OnRenderStats);
		UEngine::FEngineStatToggle ToggleFunc = UEngine::FEngineStatToggle::CreateRaw(this, &UE::PixelStreaming::FStats::OnToggleStats);
		GEngine->AddEngineStat(PixelStreamingStatName, PixelStreamingStatCategory, PixelStreamingStatDescription, RenderFunc, ToggleFunc, false);

		// Turn on the Engine stat for Pixel Streaming
		for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
		{
			if (WorldContext.WorldType == EWorldType::Game || WorldContext.WorldType == EWorldType::PIE)
			{
				UWorld* World = WorldContext.World();
				UGameViewportClient* ViewportClient = World->GetGameViewport();
				GEngine->SetEngineStat(World, ViewportClient, TEXT("PixelStreaming"), true);
			}
		}

		bRegisterEngineStats = true;
	}
}

//
// ---------------- PixelStreamingPeerStats ---------------------------
// Stats specific to a particular peer, as opposed to the entire app.
//

void UE::PixelStreaming::FPeerStats::StoreStat(UE::PixelStreaming::FStatData StatToStore)
{
	if (IsInGameThread())
	{
		StoreStat_GameThread(StatToStore);
	}
	else
	{
		AsyncTask(ENamedThreads::GameThread, [this, StatToStore]() { StoreStat_GameThread(StatToStore); });
	}
}

bool UE::PixelStreaming::FPeerStats::StoreStat_GameThread(UE::PixelStreaming::FStatData StatToStore)
{
	checkf(IsInGameThread(), TEXT("This method was not called from the game thread."));

	if (IsEngineExitRequested())
	{
		return false;
	}

	bool bUpdated = false;

	if (!StoredStats.Contains(StatToStore.StatName))
	{
		FText TextToDisplay = FText::FromString(FString::Printf(TEXT("%s: %.*f"), *StatToStore.StatName.ToString(), StatToStore.NDecimalPlacesToPrint, StatToStore.StatValue));

		UE::PixelStreaming::FRenderableStat RenderableStat{
			StatToStore,
			FCanvasTextItem(FVector2D(0, 0), TextToDisplay, FSlateFontInfo(FSlateFontInfo(UEngine::GetSmallFont(), 12)), FLinearColor(0, 1, 0))
		};

		RenderableStat.CanvasItem.EnableShadow(FLinearColor::Black);

		StoredStats.Add(StatToStore.StatName, RenderableStat);

		// first time this stat has been stored, so we also need to sort our stats so they render in consistent order
		StoredStats.KeySort([](const FName& A, const FName& B) {
			return A.FastLess(B);
		});

		return true;
	}
	else
	{
		// We already have this stat, so just update it

		UE::PixelStreaming::FRenderableStat* RenderableStat = StoredStats.Find(StatToStore.StatName);
		if (!RenderableStat)
		{
			return false;
		}

		if (RenderableStat->Stat.bSmooth && RenderableStat->Stat.StatValue != 0)
		{
			double CurValue = RenderableStat->Stat.StatValue;
			double PercentageDrift = FMath::Abs(CurValue - StatToStore.StatValue) / CurValue;
			if (PercentageDrift > UE::PixelStreaming::FStats::SmoothingFactor)
			{
				RenderableStat->Stat.StatValue = StatToStore.StatValue;
				bUpdated = true;
			}
			else
			{
				return false;
			}
		}
		else
		{
			bUpdated = RenderableStat->Stat.StatValue != StatToStore.StatValue;
			RenderableStat->Stat.StatValue = StatToStore.StatValue;
		}

		if (!bUpdated)
		{
			return false;
		}
		else
		{
			FText TextToDisplay = FText::FromString(FString::Printf(TEXT("%s: %.*f"), *StatToStore.StatName.ToString(), StatToStore.NDecimalPlacesToPrint, RenderableStat->Stat.StatValue));
			RenderableStat->CanvasItem.Text = TextToDisplay;
		}

		// Fire any callbacks to anyone listening for changing in this stat
		if (!SingleStatConsumers.Contains(StatToStore.StatName))
		{
			return bUpdated;
		}

		TArray<TWeakPtr<IPixelStreamingStatsConsumer>>* StatConsumersArr = SingleStatConsumers.Find(StatToStore.StatName);
		if (!StatConsumersArr)
		{
			return bUpdated;
		}

		for (TWeakPtr<IPixelStreamingStatsConsumer> Consumer : *StatConsumersArr)
		{
			if (Consumer.Pin())
			{
				Consumer.Pin()->ConsumeStat(AssociatedPlayer, StatToStore.StatName, RenderableStat->Stat.StatValue);
			}
		}

		return bUpdated;
	}
}