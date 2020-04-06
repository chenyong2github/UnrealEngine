// Copyright Epic Games, Inc. All Rights Reserved.

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
#include "NetworkedSimulationModelCVars.h"
#include "VisualLogger/VisualLogger.h"

DEFINE_LOG_CATEGORY_STATIC(LogNetworkSimDebug, Log, All);

namespace NetworkSimulationModelDebugCVars
{
	NETSIM_DEVCVAR_SHIPCONST_INT(DrawCanvas, 1, "nsm.debug.DrawCanvas", "Draws Canvas Debug HUD");
	NETSIM_DEVCVAR_SHIPCONST_INT(DrawFrames, 1, "nsm.debug.DrawFrames", "Draws frame data (text) in debug graphs");
	NETSIM_DEVCVAR_SHIPCONST_INT(DrawNetworkSendLines, 1, "nsm.debug.DrawNetworkSendLines", "Draws lines representing network traffic in debugger");
	NETSIM_DEVCVAR_SHIPCONST_INT(GatherServerSidePIE, 1, "nsm.debug.GatherServerSide", "Whenever we gather debug info from a client side actor, also gather server side equivelent. Only works in PIE.");
}

struct FNetworkSimulationModelDebuggerManager;

NETWORKPREDICTION_API UObject* FindReplicatedObjectOnPIEServer(const UObject* ClientObject);

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

	FNetworkSimulationModelDebuggerManager();
	~FNetworkSimulationModelDebuggerManager();

	// ---------------------------------------------------------------------------------------------------------------------------------------
	//	Outside API (registration, console commands, draw services, etc)
	// ---------------------------------------------------------------------------------------------------------------------------------------

	template <typename T>
	void RegisterNetworkSimulationModel(T* NetworkSim, const AActor* OwningActor);

	void SetDebuggerActive(AActor* OwningActor, bool InActive);

	void ToggleDebuggerActive(AActor* OwningActor);

	void SetContinousGather(bool InGather);

	void ToggleContinousGather()
	{
		SetContinousGather(!bContinousGather);
	}

	void DrawDebugService(UCanvas* C, APlayerController* PC);

	virtual void Tick(float DeltaTime) override;

	/** return the stat id to use for this tickable **/
	virtual TStatId GetStatId() const override;

	/** Gathers latest and Logs single frame */
	void LogSingleFrame(FOutputDevice& Ar);

	// ---------------------------------------------------------------------------------------------------------------------------------------
	//	Debugging API used by TNetworkSimulationModelDebugger
	// ---------------------------------------------------------------------------------------------------------------------------------------

	void Emit(FString Str = FString(), FColor Color = FColor::White, float XOffset = 0.f, float YOffset = 0.f);

	template <typename TBuffer>
	void EmitElement(TBuffer& Buffer, const FStandardLoggingParameters& Parameters)
	{
		FStringOutputDevice StrOut;
		StrOut.SetAutoEmitLineTerminator(true);

		FStandardLoggingParameters LocalParameters = Parameters;
		LocalParameters.Ar = &StrOut;

		auto* Element = Buffer[LocalParameters.Frame];
		if (Element)
		{
			Element->Log(LocalParameters);
	
			TArray<FString> StrLines;
			StrOut.ParseIntoArrayLines(StrLines, true);
			for (FString& Str : StrLines)
			{
				Emit(MoveTemp(Str));
			}
		}
	}

	void EmitQuad(FVector2D ScreenPosition, FVector2D ScreenSize, FColor Color);

	void EmitText(FVector2D ScreenPosition, FColor Color, FString Str);

	void EmitLine(FVector2D StartPosition, FVector2D EndPosition, FColor Color, float Thickness = 1.f);

private:

	INetworkSimulationModelDebugger* Find(const AActor* Actor);

	void Gather(UCanvas* C);

	void ResetCache()
	{
		Lines.Reset();
		
		CanvasItems[0].Reset();
		CanvasItems[1].Reset();
	}

	TMap<TWeakObjectPtr<const AActor>, INetworkSimulationModelDebugger*>	DebuggerMap;
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

template <typename TNetSimModel>
struct TNetworkSimulationModelDebugger : public INetworkSimulationModelDebugger
{
	using TSimTime = typename TNetSimModel::TSimTime;

	TNetworkSimulationModelDebugger(TNetSimModel* InNetSim, const AActor* OwningActor)
	{
		NetworkSim = InNetSim;
		WeakOwningActor = OwningActor;
	}

	~TNetworkSimulationModelDebugger()
	{
		
	}

	struct FCachedScreenPositionMap
	{
		struct FScreenPositions
		{
			void SetSent(const FVector2D& In) { if (SentPosition == FVector2D::ZeroVector) SentPosition = In; }
			void SetRecv(const FVector2D& In) { if (RecvPosition == FVector2D::ZeroVector) RecvPosition = In; }

			FVector2D SentPosition = FVector2D::ZeroVector;
			FVector2D RecvPosition = FVector2D::ZeroVector;
		};

		TMap<int32, FScreenPositions> Frames;
	};

	template<typename TBuffer>
	void GatherDebugGraph(FNetworkSimulationModelDebuggerManager& Out, UCanvas* Canvas, TBuffer* DebugBuffer, FRect DrawRect, const float MaxColumnTimeSeconds, const float MaxLocalFrameTime, const FString& Header, FCachedScreenPositionMap& SendCache, FCachedScreenPositionMap& RecvCache)
	{
		static float Pad = 2.f;
		static float BaseLineYPCT = 0.8f;

		auto& InputBuffer = NetworkSim->GetHistoricBuffers() ? NetworkSim->GetHistoricBuffers()->Input : NetworkSim->State.Input;

		if (Canvas && DebugBuffer && DebugBuffer->Num() > 0)
		{
			// Outline + Header
			Out.EmitLine( FVector2D(DrawRect.Min.X, DrawRect.Min.Y), FVector2D(DrawRect.Min.X, DrawRect.Max.Y), FColor::White );
			Out.EmitLine( FVector2D(DrawRect.Min.X, DrawRect.Min.Y), FVector2D(DrawRect.Max.X, DrawRect.Min.Y), FColor::White );
			Out.EmitLine( FVector2D(DrawRect.Max.X, DrawRect.Min.Y), FVector2D(DrawRect.Max.X, DrawRect.Max.Y), FColor::White );
			Out.EmitLine( FVector2D(DrawRect.Min.X, DrawRect.Max.Y), FVector2D(DrawRect.Max.X, DrawRect.Max.Y), FColor::White );

			Out.EmitText( DrawRect.Min, FColor::White, Header );

			// Frame Columns
			const float BaseLineYPos = DrawRect.Min.Y + (BaseLineYPCT * (DrawRect.Max.Y - DrawRect.Min.Y));

			const float AboveBaseLineSecondsToPixelsY = (BaseLineYPos - DrawRect.Min.Y - Pad) / MaxColumnTimeSeconds;
			const float BelowBaseLineSecondsToPixelsY = (DrawRect.Max.Y - BaseLineYPos - Pad) / MaxLocalFrameTime;

			const float SecondsToPixelsY = FMath::Min<float>(BelowBaseLineSecondsToPixelsY, AboveBaseLineSecondsToPixelsY);

			Out.EmitLine( FVector2D(DrawRect.Min.X, BaseLineYPos), FVector2D(DrawRect.Max.X, BaseLineYPos), FColor::Black );


			FTextSizingParameters TextSizing;
			TextSizing.DrawFont = GEngine->GetTinyFont();
			TextSizing.Scaling = FVector2D(1.f,1.f);
			Canvas->CanvasStringSize(TextSizing, TEXT("00000"));

			const float FixedWidth = TextSizing.DrawXL;

			float ScreenX = DrawRect.Min.X;
			float ScreenY = BaseLineYPos + Pad;

			for (auto It = DebugBuffer->CreateIterator(); It; ++It)
			{
				auto* DebugState = It.Element();
				const float FrameHeight = SecondsToPixelsY * DebugState->LocalDeltaTimeSeconds;
				
				// Green local frame time (below baseline)
				FColor Color = FColor::Green;
				Out.EmitQuad(FVector2D( ScreenX, ScreenY), FVector2D( FixedWidth, FrameHeight), Color);
				Out.EmitText(FVector2D( ScreenX, ScreenY), FColor::Black, FString::Printf(TEXT("%.2f"), (DebugState->LocalDeltaTimeSeconds * 1000.f)));

				// Processed InputcmdsFrames (above baseline)
				float ClientX = ScreenX;
				float ClientY = ScreenY - Pad;

				for (int32 Frame : DebugState->ProcessedFrames)
				{
					auto* Cmd = InputBuffer[Frame];
					if (Cmd)
					{
						float ClientSizeX = FixedWidth;
						float ClientSizeY = SecondsToPixelsY * Cmd->GetFrameDeltaTime().ToRealTimeSeconds();

						FVector2D ScreenPos(ClientX, ClientY - ClientSizeY);
						Out.EmitQuad(ScreenPos, FVector2D( ClientSizeX, ClientSizeY), FColor::Blue);
						Out.EmitText(ScreenPos, FColor::White, LexToString(Frame));
						ClientY -= (ClientSizeY + Pad);
					}
				}

				// Unprocessed InputCmds (above processed)				
				for (int32 Frame = DebugState->PendingFrame; Frame <= DebugState->HeadFrame; ++Frame)
				{
					if (auto* Cmd = InputBuffer[Frame])
					{
						float ClientSizeX = FixedWidth;
						float ClientSizeY = SecondsToPixelsY * Cmd->GetFrameDeltaTime().ToRealTimeSeconds();
						FVector2D ScreenPos(ClientX, ClientY - ClientSizeY);
						Out.EmitQuad(ScreenPos, FVector2D( ClientSizeX, ClientSizeY), FColor::Red);
						Out.EmitText(ScreenPos, FColor::White, LexToString(Frame));
						ClientY -= (ClientSizeY + Pad);
					}
				}

				// Cache Screen Positions based on frame
				RecvCache.Frames.FindOrAdd(DebugState->LastReceivedInputFrame).SetRecv(FVector2D(ScreenX, BaseLineYPos));
				
				// Advance 
				ScreenX += FixedWidth + Pad;

				// Send cache
				SendCache.Frames.FindOrAdd(DebugState->LastSentInputFrame).SetSent(FVector2D(ScreenX, BaseLineYPos));
			}


			// Remaining Simulation Time
			TOptional<FVector2D> LastLinePos;
			FVector2D LinePos(DrawRect.Min.X, BaseLineYPos);
			for (auto It = DebugBuffer->CreateIterator(); It; ++It)
			{
				auto* DebugState = It.Element();
				
				LinePos.X += FixedWidth + Pad;
				LinePos.Y = BaseLineYPos - (DebugState->RemainingAllowedSimulationTimeSeconds * SecondsToPixelsY);

				FColor LineColor =  FColor::White;
				if (LinePos.Y < DrawRect.Min.Y)
				{
					LinePos.Y = DrawRect.Min.Y;
					LineColor = FColor::Red;
				}
				if (LinePos.Y > DrawRect.Max.Y)
				{
					LinePos.Y = DrawRect.Max.Y;
					LineColor = FColor::Red;
				}
				
				if (LastLinePos.IsSet())
				{
					Out.EmitLine(LastLinePos.GetValue(), LinePos, LineColor, 2.f);
				}

				LastLinePos = LinePos;
				
			}
		}
	}

	template<typename TBuffer>
	void CalcMaxColumnFrameTime(TBuffer* DebugBuffer, float& MaxInputTime, float& MaxLocalFrameTime)
	{
		auto& InputBuffer = NetworkSim->GetHistoricBuffers() ? NetworkSim->GetHistoricBuffers()->Input : NetworkSim->State.Input;

		for (auto It = DebugBuffer->CreateIterator(); It; ++It)
		{
			float ColumnTime = 0.f;

			auto* DebugState = It.Element();
			for (int32 Frame : DebugState->ProcessedFrames)
			{
				auto* Cmd = InputBuffer[Frame];
				if (Cmd)
				{
					ColumnTime += (float)Cmd->GetFrameDeltaTime().ToRealTimeSeconds();
				}
			}
			for (int32 Frame = DebugState->PendingFrame; Frame <= DebugState->HeadFrame; ++Frame)
			{
				if (auto* Cmd = InputBuffer[Frame])
				{
					ColumnTime += (float)Cmd->GetFrameDeltaTime().ToRealTimeSeconds();
				}
			}

			MaxInputTime = FMath::Max<float>(ColumnTime, MaxInputTime);
			MaxLocalFrameTime = FMath::Max<float>(DebugState->LocalDeltaTimeSeconds, MaxLocalFrameTime);
		}
	}

	virtual void GatherCurrent(FNetworkSimulationModelDebuggerManager& Out, UCanvas* Canvas) override
	{
		const AActor* Owner = WeakOwningActor.Get();
		if (!ensure(Owner))
		{
			return;
		}

		// ------------------------------------------------------------------------------------------------------------------------------------------------
		//	Lines
		// ------------------------------------------------------------------------------------------------------------------------------------------------

		Out.Emit(FString::Printf(TEXT("%s - %s"), *Owner->GetName(), *UEnum::GetValueAsString(TEXT("Engine.ENetRole"), Owner->GetLocalRole())), FColor::Yellow);
		Out.Emit(FString::Printf(TEXT("PendingFrame: %d (%d Buffered)"), NetworkSim->State.GetPendingTickFrame(), NetworkSim->State.GetLatestInputFrame() - NetworkSim->State.GetPendingTickFrame()));
				
		if (Owner->GetLocalRole() == ROLE_AutonomousProxy)
		{			
			FColor Color = FColor::White;
			const bool FaultDetected = NetworkSim->RepProxy_Autonomous.IsReconcileFaultDetected();

			const int32 LastSerializedFrame = NetworkSim->RepProxy_Autonomous.GetLastSerializedFrame();

			// Calc how much predicted time we have processed.				
			TSimTime PredictedMS;

			for (int32 PredFrame = LastSerializedFrame+1; PredFrame <= NetworkSim->State.GetPendingTickFrame(); ++PredFrame)
			{
				if (auto* Cmd = NetworkSim->State.ReadFrame(PredFrame))
				{
					PredictedMS += Cmd->FrameDeltaTime.Get();
				}
			}

			FString ConfirmedFrameStr = FString::Printf(TEXT("LastConfirmedFrame: %d. Prediction: %d Frames, %s MS"), LastSerializedFrame, NetworkSim->State.GetPendingTickFrame() - LastSerializedFrame, *PredictedMS.ToString());
			if (FaultDetected)
			{
				ConfirmedFrameStr += TEXT(" RECONCILE FAULT DETECTED!");
				Color = FColor::Red;
			}

			Out.Emit(MoveTemp(ConfirmedFrameStr), Color);

			FString SimulationTimeString = FString::Printf(TEXT("Local SimulationTime: %s. SerializedSimulationTime: %s. Difference MS: %s"), *NetworkSim->State.GetTotalProcessedSimulationTime().ToString(),
				*NetworkSim->RepProxy_Autonomous.GetLastSerializedSimTime().ToString(), *(NetworkSim->State.GetTotalProcessedSimulationTime() - NetworkSim->RepProxy_Autonomous.GetLastSerializedSimTime()).ToString());
			Out.Emit(MoveTemp(SimulationTimeString), Color);

			FString AllowedSimulationTimeString = FString::Printf(TEXT("%s"), *NetworkSim->State.DebugString());
			Out.Emit(MoveTemp(AllowedSimulationTimeString), Color);
		}
		else if (Owner->GetLocalRole() == ROLE_SimulatedProxy)
		{
			FColor Color = FColor::White;
			FString TimeString = FString::Printf(TEXT("Total Processed Simulation Time: %s. Last Serialized Simulation Time: %s. Delta: %s"), *NetworkSim->State.GetTotalProcessedSimulationTime().ToString(), *NetworkSim->RepProxy_Simulated.GetLastSerializedSimulationTime().ToString(), *(NetworkSim->RepProxy_Simulated.GetLastSerializedSimulationTime() - NetworkSim->State.GetTotalProcessedSimulationTime()).ToString());
			Out.Emit(MoveTemp(TimeString), Color);
		}
	}

	virtual void Tick( float DeltaTime ) override
	{
		const AActor* Owner = WeakOwningActor.Get();
		if (!Owner)
		{
			return;
		}

		UWorld* World = Owner->GetWorld();

		{
			const int32 HeadFrame = NetworkSim->State.GetPendingTickFrame();
			if (auto* LatestFrameState = NetworkSim->State.ReadFrame(HeadFrame))
			{
				auto* LatestSync = &LatestFrameState->SyncState;
				FVisualLoggingParameters VLogParams(EVisualLoggingContext::LastPredicted, HeadFrame, EVisualLoggingLifetime::Transient);
				NetworkSim->Driver->InvokeVisualLog(&LatestFrameState->InputCmd, LatestSync, NetworkSim->State.ReadAux(HeadFrame), VLogParams);
			}
		}

		{
			FStuff ServerPIEStuff = GetServerPIEStuff();
			if (ServerPIEStuff.NetworkSim)
			{
				const int32 HeadFrame = ServerPIEStuff.NetworkSim->State.GetPendingTickFrame();
				if (auto* LatestFrameState = NetworkSim->State.ReadFrame(HeadFrame))
				{
					FVisualLoggingParameters VLogParams(EVisualLoggingContext::CurrentServerPIE, HeadFrame, EVisualLoggingLifetime::Transient);
					NetworkSim->Driver->InvokeVisualLog(&LatestFrameState->InputCmd, &LatestFrameState->SyncState, ServerPIEStuff.NetworkSim->State.ReadAux(HeadFrame), VLogParams);
				}
			}
		}

		if (Owner->GetLocalRole() == ROLE_AutonomousProxy)
		{
			for (int32 Frame = NetworkSim->RepProxy_Autonomous.GetLastSerializedFrame(); Frame < NetworkSim->State.GetPendingTickFrame(); ++Frame)
			{
				if (auto* FrameState = NetworkSim->State.ReadFrame(Frame))
				{
					const EVisualLoggingContext Context = (Frame == NetworkSim->RepProxy_Autonomous.GetLastSerializedFrame()) ? EVisualLoggingContext::LastConfirmed : EVisualLoggingContext::OtherPredicted;
					FVisualLoggingParameters VLogParams(Context, Frame, EVisualLoggingLifetime::Transient);
					NetworkSim->Driver->InvokeVisualLog(&FrameState->InputCmd, &FrameState->SyncState, NetworkSim->State.ReadAux(Frame), VLogParams);
				}
			}
		}
		else if (Owner->GetLocalRole() == ROLE_SimulatedProxy)
		{
			{
				int32 HeadFrame = NetworkSim->State.GetPendingTickFrame();
				FVisualLoggingParameters VLogParams(EVisualLoggingContext::LastPredicted, HeadFrame, EVisualLoggingLifetime::Transient);
				auto* FrameState = NetworkSim->State.ReadFrame(HeadFrame);
				NetworkSim->Driver->InvokeVisualLog(&FrameState->InputCmd, &FrameState->SyncState, NetworkSim->State.ReadAux(HeadFrame), VLogParams);
			}

			{
				FVisualLoggingParameters VLogParams(EVisualLoggingContext::LastConfirmed, NetworkSim->RepProxy_Simulated.GetLastSerializedFrame(), EVisualLoggingLifetime::Transient);
				NetworkSim->Driver->InvokeVisualLog(&NetworkSim->RepProxy_Simulated.GetLastSerializedInputCmd(), &NetworkSim->RepProxy_Simulated.GetLastSerializedSyncState(), &NetworkSim->RepProxy_Simulated.GetLastSerializedAuxState(), VLogParams);
			}
			
			if (NetworkSim->GetSimulatedUpdateMode() != ESimulatedUpdateMode::Interpolate)
			{
				FVector2D ServerSimulationTimeData(Owner->GetWorld()->GetTimeSeconds(), NetworkSim->RepProxy_Simulated.GetLastSerializedSimulationTime().ToRealTimeMS());
				UE_VLOG_HISTOGRAM(Owner, LogNetworkSimDebug, Log, "Simulated Time Graph", "Serialized Simulation Time", ServerSimulationTimeData);

				FVector2D LocalSimulationTimeData(Owner->GetWorld()->GetTimeSeconds(), NetworkSim->State.GetTotalProcessedSimulationTime().ToRealTimeMS());
				UE_VLOG_HISTOGRAM(Owner, LogNetworkSimDebug, Log, "Simulated Time Graph", "Local Simulation Time", LocalSimulationTimeData);
			}
		}
	}
	

	struct FStuff
	{
		TNetSimModel* NetworkSim = nullptr;
	};

	FStuff GetStuff()
	{
		return {NetworkSim};
	}

	TFunction< FStuff() > GetServerPIEStuff;

private:
	
	TWeakObjectPtr<const AActor> WeakOwningActor;
	TNetSimModel* NetworkSim = nullptr;
};

template <typename T>
void FNetworkSimulationModelDebuggerManager::RegisterNetworkSimulationModel(T* NetworkSim, const AActor* OwningActor)
{
	TNetworkSimulationModelDebugger<T>* Debugger = new TNetworkSimulationModelDebugger<T>(NetworkSim, OwningActor);
	DebuggerMap.Add( TWeakObjectPtr<const AActor>(OwningActor), Debugger );

	// Gross stuff so that the debugger can find the ServerPIE equiv
	TWeakObjectPtr<const AActor> WeakOwner(OwningActor);
	Debugger->GetServerPIEStuff = [WeakOwner, this]()
	{
		if (AActor* ServerOwner = Cast<AActor>(FindReplicatedObjectOnPIEServer(WeakOwner.Get())))
		{
			return ((TNetworkSimulationModelDebugger<T>*)DebuggerMap.FindRef(TWeakObjectPtr<const AActor>(ServerOwner)))->GetStuff();
		}
		return typename TNetworkSimulationModelDebugger<T>::FStuff();
	};
}
