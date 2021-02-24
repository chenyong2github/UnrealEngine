// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "RemoteControlPreset.h"
#include "RemoteControlTestData.h"
#include "UObject/StrongObjectPtr.h"

#define PROP_NAME(Class, Name) GET_MEMBER_NAME_CHECKED(Class, Name)
#define GET_TEST_PROP(PropName) URemoteControlTestObject::StaticClass()->FindPropertyByName(PROP_NAME(URemoteControlTestObject, PropName))

namespace RemoteControlTest 
{
	void ValidateExposePropertyTest(FAutomationTestBase& Test, URemoteControlPreset* Preset, URemoteControlTestObject* TestObject, FProperty* Property, TOptional<FRemoteControlProperty> RCProp)
	{
		Test.TestTrue(TEXT("Property was exposed correctly"), RCProp.IsSet());
		if (!RCProp)
		{
			return;
		}

		Test.TestTrue(TEXT("IsExposed returns true."), Preset->IsExposed(RCProp->GetId()));
		Test.TestEqual(TEXT("Preset::GetProperty returns the same property."), RCProp, Preset->GetProperty(RCProp->GetId()));

		Test.TestTrue(Property->GetName() + TEXT(" must resolve correctly."), RCProp->FieldPathInfo.Resolve(TestObject));
		FRCFieldResolvedData ResolvedData = RCProp->FieldPathInfo.GetResolvedData();
		Test.TestTrue(TEXT("Resolved data is valid"), ResolvedData.ContainerAddress && ResolvedData.Field && ResolvedData.Struct);

		TOptional<FExposedProperty> ExposedProp = Preset->ResolveExposedProperty(RCProp->GetLabel());
		Test.TestTrue(TEXT("Property was exposed correctly"), ExposedProp.IsSet());
		if (!ExposedProp)
		{
			return;
		}

		Test.TestTrue(TEXT("Resolved property must be valid"), !!ExposedProp->Property);
		Test.TestTrue(TEXT("Resolved property's owner objects must be valid"), ExposedProp->OwnerObjects.Num() > 0);

		Test.TestEqual(TEXT("Resolved property must be equal to the original property."), Property, ExposedProp->Property);

		if (ExposedProp->OwnerObjects.Num())
		{
			Test.TestEqual(TEXT("Resolved property's owner objects must be valid"), TestObject, Cast<URemoteControlTestObject>(ExposedProp->OwnerObjects[0]));
		}
	}

	void TestExpose(FAutomationTestBase& Test, FProperty* Property)
	{
		// Setup test data
		TStrongObjectPtr<URemoteControlPreset> Preset{ NewObject<URemoteControlPreset>() };
		TStrongObjectPtr<URemoteControlTestObject> TestObject{ NewObject<URemoteControlTestObject>() };

		// Execute test
		FRemoteControlTarget& Target = Preset->CreateAndGetTarget({ TestObject.Get() });
		FRCFieldPathInfo Info{ Property->GetName() };
		TOptional<FRemoteControlProperty> RCProp = Target.ExposeProperty(Info, Property->GetName());

		// Validate result
		ValidateExposePropertyTest(Test, Preset.Get(), TestObject.Get(), Property, MoveTemp(RCProp));
	}

	void TestExposeContainerElement(FAutomationTestBase& Test,  FProperty* Property, FString PropertyPath, bool bCleanDuplicates = false)
	{
		// Setup test data
		TStrongObjectPtr<URemoteControlPreset> Preset{ NewObject<URemoteControlPreset>() };
		TStrongObjectPtr<URemoteControlTestObject> TestObject{ NewObject<URemoteControlTestObject>() };

		// Execute test
		FRemoteControlTarget& Target = Preset->CreateAndGetTarget({ TestObject.Get() });
		FRCFieldPathInfo Info{ PropertyPath, bCleanDuplicates };
		TOptional<FRemoteControlProperty> RCProp = Target.ExposeProperty(Info, Property->GetName());

		// Validate result
		ValidateExposePropertyTest(Test, Preset.Get(), TestObject.Get(), Property, RCProp);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRemoteControlPresetIntegrationTest, "Plugin.RemoteControl", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRemoteControlPresetIntegrationTest::RunTest(const FString& Parameters)
{
	// Test expose whole container
	RemoteControlTest::TestExpose(*this, GET_TEST_PROP(CStyleIntArray));
	RemoteControlTest::TestExpose(*this, GET_TEST_PROP(IntArray));
	RemoteControlTest::TestExpose(*this, GET_TEST_PROP(IntSet));
	RemoteControlTest::TestExpose(*this, GET_TEST_PROP(IntMap));

	// Test expose container element
	RemoteControlTest::TestExposeContainerElement(*this, GET_TEST_PROP(CStyleIntArray), GET_TEST_PROP(CStyleIntArray)->GetName() + TEXT("[0]"));
	RemoteControlTest::TestExposeContainerElement(*this, GET_TEST_PROP(IntArray),		GET_TEST_PROP(IntArray)->GetName() + TEXT("[0]"));
	RemoteControlTest::TestExposeContainerElement(*this, GET_TEST_PROP(IntSet),			GET_TEST_PROP(IntSet)->GetName() + TEXT("[0]"));
	RemoteControlTest::TestExposeContainerElement(*this, GET_TEST_PROP(IntMap),			GET_TEST_PROP(IntMap)->GetName() + TEXT("[0]"));

	// Test expose container element while skipping duplicates
	RemoteControlTest::TestExposeContainerElement(*this, GET_TEST_PROP(CStyleIntArray),  FString::Printf(TEXT("%s.%s[0]"),		 *GET_TEST_PROP(CStyleIntArray)->GetName(), *GET_TEST_PROP(CStyleIntArray)->GetName()), true);
	RemoteControlTest::TestExposeContainerElement(*this, GET_TEST_PROP(IntArray),		 FString::Printf(TEXT("%s.%s[0]"),		 *GET_TEST_PROP(IntArray)->GetName(), *GET_TEST_PROP(IntArray)->GetName()), true);
	RemoteControlTest::TestExposeContainerElement(*this, GET_TEST_PROP(IntSet),			 FString::Printf(TEXT("%s.%s[0]"),		 *GET_TEST_PROP(IntSet)->GetName(), *GET_TEST_PROP(IntSet)->GetName()), true);
	RemoteControlTest::TestExposeContainerElement(*this, GET_TEST_PROP(IntMap),			 FString::Printf(TEXT("%s.%s_Value[0]"), *GET_TEST_PROP(IntMap)->GetName(), *GET_TEST_PROP(IntMap)->GetName()), true);
	
	// Test exposing map with key indexing
	FProperty* RProperty = TBaseStructure<FColor>::Get()->FindPropertyByName(TEXT("R"));
	RemoteControlTest::TestExposeContainerElement(*this, RProperty,	FString::Printf(TEXT("%s[\"mykey\"].R"), *GET_TEST_PROP(StringColorMap)->GetName()));
	return true;
}

#undef GET_TEST_PROP
#undef PROP_NAME
