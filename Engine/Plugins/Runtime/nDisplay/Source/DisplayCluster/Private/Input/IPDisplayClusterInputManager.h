// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Input/IDisplayClusterInputManager.h"
#include "IPDisplayClusterManager.h"


/**
 * Input manager private interface
 */
class IPDisplayClusterInputManager
	: public IDisplayClusterInputManager
	, public IPDisplayClusterManager
{
public:
	virtual ~IPDisplayClusterInputManager()
	{ }

	virtual void Update() = 0;

	virtual void ExportInputData(TMap<FString, FString>& InputData) const = 0;
	virtual void ImportInputData(const TMap<FString, FString>& InputData) = 0;
};
