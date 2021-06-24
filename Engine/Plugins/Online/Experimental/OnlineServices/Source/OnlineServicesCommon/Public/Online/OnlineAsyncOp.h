// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Online/OnlineAsyncOpHandle.h"
#include "Online/OnlineTypeInfo.h"

#include "Containers/Map.h"
#include "Templates/UniquePtr.h"
#include "Async/Future.h"

namespace UE::Online {

class FOnlineServicesCommon;

// TEMP
namespace Errors
{
	inline FOnlineError Unknown() { return FOnlineError();  }
}
// END TEMP

template <typename OpType> class TOnlineAsyncOp;
template <typename OpType, typename T> class TOnlineChainableAsyncOp;

enum class EOnlineAsyncExecutionPolicy
{
	RunOnGameThread
	// TODO: Add/implement others
};

// TODO
class FOnlineAsyncExecutionPolicy
{
public:
	FOnlineAsyncExecutionPolicy(EOnlineAsyncExecutionPolicy InExecutionPolicy)
		: ExecutionPolicy(InExecutionPolicy)
	{
	}

	static FOnlineAsyncExecutionPolicy RunOnGameThread() { return FOnlineAsyncExecutionPolicy(EOnlineAsyncExecutionPolicy::RunOnGameThread); }

	EOnlineAsyncExecutionPolicy GetExecutionPolicy() const { return ExecutionPolicy; }

private:
	EOnlineAsyncExecutionPolicy ExecutionPolicy;
};


namespace Private
{

class FOnlineOperationData // Map of (TypeName,Key)->(data of any Type)
{
public:
	template <typename T>
	void Set(const FString& Key, T&& InData)
	{
		Data.Add(FOperationDataKey{ TOnlineTypeInfo<T>::GetTypeName(), Key }, MakeUnique<TData<T>>(MoveTemp(InData)));
	}

	template <typename T>
	void Set(const FString& Key, const T& InData)
	{
		Data.Add(FOperationDataKey{ TOnlineTypeInfo<T>::GetTypeName(), Key }, MakeUnique<TData<T>>(InData));
	}

	template <typename T>
	const T* Get(const FString& Key) const
	{
		if (auto Value = Data.Find(FOperationDataKey{ TOnlineTypeInfo<T>::GetTypeName(), Key }))
		{
			return static_cast<const T*>((*Value)->GetData());
		}

		return nullptr;
	}

	struct FOperationDataKey
	{
		FOnlineTypeName TypeName;
		FString Key;

		bool operator==(const FOperationDataKey& Other) const
		{
			return TypeName == Other.TypeName && Key == Other.Key;
		}
	};

private:
	class IData
	{
	public:
		virtual ~IData() {}
		virtual FOnlineTypeName GetTypeName() = 0;
		virtual void* GetData() = 0;

		template <typename T>
		const T* Get()
		{
			if (GetTypeName() == TOnlineTypeInfo<T>::GetTypeName())
			{
				return static_cast<T*>(GetData());
			}

			return nullptr;
		}
	};

	template <typename T>
	class TData : public IData
	{
	public:
		TData(const T& InData)
			: Data(InData)
		{
		}

		TData(T&& InData)
			: Data(MoveTemp(InData))
		{
		}

		virtual FOnlineTypeName GetTypeName() override
		{
			return  TOnlineTypeInfo<T>::GetTypeName();
		}

		virtual void* GetData() override
		{
			return &Data;
		}

	private:
		T Data;
	};

	friend uint32 GetTypeHash(const FOperationDataKey& Key);

	TMap<FOperationDataKey, TUniquePtr<IData>> Data;
};

inline uint32 GetTypeHash(const FOnlineOperationData::FOperationDataKey& Key)
{
	using ::GetTypeHash;
	return HashCombine(GetTypeHash(Key.TypeName), GetTypeHash(Key.Key));
}

template <typename T>
struct TUnwrapFuture
{
	using Type = T;
};

template <typename T>
struct TUnwrapFuture<TFuture<T>>
{
	using Type = T;
};

template <typename T>
using TUnwrapFuture_T = typename TUnwrapFuture<T>::Type;

template<typename ResultType>
inline void SetPromiseValue(TPromise<ResultType>& Promise, TFuture<ResultType>&& Future)
{
	Promise.SetValue(Future.Get());
}

template <typename T>
inline void SetPromiseValue(TPromise<void>& Promise, TFuture<T>&& Future)
{
	Promise.SetValue();
}

template<typename Func, typename... ParamTypes, typename ResultType>
inline void SetPromiseValueFromCallable(TPromise<ResultType>& Promise, Func& Function, ParamTypes&&... Params)
{
	Promise.SetValue(Function(Forward<ParamTypes>(Params)...));
}
template<typename Func, typename... ParamTypes>
inline void SetPromiseValueFromCallable(TPromise<void>& Promise, Func& Function, ParamTypes&&... Params)
{
	Function(Forward<ParamTypes>(Params)...);
	Promise.SetValue();
}

template <typename T>
struct TAsyncOpHelper
{
	template<typename Func, typename AsyncOpType, typename FutureValueType>
	inline static auto Call(Func& Function, AsyncOpType& Op, TFuture<FutureValueType>& Value) -> decltype(Invoke(Function, Op, Value.Get()))
	{
		return Invoke(Function, Op, Value.Get());
	}
};

template <>
struct TAsyncOpHelper<void>
{
	template<typename Func, typename AsyncOpType, typename FutureValueType>
	inline static auto Call(Func& Function, AsyncOpType& Op, TFuture<FutureValueType>& Value) -> decltype(Invoke(Function, Op))
	{
		return Invoke(Function, Op);
	}
};

template <typename CallableType, typename... ParamTypes>
struct TChainableAsyncOpCallableResultHelper
{
	using Type = TInvokeResult_T<CallableType, ParamTypes...>;
};

template <typename CallableType, typename OpType>
struct TChainableAsyncOpCallableResultHelper<CallableType, TOnlineAsyncOp<OpType>&, void>
{
	using Type = TInvokeResult_T<CallableType, TOnlineAsyncOp<OpType>&>;
};

template <typename CallableType, typename... ParamTypes>
using TChainableAsyncOpCallableResultHelper_T = typename TChainableAsyncOpCallableResultHelper<CallableType, ParamTypes...>::Type;

template <typename OpType, typename CallableType, typename LastResultType>
TOnlineChainableAsyncOp<OpType, Private::TUnwrapFuture_T<TChainableAsyncOpCallableResultHelper_T<CallableType, TOnlineAsyncOp<OpType>&, LastResultType>>> CreateChainableAsyncOp(TOnlineAsyncOp<OpType>& Op, TFuture<LastResultType>& LastResult, CallableType&& InCallable, FOnlineAsyncExecutionPolicy ExecutionPolicy)
{
	using ReturnType = TChainableAsyncOpCallableResultHelper_T<CallableType, TOnlineAsyncOp<OpType>&, LastResultType>;
	using UnwrappedReturnType = Private::TUnwrapFuture_T<ReturnType>;

	TPromise<UnwrappedReturnType> Promise;
	TFuture<UnwrappedReturnType> Future = Promise.GetFuture();

	using LastResultContinuationFutureType = std::conditional_t<std::is_same_v<void, LastResultType>, TFuture<int>, TFuture<LastResultType>>;
	LastResult.Then([ExecutionPolicy, Callable = MoveTemp(InCallable), WeakOp = TWeakPtr<TOnlineAsyncOp<OpType>>(Op.AsShared()), MovedPromise = MoveTemp(Promise)](LastResultContinuationFutureType&& Result) mutable // mutable so we can set the promise value
	{
		TSharedPtr<TOnlineAsyncOp<OpType>> PinnedOp = WeakOp.Pin();
		if (PinnedOp && !PinnedOp->IsComplete())
		{
			PinnedOp->GetServices().Execute(ExecutionPolicy, [Callable2 = MoveTemp(Callable), WeakOp, MovedPromise2 = MoveTemp(MovedPromise), MovedResult = MoveTemp(Result)]() mutable // mutable so we can set the promise value
			{
				TSharedPtr<TOnlineAsyncOp<OpType>> PinnedOp2 = WeakOp.Pin();
				if (PinnedOp2 && !PinnedOp2->IsComplete())
				{
					if constexpr (std::is_same_v<ReturnType, TFuture<UnwrappedReturnType>>) // return type is a future
					{
						using ContinuationFutureType = std::conditional_t<std::is_same_v<void, UnwrappedReturnType>, TFuture<int>, TFuture<UnwrappedReturnType>>; // TFuture<void> continuation uses a TFuture<int>
						TAsyncOpHelper<LastResultType>::Call(Callable2, *PinnedOp2, MovedResult).Then([WeakOp, MovedPromise3 = MoveTemp(MovedPromise2)](ContinuationFutureType&& Value) mutable // mutable so we can set the promise value
						{
							TSharedPtr<TOnlineAsyncOp<OpType>> PinnedOp3 = WeakOp.Pin();
							if (PinnedOp3 && !PinnedOp3->IsComplete())
							{
								Private::SetPromiseValue(MovedPromise3, MoveTemp(Value));
							}
						});
					}
					else
					{
						if constexpr (std::is_same_v<void, LastResultType>)
						{
							Private::SetPromiseValueFromCallable(MovedPromise2, Callable2, *PinnedOp2);
						}
						else
						{
							Private::SetPromiseValueFromCallable(MovedPromise2, Callable2, *PinnedOp2, Forward<LastResultType>(MovedResult.Get()));
						}
					}
				}
			});
		}
	});

	return TOnlineChainableAsyncOp<OpType, UnwrappedReturnType>(Op, MoveTemp(Future));
}

/* Private */ }

template <typename OpType, typename T>
class TOnlineChainableAsyncOp
{
public:
	TOnlineChainableAsyncOp(TOnlineAsyncOp<OpType>& InOwningOperation, TFuture<T>&& InLastResult)
		: OwningOperation(InOwningOperation)
		, LastResult(MoveTemp(InLastResult))
	{
	}

	TOnlineChainableAsyncOp(TOnlineChainableAsyncOp&& Other)
		: OwningOperation(Other.OwningOperation)
		, LastResult(MoveTemp(Other.LastResult))
	{
	}

	TOnlineChainableAsyncOp& operator=(TOnlineChainableAsyncOp&& Other)
	{
		check(&OwningOperation == &Other.OwningOperation); // Can't reassign this
		LastResult = MoveTemp(Other.LastResult);
		return *this;
	}

	// Callable that takes a FOnlineAsyncOp& or TOnlineAsyncOp<OpType>& and a const T& and returns a Type or TFuture<Type>
	template <typename CallableType>
	TOnlineChainableAsyncOp<OpType, Private::TUnwrapFuture_T<Private::TChainableAsyncOpCallableResultHelper_T<CallableType, TOnlineAsyncOp<OpType>&, T>>>
		Then(CallableType&& InCallable, FOnlineAsyncExecutionPolicy ExecutionPolicy = FOnlineAsyncExecutionPolicy::RunOnGameThread())
	{
		return Private::CreateChainableAsyncOp<OpType>(OwningOperation, LastResult, InCallable, ExecutionPolicy);
	}

	// placeholder
	template <typename... U>
	void Enqueue(U&&...)
	{
		static_assert(std::is_same_v<T, void>, "Continuation result discarded. Continuation prior to calling Enqueue must have a void or TFuture<void> return type.");
		OwningOperation.Enqueue();
	}

protected:
	TOnlineAsyncOp<OpType>& OwningOperation;
	TFuture<T> LastResult;
};

class FOnlineAsyncOp
{
public:
	virtual ~FOnlineAsyncOp() {}

	Private::FOnlineOperationData Data;

	virtual void SetError(FOnlineError&& Error) = 0;
};

// This class represents an async operation on the public interface
// There may be one or more handles pointing to one instance
template <typename OpType>
class TOnlineAsyncOp 
	: public FOnlineAsyncOp
	, public TSharedFromThis<TOnlineAsyncOp<OpType>>
{
public:
	using ParamsType = typename OpType::Params;
	using ResultType = typename OpType::Result;

	TOnlineAsyncOp(FOnlineServicesCommon& InServices, ParamsType&& Params)
		: Services(InServices)
		, SharedState(MakeShared<FAsyncOpSharedState>(MoveTemp(Params)))
	{
	}

	~TOnlineAsyncOp()
	{
	}

	bool IsReady() const
	{
		return SharedState->State != EAsyncOpState::Invalid;
	}

	bool IsComplete() const
	{
		return SharedState->State >= EAsyncOpState::Complete;
	}

	const ParamsType& GetParams() const
	{
		return SharedState->Params;
	}

	// Callable that takes a FOnlineAsyncOp& or TOnlineAsyncOp<OpType>& and returns a Type or TFuture<Type>
	template <typename CallableType>
	TOnlineChainableAsyncOp<OpType, Private::TUnwrapFuture_T<TInvokeResult_T<CallableType, TOnlineAsyncOp<OpType>&>>>
		Then(CallableType&& InCallable, FOnlineAsyncExecutionPolicy ExecutionPolicy = FOnlineAsyncExecutionPolicy::RunOnGameThread())
	{
		TFuture<void> StartExecutionFuture = StartExecutionPromise.GetFuture();
		return Private::CreateChainableAsyncOp<OpType>(*this, StartExecutionFuture, InCallable, ExecutionPolicy);
	}

	operator TOnlineChainableAsyncOp<OpType, void>()
	{
		return TOnlineChainableAsyncOp<OpType, void>(*this, StartExecutionPromise.GetFuture());
	}

	static TOnlineAsyncOp<OpType> CreateError(const FOnlineError& Error) { return TOnlineAsyncOp<OpType>(); }

	TOnlineAsyncOpHandle<OpType> GetHandle()
	{
		return TOnlineAsyncOpHandle<OpType>(CreateSharedState());
	}

	void Cancel(const FOnlineError& Reason)
	{
		SetError(Reason);
		SharedState->State = EAsyncOpState::Cancelled;
	}

	void SetResult(ResultType&& InResult)
	{
		SharedState->Result = TOnlineResult<ResultType>(MoveTemp(InResult));
		SharedState->State = EAsyncOpState::Complete;

		TriggerOnComplete(SharedState->Result);
	}

	virtual void SetError(FOnlineError&& Error)
	{
		SharedState->Result = TOnlineResult<ResultType>(MoveTemp(Error));
		SharedState->State = EAsyncOpState::Complete;

		TriggerOnComplete(SharedState->Result);
	}

	FOnlineServicesCommon& GetServices() { return Services; }

	template <typename... U>
	void Enqueue(U&&...)
	{
		// placeholder
		StartExecutionPromise.SetValue();
	}

protected:
	FOnlineServicesCommon& Services;

	void TriggerOnComplete(const TOnlineResult<ResultType>& Result)
	{
		TArray<TSharedRef<FAsyncOpSharedHandleState>> SharedHandleStatesCopy(SharedHandleStates);
		for (TSharedRef<FAsyncOpSharedHandleState>& SharedHandleState : SharedHandleStatesCopy)
		{
			SharedHandleState->TriggerOnComplete(Result);
		}
	}

	class FAsyncOpSharedState
	{
	public:
		FAsyncOpSharedState(ParamsType&& InParams)
			: Params(MoveTemp(InParams))
		{
		}

		ParamsType Params;
		// This will need to be protected with a mutex if we want to allow this to be set from multiple threads (eg, set result from a task graph thread, while allowing this to be cancelled from the game thread)
		TOnlineResult<ResultType> Result{ Errors::Unknown() };
		EAsyncOpState State = EAsyncOpState::Invalid;

		bool IsComplete() const
		{
			return State >= EAsyncOpState::Complete;
		}
	};

	class FAsyncOpSharedHandleState : public Private::IOnlineAsyncOpSharedState<OpType>, public TSharedFromThis<FAsyncOpSharedHandleState>
	{
	public:
		FAsyncOpSharedHandleState(const TSharedRef<TOnlineAsyncOp<OpType>>& InAsyncOp)
			: SharedState(InAsyncOp->SharedState)
			, AsyncOp(InAsyncOp)
		{
		}

		~FAsyncOpSharedHandleState()
		{
			Detach();
		}

		virtual void Cancel(const FOnlineError& Reason) override
		{
			TSharedPtr<TOnlineAsyncOp<OpType>> PinnedOP = AsyncOp.Pin();
			if (PinnedOP.IsValid())
			{
				bCancelled = true;
				TriggerOnComplete(TOnlineResult<ResultType>(Reason));
			}
		}

		virtual EAsyncOpState GetState() const override
		{
			return bCancelled ? EAsyncOpState::Cancelled : SharedState->State;
		}

		virtual void SetOnProgress(TDelegate<void(const FAsyncProgress&)>&& Function) override
		{
			OnProgressFn = MoveTemp(Function);
		}

		virtual void SetOnWillRetry(TDelegate<void(TOnlineAsyncOpHandle<OpType>& Handle, const FWillRetry&)>&& Function) override
		{
			OnWillRetryFn = MoveTemp(Function);
		}

		virtual void SetOnComplete(TDelegate<void(const TOnlineResult<ResultType>&)>&& Function) override
		{
			OnCompleteFn = MoveTemp(Function);
			if (SharedState->IsComplete())
			{
				TriggerOnComplete(SharedState->Result);
			}
		}

		void TriggerOnComplete(const TOnlineResult<ResultType>& Result)
		{
			// TODO: Execute OnCompleteFn next tick on game thread
			if (OnCompleteFn.IsBound())
			{
				OnCompleteFn.ExecuteIfBound(Result);
				OnCompleteFn.Unbind();
				Detach();
			}
		}

	private:
		void Detach()
		{
			TSharedPtr<TOnlineAsyncOp<OpType>> PinnedOp = AsyncOp.Pin();
			AsyncOp.Reset();
			if (PinnedOp.IsValid())
			{
				PinnedOp->Detach(this->AsShared());
			}
		}

		TDelegate<void(const FAsyncProgress&)> OnProgressFn;
		TDelegate<void(TOnlineAsyncOpHandle<OpType>& Handle, const FWillRetry&)> OnWillRetryFn;
		TDelegate<void(const TOnlineResult<ResultType>&)> OnCompleteFn;

		bool bCancelled = false;
		TSharedRef<FAsyncOpSharedState> SharedState;
		TWeakPtr<TOnlineAsyncOp<OpType>> AsyncOp;
	};

	void Detach(const TSharedRef<FAsyncOpSharedHandleState>& SharedHandleState)
	{
		SharedHandleStates.Remove(SharedHandleState);
	}

	TSharedRef<Private::IOnlineAsyncOpSharedState<OpType>> CreateSharedState()
	{
		TSharedRef<FAsyncOpSharedHandleState> SharedHandleState = MakeShared<FAsyncOpSharedHandleState>(this->AsShared());
		SharedHandleStates.Add(SharedHandleState);
		return StaticCastSharedRef<Private::IOnlineAsyncOpSharedState<OpType>>(SharedHandleState);
	}

	TSharedRef<FAsyncOpSharedState> SharedState;
	TArray<TSharedRef<FAsyncOpSharedHandleState>> SharedHandleStates;
	TPromise<void> StartExecutionPromise;
};

/* UE::Online */ }
