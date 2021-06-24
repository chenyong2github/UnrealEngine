// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"

namespace UE::Online {

class IOnlineComponent
{
public:
	virtual ~IOnlineComponent() {}
	// Called after component has been constructed. It is not safe to reference other components at this time
	virtual void Initialize() = 0;
	// Called after all components have been initialized
	virtual void PostInitialize() = 0;
	// Called whenever we need to reload data from config
	virtual void LoadConfig() = 0;
	// Called every Tick
	virtual void Tick(float DeltaSeconds) = 0;
	// Called before any component has been shutdown
	virtual void PreShutdown() = 0;
	// Called right before the component is destroyed. It is not safe to reference any other components at this time
	virtual void Shutdown() = 0;
};

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
	template <typename U>
	TOnlineComponent(const FString& ComponentName, const TSharedRef<U>& OwningRef)
		: WeakThis(TSharedPtr<ComponentType>(OwningRef, static_cast<ComponentType*>(this)))
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
		return static_cast<const ComponentType*>(this)->GetServices()->LoadConfig(Struct, GetConfigSectionName(), OperationName);
	}

	const FString& GetConfigSectionName() const { return ConfigSectionName; }

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

private:
	TWeakPtr<ComponentType> WeakThis;
	FString ConfigSectionName;
};

/* UE::Online */ }
