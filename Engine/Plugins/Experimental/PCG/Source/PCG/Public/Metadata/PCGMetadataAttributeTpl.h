// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Metadata/PCGMetadataAttribute.h"
#include "Metadata/PCGMetadataAttributeTraits.h"

template<typename T>
class FPCGMetadataAttribute : public FPCGMetadataAttributeBase
{
public:
	FPCGMetadataAttribute(UPCGMetadata* InMetadata, FName InName, const FPCGMetadataAttributeBase* InParent, const T& InDefaultValue, bool bInAllowsInterpolation)
		: FPCGMetadataAttributeBase(InMetadata, InName, InParent, bInAllowsInterpolation)
		, DefaultValue(InDefaultValue)
	{
		TypeId = PCG::Private::MetadataTypes<T>::Id;

		if (GetParent())
		{
			ValueKeyOffset = GetParent()->GetValueKeyOffsetForChild();
		}
	}

	// This constructor is used only during serialization
	FPCGMetadataAttribute()
	{
		TypeId = PCG::Private::MetadataTypes<T>::Id;
	}

	virtual void Serialize(UPCGMetadata* InMetadata, FArchive& InArchive) override
	{
		FPCGMetadataAttributeBase::Serialize(InMetadata, InArchive);

		InArchive << Values;
		InArchive << DefaultValue;
		
		// Initialize non-serialized members
		if (InArchive.IsLoading())
		{
			ValueKeyOffset = GetParent() ? GetParent()->GetValueKeyOffsetForChild() : 0;
		}
	}

	const FPCGMetadataAttribute* GetParent() const { return static_cast<const FPCGMetadataAttribute*>(Parent); }

	virtual FPCGMetadataAttributeBase* Copy(FName NewName, UPCGMetadata* InMetadata, bool bKeepParent, bool bCopyEntries = true, bool bCopyValues = true) const override
	{
		// this copies to a new attribute
		check(!bKeepParent || Metadata->GetRoot() == InMetadata->GetRoot());
		FPCGMetadataAttribute<T>* AttributeCopy = new FPCGMetadataAttribute<T>(InMetadata, NewName, bKeepParent ? this : nullptr, DefaultValue, bAllowsInterpolation);

		if (bCopyEntries)
		{
			EntryMapLock.ReadLock();
			AttributeCopy->EntryToValueKeyMap = EntryToValueKeyMap;
			EntryMapLock.ReadUnlock();
		}

		if (bCopyValues)
		{
			ValueLock.ReadLock();
			AttributeCopy->Values = Values;
			AttributeCopy->ValueKeyOffset = ValueKeyOffset;
			ValueLock.ReadUnlock();
		}

		return AttributeCopy;
	}

	virtual void SetValue(PCGMetadataEntryKey ItemKey, const FPCGMetadataAttributeBase* InAttribute, PCGMetadataEntryKey InEntryKey) override
	{
		check(ItemKey != PCGInvalidEntryKey);
		if (InAttribute == this)
		{
			SetValueFromValueKey(ItemKey, GetValueKey(InEntryKey));
		}
		else if (InAttribute)
		{
			SetValue(ItemKey, static_cast<const FPCGMetadataAttribute<T>*>(InAttribute)->GetValueFromItemKey(InEntryKey));
		}
	}

	virtual void SetZeroValue(PCGMetadataEntryKey ItemKey) override
	{
		check(ItemKey != PCGInvalidEntryKey);
		ZeroValue(ItemKey);
	}

	virtual void AccumulateValue(PCGMetadataEntryKey ItemKey, const FPCGMetadataAttributeBase* InAttribute, PCGMetadataEntryKey InEntryKey, float Weight) override
	{
		check(ItemKey != PCGInvalidEntryKey);
		Accumulate(ItemKey, InAttribute, InEntryKey, Weight);
	}

	virtual void SetWeightedValue(PCGMetadataEntryKey ItemKey, const FPCGMetadataAttributeBase* InAttribute, const TArrayView<TPair<PCGMetadataEntryKey, float>> InWeightedKeys) override
	{
		check(ItemKey != PCGInvalidEntryKey);
		Accumulate(ItemKey, InAttribute, InWeightedKeys);
	}

	virtual void SetValue(PCGMetadataEntryKey ItemKey, const FPCGMetadataAttributeBase* InAttributeA, PCGMetadataEntryKey InEntryKeyA, const FPCGMetadataAttributeBase* InAttributeB, PCGMetadataEntryKey InEntryKeyB, EPCGMetadataOp Op) override
	{
		check(ItemKey != PCGInvalidEntryKey);
		bool bAppliedValue = false;

		if (InAttributeA && InAttributeB && bAllowsInterpolation)
		{
			if (Op == EPCGMetadataOp::Min)
			{
				bAppliedValue = SetMin(ItemKey, InAttributeA, InEntryKeyA, InAttributeB, InEntryKeyB);
			}
			else if (Op == EPCGMetadataOp::Max)
			{
				bAppliedValue = SetMax(ItemKey, InAttributeA, InEntryKeyA, InAttributeB, InEntryKeyB);
			}
			else if (Op == EPCGMetadataOp::Sub)
			{
				bAppliedValue = SetSub(ItemKey, InAttributeA, InEntryKeyA, InAttributeB, InEntryKeyB);
			}
			else if (Op == EPCGMetadataOp::Add)
			{
				bAppliedValue = SetAdd(ItemKey, InAttributeA, InEntryKeyA, InAttributeB, InEntryKeyB);
			}
			else if (Op == EPCGMetadataOp::Mul)
			{
				bAppliedValue = SetMul(ItemKey, InAttributeA, InEntryKeyA, InAttributeB, InEntryKeyB);
			}
			else if (Op == EPCGMetadataOp::Div)
			{
				bAppliedValue = SetDiv(ItemKey, InAttributeA, InEntryKeyA, InAttributeB, InEntryKeyB);
			}
		}
		else if (InAttributeA && InAttributeB && HasNonDefaultValue(ItemKey))
		{
			// In this case, the current already has a value, in which case we should not update it
			bAppliedValue = true;
		}

		if (bAppliedValue)
		{
			// Nothing to do
		}
		else if (InAttributeA)
		{
			if (InAttributeA == this)
			{
				SetValueFromValueKey(ItemKey, GetValueKey(InEntryKeyA));
			}
			else
			{
				SetValue(ItemKey, static_cast<const FPCGMetadataAttribute<T>*>(InAttributeA)->GetValueFromItemKey(InEntryKeyA));
			}
		}
		else if (InAttributeB)
		{
			if (InAttributeB == this)
			{
				SetValueFromValueKey(ItemKey, GetValueKey(InEntryKeyB));
			}
			else
			{
				SetValue(ItemKey, static_cast<const FPCGMetadataAttribute<T>*>(InAttributeB)->GetValueFromItemKey(InEntryKeyB));
			}
		}
	}

	virtual bool IsEqualToDefaultValue(PCGMetadataValueKey ValueKey) const
	{
		return PCG::Private::MetadataTraits<T>::Equal(GetValue(ValueKey), DefaultValue);
	}

	PCGMetadataValueKey GetValueKeyOffsetForChild() const
	{
		FReadScopeLock ScopeLock(ValueLock);
		return Values.Num() + ValueKeyOffset;
	}

	/** Adds the value, returns the value key for the given value */
	PCGMetadataValueKey AddValue(const T& InValue)
	{
		PCGMetadataValueKey FoundValue = FindValue(InValue);

		if (FoundValue == PCGDefaultValueKey)
		{
			FWriteScopeLock ScopeLock(ValueLock);
			return Values.Add(InValue) + ValueKeyOffset;
		}
		else
		{
			return FoundValue;
		}
	}

	void SetValue(PCGMetadataEntryKey ItemKey, const T& InValue)
	{
		check(ItemKey != PCGInvalidEntryKey);
		SetValueFromValueKey(ItemKey, AddValue(InValue));
	}

	template<typename U>
	void SetValue(PCGMetadataEntryKey ItemKey, const U& InValue)
	{
		check(ItemKey != PCGInvalidEntryKey);
		SetValueFromValueKey(ItemKey, AddValue(T(InValue)));
	}

	T GetValueFromItemKey(PCGMetadataEntryKey ItemKey) const
	{
		return GetValue(GetValueKey(ItemKey));
	}

	T GetValue(PCGMetadataValueKey ValueKey) const
	{
		if (ValueKey == PCGDefaultValueKey)
		{
			return DefaultValue;
		}
		else if (ValueKey >= ValueKeyOffset)
		{
			FReadScopeLock ScopeLock(ValueLock);
			return Values[ValueKey - ValueKeyOffset];
		}
		else if (GetParent())
		{
			return GetParent()->GetValue(ValueKey);
		}
		else
		{
			return DefaultValue;
		}
	}

	/** Code related to finding values / compressing data */
	template<typename IT = T, typename TEnableIf<PCG::Private::MetadataTraits<IT>::CompressData>::Type* = nullptr>
	PCGMetadataValueKey FindValue(const T& InValue) const
	{
		PCGMetadataValueKey ParentValueKey = (GetParent() ? GetParent()->FindValue(InValue) : PCGDefaultValueKey);
		if (ParentValueKey != PCGDefaultValueKey)
		{
			return ParentValueKey;
		}
		else
		{
			ValueLock.ReadLock();
			const int32 ValueIndex = Values.FindLast(InValue);
			ValueLock.ReadUnlock();

			if (ValueIndex != INDEX_NONE)
			{
				return ValueIndex + ValueKeyOffset;
			}
			else
			{
				return ParentValueKey;
			}
		}
	}

	template<typename IT = T, typename TEnableIf<!PCG::Private::MetadataTraits<IT>::CompressData>::Type* = nullptr>
	PCGMetadataValueKey FindValue(const T& InValue) const
	{
		return PCGDefaultValueKey;
	}

protected:
	/** Code related to computing compared values (min, max, sub, add) */
	template<typename IT = T, typename TEnableIf<PCG::Private::MetadataTraits<IT>::CanMinMax>::Type* = nullptr>
	bool SetMin(PCGMetadataEntryKey ItemKey, const FPCGMetadataAttributeBase* InAttributeA, PCGMetadataEntryKey InEntryKeyA, const FPCGMetadataAttributeBase* InAttributeB, PCGMetadataEntryKey InEntryKeyB)
	{
		SetValue(ItemKey, 
			PCG::Private::MetadataTraits<IT>::Min(
				static_cast<const FPCGMetadataAttribute<T>*>(InAttributeA)->GetValueFromItemKey(InEntryKeyA),
				static_cast<const FPCGMetadataAttribute<T>*>(InAttributeB)->GetValueFromItemKey(InEntryKeyB)));

		return true;
	}

	template<typename IT = T, typename TEnableIf<!PCG::Private::MetadataTraits<IT>::CanMinMax>::Type* = nullptr>
	bool SetMin(PCGMetadataEntryKey ItemKey, const FPCGMetadataAttributeBase* InAttributeA, PCGMetadataEntryKey InEntryKeyA, const FPCGMetadataAttributeBase* InAttributeB, PCGMetadataEntryKey InEntryKeyB)
	{
		return false;
	}

	template<typename IT = T, typename TEnableIf<PCG::Private::MetadataTraits<IT>::CanMinMax>::Type* = nullptr>
	bool SetMax(PCGMetadataEntryKey ItemKey, const FPCGMetadataAttributeBase* InAttributeA, PCGMetadataEntryKey InEntryKeyA, const FPCGMetadataAttributeBase* InAttributeB, PCGMetadataEntryKey InEntryKeyB)
	{
		SetValue(ItemKey,
			PCG::Private::MetadataTraits<IT>::Max(
				static_cast<const FPCGMetadataAttribute<T>*>(InAttributeA)->GetValueFromItemKey(InEntryKeyA),
				static_cast<const FPCGMetadataAttribute<T>*>(InAttributeB)->GetValueFromItemKey(InEntryKeyB)));

		return true;
	}

	template<typename IT = T, typename TEnableIf<!PCG::Private::MetadataTraits<IT>::CanMinMax>::Type* = nullptr>
	bool SetMax(PCGMetadataEntryKey ItemKey, const FPCGMetadataAttributeBase* InAttributeA, PCGMetadataEntryKey InEntryKeyA, const FPCGMetadataAttributeBase* InAttributeB, PCGMetadataEntryKey InEntryKeyB)
	{
		return false;
	}

	template<typename IT = T, typename TEnableIf<PCG::Private::MetadataTraits<IT>::CanSubAdd>::Type* = nullptr>
	bool SetAdd(PCGMetadataEntryKey ItemKey, const FPCGMetadataAttributeBase* InAttributeA, PCGMetadataEntryKey InEntryKeyA, const FPCGMetadataAttributeBase* InAttributeB, PCGMetadataEntryKey InEntryKeyB)
	{
		SetValue(ItemKey,
			PCG::Private::MetadataTraits<IT>::Add(
				static_cast<const FPCGMetadataAttribute<T>*>(InAttributeA)->GetValueFromItemKey(InEntryKeyA),
				static_cast<const FPCGMetadataAttribute<T>*>(InAttributeB)->GetValueFromItemKey(InEntryKeyB)));

		return true;
	}

	template<typename IT = T, typename TEnableIf<!PCG::Private::MetadataTraits<IT>::CanSubAdd>::Type* = nullptr>
	bool SetAdd(PCGMetadataEntryKey ItemKey, const FPCGMetadataAttributeBase* InAttributeA, PCGMetadataEntryKey InEntryKeyA, const FPCGMetadataAttributeBase* InAttributeB, PCGMetadataEntryKey InEntryKeyB)
	{
		return false;
	}

	template<typename IT = T, typename TEnableIf<PCG::Private::MetadataTraits<IT>::CanSubAdd>::Type* = nullptr>
	bool SetSub(PCGMetadataEntryKey ItemKey, const FPCGMetadataAttributeBase* InAttributeA, PCGMetadataEntryKey InEntryKeyA, const FPCGMetadataAttributeBase* InAttributeB, PCGMetadataEntryKey InEntryKeyB)
	{
		SetValue(ItemKey,
			PCG::Private::MetadataTraits<IT>::Sub(
				static_cast<const FPCGMetadataAttribute<T>*>(InAttributeA)->GetValueFromItemKey(InEntryKeyA),
				static_cast<const FPCGMetadataAttribute<T>*>(InAttributeB)->GetValueFromItemKey(InEntryKeyB)));

		return true;
	}

	template<typename IT = T, typename TEnableIf<!PCG::Private::MetadataTraits<IT>::CanSubAdd>::Type* = nullptr>
	bool SetSub(PCGMetadataEntryKey ItemKey, const FPCGMetadataAttributeBase* InAttributeA, PCGMetadataEntryKey InEntryKeyA, const FPCGMetadataAttributeBase* InAttributeB, PCGMetadataEntryKey InEntryKeyB)
	{
		return false;
	}

	template<typename IT = T, typename TEnableIf<PCG::Private::MetadataTraits<IT>::CanMulDiv>::Type* = nullptr>
	bool SetMul(PCGMetadataEntryKey ItemKey, const FPCGMetadataAttributeBase* InAttributeA, PCGMetadataEntryKey InEntryKeyA, const FPCGMetadataAttributeBase* InAttributeB, PCGMetadataEntryKey InEntryKeyB)
	{
		SetValue(ItemKey,
			PCG::Private::MetadataTraits<IT>::Mul(
				static_cast<const FPCGMetadataAttribute<T>*>(InAttributeA)->GetValueFromItemKey(InEntryKeyA),
				static_cast<const FPCGMetadataAttribute<T>*>(InAttributeB)->GetValueFromItemKey(InEntryKeyB)));

		return true;
	}

	template<typename IT = T, typename TEnableIf<!PCG::Private::MetadataTraits<IT>::CanMulDiv>::Type* = nullptr>
	bool SetMul(PCGMetadataEntryKey ItemKey, const FPCGMetadataAttributeBase* InAttributeA, PCGMetadataEntryKey InEntryKeyA, const FPCGMetadataAttributeBase* InAttributeB, PCGMetadataEntryKey InEntryKeyB)
	{
		return false;
	}

	template<typename IT = T, typename TEnableIf<PCG::Private::MetadataTraits<IT>::CanMulDiv>::Type* = nullptr>
	bool SetDiv(PCGMetadataEntryKey ItemKey, const FPCGMetadataAttributeBase* InAttributeA, PCGMetadataEntryKey InEntryKeyA, const FPCGMetadataAttributeBase* InAttributeB, PCGMetadataEntryKey InEntryKeyB)
	{
		SetValue(ItemKey,
			PCG::Private::MetadataTraits<IT>::Div(
				static_cast<const FPCGMetadataAttribute<T>*>(InAttributeA)->GetValueFromItemKey(InEntryKeyA),
				static_cast<const FPCGMetadataAttribute<T>*>(InAttributeB)->GetValueFromItemKey(InEntryKeyB)));

		return true;
	}

	template<typename IT = T, typename TEnableIf<!PCG::Private::MetadataTraits<IT>::CanMulDiv>::Type* = nullptr>
	bool SetDiv(PCGMetadataEntryKey ItemKey, const FPCGMetadataAttributeBase* InAttributeA, PCGMetadataEntryKey InEntryKeyA, const FPCGMetadataAttributeBase* InAttributeB, PCGMetadataEntryKey InEntryKeyB)
	{
		return false;
	}

	/** Weighted/interpolated values related code */
	template<typename IT = T, typename TEnableIf<PCG::Private::MetadataTraits<IT>::CanInterpolate>::Type* = nullptr>
	void ZeroValue(PCGMetadataEntryKey ItemKey)
	{
		SetValue(ItemKey, PCG::Private::MetadataTraits<IT>::ZeroValue());
	}

	template<typename IT = T, typename TEnableIf<!PCG::Private::MetadataTraits<IT>::CanInterpolate>::Type* = nullptr>
	void ZeroValue(PCGMetadataEntryKey ItemKey)
	{
		// Intentionally empty
	}

	template<typename IT = T, typename TEnableIf<PCG::Private::MetadataTraits<IT>::CanInterpolate>::Type* = nullptr>
	void Accumulate(PCGMetadataEntryKey ItemKey, const FPCGMetadataAttributeBase* InAttribute, PCGMetadataEntryKey InEntryKey, float Weight)
	{
		SetValue(ItemKey,
			PCG::Private::MetadataTraits<IT>::WeightedSum(
				GetValueFromItemKey(ItemKey),
				static_cast<const FPCGMetadataAttribute<T>*>(InAttribute)->GetValueFromItemKey(InEntryKey),
				Weight));
	}

	template<typename IT = T, typename TEnableIf<!PCG::Private::MetadataTraits<IT>::CanInterpolate>::Type* = nullptr>
	void Accumulate(PCGMetadataEntryKey ItemKey, const FPCGMetadataAttributeBase* InAttribute, PCGMetadataEntryKey InEntryKey, float Weight)
	{
		// Empty on purpose
	}

	template<typename IT = T, typename TEnableIf<PCG::Private::MetadataTraits<IT>::CanInterpolate>::Type* = nullptr>
	void Accumulate(PCGMetadataEntryKey ItemKey, const FPCGMetadataAttributeBase* InAttribute, const TArrayView<TPair<PCGMetadataEntryKey, float>>& InWeightedKeys)
	{
		IT Value = PCG::Private::MetadataTraits<IT>::ZeroValue();
		for (const TPair<PCGMetadataEntryKey, float>& WeightedEntry : InWeightedKeys)
		{
			PCG::Private::MetadataTraits<IT>::WeightedSum(
				Value,
				static_cast<const FPCGMetadataAttribute<T>*>(InAttribute)->GetValueFromItemKey(WeightedEntry.Key),
				WeightedEntry.Value);
		}

		SetValue(ItemKey, Value);
	}

	template<typename IT = T, typename TEnableIf<!PCG::Private::MetadataTraits<IT>::CanInterpolate>::Type* = nullptr>
	void Accumulate(PCGMetadataEntryKey ItemKey, const FPCGMetadataAttributeBase* InAttribute, const TArrayView<TPair<PCGMetadataEntryKey, float>>& InWeightedKeys)
	{
		// Empty on purpose
	}

protected:
	mutable FRWLock ValueLock;
	TArray<T> Values;
	T DefaultValue = T{};
	PCGMetadataValueKey ValueKeyOffset = 0;
};

namespace PCGMetadataAttribute
{
	inline FPCGMetadataAttributeBase* AllocateEmptyAttributeFromType(int16 TypeId)
	{
#define AllocatePCGMetadataAttributeOnType(Type) case PCG::Private::MetadataTypes<Type>::Id : { return new FPCGMetadataAttribute<Type>(); } break;

		switch (TypeId)
		{
			AllocatePCGMetadataAttributeOnType(float);
			AllocatePCGMetadataAttributeOnType(double);
			AllocatePCGMetadataAttributeOnType(int32);
			AllocatePCGMetadataAttributeOnType(int64);
			AllocatePCGMetadataAttributeOnType(FVector);
			AllocatePCGMetadataAttributeOnType(FVector4);
			AllocatePCGMetadataAttributeOnType(FQuat);
			AllocatePCGMetadataAttributeOnType(FTransform);
			AllocatePCGMetadataAttributeOnType(FString);
			AllocatePCGMetadataAttributeOnType(bool);
			AllocatePCGMetadataAttributeOnType(FRotator);
			AllocatePCGMetadataAttributeOnType(FName);

		default:
			return nullptr;
		}

#undef AllocatePCGMetadataAttributeOnType
	}
}