// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RootMotionModifier.h"
#include "RootMotionModifier_AdjustmentBlendWarp.generated.h"

USTRUCT()
struct FMotionDeltaTrack
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FTransform> BoneTransformTrack;

	UPROPERTY()
	TArray<FVector> DeltaTranslationTrack;

	UPROPERTY()
	TArray<FRotator> DeltaRotationTrack;

	UPROPERTY()
	FVector TotalTranslation = FVector::ZeroVector;

	UPROPERTY()
	FRotator TotalRotation = FRotator::ZeroRotator;
};

USTRUCT()
struct FMotionDeltaTrackContainer
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FMotionDeltaTrack> Tracks;

	void Init(int32 InNumTracks)
	{
		Tracks.Reserve(InNumTracks);
	}
};

USTRUCT()
struct MOTIONWARPING_API FRootMotionModifier_AdjustmentBlendWarp : public FRootMotionModifier_Warp
{
	GENERATED_BODY()

public:

	UPROPERTY()
	bool bWarpIKBones = false;

	UPROPERTY()
	TArray<FName> IKBones;

	FRootMotionModifier_AdjustmentBlendWarp();
	virtual ~FRootMotionModifier_AdjustmentBlendWarp() {}

	virtual UScriptStruct* GetScriptStruct() const { return FRootMotionModifier_AdjustmentBlendWarp::StaticStruct(); }
	virtual void OnSyncPointChanged(UMotionWarpingComponent& OwnerComp) override;
	virtual FTransform ProcessRootMotion(UMotionWarpingComponent& OwnerComp, const FTransform& InRootMotion, float DeltaSeconds) override;

	void GetIKBoneTransformAndAlpha(FName BoneName, FTransform& OutTransform, float& OutAlpha) const;

protected:

	UPROPERTY()
	FTransform CachedMeshTransform;

	UPROPERTY()
	FTransform CachedMeshRelativeTransform;

	UPROPERTY()
	FTransform CachedRootMotion;

	UPROPERTY()
	FAnimSequenceTrackContainer Result;

	UPROPERTY()
	float ActualStartTime = 0.f;

	void PrecomputeWarpedTracks(UMotionWarpingComponent& OwnerComp);

	FTransform ExtractWarpedRootMotion() const;

	void ExtractBoneTransformAtTime(FTransform& OutTransform, const FName& BoneName, float Time) const;
	void ExtractBoneTransformAtTime(FTransform& OutTransform, int32 TrackIndex, float Time) const;
	void ExtractBoneTransformAtFrame(FTransform& OutTransform, int32 TrackIndex, int32 Frame) const;

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	void DrawDebugWarpedTracks(UMotionWarpingComponent& OwnerComp, float DrawDuration) const;
#endif

	static void ExtractMotionDeltaFromRange(const FBoneContainer& BoneContainer, const UAnimSequenceBase* Animation, float StartTime, float EndTime, float SampleRate, FMotionDeltaTrackContainer& OutMotionDeltaTracks);

	static void AdjustmentBlendWarp(const FBoneContainer& BoneContainer, const FCSPose<FCompactPose>& AdditivePose, const FMotionDeltaTrackContainer& MotionDeltaTracks, FAnimSequenceTrackContainer& Output);
};

UCLASS(meta = (DisplayName = "Adjustment Blend Warp"))
class MOTIONWARPING_API URootMotionModifierConfig_AdjustmentBlendWarp : public URootMotionModifierConfig_Warp
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config")
	bool bWarpIKBones;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config", meta = (EditCondition = "bWarpIKBones"))
	TArray<FName> IKBones;

	URootMotionModifierConfig_AdjustmentBlendWarp(const FObjectInitializer& ObjectInitializer)
		: Super(ObjectInitializer) {}

	virtual void AddRootMotionModifier(UMotionWarpingComponent* MotionWarpingComp, const UAnimSequenceBase* Animation, float StartTime, float EndTime) const override
	{
		URootMotionModifierConfig_AdjustmentBlendWarp::AddRootMotionModifierAdjustmentBlendWarp(MotionWarpingComp, Animation, StartTime, EndTime, SyncPointName, bWarpTranslation, bIgnoreZAxis, bWarpRotation, bWarpIKBones, IKBones);
	}

	UFUNCTION(BlueprintCallable, Category = "Motion Warping")
	static void AddRootMotionModifierAdjustmentBlendWarp(UMotionWarpingComponent* InMotionWarpingComp, const UAnimSequenceBase* InAnimation, float InStartTime, float InEndTime, FName InSyncPointName, bool bInWarpTranslation, bool bInIgnoreZAxis, bool bInWarpRotation, bool bInWarpIKBones, const TArray<FName>& InIKBones);

	UFUNCTION(BlueprintPure, Category = "Motion Warping")
	static void GetIKBoneTransformAndAlpha(ACharacter* Character, FName BoneName, FTransform& OutTransform, float& OutAlpha);
};