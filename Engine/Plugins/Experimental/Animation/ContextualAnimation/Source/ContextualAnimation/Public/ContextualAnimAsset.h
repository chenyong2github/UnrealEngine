// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DataAsset.h"
#include "Animation/AnimSequence.h"
#include "ContextualAnimAsset.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogContextualAnim, Log, All);

UCLASS()
class UContextualAnimUtilities : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	static void ExtractLocalSpacePose(const UAnimSequenceBase* Animation, const FBoneContainer& BoneContainer, float Time, bool bExtractRootMotion, FCompactPose& OutPose);
	static void ExtractComponentSpacePose(const UAnimSequenceBase* Animation, const FBoneContainer& BoneContainer, float Time, bool bExtractRootMotion, FCSPose<FCompactPose>& OutPose);
	static FTransform ExtractRootMotionFromAnimation(const UAnimSequenceBase* Animation, float StartTime, float EndTime);
};

USTRUCT()
struct FAlignmentTrackContainer
{
	GENERATED_BODY()

	UPROPERTY()
	FRawAnimSequenceTrack Track;

	UPROPERTY()
	float SampleRate;

	FTransform ExtractTransformAtTime(float Time) const;

	void Initialize(float InSampleRate)
	{
		Track.PosKeys.Empty();
		Track.RotKeys.Empty();
		Track.ScaleKeys.Empty();

		SampleRate = InSampleRate;
	}
};

USTRUCT(BlueprintType)
struct FContextualAnimDistanceParam
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Defaults", meta = (UIMin = 0, ClampMin = 0))
	float Value = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults", meta = (UIMin = 0, ClampMin = 0))
	float MinDistance = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults", meta = (UIMin = 0, ClampMin = 0))
	float MaxDistance = 0.f;
};

USTRUCT(BlueprintType)
struct FContextualAnimAngleParam
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Defaults")
	float Value = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults")
	float Tolerance = 0.f;
};

USTRUCT(BlueprintType)
struct FContextualAnimData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults")
	TSoftObjectPtr<UAnimMontage> Animation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults", meta = (UIMin = 0, ClampMin = 0))
	float EntryTime = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults", meta = (UIMin = 0, ClampMin = 0))
	float SyncTime = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults")
	float OffsetFromOrigin = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults")
	FContextualAnimDistanceParam Distance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults")
	FContextualAnimAngleParam Angle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults")
	FContextualAnimAngleParam Facing;

	UPROPERTY()
	FAlignmentTrackContainer AlignmentData;

	FORCEINLINE FTransform GetAlignmentTransformAtTime(float Time) const { return AlignmentData.ExtractTransformAtTime(Time); }
	FORCEINLINE FTransform GetAlignmentTransformAtEntryTime() const { return AlignmentData.ExtractTransformAtTime(EntryTime); }
	FORCEINLINE FTransform GetAlignmentTransformAtSyncTime() const  { return AlignmentData.ExtractTransformAtTime(SyncTime); }
};

UCLASS(Blueprintable)
class CONTEXTUALANIMATION_API UContextualAnimAsset : public UDataAsset
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Alignment")
	FName AlignmentJoint;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Alignment")
	FTransform MeshToComponent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animations")
	TArray<FContextualAnimData> DataContainer;

	UContextualAnimAsset(const FObjectInitializer& ObjectInitializer);

	virtual void PreSave(const class ITargetPlatform* TargetPlatform) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR

};
