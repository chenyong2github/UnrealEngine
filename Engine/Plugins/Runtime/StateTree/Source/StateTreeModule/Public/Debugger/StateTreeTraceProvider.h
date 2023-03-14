// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_STATETREE_DEBUGGER

#include "IStateTreeTraceProvider.h"
#include "Model/PointTimeline.h"
#include "StateTreeTypes.h" // required to compile TMap<FStateTreeInstanceDebugId, ...>

namespace TraceServices { class IAnalysisSession; }
enum class EStateTreeTraceInstanceEventType : uint8;
class UStateTree;

class FStateTreeTraceProvider : public IStateTreeTraceProvider
{
public:
	static FName ProviderName;

	explicit FStateTreeTraceProvider(TraceServices::IAnalysisSession& InSession);
	
	void AppendEvent(FStateTreeInstanceDebugId InInstanceId, double InTime, const FStateTreeTraceEventVariantType& Event);
	void AppendInstance(
		const UStateTree* InStateTree,
		const FStateTreeInstanceDebugId InInstanceId,
		const TCHAR* InInstanceName,
		const EStateTreeTraceInstanceEventType EventType);

protected:
	/** IStateTreeDebuggerProvider interface */
	virtual void GetActivateInstances(TArray<FStateTreeDebuggerInstanceDesc>& OutInstances) const override;
	virtual bool ReadTimelines(const FStateTreeInstanceDebugId InstanceId, TFunctionRef<void(const FStateTreeInstanceDebugId ProcessedInstanceId, const FEventsTimeline&)> Callback) const override;

private:
	TraceServices::IAnalysisSession& Session;
	TArray<FStateTreeDebuggerInstanceDesc> ActiveInstances;
	TMap<FStateTreeInstanceDebugId, uint32> InstanceIdToDebuggerEntryTimelines;
	TArray<TSharedRef<TraceServices::TPointTimeline<FStateTreeTraceEventVariantType>>> EventsTimelines;
};
#endif // WITH_STATETREE_DEBUGGER