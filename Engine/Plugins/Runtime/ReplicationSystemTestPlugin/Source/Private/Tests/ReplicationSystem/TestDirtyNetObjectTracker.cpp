// Copyright Epic Games, Inc. All Rights Reserved.

#include "NetworkAutomationTest.h"
#include "NetworkAutomationTestMacros.h"
#include "Iris/ReplicationSystem/DirtyNetObjectTracker.h"
#include "Iris/ReplicationSystem/ReplicationSystemInternal.h"
#include "Tests/ReplicationSystem/ReplicationSystemTestFixture.h"

namespace UE::Net::Private
{

class FDirtyNetObjectTrackerTestFixture : public FReplicationSystemTestFixture
{
	using Super = FReplicationSystemTestFixture;

public:
	virtual void SetUp() override
	{
		Super::SetUp();
		DirtyNetObjectTracker = &ReplicationSystem->GetReplicationSystemInternal()->GetDirtyNetObjectTracker();
		ReplicationSystemId = ReplicationSystem->GetId();
		
		FDirtyObjectsAccessor DirtyObjectsAccessor(*DirtyNetObjectTracker);
		NetObjectIndexRangeEnd = DirtyObjectsAccessor.GetDirtyNetObjects().GetNumBits() - 1U;
	}

	virtual void TearDown() override
	{
		DirtyNetObjectTracker = nullptr;
		ReplicationSystemId = ~0U;
		Super::TearDown();
	}

protected:
	static constexpr uint32 NetObjectIndexRangeStart = 1;

	FDirtyNetObjectTracker* DirtyNetObjectTracker = nullptr;
	uint32 ReplicationSystemId = ~0U;
	uint32 NetObjectIndexRangeEnd = 1;
};

UE_NET_TEST_FIXTURE(FDirtyNetObjectTrackerTestFixture, TestNoObjectIsDirtyFromStart)
{
	FDirtyObjectsAccessor DirtyObjectsAccessor(*DirtyNetObjectTracker);

	const FNetBitArrayView DirtyObjects = DirtyObjectsAccessor.GetDirtyNetObjects();
	UE_NET_ASSERT_EQ(DirtyObjects.GetNumBits(), NetObjectIndexRangeEnd + 1U);

	UE_NET_ASSERT_FALSE_MSG(DirtyObjects.IsAnyBitSet(), "Objects are marked as dirty before marking any object state as dirty");
}

UE_NET_TEST_FIXTURE(FDirtyNetObjectTrackerTestFixture, CannotMarkInvalidObjectAsDirty)
{
	MarkNetObjectStateDirty(ReplicationSystemId, FNetRefHandle().GetId());
	MarkNetObjectStateDirty(ReplicationSystemId, NetObjectIndexRangeEnd + 1);

	FDirtyObjectsAccessor DirtyObjectsAccessor(*DirtyNetObjectTracker);
	const FNetBitArrayView DirtyObjects = DirtyObjectsAccessor.GetDirtyNetObjects();
	UE_NET_ASSERT_FALSE(DirtyObjects.IsAnyBitSet());
}

UE_NET_TEST_FIXTURE(FDirtyNetObjectTrackerTestFixture, CanMarkValidObjectAsDirty)
{
	const uint32 IndexInRange = NetObjectIndexRangeStart + (NetObjectIndexRangeEnd - NetObjectIndexRangeStart)/2;
	MarkNetObjectStateDirty(ReplicationSystemId, IndexInRange);
	MarkNetObjectStateDirty(ReplicationSystemId, NetObjectIndexRangeStart);
	MarkNetObjectStateDirty(ReplicationSystemId, NetObjectIndexRangeEnd);

	FDirtyObjectsAccessor DirtyObjectsAccessor(*DirtyNetObjectTracker);
	const FNetBitArrayView DirtyObjects = DirtyObjectsAccessor.GetDirtyNetObjects();
	UE_NET_ASSERT_TRUE(DirtyObjects.GetBit(IndexInRange));
	UE_NET_ASSERT_TRUE(DirtyObjects.GetBit(NetObjectIndexRangeStart));
	UE_NET_ASSERT_TRUE(DirtyObjects.GetBit(NetObjectIndexRangeEnd));
}

UE_NET_TEST_FIXTURE(FDirtyNetObjectTrackerTestFixture, CanClearDirtyObjects)
{
	MarkNetObjectStateDirty(ReplicationSystemId, NetObjectIndexRangeStart);
	MarkNetObjectStateDirty(ReplicationSystemId, NetObjectIndexRangeEnd);

	FNetBitArray CleanedObjects;
	CleanedObjects.Init(NetObjectIndexRangeEnd+1);

	CleanedObjects.SetBit(NetObjectIndexRangeStart);
	CleanedObjects.SetBit(NetObjectIndexRangeEnd);

	DirtyNetObjectTracker->UpdateAccumulatedDirtyList();
	DirtyNetObjectTracker->ClearDirtyNetObjects(MakeNetBitArrayView(CleanedObjects));

	const FNetBitArrayView AccumulatedDirtyObjects = DirtyNetObjectTracker->GetAccumulatedDirtyNetObjects();
	UE_NET_ASSERT_FALSE(AccumulatedDirtyObjects.IsAnyBitSet());
}

UE_NET_TEST_FIXTURE(FDirtyNetObjectTrackerTestFixture, DelayedDirtyBitTracking)
{

	const uint32 FirstObjectIndex = NetObjectIndexRangeStart;
	const uint32 SecondObjectIndex = NetObjectIndexRangeStart + 1;
	MarkNetObjectStateDirty(ReplicationSystemId, FirstObjectIndex);
	MarkNetObjectStateDirty(ReplicationSystemId, SecondObjectIndex);

	// Clean first object
	{
		FNetBitArray CleanedObjects;
		CleanedObjects.Init(NetObjectIndexRangeEnd + 1);
		CleanedObjects.SetBit(FirstObjectIndex);

		DirtyNetObjectTracker->UpdateAccumulatedDirtyList();
		DirtyNetObjectTracker->ClearDirtyNetObjects(MakeNetBitArrayView(CleanedObjects));
	}

	const FNetBitArrayView AccumulatedDirtyObjects = DirtyNetObjectTracker->GetAccumulatedDirtyNetObjects();

	UE_NET_ASSERT_FALSE(AccumulatedDirtyObjects.GetBit(FirstObjectIndex));
	UE_NET_ASSERT_TRUE(AccumulatedDirtyObjects.GetBit(SecondObjectIndex));

	// Clean second object
	{
		FNetBitArray CleanedObjects;
		CleanedObjects.Init(NetObjectIndexRangeEnd + 1);
		CleanedObjects.SetBit(SecondObjectIndex);

		DirtyNetObjectTracker->UpdateAccumulatedDirtyList();
		DirtyNetObjectTracker->ClearDirtyNetObjects(MakeNetBitArrayView(CleanedObjects));
	}

	UE_NET_ASSERT_FALSE(AccumulatedDirtyObjects.GetBit(FirstObjectIndex));
	UE_NET_ASSERT_FALSE(AccumulatedDirtyObjects.GetBit(SecondObjectIndex));
}

UE_NET_TEST(DirtyNetObjectTracker, MarkingObjectAsDirtyInNonExistingSystemDoesNotCrash)
{
	constexpr uint32 NonExistingReplicationSystemId = 4711;
	constexpr uint32 ArbitraryNetObjectIndex = 1174;
	MarkNetObjectStateDirty(NonExistingReplicationSystemId, ArbitraryNetObjectIndex);
}

}
