// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Templates/SharedPointer.h"

class UQuartzSubsystem;
struct FQuartzTickableObjectsManager;

class FQuartzSubscriptionToken
{
public:
	FQuartzSubscriptionToken();
	~FQuartzSubscriptionToken();
	
	void Subscribe(FQuartzTickableObject* Subscriber, UQuartzSubsystem* QuartzSubsystem);
	void Unsubscribe();

	TSharedPtr<FQuartzTickableObjectsManager> GetTickableObjectManager();
	bool IsSubscribed() const;
	
private:
	FQuartzTickableObject* SubscribingObject;
	TWeakPtr<FQuartzTickableObjectsManager> TickableObjectManagerPtr;
};
