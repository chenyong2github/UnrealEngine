// Copyright Epic Games, Inc. All Rights Reserved.
 
#include "LidarPointCloudShared.h"
#include "LidarPointCloud.h"

double FBenchmarkTimer::Time = 0;

FArchive& operator<<(FArchive& Ar, FLidarPointCloudPoint& P)
{
	Ar << P.Location << P.Color;

	if (Ar.CustomVer(ULidarPointCloud::PointCloudFileGUID) > 8)
	{
		uint8 bVisible = P.bVisible;
		Ar << bVisible;
		P.bVisible = bVisible;
	}

	if (Ar.CustomVer(ULidarPointCloud::PointCloudFileGUID) > 12)
	{
		uint8 ClassificationID = P.ClassificationID;
		Ar << ClassificationID;
		P.ClassificationID = ClassificationID;
	}

	return Ar;
}

const FDoubleVector FDoubleVector::ZeroVector = FDoubleVector(FVector::ZeroVector);
const FDoubleVector FDoubleVector::OneVector = FDoubleVector(FVector::OneVector);
const FDoubleVector FDoubleVector::UpVector = FDoubleVector(FVector::UpVector);
const FDoubleVector FDoubleVector::ForwardVector = FDoubleVector(FVector::ForwardVector);
const FDoubleVector FDoubleVector::RightVector = FDoubleVector(FVector::RightVector);

void FLidarPointCloudDataBuffer::MarkAsFree()
{
	if (PendingSize > 0)
	{
		Resize(PendingSize, true);
		PendingSize = 0;
	}

	bInUse = false;
}

void FLidarPointCloudDataBuffer::Initialize(const int32& Size)
{
	Data.AddUninitialized(Size);
}

void FLidarPointCloudDataBuffer::Resize(const int32& NewBufferSize, bool bForce /*= false*/)
{
	if (bInUse && !bForce)
	{
		// Don't want to resize while in use, flag the new size as pending
		// This will cause a resize as soon as the buffer is freed
		PendingSize = NewBufferSize;
	}
	else
	{
		int32 Delta = NewBufferSize - Data.Num();

		// Expand
		if (Delta > 0)
		{
			Data.AddUninitialized(Delta);
		}
		// Shrink
		else
		{
			Data.RemoveAtSwap(0, -Delta, true);
		}
	}
}

FLidarPointCloudDataBufferManager::FLidarPointCloudDataBufferManager(const int32& BufferSize)
	: BufferSize(BufferSize)
	, Head(FLidarPointCloudDataBuffer())
	, Tail(&Head)
{
	Head.Element.Initialize(BufferSize);
}

FLidarPointCloudDataBufferManager::~FLidarPointCloudDataBufferManager()
{
	TList<FLidarPointCloudDataBuffer>* Iterator = &Head;
	while (Iterator)
	{
		if (Iterator != &Head)
		{
			TList<FLidarPointCloudDataBuffer>* Tmp = Iterator;
			Iterator = Iterator->Next;
			delete Tmp;
		}
		else
		{
			Iterator = Iterator->Next;
		}
	}
}

FLidarPointCloudDataBuffer* FLidarPointCloudDataBufferManager::GetFreeBuffer()
{
	FLidarPointCloudDataBuffer* OutBuffer = nullptr;

	// Find available memory allocation
	{
		TList<FLidarPointCloudDataBuffer>* Iterator = &Head;
		while (Iterator)
		{
			if (!Iterator->Element.bInUse)
			{
				OutBuffer = &Iterator->Element;
				break;
			}

			Iterator = Iterator->Next;
		}
	}

	// If none found, add a new one
	if (!OutBuffer)
	{
		Tail->Next = new TList<FLidarPointCloudDataBuffer>(FLidarPointCloudDataBuffer());
		Tail = Tail->Next;
		OutBuffer = &Tail->Element;
		OutBuffer->Initialize(BufferSize);
	}

	OutBuffer->bInUse = true;

	return OutBuffer;
}

void FLidarPointCloudDataBufferManager::Resize(const int32& NewBufferSize)
{
	// Skip, if no change required
	if (BufferSize == NewBufferSize)
	{
		return;
	}

	BufferSize = NewBufferSize;

	TList<FLidarPointCloudDataBuffer>* Iterator = &Head;
	while (Iterator)
	{
		Iterator->Element.Resize(NewBufferSize);
		Iterator = Iterator->Next;
	}
}

