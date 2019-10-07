// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DatasmithGLTFAnimationImporter.h"

#include "GLTFAsset.h"

#include "DatasmithSceneFactory.h"
#include "IDatasmithSceneElements.h"
#include "DatasmithUtils.h"

namespace DatasmithGLTFImporterImpl
{
	EDatasmithTransformType ConvertToTransformType(GLTF::FAnimation::EPath Path)
	{
		switch (Path)
		{
			case GLTF::FAnimation::EPath::Translation:
				return EDatasmithTransformType::Translation;
			case GLTF::FAnimation::EPath::Rotation:
				return EDatasmithTransformType::Rotation;
			case GLTF::FAnimation::EPath::Scale:
				return EDatasmithTransformType::Scale;
			case GLTF::FAnimation::EPath::Weights:
			default:
				check(false);
				return EDatasmithTransformType::Translation;
		}
	}

	FDatasmithTransformFrameInfo CreateFrameInfo(float FrameNumber, const FVector& Vec)
	{
		check(FrameNumber >= 0.f);
		FrameNumber = FMath::RoundFromZero(FrameNumber);
		return FDatasmithTransformFrameInfo(FrameNumber, Vec);
	}

}

FDatasmithGLTFAnimationImporter::FDatasmithGLTFAnimationImporter(TArray<GLTF::FLogMessage>& LogMessages)
    : CurrentScene(nullptr)
    , LogMessages(LogMessages)
{
}

void FDatasmithGLTFAnimationImporter::CreateAnimations(const GLTF::FAsset& GLTFAsset)
{
	using namespace DatasmithGLTFImporterImpl;

	check(CurrentScene);

	ImportedSequences.Empty();
	for (const GLTF::FAnimation& Animation : GLTFAsset.Animations)
	{
		TSharedRef<IDatasmithLevelSequenceElement> SequenceElement = FDatasmithSceneFactory::CreateLevelSequence(*Animation.Name);

		NodeChannelMap.Empty(GLTFAsset.Nodes.Num() / 2);

		for (const GLTF::FAnimation::FChannel& Channel : Animation.Channels)
		{
			if (Channel.Target.Path != GLTF::FAnimation::EPath::Weights)
			{
				TArray<GLTF::FAnimation::FChannel>& NodeChannels = NodeChannelMap.FindOrAdd(&Channel.Target.Node);
				NodeChannels.Add(Channel);
			}
			else
				LogMessages.Emplace(GLTF::EMessageSeverity::Error, TEXT("Morph animations aren't supported: ") + Animation.Name);
		}

		const float FrameRate = SequenceElement->GetFrameRate();
		for (const auto& NodeChannelPair : NodeChannelMap)
		{
			const GLTF::FNode*                        Node     = NodeChannelPair.Get<0>();
			const TArray<GLTF::FAnimation::FChannel>& Channels = NodeChannelPair.Get<1>();

			TSharedRef<IDatasmithTransformAnimationElement> AnimationElement = FDatasmithSceneFactory::CreateTransformAnimation(*Node->Name);

			CreateAnimationFrames(Animation, Channels, FrameRate, *AnimationElement);
			SequenceElement->AddAnimation(AnimationElement);
		}

		ImportedSequences.Add(SequenceElement);
		CurrentScene->AddLevelSequence(SequenceElement);
	}
}

uint32 FDatasmithGLTFAnimationImporter::CreateAnimationFrames(const GLTF::FAnimation&                   Animation,
                                                              const TArray<GLTF::FAnimation::FChannel>& Channels,
                                                              float                                     FrameRate,
                                                              IDatasmithTransformAnimationElement&      AnimationElement)
{
	using namespace DatasmithGLTFImporterImpl;

	static_assert((int)GLTF::FAnimation::EPath::Translation == 0, "INVALID_VALUE");
	static_assert((int)GLTF::FAnimation::EPath::Rotation == 1, "INVALID_VALUE");
	static_assert((int)GLTF::FAnimation::EPath::Scale == 2, "INVALID_VALUE");
	static_assert((int)GLTF::FAnimation::EInterpolation::Linear == (int)EDatasmithCurveInterpMode::Linear, "INVALID_ENUM_VALUE");
	static_assert((int)GLTF::FAnimation::EInterpolation::Step == (int)EDatasmithCurveInterpMode::Constant, "INVALID_ENUM_VALUE");
	static_assert((int)GLTF::FAnimation::EInterpolation::CubicSpline == (int)EDatasmithCurveInterpMode::Cubic, "INVALID_ENUM_VALUE");

	uint32 FrameCount = 0;

	EDatasmithTransformChannels ActiveChannels = EDatasmithTransformChannels::None;

	bool bProcessedPath[3] = {false, false, false};
	for (const GLTF::FAnimation::FChannel& Channel : Channels)
	{
		check(bProcessedPath[(int32)Channel.Target.Path] == false);

		const GLTF::FAnimation::FSampler& Sampler = Animation.Samplers[Channel.Sampler];
		Sampler.Input.GetFloatArray(FrameTimeBuffer);

		const EDatasmithTransformType TransformType = ConvertToTransformType(Channel.Target.Path);
		AnimationElement.SetCurveInterpMode(TransformType, (EDatasmithCurveInterpMode)Sampler.Interpolation);

		int32 Index = 0;
		switch (Channel.Target.Path)
		{
			case GLTF::FAnimation::EPath::Rotation:
				// always vec4
				FrameDataBuffer.SetNumUninitialized(Sampler.Output.Count * 4);
				Sampler.Output.GetVec4Array(reinterpret_cast<FVector4*>(FrameDataBuffer.GetData()));

				for (float Time : FrameTimeBuffer)
				{
					const float* Values = &FrameDataBuffer[(Index++) * 4];

					// glTF uses a right-handed coordinate system, with Y up.
					// UE4 uses a left-handed coordinate system, with Z up.
					// Quat = (qX, qY, qZ, qW) = (sin(angle/2) * aX, sin(angle/2) * aY, sin(angle/2) * aZ, cons(angle/2))
					// where (aX, aY, aZ) - rotation axis, angle - rotation angle
					// Y swapped with Z between these coordinate systems
					// also, as handedness is changed rotation is inversed - hence negation
					// therefore QuatUE = (-qX, -qZ, -qY, qw)
					const FQuat  Quat(-Values[0], -Values[2], -Values[1], Values[3]);

					const FDatasmithTransformFrameInfo FrameInfo = CreateFrameInfo(Time * FrameRate, Quat.Euler());
					AnimationElement.AddFrame(TransformType, FrameInfo);
				}

				ActiveChannels = ActiveChannels | FDatasmithAnimationUtils::SetChannelTypeComponents(ETransformChannelComponents::All, TransformType);
				break;
			case GLTF::FAnimation::EPath::Translation:
			case GLTF::FAnimation::EPath::Scale:
				// always vec3
				FrameDataBuffer.SetNumUninitialized(Sampler.Output.Count * 3);
				Sampler.Output.GetCoordArray(reinterpret_cast<FVector*>(FrameDataBuffer.GetData()));

				if (Channel.Target.Path == GLTF::FAnimation::EPath::Translation)
				{
					for (float Time : FrameTimeBuffer)
					{
						const float*  Values = &FrameDataBuffer[(Index++) * 3];
						const FVector Vec(Values[0], Values[1], Values[2]);

						const FDatasmithTransformFrameInfo FrameInfo = CreateFrameInfo(Time * FrameRate, Vec * ScaleFactor);
						AnimationElement.AddFrame(TransformType, FrameInfo);
					}
				}
				else
				{
					for (float Time : FrameTimeBuffer)
					{
						const float*  Values = &FrameDataBuffer[(Index++) * 3];
						const FVector Vec(Values[0], Values[1], Values[2]);

						const FDatasmithTransformFrameInfo FrameInfo = CreateFrameInfo(Time * FrameRate, Vec);
						AnimationElement.AddFrame(TransformType, FrameInfo);
					}
				}

				ActiveChannels = ActiveChannels | FDatasmithAnimationUtils::SetChannelTypeComponents(ETransformChannelComponents::All, TransformType);
				break;
			default:
				check(false);
				break;
		}

		FrameCount                                 = FMath::Max(Sampler.Input.Count, FrameCount);
		bProcessedPath[(int32)Channel.Target.Path] = true;
	}

	AnimationElement.SetEnabledTransformChannels(ActiveChannels);

	return FrameCount;
}
