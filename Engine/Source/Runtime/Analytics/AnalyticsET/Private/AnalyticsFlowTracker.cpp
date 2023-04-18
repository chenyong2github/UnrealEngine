// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnalyticsFlowTracker.h"

#if UE_BUILD_DEVELOPMENT || UE_BUILD_DEBUG
UE_DISABLE_OPTIMIZATION
#endif

void FAnalyticsFlowTracker::SetProvider(TSharedPtr<IAnalyticsProviderET>& InProvider)
{
	AnalyticsProvider = InProvider;
}

void FAnalyticsFlowTracker::StartSession()
{
}


void FAnalyticsFlowTracker::EndSession()
{
	FScopeLock ScopeLock(&CriticalSection);

	// End all the open flows from the stack
	while (FlowDataStack.Num())
	{
		EndFlowInternal(FlowDataStack.Last());
	}

	ensure(FlowDataRegistry.IsEmpty());
	ensure(FlowGuidRegistry.IsEmpty());

	AnalyticsProvider.Reset();
}

FGuid FAnalyticsFlowTracker::StartFlow(const FName& NewFlowName)
{
	FScopeLock ScopeLock(&CriticalSection);
	TRACE_BOOKMARK(TEXT("STARTFLOW: %s"), *NewFlowName.ToString());

	// Create a new Guid for this flow, can we assume it is unique?
	FGuid NewFlowGuid = FGuid::NewGuid();
	ensureMsgf(FlowDataRegistry.Find(NewFlowGuid) == nullptr, TEXT("Could not generate a unique flow guid."));

	FFlowData FlowData;

	FlowData.StartTime = FDateTime::UtcNow();
	FlowData.FlowName = NewFlowName;
	FlowData.FlowGuid = NewFlowGuid;

	// Register the name and guid pair
	FlowGuidRegistry.Add(NewFlowName, NewFlowGuid);
	FlowDataRegistry.Add(NewFlowGuid, FlowData);
	FlowDataStack.Add(NewFlowGuid);

	return NewFlowGuid;
}

FGuid FAnalyticsFlowTracker::StartFlowStep(const FName& NewFlowStepName)
{
	FScopeLock ScopeLock(&CriticalSection);
	
	if (FlowDataStack.Num()>0)
	{
		return StartFlowStepInternal(NewFlowStepName, FlowDataStack.Last(0));
	}

	return FGuid();
}

FGuid FAnalyticsFlowTracker::StartFlowStep(const FName& NewFlowStepName, const FGuid& FlowGuid)
{
	FScopeLock ScopeLock(&CriticalSection);
	return StartFlowStepInternal(NewFlowStepName, FlowGuid);
}

FGuid FAnalyticsFlowTracker::StartFlowStepInternal(const FName& NewFlowStepName, const FGuid& FlowGuid)
{
	FFlowData* FlowData = FlowDataRegistry.Find(FlowGuid);

	if (ensureMsgf(FlowData, TEXT("FlowStep started outside of a valid flow scope")))
	{
		TRACE_BOOKMARK(TEXT("STARTFlowStep: %s"), *NewFlowStepName.ToString());

		// Create a new Guid for this FlowStep, can we assume it is unique?
		FGuid NewFlowStepGuid = FGuid::NewGuid();
		ensureMsgf(FlowStepDataRegistry.Find(NewFlowStepGuid) == nullptr, TEXT("Could not generate a unique FlowStep guid."));

		// Register the name and guid pair
		FlowStepGuidRegistry.Add(NewFlowStepName, NewFlowStepGuid);

		FFlowStepData NewFlowStep;

		NewFlowStep.FlowStepGuid = NewFlowStepGuid;
		NewFlowStep.FlowStepName = NewFlowStepName;
		NewFlowStep.StartTime = FDateTime::UtcNow();
		NewFlowStep.EndTime = 0;
		NewFlowStep.ScopeDepth = FlowData->FlowStepDataStack.Num();

		// Add FlowStep to Flow
		NewFlowStep.FlowGuid = FlowData->FlowGuid;
		NewFlowStep.FlowName = FlowData->FlowName;
		FlowData->FlowStepDataArray.Add(NewFlowStepGuid);
		FlowData->FlowStepDataStack.Add(NewFlowStepGuid);

		// Register the name and guid pair
		FlowStepGuidRegistry.Add(NewFlowStepName, NewFlowStepGuid);
		FlowStepDataRegistry.Add(NewFlowStepGuid, NewFlowStep);

		return NewFlowStepGuid;
	}

	return FGuid();
}

void FAnalyticsFlowTracker::EndFlowStepInternal(const FGuid& FlowStepGuid, bool bSuccess, const TArray<FAnalyticsEventAttribute>& AdditionalAttributes)
{
	if (FlowStepGuid.IsValid() == false)
		return;

	FFlowStepData* FlowStepData = FlowStepDataRegistry.Find(FlowStepGuid);

	if (ensureMsgf(FlowStepData, TEXT("FlowStep does not exist.")))
	{
		const FGuid FlowGuid = FlowStepData->FlowGuid;

		if (FlowStepData->TimeInSeconds == 0)
		{
			// Don't record again if it has already ended
			FlowStepData->EndTime = FDateTime::UtcNow();
			FlowStepData->bSuccess = bSuccess;

			TRACE_BOOKMARK(TEXT("ENDFlowStep: %s"), *FlowStepData->FlowStepName.ToString());

			const FTimespan TimeTaken = FlowStepData->EndTime - FlowStepData->StartTime;
			FlowStepData->TimeInSeconds = TimeTaken.GetTotalSeconds();
			FlowStepData->AdditionalEventAttributes = AdditionalAttributes;

			TArray<FAnalyticsEventAttribute> EventAttributes = AdditionalAttributes;

			EventAttributes.Add(FAnalyticsEventAttribute(TEXT("SchemaVersion"), FlowStepSchemaVersion));
			EventAttributes.Add(FAnalyticsEventAttribute(TEXT("FlowStepGUID"), *FlowStepData->FlowStepGuid.ToString()));
			EventAttributes.Add(FAnalyticsEventAttribute(TEXT("FlowStepName"), *FlowStepData->FlowStepName.ToString()));
			EventAttributes.Add(FAnalyticsEventAttribute(TEXT("FlowGUID"), FlowStepData->FlowGuid.ToString()));
			EventAttributes.Add(FAnalyticsEventAttribute(TEXT("FlowName"), *FlowStepData->FlowName.ToString()));
			EventAttributes.Add(FAnalyticsEventAttribute(TEXT("StartUTC"), FlowStepData->StartTime.ToUnixTimestampDecimal()));
			EventAttributes.Add(FAnalyticsEventAttribute(TEXT("TimeInSec"), FlowStepData->TimeInSeconds));
			EventAttributes.Add(FAnalyticsEventAttribute(TEXT("Success"), FlowStepData->bSuccess));

			if (AnalyticsProvider.IsValid())
			{
				AnalyticsProvider->RecordEvent(FlowStepEventName, EventAttributes);
			}

			FFlowData* FlowData = FlowDataRegistry.Find(FlowGuid);

			if (ensureMsgf(FlowData, TEXT("A sub flow does not belong to a valid flow.")))
			{
				// Most likely it will be the ending item
				for (int32 index = FlowData->FlowStepDataStack.Num() - 1; index >= 0; index--)
				{
					if (FlowData->FlowStepDataStack[index] == FlowStepGuid)
					{
						// Remove the sub flow from the stack
						FlowData->FlowStepDataStack.RemoveAt(index);
						break;
					}
				}
			}
		}
	}
}

void FAnalyticsFlowTracker::EndFlowStep(const FGuid& FlowStepGuid, bool bSuccess, const TArray<FAnalyticsEventAttribute>& AdditionalAttributes)
{
	FScopeLock ScopeLock(&CriticalSection);
	if (FlowStepGuid.IsValid())
	{
		EndFlowStepInternal(FlowStepGuid, bSuccess, AdditionalAttributes);
	}
}

void FAnalyticsFlowTracker::EndFlowStep(const FName& FlowStepName, bool bSuccess, const TArray<FAnalyticsEventAttribute>& AdditionalAttributes)
{
	FScopeLock ScopeLock(&CriticalSection);
	FGuid* FlowStepGuid = FlowGuidRegistry.Find(FlowStepName);
	if (FlowStepGuid)
	{
		EndFlowStepInternal(*FlowStepGuid, bSuccess, AdditionalAttributes);
	}
}

static void AggregateAttributes(TArray<FAnalyticsEventAttribute>& AggregatedAttibutes, const TArray<FAnalyticsEventAttribute>& Attributes)
{
	// Aggregates all attributes
	for (const FAnalyticsEventAttribute& Attribute : Attributes)
	{
		bool AttributeWasFound=false;

		for (FAnalyticsEventAttribute& AggregatedAttribute : AggregatedAttibutes )
		{
			if (Attribute.GetName() == AggregatedAttribute.GetName())
			{
				AggregatedAttribute += Attribute;
				
				// If we already have this attribute then great no more to do for this attribute
				AttributeWasFound = true;
				break;
			}
		}

		if (AttributeWasFound == false)
		{
			// No matching attribute so append
			AggregatedAttibutes.Add(Attribute);
		}	
	}
}

void FAnalyticsFlowTracker::EndFlow(const FName& FlowName, bool bSuccess, const TArray<FAnalyticsEventAttribute>& AdditionalAttributes)
{
	FScopeLock ScopeLock(&CriticalSection);
	if (FGuid* FlowGuid = FlowGuidRegistry.Find(FlowName))
	{
		EndFlowInternal(*FlowGuid, bSuccess, AdditionalAttributes);
	}
}

void FAnalyticsFlowTracker::EndFlow(bool bSuccess, const TArray<FAnalyticsEventAttribute>& AdditionalAttributes)
{
	FScopeLock ScopeLock(&CriticalSection);

	if (FlowDataStack.IsEmpty() == false)
	{
		EndFlowInternal(FlowDataStack.Last(0), bSuccess, AdditionalAttributes);
	}
}

void FAnalyticsFlowTracker::EndFlow(const FGuid& FlowGuid, bool bSuccess, const TArray<FAnalyticsEventAttribute>& AdditionalAttributes)
{
	FScopeLock ScopeLock(&CriticalSection);
	EndFlowInternal(FlowGuid, bSuccess, AdditionalAttributes);
}

void FAnalyticsFlowTracker::EndFlowInternal(const FGuid& FlowGuid, bool bSuccess, const TArray<FAnalyticsEventAttribute>& AdditionalAttributes)
{
	if (FlowGuid.IsValid() == false)
		return;

	FFlowData* FlowData = FlowDataRegistry.Find(FlowGuid);

	if (ensureMsgf(FlowData, TEXT("There is no valid flow")))
	{
		FlowData->EndTime = FDateTime::UtcNow();
		const FTimespan WallTime = FlowData->EndTime - FlowData->StartTime;
		FlowData->TimeInSeconds = WallTime.GetTotalSeconds();

		TRACE_BOOKMARK(TEXT("ENDFLOW: %s"), *FlowData->FlowName.ToString());

		TArray<FAnalyticsEventAttribute> EventAttributes = AdditionalAttributes;
		EventAttributes.Add(FAnalyticsEventAttribute(TEXT("SchemaVersion"), FlowSchemaVersion));
		EventAttributes.Add(FAnalyticsEventAttribute(TEXT("FlowGUID"), FlowData->FlowGuid.ToString()));
		EventAttributes.Add(FAnalyticsEventAttribute(TEXT("FlowName"), FlowData->FlowName.ToString()));
		EventAttributes.Add(FAnalyticsEventAttribute(TEXT("StartUTC"), FlowData->StartTime.ToUnixTimestampDecimal()));
		EventAttributes.Add(FAnalyticsEventAttribute(TEXT("Success"), bSuccess));
		EventAttributes.Add(FAnalyticsEventAttribute(TEXT("WallTimeInSec"), FlowData->TimeInSeconds));

		double TotalTimeInSeconds = 0;

		for (FGuid FlowStepGuid : FlowData->FlowStepDataArray)
		{
			EndFlowStepInternal(FlowStepGuid);

			FFlowStepData* FlowStepData = FlowStepDataRegistry.Find(FlowStepGuid);

			if (ensureMsgf(FlowStepData, TEXT("FlowStep does not exist.")))
			{
				// Aggregate the additional attributes from the sub flows
				AggregateAttributes(EventAttributes, FlowStepData->AdditionalEventAttributes);

				TotalTimeInSeconds += FlowStepData->TimeInSeconds;
				EventAttributes.Add(FAnalyticsEventAttribute(FlowStepData->FlowStepName.ToString(), FlowStepData->TimeInSeconds));
			}
		}

		EventAttributes.Add(FAnalyticsEventAttribute(TEXT("TotalTimeInSec"), TotalTimeInSeconds));

		if (AnalyticsProvider.IsValid())
		{
			AnalyticsProvider->RecordEvent(FlowEventName, EventAttributes);
		}

		// Clear up our data
		for (FGuid FlowStepGuid : FlowData->FlowStepDataArray)
		{
			FFlowStepData* FlowStepData = FlowStepDataRegistry.Find(FlowStepGuid);

			if (ensureMsgf(FlowStepData, TEXT("FlowStep does not exist.")))
			{
				// Remove the FlowStep and guid from the registry
				FlowStepDataRegistry.Remove(FlowStepGuid);
				FlowStepGuidRegistry.Remove(FlowStepData->FlowStepName);
			}
		}

		// Remove the flow and guid from the registry
		FlowDataRegistry.Remove(FlowData->FlowGuid);
		FlowGuidRegistry.Remove(FlowData->FlowName);

		// Remove the FlowData from the stack
		for (int32 index = FlowDataStack.Num() - 1; index >= 0; index--)
		{
			if (FlowDataStack[index] == FlowGuid)
			{
				// Remove the flow from the stack
				FlowDataStack.RemoveAt(index);
				break;
			}
		}
	}	
}

#if UE_BUILD_DEVELOPMENT || UE_BUILD_DEBUG
UE_ENABLE_OPTIMIZATION
#endif
