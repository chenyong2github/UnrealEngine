// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/ThreadSafeCounter.h"
#include "UObject/ObjectMacros.h"
#include "Components/SceneComponent.h"
#include "Serialization/BulkData.h"
#include "AtmosphericFogComponent.generated.h"

/** Structure storing Data for pre-computation */
USTRUCT(BlueprintType)
struct FAtmospherePrecomputeParameters
{
	GENERATED_USTRUCT_BODY()
	
	/** Rayleigh scattering density height scale, ranges from [0...1] */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=AtmosphereParam)
	float DensityHeight;

	UPROPERTY()
	float DecayHeight_DEPRECATED;

	/** Maximum scattering order */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=AtmosphereParam)
	int32 MaxScatteringOrder;

	/** Transmittance Texture Width */
	UPROPERTY()
	int32 TransmittanceTexWidth;

	/** Transmittance Texture Height */
	UPROPERTY()
	int32 TransmittanceTexHeight;

	/** Irradiance Texture Width */
	UPROPERTY()
	int32 IrradianceTexWidth;

	/** Irradiance Texture Height */
	UPROPERTY()
	int32 IrradianceTexHeight;

	/** Number of different altitudes at which to sample inscatter color (size of 3D texture Z dimension)*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=AtmosphereParam) 
	int32 InscatterAltitudeSampleNum;

	/** Inscatter Texture Height */
	UPROPERTY() 
	int32 InscatterMuNum;

	/** Inscatter Texture Width */
	UPROPERTY()
	int32 InscatterMuSNum;

	/** Inscatter Texture Width */
	UPROPERTY()
	int32 InscatterNuNum;

	FAtmospherePrecomputeParameters();

	bool operator == ( const FAtmospherePrecomputeParameters& Other ) const
	{
		return (DensityHeight == Other.DensityHeight)
			&& (MaxScatteringOrder == Other.MaxScatteringOrder)
			&& (TransmittanceTexWidth == Other.TransmittanceTexWidth)
			&& (TransmittanceTexHeight == Other.TransmittanceTexHeight)
			&& (IrradianceTexWidth == Other.IrradianceTexWidth)
			&& (IrradianceTexHeight == Other.IrradianceTexHeight)
			&& (InscatterAltitudeSampleNum == Other.InscatterAltitudeSampleNum)
			&& (InscatterMuNum == Other.InscatterMuNum)
			&& (InscatterMuSNum == Other.InscatterMuSNum)
			&& (InscatterNuNum == Other.InscatterNuNum);
	}

	bool operator != ( const FAtmospherePrecomputeParameters& Other ) const
	{
		return (DensityHeight != Other.DensityHeight)
			|| (MaxScatteringOrder != Other.MaxScatteringOrder)
			|| (TransmittanceTexWidth != Other.TransmittanceTexWidth)
			|| (TransmittanceTexHeight != Other.TransmittanceTexHeight)
			|| (IrradianceTexWidth != Other.IrradianceTexWidth)
			|| (IrradianceTexHeight != Other.IrradianceTexHeight)
			|| (InscatterAltitudeSampleNum != Other.InscatterAltitudeSampleNum)
			|| (InscatterMuNum != Other.InscatterMuNum)
			|| (InscatterMuSNum != Other.InscatterMuSNum)
			|| (InscatterNuNum != Other.InscatterNuNum);
	}
};

/**
 *	Used to create fogging effects such as clouds.
 */
UCLASS(ClassGroup=Rendering, collapsecategories, hidecategories=(Object, Mobility, Activation, "Components|Activation"), editinlinenew, meta=(BlueprintSpawnableComponent), MinimalAPI)
class UAtmosphericFogComponent : public USceneComponent
{
	GENERATED_UCLASS_BODY()

	~UAtmosphericFogComponent();

	/** Scale the scattered luminance from the atmosphere sun light. Only affect the sky and atmospheric fog. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, interp, Category=Atmosphere)
	float SunMultiplier;

	/** Scale the scattered luminance from the atmosphere sun light only on surfaces, excludes the sky. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, interp, Category=Atmosphere)
	float FogMultiplier;

	/** Scales the atmosphere transmittance over background. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, interp, Category=Atmosphere)
	float DensityMultiplier;

	/** Offset the atmosphere transmittance over background [-1.f ~ 1.f]. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, interp, Category=Atmosphere)
	float DensityOffset;

	/** Scale the view position. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, interp, Category=Atmosphere)
	float DistanceScale;

	/** Scale the view altitude (only Z scale). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, interp, Category=Atmosphere)
	float AltitudeScale;

	/** Apply a distance offset before evaluating the atmospheric fog, in km (to handle large distance). Only on surfaces, excludes the sky. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, interp, Category=Atmosphere)
	float DistanceOffset;

	/** Offset the view altitude (along Z). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, interp, Category=Atmosphere)
	float GroundOffset;

	/** The atmospheric fog start distance in centimeters. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, interp, Category=Atmosphere)
	float StartDistance;

	/** Sun half apex angle in degree, see https://en.wikipedia.org/wiki/Solid_angle */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, interp, Category = Atmosphere)
	float SunDiscScale;

	/** Default atmospheric sun light disc luminance. Used when there is no atmospheric sun light selected in the level. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, interp, Category=Lighting)
	float DefaultBrightness;

	/** Default atmospheric sun light disc color. Used when there is no sunlight placed in the level. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, interp, Category=Lighting)
	FColor DefaultLightColor;

	/** Disable sun disk rendering. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, interp, Category=Lighting, meta=(ScriptName="DisableSunDiskValue"))
	uint32 bDisableSunDisk : 1;

	/** Disable color scattering from ground. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, interp, Category=Lighting, meta=(ScriptName="DisableGroundScatteringValue"))
	uint32 bDisableGroundScattering : 1;

	/** Set brightness of the light */
	UFUNCTION(BlueprintCallable, Category="Rendering|Components|AtmosphericFog")
	ENGINE_API void SetDefaultBrightness(float NewBrightness);

	/** Set color of the light */
	UFUNCTION(BlueprintCallable, Category="Rendering|Components|AtmosphericFog")
	ENGINE_API void SetDefaultLightColor(FLinearColor NewLightColor);

	/** Set SunMultiplier */
	UFUNCTION(BlueprintCallable, Category="Rendering|Components|AtmosphericFog")
	ENGINE_API void SetSunMultiplier(float NewSunMultiplier);

	/** Set FogMultiplier */
	UFUNCTION(BlueprintCallable, Category="Rendering|Components|AtmosphericFog")
	ENGINE_API void SetFogMultiplier(float NewFogMultiplier);

	/** Set DensityMultiplier */
	UFUNCTION(BlueprintCallable, Category="Rendering|Components|AtmosphericFog")
	ENGINE_API void SetDensityMultiplier(float NewDensityMultiplier);

	/** Set DensityOffset */
	UFUNCTION(BlueprintCallable, Category="Rendering|Components|AtmosphericFog")
	ENGINE_API void SetDensityOffset(float NewDensityOffset);

	/** Set DistanceScale */
	UFUNCTION(BlueprintCallable, Category="Rendering|Components|AtmosphericFog")
	ENGINE_API void SetDistanceScale(float NewDistanceScale);

	/** Set AltitudeScale */
	UFUNCTION(BlueprintCallable, Category="Rendering|Components|AtmosphericFog")
	ENGINE_API void SetAltitudeScale(float NewAltitudeScale);

	/** Set StartDistance */
	UFUNCTION(BlueprintCallable, Category="Rendering|Components|AtmosphericFog")
	ENGINE_API void SetStartDistance(float NewStartDistance);

	/** Set DistanceOffset */
	UFUNCTION(BlueprintCallable, Category="Rendering|Components|AtmosphericFog")
	ENGINE_API void SetDistanceOffset(float NewDistanceOffset);

	/** Set DisableSunDisk */
	UFUNCTION(BlueprintCallable, Category="Rendering|Components|AtmosphericFog")
	ENGINE_API void DisableSunDisk(bool NewSunDisk);

	/** Set DisableGroundScattering */
	UFUNCTION(BlueprintCallable, Category="Rendering|Components|AtmosphericFog")
	ENGINE_API void DisableGroundScattering(bool NewGroundScattering);

	/** Set PrecomputeParams, only valid in Editor mode */
	UFUNCTION(BlueprintCallable, Category="Rendering|Components|AtmosphericFog")
	ENGINE_API void SetPrecomputeParams(float DensityHeight, int32 MaxScatteringOrder, int32 InscatterAltitudeSampleNum);

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Atmosphere)
	FAtmospherePrecomputeParameters PrecomputeParams;

public:
	UPROPERTY()
	class UTexture2D* TransmittanceTexture_DEPRECATED;

	UPROPERTY()
	class UTexture2D* IrradianceTexture_DEPRECATED;

	enum EPrecomputeState
	{
		EInvalid = 0,
		EValid = 2,
	};

	// this is mostly a legacy thing, it is only modified by the game thread
	uint32 PrecomputeCounter;
	// When non-zero, the component should flush rendering commands and see if there is any atmosphere stuff to deal with, then decrement it
	mutable FThreadSafeCounter GameThreadServiceRequest;

	UFUNCTION(BlueprintCallable, Category="Rendering|Components|AtmosphericFog")
	void StartPrecompute();	

protected:
	//~ Begin UActorComponent Interface.
	virtual void CreateRenderState_Concurrent() override;
	virtual void SendRenderTransform_Concurrent() override;
	virtual void DestroyRenderState_Concurrent() override;
	//~ End UActorComponent Interface.

	 void AddFogIfNeeded();

public:
	// Stores colored transmittance from outer space to point in atmosphere.
	class FAtmosphereTextureResource* TransmittanceResource;
	// Stores ground illuminance as a function of sun direction and atmosphere radius.
	class FAtmosphereTextureResource* IrradianceResource;
	// Stores in-scattered luminance toward a point according to height and sun direction.
	class FAtmosphereTextureResource* InscatterResource;
	
	/** Source vector data. */
	mutable FByteBulkData TransmittanceData;
	mutable FByteBulkData IrradianceData;
	mutable FByteBulkData InscatterData;
	
	//~ Begin UObject Interface. 
	virtual void PostLoad() override;
	virtual bool IsPostLoadThreadSafe() const override;
	virtual void BeginDestroy() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	void UpdatePrecomputedData();
#endif // WITH_EDITOR
	virtual void PostInterpChange(UProperty* PropertyThatChanged) override;
	virtual void Serialize(FArchive& Ar) override;
	//~ End UObject Interface

	ENGINE_API void InitResource();
	ENGINE_API void ReleaseResource();

	//~ Begin UActorComponent Interface.
	virtual TStructOnScope<FActorComponentInstanceData> GetComponentInstanceData() const override;
	//~ End UActorComponent Interface.

	void ApplyComponentInstanceData(struct FAtmospherePrecomputeInstanceData* ComponentInstanceData);
	const FAtmospherePrecomputeParameters& GetPrecomputeParameters() const { return PrecomputeParams;  }

private:
#if WITH_EDITORONLY_DATA
	class FAtmospherePrecomputeDataHandler* PrecomputeDataHandler;
#endif

	friend class FAtmosphericFogSceneInfo;
};

/** Used to store data during RerunConstructionScripts */
USTRUCT()
struct FAtmospherePrecomputeInstanceData : public FSceneComponentInstanceData
{
	GENERATED_BODY()
public:
	FAtmospherePrecomputeInstanceData() = default;
	FAtmospherePrecomputeInstanceData(const UAtmosphericFogComponent* SourceComponent)
		: FSceneComponentInstanceData(SourceComponent)
	{}

	virtual ~FAtmospherePrecomputeInstanceData() = default;

	virtual bool ContainsData() const override
	{
		return true;
	}

	virtual void ApplyToComponent(UActorComponent* Component, const ECacheApplyPhase CacheApplyPhase) override
	{
		FSceneComponentInstanceData::ApplyToComponent(Component, CacheApplyPhase);
		CastChecked<UAtmosphericFogComponent>(Component)->ApplyComponentInstanceData(this);
	}

	struct FAtmospherePrecomputeParameters PrecomputeParameter;

	FByteBulkData TransmittanceData;
	FByteBulkData IrradianceData;
	FByteBulkData InscatterData;
};
