// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_LOW_LEVEL_TESTS

#include "TestHarness.h"
#include "TestMacros/Assertions.h"
#include "ObjectPtrTestClass.h"
#include "UObject/Package.h"
#include "Misc/ScopeExit.h"
#include "UObject/UnrealType.h"
#include "UObject/LinkerPlaceholderExportObject.h"
#include "UObject/LinkerPlaceholderClass.h"
#include "UObject/ObjectHandleTracking.h"
#include "LowLevelTestsRunner/WarnFilterScope.h"

TEST_CASE("UE::CoreUObject::FClassProperty::Identical")
{
	bool bAllowRead = false;
#if UE_WITH_OBJECT_HANDLE_TRACKING
	auto CallbackHandle = UE::CoreUObject::AddObjectHandleReadCallback([&bAllowRead](TArrayView<const UObject* const> Objects)
		{
			if (!bAllowRead)
				FAIL("Unexpected read during CheckValidObject");
		});
	ON_SCOPE_EXIT
	{
		UE::CoreUObject::RemoveObjectHandleReadCallback(CallbackHandle);
	};
#endif
	UClass* Class = UObjectWithClassProperty::StaticClass();
	FObjectProperty* Property = CastField<FObjectProperty>(Class->FindPropertyByName(TEXT("ClassPtr")));
	REQUIRE(Property != nullptr);


	UPackage* TestPackage = NewObject<UPackage>(nullptr, TEXT("TestPackageName"), RF_Transient);
	TestPackage->AddToRoot();
	ON_SCOPE_EXIT
	{
		TestPackage->RemoveFromRoot();
	};
	UObjectWithClassProperty* Obj = NewObject<UObjectWithClassProperty>(TestPackage, TEXT("UObjectWithClassProperty"));

	CHECK(Property->Identical(Obj, Obj, 0u));

}
#endif