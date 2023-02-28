// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"
#include "ClusterUnionActor.generated.h"

class UClusterUnionComponent;

/**
 * A lightweight actor that can be used to own a cluster union component.
 */
UCLASS()
class ENGINE_API AClusterUnionActor : public AActor
{
	GENERATED_BODY()
public:
	AClusterUnionActor(const FObjectInitializer& ObjectInitializer);

	UFUNCTION()
	UClusterUnionComponent* GetClusterUnionComponent() const { return ClusterUnion; }

protected:
	/** The pivot used while building. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cluster Union")
	TObjectPtr<UClusterUnionComponent> ClusterUnion;
};