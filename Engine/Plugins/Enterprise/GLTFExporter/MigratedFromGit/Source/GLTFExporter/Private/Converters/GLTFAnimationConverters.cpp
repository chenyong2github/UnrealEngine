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

	const USkeleton* AnimSkeleton = AnimSequence->GetSkeleton();
	if (AnimSkeleton == nullptr)
	{
		// TODO: report error
		return FGLTFJsonAnimationIndex(INDEX_NONE);
	}

#if (ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION >= 27)
	const USkeleton* MeshSkeleton = SkeletalMesh->GetSkeleton();
#else
	const USkeleton* MeshSkeleton = SkeletalMesh->Skeleton;
#endif

	if (AnimSkeleton != MeshSkeleton)
	{
		// TODO: report error
		return FGLTFJsonAnimationIndex(INDEX_NONE);
	}

	FGLTFJsonAnimation* JsonAnimation = Builder.AddAnimation();
	Builder.SetupTask<FGLTFAnimSequenceTask>(Builder, RootNode, SkeletalMesh, AnimSequence, JsonAnimation);
	return JsonAnimation->Index;
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
	if (AnimationIndex != INDEX_NONE && Builder.ExportOptions->bExportPlaybackSettings)
	{
		FGLTFJsonAnimation& JsonAnimation = Builder.GetAnimation(AnimationIndex);
		FGLTFJsonAnimationPlayback& JsonPlayback = JsonAnimation.Playback;

		JsonPlayback.bLoop = SkeletalMeshComponent->AnimationData.bSavedLooping;
		JsonPlayback.bAutoPlay = SkeletalMeshComponent->AnimationData.bSavedPlaying;
		JsonPlayback.PlayRate = SkeletalMeshComponent->AnimationData.SavedPlayRate;
		JsonPlayback.StartTime = SkeletalMeshComponent->AnimationData.SavedPosition;
	}

	return AnimationIndex;
}

FGLTFJsonAnimationIndex FGLTFLevelSequenceConverter::Convert(const ULevel* Level, const ULevelSequence* LevelSequence)
{
	FGLTFJsonAnimation* JsonAnimation = Builder.AddAnimation();
	Builder.SetupTask<FGLTFLevelSequenceTask>(Builder, Level, LevelSequence, JsonAnimation);
	return JsonAnimation->Index;
}

FGLTFJsonAnimationIndex FGLTFLevelSequenceDataConverter::Convert(const ALevelSequenceActor* LevelSequenceActor)
{
	const ULevel* Level = LevelSequenceActor->GetLevel();
	const ULevelSequence* LevelSequence = LevelSequenceActor->LoadSequence();

	if (Level == nullptr || LevelSequence == nullptr)
	{
		return FGLTFJsonAnimationIndex(INDEX_NONE);
	}

	const FGLTFJsonAnimationIndex AnimationIndex = Builder.GetOrAddAnimation(Level, LevelSequence);
	if (AnimationIndex != INDEX_NONE && Builder.ExportOptions->bExportPlaybackSettings)
	{
		FGLTFJsonAnimation& JsonAnimation = Builder.GetAnimation(AnimationIndex);
		FGLTFJsonAnimationPlayback& JsonPlayback = JsonAnimation.Playback;

		// TODO: report warning if loop count is not 0 or -1 (infinite)
		JsonPlayback.bLoop = LevelSequenceActor->PlaybackSettings.LoopCount.Value != 0;
		JsonPlayback.bAutoPlay = LevelSequenceActor->PlaybackSettings.bAutoPlay;
		JsonPlayback.PlayRate = LevelSequenceActor->PlaybackSettings.PlayRate;
		JsonPlayback.StartTime = LevelSequenceActor->PlaybackSettings.StartTime;
	}

	return AnimationIndex;
}
