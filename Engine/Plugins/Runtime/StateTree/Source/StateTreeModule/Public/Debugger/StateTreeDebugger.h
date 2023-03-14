// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_STATETREE_DEBUGGER

#include "IStateTreeTraceProvider.h"
#include "StateTreeTypes.h"
#include "StateTree.h"
#include "Tickable.h"
#include "TraceServices/Model/Diagnostics.h"
#include "TraceServices/Model/AnalysisSession.h"
#include "TraceServices/Model/Frames.h"

namespace UE::Trace
{
	class FStoreClient;
}

class IStateTreeModule;
class UStateTreeState;
class UStateTree;

namespace UE::StateTreeDebugger
{
	struct FFrameIndexSpan
	{
		FFrameIndexSpan() = default;
		FFrameIndexSpan(const uint64 FrameIdx, const uint32 EventIdx)
			: FrameIdx(FrameIdx)
			, EventIdx(EventIdx)
		{
		}

		/** Frame index in the analysis session */
		uint64 FrameIdx = INDEX_NONE;

		/** Index of the first event for that Frame index */
		int32 EventIdx = INDEX_NONE;
	};
}

struct STATETREEMODULE_API FStateTreeDebuggerInstanceDesc
{
	FStateTreeDebuggerInstanceDesc() = default;
	FStateTreeDebuggerInstanceDesc(const UStateTree* InStateTree, const FStateTreeInstanceDebugId InId, const FString& InName);

	bool IsValid() const;

	bool operator==(const FStateTreeDebuggerInstanceDesc& Other) const
	{
		return StateTree == Other.StateTree
			&& Id == Other.Id;
	}

	bool operator!=(const FStateTreeDebuggerInstanceDesc& Other) const
	{
		return !(*this == Other);
	}

	friend FString LexToString(const FStateTreeDebuggerInstanceDesc& InstanceDesc)
	{
		return FString::Printf(TEXT("%s | %s | %s"),
			*GetNameSafe(InstanceDesc.StateTree.Get()),
			*LexToString(InstanceDesc.Id),
			*InstanceDesc.Name);
	}

	TWeakObjectPtr<const UStateTree> StateTree = nullptr;
	FString Name;
	FStateTreeInstanceDebugId Id = FStateTreeInstanceDebugId::Invalid;
};


DECLARE_DELEGATE_TwoParams(FOnStateTreeDebuggerTracesUpdated, TConstArrayView<FStateTreeTraceEventVariantType>, TConstArrayView<UE::StateTreeDebugger::FFrameIndexSpan>);
DECLARE_DELEGATE_TwoParams(FOnStateTreeDebuggerBreakpointHit, FStateTreeInstanceDebugId InstanceId, FStateTreeStateHandle StateHandle);
DECLARE_DELEGATE_OneParam(FOnStateTreeDebuggerBreakpointsChanged, TConstArrayView<FStateTreeStateHandle> Breakpoints);
DECLARE_DELEGATE_OneParam(FOnStateTreeDebuggerActiveStatesChanges, TConstArrayView<FStateTreeStateHandle> ActiveStates);

struct STATETREEMODULE_API FStateTreeDebugger : FTickableGameObject
{
	struct FTraceDescriptor
	{
		FTraceDescriptor() = default;
		FTraceDescriptor(const FString& Name, const uint32 Id) : Name(Name), TraceId(Id) {}
		
		bool operator==(const FTraceDescriptor& Other) const { return Other.TraceId == TraceId; }
		bool operator!=(const FTraceDescriptor& Other) const { return !(Other == *this); }
		bool IsValid() const { return TraceId != INDEX_NONE; }

		FString Name;
		uint32 TraceId = INDEX_NONE;

		TraceServices::FSessionInfo SessionInfo;
	};
	
	FStateTreeDebugger();
	virtual ~FStateTreeDebugger() override;

	/** Stops reading traces every frame to preserve current state */
	void Pause();

	/** Resumes reading traces every frame */
	void Unpause();

	/** Forces a single refresh to latest state. Useful when simulation is paused. */
	void SyncToCurrentSessionDuration();
	
	bool CanStepBack() const;
	bool CanStepForward() const;
	void StepBack();
	void StepForward();
	
	bool IsAnalysisSessionActive() const { return GetAnalysisSession() != nullptr; }

	void GetActivateInstances(TArray<FStateTreeDebuggerInstanceDesc>& OutInstances) const;
	const FStateTreeDebuggerInstanceDesc& GetDebuggedInstance() const;
	void SetDebuggedInstance(const FStateTreeDebuggerInstanceDesc& SelectedInstance);
	FString GetDebuggedInstanceDescription() const;
	static FString DescribeTrace(const FTraceDescriptor& TraceDescriptor);
	static FString DescribeInstance(const FStateTreeDebuggerInstanceDesc& StateTreeInstanceDesc);

	void GetLiveTraces(TArray<FTraceDescriptor>& OutTraceDescriptors) const;
	void StartLastLiveSessionAnalysis();
	void StartSessionAnalysis(const FTraceDescriptor& TraceDescriptor);
	FTraceDescriptor GetDebuggedTraceDescriptor() const { return ActiveSessionTraceDescriptor; }
	FString GetDebuggedTraceDescription() const;
	
	void ToggleBreakpoints(const TConstArrayView<FStateTreeStateHandle> SelectedStates);

	FOnStateTreeDebuggerTracesUpdated OnTracesUpdated;
	FOnStateTreeDebuggerBreakpointHit OnBreakpointHit;
	FOnStateTreeDebuggerBreakpointsChanged OnBreakpointsChanged;
	FOnStateTreeDebuggerActiveStatesChanges OnActiveStatesChanged;

protected:
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual bool IsTickableWhenPaused() const override { return true; }
	virtual bool IsTickableInEditor() const override { return true; }
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(FStateTreeDebugger, STATGROUP_Tickables); }
	
private:
	void ResetAnalysisData();
	
	void ReadTrace(double ScrubTime);
	void ReadTrace(uint64 FrameIndex);
	void ReadTrace(
		const TraceServices::IAnalysisSession& Session,
		const TraceServices::IFrameProvider& FrameProvider,
		const TraceServices::FFrame& Frame
		);
	
	void StopAnalysis();

	void SetCurrentScrubTime(double RecordingDuration);	
	void SetActiveStates(const TConstArrayView<FStateTreeStateHandle> NewActiveStates);
	
	const TraceServices::IAnalysisSession* GetAnalysisSession() const;
	UE::Trace::FStoreClient* GetStoreClient() const;

	bool ProcessEvent(const FStateTreeInstanceDebugId InstanceId, const TraceServices::FFrame& Frame, const FStateTreeTraceEventVariantType& Event);
	void AddEvents(float StartTime, float EndTime, const TraceServices::IFrameProvider& FrameProvider, const IStateTreeTraceProvider& StateTreeTraceProvider);
	void UpdateMetadata(FTraceDescriptor& TraceDescriptor) const;

	IStateTreeModule& StateTreeModule;
	
	/** The trace analysis session. */
	TSharedPtr<const TraceServices::IAnalysisSession> AnalysisSession;
	
	FTraceDescriptor ActiveSessionTraceDescriptor;

	FStateTreeDebuggerInstanceDesc DebuggedInstance;
	
	TArray<FStateTreeStateHandle> StatesWithBreakpoint;
	TArray<FStateTreeStateHandle> ActiveStates;
	TArray<FStateTreeTraceEventVariantType> Events;
	
	FStateTreeInstanceDebugId HitBreakpointInstanceId = FStateTreeInstanceDebugId::Invalid;
	int32 HitBreakpointStateIndex = INDEX_NONE;

	inline static constexpr double UnsetTime = -1;
	double RecordingDuration = UnsetTime;
	double CurrentScrubTime = UnsetTime;
	double LastTraceReadTime = UnsetTime;

	uint64 CurrentFrameIndex = INDEX_NONE;
	TArray<UE::StateTreeDebugger::FFrameIndexSpan> FramesWithEvents;

	bool bProcessingInitialEvents = false;
	bool bPaused = false;
};

#endif // WITH_STATETREE_DEBUGGER
