// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGElement.h"
#include "PCGContext.h"
#include "PCGModule.h"
#include "PCGSettings.h"
#include "Elements/PCGDebugElement.h"
#include "Elements/PCGSelfPruning.h"
#include "Graph/PCGGraphCache.h"

bool IPCGElement::Execute(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(IPCGElement::Execute);
	// Early out to stop execution
	if (Context->InputData.bCancelExecution)
	{
		Context->OutputData.bCancelExecution = true;

		if (IsCancellable())
		{
			return true;
		}
	}

	const UPCGSettings* Settings = Context->GetInputSettings<UPCGSettings>();
	if (Settings && Settings->ExecutionMode == EPCGSettingsExecutionMode::Disabled)
	{
		//Pass-through
		Context->OutputData = Context->InputData;
		CleanupAndValidateOutput(Context);
		return true;
	}
	else
	{
		/** TODO - Placeholder feature */
		TArray<FPCGTaggedData> BypassedTaggedData;
		if (Settings && !Settings->FilterOnTags.IsEmpty())
		{
			// Move any of the inputs that don't have the tags to the outputs as a pass-through
			// NOTE: this breaks a bit the ordering of inputs, however, there's no obvious way around it
			TArray<FPCGTaggedData> FilteredTaggedData;
			for (FPCGTaggedData& TaggedData : Context->InputData.TaggedData)
			{
				if (TaggedData.Tags.Intersect(Settings->FilterOnTags).IsEmpty())
				{
					if (Settings->bPassThroughFilteredOutInputs)
					{
						Context->OutputData.TaggedData.Add(TaggedData);
						BypassedTaggedData.Add(TaggedData);
					}
				}
				else // input has the required tags
				{
					FilteredTaggedData.Add(TaggedData);
				}
			}

			Context->InputData.TaggedData = FilteredTaggedData;
		}

		bool bDone = false;

#if WITH_EDITOR
		if (ShouldLog())
		{
			UE_LOG(LogPCG, Verbose, TEXT("---------------------------------------"));
		}
#endif

#if WITH_EDITOR
		const double StartTime = FPlatformTime::Seconds();
#endif

		bDone = ExecuteInternal(Context);

#if WITH_EDITOR
		const double EndTime = FPlatformTime::Seconds();
		const double ElapsedTime = EndTime - StartTime;
		Context->ElapsedTime += ElapsedTime;
		Context->ExecutionCount++;

		// TODO: Is hardcoded, should be configurable
		constexpr int MaxNumberOfTrackedTimers = 100;
		{
			FScopeLock Lock(&TimersLock);
			if (Timers.Num() < MaxNumberOfTrackedTimers)
			{
				Timers.Add(ElapsedTime);
			}
			else
			{
				Timers[CurrentTimerIndex] = ElapsedTime;
			}
			CurrentTimerIndex = (CurrentTimerIndex + 1) % MaxNumberOfTrackedTimers;
		}
#endif

		if (bDone)
		{
			CleanupAndValidateOutput(Context);

#if WITH_EDITOR
			PCGE_LOG(Log, "Executed in (%f)s and (%d) call(s)", Context->ElapsedTime, Context->ExecutionCount);
#else
			PCGE_LOG(Log, "Executed");
#endif
		}

		/** TODO - Placeholder feature */
		if (bDone && Settings && !Settings->TagsAppliedOnOutput.IsEmpty())
		{
			for (FPCGTaggedData& TaggedData : Context->OutputData.TaggedData)
			{
				if (!BypassedTaggedData.Contains(TaggedData))
				{
					TaggedData.Tags.Append(Settings->TagsAppliedOnOutput);
				}
			}
		}

#if WITH_EDITOR
		if (bDone && Settings)
		{
			if (Settings->DebugSettings.bCheckForDuplicates)
			{
				FPCGDataCollection ElementInputs = Context->InputData;
				FPCGDataCollection ElementOutputs = Context->OutputData;

				Context->InputData = ElementOutputs;
				Context->OutputData = FPCGDataCollection();

				PCGE_LOG(Verbose, "Performing remove duplicate points test (perf warning)");
				PCGSelfPruningElement::Execute(Context, EPCGSelfPruningType::RemoveDuplicates, 0.0f, false);

				Context->InputData = ElementInputs;
				Context->OutputData = ElementOutputs;
			}
		}
#endif

		return bDone;
	}
}

#if WITH_EDITOR
void IPCGElement::DebugDisplay(FPCGContext* Context) const
{
	const UPCGSettings* Settings = Context->GetInputSettings<UPCGSettings>();
	if (Settings && (Settings->ExecutionMode == EPCGSettingsExecutionMode::Debug || Settings->ExecutionMode == EPCGSettingsExecutionMode::Isolated))
	{
		FPCGDataCollection ElementInputs = Context->InputData;
		FPCGDataCollection ElementOutputs = Context->OutputData;

		Context->InputData = ElementOutputs;
		Context->OutputData = FPCGDataCollection();

		PCGDebugElement::ExecuteDebugDisplay(Context);

		Context->InputData = ElementInputs;
		Context->OutputData = ElementOutputs;

		// Null out the output if this node is executed in isolation
		if (Settings->ExecutionMode == EPCGSettingsExecutionMode::Isolated)
		{
			Context->OutputData.bCancelExecution = true;
		}
	}
}

void IPCGElement::ResetTimers()
{
	FScopeLock Lock(&TimersLock);
	Timers.Empty();
	CurrentTimerIndex = 0;

}
#endif // WITH_EDITOR

void IPCGElement::CleanupAndValidateOutput(FPCGContext* Context) const
{
	check(Context);
	const UPCGSettings* Settings = Context->GetInputSettings<UPCGSettings>();

	if (!IsPassthrough() && Settings)
	{
		// Cleanup any residual labels if the node isn't supposed to produce them
		// TODO: this is a bit of a crutch, could be refactored out if we review the way we push tagged data
		TArray<FPCGPinProperties> OutputPinProperties = Settings->OutputPinProperties();
		if(OutputPinProperties.Num() == 1)
		{
			for (FPCGTaggedData& TaggedData : Context->OutputData.TaggedData)
			{
				TaggedData.Pin = OutputPinProperties[0].Label;
			}
		}

		// Validate all out data for errors in labels
#if WITH_EDITOR
		if (Settings->ExecutionMode != EPCGSettingsExecutionMode::Disabled)
		{
			for (FPCGTaggedData& TaggedData : Context->OutputData.TaggedData)
			{
				int32 MatchIndex = OutputPinProperties.IndexOfByPredicate([&TaggedData](const FPCGPinProperties& InProp) { return TaggedData.Pin == InProp.Label; });
				if (MatchIndex == INDEX_NONE)
				{
					PCGE_LOG(Warning, "Output generated for pin %s but cannot be routed", *TaggedData.Pin.ToString());
				}
				// TODO: Temporary fix for Settings directly from InputData (ie. from elements with code and not PCG nodes)
				else if(TaggedData.Data && !(OutputPinProperties[MatchIndex].AllowedTypes & TaggedData.Data->GetDataType()) && TaggedData.Data->GetDataType() != EPCGDataType::Settings)
				{
					PCGE_LOG(Warning, "Output generated for pin %s does not have a compatible type: %s", *TaggedData.Pin.ToString(), *UEnum::GetValueAsString(TaggedData.Data->GetDataType()));
				}
			}
		}
#endif
	}
}

FPCGContext* FSimplePCGElement::Initialize(const FPCGDataCollection& InputData, UPCGComponent* SourceComponent, const UPCGNode* Node)
{
	FPCGContext* Context = new FPCGContext();
	Context->InputData = InputData;
	Context->SourceComponent = SourceComponent;
	Context->Node = Node;

	return Context;
}