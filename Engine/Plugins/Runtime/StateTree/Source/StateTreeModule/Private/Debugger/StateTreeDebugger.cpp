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

#define LOCTEXT_NAMESPACE "StateTreeDebugger"

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
// FStateTreeDebugger
//----------------------------------------------------------------//
FStateTreeDebugger::FStateTreeDebugger()
	: StateTreeModule(FModuleManager::GetModuleChecked<IStateTreeModule>("StateTreeModule"))
	, ScrubState(EventCollections)
{
}

FStateTreeDebugger::~FStateTreeDebugger()
{
	StopAnalysis();
}

void FStateTreeDebugger::Tick(const float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FStateTreeDebugger::Tick);

	if (RetryLoadNextLiveSessionTimer > 0.0f)
	{
		// We are still not connected to the last live session.
		// Update polling timer and retry with remaining time; 0 or less will stop retries.
		if (TryStartNewLiveSessionAnalysis(RetryLoadNextLiveSessionTimer - DeltaTime))
		{
			RetryLoadNextLiveSessionTimer = 0.0f;
			LastLiveSessionId = INDEX_NONE;
		}
	}
	
	UpdateInstances();
	SyncToCurrentSessionDuration();
}

void FStateTreeDebugger::StopAnalysis()
{
	if (const TraceServices::IAnalysisSession* Session = GetAnalysisSession())
	{
		Session->Stop(true);
		AnalysisSession.Reset();	
	}
}

void FStateTreeDebugger::Pause()
{
	bPaused = true;
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
		{
			TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session);
			AnalysisDuration = Session->GetDurationSeconds();
		}
		ReadTrace(AnalysisDuration);
	}
}

FText FStateTreeDebugger::GetInstanceName(FStateTreeInstanceDebugId InstanceId) const
{
	using namespace UE::StateTreeDebugger;
	const FInstanceDescriptor* FoundInstance = InstanceDescs.FindByPredicate([InstanceId](const FInstanceDescriptor& InstanceDesc)
		{
			return InstanceDesc.Id == InstanceId;
		});

	return (FoundInstance != nullptr) ? FText::FromString(FoundInstance->Name) : LOCTEXT("InstanceNotFound","Instance not found");
}

FText FStateTreeDebugger::GetInstanceDescription(FStateTreeInstanceDebugId InstanceId) const
{
	using namespace UE::StateTreeDebugger;
	const FInstanceDescriptor* FoundInstance = InstanceDescs.FindByPredicate([InstanceId](const FInstanceDescriptor& InstanceDesc)
		{
			return InstanceDesc.Id == InstanceId;
		});

	return (FoundInstance != nullptr) ? DescribeInstance(*FoundInstance) : LOCTEXT("InstanceNotFound","Instance not found");
}

void FStateTreeDebugger::SelectInstance(const FStateTreeInstanceDebugId InstanceId)
{
	if (SelectedInstanceId != InstanceId)
	{
		SelectedInstanceId = InstanceId;

		// Notify so listener can cleanup anything related to previous instance
		OnSelectedInstanceCleared.ExecuteIfBound();

		// Update event collection index for newly debugged instance
		ScrubState.SetEventCollectionIndex(InstanceId.IsValid() ? EventCollections.IndexOfByPredicate([InstanceId = InstanceId](const UE::StateTreeDebugger::FInstanceEventCollection& Entry)
			{
				return Entry.InstanceId == InstanceId;
			})
			: INDEX_NONE);

		OnScrubStateChanged.Execute(ScrubState);

		RefreshActiveStates();
	}
}

void FStateTreeDebugger::GetSessionInstances(TArray<UE::StateTreeDebugger::FInstanceDescriptor>& OutInstances) const
{
	if (const TraceServices::IAnalysisSession* Session = GetAnalysisSession())
	{
		TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session);

		if (const IStateTreeTraceProvider* Provider = Session->ReadProvider<IStateTreeTraceProvider>(FStateTreeTraceProvider::ProviderName))
		{
			Provider->GetInstances(OutInstances);
		}
	}
}

void FStateTreeDebugger::UpdateInstances()
{
	if (const TraceServices::IAnalysisSession* Session = GetAnalysisSession())
	{
		TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session);

		if (const IStateTreeTraceProvider* Provider = Session->ReadProvider<IStateTreeTraceProvider>(FStateTreeTraceProvider::ProviderName))
		{
			Provider->GetInstances(InstanceDescs);
		}
	}
}

void FStateTreeDebugger::RequestAnalysisOfNextLiveSession()
{
	// Invalidate our current active session
	ActiveSessionTraceDescriptor = FTraceDescriptor();

	// Stop current analysis if any
	StopAnalysis();

	TArray<FTraceDescriptor> Traces;
	GetLiveTraces(Traces);
	
	LastLiveSessionId = Traces.Num() ? Traces.Last().TraceId : INDEX_NONE;

	// This won't succeed yet but will schedule our next retry
	TryStartNewLiveSessionAnalysis(1.0f);
}

bool FStateTreeDebugger::TryStartNewLiveSessionAnalysis(const float RetryPollingDuration)
{
	TArray<FTraceDescriptor> Traces;
	GetLiveTraces(Traces);

	if (Traces.Num() && Traces.Last().TraceId != LastLiveSessionId)
	{
		return StartSessionAnalysis(Traces.Last());
	}
	
	RetryLoadNextLiveSessionTimer = RetryPollingDuration;
	ensure(RetryLoadNextLiveSessionTimer > 0);
	UE_CLOG(RetryLoadNextLiveSessionTimer > 0, LogStateTree, Log, TEXT("Unable to start analysis for the most recent live session."));

	return false;
}

bool FStateTreeDebugger::StartSessionAnalysis(const FTraceDescriptor& TraceDescriptor)
{
	if (ActiveSessionTraceDescriptor == TraceDescriptor)
	{
		return ActiveSessionTraceDescriptor.IsValid();
	}

	ActiveSessionTraceDescriptor = FTraceDescriptor();
	UE::Trace::FStoreClient* StoreClient = GetStoreClient();
	if (StoreClient == nullptr)
	{
		return false;
	}

	// Make sure any active analysis is stopped
	StopAnalysis();

	RecordingDuration = 0;
	AnalysisDuration = 0;
	LastTraceReadTime = 0;

	const uint32 TraceId = TraceDescriptor.TraceId;

	// Make sure it is still live
	const UE::Trace::FStoreClient::FSessionInfo* SessionInfo = StoreClient->GetSessionInfoByTraceId(TraceId);
	if (SessionInfo != nullptr)
	{
		UE::Trace::FStoreClient::FTraceData TraceData = StoreClient->ReadTrace(TraceId);
		if (!TraceData)
		{
			return false;
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
			
			SyncToCurrentSessionDuration();
		}

		if (AnalysisSession.IsValid())
		{
			ActiveSessionTraceDescriptor = TraceDescriptor;	
		}
	}

	return ActiveSessionTraceDescriptor.IsValid();
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

FText FStateTreeDebugger::GetSelectedTraceDescription() const
{
	if (ActiveSessionTraceDescriptor.IsValid())
	{
		return DescribeTrace(ActiveSessionTraceDescriptor);
	}

	return LOCTEXT("NoSelectedTraceDescriptor", "No trace selected");
}

void FStateTreeDebugger::SetScrubTime(const double ScrubTime)
{
	if (ScrubState.SetScrubTime(ScrubTime))
	{
		OnScrubStateChanged.Execute(ScrubState);

		RefreshActiveStates();
	}
}

bool FStateTreeDebugger::IsActiveInstance(const double Time, const FStateTreeInstanceDebugId InstanceId) const
{
	for (const UE::StateTreeDebugger::FInstanceDescriptor& Desc : InstanceDescs)
	{
		if (Desc.Id == InstanceId && Desc.Lifetime.Contains(Time))
		{
			return true;
		}
	}
	return false;
}

FText FStateTreeDebugger::DescribeTrace(const FTraceDescriptor& TraceDescriptor)
{
	if (TraceDescriptor.IsValid())
	{
		const TraceServices::FSessionInfo& SessionInfo = TraceDescriptor.SessionInfo;

		return FText::FromString(FString::Printf(TEXT("%s-%s-%s-%s-%s"),
			*LexToString(TraceDescriptor.TraceId),
			*SessionInfo.Platform,
			*SessionInfo.AppName,
			LexToString(SessionInfo.ConfigurationType),
			LexToString(SessionInfo.TargetType)));
	}

	return LOCTEXT("InvalidTraceDescriptor", "Invalid");
}

FText FStateTreeDebugger::DescribeInstance(const UE::StateTreeDebugger::FInstanceDescriptor& InstanceDesc)
{
	if (InstanceDesc.IsValid() == false)
	{
		return LOCTEXT("NoSelectedInstanceDescriptor", "No instance selected");
	}
	return FText::FromString(LexToString(InstanceDesc));
}

void FStateTreeDebugger::SetActiveStates(const TConstArrayView<FStateTreeStateHandle> NewActiveStates)
{
	ActiveStates = NewActiveStates;
	OnActiveStatesChanged.ExecuteIfBound(ActiveStates);
}

void FStateTreeDebugger::RefreshActiveStates()
{
	TArray<FStateTreeStateHandle> NewActiveStates;

	if (ScrubState.IsPointingToValidActiveStates())
	{
		const UE::StateTreeDebugger::FInstanceEventCollection& EventCollection = EventCollections[ScrubState.GetEventCollectionIndex()];
		const int32 EventIndex = EventCollection.ActiveStatesChanges[ScrubState.GetActiveStatesIndex()].EventIndex;
		NewActiveStates = EventCollection.Events[EventIndex].Get<FStateTreeTraceActiveStatesEvent>().ActiveStates;
	}

	SetActiveStates(NewActiveStates);
}

bool FStateTreeDebugger::CanStepBackToPreviousStateWithEvents() const
{
	return bPaused ? ScrubState.HasPreviousFrame() : false;
}

void FStateTreeDebugger::StepBackToPreviousStateWithEvents()
{
	ScrubState.GotoPreviousFrame();
	OnScrubStateChanged.Execute(ScrubState);
	
	RefreshActiveStates();
}

bool FStateTreeDebugger::CanStepForwardToNextStateWithEvents() const
{
	return bPaused ? ScrubState.HasNextFrame() : false;
}

void FStateTreeDebugger::StepForwardToNextStateWithEvents()
{
	ScrubState.GotoNextFrame();
	OnScrubStateChanged.Execute(ScrubState);

	RefreshActiveStates();
}

bool FStateTreeDebugger::CanStepBackToPreviousStateChange() const
{
	return bPaused ? ScrubState.HasPreviousActiveStates() : false;
}

void FStateTreeDebugger::StepBackToPreviousStateChange()
{
	ScrubState.GotoPreviousActiveStates();
	OnScrubStateChanged.Execute(ScrubState);
	
	RefreshActiveStates();
}

bool FStateTreeDebugger::CanStepForwardToNextStateChange() const
{
	return bPaused ? ScrubState.HasNextActiveStates() : false;
}

void FStateTreeDebugger::StepForwardToNextStateChange()
{
	ScrubState.GotoNextActiveStates();
	OnScrubStateChanged.Execute(ScrubState);

	RefreshActiveStates();
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

void FStateTreeDebugger::ReadTrace(const uint64 FrameIndex)
{
	if (const TraceServices::IAnalysisSession* Session = GetAnalysisSession())
	{
		TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session);

		const TraceServices::IFrameProvider& FrameProvider = ReadFrameProvider(*Session);

		if (const TraceServices::FFrame* TargetFrame = FrameProvider.GetFrame(TraceFrameType_Game, FrameIndex))
		{
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
			// Process only completed frames 
			if (TargetFrame.EndTime == std::numeric_limits<double>::infinity())
			{
				if (const TraceServices::FFrame* PreviousCompleteFrame = FrameProvider.GetFrame(TraceFrameType_Game, TargetFrame.Index - 1))
				{
					ReadTrace(*Session, FrameProvider, *PreviousCompleteFrame);
				}
			}
			else
			{
				ReadTrace(*Session, FrameProvider, TargetFrame);
			}
		}
	}

	// Notify outside session read scope
	SendNotifications();
}

void FStateTreeDebugger::SendNotifications()
{
	if (NewInstances.Num() > 0)
	{
		for (const FStateTreeInstanceDebugId NewInstanceId : NewInstances)
		{
			OnNewInstance.ExecuteIfBound(NewInstanceId);
		}
		NewInstances.Reset();
	}

	if (HitBreakpointStateIndex != INDEX_NONE)
	{
		check(HitBreakpointInstanceId.IsValid());
		check(StatesWithBreakpoint.IsValidIndex(HitBreakpointStateIndex));

		// Force scrub time to latest read time to reflect most recent events.
		// This will notify scrub position changed and active states
		SetScrubTime(LastTraceReadTime);

		// Make sure the instance is selected in case the breakpoint was set for any instances 
		if (SelectedInstanceId != HitBreakpointInstanceId)
		{
			SelectInstance(HitBreakpointInstanceId);
		}

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
	if (LastTraceReadTime == 0 || (bValidLastReadFrame && Frame.Index > LastReadFrame.Index))
	{
		if (const IStateTreeTraceProvider* Provider = Session.ReadProvider<IStateTreeTraceProvider>(FStateTreeTraceProvider::ProviderName))
		{
			AddEvents(LastTraceReadTime, Frame.EndTime, FrameProvider, *Provider);
			LastTraceReadTime = Frame.EndTime;
		}
	}
}

bool FStateTreeDebugger::ProcessEvent(const FStateTreeInstanceDebugId InstanceId, const TraceServices::FFrame& Frame, const FStateTreeTraceEventVariantType& Event)
{
	using namespace UE::StateTreeDebugger;

	// Breakpoints 
	if (bPaused == false // ignored when scrubbing a paused session
		&& StateTreeAsset != nullptr // asset is required to properly match state handles
		&& HitBreakpointStateIndex == INDEX_NONE // stop on first hit breakpoint
		&& StatesWithBreakpoint.Num()
		&& (SelectedInstanceId == InstanceId || SelectedInstanceId.IsInvalid())) // allow breakpoints on any instances if not specified
	{
		const FStateTreeTraceStateEvent* StateEvent = Event.TryGet<FStateTreeTraceStateEvent>();
		if (StateEvent != nullptr && StateEvent->EventType == EStateTreeTraceEventType::OnEnter)
		{
			const UStateTree* InstanceStateTree = nullptr;
			if (SelectedInstanceId.IsInvalid())
			{
				// No specific instance selected yet, find matching descriptor from id to extract the associated StateTree asset
				const FInstanceDescriptor* FoundInstance = InstanceDescs.FindByPredicate(
					[InstanceId](const FInstanceDescriptor& InstanceDesc)
					{
						return InstanceDesc.Id == InstanceId;
					});

				if (FoundInstance != nullptr)
				{
					InstanceStateTree = FoundInstance->StateTree.Get();
				}
			}

			if (SelectedInstanceId.IsValid() || InstanceStateTree == StateTreeAsset)
			{
				HitBreakpointStateIndex = StatesWithBreakpoint.Find(StateEvent->GetStateHandle());
				if (HitBreakpointStateIndex != INDEX_NONE)
				{
					HitBreakpointInstanceId = InstanceId;
				}
			}
		}
	}

	FInstanceEventCollection* ExistingCollection = EventCollections.FindByPredicate([InstanceId](const FInstanceEventCollection& Entry)
		{
			return Entry.InstanceId == InstanceId;
		});

	// Create missing EventCollection if necessary
	if (ExistingCollection == nullptr)
	{
		// Push deferred notification for new instance Id
		NewInstances.Push(InstanceId);

		// Update the active event collection index when it's newly created for the currently debugged instance.
		// Otherwise (i.e. EventCollection already exists) it is updated when switching instance (i.e. SelectInstance)
		if (SelectedInstanceId == InstanceId && ScrubState.GetEventCollectionIndex() == INDEX_NONE)
		{
			ScrubState.SetEventCollectionIndex(EventCollections.Num());
		}

		ExistingCollection = &EventCollections.Emplace_GetRef(InstanceId);
	}

	check(ExistingCollection);
	TArray<FStateTreeTraceEventVariantType>& Events = ExistingCollection->Events;

	// Add new frame span if none added yet or new frame
	if (ExistingCollection->FrameSpans.IsEmpty() || ExistingCollection->FrameSpans.Last().Frame.Index < Frame.Index)
	{
		double RecordingWorldTime = 0;
		Visit([&RecordingWorldTime](auto& TypedEvent)
			{
				RecordingWorldTime = TypedEvent.RecordingWorldTime;
			}, Event);

		// Update global recording duration
		RecordingDuration = RecordingWorldTime;

		ExistingCollection->FrameSpans.Add(UE::StateTreeDebugger::FFrameSpan(Frame, RecordingWorldTime, Events.Num()));
	}

	// Add activate states change info
	if (Event.IsType<FStateTreeTraceActiveStatesEvent>())
	{
		checkf(ExistingCollection->FrameSpans.Num() > 0, TEXT("Expecting to always be in a frame span at this point."));
		const int32 FrameSpanIndex = ExistingCollection->FrameSpans.Num()-1;

		// Add new entry for the first event or if the last event is for a different frame
		if (ExistingCollection->ActiveStatesChanges.IsEmpty()
			|| ExistingCollection->ActiveStatesChanges.Last().SpanIndex != FrameSpanIndex)
		{
			ExistingCollection->ActiveStatesChanges.Push({FrameSpanIndex, Events.Num()});
		}
		else
		{
			// Multiple events for change of active states in the same frame, keep the last one until we implement scrubbing within a frame
			ExistingCollection->ActiveStatesChanges.Last().EventIndex = Events.Num();
		}
	}

	// Store event in the collection
	Events.Emplace(Event);

	return /*bKeepProcessing*/true;
}

const UE::StateTreeDebugger::FInstanceEventCollection& FStateTreeDebugger::GetEventCollection(FStateTreeInstanceDebugId InstanceId) const\
{
	using namespace UE::StateTreeDebugger;
	const FInstanceEventCollection* ExistingCollection = EventCollections.FindByPredicate([InstanceId](const FInstanceEventCollection& Entry)
	{
		return Entry.InstanceId == InstanceId;
	});

	return ExistingCollection != nullptr ? *ExistingCollection : FInstanceEventCollection::Invalid;
}

void FStateTreeDebugger::AddEvents(const double StartTime, const double EndTime, const TraceServices::IFrameProvider& FrameProvider, const IStateTreeTraceProvider& StateTreeTraceProvider)
{
	check(StateTreeAsset.IsValid());
	StateTreeTraceProvider.ReadTimelines(*StateTreeAsset,
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

#undef LOCTEXT_NAMESPACE

#endif // WITH_STATETREE_DEBUGGER
