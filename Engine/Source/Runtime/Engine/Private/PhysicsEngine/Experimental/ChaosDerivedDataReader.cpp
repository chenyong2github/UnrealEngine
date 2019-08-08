// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#if INCLUDE_CHAOS

#include "Physics/Experimental/ChaosDerivedDataReader.h"
#include "Chaos/ChaosArchive.h"
#include "Chaos/TriangleMeshImplicitObject.h"

template<typename T, int d>
FChaosDerivedDataReader<T, d>::FChaosDerivedDataReader(FUntypedBulkData* InBulkData)
	: bReadSuccessful(false)
{
	const int32 DataTypeSize = sizeof(T);

	uint8* DataPtr = (uint8*)InBulkData->LockReadOnly();
	FBufferReader Ar(DataPtr, InBulkData->GetBulkDataSize(), false);
	Chaos::FChaosArchive ChaosAr(Ar);

	int32 SerializedDataSize = -1;

	ChaosAr << SerializedDataSize;

	if(SerializedDataSize != DataTypeSize)
	{
		// Can't use this data, it was serialized for a different precision 
		ensureMsgf(false, TEXT("Failed to load Chaos body setup bulk data. Expected fp precision to be width %d but it was %d"), DataTypeSize, SerializedDataSize);
	}
	else
	{
		ChaosAr << ConvexImplicitObjects << TrimeshImplicitObjects << UVInfo;

		bReadSuccessful = true;
	}

	InBulkData->Unlock();
}

template class FChaosDerivedDataReader<float, 3>;

#endif
