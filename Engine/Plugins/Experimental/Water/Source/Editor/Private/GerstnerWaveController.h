// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "GameFramework/Actor.h"
#include "WaterBodyActor.h"

#include "GerstnerWaveController.generated.h"

class UStaticMeshComponent;
class USceneComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UTexture;
class UTextureRenderTarget2D;
class AWaterMeshActor;
class UMaterialParameterCollection;


UENUM()
enum class EWaterQualityLevel : uint8
{
	Low     UMETA(DisplayName = "Low", ToolTip = "6 Waves"),
	Medium  UMETA(DisplayName = "Medium", ToolTip = "9 Waves"),
	High    UMETA(DisplayName = "High", ToolTip = "12 Waves"),
	Epic    UMETA(DisplayName = "Epic", ToolTip = "16 Waves")
};

USTRUCT(BlueprintType)
struct FWaveSpectrumSettings_C
{
	GENERATED_USTRUCT_BODY()

		//Constructor
	FWaveSpectrumSettings_C() :
		MaxWaves(6),
		Seed(0),
		MinWavelength(521.0f),
		MaxWavelength(6000.0f),
		WavelengthFalloff(2.0f),
		MinAmplitude(4.0f),
		MaxAmplitude(80.0f),
		AmplitudeFalloff(2.0f),
		WindDirection(FVector2D(1.0f, 0.0f)),
		DirectionAngularSpread(1325.0f),
		SmallWaveSteepness(0.4f),
		LargeWaveSteepness(0.2f),
		SteepnessFalloff(1.0f)
	{	}

		/** Indices to final section entries of the merged skeletal mesh */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Default")
	int32 MaxWaves;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Default")
	int32 Seed;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "Min Wavelength", Category = "Wavelengths", OverrideNativeName = "Min Wavelength"))
	float MinWavelength;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "Max Wavelength", Category = "Wavelengths", OverrideNativeName = "Max Wavelength"))
	float MaxWavelength;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "Wavelength Falloff", Category = "Wavelengths", OverrideNativeName = "Wavelength Falloff"))
	float WavelengthFalloff;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "Min Amplitude", Category = "Wavelengths", OverrideNativeName = "Min Amplitude"))
	float MinAmplitude;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "Max Amplitude", Category = "Wavelengths", OverrideNativeName = "Max Amplitude"))
	float MaxAmplitude;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "Amplitude Falloff", Category = "Wavelengths", OverrideNativeName = "Amplitude Falloff"))
	float AmplitudeFalloff;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "Dominant Wind Direction", Category = "Directions", OverrideNativeName = "Dominant Wind Direction"))
	FVector2D WindDirection;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "Direction Angular Spread", Category = "Directions", OverrideNativeName = "Direction Angular Spread"))
	float DirectionAngularSpread;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "Small Wave Steepness", Category = "Steepness", OverrideNativeName = "Small Wave Steepness"))
	float SmallWaveSteepness;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "Large Wave Steepness", Category = "Steepness", OverrideNativeName = "Large Wave Steepness"))
	float LargeWaveSteepness;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "Steepness Falloff", Category = "Steepness", OverrideNativeName = "Steepness Falloff"))
	float SteepnessFalloff;
};



USTRUCT(BlueprintType)
struct FOverrideScalarParam_C
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "ParamName", Category = "Default", OverrideNativeName = "ParamName"))
	FName ParamName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "Value", Category = "Default", OverrideNativeName = "Value"))
	float Value;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "Override", Category = "Default", OverrideNativeName = "Override"))
	bool Override;

};

USTRUCT(BlueprintType)
struct FOverrideVectorParam_C
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "ParamName", Category = "Default", OverrideNativeName = "ParamName"))
	FName ParamName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "Value", Category = "Default", OverrideNativeName = "Value"))
	FVector Value;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "Override", Category = "Default", OverrideNativeName = "Override"))
	bool Override;

};


USTRUCT(BlueprintType)
struct FWaveParams_C
{
	GENERATED_USTRUCT_BODY()

		//Constructor
		FWaveParams_C() :
		K(FVector2D(1.0f, 0.0f)),
		W(500.0f),
		A(1.0f),
		S(0.1f),
		O(0.0f),
		OverrideIndex(0)
	{ }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (Category = "Default"))
	FVector2D K;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (Category = "Default"))
	float W;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (Category = "Default"))
	float A;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (Category = "Default"))
	float S;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (Category = "Default"))
	float O;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (Category = "Default"))
	int32 OverrideIndex;
};


UCLASS(config = Engine, Blueprintable, BlueprintType)
class AGerstnerWaveController_C : public AActor
{
public:
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, NonTransactional, meta=(Category="Default", OverrideNativeName="WaterMesh"))
	UStaticMeshComponent* WaterMesh;

	UPROPERTY(BlueprintReadWrite, NonTransactional, meta=(Category="Default", OverrideNativeName="DefaultSceneRoot"))
	USceneComponent* DefaultSceneRoot;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (Category = "Default"))
	UMaterialParameterCollection* WaterMaterialParameterCollection;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Quality Setting", Category="Wave Spectrum", OverrideNativeName="Quality Setting"))
	EWaterQualityLevel QualitySetting;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Seed", Category="Wave Spectrum", OverrideNativeName="Seed"))
	FRandomStream Seed;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Min Wavelength", Category="Wave Spectrum|Wavelengths", OverrideNativeName="Min Wavelength"))
	float MinWavelength;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Max Wavelength", Category="Wave Spectrum|Wavelengths", OverrideNativeName="Max Wavelength"))
	float MaxWavelength;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Wavelength Falloff", Category="Wave Spectrum|Wavelengths", OverrideNativeName="Wavelength Falloff"))
	float WavelengthFalloff;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Min Amplitude", Category="Wave Spectrum|Amplitudes", OverrideNativeName="Min Amplitude"))
	float MinAmplitude;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Max Amplitude", Category="Wave Spectrum|Amplitudes", OverrideNativeName="Max Amplitude"))
	float MaxAmplitude;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Amplitude Falloff", Category="Wave Spectrum|Amplitudes", OverrideNativeName="Amplitude Falloff"))
	float AmplitudeFalloff;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Wind Direction", Category="Wave Spectrum", OverrideNativeName="Wind Direction"))
	FVector2D WindDirection;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Direction Angular Spread", Category="Wave Spectrum", OverrideNativeName="Direction Angular Spread"))
	float DirectionAngularSpread;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Detail Normal Intensity", Category="Material", OverrideNativeName="Detail Normal Intensity"))
	float DetailNormalIntensity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Detail Normal Scale", Category="Material", OverrideNativeName="Detail Normal Scale"))
	float DetailNormalScale;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, meta=(DisplayName="Override Scalar Params", Category="Overrides", OverrideNativeName="Override Scalar Params"))
	TArray<FOverrideScalarParam_C> OverrideScalarParams;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, meta=(DisplayName="Override Vector Params", Category="Overrides", OverrideNativeName="Override Vector Params"))
	TArray<FOverrideVectorParam_C> OverrideVectorParams;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Inscatter Brightness", Category="Material", OverrideNativeName="Inscatter Brightness"))
	float InscatterBrightness;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Inscatter Contrast", Category="Material", OverrideNativeName="Inscatter Contrast"))
	float InscatterContrast;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Material", Category="Material", OverrideNativeName="Material"))
	UMaterialInterface* Material;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, meta=(DisplayName="Randomness", Category="Wave Spectrum", OverrideNativeName="Randomness"))
	float Randomness;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Small Wave Steepness", Category="Wave Spectrum|Steepness", OverrideNativeName="Small Wave Steepness"))
	float SmallWaveSteepness;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "MID", Category = "Debug", OverrideNativeName = "MID"))
	UMaterialInstanceDynamic* MID;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "Freeze Time Editor", Category = "Debug", OverrideNativeName = "Freeze Time Editor"))
	bool FreezeTimeEditor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Waves", Category="Debug", OverrideNativeName="Waves"))
	TArray<FWaveParams_C> Waves;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "Display Waves", Category = "Debug", tooltip, OverrideNativeName = "Display Waves"))
	int32 DisplayWaves;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Override Waves", Category="Overrides", OverrideNativeName="Override Waves"))
	TArray<FWaveParams_C> OverrideWaves;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Detail Normal", Category="Material", OverrideNativeName="Detail Normal"))
	UTexture* DetailNormal;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Enable Overrides", Category="Overrides", OverrideNativeName="Enable Overrides"))
	bool EnableOverrides;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Large Wave Steepness", Category="Wave Spectrum|Steepness", OverrideNativeName="Large Wave Steepness"))
	float LargeWaveSteepness;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Steepness Falloff", Category="Wave Spectrum|Steepness", OverrideNativeName="Steepness Falloff"))
	float SteepnessFalloff;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, meta=(DisplayName="Water Waves", Category="Default", MultiLine="true", OverrideNativeName="WaterWaves"))
	TArray<FWaterWaveParams> WaterWaves;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Target Depth For Full Waves", Category="Wave Spectrum", tooltip="The depth curve is exponential. It is defined as the depth at which the wave strength will be 87%.  This is using 1-exp(-Depth / (TargetDepth/2))", OverrideNativeName="Target Depth for Full Waves"))
	float TargetDepthForFullWaves;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Lake Spectrum", Category="Default", MultiLine="true", OverrideNativeName="Lake Spectrum"))
	FWaveSpectrumSettings_C LakeSpectrum;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Ocean Spectrum", Category="Default", MultiLine="true", OverrideNativeName="Ocean Spectrum"))
	FWaveSpectrumSettings_C OceanSpectrum;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Null Spectrum", Category="Default", MultiLine="true", OverrideNativeName="Null Spectrum"))
	FWaveSpectrumSettings_C NullSpectrum;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Lake Waves", Category="Default", MultiLine="true", OverrideNativeName="LakeWaves"))
	TArray<FWaterWaveParams> LakeWaves;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Ocean Waves", Category="Default", MultiLine="true", OverrideNativeName="OceanWaves"))
	TArray<FWaterWaveParams> OceanWaves;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Null Waves", Category="Default", MultiLine="true", OverrideNativeName="NullWaves"))
	TArray<FWaterWaveParams> NullWaves;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Wave Params RT", Category="Default", MultiLine="true", OverrideNativeName="WaveParamsRT"))
	UTextureRenderTarget2D* WaveParamsRT;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, meta=(DisplayName="Max Waves Per Water Body", Category="Default", MultiLine="true", OverrideNativeName="MaxWavesPerWaterBody"))
	int32 MaxWavesPerWaterBody;

	AGerstnerWaveController_C(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UFUNCTION(BlueprintCallable, meta = (CallInEditor = "true", Category="Default", OverrideNativeName = "Set Waves On WaterBodies"))
	virtual void SetWavesOnWaterBodies();

	UFUNCTION(BlueprintCallable, meta = (Category = "Default", OverrideNativeName = "RenderWaveTextureData"))
	virtual void RenderWaveTextureData();

	UFUNCTION(BlueprintCallable, meta = (Category = "Default", OverrideNativeName = "GetWaterWaves"))
	virtual void GetWaterWaves(TArray<FWaterWaveParams>& OutWaterWaveParams, float& OutBigWaveDepth);

	UFUNCTION(BlueprintCallable, meta = (Category = "Default", OverrideNativeName = "Set Distance Field"))
	virtual void SetDistanceField(UTexture* InRT);

	UFUNCTION(BlueprintCallable, meta = (Category = "Default", OverrideNativeName = "Set All Wave MPC Params"))
	virtual void SetAllWaveMPCParams();

	UFUNCTION(BlueprintCallable, meta = (Category = "Default", OverrideNativeName = "Set Single Wave MPC Parameters"))
	virtual void SetSingleWaveMPCParameters(FWaveParams_C const& InWaveParams, int32 InIdx);

	UFUNCTION(BlueprintCallable, meta = (Category = "Default", OverrideNativeName = "Manual Parameter Overrides"))
	virtual void ManualParameterOverrides();

	UFUNCTION(BlueprintCallable, meta = (Category = "Default", OverrideNativeName = "General MPC Params"))
	virtual void GeneralMPCParams();

	UFUNCTION(BlueprintCallable, meta = (Category = "Default", OverrideNativeName = "Harvest Material Params"))
	virtual void HarvestMaterialParams();

	UFUNCTION(BlueprintCallable, meta = (Category = "Default", OverrideNativeName = "Generate Spectrum Old"))
	virtual void GenerateSpectrumOld();

	UFUNCTION(BlueprintCallable, meta = (Category = "Default", OverrideNativeName = "Generate Spectrum"))
	virtual void GenerateSpectrum(FWaveSpectrumSettings_C InSpectrum, TArray<FWaterWaveParams>& OutWaves);

	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;

	private:
	int32 GetQualityWaveCount();

};
