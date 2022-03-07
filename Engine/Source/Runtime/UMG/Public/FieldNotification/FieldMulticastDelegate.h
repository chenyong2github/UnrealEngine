// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Delegates/Delegate.h"
#include "FieldNotification/IFieldValueChanged.h"
#include "UObject/Object.h"

#include <limits>

namespace UE::FieldNotification
{
	
class UMG_API FFieldMulticastDelegate
{
public:
	using FDelegate = INotifyFieldValueChanged::FFieldValueChangedDelegate;

private:
	struct FInstanceProtectedDelegate : public TDelegateBase<FDefaultDelegateUserPolicy>
	{
		using Super = TDelegateBase<FDefaultDelegateUserPolicy>;
		using Super::GetDelegateInstanceProtected;
	};

	struct FInvocationKey
	{
		TWeakObjectPtr<const UObject> Object;
		FFieldId Id;
		bool operator<(const FInvocationKey& Element) const
		{
			return Id.GetName().FastLess(Element.Id.GetName());
		}
	};

	struct FInvocationElement
	{
		FInvocationKey Key;
		FDelegate Delegate;
		bool operator<(const FInvocationElement& InElement) const
		{
			return Key < InElement.Key;
		}
	};

	friend bool operator<(const FInvocationElement& A, const FInvocationKey& B)
	{
		return A.Key < B;
	}
	friend bool operator<(const FInvocationKey& A, const FInvocationElement& B)
	{
		return A < B.Key;
	}
	friend bool operator<(const FInvocationElement& A, const FFieldId& B)
	{
		return A.Key.Id.GetName().FastLess(B.GetName());
	}
	friend bool operator<(const FFieldId& A, const FInvocationElement& B)
	{
		return A.GetName().FastLess(B.Key.Id.GetName());
	}

	using InvocationListType = TArray<FInvocationElement>;

public:
	FDelegateHandle Add(const UObject* InObject, FFieldId InFieldId, FDelegate InNewDelegate);

	struct FRemoveResult
	{
		bool bRemoved = false;
		bool bHasOtherBoundDelegates = false;
		const UObject* Object = nullptr;
		FFieldId FieldId;
	};
	FRemoveResult Remove(FDelegateHandle InDelegate);

	struct FRemoveFromResult
	{
		bool bRemoved = false;
		bool bHasOtherBoundDelegates = false;
	};
	FRemoveFromResult RemoveFrom(const UObject* InObject, FFieldId InFieldId, FDelegateHandle InDelegate);

	struct FRemoveAllResult
	{
		int32 RemoveCount = 0;
		TBitArray<> HasFields;
	};
	FRemoveAllResult RemoveAll(const UObject* InObject, const void* InUserObject);
	FRemoveAllResult RemoveAll(const UObject* InObject, FFieldId InFieldId, const void* InUserObject);

	void Broadcast(UObject* InObject, FFieldId InFieldId);

	void Reset();

private:
	int32 LowerBound(FFieldId InFieldId) const
	{
		int32 Num = AddedEmplaceAt > GetNum(Delegates) ? GetNum(Delegates) : AddedEmplaceAt;
		return AlgoImpl::LowerBoundInternal(GetData(Delegates), Num, InFieldId, FIdentityFunctor(), TLess<>());
	}

	int32 LowerBound(const FInvocationKey& InKey) const
	{
		int32 Num = AddedEmplaceAt > GetNum(Delegates) ? GetNum(Delegates) : AddedEmplaceAt;
		return AlgoImpl::LowerBoundInternal(GetData(Delegates), Num, InKey, FIdentityFunctor(), TLess<>());
	}

	int32 UpperBound(FFieldId InFieldId) const
	{
		int32 Num = AddedEmplaceAt > GetNum(Delegates) ? GetNum(Delegates) : AddedEmplaceAt;
		return AlgoImpl::UpperBoundInternal(GetData(Delegates), Num, InFieldId, FIdentityFunctor(), TLess<>());
	}

	int32 UpperBound(const FInvocationKey& InKey) const
	{
		int32 Num = AddedEmplaceAt > GetNum(Delegates) ? GetNum(Delegates) : AddedEmplaceAt;
		return AlgoImpl::UpperBoundInternal(GetData(Delegates), Num, InKey, FIdentityFunctor(), TLess<>());
	}

	const IDelegateInstance* GetDelegateInstance(FDelegate& InDelegate) const
	{
		return static_cast<FInstanceProtectedDelegate&>(static_cast<TDelegateBase<FDefaultDelegateUserPolicy>&>(InDelegate)).GetDelegateInstanceProtected();
	}

	void ExecuteLockOperations();

private:
	InvocationListType Delegates;
	int16 DelegateLockCount = 0;
	int16 CompactionCount = 0;
	uint16 AddedEmplaceAt = std::numeric_limits<uint16>::max();
};

} //namespace