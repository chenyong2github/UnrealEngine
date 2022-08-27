// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFAnimationConverters.h"
#include "Builders/GLTFConvertBuilder.h"
#include "Tasks/GLTFAnimationTasks.h"
#include "Animation/AnimSequence.h"

FGLTFJsonAnimationIndex FGLTFAnimationConverter::Convert(FGLTFJsonNodeIndex RootNode, const USkeletalMesh* SkeletalMesh, const UAnimSequence* AnimSequence)
{
	if (AnimSequence->GetRawNumberOfFrames() < 0)
	{
		// TODO: report warning
		return FGLTFJsonAnimationIndex(INDEX_NONE);
	}

	const USkeleton* Skeleton = AnimSequence->GetSkeleton();
	if (Skeleton == nullptr)
	{
		// TODO: report error
		return FGLTFJsonAnimationIndex(INDEX_NONE);
	}

	if (Skeleton != SkeletalMesh->Skeleton)
	{
		// TODO: report error
		return FGLTFJsonAnimationIndex(INDEX_NONE);
	}

	const FGLTFJsonAnimationIndex AnimationIndex = Builder.AddAnimation();
	Builder.SetupTask<FGLTFAnimSequenceTask>(Builder, RootNode, SkeletalMesh, AnimSequence, AnimationIndex);
	return AnimationIndex;
}

FGLTFJsonAnimationIndex FGLTFAnimationDataConverter::Convert(FGLTFJsonNodeIndex RootNode, const USkeletalMeshComponent* SkeletalMeshComponent)
{
	const USkeletalMesh* SkeletalMesh = SkeletalMeshComponent->SkeletalMesh;
	const UAnimSequence* AnimSequence = Cast<UAnimSequence>(SkeletalMeshComponent->AnimationData.AnimToPlay);

	if (SkeletalMesh == nullptr || AnimSequence == nullptr || SkeletalMeshComponent->GetAnimationMode() != EAnimationMode::AnimationSingleNode)
	{
		return FGLTFJsonAnimationIndex(INDEX_NONE);
	}

	const FGLTFJsonAnimationIndex AnimationIndex = Builder.GetOrAddAnimation(RootNode, SkeletalMesh, AnimSequence);
	if (AnimationIndex != INDEX_NONE)
	{
		FGLTFJsonAnimation& JsonAnimation = Builder.GetAnimation(AnimationIndex);
		FGLTFJsonPlayData& JsonPlayData = JsonAnimation.PlayData;

		JsonPlayData.Looping = SkeletalMeshComponent->AnimationData.bSavedLooping;
		JsonPlayData.Playing = SkeletalMeshComponent->AnimationData.bSavedPlaying;
		JsonPlayData.PlayRate = SkeletalMeshComponent->AnimationData.SavedPlayRate;
		JsonPlayData.Position = SkeletalMeshComponent->AnimationData.SavedPosition;
	}

	return AnimationIndex;
}

FGLTFJsonAnimationIndex FGLTFLevelSequenceConverter::Convert(const ALevelSequenceActor* LevelSequenceActor, const ULevelSequence* LevelSequence)
{
	FGLTFJsonAnimation JsonAnimation;

	if (LevelSequence == nullptr)
	{
		LevelSequence = LevelSequenceActor->LoadSequence();
		if (LevelSequence == nullptr)
		{
			return FGLTFJsonAnimationIndex(INDEX_NONE);
		}

		FGLTFJsonPlayData& JsonPlayData = JsonAnimation.PlayData;
		JsonPlayData.Looping = LevelSequenceActor->PlaybackSettings.LoopCount.Value != 0;
		JsonPlayData.Playing = LevelSequenceActor->PlaybackSettings.bAutoPlay;
		JsonPlayData.PlayRate = LevelSequenceActor->PlaybackSettings.PlayRate;
		JsonPlayData.Position = LevelSequenceActor->PlaybackSettings.StartTime;
	}

	const FGLTFJsonAnimationIndex AnimationIndex = Builder.AddAnimation(JsonAnimation);
	Builder.SetupTask<FGLTFLevelSequenceTask>(Builder, LevelSequenceActor, LevelSequence, AnimationIndex);
	return AnimationIndex;
}
