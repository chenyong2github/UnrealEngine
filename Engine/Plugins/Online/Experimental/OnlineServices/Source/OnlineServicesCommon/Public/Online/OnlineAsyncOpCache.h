// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Online/OnlineAsyncOp.h"
#include "Online/OnlineId.h"
#include "Containers/Map.h"
#include "Templates/SharedPointer.h"

namespace UE::Online {

struct CLocalUserIdDefined
{
	template <typename T>
	auto Requires() -> decltype(T::LocalUserId);
};

struct FOperationConfig
{
	float CacheExpirySeconds = 0.0f;
};

class IOnlineAnyData
{
public:
	virtual ~IOnlineAnyData() {}
	virtual FOnlineTypeName GetTypeName() const = 0;

	template <typename T>
	const T* Get() const
	{
		if (GetTypeName() == TOnlineTypeInfo<T>::GetTypeName())
		{
			return static_cast<const T*>(GetData());
		}

		return nullptr;
	}

	template <typename T>
	const T& GetRef() const
	{
		const T* Value = Get<T>();
		check(Value != nullptr);
		return *Value;
	}

protected:
	virtual const void* GetData() const = 0;
};

template <typename T, typename BaseType = IOnlineAnyData>
class TOnlineAnyData : public BaseType
{
public:
	using ValueType = std::remove_reference_t<T>;

	TOnlineAnyData(const ValueType& InData)
		: Data(InData)
	{
	}

	TOnlineAnyData(ValueType&& InData)
		: Data(MoveTemp(InData))
	{
	}

	virtual FOnlineTypeName GetTypeName() const override
	{
		return  TOnlineTypeInfo<ValueType>::GetTypeName();
	}

	const T& GetDataRef() const 
	{
		return Data;
	}

protected:
	virtual const void* GetData() const override
	{
		return &Data;
	}

	T Data;
};

template <typename OpType>
struct TAsyncOpParamsFuncs
{
	inline static bool Compare(const typename OpType::Params& First, const typename OpType::Params& Second)
	{
		bool bResult = true;
		Meta::VisitFields<typename OpType::Params>([&bResult, &First, &Second](const auto& Field)
		{
			bResult = bResult && (First.*Field.Pointer) != (Second.*Field.Pointer);
		});
		return bResult;
	}

	inline static uint32 GetTypeHash(const typename OpType::Params& Params)
	{
		using ::GetTypeHash;
		uint32 CombinedHash = 0;
		Meta::VisitFields(Params,
			[&CombinedHash](const TCHAR* FieldName, const auto& Field)
			{
				//HashCombine(CombinedHash, GetTypeHash(Field));
			});
		return CombinedHash;
	}
};

class FOnlineAsyncOpCache
{
public:
	FOnlineAsyncOpCache(const FString& ConfigPath, class FOnlineServicesCommon& InServices)
		: Services(InServices)
	{
	}

	// Create an operation
	template <typename OpType>
	TOnlineAsyncOp<OpType>& GetOp(typename OpType::Params&& Params)
	{
		TSharedRef<TOnlineAsyncOp<OpType>> Op = CreateOp<OpType>(MoveTemp(Params));

		return *Op;
	}

	// Join an existing operation or use a non-expired cached result, or create an operation that can later be joined
	template <typename OpType>
	TOnlineAsyncOp<OpType>& GetJoinableOp(typename OpType::Params&& Params)
	{
		TSharedPtr<TOnlineAsyncOp<OpType>> Op;

		// find existing op
		if (false)
		{

		}
		else
		{
			Op = CreateOp<OpType>(MoveTemp(Params));
		}

		return *Op;
	}

	// Merge with a pending operation, or create an operation
	template <typename OpType>
	TOnlineAsyncOp<OpType>& GetMergableOp(typename OpType::Params&& Params)
	{
		TSharedPtr<TOnlineAsyncOp<OpType>> Op;

		// find existing op
		if (false)
		{

		}
		else
		{
			Op = CreateOp<OpType>(MoveTemp(Params));
		}

		return *Op;
	}

	FOnlineServicesCommon& Services;

private:
	template <typename OpType>
	TSharedRef<TOnlineAsyncOp<OpType>> CreateOp(typename OpType::Params&& Params)
	{
		TUniquePtr<TWrappedOperation<OpType>> WrappedOp = MakeUnique<TWrappedOperation<OpType>>(Services, MoveTemp(Params));

		TSharedRef<TOnlineAsyncOp<OpType>> Op = WrappedOp->GetDataRef();

		if constexpr (TModels<CLocalUserIdDefined, typename OpType::Params>::Value)
		{
			// add LocalUserId to the Op Data
			Op->Data.template Set<decltype(Params.LocalUserId)>(TEXT("LocalUserId"), Op->GetParams().LocalUserId);
		}

		// TODO: This needs to be aware of operation cache expiry policy
		Operations.Add(FWrappedOperationKey(*Op), MoveTemp(WrappedOp));

		return MoveTemp(Op);
	}

	class IWrappedOperation : public IOnlineAnyData
	{
	public:
		virtual bool IsJoinable() = 0;
		virtual bool IsMergable() = 0;
	};

	template <typename OpType>
	class TWrappedOperation : public TOnlineAnyData<TSharedRef<TOnlineAsyncOp<OpType>>, IWrappedOperation>
	{
	public:
		template <typename... ParamTypes>
		TWrappedOperation(ParamTypes&&... Params)
			: TOnlineAnyData<TSharedRef<TOnlineAsyncOp<OpType>>, IWrappedOperation>(MakeShared<TOnlineAsyncOp<OpType>>(Forward<ParamTypes>(Params)...))
		{
		}

		virtual bool IsJoinable() override { return true; }
		virtual bool IsMergable() override { return false; }
	};

	class FWrappedOperationKey
	{
	public:
		template <typename OpType>
		FWrappedOperationKey(const TOnlineAsyncOp<OpType>& Operation)
			: Impl(new TWrappedOperationKeyImpl<OpType>(Operation.GetParams()))
		{
		}

		bool operator==(const FWrappedOperationKey& Other) const
		{
			return Impl->Compare(*Other.Impl);
		}

		uint32 GetTypeHash() const
		{
			return Impl->GetTypeHash();
		}

	private:
		class IWrappedOperationKeyImpl : public IOnlineAnyData
		{
		public:
			virtual bool Compare(const IWrappedOperationKeyImpl& Other) const = 0;
			virtual uint32 GetTypeHash() const = 0;
		};

		template <typename OpType>
		class TWrappedOperationKeyImpl : public TOnlineAnyData<const typename OpType::Params&, IWrappedOperationKeyImpl>
		{
		public:
			using DataType = const typename OpType::Params&;
			using ValueType = const typename OpType::Params;

			TWrappedOperationKeyImpl(DataType& ParamsRef)
				: TOnlineAnyData<DataType&, IWrappedOperationKeyImpl>(ParamsRef)
			{
			}

			virtual bool Compare(const IWrappedOperationKeyImpl& Other) const override
			{
				if (Other.GetTypeName() == this->GetTypeName())
				{
					return TAsyncOpParamsFuncs<OpType>::Compare(this->template GetRef<ValueType>(), Other.template GetRef<ValueType>());
				}

				return false;
			}

			virtual uint32 GetTypeHash() const override
			{
				const uint32 Hash = TAsyncOpParamsFuncs<OpType>::GetTypeHash(this->template GetRef<ValueType>());
				return HashCombine(::UE::Online::GetTypeHash(this->GetTypeName()), Hash);
			}
		};

		TUniquePtr<IWrappedOperationKeyImpl> Impl;
	};

	friend uint32 GetTypeHash(const FWrappedOperationKey& Key);

	TMap<FWrappedOperationKey, TUniquePtr<IWrappedOperation>> Operations;
	TMap<FAccountId, TMap<FWrappedOperationKey, TUniquePtr<IWrappedOperation>>> UserOperations;
};

inline uint32 GetTypeHash(const FOnlineAsyncOpCache::FWrappedOperationKey& Key)
{
	return Key.GetTypeHash();
}

/* UE::Online */ }

#include "OnlineAsyncOpCache_Meta.inl"
