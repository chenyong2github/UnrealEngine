// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DisplayClusterSceneComponent.h"
#include "DisplayClusterMeshComponent.generated.h"

class UStaticMeshComponent;


/**
 * Mesh component
 */
UCLASS( ClassGroup=(Custom), Blueprintable, meta = (BlueprintSpawnableComponent) )
class DISPLAYCLUSTER_API UDisplayClusterMeshComponent
	: public UDisplayClusterSceneComponent
{
	GENERATED_BODY()

public:
	UDisplayClusterMeshComponent(const FObjectInitializer& ObjectInitializer);

public:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

public:
	UStaticMeshComponent* GetMesh() const
	{
		return Mesh;
	}

	void SetMesh(UStaticMeshComponent* InMesh)
	{
		Mesh = InMesh;
	}

protected:
	UPROPERTY(BlueprintReadOnly, Category = "DisplayCluster")
	FString AssetPath;

	UPROPERTY(EditAnywhere, Category = "DisplayCluster")
	UStaticMeshComponent* Mesh;
};
