// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "CoreMinimal.h"
#include "Managers/MLAdapterManager.h"
#include "MLAdapterNoRPCManager.generated.h"

/**
 * No RPC manager won't start RPC server and will immediately start a session and spawn the default agent.
 */
UCLASS()
class MLADAPTER_API UMLAdapterNoRPCManager : public UMLAdapterManager
{
	GENERATED_BODY()
public:
	virtual void OnPostWorldInit(UWorld* World, const UWorld::InitializationValues) override;
};
