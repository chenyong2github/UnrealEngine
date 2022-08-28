// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFAnimationConverters.h"
#include "Converters/GLTFConverterUtility.h"
#include "Builders/GLTFConvertBuilder.h"
#include "Animation/AnimSequence.h"

FGLTFJsonAnimationIndex FGLTFAnimationConverter::Convert(FGLTFJsonNodeIndex RootNode, const USkeletalMesh* SkeletalMesh, const UAnimSequence* AnimSequence)
{
	const int32 FrameCount = AnimSequence->GetRawNumberOfFrames();
	if (FrameCount < 0)
	{
		// TODO: report warning
		return FGLTFJsonAnimationIndex(INDEX_NONE);
	}

	USkeleton* Skeleton = AnimSequence->GetSkeleton();
	if (Skeleton == nullptr)
	{
		// TODO: report error
		return FGLTFJsonAnimationIndex(INDEX_NONE);
	}

	FGLTFJsonAnimation JsonAnimation;
	AnimSequence->GetName(JsonAnimation.Name);

	TArray<float> Timestamps;
	Timestamps.AddUninitialized(FrameCount);

	for (int32 Frame = 0; Frame < FrameCount; ++Frame)
	{
		Timestamps[Frame] = AnimSequence->GetTimeAtFrame(Frame);
	}

	FGLTFJsonAccessor JsonInputAccessor;
	JsonInputAccessor.BufferView = Builder.AddBufferView(Timestamps);
	JsonInputAccessor.ComponentType = EGLTFJsonComponentType::F32;
	JsonInputAccessor.Type = EGLTFJsonAccessorType::Scalar;
	JsonInputAccessor.MinMaxLength = 1;
	JsonInputAccessor.Min[0] = 0;

	const EGLTFJsonInterpolation Interpolation = FGLTFConverterUtility::ConvertInterpolation(AnimSequence->Interpolation);
	const TArray<FName>& TrackNames = AnimSequence->GetAnimationTrackNames();
	const int32 TrackCount = TrackNames.Num();

	for (int32 TrackIndex = 0; TrackIndex < TrackCount; ++TrackIndex)
	{
		const FRawAnimSequenceTrack& Track = AnimSequence->GetRawAnimationTrack(TrackIndex);

		const int32 SkeletonBoneIndex = AnimSequence->GetSkeletonIndexFromRawDataTrackIndex(TrackIndex);
		const int32 BoneIndex = Skeleton->GetMeshBoneIndexFromSkeletonBoneIndex(SkeletalMesh, SkeletonBoneIndex);
		const FGLTFJsonNodeIndex NodeIndex = Builder.GetOrAddNode(RootNode, SkeletalMesh, BoneIndex);

		const TArray<FVector>& PosKeys = Track.PosKeys;
		if (PosKeys.Num() > 0)
		{
			TArray<FGLTFJsonVector3> Translations;
			Translations.AddUninitialized(PosKeys.Num());

			for (int32 Index = 0; Index < PosKeys.Num(); ++Index)
			{
				Translations[Index] = FGLTFConverterUtility::ConvertPosition(PosKeys[Index], Builder.ExportOptions->ExportScale);
			}

			JsonInputAccessor.Count = Translations.Num();
			JsonInputAccessor.Max[0] = Timestamps[Translations.Num() - 1];

			FGLTFJsonAccessor JsonOutputAccessor;
			JsonOutputAccessor.BufferView = Builder.AddBufferView(Translations);
			JsonOutputAccessor.ComponentType = EGLTFJsonComponentType::F32;
			JsonOutputAccessor.Count = Translations.Num();
			JsonOutputAccessor.Type = EGLTFJsonAccessorType::Vec3;

			FGLTFJsonAnimationSampler JsonSampler;
			JsonSampler.Input = Builder.AddAccessor(JsonInputAccessor);
			JsonSampler.Output = Builder.AddAccessor(JsonOutputAccessor);
			JsonSampler.Interpolation = Interpolation;

			FGLTFJsonAnimationChannel JsonChannel;
			JsonChannel.Sampler = FGLTFJsonAnimationSamplerIndex(JsonAnimation.Samplers.Add(JsonSampler));
			JsonChannel.Target.Path = EGLTFJsonTargetPath::Translation;
			JsonChannel.Target.Node = NodeIndex;

			JsonAnimation.Channels.Add(JsonChannel);
		}

		const TArray<FQuat>& RotKeys = Track.RotKeys;
		if (RotKeys.Num() > 0)
		{
			TArray<FGLTFJsonQuaternion> Rotations;
			Rotations.AddUninitialized(RotKeys.Num());

			for (int32 Index = 0; Index < RotKeys.Num(); ++Index)
			{
				Rotations[Index] = FGLTFConverterUtility::ConvertRotation(RotKeys[Index]);
			}

			JsonInputAccessor.Count = Rotations.Num();
			JsonInputAccessor.Max[0] = Timestamps[Rotations.Num() - 1];

			FGLTFJsonAccessor JsonOutputAccessor;
			JsonOutputAccessor.BufferView = Builder.AddBufferView(Rotations);
			JsonOutputAccessor.ComponentType = EGLTFJsonComponentType::F32;
			JsonOutputAccessor.Count = Rotations.Num();
			JsonOutputAccessor.Type = EGLTFJsonAccessorType::Vec4;

			FGLTFJsonAnimationSampler JsonSampler;
			JsonSampler.Input = Builder.AddAccessor(JsonInputAccessor);;
			JsonSampler.Output = Builder.AddAccessor(JsonOutputAccessor);
			JsonSampler.Interpolation = Interpolation;

			FGLTFJsonAnimationChannel JsonChannel;
			JsonChannel.Sampler = FGLTFJsonAnimationSamplerIndex(JsonAnimation.Samplers.Add(JsonSampler));
			JsonChannel.Target.Path = EGLTFJsonTargetPath::Rotation;
			JsonChannel.Target.Node = NodeIndex;

			JsonAnimation.Channels.Add(JsonChannel);
		}

		const TArray<FVector>& ScaleKeys = Track.ScaleKeys;
		if (ScaleKeys.Num() > 0)
		{
			TArray<FGLTFJsonVector3> Scales;
			Scales.AddUninitialized(ScaleKeys.Num());

			for (int32 Index = 0; Index < ScaleKeys.Num(); ++Index)
			{
				Scales[Index] = FGLTFConverterUtility::ConvertScale(ScaleKeys[Index]);
			}

			JsonInputAccessor.Count = Scales.Num();
			JsonInputAccessor.Max[0] = Timestamps[Scales.Num() - 1];

			FGLTFJsonAccessor JsonOutputAccessor;
			JsonOutputAccessor.BufferView = Builder.AddBufferView(Scales);
			JsonOutputAccessor.ComponentType = EGLTFJsonComponentType::F32;
			JsonOutputAccessor.Count = Scales.Num();
			JsonOutputAccessor.Type = EGLTFJsonAccessorType::Vec3;

			FGLTFJsonAnimationSampler JsonSampler;
			JsonSampler.Input = Builder.AddAccessor(JsonInputAccessor);
			JsonSampler.Output = Builder.AddAccessor(JsonOutputAccessor);
			JsonSampler.Interpolation = Interpolation;

			FGLTFJsonAnimationChannel JsonChannel;
			JsonChannel.Sampler = FGLTFJsonAnimationSamplerIndex(JsonAnimation.Samplers.Add(JsonSampler));
			JsonChannel.Target.Path = EGLTFJsonTargetPath::Scale;
			JsonChannel.Target.Node = NodeIndex;

			JsonAnimation.Channels.Add(JsonChannel);
		}
	}

	return Builder.AddAnimation(JsonAnimation);
}
