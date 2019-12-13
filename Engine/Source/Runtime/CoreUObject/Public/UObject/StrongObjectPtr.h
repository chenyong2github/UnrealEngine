// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/GCObject.h"
#include "Templates/EnableIf.h"
#include "Templates/PointerIsConvertibleFromTo.h"
#include "Templates/UniqueObj.h"

namespace UE4StrongObjectPtr_Private
{
	class FInternalReferenceCollector : public FGCObject
	{
	public:
		FInternalReferenceCollector(const volatile UObject* InObject = nullptr)
			: Object(InObject)
		{
			check(IsInGameThread());
		}

		virtual ~FInternalReferenceCollector()
		{
			check(IsInGameThread() || IsInGarbageCollectorThread());
		}

		bool IsValid() const
		{
			return Object != nullptr;
		}

		template <typename UObjectType>
		FORCEINLINE UObjectType* GetAs() const
		{
			return (UObjectType*)Object;
		}

		FORCEINLINE void Set(const volatile UObject* InObject)
		{
			Object = InObject;
		}

		//~ FGCObject interface
		virtual void AddReferencedObjects(FReferenceCollector& Collector) override
		{
			Collector.AddReferencedObject(Object);
		}

		virtual FString GetReferencerName() const override
		{
			return "UE4StrongObjectPtr_Private::FInternalReferenceCollector";
		}

	private:
		const volatile UObject* Object;
	};
}

/**
 * Specific implementation of FGCObject that prevents a single UObject-based pointer from being GC'd while this guard is in scope.
 * @note This is the "full-fat" version of FGCObjectScopeGuard which uses a heap-allocated FGCObject so *can* safely be used with containers that treat types as trivially relocatable.
 */
template <typename ObjectType>
class TStrongObjectPtr
{
public:
	TStrongObjectPtr(TStrongObjectPtr&& InOther) = default;
	TStrongObjectPtr(const TStrongObjectPtr& InOther) = default;
	TStrongObjectPtr& operator=(TStrongObjectPtr&& InOther) = default;
	~TStrongObjectPtr() = default;

	FORCEINLINE_DEBUGGABLE TStrongObjectPtr(TYPE_OF_NULLPTR = nullptr)
	{
		static_assert(TPointerIsConvertibleFromTo<ObjectType, const volatile UObject>::Value, "TStrongObjectPtr can only be constructed with UObject types");
	}

	FORCEINLINE_DEBUGGABLE explicit TStrongObjectPtr(ObjectType* InObject)
		: ReferenceCollector(InObject)
	{
		static_assert(TPointerIsConvertibleFromTo<ObjectType, const volatile UObject>::Value, "TStrongObjectPtr can only be constructed with UObject types");
	}

	template <
		typename OtherObjectType,
		typename = decltype(ImplicitConv<ObjectType*>((OtherObjectType*)nullptr))
	>
	FORCEINLINE_DEBUGGABLE TStrongObjectPtr(const TStrongObjectPtr<OtherObjectType>& InOther)
		: ReferenceCollector(InOther.Get())
	{
	}

	FORCEINLINE_DEBUGGABLE TStrongObjectPtr& operator=(const TStrongObjectPtr& InOther)
	{
		// TUniqueObj is not assignable so we need to implement this instead of defaulting it.
		ReferenceCollector->Set(InOther.Get());
		return *this;
	}

	template <
		typename OtherObjectType,
		typename = decltype(ImplicitConv<ObjectType*>((OtherObjectType*)nullptr))
	>
	FORCEINLINE_DEBUGGABLE TStrongObjectPtr& operator=(const TStrongObjectPtr<OtherObjectType>& InOther)
	{
		ReferenceCollector->Set(InOther.Get());
		return *this;
	}

	FORCEINLINE_DEBUGGABLE ObjectType& operator*() const
	{
		check(IsValid());
		return *Get();
	}

	FORCEINLINE_DEBUGGABLE ObjectType* operator->() const
	{
		check(IsValid());
		return Get();
	}

	FORCEINLINE_DEBUGGABLE bool IsValid() const
	{
		return ReferenceCollector->IsValid();
	}

	FORCEINLINE_DEBUGGABLE explicit operator bool() const
	{
		return ReferenceCollector->IsValid();
	}

	FORCEINLINE_DEBUGGABLE ObjectType* Get() const
	{
		return ReferenceCollector->GetAs<ObjectType>();
	}

	FORCEINLINE_DEBUGGABLE void Reset(ObjectType* InNewObject = nullptr)
	{
		ReferenceCollector->Set(InNewObject);
	}

	FORCEINLINE_DEBUGGABLE friend uint32 GetTypeHash(const TStrongObjectPtr& InStrongObjectPtr)
	{
		return GetTypeHash(InStrongObjectPtr.Get());
	}

private:
	TUniqueObj<UE4StrongObjectPtr_Private::FInternalReferenceCollector> ReferenceCollector;
};

template <typename LHSObjectType, typename RHSObjectType>
FORCEINLINE bool operator==(const TStrongObjectPtr<LHSObjectType>& InLHS, const TStrongObjectPtr<RHSObjectType>& InRHS)
{
	return InLHS.Get() == InRHS.Get();
}

template <typename LHSObjectType, typename RHSObjectType>
FORCEINLINE bool operator!=(const TStrongObjectPtr<LHSObjectType>& InLHS, const TStrongObjectPtr<RHSObjectType>& InRHS)
{
	return InLHS.Get() != InRHS.Get();
}
