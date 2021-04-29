// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Editor.h"
#include "RCPropertyContainer.h"
#include "RCPropertyContainerTestData.h"
#include "Camera/CameraComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

BEGIN_DEFINE_SPEC(FPropertyContainerSpec,
	"PropertyContainer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::ApplicationContextMask)
END_DEFINE_SPEC(FPropertyContainerSpec)

void FPropertyContainerSpec::Define()
{
	Describe("Registry", [this]()
	{
		It("Returns_Valid_Subsystem", [this]
		{
			URCPropertyContainerRegistry* PropertyContainerRegistry = GEditor->GetEditorSubsystem<URCPropertyContainerRegistry>();
			TestNotNull("PropertyContainerRegistry", PropertyContainerRegistry);
		});
	});
	
	Describe("Float", [this]
	{
		It("Can_Create_Value", [this]
		{
			FProperty* FloatProperty = UCameraComponent::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UCameraComponent, PostProcessBlendWeight));
			check(FloatProperty);
		
			URCPropertyContainerRegistry* PropertyContainerRegistry = GEditor->GetEditorSubsystem<URCPropertyContainerRegistry>();
			URCPropertyContainerBase* Container = PropertyContainerRegistry->CreateContainer(GetTransientPackage(), NAME_FloatProperty, FloatProperty);

			TestNotNull("Container", Container);
		});
		It("Can_Set_Value", [this]
		{
			UPropertyContainerTestObject* ObjectInstance = NewObject<UPropertyContainerTestObject>(GetTransientPackage());
			
		    FProperty* ValueProperty = UPropertyContainerTestObject::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UPropertyContainerTestObject, SomeFloat));
		    check(ValueProperty);
				
		    URCPropertyContainerRegistry* PropertyContainerRegistry = GEditor->GetEditorSubsystem<URCPropertyContainerRegistry>();
		    URCPropertyContainerBase* Container = PropertyContainerRegistry->CreateContainer(GetTransientPackage(), NAME_FloatProperty, ValueProperty);

            void* ValuePtr = ValueProperty->ContainerPtrToValuePtr<void>(ObjectInstance);

			Container->SetValue((uint8*)ValuePtr);
		});
		It("Can_Get_Value", [this]
        {
            UPropertyContainerTestObject* ObjectInstance = NewObject<UPropertyContainerTestObject>(GetTransientPackage());
			
            FProperty* ValueProperty = UPropertyContainerTestObject::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UPropertyContainerTestObject, SomeFloat));
            check(ValueProperty);
				
            URCPropertyContainerRegistry* PropertyContainerRegistry = GEditor->GetEditorSubsystem<URCPropertyContainerRegistry>();
            URCPropertyContainerBase* Container = PropertyContainerRegistry->CreateContainer(GetTransientPackage(), NAME_FloatProperty, ValueProperty);

			float InputValue = 84.3f;
			ObjectInstance->SomeFloat = InputValue;
			
            void* ValuePtr = ValueProperty->ContainerPtrToValuePtr<void>(ObjectInstance);
            Container->SetValue((uint8*)ValuePtr);

			Container->GetValue((uint8*)ValuePtr);
			float OutputValue = ObjectInstance->SomeFloat;

			TestEqual("Input and Output", OutputValue, InputValue);
        });
	});

	Describe("Vector", [this]
	{
		It("Can_Create_Value", [this]
		{
			UPropertyContainerTestObject* ObjectInstance = NewObject<UPropertyContainerTestObject>(GetTransientPackage());

			FProperty* ValueProperty = UPropertyContainerTestObject::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UPropertyContainerTestObject, SomeVector));
			check(ValueProperty);

			URCPropertyContainerRegistry* PropertyContainerRegistry = GEditor->GetEditorSubsystem<URCPropertyContainerRegistry>();
			URCPropertyContainerBase* Container = PropertyContainerRegistry->CreateContainer(GetTransientPackage(), NAME_VectorProperty, ValueProperty);

			TestNotNull("Container", Container);
		});
		It("Can_Set_Value", [this]
		{
			UPropertyContainerTestObject* ObjectInstance = NewObject<UPropertyContainerTestObject>(GetTransientPackage());
			
		    FProperty* ValueProperty = UPropertyContainerTestObject::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UPropertyContainerTestObject, SomeVector));
		    check(ValueProperty);
				
		    URCPropertyContainerRegistry* PropertyContainerRegistry = GEditor->GetEditorSubsystem<URCPropertyContainerRegistry>();
		    URCPropertyContainerBase* Container = PropertyContainerRegistry->CreateContainer(GetTransientPackage(), NAME_VectorProperty, ValueProperty);

            void* ValuePtr = ValueProperty->ContainerPtrToValuePtr<void>(ObjectInstance);

			Container->SetValue((uint8*)ValuePtr);
		});
		It("Can_Get_Value", [this]
        {
            UPropertyContainerTestObject* ObjectInstance = NewObject<UPropertyContainerTestObject>(GetTransientPackage());
			
            FProperty* ValueProperty = UPropertyContainerTestObject::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UPropertyContainerTestObject, SomeVector));
            check(ValueProperty);
				
            URCPropertyContainerRegistry* PropertyContainerRegistry = GEditor->GetEditorSubsystem<URCPropertyContainerRegistry>();
            URCPropertyContainerBase* Container = PropertyContainerRegistry->CreateContainer(GetTransientPackage(), NAME_VectorProperty, ValueProperty);

			FVector InputValue(0.45f, 0.65f, -1.24f);
			ObjectInstance->SomeVector = InputValue;
			
            void* ValuePtr = ValueProperty->ContainerPtrToValuePtr<void>(ObjectInstance);
            Container->SetValue((uint8*)ValuePtr);

			Container->GetValue((uint8*)ValuePtr);
			FVector OutputValue = ObjectInstance->SomeVector;

			TestEqual("Input and Output", OutputValue, InputValue);
        });
	});
}
#endif
