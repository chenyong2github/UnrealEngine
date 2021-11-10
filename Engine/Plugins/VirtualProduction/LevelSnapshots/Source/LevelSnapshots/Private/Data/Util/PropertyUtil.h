// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "UObject/ObjectMacros.h"

class FProperty;
struct FArchiveSerializedPropertyChain;

namespace SnapshotUtil
{
	namespace Property
	{
		enum class EBreakBehaviour
		{
			Continue,
			Break
		};
		using FHandleValuePtr = TFunctionRef<EBreakBehaviour(void* ValuePtr)>;
		using FValuePtrPredicate = TFunctionRef<bool(void* ValuePtr)>;
		
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

		/** Iterates all properties of Struct including properties contained struct sub-properties and collections of structs */
		void ForEachProperty(UStruct* Struct, TUniqueFunction<void(FProperty*)> Callback, EPropertyFlags SkipFlags = CPF_Transient | CPF_Deprecated);
	}
}
