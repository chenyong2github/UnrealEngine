// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "OnlineSubsystemTypes.h"

/*
	BASIC USAGE
		The multicast adapter maintains the same functionality as a delegate adapter

			Identity->OnLoginCompleteDelegate.BindLambda([this](int32 UserNum){...})
			->
			MakeMulticastAdapter(this, Identity->OnLoginComplete, [this](int32 UserNum){...});

		This type of adapter will unbind itself immediately after a single execution. 

		The lifetime of the adapter is a shared ptr attached to the input delegate and will live as long as that delegate is bound.
*/
namespace UE::Online {

template<typename ComponentType, typename DelegateType, typename... LambdaArgs>
class TMulticastDelegateAdapter
	: public TSharedFromThis<TMulticastDelegateAdapter<ComponentType, DelegateType, LambdaArgs...>>
{

public:
	TMulticastDelegateAdapter(TSharedRef<ComponentType>& InParent, DelegateType& InDelegate)
		: Parent(TWeakPtr<ComponentType>(InParent))
		, Delegate(InDelegate)
	{
	}

	void SetupDelegate(TUniqueFunction<void(LambdaArgs...)>&& InCallback)
	{
		TSharedPtr<TMulticastDelegateAdapter<ComponentType, DelegateType, LambdaArgs...>> SharedAnchor = this->AsShared();

		// callback exists outside of the delegate's bound lambda since that lambda is not move-only
		Callback = [this, InCallback = MoveTemp(InCallback)](LambdaArgs... Args)
		{
			InCallback(Forward<LambdaArgs>(Args)...);
		};

		Handle = Delegate.AddLambda([this, SharedAnchor](LambdaArgs... Args) mutable 
		{
			if(Parent.IsValid())
			{
				Callback(Forward<LambdaArgs>(Args)...);
			}

			SharedAnchor.Reset();
			Delegate.Remove(Handle);
		});
	}

	FDelegateHandle GetHandle()
	{
		return Handle;
	}

	TWeakPtr<TMulticastDelegateAdapter<ComponentType, DelegateType, LambdaArgs...>> AsWeak()
	{
		return TWeakPtr<TMulticastDelegateAdapter<ComponentType, DelegateType, LambdaArgs...>>(this->AsShared());
	}

private:
	TWeakPtr<ComponentType> Parent;
	TUniqueFunction<void(LambdaArgs...)> Callback;
	DelegateType& Delegate;
	FDelegateHandle Handle;
};

/*
	These set of classes allow us to accept a generic lambda and decompose its parameters into a TUniqueFunction so we have the exact list of parameters to bind/unbind the delegate to
*/
namespace Private 
{
	template <typename... Params>
	class TMAConverterHelper2
	{
	};

	template <typename CallableObject, typename TResultType, typename... ParamTypes>
	class TMAConverterHelper2<CallableObject, TResultType, ParamTypes...>
	{
	public:

		TUniqueFunction<TResultType(ParamTypes...)> GetUniqueFn(CallableObject&& Callable)
		{
			return TUniqueFunction<TResultType(ParamTypes...)>(MoveTemp(Callable));
		}

		template<typename ComponentType, typename DelegateType>
		TSharedPtr<TMulticastDelegateAdapter<ComponentType, DelegateType, ParamTypes...>> Construct(TSharedRef<ComponentType> Interface, DelegateType& InDelegate, CallableObject&& InCallback)
		{
			TSharedPtr<TMulticastDelegateAdapter<ComponentType, DelegateType, ParamTypes...>> Adapter = MakeShared<TMulticastDelegateAdapter<ComponentType, DelegateType, ParamTypes...>>(Interface, InDelegate);
			Adapter->SetupDelegate(GetUniqueFn(MoveTemp(InCallback)));
			return Adapter;
		}
	};

	template <typename CallableObject, typename = void>
	class TMAConverterHelper
	{
	};

	template <typename CallableObject, typename ReturnType, typename... ParamTypes>
	class TMAConverterHelper<CallableObject, ReturnType(ParamTypes...)>
		: public TMAConverterHelper2<CallableObject, ReturnType, ParamTypes...>
	{
	};


	template <typename CallableObject, typename ReturnType, typename ObjectType, typename... ParamTypes>
	class TMAConverterHelper<CallableObject, ReturnType(ObjectType::*)(ParamTypes...)>
		: public TMAConverterHelper2<CallableObject, ReturnType, ParamTypes...>
	{
	};

	template <typename CallableObject, typename ReturnType, typename ObjectType, typename... ParamTypes>
	class TMAConverterHelper<CallableObject, ReturnType(ObjectType::*)(ParamTypes...) const>
		: public TMAConverterHelper2<CallableObject, ReturnType, ParamTypes...>
	{
	};

	template <typename CallableObject>
	class TMAConverter
		: public TMAConverterHelper<CallableObject, decltype(&std::remove_reference_t<CallableObject>::operator())>
	{
	};
} // namespace Private (UE::Online)

template<typename ComponentType, typename DelegateType, typename Callback>
auto MakeMulticastAdapter(TSharedRef<ComponentType> Interface, DelegateType& InDelegate, Callback&& InCallback)
{
	return Private::TMAConverter<Callback>().Construct(Interface, InDelegate, MoveTemp(InCallback));
}

template<typename ComponentType, typename DelegateType, typename Callback>
auto MakeMulticastAdapter(TSharedPtr<ComponentType> Interface, DelegateType& InDelegate, Callback&& InCallback)
{
	checkf(Interface.IsValid(), TEXT("Must pass in a valid interface to MakeDelegateAdapter!"));
	return Private::TMAConverter<Callback>().Construct(Interface.ToSharedRef(), InDelegate, MoveTemp(InCallback));
}

template<typename ComponentType, typename DelegateType, typename Callback>
auto MakeMulticastAdapter(ComponentType* Interface, DelegateType& InDelegate, Callback&& InCallback)
{
	return Private::TMAConverter<Callback>().Construct(Interface->AsShared(), InDelegate, MoveTemp(InCallback));
}

/* UE::Online */ }
