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

	static void ExtractLocalSpacePose(const UAnimSequenceBase* Animation, const FBoneContainer& BoneContainer, float Time, bool bExtractRootMotion, FCompactPose& OutPose);
	static void ExtractComponentSpacePose(const UAnimSequenceBase* Animation, const FBoneContainer& BoneContainer, float Time, bool bExtractRootMotion, FCSPose<FCompactPose>& OutPose);
	static FTransform ExtractRootMotionFromAnimation(const UAnimSequenceBase* Animation, float StartTime, float EndTime);

	static void DrawDebugPose(const UWorld* World, const UAnimSequenceBase* Animation, float Time, const FTransform& LocalToWorldTransform, const FColor& Color, float LifeTime, float Thickness);
	static void DrawDebugScene(const UWorld* World, const UContextualAnimSceneAsset* SceneAsset, float Time, const FTransform& ToWorldTransform, const FColor& Color, float LifeTime, float Thickness);
};