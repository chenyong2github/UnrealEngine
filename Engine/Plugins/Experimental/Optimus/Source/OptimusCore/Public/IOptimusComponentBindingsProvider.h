// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Interface.h"

#include "IOptimusComponentBindingsProvider.generated.h"


class AActor;
class UActorComponent;
class UOptimusComponentSourceBinding;


UINTERFACE()
class OPTIMUSCORE_API UOptimusComponentBindingsProvider :
	public UInterface
{
	GENERATED_BODY()
};


class IOptimusComponentBindingsProvider
{
	GENERATED_BODY()

public:
	/** Returns the list of components already bound */
	virtual TArray<UActorComponent*> GetBoundComponents() const = 0;

	/** Returns the actor that we're applied to */
	virtual AActor* GetActor() const = 0;

	/** Returns the deformer that the provider is tied to */
	virtual UOptimusComponentSourceBinding* GetComponentBindingByName(FName InBindingName) const = 0;
};
