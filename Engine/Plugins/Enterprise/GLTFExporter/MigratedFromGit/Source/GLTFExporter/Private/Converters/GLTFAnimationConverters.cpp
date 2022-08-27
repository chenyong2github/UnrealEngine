// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFAnimationConverters.h"
#include "Builders/GLTFConvertBuilder.h"
#include "Tasks/GLTFAnimationTasks.h"
#include "Animation/AnimSequence.h"

FGLTFJsonAnimation* FGLTFAnimationConverter::Convert(FGLTFJsonNode* RootNode, const USkeletalMesh* SkeletalMesh, const UAnimSequence* AnimSequence)
{
	if (AnimSequence->GetRawNumberOfFrames() < 0)
	{
		// TODO: report warning
		return nullptr;
	}

	const USkeleton* AnimSkeleton = AnimSequence->GetSkeleton();
	if (AnimSkeleton == nullptr)
	{
		// TODO: report error
		return nullptr;
	}

#if (ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION >= 27)
	const USkeleton* MeshSkeleton = SkeletalMesh->GetSkeleton();
#else
	const USkeleton* MeshSkeleton = SkeletalMesh->Skeleton;
#endif

	if (AnimSkeleton != MeshSkeleton)
	{
		// TODO: report error
		return nullptr;
	}

	FGLTFJsonAnimation* JsonAnimation = Builder.AddAnimation();
	Builder.SetupTask<FGLTFAnimSequenceTask>(Builder, RootNode, SkeletalMesh, AnimSequence, JsonAnimation);
	return JsonAnimation;
}

FGLTFJsonAnimation* FGLTFAnimationDataConverter::Convert(FGLTFJsonNode* RootNode, const USkeletalMeshComponent* SkeletalMeshComponent)
{
	const USkeletalMesh* SkeletalMesh = SkeletalMeshComponent->SkeletalMesh;
	const UAnimSequence* AnimSequence = Cast<UAnimSequence>(SkeletalMeshComponent->AnimationData.AnimToPlay);

	if (SkeletalMesh == nullptr || AnimSequence == nullptr || SkeletalMeshComponent->GetAnimationMode() != EAnimationMode::AnimationSingleNode)
	{
		return nullptr;
	}

	FGLTFJsonAnimation* JsonAnimation = Builder.GetOrAddAnimation(RootNode, SkeletalMesh, AnimSequence);
	if (JsonAnimation != nullptr && Builder.ExportOptions->bExportPlaybackSettings)
	{
		FGLTFJsonAnimationPlayback& JsonPlayback = JsonAnimation->Playback;

		JsonPlayback.bLoop = SkeletalMeshComponent->AnimationData.bSavedLooping;
		JsonPlayback.bAutoPlay = SkeletalMeshComponent->AnimationData.bSavedPlaying;
		JsonPlayback.PlayRate = SkeletalMeshComponent->AnimationData.SavedPlayRate;
		JsonPlayback.StartTime = SkeletalMeshComponent->AnimationData.SavedPosition;
	}

	return JsonAnimation;
}

FGLTFJsonAnimation* FGLTFLevelSequenceConverter::Convert(const ULevel* Level, const ULevelSequence* LevelSequence)
{
	FGLTFJsonAnimation* JsonAnimation = Builder.AddAnimation();
	Builder.SetupTask<FGLTFLevelSequenceTask>(Builder, Level, LevelSequence, JsonAnimation);
	return JsonAnimation;
}

FGLTFJsonAnimation* FGLTFLevelSequenceDataConverter::Convert(const ALevelSequenceActor* LevelSequenceActor)
{
	const ULevel* Level = LevelSequenceActor->GetLevel();
	const ULevelSequence* LevelSequence = LevelSequenceActor->LoadSequence();

	if (Level == nullptr || LevelSequence == nullptr)
	{
		return nullptr;
	}

	FGLTFJsonAnimation* JsonAnimation = Builder.GetOrAddAnimation(Level, LevelSequence);
	if (JsonAnimation != nullptr && Builder.ExportOptions->bExportPlaybackSettings)
	{
		FGLTFJsonAnimationPlayback& JsonPlayback = JsonAnimation->Playback;

		// TODO: report warning if loop count is not 0 or -1 (infinite)
		JsonPlayback.bLoop = LevelSequenceActor->PlaybackSettings.LoopCount.Value != 0;
		JsonPlayback.bAutoPlay = LevelSequenceActor->PlaybackSettings.bAutoPlay;
		JsonPlayback.PlayRate = LevelSequenceActor->PlaybackSettings.PlayRate;
		JsonPlayback.StartTime = LevelSequenceActor->PlaybackSettings.StartTime;
	}

	return JsonAnimation;
}
