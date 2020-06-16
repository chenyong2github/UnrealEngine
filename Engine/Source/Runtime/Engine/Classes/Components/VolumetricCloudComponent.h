// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "EngineDefines.h"
#include "GameFramework/Info.h"
#include "Misc/Guid.h"
#include "RenderResource.h"

#include "VolumetricCloudComponent.generated.h"


class FVolumetricCloudSceneProxy;


/**
 * 
 */
UCLASS(ClassGroup = Rendering, collapsecategories, hidecategories = (Object, Mobility, Activation, "Components|Activation"), editinlinenew, meta = (BlueprintSpawnableComponent), MinimalAPI)
class UVolumetricCloudComponent : public USceneComponent
{
	GENERATED_UCLASS_BODY()

	~UVolumetricCloudComponent();

	/** The altitude at which the cloud layer starts. (kilometers above the ground) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, interp, Category = "Layer", meta = (UIMin = 10.0f, UIMax = 200.0f, ClampMin = 0.0f))
	float LayerBottomAltitude;

	/** The altitude at which the cloud layer ends. (kilometers above the ground) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, interp, Category = "Layer", meta = (UIMin = 0.0f, UIMax = 50.0f, ClampMin = 0.1f))
	float LayerHeight;

	/** The altitude at which the cloud layer ends. (kilometers above the ground) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, interp, Category = "Layer", meta = (UIMin = 100.0f, UIMax = 7000.0f, ClampMin = 100.0f, ClampMax = 10000.0f))
	float PlanetRadius;

	/** The ground albedo used to light the cloud from below with respect to the sun light and sky atmosphere. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, interp, Category = "Planet", meta = (HideAlphaChannel))
	FColor GroundAlbedo;

	/** The material describing the cloud volume. It must be a CloudVolume domain material. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rendering")
	UMaterialInterface* Material;

	/** Scales atmospheric lights contribution when scattering in cloud medium. This can help counter balance the fact that our multiple scattering solution is only an approximation.*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, interp, Category = "Art direction", meta = (HideAlphaChannel))
	FLinearColor AtmosphericLightsContributionFactor;

protected:
	//~ Begin UActorComponent Interface.
	virtual void CreateRenderState_Concurrent(FRegisterComponentContext* Context) override;
	//virtual void SendRenderTransform_Concurrent() override;
	virtual void DestroyRenderState_Concurrent() override;
	//~ End UActorComponent Interface.

public:

	//~ Begin UObject Interface. 
//	virtual void PostLoad() override;
//	virtual bool IsPostLoadThreadSafe() const override;
//	virtual void BeginDestroy() override;
	virtual void PostInterpChange(FProperty* PropertyThatChanged) override;
	virtual void Serialize(FArchive& Ar) override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR
	//~ End UObject Interface

	//~ Begin UActorComponent Interface.
#if WITH_EDITOR
	virtual void CheckForErrors() override;
#endif // WITH_EDITOR
	//~ End UActorComponent Interface.

private:

	FVolumetricCloudSceneProxy* VolumetricCloudSceneProxy;

};


/**
 *	A placeable actor that simulates volumetric cloud in the sky.
 *	@see TODO address to the documentation.
 */
UCLASS(showcategories = (Movement, Rendering, "Utilities|Transformation", "Input|MouseInput", "Input|TouchInput"), ClassGroup = Fog, hidecategories = (Info, Object, Input), MinimalAPI)
class AVolumetricCloud : public AInfo
{
	GENERATED_UCLASS_BODY()

private:

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = Atmosphere, meta = (AllowPrivateAccess = "true"))
	class UVolumetricCloudComponent* VolumetricCloudComponent;

public:

};
