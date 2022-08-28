// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FMovieSceneDoubleChannel;
class UMovieScene3DTransformSection;

struct FGLTFBoneUtility
{
	static FTransform GetBindTransform(const FReferenceSkeleton& RefSkeleton, int32 BoneIndex);

	static void GetFrameTimestamps(const UAnimSequence* AnimSequence, TArray<float>& OutFrameTimestamps);

	static void GetBoneIndices(const USkeleton* Skeleton, TArray<FBoneIndexType>& OutBoneIndices);

	static void GetBoneTransformsByFrame(const UAnimSequence* AnimSequence, const TArray<float>& FrameTimestamps, const TArray<FBoneIndexType>& BoneIndices, TArray<TArray<FTransform>>& OutBoneTransformsByFrame);

	static const FMovieSceneDoubleChannel* GetTranslationChannels(const UMovieScene3DTransformSection* TransformSection);
	static const FMovieSceneDoubleChannel* GetRotationChannels(const UMovieScene3DTransformSection* TransformSection);
	static const FMovieSceneDoubleChannel* GetScaleChannels(const UMovieScene3DTransformSection* TransformSection);
};
