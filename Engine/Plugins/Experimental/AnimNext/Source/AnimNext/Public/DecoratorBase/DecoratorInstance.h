// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DecoratorBase/DecoratorPtr.h"

namespace UE::AnimNext
{
	struct FDecoratorDescription;
	struct FExecutionContext;

	/**
	 * Decorator Instance
	 * A decorator instance represents allocated data for specific decorator.
	 * @see FNodeInstance
	 * 
	 * A decorator instance is the base type that decorator instance data derives from.
	 */
	struct FDecoratorInstance
	{
		// Called after the constructor has been called when a new instance is created.
		// This is called after the default constructor.
		// You can override this function by adding a new one with the same name on your
		// derived type. You can also specialize the FDecoratorDescription with the derived
		// version as long as the reference can coerce to 'const FDecoratorDescription&'.
		// Decorators are constructed from the bottom to the top.
		void Construct(FExecutionContext& Context, FWeakDecoratorPtr DecoratorPtr, const FDecoratorDescription& DecoratorDesc)
		{
		}

		// Called before the destructor has been called when an instance is destroyed.
		// This is called before the default destructor.
		// You can override this function by adding a new one with the same name on your
		// derived type. You can also specialize the FDecoratorDescription with the derived
		// version as long as the reference can coerce to 'const FDecoratorDescription&'.
		// Decorators are destructed from the top to the bottom.
		void Destruct(FExecutionContext& Context, FWeakDecoratorPtr DecoratorPtr, const FDecoratorDescription& DecoratorDesc)
		{
		}
	};
}
