// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Metadata/PCGMetadataCommon.h"
#include "PCGPoint.h"

#include "Containers/ArrayView.h"
#include "UObject/UnrealType.h"

class FPCGMetadataAttributeBase;

///////////////////////////////////////////////////////////////////////

/**
* Base class to identify keys to use with an accessor.
*/
class IPCGAttributeAccessorKeys
{
public:
	IPCGAttributeAccessorKeys(bool bInReadOnly)
		: bIsReadOnly(bInReadOnly)
	{}

	virtual ~IPCGAttributeAccessorKeys() = default;

	/**
	* Retrieve in the given view pointers of the wanted type
	* Need to be a supported type, such as FPCGPoint, PCGMetadataEntryKey or void.
	* It will wrap around if the index/range goes outside the number of keys.
	* @param InStart - Index to start looking in the keys.
	* @param OutKeys - View on the out keys. Its size will indicate the number of elements to get.
	* @return true if it succeeded, false otherwise. (like num == 0,unsupported type or read only)
	*/
	template <typename ObjectType>
	bool GetKeys(int32 InStart, TArrayView<ObjectType*>& OutKeys);

	// Same function but const.
	template <typename ObjectType>
	bool GetKeys(int32 InStart, TArrayView<const ObjectType*>& OutKeys) const;

	/**
	* Retrieve in the given argument pointer of the wanted type.
	* Need to be a supported type, such as FPCGPoint, PCGMetadataEntryKey or void.
	* It will wrap around if the index goes outside the number of keys.
	* @param InStart - Index to start looking in the keys.
	* @param OutObject - Reference on a pointer, where the key will be written.
	* @return true if it succeeded, false otherwise. (like num == 0, unsupported type or read only)
	*/
	template <typename ObjectType>
	bool GetKey(int32 InStart, ObjectType*& OutObject)
	{
		return GetKeys(InStart, TArrayView<ObjectType*>(&OutObject, 1));
	}

	// Same function but const
	template <typename ObjectType>
	bool GetKey(int32 InStart, ObjectType const*& OutObject)
	{
		return GetKeys(InStart, TArrayView<ObjectType*>(&OutObject, 1));
	}

	/**
	* Retrieve in the given argument pointer of the wanted type at the index 0.
	* Need to be a supported type, such as FPCGPoint, PCGMetadataEntryKey or void.
	* @param OutObject - Reference on a pointer, where the key will be written.
	* @return true if it succeeded, false otherwise. (like num == 0, unsupported type or read only)
	*/
	template <typename ObjectType>
	bool GetKey(ObjectType*& OutObject)
	{
		return GetKeys(/*InStart=*/ 0, TArrayView<ObjectType*>(&OutObject, 1));
	}

	// Same function but const
	template <typename ObjectType>
	bool GetKey(ObjectType const*& OutObject) const
	{
		return GetKeys(/*InStart=*/ 0, TArrayView<const ObjectType*>(&OutObject, 1));
	}

	/*
	* Returns the number of keys.
	*/
	virtual int32 GetNum() const = 0;

protected:
	virtual bool GetPointKeys(int32 InStart, TArrayView<FPCGPoint*>& OutPoints) { return false; }
	virtual bool GetPointKeys(int32 InStart, TArrayView<const FPCGPoint*>& OutPoints) const { return false; }

	virtual bool GetGenericObjectKeys(int32 InStart, TArrayView<void*>& OutObjects) { return false; }
	virtual bool GetGenericObjectKeys(int32 InStart, TArrayView<const void*>& OutObjects) const { return false; }

	virtual bool GetMetadataEntryKeys(int32 InStart, TArrayView<PCGMetadataEntryKey*>& OutEntryKeys) { return false; }
	virtual bool GetMetadataEntryKeys(int32 InStart, TArrayView<const PCGMetadataEntryKey*>& OutEntryKeys) const { return false; }

	bool bIsReadOnly = false;
};

///////////////////////////////////////////////////////////////////////

/**
* Key around a metadata entry key
*/
class FPCGAttributeAccessorKeysEntries : public IPCGAttributeAccessorKeys
{
public:
	FPCGAttributeAccessorKeysEntries(const FPCGMetadataAttributeBase* Attribute);
	FPCGAttributeAccessorKeysEntries(PCGMetadataEntryKey EntryKey);

	virtual int32 GetNum() const override { return Entries.Num(); }

protected:
	virtual bool GetMetadataEntryKeys(int32 InStart, TArrayView<PCGMetadataEntryKey*>& OutEntryKeys) override;
	virtual bool GetMetadataEntryKeys(int32 InStart, TArrayView<const PCGMetadataEntryKey*>& OutEntryKeys) const override;

	TArray<PCGMetadataEntryKey> Entries;
};

///////////////////////////////////////////////////////////////////////

/**
* Key around points
*/
class FPCGAttributeAccessorKeysPoints : public IPCGAttributeAccessorKeys
{
public:
	FPCGAttributeAccessorKeysPoints(const TArrayView<FPCGPoint>& InPoints);
	FPCGAttributeAccessorKeysPoints(const TArrayView<const FPCGPoint>& InPoints);

	FPCGAttributeAccessorKeysPoints(FPCGPoint& InPoint);
	FPCGAttributeAccessorKeysPoints(const FPCGPoint& InPoint);

	virtual int32 GetNum() const override { return Points.Num(); }

protected:
	virtual bool GetPointKeys(int32 InStart, TArrayView<FPCGPoint*>& OutPoints) override;
	virtual bool GetPointKeys(int32 InStart, TArrayView<const FPCGPoint*>& OutPoints) const override;

	virtual bool GetGenericObjectKeys(int32 InStart, TArrayView<void*>& OutObjects) override;
	virtual bool GetGenericObjectKeys(int32 InStart, TArrayView<const void*>& OutObjects) const override;

	virtual bool GetMetadataEntryKeys(int32 InStart, TArrayView<PCGMetadataEntryKey*>& OutEntryKeys) override;
	virtual bool GetMetadataEntryKeys(int32 InStart, TArrayView<const PCGMetadataEntryKey*>& OutEntryKeys) const override;

	TArrayView<FPCGPoint> Points;
};

/////////////////////////////////////////////////////////////////

/**
* Key around generic objects
*/
template <typename ObjectType>
class FPCGAttributeAccessorKeysGeneric : public IPCGAttributeAccessorKeys
{
public:
	FPCGAttributeAccessorKeysGeneric(const TArrayView<ObjectType>& InObjects)
		: IPCGAttributeAccessorKeys(/*bInReadOnly=*/ false)
		, Objects(InObjects)
	{}

	FPCGAttributeAccessorKeysGeneric(const TArrayView<const ObjectType>& InObjects)
		: IPCGAttributeAccessorKeys(/*bInReadOnly=*/ true)
		, Objects(const_cast<ObjectType*>(InObjects.GetData()), InObjects.Num())
	{}

	FPCGAttributeAccessorKeysGeneric(ObjectType& InObject)
		: FPCGAttributeAccessorKeysGeneric(TArrayView<ObjectType>(&InObject, 1))
	{}

	FPCGAttributeAccessorKeysGeneric(const ObjectType& InObject)
		: FPCGAttributeAccessorKeysGeneric(TArrayView<const ObjectType>(&InObject, 1))
	{}

	virtual int32 GetNum() const override { return Objects.Num(); }

protected:
	virtual bool GetGenericObjectKeys(int32 InStart, TArrayView<void*>& OutObjects) override;
	virtual bool GetGenericObjectKeys(int32 InStart, TArrayView<const void*>& OutObjects) const override;

	TArrayView<ObjectType> Objects;
};

/////////////////////////////////////////////////////////////////

template <typename ObjectType>
inline bool IPCGAttributeAccessorKeys::GetKeys(int32 InStart, TArrayView<ObjectType*>& OutKeys)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(IPCGAttributeAccessorKeys::GetKeys);

	if (bIsReadOnly)
	{
		return false;
	}

	if constexpr (std::is_same_v<ObjectType, FPCGPoint>)
	{
		return GetPointKeys(InStart, OutKeys);
	}
	else if constexpr (std::is_same_v<ObjectType, PCGMetadataEntryKey>)
	{
		return GetMetadataEntryKeys(InStart, OutKeys);
	}
	else if constexpr (std::is_same_v<ObjectType, void>)
	{
		return GetGenericObjectKeys(InStart, OutKeys);
	}
	else
	{
		return false;
	}
}

template <typename ObjectType>
inline bool IPCGAttributeAccessorKeys::GetKeys(int32 InStart, TArrayView<const ObjectType*>& OutKeys) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(IPCGAttributeAccessorKeys::GetKeys);

	if constexpr (std::is_same_v<ObjectType, FPCGPoint>)
	{
		return GetPointKeys(InStart, OutKeys);
	}
	else if constexpr (std::is_same_v<ObjectType, PCGMetadataEntryKey>)
	{
		return GetMetadataEntryKeys(InStart, OutKeys);
	}
	else if constexpr (std::is_same_v<ObjectType, void>)
	{
		return GetGenericObjectKeys(InStart, OutKeys);
	}
	else
	{
		return false;
	}
}

/////////////////////////////////////////////////////////////////

namespace PCGAttributeAccessorKeys
{
	template <typename T, typename Container, typename Func>
	bool GetKeys(Container& InContainer, int32 InStart, TArrayView<T*>& OutItems, Func&& Transform)
	{
		if (InContainer.Num() == 0)
		{
			return false;
		}

		int32 Current = InStart;
		if (Current >= InContainer.Num())
		{
			Current %= InContainer.Num();
		}

		for (int32 i = 0; i < OutItems.Num(); ++i)
		{
			OutItems[i] = Transform(InContainer[Current++]);
			if (Current >= InContainer.Num())
			{
				Current = 0;
			}
		}

		return true;
	}
}

template <typename ObjectType>
bool FPCGAttributeAccessorKeysGeneric<ObjectType>::GetGenericObjectKeys(int32 InStart, TArrayView<void*>& OutObjects)
{
	return PCGAttributeAccessorKeys::GetKeys(Objects, InStart, OutObjects, [](ObjectType& Obj) -> ObjectType* { return &Obj; });
}

template <typename ObjectType>
bool FPCGAttributeAccessorKeysGeneric<ObjectType>::GetGenericObjectKeys(int32 InStart, TArrayView<const void*>& OutObjects) const
{
	return PCGAttributeAccessorKeys::GetKeys(Objects, InStart, OutObjects, [](const ObjectType& Obj) -> const ObjectType* { return &Obj; });
}