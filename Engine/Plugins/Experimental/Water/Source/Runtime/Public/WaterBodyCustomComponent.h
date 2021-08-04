// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "WaterBodyComponent.h"
#include "WaterBodyCustomComponent.generated.h"

class UStaticMeshComponent;

// ----------------------------------------------------------------------------------

UCLASS(Blueprintable)
class WATER_API UWaterBodyCustomComponent : public UWaterBodyComponent
{
	GENERATED_UCLASS_BODY()
	friend class AWaterBodyCustom;
public:
	/** AWaterBody Interface */
	virtual EWaterBodyType GetWaterBodyType() const override { return EWaterBodyType::Transition; }
	virtual TArray<UPrimitiveComponent*> GetCollisionComponents() const override;
	virtual void UpdateComponentVisibility() override;

protected:
	/** AWaterBody Interface */
	virtual void Reset() override;
	virtual void BeginUpdateWaterBody() override;
	virtual void OnUpdateBody(bool bWithExclusionVolumes) override;

	UPROPERTY(NonPIEDuplicateTransient)
	UStaticMeshComponent* MeshComp;
};