// Copyright Epic Games, Inc. All Rights Reserved.

#include "Net/Core/PushModel/PushModel.h"

#if WITH_PUSH_MODEL

#include "HAL/IConsoleManager.h"
#include "Algo/Sort.h"

#include "Types/PushModelUtils.h"
#include "Types/PushModelPerObjectState.h"
#include "Types/PushModelPerNetDriverState.h"


namespace UE4PushModelPrivate
{
	class FPushModelObjectManager_CustomId
	{
	private:

		using ThisClass = FPushModelObjectManager_CustomId;

	public:

		FPushModelObjectManager_CustomId()
		{
		}

		~FPushModelObjectManager_CustomId()
		{
		}

		void MarkPropertyDirty(const FNetPushObjectId ObjectId, const int32 RepIndex)
		{
			// The macros will take care of filtering out invalid objects, so we don't need to check here.
			PerObjectStates[ObjectId].MarkPropertyDirty(RepIndex);
		}

		void MarkPropertyDirty(const FNetPushObjectId ObjectId, const int32 StartRepIndex, const int32 EndRepIndex)
		{
			FPushModelPerObjectState& ObjectState = PerObjectStates[ObjectId];
			for (int RepIndex = StartRepIndex; RepIndex <= EndRepIndex; ++RepIndex)
			{
				ObjectState.MarkPropertyDirty(RepIndex);
			}
		}

		const FPushModelPerNetDriverHandle AddNetworkObject(const FNetPushObjectId ObjectId, const uint16 NumReplicatedProperties)
		{
			FNetPushObjectId& InternalPushId = ObjectIdToInternalId.FindOrAdd(ObjectId, INDEX_NONE);
			if (INDEX_NONE == InternalPushId)
			{
				FSparseArrayAllocationInfo AllocationInfo = PerObjectStates.AddUninitializedAtLowestFreeIndex(NewObjectLookupPosition);
				new (AllocationInfo.Pointer) FPushModelPerObjectState(ObjectId, NumReplicatedProperties);
				InternalPushId = AllocationInfo.Index;
			}

			FPushModelPerObjectState& PerObjectState = PerObjectStates[InternalPushId];
			check(PerObjectState.GetNumberOfProperties() == NumReplicatedProperties);
			check(PerObjectState.GetObjectId() == ObjectId);

			const FNetPushPerNetDriverId NetDriverId = PerObjectState.AddPerNetDriverState();
			return FPushModelPerNetDriverHandle(NetDriverId, InternalPushId);
		}

		void RemoveNetworkObject(const FPushModelPerNetDriverHandle Handle)
		{
			if (PerObjectStates.IsValidIndex(Handle.ObjectId))
			{
				FPushModelPerObjectState& PerObjectState = PerObjectStates[Handle.ObjectId];
				PerObjectState.RemovePerNetDriverState(Handle.NetDriverId);
				if (!PerObjectState.HasAnyNetDriverStates())
				{
					ObjectIdToInternalId.Remove(PerObjectState.GetObjectId());
					PerObjectStates.RemoveAt(Handle.ObjectId);

					if (NewObjectLookupPosition > Handle.ObjectId)
					{
						NewObjectLookupPosition = Handle.ObjectId;
					}
				}
			}
		}

		void PreReplication()
		{
			for (FPushModelPerObjectState& PerObjectState : PerObjectStates)
			{
				PerObjectState.PushDirtyStateToNetDrivers();
			}
		}

		void PostGarbageCollect()
		{
			// We can't compact PerObjectStates because we need ObjectIDs to be stable.
			// But we can shrink it.

			PerObjectStates.Shrink();
			ObjectIdToInternalId.Compact();
			NewObjectLookupPosition = 0;
		}

		FPushModelPerNetDriverState* GetPerNetDriverState(const FPushModelPerNetDriverHandle Handle)
		{
			if (PerObjectStates.IsValidIndex(Handle.ObjectId))
			{
				FPushModelPerObjectState& ObjectState = PerObjectStates[Handle.ObjectId];
				ObjectState.PushDirtyStateToNetDrivers();
				return &ObjectState.GetPerNetDriverState(Handle.NetDriverId);
			}

			return nullptr;
		}

	private:

		int32 NewObjectLookupPosition = 0;
		TMap<FNetPushObjectId, FNetPushObjectId> ObjectIdToInternalId;
		TSparseArray<FPushModelPerObjectState> PerObjectStates;
	};

	static FPushModelObjectManager_CustomId PushObjectManager;

	bool bIsPushModelEnabled = false;
	FAutoConsoleVariableRef CVarIsPushModelEnabled(
		TEXT("Net.IsPushModelEnabled"),
		bIsPushModelEnabled,
		TEXT("Whether or not Push Model is enabled. This networking mode allows game code to notify the networking system of changes, rather than scraping.")
	);
	
	bool bMakeBpPropertiesPushModel = true;
	FAutoConsoleVariableRef CVarMakeBpPropertiesPushModel(
		TEXT("Net.MakeBpPropertiesPushModel"),
		bMakeBpPropertiesPushModel,
		TEXT("Whether or not Blueprint Properties will be forced to used Push Model")
	);

	void MarkPropertyDirty(const FNetPushObjectId ObjectId, const int32 RepIndex)
	{
		PushObjectManager.MarkPropertyDirty(ObjectId, RepIndex);
	}

	void MarkPropertyDirty(const FNetPushObjectId ObjectId, const int32 StartRepIndex, const int32 EndRepIndex)
	{
		PushObjectManager.MarkPropertyDirty(ObjectId, StartRepIndex, EndRepIndex);
	}

	void PostGarbageCollect()
	{
		PushObjectManager.PostGarbageCollect();
	}

	/**
	 * Called by a given NetDriver to notify us that it's seen a given Object for the first time (or the first time
	 * since it was removed).
	 *
	 * This may be called multiple times for a given Object if there are multiple NetDrivers, but it's expected
	 * that each NetDriver only calls this once per object before RemoteNetworkObject is called.
	 *
	 * @param ObjectId						The UniqueId for the object.
	 * @param NumberOfReplicatedProperties	The number of replicated properties for this object.
	 *
	 * @return A Handle that can be used in other calls to uniquely identify this object per NetDriver.
	 */
	const FPushModelPerNetDriverHandle AddPushModelObject(const FNetPushObjectId ObjectId, const uint16 NumberOfReplicatedProperties)
	{
		return PushObjectManager.AddNetworkObject(ObjectId, NumberOfReplicatedProperties);
	}

	/**
	 * Called by a given NetDriver to notify us that a given Object is no longer valid for Networking.
	 *
	 * This may be called multiple times for a given Object if there are multiple NetDrivers, but it's expected
	 * that each NetDriver only calls this once per object after AddNetworkObject is called, and never before
	 * AddNetworkObject is called.
	 *
	 * @param Handle	The Push Model Object handle (returned by AddPushModelObject).
	 */
	void RemovePushModelObject(const FPushModelPerNetDriverHandle Handle)
	{
		PushObjectManager.RemoveNetworkObject(Handle);
	}

	/**
	 * Gets the NetDriver specific state for a given Push Model Object.
	 * Note, calling this will flush dirty state to all NetDriver states for the Object.
	 *
	 * @param Handle	The Push Model Object handle (returned by AddPushModelObject).
	 */
	FPushModelPerNetDriverState* GetPerNetDriverState(const FPushModelPerNetDriverHandle Handle)
	{
		return PushObjectManager.GetPerNetDriverState(Handle);
	}
}

#endif // WITH_PUSH_MODEL