// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/NameTypes.h"
#include "UObject/UnrealNames.h"
#include "IAnalyticsProviderET.h"

class FAnalyticsFlowTracker : FNoncopyable
{
public:
	FAnalyticsFlowTracker() {};
	~FAnalyticsFlowTracker() {};

	/** Sets the analytics provider for the flow tracker. */
	ANALYTICSET_API void SetProvider(TSharedPtr<IAnalyticsProviderET>& AnalyticsProvider);

	/** Begins a new flow tracking session. Will emit Flow and FlowStep events to the specified analytics provider */
	ANALYTICSET_API void StartSession();

	/** Ends all open Flows and FlowSteps*/
	ANALYTICSET_API void EndSession();
	
	/** Start a new Flow, the existing flow context will be pushed onto a stack and the new flow will become the current context*/
	ANALYTICSET_API FGuid StartFlow(const FName& FlowName);

	/** End the flow for the current context and pop the stack*/
	ANALYTICSET_API void EndFlow(bool bSuccess = true, const TArray<FAnalyticsEventAttribute>& AdditionalAttributes = {});

	/** End an existing flow by name */
	ANALYTICSET_API void EndFlow(const FName& FlowName, bool bSuccess = true, const TArray<FAnalyticsEventAttribute>& AdditionalAttributes = {});

	/** End an existing flow by GUID */
	ANALYTICSET_API void EndFlow(const FGuid& FlowGuid, bool bSuccess = true, const TArray<FAnalyticsEventAttribute>& AdditionalAttributes = {});

	/** Start a new flow and add it to the current flow context */
	ANALYTICSET_API FGuid StartFlowStep(const FName& FlowStepName);

	/** Start a new flow step and add it to a specific flow context by GUID */
	ANALYTICSET_API FGuid StartFlowStep(const FName& FlowStepName, const FGuid& FlowGuid);

	/** End an existing flow step by name */
	ANALYTICSET_API void EndFlowStep(const FName& FlowStepName, bool bSuccess = true, const TArray<FAnalyticsEventAttribute>& AdditionalAttributes = {});

	/** End an existing flow step by GUID */
	ANALYTICSET_API void EndFlowStep(const FGuid& FlowStepGuid, bool bSuccess = true, const TArray<FAnalyticsEventAttribute>& AdditionalAttributes = {});

private:

	struct FFlowStepData
	{
		FName		FlowName;
		FGuid		FlowGuid = FGuid();
		FName		FlowStepName;
		FGuid		FlowStepGuid;
		FDateTime	StartTime = 0;
		FDateTime	EndTime = 0;
		double		TimeInSeconds = 0;
		bool		bSuccess = false;
		int32		ScopeDepth;
		TArray<FAnalyticsEventAttribute> AdditionalEventAttributes;
	};


	struct FFlowData
	{
		FName						FlowName = TEXT("None");
		FGuid						FlowGuid = FGuid();
		FDateTime					StartTime = 0;
		FDateTime					EndTime = 0;
		double						TimeInSeconds = 0;
		TArray<FGuid>				FlowStepDataArray;
		TArray<FGuid>				FlowStepDataStack;
	};

	void EndFlowInternal(const FGuid& FlowGuid, bool bSuccess = true, const TArray<FAnalyticsEventAttribute>& AdditionalAttributes = {});

	FGuid StartFlowStepInternal(const FName& NewFlowStepName, const FGuid& FlowGuid);
	void EndFlowStepInternal(const FGuid& FlowStepGuid, bool bSuccess = true, const TArray<FAnalyticsEventAttribute>& AdditionalAttributes = {});

	TMap<FName, FGuid>			FlowGuidRegistry;
	TMap<FGuid, FFlowData>		FlowDataRegistry;
	TMap<FName, FGuid>			FlowStepGuidRegistry;
	TMap<FGuid, FFlowStepData>	FlowStepDataRegistry;
	TArray<FGuid>				FlowDataStack;
	FCriticalSection			CriticalSection;
	TSharedPtr<IAnalyticsProviderET> AnalyticsProvider;

	const uint32 FlowSchemaVersion = 4;
	const FString FlowEventName = TEXT("Iteration.Flow");

	const uint32 FlowStepSchemaVersion = 4;
	const FString FlowStepEventName = TEXT("Iteration.FlowStep");
};


