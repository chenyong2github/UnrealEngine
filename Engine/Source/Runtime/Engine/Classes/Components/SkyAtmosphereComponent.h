// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Info.h"
#include "Misc/Guid.h"
#include "RenderResource.h"

#include "SkyAtmosphereComponent.generated.h"


USTRUCT(BlueprintType)
struct FTentDistribution
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, interp, Category = "Tent", meta = (UIMin = 0.0, UIMax = 60.0))
	float TipAltitude;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, interp, Category = "Tent", meta = (UIMin = 0.0, UIMax = 1.0, ClampMin = 0.0, SliderExponent = 4.0))
	float TipValue;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, interp, Category = "Tent", meta = (UIMin = 0.01, UIMax = 20.0, ClampMin = 0.0))
	float Width;
};


/**
 * 
 */
UCLASS(ClassGroup = Rendering, collapsecategories, hidecategories = (Object, Mobility, Activation, "Components|Activation"), editinlinenew, meta = (BlueprintSpawnableComponent), MinimalAPI)
class USkyAtmosphereComponent : public USceneComponent
{
	GENERATED_UCLASS_BODY()

	~USkyAtmosphereComponent();



	/** The planet radius. (kilometers from the center to the ground level). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, interp, Category = "Planet", meta = (DisplayName = "Ground Radius", UIMin = 6000.0, UIMax = 7000.0, ClampMin = 100.0, ClampMax = 10000.0))
	float BottomRadius;

	/** The ground albedo that will tint the astmophere when the sun light will bounce on it. Only taken into account when MultiScattering>0.0. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, interp, Category = "Planet", meta = (HideAlphaChannel))
	FColor GroundAlbedo;



	/** The planet radius. (kilometers from the center to the ground level). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, interp, Category = "Atmosphere", meta = (UIMin = 10.0, UIMax = 200.0, ClampMin = 10.0))
	float AtmosphereHeight;

	/** Render multi scattering as if sun light would bounce around in the atmosphere. This is achieved using a dual scattering approach. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, interp, Category = "Atmosphere", meta = (DisplayName = "MultiScattering", UIMin = 0.0, UIMax = 1.0, ClampMin = 0.0, ClampMax = 2.0))
	float MultiScatteringFactor;
	


	/** Rayleigh scattering coefficient scale.*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, interp, Category = "Atmosphere - Raleigh", meta = (UIMin = 0.0, UIMax = 2.0, ClampMin = 0.0, SliderExponent = 4.0))
	float RayleighScatteringScale;

	/** The Rayleigh scattering coefficients resulting from molecules in the air at an altitude of 0 kilometer. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, interp, Category = "Atmosphere - Raleigh", meta=(HideAlphaChannel))
	FColor RayleighScattering;

	/** The altitude in kilometer at which Rayleigh scattering effect is reduced to 40%.*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, interp, Category = "Atmosphere - Raleigh", meta = (UIMin = 0.01, UIMax = 20.0, ClampMin = 0.1, SliderExponent = 5.0))
	float RayleighExponentialDistribution;



	/** Mie scattering coefficient scale.*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, interp, Category = "Atmosphere - Mie", meta = (UIMin = 0.0, UIMax = 5.0, ClampMin = 0.0, SliderExponent = 4.0))
	float MieScatteringScale;

	/** The Mie scattering coefficients resulting from particles in the air at an altitude of 0 kilometer. As it becomes higher, light will be scattered more. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, interp, Category = "Atmosphere - Mie", meta = (HideAlphaChannel))
	FColor MieScattering;

	/** Mie absorption coefficient scale.*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, interp, Category = "Atmosphere - Mie", meta = (UIMin = 0.0, UIMax = 5.0, ClampMin = 0.0, SliderExponent = 4.0))
	float MieAbsorptionScale;

	/** The Mie absorption coefficients resulting from particles in the air at an altitude of 0 kilometer. As it becomes higher, light will be absorbed more. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, interp, Category = "Atmosphere - Mie", meta = (HideAlphaChannel))
	FColor MieAbsorption;
	
	/** A value of 0 mean light is uniformly scattered. A value closer to 1 means lights will scatter more forward, resulting in halos around light sources. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, interp, Category = "Atmosphere - Mie", meta = (UIMin = 0.0, UIMax = 0.999, ClampMin = 0.0, ClampMax = 0.999))
	float MieAnisotropy;

	/** The altitude in kilometer at which Mie effects are reduced to 40%.*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, interp, Category = "Atmosphere - Mie", meta = (UIMin = 0.01, UIMax = 10.0, ClampMin = 0.1, SliderExponent = 5.0))
	float MieExponentialDistribution;



	/** Absorption coefficients for another atmosphere layer. Density increase from 0 to 1 between 10 to 25km and decreases from 1 to 0 between 25 to 40km. This approximates ozone molecules distribution in the Earth atmosphere. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, interp, Category = "Atmosphere - Absorption", meta = (DisplayName = "Absorption Scale", UIMin = 0.0, UIMax = 0.2, ClampMin = 0.0, SliderExponent = 3.0))
	float OtherAbsorptionScale;

	/** Absorption coefficients for another atmosphere layer. Density increase from 0 to 1 between 10 to 25km and decreases from 1 to 0 between 25 to 40km. The default values represents ozone molecules absorption in the Earth atmosphere. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, interp, Category = "Atmosphere - Absorption", meta = (DisplayName = "Absorption", HideAlphaChannel))
	FColor OtherAbsorption;

	/** Represents the altitude based tent distribution of absorption particles in the atmosphere. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Atmosphere - Absorption", meta = (DisplayName = "Tent Distribution"))
	FTentDistribution OtherTentDistribution;

	

	/** Scales the luminance of pixels representing the sky, i.e. not belonging to any surface. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, interp, Category = "Art direction", meta = (HideAlphaChannel))
	FLinearColor SkyLuminanceFactor;

	/** Makes the aerial perspective look thicker by scaling distances from view to surfaces (opaque and translucent). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, interp, Category = "Art direction", meta = (UIMin = 0.0, UIMax = 3.0, ClampMin = 0.0, SliderExponent = 2.0))
	float AerialPespectiveViewDistanceScale;



	UFUNCTION(BlueprintCallable, Category = "Rendering")
	void OverrideAtmosphereLightDirection(int32 AtmosphereLightIndex, const FVector& LightDirection);



protected:
	//~ Begin UActorComponent Interface.
	virtual void CreateRenderState_Concurrent() override;
	//virtual void SendRenderTransform_Concurrent() override;
	virtual void DestroyRenderState_Concurrent() override;
	//~ End UActorComponent Interface.

public:

	//~ Begin UObject Interface. 
//	virtual void PostLoad() override;
//	virtual bool IsPostLoadThreadSafe() const override;
//	virtual void BeginDestroy() override;
	virtual void PostInterpChange(UProperty* PropertyThatChanged) override;
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

	FGuid GetStaticLightingBuiltGuid() const { return bStaticLightingBuiltGUID; }

private:

	/** Add this component to the render scene */
	void AddToRenderScene() const;


	/**
	 * GUID used to associate a atmospheric component with precomputed lighting/shadowing information across levels.
	 * The GUID changes whenever the atmospheric properties change, e.g. LUTs.
	 */
	UPROPERTY()
	FGuid bStaticLightingBuiltGUID;
	/**
	 * Validate static lighting GUIDs and update as appropriate.
	 */
	void ValidateStaticLightingGUIDs();
	/**
	 * Update static lighting GUIDs.
	 */
	void UpdateStaticLightingGUIDs();
};


/**
 *	A placeable actor that simulates sky and light scattering in the atmosphere.
 *	@see TODO address to the documentation.
 */
UCLASS(showcategories = (Movement, Rendering, "Utilities|Transformation", "Input|MouseInput", "Input|TouchInput"), ClassGroup = Fog, hidecategories = (Info, Object, Input), MinimalAPI)
class ASkyAtmosphere : public AInfo
{
	GENERATED_UCLASS_BODY()

private:

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = Atmosphere, meta = (AllowPrivateAccess = "true"))
	class USkyAtmosphereComponent* SkyAtmosphereComponent;

#if WITH_EDITORONLY_DATA
	/** Arrow component to indicate default sun rotation */
	UPROPERTY()
	class UArrowComponent* ArrowComponent;
#endif

public:

};
