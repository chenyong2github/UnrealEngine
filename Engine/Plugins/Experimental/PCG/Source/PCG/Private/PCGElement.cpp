// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGElement.h"

#include "PCGComponent.h"
#include "PCGContext.h"
#include "PCGModule.h"
#include "PCGParamData.h"
#include "PCGSettings.h"
#include "Data/PCGPointData.h"
#include "Elements/PCGDebugElement.h"
#include "Elements/PCGSelfPruning.h"
#include "Graph/PCGGraphCache.h"
#include "HAL/IConsoleManager.h"

#include "Algo/Find.h"

static TAutoConsoleVariable<bool> CVarPCGValidatePointMetadata(
	TEXT("pcg.debug.ValidatePointMetadata"),
	true,
	TEXT("Controls whether we validate that the metadata entry keys on the output point data are consistent"));

bool IPCGElement::Execute(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(IPCGElement::Execute);
	check(Context && Context->NumAvailableTasks > 0 && Context->CurrentPhase < EPCGExecutionPhase::Done);
	check(Context->bIsRunningOnMainThread || !CanExecuteOnlyOnMainThread(Context));

	while (Context->CurrentPhase != EPCGExecutionPhase::Done)
	{
		bool bExecutionPostponed = false;

		switch (Context->CurrentPhase)
		{
			case EPCGExecutionPhase::NotExecuted: // Fall-through
			{
				PreExecute(Context);
				break;
			}

			case EPCGExecutionPhase::PrepareData:
			{
				FScopedCall ScopedCall(*this, Context);
				if (PrepareDataInternal(Context))
				{
					Context->CurrentPhase = EPCGExecutionPhase::Execute;
				}
				else
				{
					bExecutionPostponed = true;
				}
				break;
			}

			case EPCGExecutionPhase::Execute:
			{
				FScopedCall ScopedCall(*this, Context);
				if (ExecuteInternal(Context))
				{
					Context->CurrentPhase = EPCGExecutionPhase::PostExecute;
				}
				else
				{
					bExecutionPostponed = true;
				}
				break;
			}

			case EPCGExecutionPhase::PostExecute:
			{
				PostExecute(Context);
				break;
			}

			default: // should not happen
			{
				check(0);
				break;
			}
		}

		if (bExecutionPostponed || 
			Context->ShouldStop() ||
			(!Context->bIsRunningOnMainThread && CanExecuteOnlyOnMainThread(Context))) // phase change might require access to main thread
		{
			break;
		}
	}

	return Context->CurrentPhase == EPCGExecutionPhase::Done;
}

void IPCGElement::PreExecute(FPCGContext* Context) const
{
	// Check for early outs (task cancelled + node disabled)
	// Early out to stop execution
	if (Context->InputData.bCancelExecution || (!Context->SourceComponent.IsExplicitlyNull() && !Context->SourceComponent.IsValid()))
	{
		Context->OutputData.bCancelExecution = true;

		if (IsCancellable())
		{
			// Skip task completely
			Context->CurrentPhase = EPCGExecutionPhase::Done;
			return;
		}
	}

	// Prepare to move to prepare data phase
	Context->CurrentPhase = EPCGExecutionPhase::PrepareData;

	const UPCGSettingsInterface* SettingsInterface = Context->GetInputSettingsInterface();
	const UPCGSettings* Settings = SettingsInterface ? SettingsInterface->GetSettings() : nullptr;

	if (!SettingsInterface || !Settings)
	{
		return;
	}

	if (!SettingsInterface->bEnabled)
	{
		//Pass-through - no execution
		DisabledPassThroughData(Context);

		Context->CurrentPhase = EPCGExecutionPhase::PostExecute;
	}
	else
	{
		// Perform input filtering
		/** TODO - Placeholder feature */
		if (!Settings->FilterOnTags.IsEmpty())
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
					}
				}
				else // input has the required tags
				{
					FilteredTaggedData.Add(TaggedData);
				}
			}

			Context->InputData.TaggedData = MoveTemp(FilteredTaggedData);
			Context->BypassedOutputCount = Context->OutputData.TaggedData.Num();
		}
	}
}

bool IPCGElement::PrepareDataInternal(FPCGContext* Context) const
{
	return true;
}

void IPCGElement::PostExecute(FPCGContext* Context) const
{
	// Cleanup and validate output
	CleanupAndValidateOutput(Context);

#if WITH_EDITOR
	{
		FScopeLock Lock(&CapturedDataLock);
		PCGE_LOG(Verbose, "Executed in (%f)s and (%d) frames(s)", Timers[CurrentTimerIndex].ExecutionTime, Timers[CurrentTimerIndex].ExecutionFrameCount);
	}
#endif

	const UPCGSettingsInterface* SettingsInterface = Context->GetInputSettingsInterface();
	const UPCGSettings* Settings = SettingsInterface ? SettingsInterface->GetSettings() : nullptr;

	// Apply tags on output
	/** TODO - Placeholder feature */
	if (Settings && !Settings->TagsAppliedOnOutput.IsEmpty())
	{
		for (int32 TaggedDataIdx = Context->BypassedOutputCount; TaggedDataIdx < Context->OutputData.TaggedData.Num(); ++TaggedDataIdx)
		{
			Context->OutputData.TaggedData[TaggedDataIdx].Tags.Append(Settings->TagsAppliedOnOutput);
		}
	}

	// Additional debug things (check for duplicates),
#if WITH_EDITOR
	if (SettingsInterface && SettingsInterface->DebugSettings.bCheckForDuplicates)
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
#endif

	Context->CurrentPhase = EPCGExecutionPhase::Done;
}

void IPCGElement::DisabledPassThroughData(FPCGContext* Context) const
{
	check(Context);

	const UPCGSettings* Settings = Context->GetInputSettings<UPCGSettings>();
	check(Settings);

	if (!Context->Node)
	{
		// Full pass-through if we don't have a node
		Context->OutputData = Context->InputData;
		return;
	}

	if (Context->Node->GetInputPins().Num() == 0 || Context->Node->GetOutputPins().Num() == 0)
	{
		// No input pins or not output pins, return nothing
		return;
	}

	const UPCGPin* PassThroughPin = Context->Node->GetPassThroughInputPin();
	if (PassThroughPin == nullptr)
	{
		// No pin to grab pass through data from
		return;
	}

	// Grab data from pass-through pin
	Context->OutputData.TaggedData = Context->InputData.GetInputsByPin(PassThroughPin->Properties.Label);

	const EPCGDataType OutputType = Context->Node->GetOutputPins()[0]->Properties.AllowedTypes;

	// Pass through input data if it is not params, and if the output type supports it (e.g. if we have a incoming
	// surface connected to an input pin of type Any, do not pass the surface through to an output pin of type Point).
	auto InputDataShouldPassThrough = [OutputType](const FPCGTaggedData& InData)
	{
		EPCGDataType InputType = InData.Data ? InData.Data->GetDataType() : EPCGDataType::None;
		const bool bInputTypeNotWiderThanOutputType = !(InputType & ~OutputType);

		// Right now we allow edges from Spatial to Concrete. This can happen for example if a point processing node
		// is receving a Spatial data, and the node is disabled, it will want to pass the Spatial data through. In the
		// future we will force collapses/conversions. For now, allow an incoming Spatial to pass out through a Concrete.
		// TODO remove!
		const bool bAllowSpatialToConcrete = !!(InputType & EPCGDataType::Spatial) && !!(OutputType & EPCGDataType::Concrete);

		return InputType != EPCGDataType::Param && (bInputTypeNotWiderThanOutputType || bAllowSpatialToConcrete);
	};

	// Now remove any non-params edges, and if only one edge should come through, remove the others
	if (Settings->OnlyPassThroughOneEdgeWhenDisabled())
	{
		// Find first incoming non-params data that is coming through the pass through pin
		TArray<FPCGTaggedData> InputsOnFirstPin = Context->InputData.GetInputsByPin(PassThroughPin->Properties.Label);
		const int FirstNonParamsDataIndex = InputsOnFirstPin.IndexOfByPredicate(InputDataShouldPassThrough);

		if (FirstNonParamsDataIndex != INDEX_NONE)
		{
			// Remove everything except the data we found above
			for (int Index = Context->OutputData.TaggedData.Num() - 1; Index >= 0; --Index)
			{
				if (Index != FirstNonParamsDataIndex)
				{
					Context->OutputData.TaggedData.RemoveAt(Index);
				}
			}
		}
		else
		{
			// No data found to return
			Context->OutputData.TaggedData.Empty();
		}
	}
	else
	{
		// Remove any incoming non-params data that is coming through the pass through pin
		TArray<FPCGTaggedData> InputsOnFirstPin = Context->InputData.GetInputsByPin(PassThroughPin->Properties.Label);
		for (int Index = InputsOnFirstPin.Num() - 1; Index >= 0; --Index)
		{
			const FPCGTaggedData& Data = InputsOnFirstPin[Index];

			if (!InputDataShouldPassThrough(Data))
			{
				Context->OutputData.TaggedData.RemoveAt(Index);
			}
		}
	}
}

#if WITH_EDITOR
void IPCGElement::DebugDisplay(FPCGContext* Context) const
{
	const UPCGSettingsInterface* SettingsInterface = Context->GetInputSettingsInterface();
	if (SettingsInterface && SettingsInterface->bDebug)
	{
		FPCGDataCollection ElementInputs = Context->InputData;
		FPCGDataCollection ElementOutputs = Context->OutputData;

		Context->InputData = ElementOutputs;
		Context->OutputData = FPCGDataCollection();

		PCGDebugElement::ExecuteDebugDisplay(Context);

		Context->InputData = ElementInputs;
		Context->OutputData = ElementOutputs;
	}
}


TArray<IPCGElement::FCallTime> IPCGElement::GetTimers() const
{
	FScopeLock Lock(&CapturedDataLock);
	return Timers;
}

TArray<IPCGElement::FCapturedMessage> IPCGElement::GetCapturedMessages() const
{
	FScopeLock Lock(&CapturedDataLock);
	return CapturedMessages;

}

void IPCGElement::ResetTimers()
{
	FScopeLock Lock(&CapturedDataLock);
	Timers.Empty();
	CurrentTimerIndex = 0;
}

void IPCGElement::ResetMessages()
{
	FScopeLock Lock(&CapturedDataLock);
	CapturedMessages.Reset();
}

#endif // WITH_EDITOR

void IPCGElement::CleanupAndValidateOutput(FPCGContext* Context) const
{
	check(Context);
	const UPCGSettingsInterface* SettingsInterface = Context->GetInputSettingsInterface();
	const UPCGSettings* Settings = SettingsInterface ? SettingsInterface->GetSettings() : nullptr;

	// Implementation note - disabled passthrough nodes can happen only in subgraphs/ spawn actor nodes
	// which will behave properly when disabled. 
	if (Settings && !IsPassthrough(Settings))
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
		if (SettingsInterface->bEnabled)
		{
			for (FPCGTaggedData& TaggedData : Context->OutputData.TaggedData)
			{
				const int32 MatchIndex = OutputPinProperties.IndexOfByPredicate([&TaggedData](const FPCGPinProperties& InProp) { return TaggedData.Pin == InProp.Label; });
				if (MatchIndex == INDEX_NONE)
				{
					PCGE_LOG(Warning, "Output generated for pin %s but cannot be routed", *TaggedData.Pin.ToString());
				}
				// TODO: Temporary fix for Settings directly from InputData (ie. from elements with code and not PCG nodes)
				else if(TaggedData.Data && !(OutputPinProperties[MatchIndex].AllowedTypes & TaggedData.Data->GetDataType()) && TaggedData.Data->GetDataType() != EPCGDataType::Settings)
				{
					PCGE_LOG(Warning, "Output generated for pin %s does not have a compatible type: %s", *TaggedData.Pin.ToString(), *UEnum::GetValueAsString(TaggedData.Data->GetDataType()));
				}

				if (CVarPCGValidatePointMetadata.GetValueOnAnyThread())
				{
					if (UPCGPointData* PointData = Cast<UPCGPointData>(TaggedData.Data))
					{
						const TArray<FPCGPoint>& NewPoints = PointData->GetPoints();
						const int32 MaxMetadataEntry = PointData->Metadata ? PointData->Metadata->GetItemCountForChild() : 0;

						bool bHasError = false;

						for(int32 PointIndex = 0; PointIndex < NewPoints.Num() && !bHasError; ++PointIndex)
						{
							bHasError |= (NewPoints[PointIndex].MetadataEntry >= MaxMetadataEntry);
						}

						if (bHasError)
						{
							PCGE_LOG(Warning, "Output generated for pin %s does not have valid point metadata", *TaggedData.Pin.ToString());
						}
					}
				}
			}
		}
#endif
	}
}

bool IPCGElement::IsCacheableInstance(const UPCGSettingsInterface* InSettingsInterface) const
{
	if (InSettingsInterface)
	{
		if (!InSettingsInterface->bEnabled)
		{
			return false;
		}
		else
		{
			return IsCacheable(InSettingsInterface->GetSettings());
		}
	}
	else
	{
		return false;
	}
}

#if WITH_EDITOR
IPCGElement::FScopedCall::FScopedCall(const IPCGElement& InOwner, FPCGContext* InContext)
	: Owner(InOwner)
	, Context(InContext)
	, Phase(InContext->CurrentPhase)
	, ThreadID(FPlatformTLS::GetCurrentThreadId())
{
	StartTime = FPlatformTime::Seconds();

	GLog->AddOutputDevice(this);
}

IPCGElement::FScopedCall::~FScopedCall()
{
	GLog->RemoveOutputDevice(this);

	const double ThisFrameTime = FPlatformTime::Seconds() - StartTime;

	FScopeLock Lock(&Owner.CapturedDataLock);

	constexpr int32 MaxNumberOfTrackedTimers = 100;
	if (Phase == EPCGExecutionPhase::PrepareData)
	{
		if (Owner.Timers.Num() < MaxNumberOfTrackedTimers)
		{
			// first time here
			Owner.Timers.Add({});
			Owner.CurrentTimerIndex = Owner.Timers.Num()-1;
		}
		else
		{
			Owner.CurrentTimerIndex = (Owner.CurrentTimerIndex+1) % MaxNumberOfTrackedTimers;
			Owner.Timers[Owner.CurrentTimerIndex] = FCallTime();
		}

		FCallTime& Time = Owner.Timers[Owner.CurrentTimerIndex];
		Time.PrepareDataTime = ThisFrameTime;
	}
	else if (Phase == EPCGExecutionPhase::Execute)
	{
		FCallTime& Time = Owner.Timers[Owner.CurrentTimerIndex];

		Time.ExecutionTime += ThisFrameTime;
		Time.ExecutionFrameCount++;

		Time.MaxExecutionFrameTime = FMath::Max(Time.MaxExecutionFrameTime, ThisFrameTime);
		Time.MinExecutionFrameTime = FMath::Min(Time.MinExecutionFrameTime, ThisFrameTime);
	}
	else if (Phase == EPCGExecutionPhase::PostExecute)
	{
		FCallTime& Time = Owner.Timers[Owner.CurrentTimerIndex];

		Time.PostExecuteTime = ThisFrameTime;
	}

	Owner.CapturedMessages += std::move(CapturedMessages);
}

void IPCGElement::FScopedCall::Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const class FName& Category)
{
	// TODO: this thread id check will also filter out messages spawned from threads spawned inside of nodes. To improve that,
	// perhaps set at TLS bit on things from here and inside of PCGAsync spawned jobs. If this was done, CapturedMessages below also will
	// need protection
	if (Verbosity > ELogVerbosity::Warning || FPlatformTLS::GetCurrentThreadId() != ThreadID)
	{
		// ignore
		return;
	}

	// this is a dumb counter just so messages can be sorted in a similar order as when they were logged
	static volatile int32 MessageCounter = 0;

	CapturedMessages.Add(FCapturedMessage { MessageCounter++, Category, V, Verbosity});
}

#endif // WITH_EDITOR

FPCGContext* FSimplePCGElement::Initialize(const FPCGDataCollection& InputData, TWeakObjectPtr<UPCGComponent> SourceComponent, const UPCGNode* Node)
{
	FPCGContext* Context = new FPCGContext();
	Context->InputData = InputData;
	Context->SourceComponent = SourceComponent;
	Context->Node = Node;

	return Context;
}