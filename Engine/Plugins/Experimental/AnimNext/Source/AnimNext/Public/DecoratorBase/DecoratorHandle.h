// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DecoratorBase/NodeHandle.h"

namespace UE::AnimNext
{
	/**
	 * Decorator Handle
	 * A decorator handle represents a reference to a specific decorator instance in the shared/read-only portion
	 * of a sub-graph. It points to a FNodeDescription when resolved.
	 * @see FNodeDescription
	 */
	struct FDecoratorHandle
	{
		// Creates an invalid decorator handle
		FDecoratorHandle()
			: DecoratorIndex(0)
			, SharedOffset(INVALID_SHARED_OFFSET_VALUE)
		{}

		// Creates a decorator handle pointing to the first decorator of the specified node
		explicit FDecoratorHandle(FNodeHandle NodeHandle)
			: DecoratorIndex(0)
			, SharedOffset(NodeHandle.GetSharedOffset())
		{}

		// Creates a decorator handle pointing to the specified decorator on the specified node
		FDecoratorHandle(FNodeHandle NodeHandle, uint32 DecoratorIndex_)
			: DecoratorIndex(DecoratorIndex_)
			, SharedOffset(NodeHandle.GetSharedOffset())
		{}

		// Returns true if this decorator handle is valid, false otherwise
		bool IsValid() const { return SharedOffset != INVALID_SHARED_OFFSET_VALUE; }

		// Returns the decorator index
		uint32 GetDecoratorIndex() const { return DecoratorIndex; }

		// Returns a handle to the node in the shared data segment
		FNodeHandle GetNodeHandle() const { return FNodeHandle(SharedOffset); }

	private:
		// We pack the shared offset on 24 bits, the top 8 bits are truncated
		static constexpr uint32 INVALID_SHARED_OFFSET_VALUE = ~0u >> (32 - 24);

		uint32	DecoratorIndex : 8;
		uint32	SharedOffset : 24;		// relative to root of sub-graph
	};
}
