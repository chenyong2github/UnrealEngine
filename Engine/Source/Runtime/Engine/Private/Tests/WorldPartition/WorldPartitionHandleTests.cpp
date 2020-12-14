// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "WorldPartition/WorldPartitionHandle.h"
#include "WorldPartition/WorldPartitionActorDesc.h"

#if WITH_DEV_AUTOMATION_TESTS

#define TEST_NAME_ROOT "System.Engine.WorldPartition"

namespace WorldPartitionTests
{
	constexpr const uint32 TestFlags = EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter;

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldPartitionHandleTest, TEST_NAME_ROOT ".Handle", TestFlags)
	bool FWorldPartitionHandleTest::RunTest(const FString& Parameters)
	{
#if WITH_EDITOR
		TUniquePtr<FWorldPartitionActorDesc> ActorDescPtr1(new FWorldPartitionActorDesc());
		TUniquePtr<FWorldPartitionActorDesc> ActorDescPtr2(new FWorldPartitionActorDesc());

		FWorldPartitionHandle Handle(&ActorDescPtr1);
		FWorldPartitionReference Reference(&ActorDescPtr2);

		TestTrue(TEXT("Handle/Reference equality"), Handle != Reference);
		TestTrue(TEXT("Reference/Handle equality"), Reference != Handle);
		
		TestTrue(TEXT("Handle soft refcount"), Handle->GetSoftRefCount() == 1);
		TestTrue(TEXT("Handle hard refcount"), Handle->GetHardRefCount() == 0);
		TestTrue(TEXT("Reference soft refcount"), Reference->GetSoftRefCount() == 0);
		TestTrue(TEXT("Reference hard refcount"), Reference->GetHardRefCount() == 1);

		// Conversions
		{
			FWorldPartitionHandle HandleToReference = Reference;
			TestTrue(TEXT("Handle/Reference equality"), HandleToReference == Reference);
			TestTrue(TEXT("Reference/Handle equality"), Reference == HandleToReference);
			TestTrue(TEXT("Reference soft refcount"), Reference->GetSoftRefCount() == 1);
			TestTrue(TEXT("Reference hard refcount"), Reference->GetHardRefCount() == 1);

			FWorldPartitionReference ReferenceToHandle = Handle;
			TestTrue(TEXT("Handle/Reference equality"), ReferenceToHandle == Handle);
			TestTrue(TEXT("Handle/Reference equality"), Handle == ReferenceToHandle);
			TestTrue(TEXT("Reference soft refcount"), Reference->GetSoftRefCount() == 1);
			TestTrue(TEXT("Reference hard refcount"), Reference->GetHardRefCount() == 1);
		}

		TestTrue(TEXT("Handle soft refcount"), Handle->GetSoftRefCount() == 1);
		TestTrue(TEXT("Handle hard refcount"), Handle->GetHardRefCount() == 0);
		TestTrue(TEXT("Reference soft refcount"), Reference->GetSoftRefCount() == 0);
		TestTrue(TEXT("Reference hard refcount"), Reference->GetHardRefCount() == 1);

		Handle = Handle;
		TestTrue(TEXT("Handle soft refcount"), Handle->GetSoftRefCount() == 1);
		TestTrue(TEXT("Handle hard refcount"), Handle->GetHardRefCount() == 0);

		Reference = Reference;
		TestTrue(TEXT("Reference soft refcount"), Reference->GetSoftRefCount() == 0);
		TestTrue(TEXT("Reference hard refcount"), Reference->GetHardRefCount() == 1);

		// inplace new test
		{
			uint8 Buffer[sizeof(FWorldPartitionHandle)];
			FWorldPartitionHandle* HandlePtr = new (Buffer) FWorldPartitionHandle(Reference);

			TestTrue(TEXT("Handle array soft refcount"), Reference->GetSoftRefCount() == 1);
			TestTrue(TEXT("Handle array hard refcount"), Reference->GetHardRefCount() == 1);

			HandlePtr->~FWorldPartitionHandle();

			TestTrue(TEXT("Handle array soft refcount"), Reference->GetSoftRefCount() == 0);
			TestTrue(TEXT("Handle array hard refcount"), Reference->GetHardRefCount() == 1);
		}

		// TArray test
		{
			TArray<FWorldPartitionHandle> HandleList;
			HandleList.Add(Handle);

			TestTrue(TEXT("Handle array soft refcount"), Handle->GetSoftRefCount() == 2);
			TestTrue(TEXT("Handle array hard refcount"), Handle->GetHardRefCount() == 0);

			FWorldPartitionReference ReferenceToHandle = Handle;
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
			TSet<FWorldPartitionHandle> HandleSet;
			HandleSet.Add(Handle);

			TestTrue(TEXT("Handle set soft refcount"), Handle->GetSoftRefCount() == 2);
			TestTrue(TEXT("Handle set hard refcount"), Handle->GetHardRefCount() == 0);

			FWorldPartitionReference ReferenceToHandle = Handle;
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
#endif
		return true;
	}
}

#undef TEST_NAME_ROOT

#endif 
