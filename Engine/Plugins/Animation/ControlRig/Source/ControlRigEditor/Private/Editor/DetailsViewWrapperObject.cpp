// Copyright Epic Games, Inc. All Rights Reserved.

#include "DetailsViewWrapperObject.h"
#include "Units/RigUnit.h"
#include "ControlRigElementDetails.h"
#include "ControlRigLocalVariableDetails.h"
#include "Modules/ModuleManager.h"

#if WITH_EDITOR
#include "PropertyEditorModule.h"
#endif

TMap<UScriptStruct*, UClass*> UDetailsViewWrapperObject::StructToClass;
TMap<UClass*, UScriptStruct*> UDetailsViewWrapperObject::ClassToStruct;

UClass* UDetailsViewWrapperObject::GetClassForStruct(UScriptStruct* InStruct, bool bCreateIfNeeded)
{
	check(InStruct != nullptr);

	UClass** ExistingClass = StructToClass.Find(InStruct);
	if(ExistingClass)
	{
		return *ExistingClass;
	}

	if(!bCreateIfNeeded)
	{
		return nullptr;
	}

	UClass* SuperClass = UDetailsViewWrapperObject::StaticClass();
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

	// duplicate all properties from the struct to the wrapper object
	FField** LinkToProperty = &WrapperClass->ChildProperties;

	for (TFieldIterator<FProperty> PropertyIt(InStruct); PropertyIt; ++PropertyIt)
	{
		const FProperty* InProperty = *PropertyIt;
		FProperty* NewProperty = CastFieldChecked<FProperty>(FField::Duplicate(InProperty, WrapperClass, InProperty->GetFName()));
		check(NewProperty);
		FField::CopyMetaData(InProperty, NewProperty);

		if (NewProperty->HasMetaData(TEXT("Input")) || NewProperty->HasMetaData(TEXT("Visible")))
		{
			// filter out execute pins
			bool bIsEditable = true;
			if (FStructProperty* StructProperty = CastField<FStructProperty>(NewProperty))
			{
				if (StructProperty->Struct->IsChildOf(FRigVMExecuteContext::StaticStruct()))
				{
					bIsEditable = false;
				}
			}

			if(bIsEditable)
			{
				NewProperty->SetPropertyFlags(NewProperty->GetPropertyFlags() | CPF_Edit);
			}
		}

		*LinkToProperty = NewProperty;
		LinkToProperty = &(*LinkToProperty)->Next;
	}

#if WITH_EDITOR
	if(InStruct->IsChildOf(FRigUnit::StaticStruct()))
	{
		FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		if (!PropertyEditorModule.GetClassNameToDetailLayoutNameMap().Contains(WrapperClassName))
		{
			PropertyEditorModule.RegisterCustomClassLayout(WrapperClassName, FOnGetDetailCustomizationInstance::CreateStatic(&FRigUnitDetails::MakeInstance));
		}
	}
	else if(InStruct->IsChildOf(FRigBaseElement::StaticStruct()))
	{
		FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		if (!PropertyEditorModule.GetClassNameToDetailLayoutNameMap().Contains(WrapperClassName))
		{
			if(InStruct == FRigBoneElement::StaticStruct())
			{
				PropertyEditorModule.RegisterCustomClassLayout(WrapperClassName, FOnGetDetailCustomizationInstance::CreateStatic(&FRigBoneElementDetails::MakeInstance));
			}
			else if(InStruct == FRigNullElement::StaticStruct())
			{
				PropertyEditorModule.RegisterCustomClassLayout(WrapperClassName, FOnGetDetailCustomizationInstance::CreateStatic(&FRigNullElementDetails::MakeInstance));
			}
			else if(InStruct == FRigControlElement::StaticStruct())
			{
				PropertyEditorModule.RegisterCustomClassLayout(WrapperClassName, FOnGetDetailCustomizationInstance::CreateStatic(&FRigControlElementDetails::MakeInstance));
			}
		}
	}
	else if(InStruct->IsChildOf(FRigVMGraphVariableDescription::StaticStruct()))
	{
		FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		if (!PropertyEditorModule.GetClassNameToDetailLayoutNameMap().Contains(WrapperClassName))
		{
			PropertyEditorModule.RegisterCustomClassLayout(WrapperClassName, FOnGetDetailCustomizationInstance::CreateStatic(&FRigVMLocalVariableDetails::MakeInstance));
		}
	}
#endif
	
	// Update the class
	WrapperClass->Bind();
	WrapperClass->StaticLink(true);
	
	// Similar to FConfigPropertyHelperDetails::CustomizeDetails, this is required for GC to work properly
	WrapperClass->AssembleReferenceTokenStream();

	StructToClass.Add(InStruct, WrapperClass);
	ClassToStruct.Add(WrapperClass, InStruct);

	UObject* CDO = WrapperClass->GetDefaultObject(true);
	CDO->AddToRoot();

	// import the defaults from the struct onto the class
	TSharedPtr<FStructOnScope> DefaultStruct = MakeShareable(new FStructOnScope(InStruct));
	CopyPropertiesForUnrelatedStructs((uint8*)CDO, WrapperClass, DefaultStruct->GetStructMemory(), DefaultStruct->GetStruct());

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

	UDetailsViewWrapperObject* Instance = NewObject<UDetailsViewWrapperObject>(InOuter, WrapperClass, NAME_None, RF_Public | RF_Transient | RF_TextExportTransient | RF_DuplicateTransient);
	Instance->SetContent(InStructMemory, InStruct);
	return Instance;
}

UScriptStruct* UDetailsViewWrapperObject::GetWrappedStruct() const
{
	return ClassToStruct.FindChecked(GetClass());
}

void UDetailsViewWrapperObject::SetContent(const uint8* InStructMemory, const UStruct* InStruct)
{
	CopyPropertiesForUnrelatedStructs((uint8*)this, GetClass(), InStructMemory, InStruct);
}

void UDetailsViewWrapperObject::GetContent(uint8* OutStructMemory, const UStruct* InStruct) const
{
	CopyPropertiesForUnrelatedStructs(OutStructMemory, InStruct, (const uint8*)this, GetClass());
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

void UDetailsViewWrapperObject::CopyPropertiesForUnrelatedStructs(uint8* InTargetMemory,
	const UStruct* InTargetStruct, const uint8* InSourceMemory, const UStruct* InSourceStruct)
{
	check(InTargetMemory);
	check(InTargetStruct);
	check(InSourceMemory);
	check(InSourceStruct);
	
	for (TFieldIterator<FProperty> PropertyIt(InTargetStruct); PropertyIt; ++PropertyIt)
	{
		const FProperty* TargetProperty = *PropertyIt;
		const FProperty* SourceProperty = InSourceStruct->FindPropertyByName(TargetProperty->GetFName());
		if(SourceProperty == nullptr)
		{
			continue;
		}
		check(TargetProperty->SameType(SourceProperty));

		uint8* TargetPropertyMemory = TargetProperty->ContainerPtrToValuePtr<uint8>(InTargetMemory);
		const uint8* SourcePropertyMemory = SourceProperty->ContainerPtrToValuePtr<uint8>(InSourceMemory);
		TargetProperty->CopyCompleteValue(TargetPropertyMemory, SourcePropertyMemory);
	}
}
