// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "Animation/AnimSequence.h"
#include "ContextualAnimUtilities.generated.h"

class UContextualAnimSceneAsset;
class USkeletalMeshComponent;
class UAnimInstance;
class AActor;

UCLASS()
class CONTEXTUALANIMATION_API UContextualAnimUtilities : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/** 
	 * Helper function to extract local space pose from an animation at a given time.
	 * If the supplied animation is a montage it will extract the pose from the first track
	 * IMPORTANT: This function expects you to add a MemMark (FMemMark Mark(FMemStack::Get());) at the correct scope if you are using it from outside world's tick
	 */
	static void ExtractLocalSpacePose(const UAnimSequenceBase* Animation, const FBoneContainer& BoneContainer, float Time, bool bExtractRootMotion, FCompactPose& OutPose);

	/**
	 * Helper function to extract component space pose from an animation at a given time
     * If the supplied animation is a montage it will extract the pose from the first track
	 * IMPORTANT: This function expects you to add a MemMark (FMemMark Mark(FMemStack::Get());) at the correct scope if you are using it from outside world's tick
	 */
	static void ExtractComponentSpacePose(const UAnimSequenceBase* Animation, const FBoneContainer& BoneContainer, float Time, bool bExtractRootMotion, FCSPose<FCompactPose>& OutPose);

	/** Extract Root Motion transform from a contiguous position range */
	static FTransform ExtractRootMotionFromAnimation(const UAnimSequenceBase* Animation, float StartTime, float EndTime);

	/** Extract root bone transform at a given time */
	static FTransform ExtractRootTransformFromAnimation(const UAnimSequenceBase* Animation, float Time);

	static void DrawDebugPose(const UWorld* World, const UAnimSequenceBase* Animation, float Time, const FTransform& LocalToWorldTransform, const FColor& Color, float LifeTime, float Thickness);
	
	static void DrawDebugScene(const UWorld* World, const UContextualAnimSceneAsset* SceneAsset, int32 AnimDataIndex, float Time, const FTransform& ToWorldTransform, const FColor& Color, float LifeTime, float Thickness);

	static USkeletalMeshComponent* TryGetSkeletalMeshComponent(AActor* Actor);

	static UAnimInstance* TryGetAnimInstance(AActor* Actor);

	UFUNCTION(BlueprintCallable, Category = "Contextual Anim|Utilities", meta = (DisplayName = "GetSectionStartAndEndTime"))
	static void BP_Montage_GetSectionStartAndEndTime(const UAnimMontage* Montage, int32 SectionIndex, float& OutStartTime, float& OutEndTime);

	UFUNCTION(BlueprintCallable, Category = "Contextual Anim|Utilities", meta = (DisplayName = "GetSectionTimeLeftFromPos"))
	static float BP_Montage_GetSectionTimeLeftFromPos(const UAnimMontage* Montage, float Position);

	UFUNCTION(BlueprintCallable, Category = "Contextual Anim|Utilities", meta = (DisplayName = "GetSectionLength"))
	static float BP_Montage_GetSectionLength(const UAnimMontage* Montage, int32 SectionIndex);
};