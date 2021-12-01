// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "CoreMinimal.h"
#include "MLAdapterManager.h"
#include "MLAdapterInferenceManager.generated.h"

/**
 * Inference manager is only doing inference so we won't startup the RPC server.
 */
UCLASS()
class MLADAPTERINFERENCE_API UMLAdapterInferenceManager : public UMLAdapterManager
{
	GENERATED_BODY()
public:
	virtual void OnPostWorldInit(UWorld* World, const UWorld::InitializationValues) override;
};
