// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"
#include "IMagicLeapPlugin.h"

class FLuminARModule : public ILuminARModule
{
	/** IModuleInterface implementation */
	virtual void StartupModule() override;

	//create for mutual connection (regardless of construction order)
	virtual TSharedPtr<IARSystemSupport, ESPMode::ThreadSafe> CreateARImplementation() override;
	//Now connect (regardless of connection order)
	virtual void ConnectARImplementationToXRSystem(FXRTrackingSystemBase* InXRTrackingSystem) override;
	//Now initialize fully connected systems
	virtual void InitializeARImplementation() override;

public:
	static TSharedPtr<class FLuminARImplementation, ESPMode::ThreadSafe> GetLuminARSystem();

private:
	TSharedPtr<FLuminARImplementation, ESPMode::ThreadSafe> LuminARImplementation;
	static TWeakPtr<FLuminARImplementation, ESPMode::ThreadSafe> LuminARImplmentationPtr;
};
