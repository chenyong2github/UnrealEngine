// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Engine/EngineTypes.h"
#include "Components/MeshComponent.h"
#include "HairStrandsAsset.h"

#include "HairStrandsComponent.generated.h"

/** Component that allows you to specify custom triangle mesh geometry */
UCLASS(hidecategories = (Object, Physics, Activation, "Components|Activation"), editinlinenew, meta = (BlueprintSpawnableComponent), ClassGroup = Rendering)
class HAIRSTRANDSCORE_API UHairStrandsComponent : public UMeshComponent
{
	GENERATED_UCLASS_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HairStrands")
	UHairStrandsAsset* HairStrandsAsset;

	/** Controls the hair density, to reduce or increase hair count during shadow rendering. This allows to increase/decrease the shadowing on hair when the number of strand is not realistic */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HairStrands Rendering", meta = (ClampMin = "0.0001", UIMin = "0.01", UIMax = "10.0"))
	float HairDensity;

	/** Threshold for merging consecutive hair segments when loading asset */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HairStrands Rendering", meta = (ClampMin = "0.01", UIMin = "0.01", UIMax = "1.0"))
	float MergeThreshold;

	/** Enable this asset to be rendered within the offline reference renderer */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Asset, meta = (MultiLine = false))
	bool bUsedForReference;

	/** The instance of the hair strands. */
	FHairStrandsInstance* HairStrandsInstance;

	//~ Begin UActorComponent Interface.
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;
	//~ End UActorComponent Interface.

	//~ Begin USceneComponent Interface.
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	//~ End USceneComponent Interface.

	//~ Begin UPrimitiveComponent Interface.
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	//~ End UPrimitiveComponent Interface.

	//~ Begin UMeshComponent Interface.
	virtual void PostLoad() override;
	virtual int32 GetNumMaterials() const override;
	//~ End UMeshComponent Interface.
};
