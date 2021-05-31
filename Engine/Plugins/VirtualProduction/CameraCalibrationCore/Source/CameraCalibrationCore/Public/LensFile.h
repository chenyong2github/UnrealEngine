// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Tickable.h"
#include "UObject/Object.h"

#include "CoreTypes.h"
#include "Engine/Texture.h"
#include "ICalibratedMapProcessor.h"
#include "LensData.h"
#include "Misc/Optional.h"
#include "Tables/DistortionParametersTable.h"
#include "Tables/EncodersTable.h"
#include "Tables/FocalLengthTable.h"
#include "Tables/ImageCenterTable.h"
#include "Tables/NodalOffsetTable.h"
#include "Tables/STMapTable.h"
#include "Templates/UniquePtr.h"
#include "UObject/ObjectMacros.h"

#include "LensFile.generated.h"


class UCineCameraComponent;
class ULensDistortionModelHandlerBase;


/** Mode of operation of Lens File */
UENUM()
enum class ELensDataMode : uint8
{
	Parameters = 0,
	STMap = 1
};

/** Data categories manipulated in the camera calibration tools */
enum class ELensDataCategory : uint8
{
	Focus,
	Iris,
	Zoom,
	Distortion,
	ImageCenter,
	STMap,
	NodalOffset,
};


/**
 * A Lens file containing calibration mapping from FIZ data
 */
UCLASS(BlueprintType)
class CAMERACALIBRATIONCORE_API ULensFile : public UObject, public FTickableGameObject
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
	
	/** Returns interpolated distortion parameters */
	bool EvaluateDistortionParameters(float InFocus, float InZoom, FDistortionInfo& OutEvaluatedValue) const;

	/** Returns interpolated focal length */
	bool EvaluateFocalLength(float InFocus, float InZoom, FFocalLengthInfo& OutEvaluatedValue) const;

	/** Returns interpolated image center parameters based on input focus and zoom */
	bool EvaluateImageCenterParameters(float InFocus, float InZoom, FImageCenterInfo& OutEvaluatedValue) const;

	/** Draws the distortion map based on evaluation point*/
	bool EvaluateDistortionData(float InFocus, float InZoom, FVector2D InFilmback, ULensDistortionModelHandlerBase* InLensHandler, FDistortionData& OutDistortionData) const;

	/** Returns interpolated nodal point offset based on input focus and zoom */
	bool EvaluateNodalPointOffset(float InFocus, float InZoom, FNodalPointOffset& OutEvaluatedValue) const;

	/** Whether focus encoder mapping is configured */
	bool HasFocusEncoderMapping() const;

	/** Returns interpolated focus based on input normalized value and mapping */
	float EvaluateNormalizedFocus(float InNormalizedValue) const;

	/** Whether iris encoder mapping is configured */
	bool HasIrisEncoderMapping() const;

	/** Returns interpolated iris based on input normalized value and mapping */
	float EvaluateNormalizedIris(float InNormalizedValue) const;

	/** Callbacked when stmap derived data has completed */
	void OnDistortionDerivedDataJobCompleted(const FDerivedDistortionDataJobOutput& JobOutput);

	/** Whether the sensor dimensions in the lens file will be compatible with the sensor dimensions of the input CineCameraComponent */
	bool IsCineCameraCompatible(const UCineCameraComponent* CineCameraComponent) const;

	/** Adds a distortion point in our map. If a point already exist at the location, it is updated */
	void AddDistortionPoint(float NewFocus, float NewZoom, const FDistortionInfo& NewPoint, const FFocalLengthInfo& NewFocalLength);

	/** Adds a focal length point in our map. If a point already exist at the location, it is updated */
	void AddFocalLengthPoint(float NewFocus, float NewZoom, const FFocalLengthInfo& NewFocalLength);

	/** Adds an ImageCenter point in our map. If a point already exist at the location, it is updated */
	void AddImageCenterPoint(float NewFocus, float NewZoom, const FImageCenterInfo& NewPoint);

	/** Adds an NodalOffset point in our map. If a point already exist at the location, it is updated */
	void AddNodalOffsetPoint(float NewFocus, float NewZoom, const FNodalPointOffset& NewPoint);

	/** Adds an STMap point in our map. If a point already exist at the location, it is updated */
	void AddSTMapPoint(float NewFocus, float NewZoom, const FSTMapInfo& NewPoint);

	/** Removes a focus point */
	void RemoveFocusPoint(ELensDataCategory InDataCategory, float InFocus);

	/** Removes a zoom point */
	void RemoveZoomPoint(ELensDataCategory InDataCategory, float InFocus, float InZoom);

	/** Removes all points in lens files */
	void ClearAll();
	
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
	UPROPERTY(EditAnywhere, Category = "Lens mapping")
	ELensDataMode DataMode = ELensDataMode::Parameters;

	/** Metadata user could enter for its lens */
	UPROPERTY(EditAnywhere, Category = "Metadata")
	TMap<FString, FString> UserMetadata;

	/** Encoder mapping table */
	UPROPERTY()
	FEncodersTable EncodersTable;

	/** Distortion parameters table mapping to input focus/zoom  */
	UPROPERTY()
	FDistortionTable DistortionTable;

	/** Focal length table mapping to input focus/zoom  */
	UPROPERTY()
	FFocalLengthTable FocalLengthTable;

	/** Image center table mapping to input focus/zoom  */
	UPROPERTY()
	FImageCenterTable ImageCenterTable;

	/** Nodal offset table mapping to input focus/zoom  */
	UPROPERTY()
	FNodalOffsetTable NodalOffsetTable;

	/** STMap table mapping to input focus/zoom  */
	UPROPERTY()
	FSTMapTable STMapTable;
	
	/** Tolerance used to consider input focus or zoom to be identical */
	static constexpr float InputTolerance = 0.001f;
	
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
struct CAMERACALIBRATIONCORE_API FLensFilePicker
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


