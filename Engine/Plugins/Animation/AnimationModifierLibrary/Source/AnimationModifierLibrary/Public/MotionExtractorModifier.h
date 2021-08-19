// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AnimationModifier.h"
#include "MotionExtractorModifier.generated.h"

/** Type of motion to extract */
UENUM(BlueprintType)
enum class EMotionExtractor_MotionType : uint8
{
	Translation,
	Rotation,
	Scale,
	TranslationSpeed,
	RotationSpeed
};

/** Axis to get the final value from */
UENUM(BlueprintType)
enum class EMotionExtractor_Axis : uint8
{
	X,
	Y,
	Z,
	XY,
	XZ,
	YZ,
	XYZ
};

/** Math operations that can be applied to the extracted value before add it to the curve */
UENUM(BlueprintType)
enum class EMotionExtractor_MathOperation : uint8
{
	None,
	Addition,
	Subtraction,
	Division,
	Multiplication
};

/** Extracts motion from a bone in the animation and bakes it into a curve */
UCLASS()
class UMotionExtractorModifier : public UAnimationModifier
{
	GENERATED_BODY()

public:

	/** Bone we are going to generate the curve from */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	FName BoneName;

	/** Type of motion to extract */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	EMotionExtractor_MotionType MotionType;

	/** Axis to get the value from */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	EMotionExtractor_Axis Axis;

	/** Whether to extract the bone pose in component space or local space */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	bool bComponentSpace;

	/** Whether to convert the final value to absolute (positive) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	bool bAbsoluteValue;

	/** Optional math operation to apply on the extracted value before add it to the generated curve */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	EMotionExtractor_MathOperation MathOperation;

	/** Right operand for the math operation selected */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (EditCondition = "MathOperation != EMotionExtractor_MathOperation::None"))
	float Modifier;

	/** Rate used to sample the animation */
	UPROPERTY(EditAnywhere, Category = Settings, meta = (ClampMin = "1"))
	int32 SampleRate;

	/** Whether we want to specify a custom name for the curve. If false, the name of the curve will be auto generated based on the data we are going to extract */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	bool bUseCustomCurveName;

	/** Custom name for the curve we are going to generate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (EditCondition = "bUseCustomCurveName"))
	FName CustomCurveName;

	UMotionExtractorModifier();

	virtual void OnApply_Implementation(UAnimSequence* Animation) override;
	virtual void OnRevert_Implementation(UAnimSequence* Animation) override;

	/** Returns the name for the curve. If CurveName is None it generates a name based on the data we are going to extract */
	FName GetCurveName() const;

	/** Returns the desired value from the extracted poses */
	float GetDesiredValue(const FTransform& BoneTransform, const FTransform& LastBoneTransform, float DeltaTime) const;

	/** Helper function to extract the pose for a given bone at a given time */
	static FTransform ExtractBoneTransform(UAnimSequence* Animation, const FBoneContainer& BoneContainer, FCompactPoseBoneIndex CompactPoseBoneIndex, float Time, bool bComponentSpace);

	/** Helper function to calculate the magnitude of a vector only considering a specific axis or axes */
	static float CalculateMagnitude(const FVector& Vector, EMotionExtractor_Axis Axis);
};
