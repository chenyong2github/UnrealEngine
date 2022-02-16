// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"
#include "PCGData.h"
#include "PCGNode.h"
#include "PCGSubsystem.h"

struct FPCGContext;
class UPCGComponent;
struct FPCGGraphCache;

typedef TSharedPtr<FPCGContext, ESPMode::ThreadSafe> FPCGContextPtr;

struct FPCGContext
{
	virtual ~FPCGContext() = default;

	FPCGDataCollection InputData;
	FPCGDataCollection OutputData;
	UPCGComponent* SourceComponent = nullptr;
	FPCGGraphCache* Cache = nullptr;
	// TODO: add RNG source
	// TODO: replace this by a better identification mechanism
	const UPCGNode* Node = nullptr;
	FPCGTaskId TaskId = InvalidTaskId;
	bool bIsPaused = false;

	template<typename SettingsType>
	const SettingsType* GetInputSettings()
	{
		if (Node && Node->DefaultSettings)
		{
			return Cast<SettingsType>(InputData.GetSettings(Node->DefaultSettings));
		}
		else
		{
			return InputData.GetSettings<SettingsType>();
		}
	}
};
