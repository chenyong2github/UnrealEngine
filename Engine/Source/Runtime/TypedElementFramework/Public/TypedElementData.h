// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/ScopeRWLock.h"
#include "Containers/ChunkedArray.h"
#include "TypedElementLimits.h"

/**
 * Macro to declare the required RTTI data for types representing element data.
 * @note Place this in the public section of your type declaration.
 */
#define UE_DECLARE_TYPED_ELEMENT_DATA_RTTI(ELEMENT_DATA_TYPE)						\
	static FTypedHandleTypeId Private_RegisteredTypeId;								\
	static FTypedHandleTypeId StaticTypeId() { return Private_RegisteredTypeId; }	\
	static FName StaticTypeName() { static const FName TypeName = #ELEMENT_DATA_TYPE; return TypeName; }

/**
 * Macro to define the required RTTI data for types representing element data.
 * @note Place this in the cpp file for your type definition.
 */
#define UE_DEFINE_TYPED_ELEMENT_DATA_RTTI(ELEMENT_DATA_TYPE)						\
	FTypedHandleTypeId ELEMENT_DATA_TYPE::Private_RegisteredTypeId = 0;

/**
 * Base class for the internal payload data associated with elements.
 */
class FTypedElementInternalData
{
public:
	FTypedElementInternalData() = default;
	
	FTypedElementInternalData(const FTypedElementInternalData&) = delete;
	FTypedElementInternalData& operator=(const FTypedElementInternalData&) = delete;
	
	FTypedElementInternalData(FTypedElementInternalData&& InOther) = default;
	FTypedElementInternalData& operator=(FTypedElementInternalData&&) = default;

	virtual ~FTypedElementInternalData() = default;

#if WITH_TYPED_ELEMENT_REFCOUNT
	FORCEINLINE void AddRef() const
	{
		checkSlow(RefCount < TNumericLimits<FTypedHandleRefCount>::Max());
		FPlatformAtomics::InterlockedIncrement(&RefCount);
	}

	FORCEINLINE void ReleaseRef() const
	{
		checkSlow(RefCount > 0);
		FPlatformAtomics::InterlockedDecrement(&RefCount);
	}

	FORCEINLINE FTypedHandleRefCount GetRefCount() const
	{
		return FPlatformAtomics::AtomicRead(&RefCount);
	}
#endif	// WITH_TYPED_ELEMENT_REFCOUNT

	virtual const void* GetUntypedData() const
	{
		return nullptr;
	}

private:
#if WITH_TYPED_ELEMENT_REFCOUNT
	mutable FTypedHandleRefCount RefCount = 0;
#endif	// WITH_TYPED_ELEMENT_REFCOUNT
};

/**
 * Internal payload data associated with typed elements.
 */
template <typename ElementDataType>
class TTypedElementInternalData : public FTypedElementInternalData
{
public:
	TTypedElementInternalData() = default;

	TTypedElementInternalData(TTypedElementInternalData&& InOther) = default;
	TTypedElementInternalData& operator=(TTypedElementInternalData&&) = default;

	virtual ~TTypedElementInternalData() = default;

	FORCEINLINE const ElementDataType& GetData() const
	{
		return Data;
	}

	FORCEINLINE ElementDataType& GetMutableData()
	{
		return Data;
	}

	virtual const void* GetUntypedData() const override
	{
		return &Data;
	}

private:
	ElementDataType Data;
};

/**
 * Internal payload data associated with typeless elements.
 */
template <>
class TTypedElementInternalData<void> : public FTypedElementInternalData
{
public:
	TTypedElementInternalData() = default;

	TTypedElementInternalData(TTypedElementInternalData&& InOther) = default;
	TTypedElementInternalData& operator=(TTypedElementInternalData&&) = default;

	virtual ~TTypedElementInternalData() = default;
};

/**
 * Data store implementation used by the element registry to manage internal data. 
 * @note This is the generic implementation that uses an array and manages the IDs itself.
 */
template <typename ElementDataType>
class TTypedElementInternalDataStore
{
public:
	static_assert(TNumericLimits<int32>::Max() >= TypedHandleMaxElementId, "TTypedElementInternalDataStore internally uses signed 32-bit indices so cannot store TypedHandleMaxElementId! Consider making this container 64-bit aware, or explicitly remove this compile time check.");

	TTypedElementInternalData<ElementDataType>& AddDataForElement(FTypedHandleElementId& InOutElementId)
	{
		FWriteScopeLock InternalDataLock(InternalDataRW);

		checkSlow(InOutElementId < 0);

		InOutElementId = InternalDataFreeIndices.Num() > 0
			? InternalDataFreeIndices.Pop(/*bAllowShrinking*/false)
			: InternalDataArray.Add();

		return InternalDataArray[InOutElementId];
	}

	void RemoveDataForElement(const FTypedHandleElementId InElementId, const FTypedElementInternalData* InExpectedDataPtr)
	{
		FWriteScopeLock InternalDataLock(InternalDataRW);

		checkSlow(InternalDataArray.IsValidIndex(InElementId));
		
		TTypedElementInternalData<ElementDataType>& InternalData = InternalDataArray[InElementId];
		checkf(InExpectedDataPtr == &InternalData, TEXT("Internal data pointer did not match the expected value! Does this handle belong to a different element registry?"));
		InternalData = TTypedElementInternalData<ElementDataType>();
		InternalDataFreeIndices.Add(InElementId);
	}

	const TTypedElementInternalData<ElementDataType>& GetDataForElement(const FTypedHandleElementId InElementId) const
	{
		FReadScopeLock InternalDataLock(InternalDataRW);

		checkSlow(InternalDataArray.IsValidIndex(InElementId));
		return InternalDataArray[InElementId];
	}

	static FORCEINLINE void SetStaticDataTypeId(const FTypedHandleTypeId InTypeId)
	{
		checkSlow(ElementDataType::Private_RegisteredTypeId == 0);
		ElementDataType::Private_RegisteredTypeId = InTypeId;
	}

	static FORCEINLINE FTypedHandleTypeId StaticDataTypeId()
	{
		return ElementDataType::StaticTypeId();
	}

	static FORCEINLINE FName StaticDataTypeName()
	{
		return ElementDataType::StaticTypeName();
	}

private:
	mutable FRWLock InternalDataRW;
	TChunkedArray<TTypedElementInternalData<ElementDataType>> InternalDataArray;
	TArray<int32> InternalDataFreeIndices;
};

/**
 * Data store implementation used by the element registry to manage internal data. 
 * @note This is the typeless implementation that uses external IDs, and exists only to track ref counts.
 */
template <>
class TTypedElementInternalDataStore<void>
{
public:
	static_assert(TNumericLimits<int32>::Max() >= TypedHandleMaxElementId, "TTypedElementInternalDataStore internally uses signed 32-bit indices so cannot store TypedHandleMaxElementId! Consider making this container 64-bit aware, or explicitly remove this compile time check.");

	TTypedElementInternalData<void>& AddDataForElement(FTypedHandleElementId& InOutElementId)
	{
#if WITH_TYPED_ELEMENT_REFCOUNT
		FWriteScopeLock InternalDataLock(InternalDataRW);

		checkSlow(InOutElementId >= 0);
		checkSlow(!ElementIdToArrayIndex.Contains(InOutElementId));

		const int32 InternalDataArrayIndex = InternalDataFreeIndices.Num() > 0
			? InternalDataFreeIndices.Pop(/*bAllowShrinking*/false)
			: InternalDataArray.Add();

		ElementIdToArrayIndex.Add(InOutElementId, InternalDataArrayIndex);
		return InternalDataArray[InternalDataArrayIndex];
#else	// WITH_TYPED_ELEMENT_REFCOUNT
		return SharedInternalData;
#endif	// WITH_TYPED_ELEMENT_REFCOUNT
	}

	void RemoveDataForElement(const FTypedHandleElementId InElementId, const FTypedElementInternalData* InExpectedDataPtr)
	{
#if WITH_TYPED_ELEMENT_REFCOUNT
		FWriteScopeLock InternalDataLock(InternalDataRW);

		int32 InternalDataArrayIndex = INDEX_NONE;
		ElementIdToArrayIndex.RemoveAndCopyValue(InElementId, InternalDataArrayIndex);

		checkSlow(InternalDataArray.IsValidIndex(InternalDataArrayIndex));

		TTypedElementInternalData<void>& InternalData = InternalDataArray[InElementId];
		checkf(InExpectedDataPtr == &InternalData, TEXT("Internal data pointer did not match the expected value! Does this handle belong to a different element registry?"));
		InternalData = TTypedElementInternalData<void>();
		InternalDataFreeIndices.Add(InternalDataArrayIndex);
#else	// WITH_TYPED_ELEMENT_REFCOUNT
		checkf(InExpectedDataPtr == &SharedInternalData, TEXT("Internal data pointer did not match the expected value! Does this handle belong to a different element registry?"));
#endif	// WITH_TYPED_ELEMENT_REFCOUNT
	}

	const TTypedElementInternalData<void>& GetDataForElement(const FTypedHandleElementId InElementId) const
	{
#if WITH_TYPED_ELEMENT_REFCOUNT
		FReadScopeLock InternalDataLock(InternalDataRW);

		const int32* InternalDataArrayIndexPtr = ElementIdToArrayIndex.Find(InElementId);
		checkSlow(InternalDataArrayIndexPtr && InternalDataArray.IsValidIndex(*InternalDataArrayIndexPtr));
		return InternalDataArray[*InternalDataArrayIndexPtr];
#else	// WITH_TYPED_ELEMENT_REFCOUNT
		return SharedInternalData;
#endif	// WITH_TYPED_ELEMENT_REFCOUNT
	}

	static FORCEINLINE void SetStaticDataTypeId(const FTypedHandleTypeId InTypeId)
	{
	}

	static FORCEINLINE FTypedHandleTypeId StaticDataTypeId()
	{
		return 0;
	}

	static FORCEINLINE FName StaticDataTypeName()
	{
		return FName();
	}

private:
#if WITH_TYPED_ELEMENT_REFCOUNT
	mutable FRWLock InternalDataRW;
	TChunkedArray<TTypedElementInternalData<void>> InternalDataArray;
	TArray<int32> InternalDataFreeIndices;
	TMap<FTypedHandleElementId, int32> ElementIdToArrayIndex;
#else	// WITH_TYPED_ELEMENT_REFCOUNT
	TTypedElementInternalData<void> SharedInternalData;
#endif	// WITH_TYPED_ELEMENT_REFCOUNT
};
