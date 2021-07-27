// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ComponentAssetBroker.h"

class FOptimusComputeComponentBroker : public IComponentAssetBroker
{
public:
	~FOptimusComputeComponentBroker() override 
	{} 
	
	// IComponentAssetBroker overrides
	UClass* GetSupportedAssetClass() override;
	bool AssignAssetToComponent(UActorComponent* InComponent, UObject* InAsset) override;
	UObject* GetAssetFromComponent(UActorComponent* InComponent) override;
	
};
