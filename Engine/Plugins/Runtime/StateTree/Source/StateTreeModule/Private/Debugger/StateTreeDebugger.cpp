// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_STATETREE_DEBUGGER

#include "Debugger/StateTreeDebugger.h"
#include "Algo/RemoveIf.h"
#include "Debugger/IStateTreeTraceProvider.h"
#include "Debugger/StateTreeTraceProvider.h"
#include "Debugger/StateTreeTraceTypes.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "StateTree.h"
#include "StateTreeModule.h"
#include "Trace/StoreClient.h"
#include "TraceServices/AnalysisService.h"
#include "TraceServices/ITraceServicesModule.h"
#include "TraceServices/Model/AnalysisSession.h"
#include "TraceServices/Model/Frames.h"
#include "Trace/Analyzer.h"
#include "Trace/Analysis.h"
#include "TraceServices/Model/Diagnostics.h"
#include "GenericPlatform/GenericPlatformMisc.h"

//----------------------------------------------------------------//
// UE::StateTreeDebugger
//----------------------------------------------------------------//
namespace UE::StateTreeDebugger
{
	struct FDiagnosticsSessionAnalyzer : public UE::Trace::IAnalyzer
	{
		virtual void OnAnalysisBegin(const FOnAnalysisContext& Context) override
		{
			auto& Builder = Context.InterfaceBuilder;
			Builder.RouteEvent(RouteId_Session2, "Diagnostics", "Session2");
		}

		virtual bool OnEvent(const uint16 RouteId, EStyle, const FOnEventContext& Context) override
		{
			const FEventData& EventData = Context.EventData;

			switch (RouteId)
			{
			case RouteId_Session2:
				{
					EventData.GetString("Platform", SessionInfo.Platform);
					EventData.GetString("AppName", SessionInfo.AppName);
					EventData.GetString("CommandLine", SessionInfo.CommandLine);
					EventData.GetString("Branch", SessionInfo.Branch);
					EventData.GetString("BuildVersion", SessionInfo.BuildVersion);
					SessionInfo.Changelist = EventData.GetValue<uint32>("Changelist", 0);
					SessionInfo.ConfigurationType = (EBuildConfiguration) EventData.GetValue<uint8>("ConfigurationType");
					SessionInfo.TargetType = (EBuildTargetType) EventData.GetValue<uint8>("TargetType");

					return false;
				}
			default: ;
			}

			return true;
		}

		enum : uint16
		{
			RouteId_Session2,
		};

		TraceServices::FSessionInfo SessionInfo;
	};

} // UE::StateTreeDebugger


//----------------------------------------------------------------//
// FStateTreeDebuggerInstanceDesc
//----------------------------------------------------------------//
FStateTreeDebuggerInstanceDesc::FStateTreeDebuggerInstanceDesc(const UStateTree* InStateTree, const FStateTreeInstanceDebugId InId, const FString& InName)
	: StateTree(InStateTree)
	, Name(InName)
	, Id(InId)	
{ 
}

bool FStateTreeDebuggerInstanceDesc::IsValid() const
{
	return StateTree.IsValid() && Name.Len() && Id.IsValid();
}


//----------------------------------------------------------------//
// FStateTreeDebugger
//----------------------------------------------------------------//
FStateTreeDebugger::FStateTreeDebugger()
	: StateTreeModule(FModuleManager::GetModuleChecked<IStateTreeModule>("StateTreeModule"))
{
}

FStateTreeDebugger::~FStateTreeDebugger()
{
	StopAnalysis();
}

void FStateTreeDebugger::Tick(float DeltaTime)
{
	GetSessionActiveInstances(ActiveInstances);

	if (!bPaused)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FStateTreeDebugger::Tick);
		SyncToCurrentSessionDuration();
	}

	if (DebuggedInstance.IsValid())
	{
		if (!ActiveInstances.Contains(DebuggedInstance))
		{
			SetDebuggedInstance(FStateTreeDebuggerInstanceDesc());
		}
	}
}

bool FStateTreeDebugger::IsTickable() const
{
	return AnalysisSession.IsValid();
}

void FStateTreeDebugger::StopAnalysis()
{
	if (const TraceServices::IAnalysisSession* Session = GetAnalysisSession())
	{
		Session->Stop(true);
		AnalysisSession.Reset();	
	}

	ResetAnalysisData();
}

void FStateTreeDebugger::Pause()
{
	bPaused = true;

	// Force a refresh to make sure most recent traces are processed
	SyncToCurrentSessionDuration();
}

void FStateTreeDebugger::Unpause()
{
	// unpause and let next tick read the traces
	bPaused = false;	
}

void FStateTreeDebugger::SyncToCurrentSessionDuration()
{
	if (const TraceServices::IAnalysisSession* Session = GetAnalysisSession())
	{
		double SessionDuration = 0.;
		{
			TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session);
			SessionDuration = Session->GetDurationSeconds();
		}

		ReadTrace(SessionDuration);
	}
}

const FStateTreeDebuggerInstanceDesc& FStateTreeDebugger::GetDebuggedInstance() const
{
	return DebuggedInstance;
}

FString FStateTreeDebugger::GetDebuggedInstanceDescription() const
{
	return DescribeInstance(DebuggedInstance);
}

void FStateTreeDebugger::SetDebuggedInstance(const FStateTreeDebuggerInstanceDesc& SelectedInstance)
{
	if (DebuggedInstance != SelectedInstance)
	{
		DebuggedInstance = SelectedInstance;
		
		// Reparse traces
		ResetAnalysisData();

		bProcessingInitialEvents = true;
		SyncToCurrentSessionDuration();
		bProcessingInitialEvents = false;
	}
}

TConstArrayView<FStateTreeDebuggerInstanceDesc> FStateTreeDebugger::GetActiveInstances() const
{
	return ActiveInstances;
}

void FStateTreeDebugger::GetSessionActiveInstances(TArray<FStateTreeDebuggerInstanceDesc>& OutInstances) const
{
	if (const TraceServices::IAnalysisSession* Session = GetAnalysisSession())
	{
		TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session);

		if (const IStateTreeTraceProvider* Provider = Session->ReadProvider<IStateTreeTraceProvider>(FStateTreeTraceProvider::ProviderName))
		{
			Provider->GetActiveInstances(OutInstances);
		}
	}
}

void FStateTreeDebugger::StartLastLiveSessionAnalysis()
{
	TArray<FTraceDescriptor> Traces;
	GetLiveTraces(Traces);

	if (Traces.Num())
	{
		StartSessionAnalysis(Traces.Last());
	}
}

void FStateTreeDebugger::StartSessionAnalysis(const FTraceDescriptor& TraceDescriptor)
{
	if (ActiveSessionTraceDescriptor == TraceDescriptor)
	{
		return;
	}
	
	UE::Trace::FStoreClient* StoreClient = GetStoreClient();
	if (StoreClient == nullptr)
	{
		return;
	}

	// Make sure any active analysis is stopped
	StopAnalysis();

	const uint32 TraceId = TraceDescriptor.TraceId;
	
	// Make sure it is still live
	const UE::Trace::FStoreClient::FSessionInfo* SessionInfo = StoreClient->GetSessionInfoByTraceId(TraceId);
	if (SessionInfo != nullptr)
	{
		UE::Trace::FStoreClient::FTraceData TraceData = StoreClient->ReadTrace(TraceId);
		if (!TraceData)
		{
			return;
		}

		FString TraceName(StoreClient->GetStatus()->GetStoreDir());
		const UE::Trace::FStoreClient::FTraceInfo* TraceInfo = StoreClient->GetTraceInfoById(TraceId);
		if (TraceInfo != nullptr)
		{
			FString Name(TraceInfo->GetName());
			if (!Name.EndsWith(TEXT(".utrace")))
			{
				Name += TEXT(".utrace");
			}
			TraceName = FPaths::Combine(TraceName, Name);
			FPaths::NormalizeFilename(TraceName);
		}
		
		ITraceServicesModule& TraceServicesModule = FModuleManager::LoadModuleChecked<ITraceServicesModule>("TraceServices");
		if (const TSharedPtr<TraceServices::IAnalysisService> TraceAnalysisService = TraceServicesModule.GetAnalysisService())
		{
			checkf(!AnalysisSession.IsValid(), TEXT("Must make sure that current session was properly stopped before starting a new one otherwise it can cause threading issues"));
			AnalysisSession = TraceAnalysisService->StartAnalysis(TraceId, *TraceName, MoveTemp(TraceData));
			
			bProcessingInitialEvents = true;
			SyncToCurrentSessionDuration();
			bProcessingInitialEvents = false;
		}

		ActiveSessionTraceDescriptor = AnalysisSession.IsValid() ? TraceDescriptor : FTraceDescriptor();
	}
}

void FStateTreeDebugger::GetLiveTraces(TArray<FTraceDescriptor>& OutTraceDescriptors) const
{
	UE::Trace::FStoreClient* StoreClient = GetStoreClient();
	if (StoreClient == nullptr)
	{
		return;
	}

	OutTraceDescriptors.Reset();

	const uint32 SessionCount = StoreClient->GetSessionCount();
	for (uint32 SessionIndex = 0; SessionIndex < SessionCount; ++SessionIndex)
	{
		const UE::Trace::FStoreClient::FSessionInfo* SessionInfo = StoreClient->GetSessionInfo(SessionIndex);
		if (SessionInfo != nullptr)
		{
			const uint32 TraceId = SessionInfo->GetTraceId();
			const UE::Trace::FStoreClient::FTraceInfo* TraceInfo = StoreClient->GetTraceInfoById(TraceId);
			if (TraceInfo != nullptr)
			{
				FTraceDescriptor& Trace = OutTraceDescriptors.AddDefaulted_GetRef();
				Trace.TraceId = TraceId;
				Trace.Name = FString(TraceInfo->GetName());
				UpdateMetadata(Trace);
			}
		}
	}
}

void FStateTreeDebugger::UpdateMetadata(FTraceDescriptor& TraceDescriptor) const
{
	UE::Trace::FStoreClient* StoreClient = GetStoreClient();
	if (StoreClient == nullptr)
	{
		return;
	}

	const UE::Trace::FStoreClient::FTraceData TraceData = StoreClient->ReadTrace(TraceDescriptor.TraceId);
	if (!TraceData)
	{
		return;
	}

	// inspired from FStoreBrowser
	struct FDataStream : public UE::Trace::IInDataStream
	{
		enum class EReadStatus
		{
			Ready = 0,
			StoppedByReadSizeLimit
		};

		virtual int32 Read(void* Data, const uint32 Size) override
		{
			if (BytesRead >= 1024 * 1024)
			{
				Status = EReadStatus::StoppedByReadSizeLimit;
				return 0;
			}
			const int32 InnerBytesRead = Inner->Read(Data, Size);
			BytesRead += InnerBytesRead;

			return InnerBytesRead;
		}

		virtual void Close() override
		{
			Inner->Close();
		}

		IInDataStream* Inner = nullptr;
		int32 BytesRead = 0;
		EReadStatus Status = EReadStatus::Ready;
	};

	FDataStream DataStream;
	DataStream.Inner = TraceData.Get();

	UE::StateTreeDebugger::FDiagnosticsSessionAnalyzer Analyzer;
	UE::Trace::FAnalysisContext Context;
	Context.AddAnalyzer(Analyzer);
	Context.Process(DataStream).Wait();

	TraceDescriptor.SessionInfo = Analyzer.SessionInfo;
}

FString FStateTreeDebugger::GetDebuggedTraceDescription() const
{
	if (ActiveSessionTraceDescriptor.IsValid())
	{
		return DescribeTrace(ActiveSessionTraceDescriptor);
	}

	return TEXT("No trace selected");
}

FString FStateTreeDebugger::DescribeTrace(const FTraceDescriptor& TraceDescriptor)
{
	if (TraceDescriptor.IsValid())
	{
		const TraceServices::FSessionInfo& SessionInfo = TraceDescriptor.SessionInfo;

		return FString::Printf(TEXT("%s-%s-%s-%s-%s"),
			*LexToString(TraceDescriptor.TraceId),
			*SessionInfo.Platform,
			*SessionInfo.AppName,
			LexToString(SessionInfo.ConfigurationType),
			LexToString(SessionInfo.TargetType));
	}

	return TEXT("Invalid");
}

FString FStateTreeDebugger::DescribeInstance(const FStateTreeDebuggerInstanceDesc& InstanceDesc)
{
	if (FStateTreeDebuggerInstanceDesc() == InstanceDesc)
	{
		return TEXT("No instance selected");
	}
	return LexToString(InstanceDesc);
}

void FStateTreeDebugger::SetActiveStates(const TConstArrayView<FStateTreeStateHandle> NewActiveStates)
{
	ActiveStates = NewActiveStates;
	OnActiveStatesChanged.ExecuteIfBound(ActiveStates);
}

bool FStateTreeDebugger::CanStepBack() const
{
	return bPaused && CurrentFrameIndex != INDEX_NONE
		&& FramesWithEvents.ContainsByPredicate([CurrentIndex = CurrentFrameIndex](const UE::StateTreeDebugger::FFrameIndexSpan& IteratedFrame)
		{
			return IteratedFrame.FrameIdx < CurrentIndex;
		});
}

void FStateTreeDebugger::StepBack()
{
	const int32 PrevFrameIndex = FramesWithEvents.FindLastByPredicate([CurrentIndex = CurrentFrameIndex](const UE::StateTreeDebugger::FFrameIndexSpan& IteratedFrame)
	{
		return IteratedFrame.FrameIdx < CurrentIndex;
	});
	
	if (PrevFrameIndex != INDEX_NONE)
	{
		// Rebuild from beginning up to previous frame with events
		Events.Reset();
		LastTraceReadTime = UnsetTime;
		ReadTrace(FramesWithEvents[PrevFrameIndex].FrameIdx);
		check(CurrentFrameIndex == FramesWithEvents[PrevFrameIndex].FrameIdx);
	}
}

bool FStateTreeDebugger::CanStepForward() const
{
	return bPaused && CurrentFrameIndex != INDEX_NONE
		&& FramesWithEvents.ContainsByPredicate([CurrentIndex = CurrentFrameIndex](const UE::StateTreeDebugger::FFrameIndexSpan& IteratedFrame)
		{
			return IteratedFrame.FrameIdx > CurrentIndex;
		});
}

void FStateTreeDebugger::StepForward()
{
	const UE::StateTreeDebugger::FFrameIndexSpan* NextFrame = FramesWithEvents.FindByPredicate([CurrentIndex = CurrentFrameIndex](const UE::StateTreeDebugger::FFrameIndexSpan& IteratedFrame)
	{
		return IteratedFrame.FrameIdx > CurrentIndex;
	});
	
	if (NextFrame != nullptr)
	{
		const uint64 FrameIndex = (*NextFrame).FrameIdx; 
		ReadTrace(FrameIndex);
		check(CurrentFrameIndex == FrameIndex);
	}
}

void FStateTreeDebugger::ToggleBreakpoints(const TConstArrayView<FStateTreeStateHandle> SelectedStates)
{
	TArray<FStateTreeStateHandle> CurrentBreakpoints = StatesWithBreakpoint;	
	TArray<FStateTreeStateHandle> NewBreakpoints(SelectedStates);

	// remove from the selected states any state that already has a breakpoint
	const int32 ExistingBreakpointsStartIndex = Algo::RemoveIf
	(NewBreakpoints,
		[&CurrentBreakpoints](const FStateTreeStateHandle BreakpointToToggle)
		{
			return CurrentBreakpoints.Contains(BreakpointToToggle);
		});

	for (int32 i = ExistingBreakpointsStartIndex; i < NewBreakpoints.Num(); i++)
	{
		CurrentBreakpoints.RemoveSingleSwap(NewBreakpoints[i], /*bAllowShrinking*/false);
	}

	NewBreakpoints.SetNum(ExistingBreakpointsStartIndex, /*bAllowShrinking*/false);

	// Both lists were reduced and can be merged as the new complete list of breakpoints.
	CurrentBreakpoints.Append(NewBreakpoints);

	StatesWithBreakpoint = CurrentBreakpoints;

	OnBreakpointsChanged.ExecuteIfBound(StatesWithBreakpoint);
}

const TraceServices::IAnalysisSession* FStateTreeDebugger::GetAnalysisSession() const
{
	return AnalysisSession.Get();
}

UE::Trace::FStoreClient* FStateTreeDebugger::GetStoreClient() const
{
	return StateTreeModule.GetStoreClient();
}

void FStateTreeDebugger::ResetAnalysisData()
{
	Events.Reset();
	HitBreakpointStateIndex = INDEX_NONE;
	HitBreakpointInstanceId.Reset();
	LastTraceReadTime = UnsetTime;
	CurrentScrubTime = UnsetTime;
	CurrentFrameIndex = INDEX_NONE;
	FramesWithEvents.Reset();

	SetActiveStates({});
}

void FStateTreeDebugger::ReadTrace(const uint64 FrameIndex)
{
	if (const TraceServices::IAnalysisSession* Session = GetAnalysisSession())
	{
		TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session);

		const TraceServices::IFrameProvider& FrameProvider = ReadFrameProvider(*Session);

		if (const TraceServices::FFrame* TargetFrame = FrameProvider.GetFrame(TraceFrameType_Game, FrameIndex))
		{
			CurrentScrubTime = TargetFrame->EndTime;
			ReadTrace(*Session, FrameProvider, *TargetFrame);
		}
	}

	// Notify outside session read scope
	SendNotifications();
}

void FStateTreeDebugger::ReadTrace(const double ScrubTime)
{
	if (const TraceServices::IAnalysisSession* Session = GetAnalysisSession())
	{
		TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session);

		const TraceServices::IFrameProvider& FrameProvider = ReadFrameProvider(*Session);

		TraceServices::FFrame TargetFrame;
		if (FrameProvider.GetFrameFromTime(TraceFrameType_Game, ScrubTime, TargetFrame))
		{
			CurrentScrubTime = ScrubTime;
			ReadTrace(*Session, FrameProvider, TargetFrame);						
		}
	}

	// Notify outside session read scope
	SendNotifications();
}

void FStateTreeDebugger::SendNotifications()
{
	if (bNewEvents)
	{
		OnTracesUpdated.ExecuteIfBound(Events, FramesWithEvents);
		bNewEvents = false;
	}

	if (HitBreakpointStateIndex != INDEX_NONE)
	{
		check(HitBreakpointInstanceId.IsValid());
		check(StatesWithBreakpoint.IsValidIndex(HitBreakpointStateIndex));
		OnBreakpointHit.ExecuteIfBound(HitBreakpointInstanceId, StatesWithBreakpoint[HitBreakpointStateIndex]);

		HitBreakpointStateIndex = INDEX_NONE;
		HitBreakpointInstanceId.Reset();
	}
}

void FStateTreeDebugger::ReadTrace(
	const TraceServices::IAnalysisSession& Session,
	const TraceServices::IFrameProvider& FrameProvider,
	const TraceServices::FFrame& Frame
	)
{
	TraceServices::FFrame LastReadFrame;
	const bool bValidLastReadFrame = FrameProvider.GetFrameFromTime(TraceFrameType_Game, LastTraceReadTime, LastReadFrame);
	if (LastTraceReadTime == UnsetTime || (bValidLastReadFrame && Frame.Index > LastReadFrame.Index))
	{
		if (const IStateTreeTraceProvider* Provider = Session.ReadProvider<IStateTreeTraceProvider>(FStateTreeTraceProvider::ProviderName))
		{
			AddEvents(LastTraceReadTime, Frame.EndTime, FrameProvider, *Provider);
			LastTraceReadTime = CurrentScrubTime;
		}
	}

	if (FramesWithEvents.ContainsByPredicate([CurrentIndex = CurrentFrameIndex](const UE::StateTreeDebugger::FFrameIndexSpan& IteratedFrame)
		{
			return IteratedFrame.FrameIdx == CurrentIndex;
		}))
	{
		CurrentFrameIndex = Frame.Index;	
	}
	else if (FramesWithEvents.Num())
	{
		CurrentFrameIndex = FramesWithEvents.Last().FrameIdx;
	}
}

bool FStateTreeDebugger::ProcessEvent(const FStateTreeInstanceDebugId InstanceId, const TraceServices::FFrame& Frame, const FStateTreeTraceEventVariantType& Event)
{
	if (DebuggedInstance.Id == InstanceId && Event.IsType<FStateTreeTraceActiveStatesEvent>())
	{
		// Active states
		SetActiveStates(Event.Get<FStateTreeTraceActiveStatesEvent>().ActiveStates);
	}
	else
	{
		// Breakpoints 
		if (bPaused == false // ignored when scrubbing a paused session
			&& bProcessingInitialEvents == false // only processed for events added after the initial parsing
			&& DebuggedAsset != nullptr // asset is required to properly match state handles
			&& HitBreakpointStateIndex == INDEX_NONE // stop on first hit breakpoint
			&& StatesWithBreakpoint.Num()
			&& (DebuggedInstance.Id == InstanceId || !DebuggedInstance.IsValid())) // allow breakpoints on any instances if not specified
		{
			const FStateTreeTraceStateEvent* StateEvent = Event.TryGet<FStateTreeTraceStateEvent>();
			if (StateEvent != nullptr && StateEvent->EventType == EStateTreeTraceNodeEventType::OnEnter)
			{
				const UStateTree* InstanceStateTree = nullptr;
				if (!DebuggedInstance.IsValid())
				{
					// No specific instance selected yet, find matching descriptor from id to extract the associated StateTree asset
					const FStateTreeDebuggerInstanceDesc* FoundInstance = ActiveInstances.FindByPredicate(
						[InstanceId](const FStateTreeDebuggerInstanceDesc& InstanceDesc)
						{
							return InstanceDesc.Id == InstanceId;
						});

					if (FoundInstance != nullptr)
					{
						InstanceStateTree = FoundInstance->StateTree.Get();
					}
				}

				if (DebuggedInstance.IsValid() || InstanceStateTree == DebuggedAsset)
				{
					HitBreakpointStateIndex = StatesWithBreakpoint.Find(FStateTreeStateHandle(StateEvent->StateIdx));
					if (HitBreakpointStateIndex != INDEX_NONE)
					{
						HitBreakpointInstanceId = InstanceId;
					}
				}
			}
		}

		// Store events for currently debugged entry 
		if (DebuggedInstance.Id == InstanceId)
		{
			if (FramesWithEvents.IsEmpty() || FramesWithEvents.Last().FrameIdx < Frame.Index)
			{
				FramesWithEvents.Add(UE::StateTreeDebugger::FFrameIndexSpan(Frame.Index, Events.Num()));
			}

			Events.Emplace(Event);
			bNewEvents = true;
		}
	}
	
	return /*bKeepProcessing*/true;
}

void FStateTreeDebugger::AddEvents(float StartTime, float EndTime, const TraceServices::IFrameProvider& FrameProvider, const IStateTreeTraceProvider& StateTreeTraceProvider)
{
	StateTreeTraceProvider.ReadTimelines(DebuggedInstance.Id,
		[this, StartTime, EndTime, &FrameProvider](const FStateTreeInstanceDebugId InstanceId, const IStateTreeTraceProvider::FEventsTimeline& TimelineData)
		{
			// Keep track of the frames containing events. Starting with an invalid frame.
			TraceServices::FFrame Frame;
			Frame.Index = INDEX_NONE;
			
			TimelineData.EnumerateEvents(StartTime,	EndTime,
				[this, InstanceId, &FrameProvider, &Frame](const double EventStartTime, const double EventEndTime, uint32 InDepth, const FStateTreeTraceEventVariantType& Event)
				{
					// Fetch frame when not set yet or if events no longer part of the current one
					if (Frame.Index == INDEX_NONE ||
						(EventEndTime < Frame.StartTime || Frame.EndTime < EventStartTime))
					{
						FrameProvider.GetFrameFromTime(TraceFrameType_Game, EventStartTime, Frame);	
					}
					
					const bool bKeepProcessing = ProcessEvent(InstanceId, Frame, Event);
					return bKeepProcessing ? TraceServices::EEventEnumerate::Continue : TraceServices::EEventEnumerate::Stop;
				});
		});
}

#endif // WITH_STATETREE_DEBUGGER