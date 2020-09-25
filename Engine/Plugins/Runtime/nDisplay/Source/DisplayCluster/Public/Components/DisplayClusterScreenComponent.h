// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DisplayClusterSceneComponent.h"
#include "DisplayClusterScreenComponent.generated.h"

class UStaticMeshComponent;


/**
 * Projection screen component
 */
UCLASS(ClassGroup = (Custom), Blueprintable, meta = (BlueprintSpawnableComponent))
class DISPLAYCLUSTER_API UDisplayClusterScreenComponent
	: public UDisplayClusterSceneComponent
{
	GENERATED_BODY()

public:
	UDisplayClusterScreenComponent(const FObjectInitializer& ObjectInitializer);

public:
	FVector2D GetScreenSize() const
	{
		return Size;
	}

	void SetScreenSize(const FVector2D& InSize)
	{
		Size = InSize;
	}

public:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	UPROPERTY(EditAnywhere, Category = "DisplayCluster")
	FVector2D Size;

	UPROPERTY(VisibleAnywhere, Category = "DisplayCluster")
	UStaticMeshComponent* ScreenGeometryComponent = nullptr;
};
