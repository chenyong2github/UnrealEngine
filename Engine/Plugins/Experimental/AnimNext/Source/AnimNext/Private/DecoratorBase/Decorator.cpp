// Copyright Epic Games, Inc. All Rights Reserved.

#include "DecoratorBase/Decorator.h"

#include "DecoratorBase/DecoratorRegistry.h"

namespace UE::AnimNext
{
	FDecoratorStaticInitHook::FDecoratorStaticInitHook(DecoratorConstructorFunc DecoratorConstructor_)
		: DecoratorConstructor(DecoratorConstructor_)
	{
		FDecoratorRegistry::StaticRegister(DecoratorConstructor_);
	}

	FDecoratorStaticInitHook::~FDecoratorStaticInitHook()
	{
		FDecoratorRegistry::StaticUnregister(DecoratorConstructor);
	}
}
