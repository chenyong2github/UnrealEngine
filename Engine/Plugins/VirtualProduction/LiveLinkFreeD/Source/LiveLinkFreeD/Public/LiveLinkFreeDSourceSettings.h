// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LiveLinkSourceSettings.h"
#include "LiveLinkFreeDSourceSettings.generated.h"

USTRUCT()
struct FFreeDEncoderData
{
	GENERATED_BODY()

	/** Is this encoder data valid? */
	UPROPERTY(EditAnywhere, Category = "Encoder Data")
	bool bIsValid;

	/** Multiply this by the normalized encoder value */
	UPROPERTY(EditAnywhere, Category = "Encoder Data", meta = (EditCondition = "bIsValid"))
	float Scale;

	/** Use manual Min/Max values for the encoder normalization (normally uses dynamic auto ranging based on inputs) */
	UPROPERTY(EditAnywhere, Category = "Encoder Data", meta = (EditCondition = "bIsValid"))
	bool bUseManualRange;

	/** Minimum raw encoder value */
	UPROPERTY(EditAnywhere, Category = "Encoder Data", meta = (ClampMin = 0, ClampMax = 0x00ffffff, EditCondition = "bIsValid && bUseManualRange"))
	int32 Min;

	/** Maximum raw encoder value */
	UPROPERTY(EditAnywhere, Category = "Encoder Data", meta = (ClampMin = 0, ClampMax = 0x00ffffff, EditCondition = "bIsValid && bUseManualRange"))
	int32 Max;

	/** Mask bits for raw encoder value */
	UPROPERTY(EditAnywhere, Category = "Encoder Data", meta = (ClampMin = 0, ClampMax = 0x00ffffff, EditCondition = "bIsValid && bUseManualRange"))
	int32 MaskBits;
};

UENUM(BlueprintType)
enum class EFreeDDefaultConfigs : uint8
{
	Generic,
	Panasonic,
	Sony,
	Stype UMETA(DisplayName="stYpe"),
	Mosys,
	Ncam
};

UENUM(BlueprintType)
enum class EFreeDAxisRemap : uint8
{
	PositiveX,
	NegativeX,
	PositiveY,
	NegativeY,
	PositiveZ,
	NegativeZ
};

UCLASS()
class LIVELINKFREED_API ULiveLinkFreeDSourceSettings : public ULiveLinkSourceSettings
{
	GENERATED_BODY()

public:
	/** Send extra string meta data (Camera ID and FrameCounter) */
	UPROPERTY(EditAnywhere, Category = "Source")
	bool bSendExtraMetaData = false;

	/** Default configurations for specific manufacturers */
	UPROPERTY(EditAnywhere, Category = "Source")
	EFreeDDefaultConfigs DefaultConfig = EFreeDDefaultConfigs::Generic;

	/** X axis remap settings*/
	UPROPERTY(EditAnywhere, Category = "Source")
	EFreeDAxisRemap RemapXAxis = EFreeDAxisRemap::PositiveY;

	/** Y axis remap settings*/
	UPROPERTY(EditAnywhere, Category = "Source")
	EFreeDAxisRemap RemapYAxis = EFreeDAxisRemap::PositiveX;

	/** Z axis remap settings*/
	UPROPERTY(EditAnywhere, Category = "Source")
	EFreeDAxisRemap RemapZAxis = EFreeDAxisRemap::PositiveZ;

	/** Raw focus distance (in cm) encoder parameters for this camera - 24 bits max */
	UPROPERTY(EditAnywhere, Category = "Source")
	FFreeDEncoderData FocusDistanceEncoderData = { true, 10000.0f, false, 0x00ffffff, 0, 0x00ffffff };

	/** Raw focal length/zoom (in mm) encoder parameters for this camera - 24 bits max */
	UPROPERTY(EditAnywhere, Category = "Source")
	FFreeDEncoderData FocalLengthEncoderData = { true, 100.0f, false, 0x00ffffff, 0, 0x00ffffff };

	/** Raw user defined/spare data encoder (normally used for Aperture) parameters for this camera - 16 bits max */
	UPROPERTY(EditAnywhere, Category = "Source")
	FFreeDEncoderData UserDefinedEncoderData = { false, 1.0f, false, 0x0000ffff, 0, 0x0000ffff };
};
