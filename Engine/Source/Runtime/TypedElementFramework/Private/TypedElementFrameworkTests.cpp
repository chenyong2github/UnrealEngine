// Copyright Epic Games, Inc. All Rights Reserved.

#include "TypedElementFrameworkTests.h"
#include "Misc/AutomationTest.h"
#include "ProfilingDebugging/ScopedTimers.h"

UE_DEFINE_TYPED_ELEMENT_DATA_RTTI(FTestTypedElementData);

FText UTestTypedElementInterfaceA_ImplTyped::GetDisplayName(const FTypedElementHandle& InElementHandle)
{
	const FTestTypedElementData* ElementData = InElementHandle.GetData<FTestTypedElementData>();
	if (!ElementData)
	{
		return FText();
	}

	return FText();
}

bool UTestTypedElementInterfaceA_ImplTyped::SetDisplayName(const FTypedElementHandle& InElementHandle, FText InNewName, bool bNotify)
{
	const FTestTypedElementData* ElementData = InElementHandle.GetData<FTestTypedElementData>();
	if (!ElementData)
	{
		return false;
	}

	return false;
}

FText UTestTypedElementInterfaceA_ImplUntyped::GetDisplayName(const FTypedElementHandle& InElementHandle)
{
	return FText();
}

bool UTestTypedElementInterfaceA_ImplUntyped::SetDisplayName(const FTypedElementHandle& InElementHandle, FText InNewName, bool bNotify)
{
	return false;
}

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTypedElementRegistrySmokeTest, "System.Runtime.TypedElementRegistry.SmokeTest", EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)
bool FTypedElementRegistrySmokeTest::RunTest(const FString& Parameters)
{
	UTypedElementRegistry* Registry = NewObject<UTypedElementRegistry>();
	
	auto TestInterfaceAHandle = [](const TTypedElement<UTestTypedElementInterfaceA>& InElementDataInterfaceHandle)
	{
		// Proxy API added via specialization
		InElementDataInterfaceHandle.SetDisplayName(FText());
		InElementDataInterfaceHandle.GetDisplayName();

		// Verbose API
		InElementDataInterfaceHandle.GetInterfaceChecked().SetDisplayName(InElementDataInterfaceHandle, FText());
		InElementDataInterfaceHandle.GetInterfaceChecked().GetDisplayName(InElementDataInterfaceHandle);
	};

	auto TestInterfaceAccess = [&Registry, &TestInterfaceAHandle](const FTypedElementHandle& InElementHandle)
	{
		// Get the interface and the element handle in two calls - this is how scripting might work
		if (UTestTypedElementInterfaceA* Interface = Registry->GetElementInterface<UTestTypedElementInterfaceA>(InElementHandle))
		{
			Interface->SetDisplayName(InElementHandle, FText());
			Interface->GetDisplayName(InElementHandle);
		}

		// Get the interface and the element handle in a single call - this is how C++ might work
		if (TTypedElement<UTestTypedElementInterfaceA> Element = Registry->GetElement<UTestTypedElementInterfaceA>(InElementHandle))
		{
			TestInterfaceAHandle(Element);
		}
	};

	// Test that all the cast variants compile
	{
		FTypedElement DummyElement;
		CastTypedElement<UTestTypedElementInterfaceA>(DummyElement);
		CastTypedElement<UTestTypedElementInterfaceA>(FTypedElement());
		CastTypedElementChecked<UTestTypedElementInterfaceA>(DummyElement);
		CastTypedElementChecked<UTestTypedElementInterfaceA>(FTypedElement());

		TTypedElement<UTestTypedElementInterfaceA> DummyCastedElement;
		CastTypedElement<UTestTypedElementInterfaceA>(DummyElement, DummyCastedElement);
		CastTypedElement<UTestTypedElementInterfaceA>(FTypedElement(), DummyCastedElement);
		CastTypedElementChecked<UTestTypedElementInterfaceA>(DummyElement, DummyCastedElement);
		CastTypedElementChecked<UTestTypedElementInterfaceA>(FTypedElement(), DummyCastedElement);
	}

	const FName DummyElementType_Typed = "DummyElementType_Typed";
	Registry->RegisterElementType<FTestTypedElementData>(DummyElementType_Typed);
	Registry->RegisterElementInterface<UTestTypedElementInterfaceA>(DummyElementType_Typed, NewObject<UTestTypedElementInterfaceA_ImplTyped>());

	const FName DummyElementType_Untyped = "DummyElementType_Untyped";
	Registry->RegisterElementType(DummyElementType_Untyped);
	Registry->RegisterElementInterface<UTestTypedElementInterfaceA>(DummyElementType_Untyped, NewObject<UTestTypedElementInterfaceA_ImplUntyped>());

	TTypedElementOwner<FTestTypedElementData> TypedElement1 = Registry->CreateElement<FTestTypedElementData>(DummyElementType_Typed);
	TypedElement1.GetDataChecked().InternalElementId = "TypedElement1";
	TTypedElementOwner<FTestTypedElementData> TypedElement2 = Registry->CreateElement<FTestTypedElementData>(DummyElementType_Typed);
	TypedElement2.GetDataChecked().InternalElementId = "TypedElement2";
	TTypedElementOwner<FTestTypedElementData> TypedElement3 = Registry->CreateElement<FTestTypedElementData>(DummyElementType_Typed);
	TypedElement3.GetDataChecked().InternalElementId = "TypedElement3";

	FTypedElementOwner UntypedElement1 = Registry->CreateElement(DummyElementType_Untyped, 0);
	FTypedElementOwner UntypedElement2 = Registry->CreateElement(DummyElementType_Untyped, 1);
	FTypedElementOwner UntypedElement3 = Registry->CreateElement(DummyElementType_Untyped, 2);

	TestInterfaceAccess(TypedElement1.AcquireHandle());
	TestInterfaceAccess(UntypedElement1.AcquireHandle());

	UTypedElementList* ElementList = Registry->CreateElementList();
	ElementList->Add(TypedElement1);
	ElementList->Add(TypedElement2);
	ElementList->Add(TypedElement3);
	ElementList->Add(UntypedElement1);
	ElementList->Add(UntypedElement2);
	ElementList->Add(UntypedElement3);
	
	ElementList->ForEachElementHandle([&TestInterfaceAccess](const FTypedElementHandle& InElementHandle)
	{
		TestInterfaceAccess(InElementHandle);
		return true;
	});
	
	ElementList->ForEachElement<UTestTypedElementInterfaceA>([&TestInterfaceAHandle](const TTypedElement<UTestTypedElementInterfaceA>& InElementHandle)
	{
		TestInterfaceAHandle(InElementHandle);
		return true;
	});

	ElementList->Empty();
	ElementList = nullptr;

	Registry->DestroyElement(TypedElement1);
	Registry->DestroyElement(TypedElement2);
	Registry->DestroyElement(TypedElement3);

	Registry->DestroyElement(UntypedElement1);
	Registry->DestroyElement(UntypedElement2);
	Registry->DestroyElement(UntypedElement3);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTypedElementRegistryPerfTest, "System.Runtime.TypedElementRegistry.PerfTest", EAutomationTestFlags::EditorContext | EAutomationTestFlags::PerfFilter)
bool FTypedElementRegistryPerfTest::RunTest(const FString& Parameters)
{
	constexpr int32 NumHandlesToTest = 1000000;

	UTypedElementRegistry* Registry = NewObject<UTypedElementRegistry>();

	const FName DummyElementType_Typed = "DummyElementType_Typed";
	Registry->RegisterElementType<FTestTypedElementData>(DummyElementType_Typed);
	Registry->RegisterElementInterface<UTestTypedElementInterfaceA>(DummyElementType_Typed, NewObject<UTestTypedElementInterfaceA_ImplTyped>());

	const FName DummyElementType_Untyped = "DummyElementType_Untyped";
	Registry->RegisterElementType(DummyElementType_Untyped);
	Registry->RegisterElementInterface<UTestTypedElementInterfaceA>(DummyElementType_Untyped, NewObject<UTestTypedElementInterfaceA_ImplUntyped>());

	TArray<TTypedElementOwner<FTestTypedElementData>> TypedOwnerHandles;
	TArray<FTypedElementOwner> UntypedOwnerHandles;
	UTypedElementList* ElementList = Registry->CreateElementList();

	// Create typed handles
	{
		TypedOwnerHandles.Reserve(NumHandlesToTest);
		{
			FScopedDurationTimeLogger Timer(FString::Printf(TEXT("Creating %d typed handles"), NumHandlesToTest));
			for (int32 Index = 0; Index < NumHandlesToTest; ++Index)
			{
				TypedOwnerHandles.Emplace(Registry->CreateElement<FTestTypedElementData>(DummyElementType_Typed));
			}
		}
	}

	// Create untyped handles
	{
		UntypedOwnerHandles.Reserve(NumHandlesToTest);
		{
			FScopedDurationTimeLogger Timer(FString::Printf(TEXT("Creating %d untyped handles"), NumHandlesToTest));
			for (int32 Index = 0; Index < NumHandlesToTest; ++Index)
			{
				UntypedOwnerHandles.Emplace(Registry->CreateElement(DummyElementType_Untyped, Index));
			}
		}
	}

	// Populate an element list with all handles
	{
		FScopedDurationTimeLogger Timer(FString::Printf(TEXT("Appending %d typed handles to list"), TypedOwnerHandles.Num() + UntypedOwnerHandles.Num()));
		ElementList->Reserve(TypedOwnerHandles.Num() + UntypedOwnerHandles.Num());
		ElementList->Append(TypedOwnerHandles);
		ElementList->Append(UntypedOwnerHandles);
	}

	// Find an interface from each handle
	{
		FScopedDurationTimeLogger Timer(FString::Printf(TEXT("Finding %d interfaces from list"), ElementList->Num()));
		ElementList->ForEachElementHandle([Registry](const FTypedElementHandle& InElementHandle)
		{
			Registry->GetElementInterface<UTestTypedElementInterfaceA>(InElementHandle);
			return true;
		});
	}

	// Find an element from each handle
	{
		FScopedDurationTimeLogger Timer(FString::Printf(TEXT("Finding %d elements from list"), ElementList->Num()));
		ElementList->ForEachElementHandle([Registry](const FTypedElementHandle& InElementHandle)
		{
			Registry->GetElement<UTestTypedElementInterfaceA>(InElementHandle);
			return true;
		});
	}

	// Enumerate all elements that implement an interface
	{
		FScopedDurationTimeLogger Timer(FString::Printf(TEXT("Enumerating %d elements in list"), ElementList->Num()));
		ElementList->ForEachElement<UTestTypedElementInterfaceA>([Registry](const TTypedElement<UTestTypedElementInterfaceA>& InElementHandle)
		{
			return true;
		});
	}

	// Clear the element list
	{
		FScopedDurationTimeLogger Timer(FString::Printf(TEXT("Reset %d elements in list"), ElementList->Num()));
		ElementList->Empty();
		ElementList = nullptr;
	}

	// Destroy typed handles
	{
		{
			FScopedDurationTimeLogger Timer(FString::Printf(TEXT("Destroying %d typed handles"), TypedOwnerHandles.Num()));
			for (TTypedElementOwner<FTestTypedElementData>& TypedOwnerHandle : TypedOwnerHandles)
			{
				Registry->DestroyElement(TypedOwnerHandle);
			}
		}
		TypedOwnerHandles.Reset();
	}

	// Destroy untyped handles
	{
		{
			FScopedDurationTimeLogger Timer(FString::Printf(TEXT("Destroying %d untyped handles"), UntypedOwnerHandles.Num()));
			for (FTypedElementOwner& UntypedOwnerHandle : UntypedOwnerHandles)
			{
				Registry->DestroyElement(UntypedOwnerHandle);
			}
		}
		UntypedOwnerHandles.Reset();
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
