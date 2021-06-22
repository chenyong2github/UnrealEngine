// Copyright Epic Games, Inc. All Rights Reserved.

#include "CodecPacket.h"
#include "CoreMinimal.h"
#include "VideoEncoderCommon.h"


namespace AVEncoder
{

FCodecPacketImpl::~FCodecPacketImpl()
{
	FScopeLock		Guard(&ProtectClone);
	if (MyClone)
	{
		MyClone->ReleaseClone();
	}
}

const FCodecPacket* FCodecPacketImpl::Clone() const
{
	FScopeLock		Guard(&ProtectClone);
	if (!MyClone)
	{
		FClone* NewClone = new FClone();
		NewClone->Copy(*this);
		MyClone = NewClone;
	}
	return MyClone->Clone();
}

void FCodecPacketImpl::ReleaseClone() const
{
	UE_LOG(LogVideoEncoder, Error, TEXT("Can't release original FCodecPacket!"));
	check(false);
}


FCodecPacketImpl::FClone::~FClone()
{
	FMemory::Free(const_cast<uint8*>(Data));
}

void FCodecPacketImpl::FClone::Copy(const FCodecPacketImpl& InOriginal)
{
	DataSize = InOriginal.DataSize;
	Data = static_cast<const uint8*>(FMemory::Malloc(DataSize));
	FMemory::BigBlockMemcpy(const_cast<uint8*>(Data), InOriginal.Data, DataSize);
	IsKeyFrame = InOriginal.IsKeyFrame;
}

const FCodecPacket* FCodecPacketImpl::FClone::Clone() const
{
	RefCounter.Increment();
	return this;
}

void FCodecPacketImpl::FClone::ReleaseClone() const
{
	if (RefCounter.Decrement() == 0)
	{
		delete const_cast<FClone*>(this);
	}
}


} /* namespace AVEncoder */