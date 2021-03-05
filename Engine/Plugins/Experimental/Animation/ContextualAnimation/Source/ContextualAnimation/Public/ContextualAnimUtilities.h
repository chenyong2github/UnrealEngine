// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "Animation/AnimSequence.h"
#include "ContextualAnimUtilities.generated.h"

class UContextualAnimSceneAsset;

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

	static FTransform ExtractRootMotionFromAnimation(const UAnimSequenceBase* Animation, float StartTime, float EndTime);

	static void DrawDebugPose(const UWorld* World, const UAnimSequenceBase* Animation, float Time, const FTransform& LocalToWorldTransform, const FColor& Color, float LifeTime, float Thickness);
	static void DrawDebugScene(const UWorld* World, const UContextualAnimSceneAsset* SceneAsset, float Time, const FTransform& ToWorldTransform, const FColor& Color, float LifeTime, float Thickness);
};