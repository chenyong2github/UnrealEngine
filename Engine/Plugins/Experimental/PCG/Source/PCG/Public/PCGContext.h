// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGData.h"
#include "PCGNode.h" // IWYU pragma: keep

#include "PCGContext.generated.h"

class UPCGComponent;
class UPCGSettingsInterface;
struct FPCGGraphCache;

namespace PCGContextHelpers
{
	template<typename SettingsType>
	const SettingsType* GetInputSettings(const UPCGNode* Node, const FPCGDataCollection& InputData)
	{
		if (Node && Node->GetSettings())
		{
			return Cast<SettingsType>(InputData.GetSettings(Node->GetSettings()));
		}
		else
		{
			return InputData.GetSettings<SettingsType>();
		}
	}
}

UENUM()
enum class EPCGExecutionPhase : uint8
{
		NotExecuted = 0,
		PrepareData,
		Execute,
		PostExecute,
		Done
};

USTRUCT(BlueprintType)
struct PCG_API FPCGContext
{
	GENERATED_BODY()

	virtual ~FPCGContext() = default;

	FPCGDataCollection InputData;
	FPCGDataCollection OutputData;
	TWeakObjectPtr<UPCGComponent> SourceComponent = nullptr;
	int32 NumAvailableTasks = 0;

	// TODO: add RNG source
	// TODO: replace this by a better identification mechanism
	const UPCGNode* Node = nullptr;
	FPCGTaskId TaskId = InvalidPCGTaskId;
	FPCGTaskId CompiledTaskId = InvalidPCGTaskId;
	bool bIsPaused = false;

	EPCGExecutionPhase CurrentPhase = EPCGExecutionPhase::NotExecuted;
	int32 BypassedOutputCount = 0;

	double EndTime = 0.0;
	bool bIsRunningOnMainThread = true;

	/** True if currently inside a PCGAsync scope - will prevent further async processing */
	bool bIsRunningAsyncCall = false;

	const UPCGSettingsInterface* GetInputSettingsInterface() const;

	template<typename SettingsType>
	const SettingsType* GetInputSettings() const
	{
		return PCGContextHelpers::GetInputSettings<SettingsType>(Node, InputData);
	}

	FString GetTaskName() const;
	FString GetComponentName() const;
	bool ShouldStop() const;
};
