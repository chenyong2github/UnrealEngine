// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "WorldPartition/WorldPartitionHandle.h"
#include "WorldPartition/WorldPartitionActorDesc.h"

#if WITH_DEV_AUTOMATION_TESTS

#define TEST_NAME_ROOT "System.Engine.WorldPartition"

namespace WorldPartitionTests
{
	constexpr const uint32 TestFlags = EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter;

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldPartitionSoftRefTest, TEST_NAME_ROOT ".Handle", TestFlags)
	bool FWorldPartitionSoftRefTest::RunTest(const FString& Parameters)
	{
#if WITH_EDITOR
		TestTrue(TEXT("SoftRef sizeof"), sizeof(FWorldPartitionSoftRef) == sizeof(void*));
		TestTrue(TEXT("HardRef sizeof"), sizeof(FWorldPartitionHardRef) == sizeof(void*));

		TUniquePtr<FWorldPartitionActorDesc> ActorDescPtr1(new FWorldPartitionActorDesc());
		TUniquePtr<FWorldPartitionActorDesc> ActorDescPtr2(new FWorldPartitionActorDesc());

		FWorldPartitionSoftRef Handle(&ActorDescPtr1);
		FWorldPartitionHardRef Reference(&ActorDescPtr2);

		// Auto reference tests
		{
			TestTrue(TEXT("Handle soft refcount"), Handle->GetSoftRefCount() == 1);
			TestTrue(TEXT("Handle hard refcount"), Handle->GetHardRefCount() == 0);
			TestTrue(TEXT("Reference soft refcount"), Reference->GetSoftRefCount() == 0);
			TestTrue(TEXT("Reference hard refcount"), Reference->GetHardRefCount() == 1);

			FWorldPartitionPinRef AutoRefToHandle(Handle);
			FWorldPartitionPinRef AutoRefToReference(Reference);

			TestTrue(TEXT("Pin to Handle soft refcount"), Handle->GetSoftRefCount() == 2);
			TestTrue(TEXT("Pin to Handle hard refcount"), Handle->GetHardRefCount() == 0);
			TestTrue(TEXT("Pin to Reference soft refcount"), Reference->GetSoftRefCount() == 0);
			TestTrue(TEXT("Pin to Reference hard refcount"), Reference->GetHardRefCount() == 2);
		}

		TestTrue(TEXT("Handle/Reference equality"), Handle != Reference);
		TestTrue(TEXT("Reference/Handle equality"), Reference != Handle);
		
		TestTrue(TEXT("Handle soft refcount"), Handle->GetSoftRefCount() == 1);
		TestTrue(TEXT("Handle hard refcount"), Handle->GetHardRefCount() == 0);
		TestTrue(TEXT("Reference soft refcount"), Reference->GetSoftRefCount() == 0);
		TestTrue(TEXT("Reference hard refcount"), Reference->GetHardRefCount() == 1);

		// Conversions
		{
			FWorldPartitionSoftRef HandleToReference = Reference;
			TestTrue(TEXT("Handle/Reference equality"), HandleToReference == Reference);
			TestTrue(TEXT("Reference/Handle equality"), Reference == HandleToReference);
			TestTrue(TEXT("Reference soft refcount"), Reference->GetSoftRefCount() == 1);
			TestTrue(TEXT("Reference hard refcount"), Reference->GetHardRefCount() == 1);

			FWorldPartitionHardRef ReferenceToHandle = Handle;
			TestTrue(TEXT("Handle/Reference equality"), ReferenceToHandle == Handle);
			TestTrue(TEXT("Handle/Reference equality"), Handle == ReferenceToHandle);
			TestTrue(TEXT("Reference soft refcount"), Reference->GetSoftRefCount() == 1);
			TestTrue(TEXT("Reference hard refcount"), Reference->GetHardRefCount() == 1);
		}

		TestTrue(TEXT("Handle soft refcount"), Handle->GetSoftRefCount() == 1);
		TestTrue(TEXT("Handle hard refcount"), Handle->GetHardRefCount() == 0);
		TestTrue(TEXT("Reference soft refcount"), Reference->GetSoftRefCount() == 0);
		TestTrue(TEXT("Reference hard refcount"), Reference->GetHardRefCount() == 1);

		// inplace new test
		{
			uint8 Buffer[sizeof(FWorldPartitionSoftRef)];
			FWorldPartitionSoftRef* HandlePtr = new (Buffer) FWorldPartitionSoftRef(Reference);

			TestTrue(TEXT("Handle array soft refcount"), Reference->GetSoftRefCount() == 1);
			TestTrue(TEXT("Handle array hard refcount"), Reference->GetHardRefCount() == 1);

			HandlePtr->~FWorldPartitionSoftRef();

			TestTrue(TEXT("Handle array soft refcount"), Reference->GetSoftRefCount() == 0);
			TestTrue(TEXT("Handle array hard refcount"), Reference->GetHardRefCount() == 1);
		}

		// TArray test
		{
			TArray<FWorldPartitionSoftRef> HandleList;
			HandleList.Add(Handle);

			TestTrue(TEXT("Handle array soft refcount"), Handle->GetSoftRefCount() == 2);
			TestTrue(TEXT("Handle array hard refcount"), Handle->GetHardRefCount() == 0);

			FWorldPartitionHardRef ReferenceToHandle = Handle;
			TestTrue(TEXT("Handle/Reference equality"), ReferenceToHandle == Handle);
			TestTrue(TEXT("Handle/Reference equality"), Handle == ReferenceToHandle);
			TestTrue(TEXT("Handle soft refcount"), Handle->GetSoftRefCount() == 2);
			TestTrue(TEXT("Handle hard refcount"), Handle->GetHardRefCount() == 1);

			TestTrue(TEXT("Handle array contains handle"), HandleList.Contains(Handle));
			TestTrue(TEXT("Handle array contains reference"), HandleList.Contains(ReferenceToHandle));

			HandleList.Add(Reference);
			
			TestTrue(TEXT("Handle array contains reference"), HandleList.Contains(Reference));
			TestTrue(TEXT("Handle array soft refcount"), Reference->GetSoftRefCount() == 1);
			TestTrue(TEXT("Handle array hard refcount"), Reference->GetHardRefCount() == 1);
			
			HandleList.Remove(Handle);
			TestTrue(TEXT("Handle array soft refcount"), Handle->GetSoftRefCount() == 1);
			TestTrue(TEXT("Handle array hard refcount"), Handle->GetHardRefCount() == 1);

			HandleList.Remove(Reference);
			TestTrue(TEXT("Handle array soft refcount"), Reference->GetSoftRefCount() == 0);
			TestTrue(TEXT("Handle array hard refcount"), Reference->GetHardRefCount() == 1);
		}

		TestTrue(TEXT("Handle soft refcount"), Handle->GetSoftRefCount() == 1);
		TestTrue(TEXT("Handle hard refcount"), Handle->GetHardRefCount() == 0);
		TestTrue(TEXT("Reference soft refcount"), Reference->GetSoftRefCount() == 0);
		TestTrue(TEXT("Reference hard refcount"), Reference->GetHardRefCount() == 1);

		// TSet test
		{
			TSet<FWorldPartitionSoftRef> HandleSet;
			HandleSet.Add(Handle);

			TestTrue(TEXT("Handle set soft refcount"), Handle->GetSoftRefCount() == 2);
			TestTrue(TEXT("Handle set hard refcount"), Handle->GetHardRefCount() == 0);

			FWorldPartitionHardRef ReferenceToHandle = Handle;
			TestTrue(TEXT("Handle/Reference equality"), ReferenceToHandle == Handle);
			TestTrue(TEXT("Handle/Reference equality"), Handle == ReferenceToHandle);
			TestTrue(TEXT("Handle soft refcount"), Handle->GetSoftRefCount() == 2);
			TestTrue(TEXT("Handle hard refcount"), Handle->GetHardRefCount() == 1);

			TestTrue(TEXT("Handle set contains handle"), HandleSet.Contains(Handle));
			TestTrue(TEXT("Handle set contains reference"), HandleSet.Contains(ReferenceToHandle));

			HandleSet.Add(Reference);
			TestTrue(TEXT("Handle set contains reference"), HandleSet.Contains(Reference));

			TestTrue(TEXT("Reference soft refcount"), Reference->GetSoftRefCount() == 1);
			TestTrue(TEXT("Reference hard refcount"), Reference->GetHardRefCount() == 1);
		}

		TestTrue(TEXT("Handle soft refcount"), Handle->GetSoftRefCount() == 1);
		TestTrue(TEXT("Handle hard refcount"), Handle->GetHardRefCount() == 0);
		TestTrue(TEXT("Reference soft refcount"), Reference->GetSoftRefCount() == 0);
		TestTrue(TEXT("Reference hard refcount"), Reference->GetHardRefCount() == 1);

		// Move tests
		{
			// Handle move
			TestTrue(TEXT("Handle soft refcount"), Handle->GetSoftRefCount() == 1);
			TestTrue(TEXT("Handle hard refcount"), Handle->GetHardRefCount() == 0);
			{
				FWorldPartitionSoftRef HandleCopy(MoveTemp(Handle));
				TestTrue(TEXT("Handle move src not valid"), !Handle.IsValid());
				TestTrue(TEXT("Handle move dst valid"), HandleCopy.IsValid());
				TestTrue(TEXT("Handle soft refcount"), HandleCopy->GetSoftRefCount() == 1);
				TestTrue(TEXT("Handle hard refcount"), HandleCopy->GetHardRefCount() == 0);

				Handle = MoveTemp(HandleCopy);
				TestTrue(TEXT("Handle move src not valid"), !HandleCopy.IsValid());
				TestTrue(TEXT("Handle move dst valid"), Handle.IsValid());
				TestTrue(TEXT("Handle soft refcount"), Handle->GetSoftRefCount() == 1);
				TestTrue(TEXT("Handle hard refcount"), Handle->GetHardRefCount() == 0);
			}

			// Reference move
			TestTrue(TEXT("Reference soft refcount"), Reference->GetSoftRefCount() == 0);
			TestTrue(TEXT("Reference hard refcount"), Reference->GetHardRefCount() == 1);
			{
				FWorldPartitionHardRef ReferenceCopy(MoveTemp(Reference));
				TestTrue(TEXT("Reference move src not valid"), !Reference.IsValid());
				TestTrue(TEXT("Reference move dst valid"), ReferenceCopy.IsValid());
				TestTrue(TEXT("Reference soft refcount"), ReferenceCopy->GetSoftRefCount() == 0);
				TestTrue(TEXT("Reference hard refcount"), ReferenceCopy->GetHardRefCount() == 1);

				Reference = MoveTemp(ReferenceCopy);
				TestTrue(TEXT("Reference move src not valid"), !ReferenceCopy.IsValid());
				TestTrue(TEXT("Reference move dst valid"), Reference.IsValid());
				TestTrue(TEXT("Reference soft refcount"), Reference->GetSoftRefCount() == 0);
				TestTrue(TEXT("Reference hard refcount"), Reference->GetHardRefCount() == 1);
			}

			// Handle reference move
			{
				FWorldPartitionSoftRef HandleFromReference(MoveTemp(Reference));
				TestTrue(TEXT("Handle move src not valid"), !Reference.IsValid());
				TestTrue(TEXT("Handle move dst valid"), HandleFromReference.IsValid());
				TestTrue(TEXT("Handle soft refcount"), HandleFromReference->GetSoftRefCount() == 1);
				TestTrue(TEXT("Handle hard refcount"), HandleFromReference->GetHardRefCount() == 0);

				Reference = MoveTemp(HandleFromReference);
				TestTrue(TEXT("Handle move src not valid"), !HandleFromReference.IsValid());
				TestTrue(TEXT("Handle move dst valid"), Reference.IsValid());
				TestTrue(TEXT("Handle soft refcount"), Reference->GetSoftRefCount() == 0);
				TestTrue(TEXT("Handle hard refcount"), Reference->GetHardRefCount() == 1);
			}

			// Reference handle move
			{
				FWorldPartitionHardRef ReferenceFromHandle(MoveTemp(Handle));
				TestTrue(TEXT("Reference move src not valid"), !Handle.IsValid());
				TestTrue(TEXT("Reference move dst valid"), ReferenceFromHandle.IsValid());
				TestTrue(TEXT("Reference soft refcount"), ReferenceFromHandle->GetSoftRefCount() == 0);
				TestTrue(TEXT("Reference hard refcount"), ReferenceFromHandle->GetHardRefCount() == 1);

				Handle = MoveTemp(ReferenceFromHandle);
				TestTrue(TEXT("Reference move src not valid"), !ReferenceFromHandle.IsValid());
				TestTrue(TEXT("Reference move dst valid"), Handle.IsValid());
				TestTrue(TEXT("Reference soft refcount"), Handle->GetSoftRefCount() == 1);
				TestTrue(TEXT("Reference hard refcount"), Handle->GetHardRefCount() == 0);
			}
		}
#endif
		return true;
	}
}

#undef TEST_NAME_ROOT

#endif 
