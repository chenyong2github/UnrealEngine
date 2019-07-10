// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Serializable.h"
#include "Chaos/ArrayCollectionArray.h"
#include "Serialization/ArchiveProxy.h"
#include "UObject/DestructionObjectVersion.h"

namespace Chaos
{
template <typename T, int d>
class CHAOS_API TImplicitObject; //needed for legacy serializer

class CHAOS_API FChaosArchive : public FArchiveProxy
{
public:
	FChaosArchive(FArchive& ArIn)
		: FArchiveProxy(ArIn)
		, TagCount(0)
	{
	}

	virtual ~FChaosArchive() {}

	template <typename T>
	void SerializePtr(TSerializablePtr<T>& Obj)
	{
		bool bExists = Obj.Get() != nullptr;
		InnerArchive << bExists;
		if (!bExists)
		{
			Obj.Reset();
			return;
		}

		if (InnerArchive.IsLoading())
		{
			int32 Tag;
			InnerArchive << Tag;

			const int32 SlotsNeeded = Tag + 1 - TagToObject.Num();
			if (SlotsNeeded > 0)
			{
				TagToObject.AddZeroed(SlotsNeeded);
			}

			if (TagToObject[Tag])
			{
				Obj.SetFromRawLowLevel((const T*)TagToObject[Tag]);
			}
			else
			{
				T::StaticSerialize(*this, Obj);
				TagToObject[Tag] = (void*)Obj.Get();
			}
		}
		else if (InnerArchive.IsSaving())
		{
			void* ObjRaw = (void*)Obj.Get();
			check(PendingAdds.Contains(ObjRaw) == false);	//catch dependency cycles. Not supported
			if (ObjToTag.Contains(ObjRaw))
			{
				InnerArchive << ObjToTag[ObjRaw];
			}
			else
			{
				PendingAdds.Add(ObjRaw);

				int32 Tag = TagCount++;
				ObjToTag.Add(ObjRaw, Tag);

				InnerArchive << Tag;
				T::StaticSerialize(*this, Obj);

				PendingAdds.Remove(ObjRaw);
			}
		}
	}

	template <typename T>
	void SerializePtr(TUniquePtr<T>& Obj)
	{
		InnerArchive.UsingCustomVersion(FDestructionObjectVersion::GUID);
		if (InnerArchive.CustomVer(FDestructionObjectVersion::GUID) < FDestructionObjectVersion::ChaosArchiveAdded)
		{
			SerializeLegacy(Obj);
		}
		else
		{
			TSerializablePtr<T> Copy(Obj);
			SerializePtr(Copy);

			if (InnerArchive.IsLoading())
			{
				check(!Obj);
				Obj.Reset(const_cast<T*>(Copy.Get()));
			}
		}
	}

private:

	template <typename T>
	void SerializeLegacy(TUniquePtr<T>& Obj)
	{
		check(false);
	}

	void SerializeLegacy(TUniquePtr<TImplicitObject<float, 3>>& Obj);

	TArray<void*> TagToObject;
	TMap<void*, int32> ObjToTag;
	TSet<void*> PendingAdds;
	int32 TagCount;
};

template <typename T>
FORCEINLINE FChaosArchive& operator<<(FChaosArchive& Ar, TSerializablePtr<T>& Serializable)
{
	Ar.SerializePtr(Serializable);
	return Ar;
}

template <typename T>
FChaosArchive& operator<<(FChaosArchive& Ar, TArray<TSerializablePtr<T>>& Array)
{
	int32 ArrayNum = Array.Num();
	Ar << ArrayNum;
	Array.SetNum(ArrayNum);

	for (int32 Idx = 0; Idx < ArrayNum; ++Idx)
	{
		Ar << Array[Idx];
	}

	return Ar;
}

template <typename T>
typename TEnableIf<T::IsSerializablePtr, FChaosArchive&>::Type operator<<(FChaosArchive& Ar, TUniquePtr<T>& Obj)
{
	Ar.SerializePtr(Obj);
	return Ar;
}

template <typename T>
typename TEnableIf<T::IsSerializablePtr, FChaosArchive&>::Type operator<<(FChaosArchive& Ar, TArray<TUniquePtr<T>>& Array)
{
	int32 ArrayNum = Array.Num();
	Ar << ArrayNum;
	Array.SetNum(ArrayNum);

	for (int32 Idx = 0; Idx < ArrayNum; ++Idx)
	{
		Ar << Array[Idx];
	}

	return Ar;
}

}
