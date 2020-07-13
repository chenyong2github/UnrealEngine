// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveLinkPacket.h"
#include "LiveStreamAnimationLog.h"

#include "UObject/CoreNet.h"
#include "Math/NumericLimits.h"
#include "Serialization/Archive.h"

namespace LiveStreamAnimation
{
	struct FWriteToStreamParams
	{
		class FArchive& Writer;
		const FLiveLinkPacket& InPacket;
	};

	struct FReadFromStreamParams
	{
		class FArchive& Reader;
		const FLiveStreamAnimationHandle SubjectHandle;
	};

	//~ Begin FLiveLinkPacket + Generic Serialization
	FLiveLinkPacket::~FLiveLinkPacket()
	{
	}

	void FLiveLinkPacket::WriteToStream(FArchive& InWriter, const FLiveLinkPacket& InPacket)
	{
		uint8 PacketTypeValue = static_cast<uint8>(InPacket.PacketType);
		InWriter << PacketTypeValue;
		InWriter << const_cast<FLiveStreamAnimationHandle&>(InPacket.SubjectHandle);

		if (InWriter.IsError())
		{
			return;
		}

		FWriteToStreamParams Params{InWriter, InPacket};

		switch (InPacket.PacketType)
		{
		case ELiveLinkPacketType::AddOrUpdateSubject:
			FLiveLinkAddOrUpdateSubjectPacket::WriteToStream(Params);
			break;

		case ELiveLinkPacketType::RemoveSubject:
			FLiveLinkRemoveSubjectPacket::WriteToStream(Params);
			break;

		case ELiveLinkPacketType::AnimationFrame:
			FLiveLinkAnimationFramePacket::WriteToStream(Params);
			break;

		default:
			UE_LOG(LogLiveStreamAnimation, Warning, TEXT("FLiveLinkPacket::WriteToStream: Invalid packet type %d"), static_cast<int32>(InPacket.PacketType));
			InWriter.SetError();
			break;
		}
	}

	TUniquePtr<FLiveLinkPacket> FLiveLinkPacket::ReadFromStream(FArchive& InReader)
	{
		FLiveStreamAnimationHandle Handle;
		uint8 PacketTypeValue = 0;
		InReader << PacketTypeValue;
		InReader << Handle;

		if (InReader.IsError())
		{
			return nullptr;
		}

		const ELiveLinkPacketType PacketType = static_cast<ELiveLinkPacketType>(PacketTypeValue);

		FReadFromStreamParams Params{InReader, Handle};

		switch (PacketType)
		{
		case ELiveLinkPacketType::AddOrUpdateSubject:
			return FLiveLinkAddOrUpdateSubjectPacket::ReadFromStream(Params);

		case ELiveLinkPacketType::RemoveSubject:
			return FLiveLinkRemoveSubjectPacket::ReadFromStream(Params);

		case ELiveLinkPacketType::AnimationFrame:
			return FLiveLinkAnimationFramePacket::ReadFromStream(Params);

		default:
			UE_LOG(LogLiveStreamAnimation, Warning, TEXT("FLiveLinkPacket::ReadFromStream: Invalid packet type %d"), static_cast<int32>(PacketType));
			InReader.SetError();
			return nullptr;
		}
	}
	//~ End FLiveLinkPacket + Generic Serialization.

	//~ Begin FLiveLinkAddOrUpdateSubjectPacket Serialization.
	static void SerializeSkeletonData(FArchive& InAr, FLiveLinkSkeletonStaticData& Data)
	{
		static constexpr uint32 MaxSize = static_cast<uint32>(TNumericLimits<int32>::Max());

		uint32 UnsignedArraySize = static_cast<uint32>(Data.BoneNames.Num());
		InAr.SerializeIntPacked(UnsignedArraySize);

		const int32 ArraySize = static_cast<int32>(UnsignedArraySize);
		if (UnsignedArraySize > MaxSize || ArraySize <= 0)
		{
			UE_LOG(LogLiveStreamAnimation, Warning, TEXT("SerializeSkeletonData: Invalid array size %d"), ArraySize);
			InAr.SetError();
			return;
		}

		if (InAr.IsLoading())
		{
			Data.BoneNames.SetNumUninitialized(ArraySize);
			Data.BoneParents.SetNumUninitialized(ArraySize);
		}

		for (int32 i = 0; i < ArraySize; ++i)
		{
			InAr.SerializeIntPacked(reinterpret_cast<uint32&>(Data.BoneParents[i]));
		}

		for (int32 i = 0; i < ArraySize; ++i)
		{
			InAr << Data.BoneNames[i];
		}
	}

	static bool ValidateStaticData(const FLiveLinkSkeletonStaticData& InStaticData)
	{
		if (InStaticData.BoneParents.Num() != InStaticData.BoneNames.Num())
		{
			UE_LOG(LogLiveStreamAnimation, Warning, TEXT("LiveStreamAnimation::ValidateStaticData: Invalid number of bones and parents. Bones=%d, Parents=%d"),
				InStaticData.BoneNames.Num(), InStaticData.BoneParents.Num());

			return false;
		}

		return true;
	}

	TUniquePtr<FLiveLinkAddOrUpdateSubjectPacket> FLiveLinkAddOrUpdateSubjectPacket::CreatePacket(
		const FLiveStreamAnimationHandle InSubjectHandle,
		FLiveLinkSkeletonStaticData&& InStaticData)
	{
		if (!InSubjectHandle.IsValid())
		{
			UE_LOG(LogLiveStreamAnimation, Warning, TEXT("FLiveLinkAddOrUpdateSubjectPacket::CreatePacket: Invalid subject handle."));
			return nullptr;
		}

		if (!ValidateStaticData(InStaticData))
		{
			UE_LOG(LogLiveStreamAnimation, Warning, TEXT("FLiveLinkAddOrUpdateSubjectPacket::CreatePacket: Invalid static data."));
			return nullptr;
		}

		return TUniquePtr<FLiveLinkAddOrUpdateSubjectPacket>(new FLiveLinkAddOrUpdateSubjectPacket(
			InSubjectHandle,
			MoveTemp(InStaticData)
		));
	}

	void FLiveLinkAddOrUpdateSubjectPacket::WriteToStream(FWriteToStreamParams& Params)
	{
		const FLiveLinkAddOrUpdateSubjectPacket& Packet = static_cast<const FLiveLinkAddOrUpdateSubjectPacket&>(Params.InPacket);
		SerializeSkeletonData(Params.Writer, const_cast<FLiveLinkSkeletonStaticData&>(Packet.StaticData));
	}

	TUniquePtr<FLiveLinkPacket> FLiveLinkAddOrUpdateSubjectPacket::ReadFromStream(FReadFromStreamParams& Params)
	{
		FName SubjectName;
		FLiveLinkSkeletonStaticData StaticData;
		SerializeSkeletonData(Params.Reader, StaticData);

		if (!Params.Reader.IsError())
		{
			return FLiveLinkAddOrUpdateSubjectPacket::CreatePacket(Params.SubjectHandle, MoveTemp(StaticData));
		}

		return nullptr;
	}
	//~ End FLiveLinkAddOrUpdateSubjectPacket Serialization.

	//~ Begin FLiveLinkRemoveSubjectPacketSerialization.
	TUniquePtr<FLiveLinkRemoveSubjectPacket> FLiveLinkRemoveSubjectPacket::CreatePacket(const FLiveStreamAnimationHandle InSubjectHandle)
	{
		if (!InSubjectHandle.IsValid())
		{
			UE_LOG(LogLiveStreamAnimation, Warning, TEXT("FLiveLinkRemoveSubjectPacket::CreatePacket: Invalid subject handle."));
			return nullptr;
		}

		return TUniquePtr<FLiveLinkRemoveSubjectPacket>(new FLiveLinkRemoveSubjectPacket(InSubjectHandle));
	}

	void FLiveLinkRemoveSubjectPacket::WriteToStream(FWriteToStreamParams& Params)
	{
		// Nothing extra to write, other than standard packet data.
	}

	TUniquePtr<FLiveLinkPacket> FLiveLinkRemoveSubjectPacket::ReadFromStream(FReadFromStreamParams& Params)
	{
		// Nothing extra to read, other than standard packet data.
		return FLiveLinkRemoveSubjectPacket::CreatePacket(Params.SubjectHandle);
	}
	//~ End FLiveLinkRemoveSubjectPacketSerialization

	//~ Begin FLiveLinkAnimationFramePacket Serialization.
	static void SerializeFrameData(FArchive& InAr, FLiveLinkAnimationFrameData& Data, FLiveStreamAnimationLiveLinkSourceOptions& Options)
	{
		const bool bIsLoading = InAr.IsLoading();

		uint8 PackedOptions = 0;

		if (!bIsLoading)
		{
			PackedOptions = (
				Options.bWithSceneTime << 7 |
				Options.bWithStringMetaData << 5 |
				Options.bWithPropertyValues << 4 |
				Options.bWithTransformTranslation << 3 |
				Options.bWithTransformRotation << 2 |
				Options.bWithTransformScale << 1
			);
		}

		InAr << PackedOptions;

		if (bIsLoading)
		{
			Options.bWithSceneTime = (0x1 & (PackedOptions >> 7));
			Options.bWithStringMetaData = (0x1 & (PackedOptions >> 5));
			Options.bWithPropertyValues = (0x1 & (PackedOptions >> 4));
			Options.bWithTransformTranslation = (0x1 & (PackedOptions >> 3));
			Options.bWithTransformRotation = (0x1 & (PackedOptions >> 2));
			Options.bWithTransformScale = (0x1 & (PackedOptions >> 1));
		}

		if (Options.bWithSceneTime)
		{
			FQualifiedFrameTime& SceneTime = Data.MetaData.SceneTime;

			InAr << SceneTime.Time.FrameNumber.Value;
			InAr << SceneTime.Rate.Numerator;
			InAr << SceneTime.Rate.Denominator;

			float SubFrame = SceneTime.Time.GetSubFrame();
			InAr << SubFrame;

			if (bIsLoading)
			{
				SceneTime.Time = FFrameTime(SceneTime.Time.FrameNumber, SubFrame);
			}
		}

		if (Options.bWithStringMetaData)
		{
			InAr << Data.MetaData.StringMetaData;
		}

		if (Options.bWithTransformTranslation | Options.bWithTransformRotation | Options.bWithTransformScale)
		{
			int32 NumTransforms = Data.Transforms.Num();
			InAr << NumTransforms;

			if (bIsLoading)
			{
				Data.Transforms.SetNum(NumTransforms);
			}

			// TODO: Quantization, Compression, etc.?
			FVector Translation(0.f);
			FQuat Rotation = FQuat::Identity;
			FVector Scale(1.f,1.f,1.f);
			for (FTransform& Transform : Data.Transforms)
			{
				if (bIsLoading)
				{
					if (Options.bWithTransformTranslation)
					{
						InAr << Translation;
					}
					if (Options.bWithTransformRotation)
					{
						InAr << Rotation;
					}
					if (Options.bWithTransformScale)
					{
						InAr << Scale;
					}

					Transform.SetComponents(Rotation, Translation, Scale);
				}
				else
				{
					if (Options.bWithTransformTranslation)
					{
						Translation = Transform.GetTranslation();
						InAr << Translation;
					}
					if (Options.bWithTransformRotation)
					{
						Rotation = Transform.GetRotation();
						InAr << Rotation;
					}
					if (Options.bWithTransformScale)
					{
						Scale = Transform.GetScale3D();
						InAr << Scale;
					}
				}
			}
		}

		if (Options.bWithPropertyValues)
		{
			int32 NumProperties = Data.PropertyValues.Num();
			InAr << NumProperties;

			if (bIsLoading)
			{
				Data.PropertyValues.SetNumUninitialized(NumProperties);
			}

			for (float& PropertyValue : Data.PropertyValues)
			{
				InAr << PropertyValue;
			}
		}
	}

	TUniquePtr<FLiveLinkAnimationFramePacket> FLiveLinkAnimationFramePacket::CreatePacket(
		const FLiveStreamAnimationHandle InSubjectHandle,
		const FLiveStreamAnimationLiveLinkSourceOptions InOptions,
		FLiveLinkAnimationFrameData&& InFrameData)
	{
		if (!InSubjectHandle.IsValid())
		{
			UE_LOG(LogLiveStreamAnimation, Warning, TEXT("FLiveLinkAnimationFramePacket::CreatePacket: Invalid subject handle."));
			return nullptr;
		}

		const bool bWithTransforms = InOptions.WithTransforms();
		const int32 NumTransforms = bWithTransforms ? InFrameData.Transforms.Num() : 0;
		const int32 NumProperties = InOptions.bWithPropertyValues ? InFrameData.PropertyValues.Num() : 0;

		// We need at least some data to be sent, so either (or both) property values
		// or transform data must be enabled.
		if ((NumTransforms + NumProperties) == 0)
		{
			UE_LOG(LogLiveStreamAnimation, Warning, TEXT("FLiveLinkAnimationFramePacket::CreatePacket: Must enable at least one transform component or property values"));
			return nullptr;
		}

		return TUniquePtr<FLiveLinkAnimationFramePacket>(new FLiveLinkAnimationFramePacket(
			InSubjectHandle,
			InOptions,
			MoveTemp(InFrameData)
		));
	}

	void FLiveLinkAnimationFramePacket::WriteToStream(FWriteToStreamParams& Params)
	{
		const FLiveLinkAnimationFramePacket& Packet = static_cast<const FLiveLinkAnimationFramePacket&>(Params.InPacket);
		SerializeFrameData(
			Params.Writer,
			const_cast<FLiveLinkAnimationFrameData&>(Packet.FrameData),
			const_cast<FLiveStreamAnimationLiveLinkSourceOptions&>(Packet.Options)
		);
	}

	TUniquePtr<FLiveLinkPacket> FLiveLinkAnimationFramePacket::ReadFromStream(FReadFromStreamParams& Params)
	{
		FLiveStreamAnimationLiveLinkSourceOptions Options;
		FLiveLinkAnimationFrameData FrameData;

		SerializeFrameData(Params.Reader, FrameData, Options);

		if (!Params.Reader.IsError())
		{
			return FLiveLinkAnimationFramePacket::CreatePacket(Params.SubjectHandle, Options, MoveTemp(FrameData));
		}

		return nullptr;
	}
	//~ End FLiveLinkAnimationFramePacket Serialization.
}