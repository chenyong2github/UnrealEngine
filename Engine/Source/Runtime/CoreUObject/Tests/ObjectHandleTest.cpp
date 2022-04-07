// Copyright Epic Games, Inc. All Rights Reserved.

#include "UObject/ObjectHandle.h"

#include "HAL/PlatformProperties.h"
#include "ObjectRefTrackingTestBase.h"
#include "IO/IoDispatcher.h"

#include "TestHarness.h"

static_assert(sizeof(FObjectHandle) == sizeof(void*), "FObjectHandle type must always compile to something equivalent to a pointer size.");

class FObjectHandleTestBase : public FObjectRefTrackingTestBase
{
public:
	
protected:
	
	UObject* ResolveHandle(FObjectHandle& TargetHandle)
	{
	#if UE_WITH_OBJECT_HANDLE_LATE_RESOLVE
		// Late resolved handles cannot be null or resolved at this point
		bool bValue = IsObjectHandleNull(TargetHandle);
		TEST_FALSE(TEXT("Handle to target is null"), bValue);
		if (bValue)
		{
			return nullptr;
		}
		bValue = IsObjectHandleResolved(TargetHandle);
		TEST_FALSE(TEXT("Handle to target is resolved"), bValue);
		if (bValue)
		{
			return nullptr;
		}
	#else
		bool bValue = IsObjectHandleResolved(TargetHandle);
		// Immediately resolved handles may be null (if the target is invalid) and must be resolved at this point
		TEST_TRUE(TEXT("Handle to target is not resolved"), bValue);
		if (!bValue)
		{
			return nullptr;
		}
	#endif

		return ResolveObjectHandle(TargetHandle);
	}

	UObject* ConstructAndResolveHandle(const ANSICHAR* PackageName, const ANSICHAR* ObjectName, const ANSICHAR* ClassPackageName = nullptr, const ANSICHAR* ClassName = nullptr)
	{
		FObjectRef TargetRef{FName(PackageName), FName(ClassPackageName), FName(ClassName), FObjectPathId(ObjectName)};
		bool bValue = IsObjectRefNull(TargetRef);
		TEST_FALSE(TEXT("Reference to target is null"), bValue);
		if (bValue)
		{
			return nullptr;
		}

		FObjectHandle TargetHandle = MakeObjectHandle(TargetRef);
		return ResolveHandle(TargetHandle);
	}

	UObject* ConstructAndResolveHandle(const FPackedObjectRef& PackedTargetRef)
	{
		bool bValue = IsPackedObjectRefNull(PackedTargetRef);
		TEST_FALSE(TEXT("Reference to target is null"), bValue);
		if (bValue)
		{
			return nullptr;
		}

		FObjectHandle TargetHandle = MakeObjectHandle(PackedTargetRef);
		return ResolveHandle(TargetHandle);
	}

	bool TestResolvableNonNull(const ANSICHAR* PackageName, const ANSICHAR* ObjectName, const ANSICHAR* ClassPackageName = nullptr, const ANSICHAR* ClassName = nullptr, bool bExpectSubRefReads = false)
	{
		FSnapshotObjectRefMetrics ObjectRefMetrics(*this);
		UObject* ResolvedObject = ConstructAndResolveHandle(PackageName, ObjectName, ClassPackageName, ClassName);
		ObjectRefMetrics.TestNumResolves(TEXT("NumResolves should be incremented by one after a resolve attempt"), 1);
		ObjectRefMetrics.TestNumReads(TEXT("NumReads should be incremented by one after a resolve attempt"), 1, bExpectSubRefReads /*bAllowAdditionalReads*/);

		if (!ResolvedObject)
		{
			FAIL_CHECK(FString::Printf(TEXT("Expected '%s.%s' to resolve to non null."), ANSI_TO_TCHAR(PackageName), ANSI_TO_TCHAR(ObjectName)));
			return false;
		}
		ObjectRefMetrics.TestNumFailedResolves(TEXT("NumFailedResolves should not change after a successful resolve attempt"), 0);

		return true;
	}

	bool TestResolveFailure(const ANSICHAR* PackageName, const ANSICHAR* ObjectName, const ANSICHAR* ClassPackageName = nullptr, const ANSICHAR* ClassName = nullptr)
	{
		FSnapshotObjectRefMetrics ObjectRefMetrics(*this);
		UObject* ResolvedObject = ConstructAndResolveHandle(PackageName, ObjectName, ClassPackageName, ClassName);
		ObjectRefMetrics.TestNumResolves(TEXT("NumResolves should be incremented by one after a resolve attempt"), 1);
		ObjectRefMetrics.TestNumReads(TEXT("NumReads should be incremented by one after a resolve attempt"), 1);

		if (ResolvedObject)
		{
			FAIL_CHECK(FString::Printf(TEXT("Expected '%s.%s' to resolve to null."), ANSI_TO_TCHAR(PackageName), ANSI_TO_TCHAR(ObjectName)));
			return false;
		}
		ObjectRefMetrics.TestNumFailedResolves(TEXT("NumFailedResolves should be incremented by one after a failed resolve attempt"), 1);
		return true;
	}

	bool TestResolveFailure(FPackedObjectRef PackedRef)
	{
		FSnapshotObjectRefMetrics ObjectRefMetrics(*this);
		UObject* ResolvedObject = ConstructAndResolveHandle(PackedRef);
		ObjectRefMetrics.TestNumResolves(TEXT("NumResolves should be incremented by one after a resolve attempt"), 1);
		ObjectRefMetrics.TestNumReads(TEXT("NumReads should be incremented by one after a resolve attempt"), 1);

		if (ResolvedObject)
		{
			FAIL_CHECK(FString::Printf(TEXT("Expected PACKEDREF(%" UPTRINT_X_FMT ") to resolve to null."), PackedRef.EncodedRef));
			return false;
		}
		ObjectRefMetrics.TestNumFailedResolves(TEXT("NumFailedResolves should be incremented by one after a failed resolve attempt"), 1);
		return true;
	}
};

TEST_CASE_METHOD(FObjectHandleTestBase, "CoreUObject::FObjectHandle::Null Behavior", "[CoreUObject][ObjectHandle]")
{
	FObjectHandle TargetHandle = MakeObjectHandle(nullptr);

	TEST_TRUE(TEXT("Handle to target is null"), IsObjectHandleNull(TargetHandle));
	TEST_TRUE(TEXT("Handle to target is resolved"), IsObjectHandleResolved(TargetHandle));

	FSnapshotObjectRefMetrics ObjectRefMetrics(*this);
	UObject* ResolvedObject = ResolveObjectHandle(TargetHandle);

	TEST_EQUAL(TEXT("Resolved object is equal to original object"), (UObject*)nullptr, ResolvedObject);

	ObjectRefMetrics.TestNumFailedResolves(TEXT("NumFailedResolves should not change after a resolve attempt on a null handle"), 0);
	ObjectRefMetrics.TestNumResolves(TEXT("NumResolves should not change after a resolve attempt on a null handle"), 0);
	ObjectRefMetrics.TestNumReads(TEXT("NumReads should be incremented by one after a resolve attempt on a null handle"), 1);
}

TEST_CASE_METHOD(FObjectHandleTestBase, "CoreUObject::FObjectHandle::Pointer Behavior", "[CoreUObject][ObjectHandle]")
{
	FObjectHandle TargetHandle = MakeObjectHandle((UObject*)0x0042);

	TEST_FALSE(TEXT("Handle to target is null"), IsObjectHandleNull(TargetHandle));
	TEST_TRUE(TEXT("Handle to target is resolved"), IsObjectHandleResolved(TargetHandle));

	FSnapshotObjectRefMetrics ObjectRefMetrics(*this);
	UObject* ResolvedObject = ResolveObjectHandle(TargetHandle);

	TEST_EQUAL(TEXT("Resolved object is equal to original object"), (UObject*)0x0042, ResolvedObject);

	ObjectRefMetrics.TestNumResolves(TEXT("NumResolves should not change after a resolve attempt on a pointer handle"), 0);
	ObjectRefMetrics.TestNumFailedResolves(TEXT("NumFailedResolves should not change after a resolve attempt on a pointer handle"), 0);
	ObjectRefMetrics.TestNumReads(TEXT("NumReads should be incremented by one after a resolve attempt on a pointer handle"),1);
}

TEST_CASE_METHOD(FObjectHandleTestBase, "CoreUObject::FObjectHandle::Resolve Engine Content Target", "[CoreUObject][ObjectHandle][.Engine]")
{
	// Confirm we successfully resolve a correct reference to engine content
	TestResolvableNonNull("/Engine/EngineResources/DefaultTexture", "DefaultTexture");

	// @TODO: OBJPTR: These assets aren't in a standard cook of EngineTest, so avoid testing them when using cooked content.  Should look for other assets to use instead.
	if (!FPlatformProperties::RequiresCookedData())
	{
		// Confirm we successfully resolve a correct reference to a subobject in engine content
		TestResolvableNonNull("/Engine/FunctionalTesting/Blueprints/AITesting_MoveGoal", "AITesting_MoveGoal.EventGraph.K2Node_VariableGet_142", nullptr, nullptr, true);

		// Attempt to load something that uses a User Defined Enum
		TestResolvableNonNull("/Engine/ArtTools/RenderToTexture/Macros/RenderToTextureMacros", "RenderToTextureMacros:Array to HLSL Float Array.K2Node_Select_1", nullptr, nullptr, true);
	}
}

TEST_CASE_METHOD(FObjectHandleTestBase, "CoreUObject::FObjectHandle::Resolve Non Existent Target", "[CoreUObject][ObjectHandle]")
{
	// Confirm we don't successfully resolve an incorrect reference to engine content
	TestResolveFailure("/Engine/EngineResources/NonExistentPackageName_0", "DefaultTexture");
	TestResolveFailure("/Engine/EngineResources/DefaultTexture", "NonExistentObject_0");
}


TEST_CASE_METHOD(FObjectHandleTestBase, "CoreUObject::FObjectHandle::Resolve Script Target", "[CoreUObject][ObjectHandle][.Engine]")
{
	// Confirm we successfully resolve a correct reference to engine content
	TestResolvableNonNull("/Script/Engine", "Default__Actor");
	TestResolvableNonNull("/Script/Engine", "DefaultPawn");
}

#if UE_WITH_OBJECT_HANDLE_LATE_RESOLVE

TEST_CASE_METHOD(FObjectHandleTestBase, "CoreUObject::FObjectHandle::Resolve Malformed Handle", "[CoreUObject][ObjectHandle]")
{
	TestResolveFailure(FPackedObjectRef { 0xFFFF'FFFF'FFFF'FFFFull });
	TestResolveFailure(FPackedObjectRef { 0xEFEF'EFEF'EFEF'EFEFull });
}
#endif // UE_WITH_OBJECT_HANDLE_LATE_RESOLVE
