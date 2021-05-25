// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

#include "Curves/RichCurve.h"
#include "Engine/Texture.h"
#include "ICalibratedMapProcessor.h"
#include "LensData.h"
#include "Misc/Optional.h"
#include "Templates/UniquePtr.h"
#include "Tickable.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "LensFile.generated.h"


class FLensFilePreComputeDataProcessor;
class UCineCameraComponent;
class ULensDistortionModelHandlerBase;


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
struct CAMERACALIBRATION_API FDistortionData
{
	GENERATED_BODY()

public:

	UPROPERTY(VisibleAnywhere, Category = "Distortion")
	TArray<FVector2D> DistortedUVs;

	/** Estimated overscan factor based on distortion to have distorted cg covering full size */
	UPROPERTY(EditAnywhere, Category = "Distortion")
	float OverscanFactor = 1.0f;
};

/**
 * Encoder mapping
 */
USTRUCT(BlueprintType)
struct CAMERACALIBRATION_API FEncoderMapping
{
	GENERATED_BODY()

public:

	/** Focus curve from encoder values to nominal values */
	UPROPERTY()
	FRichCurve Focus;

	/** Iris curve from encoder values to nominal values */
	UPROPERTY()
	FRichCurve Iris;

	/** 
	 * Zoom curve from encoder values to nominal values 
	 * @note To be removed and use only Fx/Fy calibrated values
	 */
	UPROPERTY()
	FRichCurve Zoom;
};

/**
 * Derived data computed from parameters or stmap
 */
USTRUCT(BlueprintType)
struct CAMERACALIBRATION_API FDerivedDistortionData
{
	GENERATED_BODY()

	/** Precomputed data about distortion */
	UPROPERTY(VisibleAnywhere, Category = "Distortion")
	FDistortionData DistortionData;

	/** Computed displacement map based on undistortion data */
	UPROPERTY(Transient, VisibleAnywhere, Category = "Distortion")
	UTextureRenderTarget2D* UndistortionDisplacementMap = nullptr;

	/** Computed displacement map based on distortion data */
	UPROPERTY(Transient, VisibleAnywhere, Category = "Distortion")
	UTextureRenderTarget2D* DistortionDisplacementMap = nullptr;

	/** When dirty, derived data needs to be recomputed */
	bool bIsDirty = true;
};

/**
 * A data point associating focus and zoom to lens parameters
 */
USTRUCT(BlueprintType)
struct CAMERACALIBRATION_API FDistortionMapPoint
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
* A data point associating focus and zoom to precalibrated STMap
*/
USTRUCT(BlueprintType)
struct CAMERACALIBRATION_API FCalibratedMapPoint
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
			&& DerivedDistortionData.UndistortionDisplacementMap != nullptr
			&& DerivedDistortionData.DistortionDisplacementMap != nullptr
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
 * A data point associating focus and zoom to the principal point (image center)
 */
USTRUCT(BlueprintType)
struct CAMERACALIBRATION_API FIntrinsicMapPoint
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
struct CAMERACALIBRATION_API FNodalOffsetMapPoint
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
class CAMERACALIBRATION_API ULensFile : public UObject, public FTickableGameObject
{
	GENERATED_BODY()


public:

	ULensFile();

	//~Begin UObject interface
#if WITH_EDITOR
	virtual void PostEditChangeChainProperty( struct FPropertyChangedChainEvent& PropertyChangedEvent ) override;
#endif //WITH_EDITOR
	
	virtual void PostInitProperties() override;
	//~End UObject interface

	//~ Begin FTickableGameObject
	virtual bool IsTickableInEditor() const override { return true; }
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	//~ End FTickableGameObject
	
	/** Returns interpolated distortion parameters based on input focus and zoom */
	bool EvaluateDistortionParameters(float InFocus, float InZoom, FDistortionInfo& OutEvaluatedValue) const;

	/** Returns interpolated intrinsic parameters based on input focus and zoom */
	bool EvaluateIntrinsicParameters(float InFocus, float InZoom, FIntrinsicParameters& OutEvaluatedValue) const;

	/** Draws the distortion map based on evaluation point*/
	bool EvaluateDistortionData(float InFocus, float InZoom, FVector2D InFilmback, ULensDistortionModelHandlerBase* InLensHandler, FDistortionData& OutDistortionData) const;

	/** Returns interpolated nodal point offset based on input focus and zoom */
	bool EvaluateNodalPointOffset(float InFocus, float InZoom, FNodalPointOffset& OutEvaluatedValue) const;

	/** Whether focus encoder mapping is configured */
	bool HasFocusEncoderMapping() const;

	/** Returns interpolated focus based on input normalized value and mapping */
	float EvaluateNormalizedFocus(float InNormalizedValue);

	/** Whether iris encoder mapping is configured */
	bool HasIrisEncoderMapping() const;

	/** Returns interpolated iris based on input normalized value and mapping */
	float EvaluateNormalizedIris(float InNormalizedValue);

	/** Whether zoom encoder mapping is configured */
	bool HasZoomEncoderMapping() const;

	/** Returns interpolated zoom based on input normalized value and mapping */
	float EvaluateNormalizedZoom(float InNormalizedValue);

	/** Callbacked when stmap derived data has completed */
	void OnDistortionDerivedDataJobCompleted(const FDerivedDistortionDataJobOutput& JobOutput);

	/** Whether the sensor dimensions in the lens file will be compatible with the sensor dimensions of the input CineCameraComponent */
	bool IsCineCameraCompatible(const UCineCameraComponent* CineCameraComponent);

protected:

	/** Updates derived data entries to make sure it matches what is assigned in map points based on data mode */
	void UpdateDerivedData();

	/** Returns the overscan factor based on distorted UV and image center */
	float ComputeOverscan(const FDistortionData& DerivedData, FVector2D PrincipalPoint) const;

	/** Clears output displacement map on LensHandler to have no distortion and setup distortion data to match that */
	void SetupNoDistortionOutput(ULensDistortionModelHandlerBase* LensHandler, FDistortionData& OutDistortionData) const;

	/** Evaluates distortion based on InFocus and InZoom using parameters */
	bool EvaluateDistortionForParameters(float InFocus, float InZoom, FVector2D InFilmback, ULensDistortionModelHandlerBase* LensHandler, FDistortionData& OutDistortionData) const;
	
	/** Evaluates distortion based on InFocus and InZoom using STMaps */
	bool EvaluteDistortionForSTMaps(float InFocus, float InZoom, FVector2D InFilmback, ULensDistortionModelHandlerBase* LensHandler, FDistortionData& OutDistortionData) const;
	
public:

	/** Lens information */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lens info")
	FLensInfo LensInfo;

	/** Type of data used for lens mapping */
	UPROPERTY(EditAnywhere, Category="Lens mapping")
	ELensDataMode DataMode = ELensDataMode::Parameters;
	
	/** Mapping between FIZ data and distortion parameters (k1, k2...) */
	UPROPERTY(EditAnywhere, Category="FIZ map", meta = (EditCondition = "DataMode == ELensDataMode::Parameters"))
	TArray<FDistortionMapPoint> DistortionMapping;

	/** Mapping between FIZ data and intrinsic parameters (fx/fy and principal point) */
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

	/** Texture used to store temporary undistortion displacement map when using map blending */
	UPROPERTY(Transient)
	TArray<UTextureRenderTarget2D*> UndistortionDisplacementMapHolders;

	/** Texture used to store temporary distortion displacement map when using map blending */
	UPROPERTY(Transient)
	TArray<UTextureRenderTarget2D*> DistortionDisplacementMapHolders;

	static constexpr int32 DisplacementMapHolderCount = 4;

	/** UV coordinates of 8 points (4 corners + 4 mid points) */
	static const TArray<FVector2D> UndistortedUVs;
};


/**
 * Wrapper to facilitate default lensfile vs picker
 */
USTRUCT(BlueprintType)
struct CAMERACALIBRATION_API FLensFilePicker
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

/** Structure that caches the inputs (and other useful bits) used when evaluating the Lens File */
struct FLensFileEvalData
{
	FLensFileEvalData()
	{
		Invalidate();
	};

	ULensFile* LensFile;

	/** The values that should be used as inputs to the Lut in the LensFile */
	struct
	{
		/** Focus input */
		TOptional<float> Focus;

		/** Iris input */
		TOptional<float> Iris;

		/** Zoom input */
		TOptional<float> Zoom;
	} Input;

	/** Information about the camera associated with the lens evaluation */
	struct
	{
		uint32 UniqueId;
	} Camera;

	/** Information about the Distortion evaluation */
	struct
	{
		/** True if distotion was applied (and the lens distortion handler updated its state) */
		bool bWasEvaluated;
	} Distortion;

	/** Information about the nodal offset evaluation */
	struct
	{
		/** True if the evaluated nodal offset was applied to the camera */
		bool bWasApplied;
	} NodalOffset;

	/** Invalidates the data in this structure and avoid using stale or invalid values */
	void Invalidate()
	{
		LensFile = nullptr;

		Input.Focus.Reset();
		Input.Iris.Reset();
		Input.Zoom.Reset();

		Camera.UniqueId = INDEX_NONE;
		Distortion.bWasEvaluated = false;
		NodalOffset.bWasApplied = false;
	}
};


