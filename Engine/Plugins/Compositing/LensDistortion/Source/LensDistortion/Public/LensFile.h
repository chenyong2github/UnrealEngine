// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

#include "Engine/Texture.h"
#include "ICalibratedMapProcessor.h"
#include "LensData.h"
#include "Templates/UniquePtr.h"
#include "Tickable.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "LensFile.generated.h"


class FLensFilePreComputeDataProcessor;


/** Mode of operation of Lens File */
UENUM()
enum class ELensDataMode : uint8
{
	Parameters = 0,
	STMap = 1
};

/**
 * Distortion data evaluated for given FZ pair based on lens parameters
 */
USTRUCT(BlueprintType)
struct LENSDISTORTION_API FDistortionData
{
	GENERATED_BODY()

public:

	UPROPERTY(VisibleAnywhere, Category = "Distortion")
	TArray<FVector2D> DistortedUVs;

	/** Estimated overscan factor based on distortion to have distorted cg covering full size */
	UPROPERTY(EditAnywhere, Category = "Nodal Point")
	float OverscanFactor = 1.0f;
};

/**
 * Encoder mapping
 */
USTRUCT(BlueprintType)
struct LENSDISTORTION_API FEncoderMapping
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, Category = "Encoder")
	TArray<FEncoderPoint> Focus;

	UPROPERTY(EditAnywhere, Category = "Encoder")
	TArray<FEncoderPoint> Iris;

	UPROPERTY(EditAnywhere, Category = "Encoder")
	TArray<FEncoderPoint> Zoom;
};

/**
 * A data point associating focus and zoom to lens parameters
 */
USTRUCT(BlueprintType)
struct LENSDISTORTION_API FDistortionMapPoint
{
	GENERATED_BODY()

public:
	FDistortionMapPoint()
    : Identifier(FGuid::NewGuid())
	{}

	const FGuid& GetIdentifier() const { return Identifier; }

	/** Returns whether this point is considered valid */
	bool IsValid() const { return true; }


public:

	UPROPERTY(EditAnywhere, Category = "Distortion")
	float Focus = 0.0f;

	UPROPERTY(EditAnywhere, Category = "Distortion")
	float Zoom = 0.0f;

	UPROPERTY(EditAnywhere, Category = "Distortion")
	FDistortionInfo DistortionInfo;

private:

	/** Unique identifier for this map point to associate it with derived data */
	FGuid Identifier;
};

/**
 * Derived data computed from parameters or stmap
 */
USTRUCT(BlueprintType)
struct LENSDISTORTION_API FDerivedDistortionData
{
	GENERATED_BODY()

	/** Precomputed data about distortion */
	UPROPERTY(VisibleAnywhere, Category = "Distortion")
	FDistortionData DistortionData;

	/** Computed displacement map based on distortion data */
	UPROPERTY(Transient, VisibleAnywhere, Category = "Distortion")
	UTextureRenderTarget2D* DisplacementMap = nullptr;

	/** When dirty, derived data needs to be recomputed */
	bool bIsDirty = true;
};

/**
* A data point associating focus and zoom to precalibrated STMap
*/
USTRUCT(BlueprintType)
struct LENSDISTORTION_API FCalibratedMapPoint
{
	GENERATED_BODY()

public:

	FCalibratedMapPoint()
		: Identifier(FGuid::NewGuid())
	{}

	/** Returns the identifier of this point */
	const FGuid& GetIdentifier() const { return Identifier; }

	/** Returns whether this point is considered valid */
	bool IsValid() const 
	{ 
		return (DistortionMap != nullptr 
			&& DerivedDistortionData.DisplacementMap != nullptr 
			&& DerivedDistortionData.bIsDirty == false);
	}

public:
	UPROPERTY(EditAnywhere, Category = "Distortion")
	float Focus = 0.0f;

	UPROPERTY(EditAnywhere, Category = "Distortion")
	float Zoom = 0.0f;

	/** Pre calibrated UVMap/STMap
	 * RG channels are expected to have undistortion map (from distorted to undistorted)
	 * BA channels are expected to have distortion map (from undistorted (CG) to distorted)
	 */
	UPROPERTY(EditAnywhere, Category = "Distortion")
	UTexture* DistortionMap = nullptr;

	/** Derived distortion data associated with this point */
	UPROPERTY(Transient)
	FDerivedDistortionData DerivedDistortionData;

private:

	/** Unique identifier for this map point to associate it with derived data */
	FGuid Identifier;
};

/**
 * A data point associating focus and zoom to center shift
 */
USTRUCT(BlueprintType)
struct LENSDISTORTION_API FIntrinsicMapPoint
{
	GENERATED_BODY()

public:

	/** Returns whether this point is considered valid */
	bool IsValid() const { return true; }

public:

	UPROPERTY(EditAnywhere, Category = "Center shift")
	float Focus = 0.0f;

	UPROPERTY(EditAnywhere, Category = "Center shift")
	float Zoom = 0.0f;

	UPROPERTY(EditAnywhere, Category = "Center shift")
	FIntrinsicParameters Parameters;
};

/**
 * A data point associating focus and zoom to Nodal offset
 */
USTRUCT(BlueprintType)
struct LENSDISTORTION_API FNodalOffsetMapPoint
{
	GENERATED_BODY()

public:

	/** Returns whether this point is considered valid */
	bool IsValid() const { return true; }

public:

	UPROPERTY(EditAnywhere, Category = "Nodal Point")
	float Focus = 0.0f;

	UPROPERTY(EditAnywhere, Category = "Nodal Point")
	float Zoom = 0.0f;

	UPROPERTY(EditAnywhere, Category = "Nodal Point")
	FNodalPointOffset NodalOffset;
};



/**
 * A Lens file containing calibration mapping from FIZ data
 */
UCLASS(BlueprintType)
class LENSDISTORTION_API ULensFile : public UObject, public FTickableGameObject
{
	GENERATED_BODY()


public:

	ULensFile();

	//~Begin UObject interface
#if WITH_EDITOR
	virtual void PostEditChangeChainProperty( struct FPropertyChangedChainEvent& PropertyChangedEvent ) override;
#endif //WITH_EDITOR

	//~End UObject interface

	//~ Begin FTickableGameObject
	virtual bool IsTickableInEditor() const override { return true; }
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	//~ End FTickableGameObject
	
	/** Returns interpolated distortion parameters based on input focus and zoom */
	bool EvaluateDistortionParameters(float InFocus, float InZoom, FDistortionInfo& OutEvaluatedValue);

	/** Returns interpolated intrinsic parameters based on input focus and zoom */
	bool EvaluateIntrinsicParameters(float InFocus, float InZoom, FIntrinsicParameters& OutEvaluatedValue);

	/** Draws the distortion map based on evaluation point*/
	bool EvaluateDistortionData(float InFocus, float InZoom, UTextureRenderTarget2D* OutDisplacementMap, FDistortionData& OutDistortionData) const;

	/** Returns interpolated nodal point offset based on input focus and zoom */
	bool EvaluateNodalPointOffset(float InFocus, float InZoom, FNodalPointOffset& OutEvaluatedValue);

	/** Whether focus encoder mapping is configured */
	bool HasFocusEncoderMapping() const;

	/** Returns interpolated focus based on input normalized value and mapping */
	bool EvaluateNormalizedFocus(float InNormalizedValue, float& OutEvaluatedValue);

	/** Whether iris encoder mapping is configured */
	bool HasIrisEncoderMapping() const;

	/** Returns interpolated iris based on input normalized value and mapping */
	float EvaluateNormalizedIris(float InNormalizedValue, float& OutEvaluatedValue);

	/** Whether zoom encoder mapping is configured */
	bool HasZoomEncoderMapping() const;

	/** Returns interpolated zoom based on input normalized value and mapping */
	float EvaluateNormalizedZoom(float InNormalizedValue, float& OutEvaluatedValue);

	/** Callbacked when stmap derived data has completed */
	void OnDistortionDerivedDataJobCompleted(const FDerivedDistortionDataJobOutput& JobOutput);

	//~ Begin UObject Interface
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif	
	//~ End UObject Interface

protected:

	/** Updates derived data entries to make sure it matches what is assigned in map points based on data mode */
	void UpdateDerivedData();

	/** Returns the overscan factor based on distorted UV and center shift */
	float ComputeOverscan(const FDistortionData& DerivedData, FVector2D CenterShift) const;
	
public:

	/** Lens information */
	UPROPERTY(EditAnywhere, Category = "Lens info")
	FLensInfo LensInfo;

	/** Type of data used for lens mapping */
	UPROPERTY(EditAnywhere, Category="Lens mapping")
	ELensDataMode DataMode = ELensDataMode::Parameters;
	
	/** Mapping between FIZ data and distortion parameters (k1, k2...) */
	UPROPERTY(EditAnywhere, Category="FIZ map", meta = (EditCondition = "DataMode == ELensDataMode::Parameters"))
	TArray<FDistortionMapPoint> DistortionMapping;

	/** Mapping between FIZ data and intrinsic parameters (focal length, center shift) */
	UPROPERTY(EditAnywhere, Category="FIZ map")
	TArray<FIntrinsicMapPoint> IntrinsicMapping;

	/** Precomputed data associated to a calibration point */
	UPROPERTY(EditAnywhere, Category="FIZ map", meta = (EditCondition = "DataMode == ELensDataMode::STMap"))
	TArray<FCalibratedMapPoint> CalibratedMapPoints;
	
	/** Mapping between FIZ data and nodal point */
	UPROPERTY(EditAnywhere, Category = "FIZ map")
	TArray<FNodalOffsetMapPoint> NodalOffsetMapping;

	/** Metadata user could enter for its lens */
	UPROPERTY(EditAnywhere, Category = "Metadata")
	TMap<FString, FString> UserMetadata;

	/** Encoder mapping from normalized value to values in physical units */
	UPROPERTY(EditAnywhere, Category = "Encoder", AdvancedDisplay)
	FEncoderMapping EncoderMapping;

protected:

	/** Derived data compute jobs we are waiting on */
	int32 DerivedDataInFlightCount = 0;

	/** Processor handling derived data out of calibrated st maps */
	TUniquePtr<ICalibratedMapProcessor> CalibratedMapProcessor;
};


/**
 * Wrapper to facilitate default lensfile vs picker
 */
USTRUCT(BlueprintType)
struct LENSDISTORTION_API FLensFilePicker
{
	GENERATED_BODY()

public:

	/** Get the proper lens whether it's the default one or the picked one */
	ULensFile* GetLensFile() const;

public:
	/** You can override lens file to use if the default one is not desired */
	UPROPERTY(BlueprintReadWrite, Category = "Lens File")
	bool bOverrideDefaultLensFile = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lens File", Meta = (EditCondition = "bOverrideDefaultLensFile"))
	ULensFile* LensFile = nullptr;
};
