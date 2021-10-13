// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"
#include "Online/OnlineAsyncOp.h"
#include "Online/IOnlineComponent.h"
#include "Online/OnlineServicesCommon.h"

namespace UE::Online {

template <typename ComponentType>
class TOnlineComponent
	: public ComponentType
	, public IOnlineComponent
{
public:
	TOnlineComponent(const TOnlineComponent&) = delete;
	TOnlineComponent(TOnlineComponent&&) = delete;

	/**
	 */
	TOnlineComponent(const FString& ComponentName, FOnlineServicesCommon& InServices)
		: Services(InServices)
		, WeakThis(TSharedPtr<ComponentType>(InServices.AsShared(), static_cast<ComponentType*>(this)))
		, ConfigName(ComponentName)
		{
		}

	virtual void Initialize() override {}
	virtual void PostInitialize() override {}
	virtual void LoadConfig() override {}
	virtual void Tick(float DeltaSeconds) override {}
	virtual void PreShutdown() override {}
	virtual void Shutdown() override {}

	template <typename StructType>
	bool LoadConfig(StructType& Struct, const FString& OperationName = FString()) const
	{
		return static_cast<const ComponentType*>(this)->GetServices()->LoadConfig(Struct, GetConfigName(), OperationName);
	}

	const FString& GetConfigName() const { return ConfigName; }

	template <typename OpType>
	TOnlineAsyncOp<OpType>& GetOp(typename OpType::Params&& Params)
	{
		return Services.template GetOp<OpType>(MoveTemp(Params), GetConfigSectionHeiarchy(OpType::Name));
	}

	template <typename OpType, typename ParamsFuncsType = TJoinableOpParamsFuncs<OpType>>
	TOnlineAsyncOp<OpType>& GetJoinableOp(typename OpType::Params&& Params)
	{
		return Services.template GetJoinableOp<OpType, ParamsFuncsType>(MoveTemp(Params), GetConfigSectionHeiarchy(OpType::Name));
	}

	template <typename OpType, typename ParamsFuncsType = TMergeableOpParamsFuncs<OpType>>
	TOnlineAsyncOp<OpType>& GetMergeableOp(typename OpType::Params&& Params)
	{
		return Services.template GetMergeableOp<OpType, ParamsFuncsType>(MoveTemp(Params), GetConfigSectionHeiarchy(OpType::Name));
	}

	FOnlineServicesCommon& GetServices()
	{
		return Services;
	}

	// TSharedFromThis-like behaviour
	TSharedRef<ComponentType> AsShared()
	{
		TSharedPtr<ComponentType> SharedThis(WeakThis.Pin());
		check(SharedThis.Get() == this);
		return MoveTemp(SharedThis).ToSharedRef();
	}

	TSharedRef<ComponentType const> AsShared() const
	{
		TSharedPtr<ComponentType const> SharedThis(WeakThis.Pin());
		check(SharedThis.Get() == this);
		return MoveTemp(SharedThis).ToSharedRef();
	}

	TArray<FString> GetConfigSectionHeiarchy(const FString& OperationName = FString())
	{
		TArray<FString> SectionHeiarchy;
		FString SectionName = TEXT("OnlineServices");
		SectionHeiarchy.Add(SectionName);
		SectionName += TEXT(".") + GetServices().GetConfigName();
		SectionHeiarchy.Add(SectionName);
		SectionName += TEXT(".") + GetConfigName();
		SectionHeiarchy.Add(SectionName);
		if (!OperationName.IsEmpty())
		{
			SectionName += TEXT(".") + OperationName;
			SectionHeiarchy.Add(SectionName);
		}
		return SectionHeiarchy;
	}

protected:
	template <class OtherType>
	static TSharedRef<OtherType> SharedThis(OtherType* ThisPtr)
	{
		return StaticCastSharedRef<OtherType>(ThisPtr->AsShared());
	}

	template <class OtherType>
	static TSharedRef<OtherType const> SharedThis(const OtherType* ThisPtr)
	{
		return StaticCastSharedRef<OtherType const>(ThisPtr->AsShared());
	}

	FOnlineServicesCommon& Services;

private:
	TWeakPtr<ComponentType> WeakThis;
	FString ConfigName;
};

/* UE::Online */ }
