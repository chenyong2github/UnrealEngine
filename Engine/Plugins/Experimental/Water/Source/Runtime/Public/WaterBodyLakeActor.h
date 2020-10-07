// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "WaterBodyActor.h"
#include "WaterBodyLakeActor.generated.h"

class UBoxComponent;
class ULakeCollisionComponent;

// ----------------------------------------------------------------------------------

UCLASS(MinimalAPI)
class ULakeGenerator : public UWaterBodyGenerator
{
	GENERATED_UCLASS_BODY()

public:
	virtual void Reset() override;
	virtual void OnUpdateBody(bool bWithExclusionVolumes) override;
	virtual void PostLoad() override;
	virtual TArray<UPrimitiveComponent*> GetCollisionComponents() const override;

private:
	UPROPERTY(NonPIEDuplicateTransient)
	UStaticMeshComponent* LakeMeshComp;

	UPROPERTY()
	UBoxComponent* LakeCollisionComp_DEPRECATED;

	UPROPERTY(NonPIEDuplicateTransient)
	ULakeCollisionComponent* LakeCollision;
};

// ----------------------------------------------------------------------------------

UCLASS(Blueprintable)
class WATER_API AWaterBodyLake : public AWaterBody
{
	GENERATED_UCLASS_BODY()

public:
	/** AWaterBody Interface */
	virtual EWaterBodyType GetWaterBodyType() const override { return EWaterBodyType::Lake; }
	virtual TArray<UPrimitiveComponent*> GetCollisionComponents() const override;

protected:
	/** AWaterBody Interface */
	virtual void InitializeBody() override;
	virtual bool IsBodyInitialized() const override { return !!LakeGenerator; }
	virtual void UpdateWaterBody(bool bWithExclusionVolumes) override;

	UPROPERTY(NonPIEDuplicateTransient)
	ULakeGenerator* LakeGenerator;
};