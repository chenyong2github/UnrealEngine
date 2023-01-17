// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_LOW_LEVEL_TESTS

#include "TestHarness.h"

#include "CoreMinimal.h"
#include "UObject/ObjectHandle.h"
#include "Misc/ScopeLock.h"

#if UE_WITH_OBJECT_HANDLE_LATE_RESOLVE
inline FObjectHandle MakeUnresolvedHandle(const UObject* Obj)
{
	UE::CoreUObject::Private::FPackedObjectRef PackedObjectRef = UE::CoreUObject::Private::MakePackedObjectRef(Obj);
	return { PackedObjectRef.EncodedRef };
}

#endif

class FObjectRefTrackingTestBase
{
public:
	uint32 GetNumResolves() const { return NumResolves; }
	uint32 GetNumFailedResolves() const { return NumFailedResolves; }
	uint32 GetNumReads() const { return NumReads; }

	struct FSnapshotObjectRefMetrics
	{
	public:
		FSnapshotObjectRefMetrics(FObjectRefTrackingTestBase& InTest)
		: Test(InTest)
		, OriginalNumResolves(Test.GetNumResolves())
		, OriginalNumFailedResolves(Test.GetNumFailedResolves())
		, OriginalNumReads(Test.GetNumReads())
		{
			Test.ConditionalInstallCallbacks();
		}

		void TestNumResolves(const TCHAR* What, uint32 ExpectedDelta)
		{
#if UE_WITH_OBJECT_HANDLE_TRACKING
			TEST_EQUAL(What, OriginalNumResolves + ExpectedDelta, Test.GetNumResolves());
#endif
		}

		void TestNumFailedResolves(const TCHAR* What, uint32 ExpectedDelta)
		{
#if UE_WITH_OBJECT_HANDLE_TRACKING
			TEST_EQUAL(What, OriginalNumFailedResolves + ExpectedDelta, Test.GetNumFailedResolves());
#endif
		}

		void TestNumReads(const TCHAR* What, uint32 ExpectedDelta, bool bAllowAdditionalReads = false)
		{
#if UE_WITH_OBJECT_HANDLE_TRACKING
			bool bValue = false;
			if (bAllowAdditionalReads)
			{
				INFO(What);
				CHECK(Test.GetNumReads() >= OriginalNumReads + ExpectedDelta);
			}
			else
			{
				bValue = OriginalNumReads + ExpectedDelta == Test.GetNumReads();
				TEST_TRUE(What, bValue);
			}
#endif
		}
	private:
		FObjectRefTrackingTestBase& Test;
		uint32 OriginalNumResolves;
		uint32 OriginalNumFailedResolves;
		uint32 OriginalNumReads;
	};

private:
#if UE_WITH_OBJECT_HANDLE_TRACKING
	static void OnRefResolved(const FObjectRef& ObjectRef, UPackage* Pkg, UObject* Obj)
	{
		NumResolves++;
		if (!ObjectRef.IsNull() && !Obj)
		{
			NumFailedResolves++;
		}
	}
	static void OnRefRead(UObject* Obj)
	{
		NumReads++;
	}
#endif
	
	static void ConditionalInstallCallbacks()
	{
		static bool bCallbacksInstalled = false;
		static FCriticalSection CallbackInstallationLock;

		if (bCallbacksInstalled)
		{
			return;
		}

		FScopeLock ScopeLock(&CallbackInstallationLock);
		if (!bCallbacksInstalled)
		{
#if UE_WITH_OBJECT_HANDLE_TRACKING
			ResolvedCallbackHandle = AddObjectHandleReferenceResolvedCallback(FObjectHandleReferenceResolvedDelegate::CreateStatic(OnRefResolved));
			HandleReadCallbackHandle = AddObjectHandleReadCallback(FObjectHandleReadDelegate::CreateStatic(OnRefRead));
			// TODO We should unhook these handles somewhere, but i don't want to refactor the test, it's not as if they were
			// being unhooked before.  So...
#endif
			bCallbacksInstalled = true;
		}
	}

#if UE_WITH_OBJECT_HANDLE_TRACKING
	static FDelegateHandle ResolvedCallbackHandle;
	static FDelegateHandle HandleReadCallbackHandle;
#endif
	static thread_local uint32 NumResolves;
	static thread_local uint32 NumFailedResolves;
	static thread_local uint32 NumReads;
};

#endif