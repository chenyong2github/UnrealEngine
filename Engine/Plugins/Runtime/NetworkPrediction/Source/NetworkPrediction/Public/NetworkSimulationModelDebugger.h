// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Math/Color.h"
#include "DrawDebugHelpers.h"
#include "Debug/DebugDrawService.h"
#include "Engine/Canvas.h"
#include "Debug/ReporterGraph.h"
#include "Engine/Engine.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "Tickable.h"
#include "NetworkPredictionTypes.h"
#include "CanvasItem.h"
#include "Containers/Array.h"
#include "Templates/UniquePtr.h"
#include "NetworkSimulationModel.h"

DEFINE_LOG_CATEGORY_STATIC(LogNetworkSimDebug, Log, All);

namespace NetworkSimulationModelDebugCVars
{
static int32 DrawKeyframes = 1;
static FAutoConsoleVariableRef CVarDrawKeyframes(TEXT("nsm.debug.DrawKeyFrames"),
	DrawKeyframes, TEXT("Draws keyframe data (text) in debug graphs"), ECVF_Default);

static int32 GatherServerSidePIE = 1;
static FAutoConsoleVariableRef CVarGatherServerSidePIE(TEXT("nsm.debug.GatherServerSide"),
	GatherServerSidePIE, TEXT("Whenever we gather debug info from a client side actor, also gather server side equivelent. Only works in PIE."), ECVF_Default);
}

struct FNetworkSimulationModelDebuggerManager;

NETWORKPREDICTION_API UObject* FindReplicatedObjectOnPIEServer(UObject* ClientObject);

// ------------------------------------------------------------------------------------------------------------------------
//	Debugger support classes
// ------------------------------------------------------------------------------------------------------------------------

struct INetworkSimulationModelDebugger
{
	virtual ~INetworkSimulationModelDebugger() { }

	bool IsActive() { return bActive; }
	void SetActive(bool InActive) { bActive = InActive; }

	virtual void GatherCurrent(FNetworkSimulationModelDebuggerManager& Out, UCanvas* C) = 0;
	virtual void Tick( float DeltaTime ) = 0;

protected:
	bool bActive = false; // Whether you should draw every frame
};

struct NETWORKPREDICTION_API FNetworkSimulationModelDebuggerManager: public FTickableGameObject, FNoncopyable
{
	static FNetworkSimulationModelDebuggerManager& Get();

	~FNetworkSimulationModelDebuggerManager()
	{
		if (Graph.IsValid())
		{
			Graph->RemoveFromRoot();
		}
	}

	FNetworkSimulationModelDebuggerManager()
	{
		DrawDebugServicesHandle = UDebugDrawService::Register(TEXT("Game"), FDebugDrawDelegate::CreateRaw(this, &FNetworkSimulationModelDebuggerManager::DrawDebugService));
		check(DrawDebugServicesHandle.IsValid());
	}

	// ---------------------------------------------------------------------------------------------------------------------------------------
	//	Outside API (registration, console commands, draw services, etc)
	// ---------------------------------------------------------------------------------------------------------------------------------------

	template <typename T, typename TDriver>
	void RegisterNetworkSimulationModel(T* NetworkSim, TDriver* Driver, AActor* OwningActor, FString DebugName);

	void SetDebuggerActive(AActor* OwningActor, bool InActive)
	{
		if (INetworkSimulationModelDebugger* Debugger = Find(OwningActor))
		{
			Debugger->SetActive(InActive);
		}
		ResetCache();
		Gather(LastCanvas.Get());
	}

	void ToggleDebuggerActive(AActor* OwningActor)
	{
		if (INetworkSimulationModelDebugger* Debugger = Find(OwningActor))
		{
			Debugger->SetActive(!Debugger->IsActive());
		}
		ResetCache();
		Gather(LastCanvas.Get());
	}

	void SetContinousGather(bool InGather)
	{
		bContinousGather = InGather;
		if (!bContinousGather)
		{
			Gather(LastCanvas.Get());
		}
	}

	void ToggleContinousGather()
	{
		SetContinousGather(!bContinousGather);
	}

	void DrawDebugService(UCanvas* C, APlayerController* PC)
	{
		LastCanvas = C;
		if (bContinousGather)
		{
			Gather(C);
		}
		
		FDisplayDebugManager& DisplayDebugManager = C->DisplayDebugManager;
		DisplayDebugManager.Initialize(C, GEngine->GetSmallFont(), FVector2D(4.0f, 150.0f));

		if (Lines.Num() > 0)
		{
			const float TextScale = FMath::Max(C->SizeX / 1920.0f, 1.0f);
			FCanvasTileItem TextBackgroundTile(FVector2D(0.0f, 120.0f), FVector2D(400.0f, 1800.0f) * TextScale, FColor(0, 0, 0, 100));
			TextBackgroundTile.BlendMode = SE_BLEND_Translucent;
			C->DrawItem(TextBackgroundTile);
		}

		// --------------------------------------------------------
		//	Lines
		// --------------------------------------------------------

		for (FDebugLine& Line : Lines)
		{
			DisplayDebugManager.SetDrawColor(Line.Color);
			DisplayDebugManager.DrawString(Line.Str);
		}

		// --------------------------------------------------------
		//	Canvas Items (graphs+text)
		// --------------------------------------------------------
		
		for (auto& Item : CanvasItems[0])
		{
			C->DrawItem(*Item.Get());
		}

		if (NetworkSimulationModelDebugCVars::DrawKeyframes > 0)
		{
			for (auto& Item : CanvasItems[1])
			{
				C->DrawItem(*Item.Get());
			}
		}
	}

	virtual void Tick( float DeltaTime )
	{
		for (auto It = DebuggerMap.CreateIterator(); It; ++It)
		{
			AActor* Owner = It.Key().Get();
			if (!Owner)
			{
				It.RemoveCurrent();
				continue;;
			}
			
			if (It.Value()->IsActive())
			{
				It.Value()->Tick(DeltaTime);
			}
		}
	}	

	/** return the stat id to use for this tickable **/
	virtual TStatId GetStatId() const override
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FNetworkSimulationModelDebuggerManager, STATGROUP_TaskGraphTasks);
	}

	/** Gathers latest and Logs single frame */
	void LogSingleFrame(FOutputDevice& Ar)
	{
		Gather(LastCanvas.Get());
		
		for (FDebugLine& Line : Lines)
		{
			Ar.Logf(TEXT("%s"), *Line.Str);
		}
	}

	// ---------------------------------------------------------------------------------------------------------------------------------------
	//	Debugging API used by TNetworkSimulationModelDebugger
	// ---------------------------------------------------------------------------------------------------------------------------------------

	void Emit(const FString& Str = FString(), FColor Color = FColor::White, float XOffset=0.f, float YOffset=0.f)
	{
		Lines.Emplace(FDebugLine{ Str, Color, XOffset, YOffset });
	}

	template <typename TBuffer>
	void EmitElement(TBuffer& Buffer, const FStandardLoggingParameters& Parameters)
	{
		FStringOutputDevice StrOut;
		StrOut.SetAutoEmitLineTerminator(true);

		FStandardLoggingParameters LocalParameters = Parameters;
		LocalParameters.Ar = &StrOut;

		auto* Element = Buffer.FindElementByKeyframe(LocalParameters.Keyframe);
		if (Element)
		{
			Element->Log(LocalParameters);
	
			TArray<FString> StrLines;
			StrOut.ParseIntoArrayLines(StrLines, true);
			for (FString& Str : StrLines)
			{
				Emit(Str);
			}
		}
	}

	void EmitQuad(FVector2D ScreenPosition, FVector2D ScreenSize, FColor Color)
	{
		FVector2D Quad[4];
		
		Quad[0].X = ScreenPosition.X;
		Quad[0].Y = ScreenPosition.Y;

		Quad[1].X = ScreenPosition.X;
		Quad[1].Y = ScreenPosition.Y + ScreenSize.Y;

		Quad[2].X = ScreenPosition.X + ScreenSize.X;
		Quad[2].Y = ScreenPosition.Y + ScreenSize.Y;

		Quad[3].X = ScreenPosition.X + ScreenSize.X;
		Quad[3].Y = ScreenPosition.Y;
		
		CanvasItems[0].Emplace( MakeUnique<FCanvasTriangleItem>(Quad[0], Quad[1], Quad[2], GWhiteTexture) );
		CanvasItems[0].Last()->SetColor(Color);

		CanvasItems[0].Emplace( MakeUnique<FCanvasTriangleItem>(Quad[2], Quad[3], Quad[0], GWhiteTexture) );
		CanvasItems[0].Last()->SetColor(Color);
	}

	void EmitText(FVector2D ScreenPosition, FColor Color, const FString& Str)
	{
		CanvasItems[1].Emplace( MakeUnique<FCanvasTextItem>(ScreenPosition, FText::FromString(Str), GEngine->GetTinyFont(), Color) );
	}

private:

	INetworkSimulationModelDebugger* Find(AActor* Actor)
	{
		if (!Actor)
		{
			return nullptr;
		}

		INetworkSimulationModelDebugger* Debugger = DebuggerMap.FindRef(TWeakObjectPtr<AActor>(Actor));
		if (!Debugger)
		{
			UE_LOG(LogNetworkSimDebug, Warning, TEXT("Could not find NetworkSimulationModel associated with %s"), *GetPathNameSafe(Actor));
		}
		return Debugger;
	}

	void Gather(UCanvas* C)
	{
		ResetCache();

		for (auto It = DebuggerMap.CreateIterator(); It; ++It)
		{
			AActor* Owner = It.Key().Get();
			if (!Owner)
			{
				It.RemoveCurrent();
				continue;;
			}
			
			if (It.Value()->IsActive())
			{
				It.Value()->GatherCurrent(*this, C);
				if (NetworkSimulationModelDebugCVars::GatherServerSidePIE > 0)
				{
					if (AActor* ServerSideActor = Cast<AActor>(FindReplicatedObjectOnPIEServer(Owner)))
					{
						if (INetworkSimulationModelDebugger* ServerSideSim = Find(ServerSideActor))
						{
							Emit();
							Emit();
							ServerSideSim->GatherCurrent(*this, nullptr); // Dont do graphs for server side state
						}
					}
				}
			}
		}
	}

	void ResetCache()
	{
		Lines.Reset();
		
		CanvasItems[0].Reset();
		CanvasItems[1].Reset();
	}

	TMap<TWeakObjectPtr<AActor>, INetworkSimulationModelDebugger*>	DebuggerMap;
	bool bContinousGather = true; // Whether you should gather new data every frame

	FDelegateHandle DrawDebugServicesHandle;
	struct FDebugLine
	{
		FString Str;
		FColor Color;
		float XOffset;
		float YOffset;
	};
	TArray<FDebugLine> Lines;
	TArray<TUniquePtr<FCanvasItem>> CanvasItems[2];
	TWeakObjectPtr<UReporterGraph> Graph;
	TWeakObjectPtr<UCanvas> LastCanvas;
};

template <typename TNetSimModel, typename TDriver>
struct TNetworkSimulationModelDebugger : public INetworkSimulationModelDebugger
{
	TNetworkSimulationModelDebugger(TNetSimModel* InNetSim, TDriver* InDriver, AActor* OwningActor, FString InDebugName)
	{
		NetworkSim = InNetSim;
		Driver = InDriver;
		WeakOwningActor = OwningActor;
		DebugName = InDebugName;
	}

	~TNetworkSimulationModelDebugger()
	{
		
	}	

	void GatherCurrent(FNetworkSimulationModelDebuggerManager& Out, UCanvas* Canvas) override
	{
		AActor* Owner = WeakOwningActor.Get();
		if (!ensure(Owner))
		{
			return;
		}

		// ------------------------------------------------------------------------------------------------------------------------------------------------
		//	Lines
		// ------------------------------------------------------------------------------------------------------------------------------------------------

		Out.Emit(FString::Printf(TEXT("%s - %s"), *Owner->GetName(), *UEnum::GetValueAsString(TEXT("Engine.ENetRole"), Owner->Role)), FColor::Yellow);
		Out.Emit(FString::Printf(TEXT("LastProcessedInputKeyframe: %d (%d Buffered)"), NetworkSim->TickInfo.LastProcessedInputKeyframe, NetworkSim->Buffers.Input.GetHeadKeyframe() - NetworkSim->TickInfo.LastProcessedInputKeyframe));

		// Autorproxy
		{			
			FColor Color = FColor::White;
			const bool FaultDetected = NetworkSim->RepProxy_Autonomous.IsReconcileFaultDetected();

			const int32 LastSerializedKeyframe = NetworkSim->RepProxy_Autonomous.GetLastSerializedKeyframe();

			// Calc how much predicted time we have processed. Note that we use the motionstate buffer to iterate but the MS is on the input cmd. (if we are buffering cmds, don't want to count them)
			float PredictedMS = 0.f;
			for (int32 PredKeyrame = LastSerializedKeyframe+1; PredKeyrame <= NetworkSim->Buffers.Sync.GetHeadKeyframe(); ++PredKeyrame)
			{
				if (auto* Cmd = NetworkSim->Buffers.Input.FindElementByKeyframe(PredKeyrame))
				{
					PredictedMS += Cmd->FrameDeltaTime * 1000.f;
				}
			}

			FString ConfirmedFrameStr = FString::Printf(TEXT("LastConfirmedFrame: %d. Prediction: %d Frames, %.2f MS"), LastSerializedKeyframe, NetworkSim->Buffers.Sync.GetHeadKeyframe() - LastSerializedKeyframe, PredictedMS);
			if (FaultDetected)
			{
				ConfirmedFrameStr += TEXT(" RECONCILE FAULT DETECTED!");
				Color = FColor::Red;
			}

			Out.Emit(*ConfirmedFrameStr, Color);

			FString SimulationTimeString = FString::Printf(TEXT("Local SimulationTime: %s. SerialisedSimulationTime: %s. Difference MS: %s"), *NetworkSim->TickInfo.ProcessedSimulationTime.ToString(),
				*NetworkSim->RepProxy_Autonomous.GetLastSerializedSimulationTimeKeeper().ToString(), *(NetworkSim->TickInfo.ProcessedSimulationTime - NetworkSim->RepProxy_Autonomous.GetLastSerializedSimulationTimeKeeper()).ToString());
			Out.Emit(*SimulationTimeString, Color);
			
		}

		auto EmitBuffer = [&Out](FString BufferName, auto& Buffer)
		{
			Out.Emit();
			Out.Emit(FString::Printf(TEXT("//////////////// %s ///////////////"), *BufferName), FColor::Yellow);
			Out.Emit(FString::Printf(TEXT("%s"), *Buffer.GetBasicDebugStr()));		
			Out.Emit();
			Out.EmitElement(Buffer, FStandardLoggingParameters(nullptr, EStandardLoggingContext::Full, Buffer.GetHeadKeyframe()));
		};

		EmitBuffer(TEXT("InputBuffer"), NetworkSim->Buffers.Input);
		EmitBuffer(TEXT("SyncBuffer"), NetworkSim->Buffers.Sync);

		// ------------------------------------------------------------------------------------------------------------------------------------------------
		//	Canvas
		// ------------------------------------------------------------------------------------------------------------------------------------------------

		auto* DebugBuffer = NetworkSim->GetDebugBuffer();
		if (Canvas && DebugBuffer && DebugBuffer->GetNumValidElements() > 0)
		{
			auto& InputBuffer = NetworkSim->GetHistoricBuffers() ? NetworkSim->GetHistoricBuffers()->Input : NetworkSim->Buffers.Input;

			static float StartPctX = 0.3f;
			static float StartPctY = 0.6f;
			static float LocalFrameHeightPct = 0.01f;

			float ScreenX = StartPctX * (float)Canvas->SizeX;
			float ScreenY = StartPctY * (float)Canvas->SizeY;
			float LocalFrameHeight = LocalFrameHeightPct * (float)Canvas->SizeY;

			static float LocalFrameTimeGreen = 1/30.f;
			static float LocalFrameTimeRed = 1/10.f;

			static float Thickness = 1.f;
			static float ClientOffsetY = 2.f;

			FTextSizingParameters TextSizing;
			TextSizing.DrawFont = GEngine->GetTinyFont();
			TextSizing.Scaling = FVector2D(1.f,1.f);
			Canvas->CanvasStringSize(TextSizing, TEXT("00000"));

			float MinWidth = TextSizing.DrawXL;
			float MinHeight = TextSizing.DrawYL;
			static float MinHeightMS = 1/60.f * 1000.f;	// 60hz frame is drawn at MinHeight.

			auto CalcHeight = [&](float MS)
			{
				const float Ratio = MinHeight / MinHeightMS;
				return MS * Ratio;
			};

			for (auto It = DebugBuffer->CreateIterator(); It; ++It)
			{
				auto* DebugState = It.Element();

				
				float ScreenWidth = MinWidth;
				float FramePct = FMath::Clamp<float>((DebugState->LocalDeltaTimeSeconds - LocalFrameTimeRed) / ( LocalFrameTimeGreen - LocalFrameTimeRed ), 0.f, 1.f);
				FColor Color = FColor::MakeRedToGreenColorFromScalar( FramePct );
				float ServerHeight = CalcHeight(DebugState->LocalDeltaTimeSeconds * 1000.f);

				Out.EmitQuad(FVector2D( ScreenX, ScreenY), FVector2D( ScreenWidth, ServerHeight), Color);
				Out.EmitText(FVector2D( ScreenX, ScreenY), FColor::Black, FString::Printf(TEXT("%.2f"), (DebugState->LocalDeltaTimeSeconds * 1000.f)));

				float ClientSimTime = 0.f;
				float ClientX = ScreenX;
				float ClientY = ScreenY - ClientOffsetY;
				for (int32 Keyframe : DebugState->ProcessedKeyframes)
				{
					auto* Cmd = InputBuffer.FindElementByKeyframe(Keyframe);
					if (Cmd)
					{
						ClientSimTime += Cmd->FrameDeltaTime * 1000.f;
						
						float ClientSizeX = MinWidth;
						float ClientSizeY =  CalcHeight(Cmd->FrameDeltaTime * 1000.f);

						FVector2D ScreenPos(ClientX, ClientY - ClientSizeY);
						Out.EmitQuad(ScreenPos, FVector2D( ClientSizeX, ClientSizeY), FColor::Blue);
						Out.EmitText(ScreenPos, FColor::White, LexToString(Keyframe));
						ClientY -= (ClientSizeY + ClientOffsetY);
					}
				}

				for (int32 Keyframe = DebugState->LastProcessedKeyframe+1; Keyframe <= DebugState->HeadKeyframe; ++Keyframe)
				{
					if (auto* Cmd = InputBuffer.FindElementByKeyframe(Keyframe))
					{
						float ClientSizeX = MinWidth;
						float ClientSizeY = CalcHeight(Cmd->FrameDeltaTime * 1000.f);
						FVector2D ScreenPos(ClientX, ClientY - ClientSizeY);
						Out.EmitQuad(ScreenPos, FVector2D( ClientSizeX, ClientSizeY), FColor::Red);
						Out.EmitText(ScreenPos, FColor::White, LexToString(Keyframe));
						ClientY -= (ClientSizeY + ClientOffsetY);
					}
				}

					
				// -------------------------------------------
				ScreenX += ScreenWidth + 2.f;
			}
		}
	}

	void Tick( float DeltaTime ) override
	{
		AActor* Owner = WeakOwningActor.Get();
		if (!Owner)
		{
			return;
		}

		UWorld* World = Owner->GetWorld();

		if (auto* LatestSync = NetworkSim->Buffers.Sync.GetElementFromHead(0))
		{
			LatestSync->VisualLog( FVisualLoggingParameters(EVisualLoggingContext::LastPredicted, NetworkSim->Buffers.Sync.GetHeadKeyframe(), EVisualLoggingLifetime::Transient), Driver, Driver);
		}

		FStuff ServerPIEStuff = GetServerPIEStuff();
		if (ServerPIEStuff.Driver && ServerPIEStuff.NetworkSim)
		{
			if (auto* ServerLatestSync = ServerPIEStuff.NetworkSim->Buffers.Sync.GetElementFromHead(0))
			{
				ServerLatestSync->VisualLog( FVisualLoggingParameters(EVisualLoggingContext::CurrentServerPIE, ServerPIEStuff.NetworkSim->Buffers.Sync.GetHeadKeyframe(), EVisualLoggingLifetime::Transient), ServerPIEStuff.Driver, Driver);
			}
		}

		for (int32 Keyframe = NetworkSim->RepProxy_Autonomous.GetLastSerializedKeyframe(); Keyframe < NetworkSim->Buffers.Sync.GetHeadKeyframe(); ++Keyframe)
		{
			if (auto* SyncState = NetworkSim->Buffers.Sync.FindElementByKeyframe(Keyframe))
			{
				const EVisualLoggingContext Context = (Keyframe == NetworkSim->RepProxy_Autonomous.GetLastSerializedKeyframe()) ? EVisualLoggingContext::LastConfirmed : EVisualLoggingContext::OtherPredicted;
				SyncState->VisualLog( FVisualLoggingParameters(Context, NetworkSim->Buffers.Sync.GetHeadKeyframe(), EVisualLoggingLifetime::Transient), Driver, Driver);
			}
		}
	}
	

	struct FStuff
	{
		TNetSimModel* NetworkSim = nullptr;
		TDriver* Driver = nullptr;
	};

	FStuff GetStuff()
	{
		return {NetworkSim, Driver};
	}

	TFunction< FStuff() > GetServerPIEStuff;

private:

	
	TWeakObjectPtr<AActor>	WeakOwningActor;
	FString DebugName;

	TNetSimModel* NetworkSim = nullptr;
	TDriver* Driver = nullptr;
};

template <typename T, typename TDriver>
void FNetworkSimulationModelDebuggerManager::RegisterNetworkSimulationModel(T* NetworkSim, TDriver* Driver, AActor* OwningActor, FString DebugName)
{
	TNetworkSimulationModelDebugger<T, TDriver>* Debugger = new TNetworkSimulationModelDebugger<T, TDriver>(NetworkSim, Driver, OwningActor, DebugName);
	DebuggerMap.Add( TWeakObjectPtr<AActor>(OwningActor), Debugger );

	// Gross stuff so that the debugger can find the ServerPIE equiv
	TWeakObjectPtr<AActor> WeakOwner(OwningActor);
	Debugger->GetServerPIEStuff = [WeakOwner, this]()
	{
		if (AActor* ServerOwner = Cast<AActor>(FindReplicatedObjectOnPIEServer(WeakOwner.Get())))
		{
			return ((TNetworkSimulationModelDebugger<T, TDriver>*)DebuggerMap.FindRef(TWeakObjectPtr<AActor>(ServerOwner)))->GetStuff();
		}
		return typename TNetworkSimulationModelDebugger<T, TDriver>::FStuff();
	};
}