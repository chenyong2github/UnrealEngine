// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DisplayClusterSceneComponent.h"
#include "DisplayClusterMeshComponent.generated.h"

class UStaticMeshComponent;


/**
 * Mesh component
 */
UCLASS(ClassGroup = (DisplayCluster))
class DISPLAYCLUSTER_API UDisplayClusterMeshComponent
	: public UDisplayClusterSceneComponent
{
	GENERATED_BODY()

public:
	UDisplayClusterMeshComponent(const FObjectInitializer& ObjectInitializer);

public:
	UStaticMeshComponent* GetWarpMesh() const
	{
		return WarpMeshComponent;
	}

	void SetWarpMesh(UStaticMeshComponent* InMeshComponent)
	{
		WarpMeshComponent = InMeshComponent;
	}

protected:
	virtual void ApplyConfigurationData();

protected:
	UPROPERTY(VisibleAnywhere, Category = "DisplayCluster")
	UStaticMeshComponent* WarpMeshComponent = nullptr;

#if WITH_EDITOR 
public:
	virtual void SetNodeSelection(bool bSelect) override;
#endif
};
