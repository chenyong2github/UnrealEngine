// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "Components/ActorComponent.h"

#include "ComputeGraphComponent.generated.h"

class UComputeGraph;


UCLASS(Blueprintable, meta = (BlueprintSpawnableComponent))
class UComputeGraphComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UComputeGraphComponent();

	UPROPERTY(EditAnywhere, Category = "Compute")
	TObjectPtr<UComputeGraph> ComputeGraph = nullptr;

	UFUNCTION(BlueprintCallable, Category = "Compute")
	void QueueExecute();

protected:
	//~ Begin UActorComponent Interface
	bool ShouldCreateRenderState() const override
	{
		return true;
	}

	void SendRenderDynamicData_Concurrent() override;
	//~ End UActorComponent Interface
};
