// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/Engine.h"
#include "MagicLeapTypes.generated.h"

/** Contains the result of a magic leap plugin/module call from blueprints. */
USTRUCT(BlueprintType)
struct MAGICLEAP_API FMagicLeapResult
{
public:
	GENERATED_BODY()

	FMagicLeapResult()
	: bSuccess(false)
	{}

	FMagicLeapResult(bool InSuccess, FString InExtraInfo = FString())
	: bSuccess(InSuccess)
	, AdditionalInfo(InExtraInfo)
	{
	}

	/** The success of the operation. */
	UPROPERTY(BlueprintReadOnly, Category = "MagicLeap")
	bool bSuccess;

	/** Optional information about the result of the operation. */
	UPROPERTY(BlueprintReadOnly, Category = "MagicLeap")
	FString AdditionalInfo;
};

struct FMLSingleDelegate
{
	FMLSingleDelegate()
	: Type(EType::None)
	{
	}

	enum EType
	{
		None,
		Static,
		Dynamic,
		Multicast,
	} Type;
};

template<typename TStaticDelegate, typename TDynamicDelegate, typename TMulticastDelegate, typename Param>
struct TMLSingleDelegateOneParam : public FMLSingleDelegate
{
	TMLSingleDelegateOneParam(const TMLSingleDelegateOneParam& Other)
	{
		*this = Other;
	}

	explicit TMLSingleDelegateOneParam(const TStaticDelegate& ResultDelegate)
	{
		Type = EType::Static;
		new (&CB.StaticDelegate) TStaticDelegate(ResultDelegate);
	}

	explicit TMLSingleDelegateOneParam(const TDynamicDelegate& ResultDelegate)
	{
		Type = EType::Dynamic;
		new (&CB.DynamicDelegate) TDynamicDelegate(ResultDelegate);
	}

	explicit TMLSingleDelegateOneParam(const TMulticastDelegate& ResultDelegate)
	{
		Type = EType::Multicast;
		new (&CB.MulticastDelegate) TMulticastDelegate(ResultDelegate);
	}

	TMLSingleDelegateOneParam& operator=(const TMLSingleDelegateOneParam& Other)
	{
		Type = Other.Type;
		switch (Other.Type)
		{
		case EType::None: break;
		case EType::Static: new (&CB.StaticDelegate) TStaticDelegate(*Other.CB.StaticDelegate.GetTypedPtr()); break;
		case EType::Dynamic: new (&CB.DynamicDelegate) TDynamicDelegate(*Other.CB.DynamicDelegate.GetTypedPtr()); break;
		case EType::Multicast: new (&CB.MulticastDelegate) TMulticastDelegate(*Other.CB.MulticastDelegate.GetTypedPtr()); break;
		}

		return *this;
	}

	void Call(const Param& InParam)
	{
		switch (Type)
		{
		case EType::None: checkf(false, TEXT("Delegate was not initialized!")); break;
		case EType::Static: CB.StaticDelegate.GetTypedPtr()->ExecuteIfBound(InParam); break;
		case EType::Dynamic: CB.DynamicDelegate.GetTypedPtr()->ExecuteIfBound(InParam); break;
		case EType::Multicast: CB.MulticastDelegate.GetTypedPtr()->Broadcast(InParam); break;
		}
	}

	union
	{
		TTypeCompatibleBytes<TStaticDelegate> StaticDelegate;
		TTypeCompatibleBytes<TDynamicDelegate> DynamicDelegate;
		TTypeCompatibleBytes<TMulticastDelegate> MulticastDelegate;
	} CB;
};

template<typename TStaticDelegate, typename TDynamicDelegate, typename TMulticastDelegate, typename Param1, typename Param2>
struct TMLSingleDelegateTwoParams : public FMLSingleDelegate
{
	TMLSingleDelegateTwoParams(const TMLSingleDelegateTwoParams& Other)
	{
		*this = Other;
	}

	explicit TMLSingleDelegateTwoParams(const TStaticDelegate& ResultDelegate)
	{
		Type = EType::Static;
		new (&CB.StaticDelegate) TStaticDelegate(ResultDelegate);
	}

	explicit TMLSingleDelegateTwoParams(const TDynamicDelegate& ResultDelegate)
	{
		Type = EType::Dynamic;
		new (&CB.DynamicDelegate) TDynamicDelegate(ResultDelegate);
	}

	explicit TMLSingleDelegateTwoParams(const TMulticastDelegate& ResultDelegate)
	{
		Type = EType::Multicast;
		new (&CB.MulticastDelegate) TMulticastDelegate(ResultDelegate);
	}

	TMLSingleDelegateTwoParams& operator=(const TMLSingleDelegateTwoParams& Other)
	{
		Type = Other.Type;
		switch (Other.Type)
		{
		case EType::None: break;
		case EType::Static: new (&CB.StaticDelegate) TStaticDelegate(*Other.CB.StaticDelegate.GetTypedPtr()); break;
		case EType::Dynamic: new (&CB.DynamicDelegate) TDynamicDelegate(*Other.CB.DynamicDelegate.GetTypedPtr()); break;
		case EType::Multicast: new (&CB.MulticastDelegate) TMulticastDelegate(*Other.CB.MulticastDelegate.GetTypedPtr()); break;
		}

		return *this;
	}

	void Call(const Param1& InParam1, const Param2& InParam2)
	{
		switch (Type)
		{
		case EType::None: checkf(false, TEXT("Delegate was not initialized!")); break;
		case EType::Static: CB.StaticDelegate.GetTypedPtr()->ExecuteIfBound(InParam1, InParam2); break;
		case EType::Dynamic: CB.DynamicDelegate.GetTypedPtr()->ExecuteIfBound(InParam1, InParam2); break;
		case EType::Multicast: CB.MulticastDelegate.GetTypedPtr()->Broadcast(InParam1, InParam2); break;
		}
	}

	union
	{
		TTypeCompatibleBytes<TStaticDelegate> StaticDelegate;
		TTypeCompatibleBytes<TDynamicDelegate> DynamicDelegate;
		TTypeCompatibleBytes<TMulticastDelegate> MulticastDelegate;
	} CB;
};

template<typename TStaticDelegate, typename TDynamicDelegate, typename TMulticastDelegate, typename Param1, typename Param2, typename Param3>
struct TMLSingleDelegateThreeParams : public FMLSingleDelegate
{
	TMLSingleDelegateThreeParams(const TMLSingleDelegateThreeParams& Other)
	{
		*this = Other;
	}

	explicit TMLSingleDelegateThreeParams(const TStaticDelegate& ResultDelegate)
	{
		Type = EType::Static;
		new (&CB.StaticDelegate) TStaticDelegate(ResultDelegate);
	}

	explicit TMLSingleDelegateThreeParams(const TDynamicDelegate& ResultDelegate)
	{
		Type = EType::Dynamic;
		new (&CB.DynamicDelegate) TDynamicDelegate(ResultDelegate);
	}

	explicit TMLSingleDelegateThreeParams(const TMulticastDelegate& ResultDelegate)
	{
		Type = EType::Multicast;
		new (&CB.MulticastDelegate) TMulticastDelegate(ResultDelegate);
	}

	TMLSingleDelegateThreeParams& operator=(const TMLSingleDelegateThreeParams& Other)
	{
		Type = Other.Type;
		switch (Other.Type)
		{
		case EType::None: break;
		case EType::Static: new (&CB.StaticDelegate) TStaticDelegate(*Other.CB.StaticDelegate.GetTypedPtr()); break;
		case EType::Dynamic: new (&CB.DynamicDelegate) TDynamicDelegate(*Other.CB.DynamicDelegate.GetTypedPtr()); break;
		case EType::Multicast: new (&CB.MulticastDelegate) TMulticastDelegate(*Other.CB.MulticastDelegate.GetTypedPtr()); break;
		}

		return *this;
	}

	void Call(const Param1& InParam1, const Param2& InParam2, const Param3& InParam3)
	{
		switch (Type)
		{
		case EType::None: checkf(false, TEXT("Delegate was not initialized!")); break;
		case EType::Static: CB.StaticDelegate.GetTypedPtr()->ExecuteIfBound(InParam1, InParam2, InParam3); break;
		case EType::Dynamic: CB.DynamicDelegate.GetTypedPtr()->ExecuteIfBound(InParam1, InParam2, InParam3); break;
		case EType::Multicast: CB.MulticastDelegate.GetTypedPtr()->Broadcast(InParam1, InParam2, InParam3); break;
		}
	}

	union
	{
		TTypeCompatibleBytes<TStaticDelegate> StaticDelegate;
		TTypeCompatibleBytes<TDynamicDelegate> DynamicDelegate;
		TTypeCompatibleBytes<TMulticastDelegate> MulticastDelegate;
	} CB;
};

template<typename TStaticDelegate, typename TDynamicDelegate, typename TMulticastDelegate, typename Param>
struct TMLMultiDelegateOneParam
{
	TMLMultiDelegateOneParam()
	{
	}

	TMLMultiDelegateOneParam(const TStaticDelegate& InStaticDelegate)
	: StaticDelegate(InStaticDelegate)
	{
	}

	TMLMultiDelegateOneParam(const TDynamicDelegate& InDynamicDelegate)
	: DynamicDelegate(InDynamicDelegate)
	{
	}

	TMLMultiDelegateOneParam(const TMulticastDelegate& InMulticastDelegate)
	: MulticastDelegate(InMulticastDelegate)
	{
	}

	void Call(const Param& InParam) const
	{
		StaticDelegate.ExecuteIfBound(InParam);
		DynamicDelegate.ExecuteIfBound(InParam);
		MulticastDelegate.Broadcast(InParam);
	}

	TStaticDelegate StaticDelegate;
	TDynamicDelegate DynamicDelegate;
	TMulticastDelegate MulticastDelegate;
};

template<typename TStaticDelegate, typename TDynamicDelegate, typename TMulticastDelegate, typename Param1, typename Param2>
struct TMLMultiDelegateTwoParams
{
	TMLMultiDelegateTwoParams()
	{
	}

	TMLMultiDelegateTwoParams(const TStaticDelegate& InStaticDelegate)
	: StaticDelegate(InStaticDelegate)
	{
	}

	TMLMultiDelegateTwoParams(const TDynamicDelegate& InDynamicDelegate)
	: DynamicDelegate(InDynamicDelegate)
	{
	}

	TMLMultiDelegateTwoParams(const TMulticastDelegate& InMulticastDelegate)
	: MulticastDelegate(InMulticastDelegate)
	{
	}

	void Call(const Param1& InParam1, const Param2& InParam2) const
	{
		StaticDelegate.ExecuteIfBound(InParam1, InParam2);
		DynamicDelegate.ExecuteIfBound(InParam1, InParam2);
		MulticastDelegate.Broadcast(InParam1, InParam2);
	}

	TStaticDelegate StaticDelegate;
	TDynamicDelegate DynamicDelegate;
	TMulticastDelegate MulticastDelegate;
};

template<typename TStaticDelegate, typename TDynamicDelegate, typename TMulticastDelegate, typename Param1, typename Param2, typename Param3>
struct TMLMultiDelegateThreeParams
{
	TMLMultiDelegateThreeParams()
	{
	}

	TMLMultiDelegateThreeParams(const TStaticDelegate& InStaticDelegate)
	: StaticDelegate(InStaticDelegate)
	{
	}

	TMLMultiDelegateThreeParams(const TDynamicDelegate& InDynamicDelegate)
	: DynamicDelegate(InDynamicDelegate)
	{
	}

	TMLMultiDelegateThreeParams(const TMulticastDelegate& InMulticastDelegate)
	: MulticastDelegate(InMulticastDelegate)
	{
	}

	void Call(const Param1& InParam1, const Param2& InParam2, const Param3& InParam3) const
	{
		StaticDelegate.ExecuteIfBound(InParam1, InParam2, InParam3);
		DynamicDelegate.ExecuteIfBound(InParam1, InParam2, InParam3);
		MulticastDelegate.Broadcast(InParam1, InParam2, InParam3);
	}

	TStaticDelegate StaticDelegate;
	TDynamicDelegate DynamicDelegate;
	TMulticastDelegate MulticastDelegate;
};

#define DECLARE_MLDELEGATE_OneParam(DelegateName, Param1Type, Param1Name) \
	DECLARE_DELEGATE_OneParam(DelegateName##Static, Param1Type); \
	DECLARE_DYNAMIC_DELEGATE_OneParam(DelegateName##Dynamic, Param1Type, Param1Name); \
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(DelegateName##Multi, Param1Type, Param1Name); \
	typedef TMLMultiDelegateOneParam<DelegateName##Static, DelegateName##Dynamic, DelegateName##Multi, Param1Type> DelegateName;
