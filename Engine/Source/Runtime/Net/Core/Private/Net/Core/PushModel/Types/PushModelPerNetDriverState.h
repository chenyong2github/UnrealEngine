// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Net/Core/PushModel/PushModelMacros.h"

#if WITH_PUSH_MODEL

#include "CoreMinimal.h"
#include "Containers/BitArray.h"
#include "PushModelUtils.h"

namespace UE4PushModelPrivate
{
	class FPushModelPerNetDriverState
	{
	public:

		FPushModelPerNetDriverState(const uint16 InNumberOfProperties)
			: PropertyDirtyStates(true, InNumberOfProperties)
		{
		}
		
		FPushModelPerNetDriverState(FPushModelPerNetDriverState&& Other)
			: PropertyDirtyStates(MoveTemp(Other.PropertyDirtyStates))
		{
		}
		
		FPushModelPerNetDriverState(const FPushModelPerNetDriverState& Other) = delete;
		FPushModelPerNetDriverState& operator=(const FPushModelPerNetDriverState& Other) = delete;

		void ResetDirtyStates()
		{
			ResetBitArray(PropertyDirtyStates);
		}

		void CountBytes(FArchive& Ar) const
		{
			PropertyDirtyStates.CountBytes(Ar);
		}

		const bool IsPropertyDirty(const uint16 RepIndex)
		{
			return PropertyDirtyStates[RepIndex];
		}

		TConstSetBitIterator<> GetDirtyProperties() const
		{
			return TConstSetBitIterator<>(PropertyDirtyStates);
		}

		void MarkPropertiesDirty(const TBitArray<>& OtherBitArray)
		{
			BitwiseOrBitArrays(OtherBitArray, PropertyDirtyStates);
		}

		void MarkPropertyDirty(const uint16 RepIndex)
		{
			PropertyDirtyStates[RepIndex] = true;
		}

	private:

		/**
		 * Current state of our push model properties.
		 * Note, bits will be allocated for all replicated properties, not just push model properties.
		 */
		TBitArray<> PropertyDirtyStates;
	};
}

#endif