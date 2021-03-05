// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

#include "LensData.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "LensFile.generated.h"




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

	UPROPERTY(EditAnywhere, Category = "Distortion")
	float Focus = 0.0f;

	UPROPERTY(EditAnywhere, Category = "Distortion")
	float Zoom = 0.0f;

	UPROPERTY(EditAnywhere, Category = "Distortion")
	FDistortionParameters Parameters;
};

/**
 * A data point associating focus and zoom to center shift
 */
USTRUCT(BlueprintType)
struct LENSDISTORTION_API FIntrinsicMapPoint
{
	GENERATED_BODY()

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
class LENSDISTORTION_API ULensFile : public UObject
{
	GENERATED_BODY()


public:

	ULensFile();

	/** Returns interpolated distortion parameters based on input focus and zoom */
	bool EvaluateDistortionParameters(float InFocus, float InZoom, FDistortionParameters& OutEvaluatedValue);

	/** Returns interpolated intrinsic parameters based on input focus and zoom */
	bool EvaluateIntrinsicParameters(float InFocus, float InZoom, FIntrinsicParameters& OutEvaluatedValue);

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

public:

	/** Lens information */
	UPROPERTY(EditAnywhere, Category = "Lens info")
	FLensInfo LensInfo;

	/** Mapping between FIZ data and distortion parameters (k1, k2...) */
	UPROPERTY(EditAnywhere, Category="FIZ map")
	TArray<FDistortionMapPoint> DistortionMapping;

	/** Mapping between FIZ data and intrinsic parameters (focal length, center shift) */
	UPROPERTY(EditAnywhere, Category = "FIZ map")
	TArray<FIntrinsicMapPoint> IntrinsicMapping;

	/** Mapping between FIZ data and nodal point */
	UPROPERTY(EditAnywhere, Category = "FIZ map")
	TArray<FNodalOffsetMapPoint> NodalOffsetMapping;

	/** Metadata user could enter for its lens */
	UPROPERTY(EditAnywhere, Category = "Metadata")
	TMap<FString, FString> UserMetadata;

	/** Encoder mapping from normalized value to values in physical units */
	UPROPERTY(EditAnywhere, Category = "Encoder", AdvancedDisplay)
	FEncoderMapping EncoderMapping;
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
