// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Net/Core/PushModel/PushModelMacros.h"

#if WITH_PUSH_MODEL

#include "CoreMinimal.h"
#include "PushModelPerNetDriverState.h"
#include "PushModelUtils.h"
#include "Net/Core/PushModel/PushModel.h"
#include "Containers/SparseArray.h"

namespace UE4PushModelPrivate
{
	/**
	 * This is a "state" for a given Object that is being tracked by a Push Model Object Manager.
	 * This state is shared across all NetDrivers, and so has a 1:1 mapping with actual UObjects.
	 */
	class FPushModelPerObjectState
	{
	public:

		/**
		 * Creates a new FPushModelPerObjectState.
		 *
		 * @param InObjectId			The ID that we'll use to refer to this object. Should be unique across all Push Model Objects (but doesn't necessarily have to be UObject::GetUniqueId()).
		 * @param InNumberOfProperties	The total number of replicated properties this object has.
		 */
		FPushModelPerObjectState(const FNetPushObjectId InObjectId, const uint16 InNumberOfProperties)
			: ObjectId(InObjectId)
			, DirtiedThisFrame(true, InNumberOfProperties)
		{
		}

		FPushModelPerObjectState(FPushModelPerObjectState&& Other)
			: ObjectId(Other.ObjectId)
			, DirtiedThisFrame(MoveTemp(Other.DirtiedThisFrame))
			, PerNetDriverStates(MoveTemp(Other.PerNetDriverStates))
		{
		}

		FPushModelPerObjectState(const FPushModelPerObjectState& Other) = delete;
		FPushModelPerObjectState& operator=(const FPushModelPerObjectState& Other) = delete;

		void MarkPropertyDirty(const uint16 RepIndex)
		{
			DirtiedThisFrame[RepIndex] = true;
		}
		
		/**
		 * Pushes the current dirty state of the Push Model Object to each of the Net Driver States.
		 * and then reset the dirty state.
		 */
		void PushDirtyStateToNetDrivers()
		{
			if (AreAnyBitsSet(DirtiedThisFrame))
			{
				for (FPushModelPerNetDriverState& NetDriverObject : PerNetDriverStates)
				{
					NetDriverObject.MarkPropertiesDirty(DirtiedThisFrame);
				}

				ResetBitArray(DirtiedThisFrame);
			}
		}

		FPushModelPerNetDriverState& GetPerNetDriverState(FNetPushPerNetDriverId DriverId)
		{
			return PerNetDriverStates[DriverId];
		}
		
		FNetPushPerNetDriverId AddPerNetDriverState()
		{
			FSparseArrayAllocationInfo AllocationInfo = PerNetDriverStates.AddUninitialized();
			new (AllocationInfo.Pointer) FPushModelPerNetDriverState(DirtiedThisFrame.Num());
			
			return AllocationInfo.Index;
		}
		
		void RemovePerNetDriverState(const FNetPushPerNetDriverId DriverId)
		{
			PerNetDriverStates.RemoveAt(DriverId);
		}
		
		const bool HasAnyNetDriverStates()
		{
			return PerNetDriverStates.Num() != 0;
		}
		
		void CountBytes(FArchive& Ar) const
		{
			DirtiedThisFrame.CountBytes(Ar);
			PerNetDriverStates.CountBytes(Ar);
			for (const FPushModelPerNetDriverState& PerNetDriverState : PerNetDriverStates)
			{
				PerNetDriverState.CountBytes(Ar);
			}
		}
		
		const int32 GetNumberOfProperties() const
		{
			return DirtiedThisFrame.Num();
		}

		const FNetPushObjectId GetObjectId() const
		{
			return ObjectId;
		}

	private:
	
		//! A unique ID for the object.
		const FNetPushObjectId ObjectId;

		//! Bitfield tracking which properties we've dirtied since the last time
		//! our state was pushed to NetDrivers.
		//! Note, bits will be allocated for all replicated properties, not just push model properties.
		TBitArray<> DirtiedThisFrame;

		//! Set of NetDriver states that have been requested and are currently tracking the object.
		TSparseArray<FPushModelPerNetDriverState> PerNetDriverStates;
	};
}

#endif