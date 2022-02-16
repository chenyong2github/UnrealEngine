// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGElement.h"
#include "PCGContext.h"
#include "PCGSettings.h"
#include "Elements/PCGDebugElement.h"
#include "Graph/PCGGraphCache.h"

bool IPCGElement::Execute(FPCGContextPtr Context) const
{
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
				if (TaggedData.Usage != EPCGDataUsage::Input)
				{
					FilteredTaggedData.Add(TaggedData);
				}
				else if (TaggedData.Tags.Intersect(Settings->FilterOnTags).IsEmpty())
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

		if (IsCacheable(Settings) && Context->Cache && Context->Cache->GetFromCache(this, Context->InputData, Settings, Context->OutputData))
		{
			bDone = true;
		}
		else
		{
			bDone = ExecuteInternal(Context);

			if (bDone && IsCacheable(Settings) && Context->Cache)
			{
				Context->Cache->StoreInCache(this, Context->InputData, Settings, Context->OutputData);
			}
		}

		/** TODO - Placeholder feature */
		if (bDone && Settings && !Settings->TagsAppliedOnOutput.IsEmpty())
		{
			for (FPCGTaggedData& TaggedData : Context->OutputData.TaggedData)
			{
				if (TaggedData.Usage == EPCGDataUsage::Input)
				{
					if (!BypassedTaggedData.Contains(TaggedData))
					{
						TaggedData.Tags.Append(Settings->TagsAppliedOnOutput);
					}
				}
			}
		}

#if WITH_EDITOR
		if (bDone && Settings && (Settings->ExecutionMode == EPCGSettingsExecutionMode::Debug || Settings->ExecutionMode == EPCGSettingsExecutionMode::Isolated))
		{
			PCGDebugElement::ExecuteDebugDisplay(Context);

			// Null out the output if this node is executed in isolation
			if (Settings->ExecutionMode == EPCGSettingsExecutionMode::Isolated)
			{
				Context->OutputData.bCancelExecution = true;
			}
		}
#endif

		return bDone;
	}
}

FPCGContextPtr FSimplePCGElement::Initialize(const FPCGDataCollection& InputData, UPCGComponent* SourceComponent)
{
	FPCGContextPtr Context = MakeShared<FPCGContext>();
	Context->InputData = InputData;
	Context->SourceComponent = SourceComponent;

	return Context;
}