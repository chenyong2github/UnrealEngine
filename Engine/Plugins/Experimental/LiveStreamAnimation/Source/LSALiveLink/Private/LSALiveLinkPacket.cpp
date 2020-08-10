// Copyright Epic Games, Inc. All Rights Reserved.

#include "LSALiveLinkPacket.h"
#include "LSALiveLinkLog.h"

#include "UObject/CoreNet.h"
#include "Math/NumericLimits.h"
#include "Serialization/Archive.h"

struct FWriteToStreamParams
{
	class FArchive& Writer;
	const FLSALiveLinkPacket& InPacket;
};

struct FReadFromStreamParams
{
	class FArchive& Reader;
	const FLiveStreamAnimationHandle SubjectHandle;
};

//~ Begin FLSALiveLinkPacket + Generic Serialization
FLSALiveLinkPacket::~FLSALiveLinkPacket()
{
}

void FLSALiveLinkPacket::WriteToStream(FArchive& InWriter, const FLSALiveLinkPacket& InPacket)
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
	case ELSALiveLinkPacketType::AddOrUpdateSubject:
		FLSALiveLinkAddOrUpdateSubjectPacket::WriteToStream(Params);
		break;

	case ELSALiveLinkPacketType::RemoveSubject:
		FLSALiveLinkRemoveSubjectPacket::WriteToStream(Params);
		break;

	case ELSALiveLinkPacketType::AnimationFrame:
		FLSALiveLinkAnimationFramePacket::WriteToStream(Params);
		break;

	default:
		UE_LOG(LogLSALiveLink, Warning, TEXT("FLSALiveLinkPacket::WriteToStream: Invalid packet type %d"), static_cast<int32>(InPacket.PacketType));
		InWriter.SetError();
		break;
	}
}

TUniquePtr<FLSALiveLinkPacket> FLSALiveLinkPacket::ReadFromStream(FArchive& InReader)
{
	FLiveStreamAnimationHandle Handle;
	uint8 PacketTypeValue = 0;
	InReader << PacketTypeValue;
	InReader << Handle;

	if (InReader.IsError())
	{
		return nullptr;
	}

	const ELSALiveLinkPacketType PacketType = static_cast<ELSALiveLinkPacketType>(PacketTypeValue);

	FReadFromStreamParams Params{InReader, Handle};

	switch (PacketType)
	{
	case ELSALiveLinkPacketType::AddOrUpdateSubject:
		return FLSALiveLinkAddOrUpdateSubjectPacket::ReadFromStream(Params);

	case ELSALiveLinkPacketType::RemoveSubject:
		return FLSALiveLinkRemoveSubjectPacket::ReadFromStream(Params);

	case ELSALiveLinkPacketType::AnimationFrame:
		return FLSALiveLinkAnimationFramePacket::ReadFromStream(Params);

	default:
		UE_LOG(LogLSALiveLink, Warning, TEXT("FLSALiveLinkPacket::ReadFromStream: Invalid packet type %d"), static_cast<int32>(PacketType));
		InReader.SetError();
		return nullptr;
	}
}
//~ End FLSALiveLinkPacket + Generic Serialization.

//~ Begin FLSALiveLinkAddOrUpdateSubjectPacket Serialization.
static void SerializeStaticData(FArchive& InAr, FLSALiveLinkStaticData& Data)
{
	static constexpr uint32 MaxSize = static_cast<uint32>(TNumericLimits<int32>::Max());

	uint32 UnsignedArraySize = static_cast<uint32>(Data.BoneNames.Num());
	InAr.SerializeIntPacked(UnsignedArraySize);

	const int32 ArraySize = static_cast<int32>(UnsignedArraySize);
	if (UnsignedArraySize > MaxSize || ArraySize <= 0)
	{
		UE_LOG(LogLSALiveLink, Warning, TEXT("SerializeStaticData: Invalid array size %d"), ArraySize);
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
		UE_LOG(LogLSALiveLink, Warning, TEXT("LiveStreamAnimation::ValidateStaticData: Invalid number of bones and parents. Bones=%d, Parents=%d"),
			InStaticData.BoneNames.Num(), InStaticData.BoneParents.Num());

		return false;
	}

	return true;
}

TUniquePtr<FLSALiveLinkAddOrUpdateSubjectPacket> FLSALiveLinkAddOrUpdateSubjectPacket::CreatePacket(
	const FLiveStreamAnimationHandle InSubjectHandle,
	FLSALiveLinkStaticData&& InStaticData)
{
	if (!InSubjectHandle.IsValid())
	{
		UE_LOG(LogLSALiveLink, Warning, TEXT("FLSALiveLinkAddOrUpdateSubjectPacket::CreatePacket: Invalid subject handle."));
		return nullptr;
	}

	if (!ValidateStaticData(InStaticData))
	{
		UE_LOG(LogLSALiveLink, Warning, TEXT("FLSALiveLinkAddOrUpdateSubjectPacket::CreatePacket: Invalid static data."));
		return nullptr;
	}

	return TUniquePtr<FLSALiveLinkAddOrUpdateSubjectPacket>(new FLSALiveLinkAddOrUpdateSubjectPacket(
		InSubjectHandle,
		MoveTemp(InStaticData)
	));
}

void FLSALiveLinkAddOrUpdateSubjectPacket::WriteToStream(FWriteToStreamParams& Params)
{
	const FLSALiveLinkAddOrUpdateSubjectPacket& Packet = static_cast<const FLSALiveLinkAddOrUpdateSubjectPacket&>(Params.InPacket);
	SerializeStaticData(Params.Writer, const_cast<FLSALiveLinkStaticData&>(Packet.StaticData));
}

TUniquePtr<FLSALiveLinkPacket> FLSALiveLinkAddOrUpdateSubjectPacket::ReadFromStream(FReadFromStreamParams& Params)
{
	FName SubjectName;
	FLSALiveLinkStaticData StaticData;
	SerializeStaticData(Params.Reader, StaticData);

	if (!Params.Reader.IsError())
	{
		return FLSALiveLinkAddOrUpdateSubjectPacket::CreatePacket(Params.SubjectHandle, MoveTemp(StaticData));
	}

	return nullptr;
}
//~ End FLSALiveLinkAddOrUpdateSubjectPacket Serialization.

//~ Begin FLSALiveLinkRemoveSubjectPacketSerialization.
TUniquePtr<FLSALiveLinkRemoveSubjectPacket> FLSALiveLinkRemoveSubjectPacket::CreatePacket(const FLiveStreamAnimationHandle InSubjectHandle)
{
	if (!InSubjectHandle.IsValid())
	{
		UE_LOG(LogLSALiveLink, Warning, TEXT("FLSALiveLinkRemoveSubjectPacket::CreatePacket: Invalid subject handle."));
		return nullptr;
	}

	return TUniquePtr<FLSALiveLinkRemoveSubjectPacket>(new FLSALiveLinkRemoveSubjectPacket(InSubjectHandle));
}

void FLSALiveLinkRemoveSubjectPacket::WriteToStream(FWriteToStreamParams& Params)
{
	// Nothing extra to write, other than standard packet data.
}

TUniquePtr<FLSALiveLinkPacket> FLSALiveLinkRemoveSubjectPacket::ReadFromStream(FReadFromStreamParams& Params)
{
	// Nothing extra to read, other than standard packet data.
	return FLSALiveLinkRemoveSubjectPacket::CreatePacket(Params.SubjectHandle);
}
//~ End FLSALiveLinkRemoveSubjectPacketSerialization

//~ Begin FLSALiveLinkAnimationFramePacket Serialization.
static void SerializeFrameData(FArchive& InAr, FLSALiveLinkFrameData& Data)
{
	const bool bIsLoading = InAr.IsLoading();

	uint8 PackedOptions = 0;

	// TODO: Both options and translation profile should probably be sent with skeleton data instead to
	//			save bandwidth, since they aren't going to change from frame to frame.
	//			However, at a minimum we'd need to track a subject data version, so we could throw
	//			out stale packets that had data we could no longer process if an update occurred
	//			that changed those settings.

	// TODO: It would also be nice just to use a BitWriter / BitReader here, but that would require
	//			some additional fixup in the FForwardingChannels plugin.

	if (!bIsLoading)
	{
		const bool bIsTranslationProfileValid = Data.TranslationProfileHandle.IsValid();

		PackedOptions = (
			Data.Options.bWithSceneTime << 7 |
			Data.Options.bWithStringMetaData << 5 |
			Data.Options.bWithPropertyValues << 4 |
			Data.Options.bWithTransformTranslation << 3 |
			Data.Options.bWithTransformRotation << 2 |
			Data.Options.bWithTransformScale << 1 |
			(bIsTranslationProfileValid ? 1 : 0)
		);
	}

	InAr << PackedOptions;

	if (bIsLoading)
	{
		Data.Options.bWithSceneTime = (0x1 & (PackedOptions >> 7));
		Data.Options.bWithStringMetaData = (0x1 & (PackedOptions >> 5));
		Data.Options.bWithPropertyValues = (0x1 & (PackedOptions >> 4));
		Data.Options.bWithTransformTranslation = (0x1 & (PackedOptions >> 3));
		Data.Options.bWithTransformRotation = (0x1 & (PackedOptions >> 2));
		Data.Options.bWithTransformScale = (0x1 & (PackedOptions >> 1));
			
	}

	const bool bIsTranslationProfileValid = (0x1 & (PackedOptions)) != 0;
	if (bIsTranslationProfileValid)
	{
		InAr << Data.TranslationProfileHandle;
	}

	if (Data.Options.bWithSceneTime && !InAr.IsError())
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

	if (Data.Options.bWithStringMetaData && !InAr.IsError())
	{
		InAr << Data.MetaData.StringMetaData;
	}

	if (Data.Options.WithTransforms() && !InAr.IsError())
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
				if (Data.Options.bWithTransformTranslation)
				{
					InAr << Translation;
				}
				if (Data.Options.bWithTransformRotation)
				{
					InAr << Rotation;
				}
				if (Data.Options.bWithTransformScale)
				{
					InAr << Scale;
				}

				Transform.SetComponents(Rotation, Translation, Scale);
			}
			else
			{
				if (Data.Options.bWithTransformTranslation)
				{
					Translation = Transform.GetTranslation();
					InAr << Translation;
				}
				if (Data.Options.bWithTransformRotation)
				{
					Rotation = Transform.GetRotation();
					InAr << Rotation;
				}
				if (Data.Options.bWithTransformScale)
				{
					Scale = Transform.GetScale3D();
					InAr << Scale;
				}
			}
		}
	}

	if (Data.Options.bWithPropertyValues && !InAr.IsError())
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

TUniquePtr<FLSALiveLinkAnimationFramePacket> FLSALiveLinkAnimationFramePacket::CreatePacket(
	const FLiveStreamAnimationHandle InSubjectHandle,
	FLSALiveLinkFrameData&& InFrameData)
{
	if (!InSubjectHandle.IsValid())
	{
		UE_LOG(LogLSALiveLink, Warning, TEXT("FLSALiveLinkAnimationFramePacket::CreatePacket: Invalid subject handle."));
		return nullptr;
	}

	const bool bWithTransforms = InFrameData.Options.WithTransforms();
	const int32 NumTransforms = bWithTransforms ? InFrameData.Transforms.Num() : 0;
	const int32 NumProperties = InFrameData.Options.bWithPropertyValues ? InFrameData.PropertyValues.Num() : 0;

	// We need at least some data to be sent, so either (or both) property values
	// or transform data must be enabled.
	if ((NumTransforms + NumProperties) == 0)
	{
		UE_LOG(LogLSALiveLink, Warning, TEXT("FLSALiveLinkAnimationFramePacket::CreatePacket: Must enable at least one transform component or property values"));
		return nullptr;
	}

	return TUniquePtr<FLSALiveLinkAnimationFramePacket>(new FLSALiveLinkAnimationFramePacket(
		InSubjectHandle,
		MoveTemp(InFrameData)
	));
}

void FLSALiveLinkAnimationFramePacket::WriteToStream(FWriteToStreamParams& Params)
{
	const FLSALiveLinkAnimationFramePacket& Packet = static_cast<const FLSALiveLinkAnimationFramePacket&>(Params.InPacket);
	SerializeFrameData(
		Params.Writer,
		const_cast<FLSALiveLinkFrameData&>(Packet.FrameData)
	);
}

TUniquePtr<FLSALiveLinkPacket> FLSALiveLinkAnimationFramePacket::ReadFromStream(FReadFromStreamParams& Params)
{
	FLSALiveLinkFrameData FrameData;
	SerializeFrameData(Params.Reader, FrameData);

	if (!Params.Reader.IsError())
	{
		return FLSALiveLinkAnimationFramePacket::CreatePacket(Params.SubjectHandle, MoveTemp(FrameData));
	}

	return nullptr;
}
//~ End FLSALiveLinkAnimationFramePacket Serialization.