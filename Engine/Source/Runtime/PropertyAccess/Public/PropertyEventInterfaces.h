// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "PropertyEventInterfaces.generated.h"

namespace PropertyAccess
{
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnPropertyChanged, UObject* /*InObject*/, int32 /*InBroadcastId*/);
}

UINTERFACE(MinimalAPI)
class UPropertyEventBroadcaster : public UInterface
{
	GENERATED_BODY()
};

class IPropertyEventSubscriber;

/** Interface used to broadcast property changed events. Inherit from this and call BROADCAST_PROPERTY_CHANGED */
class IPropertyEventBroadcaster
{
	GENERATED_BODY()
public:

	/** Broadcast a property changing */
	virtual void BroadcastPropertyChanged(int32 InBroadcastId) const = 0;

	/** Register a subscriber to listen for property changed events */
	virtual void RegisterSubscriber(IPropertyEventSubscriber* InSubscriber, int32 InMappingId) = 0;

	/** Bind a delegate to the property changed event */
	virtual void UnregisterSubscriber(IPropertyEventSubscriber* InSubscriber) = 0;
};

UINTERFACE(MinimalAPI)
class UPropertyEventSubscriber : public UInterface
{
	GENERATED_BODY()
};

class IPropertyEventSubscriber
{
	GENERATED_BODY()
public:

	/** Handle a property changing */
	virtual void OnPropertyChanged(IPropertyEventBroadcaster* InObject, int32 InBroadcastId) const = 0;
};
