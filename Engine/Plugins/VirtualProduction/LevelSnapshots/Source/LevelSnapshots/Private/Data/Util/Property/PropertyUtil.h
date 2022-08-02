// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "UObject/ObjectMacros.h"

class FFieldClass;
class FProperty;
struct FArchiveSerializedPropertyChain;

namespace UE::LevelSnapshots::Private
{
	enum class EBreakBehaviour
	{
		Continue,
		Break
	};
	enum class EPropertyType
	{
		/** Any other non-map property */
		NormalProperty,
		/** This is the key of a map property */
		KeyInMap,
		/** This is the value of a map property */
		ValueInMap
	};
	using FHandleValuePtr = TFunctionRef<EBreakBehaviour(void* ValuePtr, EPropertyType PropertyType)>;
	using FValuePtrPredicate = TFunctionRef<bool(void* ValuePtr, EPropertyType PropertyType)>;
	
	/**
	 * Follow a property chain to its leaf property and call a function with the value pointers.
	 * 
	 * Keep in mind that there may be 0 or more calls to the callback:
	 *	- If the property path contains a collection, there may be 0 or more calls depending on the number of contained elements
	 *	- If there is no collection, there should be exactly 1 call.
	 */
	void FollowPropertyChain(void* ContainerPtr, const FArchiveSerializedPropertyChain* PropertyChain, const FProperty* LeafProperty, FHandleValuePtr Callback);

	/**
	 * Runs a predicate function on the found value pointer until one returns true.
	 */
	bool FollowPropertyChainUntilPredicateIsTrue(void* ContainerPtr, const FArchiveSerializedPropertyChain* PropertyChain, const FProperty* LeafProperty, FValuePtrPredicate Callback);
}

