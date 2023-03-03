// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/MeshComponent.h"
#include "EngineDefines.h"
#include "GameFramework/Info.h"
#include "Misc/Guid.h"
#include "RenderResource.h"

#include "HeterogeneousVolumeComponent.generated.h"

/**
 * A component that represents a heterogeneous volume.
 */
UCLASS(Blueprintable, ClassGroup = (Rendering, Common), hidecategories = (Object, Activation, "Components|Activation"), ShowCategories = (Mobility), editinlinenew, meta = (BlueprintSpawnableComponent))
class UHeterogeneousVolumeComponent : public UMeshComponent
{
	GENERATED_UCLASS_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Volume)
	FIntVector VolumeResolution;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Volume)
	float MinimumVoxelSize;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Volume)
	uint32 bAnimate : 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Lighting)
	float LightingDownsampleFactor;

	~UHeterogeneousVolumeComponent() {}

public:
	//~ Begin USceneComponent Interface.
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	//~ End USceneComponent Interface.

	//~ Begin UPrimitiveComponent Interface.
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual int32 GetNumMaterials() const override { return 1; }
	virtual bool ShouldRenderSelected() const override { return true; }
	//~ End UPrimitiveComponent Interface.

private:
	float Time;
	float Framerate;
};

/**
 * A placeable actor that represents a heterogeneous volume.
 */
UCLASS(showcategories = (Movement, Rendering, Transformation, DataLayers, "Input|MouseInput", "Input|TouchInput"), ClassGroup = Fog, hidecategories = (Info, Object, Input), MinimalAPI)
class AHeterogeneousVolume : public AInfo
{
	GENERATED_UCLASS_BODY()

private:

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = Volume, meta = (AllowPrivateAccess = "true"))
	TObjectPtr<class UHeterogeneousVolumeComponent> HeterogeneousVolumeComponent;

#if WITH_EDITOR
	virtual bool ActorTypeSupportsDataLayer() const override { return true; }
#endif

};