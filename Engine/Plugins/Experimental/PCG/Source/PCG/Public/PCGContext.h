// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"
#include "PCGData.h"
#include "PCGNode.h"

struct FPCGContext;
class UPCGComponent;

typedef TSharedPtr<FPCGContext, ESPMode::ThreadSafe> FPCGContextPtr;

struct FPCGContext
{
	FPCGDataCollection InputData;
	FPCGDataCollection OutputData;
	const UPCGComponent* SourceComponent = nullptr;
	bool bExecutionSuccessful = false;
	// TODO: add RNG source
	// TODO: replace this by a better identification mechanism
	const UPCGNode* Node = nullptr;

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
