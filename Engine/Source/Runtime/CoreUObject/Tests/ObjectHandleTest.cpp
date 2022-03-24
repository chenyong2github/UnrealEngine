// Copyright Epic Games, Inc. All Rights Reserved.

#include "UObject/ObjectHandle.h"

#include "HAL/PlatformProperties.h"
#include "ObjectRefTrackingTestBase.h"
#include "IO/IoDispatcher.h"

static_assert(sizeof(FObjectHandle) == sizeof(void*), "FObjectHandle type must always compile to something equivalent to a pointer size.");

#if WITH_DEV_AUTOMATION_TESTS

class FObjectHandleTestBase : public FObjectRefTrackingTestBase
{
public:
	FObjectHandleTestBase(const FString& InName, const bool bInComplexTask)
	: FObjectRefTrackingTestBase(InName, bInComplexTask)
	{
	}

protected:
	
	UObject* ResolveHandle(FObjectHandle& TargetHandle)
	{
	#if UE_WITH_OBJECT_HANDLE_LATE_RESOLVE
		// Late resolved handles cannot be null or resolved at this point
		if (!TestFalse(TEXT("Handle to target is null"), IsObjectHandleNull(TargetHandle)))
		{
			return nullptr;
		}
		if (!TestFalse(TEXT("Handle to target is resolved"), IsObjectHandleResolved(TargetHandle)))
		{
			return nullptr;
		}
	#else
		// Immediately resolved handles may be null (if the target is invalid) and must be resolved at this point
		if (!TestTrue(TEXT("Handle to target is not resolved"), IsObjectHandleResolved(TargetHandle)))
		{
			return nullptr;
		}
	#endif

		return ResolveObjectHandle(TargetHandle);
	}

	UObject* ConstructAndResolveHandle(const ANSICHAR* PackageName, const ANSICHAR* ObjectName, const ANSICHAR* ClassPackageName = nullptr, const ANSICHAR* ClassName = nullptr)
	{
		FObjectRef TargetRef{FName(PackageName), FName(ClassPackageName), FName(ClassName), FObjectPathId(ObjectName)};
		if (!TestFalse(TEXT("Reference to target is null"), IsObjectRefNull(TargetRef)))
		{
			return nullptr;
		}

		FObjectHandle TargetHandle = MakeObjectHandle(TargetRef);
		return ResolveHandle(TargetHandle);
	}

	UObject* ConstructAndResolveHandle(const FPackedObjectRef& PackedTargetRef)
	{
		if (!TestFalse(TEXT("Reference to target is null"), IsPackedObjectRefNull(PackedTargetRef)))
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
			AddError(FString::Printf(TEXT("Expected '%s.%s' to resolve to non null."), ANSI_TO_TCHAR(PackageName), ANSI_TO_TCHAR(ObjectName)), 1);
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
			AddError(FString::Printf(TEXT("Expected '%s.%s' to resolve to null."), ANSI_TO_TCHAR(PackageName), ANSI_TO_TCHAR(ObjectName)), 1);
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
			AddError(FString::Printf(TEXT("Expected PACKEDREF(%" UPTRINT_X_FMT ") to resolve to null."), PackedRef.EncodedRef), 1);
			return false;
		}
		ObjectRefMetrics.TestNumFailedResolves(TEXT("NumFailedResolves should be incremented by one after a failed resolve attempt"), 1);
		return true;
	}
};

#define TEST_NAME_ROOT TEXT("System.CoreUObject.ObjectHandle")
constexpr const uint32 ObjectHandleTestFlags = EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter;

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FObjectHandleTestNullBehavior, FObjectHandleTestBase, TEST_NAME_ROOT TEXT(".NullBehavior"), ObjectHandleTestFlags)
bool FObjectHandleTestNullBehavior::RunTest(const FString& Parameters)
{
	FObjectHandle TargetHandle = MakeObjectHandle(nullptr);

	TestTrue(TEXT("Handle to target is null"), IsObjectHandleNull(TargetHandle));
	TestTrue(TEXT("Handle to target is resolved"), IsObjectHandleResolved(TargetHandle));

	FSnapshotObjectRefMetrics ObjectRefMetrics(*this);
	UObject* ResolvedObject = ResolveObjectHandle(TargetHandle);

	TestEqual(TEXT("Resolved object is equal to original object"), (UObject*)nullptr, ResolvedObject);

	ObjectRefMetrics.TestNumFailedResolves(TEXT("NumFailedResolves should not change after a resolve attempt on a null handle"), 0);
	ObjectRefMetrics.TestNumResolves(TEXT("NumResolves should not change after a resolve attempt on a null handle"), 0);
	ObjectRefMetrics.TestNumReads(TEXT("NumReads should be incremented by one after a resolve attempt on a null handle"), 1);

	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FObjectHandleTestPointerBehavior, FObjectHandleTestBase, TEST_NAME_ROOT TEXT(".PointerBehavior"), ObjectHandleTestFlags)
bool FObjectHandleTestPointerBehavior::RunTest(const FString& Parameters)
{
	FObjectHandle TargetHandle = MakeObjectHandle((UObject*)0x0042);

	TestFalse(TEXT("Handle to target is null"), IsObjectHandleNull(TargetHandle));
	TestTrue(TEXT("Handle to target is resolved"), IsObjectHandleResolved(TargetHandle));

	FSnapshotObjectRefMetrics ObjectRefMetrics(*this);
	UObject* ResolvedObject = ResolveObjectHandle(TargetHandle);

	TestEqual(TEXT("Resolved object is equal to original object"), (UObject*)0x0042, ResolvedObject);

	ObjectRefMetrics.TestNumResolves(TEXT("NumResolves should not change after a resolve attempt on a pointer handle"), 0);
	ObjectRefMetrics.TestNumFailedResolves(TEXT("NumFailedResolves should not change after a resolve attempt on a pointer handle"), 0);
	ObjectRefMetrics.TestNumReads(TEXT("NumReads should be incremented by one after a resolve attempt on a pointer handle"),1);

	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FObjectHandleTestResolveEngineContentTarget, FObjectHandleTestBase, TEST_NAME_ROOT TEXT(".ResolveEngineContentTarget"), ObjectHandleTestFlags)
bool FObjectHandleTestResolveEngineContentTarget::RunTest(const FString& Parameters)
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

	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FObjectHandleTestResolveNonExistentTarget, FObjectHandleTestBase, TEST_NAME_ROOT TEXT(".ResolveNonExistentTarget"), ObjectHandleTestFlags)
bool FObjectHandleTestResolveNonExistentTarget::RunTest(const FString& Parameters)
{
	// Confirm we don't successfully resolve an incorrect reference to engine content
	TestResolveFailure("/Engine/EngineResources/NonExistentPackageName_0", "DefaultTexture");
	TestResolveFailure("/Engine/EngineResources/DefaultTexture", "NonExistentObject_0");

	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FObjectHandleTestResolveScriptTarget, FObjectHandleTestBase, TEST_NAME_ROOT TEXT(".ResolveScriptTarget"), ObjectHandleTestFlags)
bool FObjectHandleTestResolveScriptTarget::RunTest(const FString& Parameters)
{
	// Confirm we successfully resolve a correct reference to engine content
	TestResolvableNonNull("/Script/Engine", "Default__Actor");

	TestResolvableNonNull("/Script/Engine", "DefaultPawn");

	return true;
}

#if UE_WITH_OBJECT_HANDLE_LATE_RESOLVE
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FObjectHandleTestResolveMalformedHandle, FObjectHandleTestBase, TEST_NAME_ROOT TEXT(".ResolveMalformedHandle"), ObjectHandleTestFlags)
bool FObjectHandleTestResolveMalformedHandle::RunTest(const FString& Parameters)
{
	TestResolveFailure(FPackedObjectRef { 0xFFFF'FFFF'FFFF'FFFFull });
	TestResolveFailure(FPackedObjectRef { 0xEFEF'EFEF'EFEF'EFEFull });

	return true;
}
#endif // UE_WITH_OBJECT_HANDLE_LATE_RESOLVE

#undef TEST_NAME_ROOT

#endif // WITH_DEV_AUTOMATION_TESTS
