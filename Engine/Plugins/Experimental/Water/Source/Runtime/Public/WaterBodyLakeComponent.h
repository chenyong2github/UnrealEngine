// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "WaterBodyComponent.h"
#include "WaterBodyLakeComponent.generated.h"

class UBoxComponent;
class ULakeCollisionComponent;
class UDEPRECATED_LakeGenerator;

// ----------------------------------------------------------------------------------

UCLASS(Blueprintable)
class WATER_API UWaterBodyLakeComponent : public UWaterBodyComponent
{
	GENERATED_UCLASS_BODY()

	friend class AWaterBodyLake;
public:
	/** UWaterBodyComponent Interface */
	virtual EWaterBodyType GetWaterBodyType() const override { return EWaterBodyType::Lake; }
	virtual TArray<UPrimitiveComponent*> GetCollisionComponents() const override;
	virtual TArray<UPrimitiveComponent*> GetStandardRenderableComponents() const override;

protected:
	/** UWaterBodyComponent Interface */
	virtual void Reset() override;
	virtual void OnUpdateBody(bool bWithExclusionVolumes) override;

	UPROPERTY(NonPIEDuplicateTransient)
	UStaticMeshComponent* LakeMeshComp;

	UPROPERTY(NonPIEDuplicateTransient)
	ULakeCollisionComponent* LakeCollision;
};