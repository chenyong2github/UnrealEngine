// Copyright Epic Games, Inc. All Rights Reserved.
 
#include "LidarPointCloudShared.h"
#include "LidarPointCloud.h"

double FBenchmarkTimer::Time = 0;

FArchive& operator<<(FArchive& Ar, FLidarPointCloudPoint_Legacy& P)
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

void FLidarPointCloudBulkData::CustomSerialize(FArchive& Ar, UObject* Owner)
{
	// Pre-streaming format
	if (Ar.CustomVer(ULidarPointCloud::PointCloudFileGUID) < 16)
	{
		// Load legacy data
		TArray<FLidarPointCloudPoint_Legacy> AllocatedPoints;
		TArray<FLidarPointCloudPoint_Legacy> PaddingPoints;
		Ar << AllocatedPoints << PaddingPoints;

		// Copy the data to BulkData
		Lock(LOCK_READ_WRITE);
		FLidarPointCloudPoint* TempDataPtr = DataPtr = (FLidarPointCloudPoint*)Realloc(AllocatedPoints.Num() + PaddingPoints.Num());
		for (FLidarPointCloudPoint_Legacy* Data = AllocatedPoints.GetData(), *DataEnd = Data + AllocatedPoints.Num(); Data != DataEnd; ++Data, ++TempDataPtr)
		{
			*TempDataPtr = *Data;
		}
		for (FLidarPointCloudPoint_Legacy* Data = PaddingPoints.GetData(), *DataEnd = Data + PaddingPoints.Num(); Data != DataEnd; ++Data, ++TempDataPtr)
		{
			*TempDataPtr = *Data;
		}
		TempDataPtr = nullptr;
		bHasData = true;
		Unlock();
	}
	// Pre-normals format
	else if (Ar.CustomVer(ULidarPointCloud::PointCloudFileGUID) < 19)
	{
		void* TempData = nullptr;
		int64 NumElements = 0;

		// We need to use a Legacy element size
		ElementSize = sizeof(FLidarPointCloudPoint_Legacy);

		// Get the legacy data
		Serialize(Ar, Owner);
		GetCopy(&TempData);
		NumElements = GetElementCount();

		Lock(LOCK_READ_WRITE);

		// Restore normal element size
		ElementSize = sizeof(FLidarPointCloudPoint);

		// Allocate the new buffer
		FLidarPointCloudPoint* TempDataPtr = DataPtr = (FLidarPointCloudPoint*)Realloc(NumElements);

		// Copy the legacy data
		for (FLidarPointCloudPoint_Legacy* Data = (FLidarPointCloudPoint_Legacy*)TempData, *DataEnd = Data + NumElements; Data != DataEnd; ++Data, ++TempDataPtr)
		{
			*TempDataPtr = *Data;
		}
		TempDataPtr = nullptr;
		bHasData = true;

		Unlock();

		// Release the legacy data
		FMemory::Free(TempData);
	}
	else
	{
		Serialize(Ar, Owner);
	}
}
