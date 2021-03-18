// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "WaterBodyActor.h"
#include "WaterBodyCustomActor.generated.h"

class UStaticMeshComponent;

// ----------------------------------------------------------------------------------

UCLASS(MinimalAPI)
class UCustomMeshGenerator : public UWaterBodyGenerator
{
	GENERATED_UCLASS_BODY()

public:
	virtual void Reset() override;
	virtual void OnUpdateBody(bool bWithExclusionVolumes) override;
	virtual TArray<UPrimitiveComponent*> GetCollisionComponents() const override;
	WATER_API void SetMaterial(UMaterialInterface* Material);

private:
	UPROPERTY(NonPIEDuplicateTransient)
	UStaticMeshComponent* MeshComp;
};

// ----------------------------------------------------------------------------------

UCLASS(Blueprintable)
class WATER_API AWaterBodyCustom : public AWaterBody
{
	GENERATED_UCLASS_BODY()

public:
	/** AWaterBody Interface */
	virtual EWaterBodyType GetWaterBodyType() const override { return EWaterBodyType::Transition; }
	virtual TArray<UPrimitiveComponent*> GetCollisionComponents() const override;

protected:
	/** AWaterBody Interface */
	virtual void InitializeBody() override;
	virtual bool IsBodyInitialized() const override { return !!CustomGenerator; }
	virtual void BeginUpdateWaterBody() override;
	virtual void UpdateWaterBody(bool bWithExclusionVolumes) override;

#if WITH_EDITOR
	virtual bool IsIconVisible() const override;
#endif

	UPROPERTY(NonPIEDuplicateTransient)
	UCustomMeshGenerator* CustomGenerator;
};