// Copyright Epic Games, Inc. All Rights Reserved.

#include "DetailsViewWrapperObject.h"
#include "Units/RigUnit.h"
#include "ControlRigElementDetails.h"

#if WITH_EDITOR
#include "PropertyEditorModule.h"
#endif

TMap<UScriptStruct*, UClass*> UDetailsViewWrapperObject::StructToClass;
TMap<UClass*, UScriptStruct*> UDetailsViewWrapperObject::ClassToStruct;

UClass* UDetailsViewWrapperObject::GetClassForStruct(UScriptStruct* InStruct)
{
	check(InStruct != nullptr);

	UClass** ExistingClass = StructToClass.Find(InStruct);
	if(ExistingClass)
	{
		return *ExistingClass;
	}

	UClass *SuperClass = UDetailsViewWrapperObject::StaticClass();
	const FName WrapperClassName(FString::Printf(TEXT("%s_WrapperObject"), *InStruct->GetStructCPPName()));

	UClass* WrapperClass = NewObject<UClass>(
		GetTransientPackage(),
		WrapperClassName,
		RF_Public | RF_Transient
 		);

	// make sure this doesn't get garbage collected
	WrapperClass->AddToRoot();

	// Eviscerate the class.
	WrapperClass->PurgeClass(false);
	WrapperClass->PropertyLink = SuperClass->PropertyLink;

	WrapperClass->SetSuperStruct(SuperClass);
	WrapperClass->ClassWithin = UObject::StaticClass();
	WrapperClass->ClassConfigName = SuperClass->ClassConfigName;
	WrapperClass->ClassFlags |= CLASS_NotPlaceable | CLASS_Hidden;
	WrapperClass->SetMetaData(TEXT("DisplayName"), *InStruct->GetDisplayNameText().ToString());

	struct Local
	{
		static bool IsStructHashable(const UScriptStruct* InStructType)
		{
			if (InStructType->IsNative())
			{
				return InStructType->GetCppStructOps() && InStructType->GetCppStructOps()->HasGetTypeHash();
			}
			else
			{
				for (TFieldIterator<FProperty> It(InStructType); It; ++It)
				{
					if (CastField<FBoolProperty>(*It))
					{
						continue;
					}
					else if (!It->HasAllPropertyFlags(CPF_HasGetValueTypeHash))
					{
						return false;
					}
				}
				return true;
			}
		}
	};

	FStructProperty* Property = new FStructProperty(WrapperClass, TEXT("StoredStruct"), RF_Public);
	Property->Struct = InStruct;
	if (Local::IsStructHashable(InStruct))
	{
		Property->SetPropertyFlags(CPF_HasGetValueTypeHash);
	}
	
	// Make sure the variables show up in the details panel
	Property->SetPropertyFlags(CPF_Edit);
	Property->SetMetaData(TEXT("ShowOnlyInnerProperties"), TEXT("true"));

	// For rig units mark all inputs as edit
	if(InStruct->IsChildOf(FRigUnit::StaticStruct()))
	{
		// mark all input properties with editanywhere
		for (TFieldIterator<FProperty> PropertyIt(InStruct); PropertyIt; ++PropertyIt)
		{
			FProperty* ChildProperty = *PropertyIt;
			if (!ChildProperty ->HasMetaData(TEXT("Input")))
			{
				continue;
			}

			// filter out execute pins
			if (FStructProperty* StructProperty = CastField<FStructProperty>(ChildProperty))
			{
				if (StructProperty->Struct->IsChildOf(FRigVMExecuteContext::StaticStruct()))
				{
					continue;
				}
			}

			// only do this for input pins
			if(!ChildProperty->HasMetaData(TEXT("Input")))
			{
				continue;
			}
			
			ChildProperty->SetPropertyFlags(ChildProperty->GetPropertyFlags() | CPF_Edit);
		}

#if WITH_EDITOR
		FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		if (!PropertyEditorModule.GetClassNameToDetailLayoutNameMap().Contains(InStruct->GetFName()))
		{
			PropertyEditorModule.RegisterCustomClassLayout(InStruct->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FRigUnitDetails::MakeInstance));
		}
#endif
	}

	// Given it's only one property we are adding
	// we can set the head of the linked list directly
	WrapperClass->ChildProperties = Property;

	// Update the class
	WrapperClass->Bind();
	WrapperClass->StaticLink(true);
	
	// Similar to FConfigPropertyHelperDetails::CustomizeDetails, this is required for GC to work properly
	WrapperClass->AssembleReferenceTokenStream();

	StructToClass.Add(InStruct, WrapperClass);
	ClassToStruct.Add(WrapperClass, InStruct);

	UObject* CDO = WrapperClass->GetDefaultObject(true);
	CDO->AddToRoot();

	return WrapperClass;
}

UDetailsViewWrapperObject* UDetailsViewWrapperObject::MakeInstance(UScriptStruct* InStruct, uint8* InStructMemory, UObject* InOuter)
{
	check(InStruct != nullptr);

	InOuter = InOuter == nullptr ? GetTransientPackage() : InOuter;
	
	UClass* WrapperClass = GetClassForStruct(InStruct);
	if(WrapperClass == nullptr)
	{
		return nullptr;
	}

	UDetailsViewWrapperObject* Instance = NewObject<UDetailsViewWrapperObject>(InOuter, WrapperClass, NAME_None, RF_Public | RF_Transient);
	Instance->SetContent(InStructMemory, InStruct->GetStructureSize());
	return Instance;
}

UScriptStruct* UDetailsViewWrapperObject::GetWrappedStruct() const
{
	return GetStructProperty()->Struct;
}

FStructProperty* UDetailsViewWrapperObject::GetStructProperty() const
{
	return CastFieldChecked<FStructProperty>(GetClass()->ChildProperties);
}

void UDetailsViewWrapperObject::SetContent(const uint8* InStructMemory, int32 InStructSize)
{
	UScriptStruct* Struct = GetWrappedStruct();
	check(Struct);
	check(InStructSize == Struct->GetStructureSize());

	uint8* Content = GetStoredStructPtr();
	Struct->CopyScriptStruct(Content, InStructMemory);
}

const uint8* UDetailsViewWrapperObject::GetContent()
{
	return GetStoredStructPtr();
}

void UDetailsViewWrapperObject::GetContent(uint8* OutStructMemory, int32 InStructSize)
{
	UScriptStruct* Struct = GetWrappedStruct();
	check(Struct);
	check(InStructSize == Struct->GetStructureSize());

	const uint8* Content = GetContent();
	Struct->CopyScriptStruct(OutStructMemory, Content);
}

void UDetailsViewWrapperObject::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedEvent);

	FString PropertyPath;

	const TDoubleLinkedList<FProperty*>::TDoubleLinkedListNode* PropertyNode = PropertyChangedEvent.PropertyChain.GetHead();
	do
	{
		const FString PropertyName = PropertyNode->GetValue()->GetNameCPP();
		if(PropertyPath.IsEmpty())
		{
			PropertyPath = PropertyName;
		}
		else
		{
			PropertyPath = FString::Printf(TEXT("%s->%s"), *PropertyPath, *PropertyName);
		}
		PropertyNode = PropertyNode->GetNextNode();
	}
	while(PropertyNode);

	WrappedPropertyChangedChainEvent.Broadcast(this, PropertyPath, PropertyChangedEvent);
}

uint8* UDetailsViewWrapperObject::GetStoredStructPtr()
{
	if(FStructProperty* StructProperty = CastField<FStructProperty>(GetClass()->ChildProperties))
	{
		return StructProperty->ContainerPtrToValuePtr<uint8>(this);
	}
	checkNoEntry();
	return nullptr;
}
