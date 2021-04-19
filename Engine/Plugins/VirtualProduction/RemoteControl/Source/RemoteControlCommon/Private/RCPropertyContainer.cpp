// Copyright Epic Games, Inc. All Rights Reserved.

#include "RCPropertyContainer.h"

#include "Editor.h"
#include "RemoteControlCommonModule.h"

namespace PropertyContainers
{
	URCPropertyContainerBase* CreateContainerForProperty(UObject* InOwner, const FProperty* InSrcProperty)
	{
		ensure(IsInGameThread());

		check(InOwner);
		check(InSrcProperty);

		URCPropertyContainerRegistry* ContainerRegistry = GEditor->GetEditorSubsystem<URCPropertyContainerRegistry>();
		FName PropertyTypeName = InSrcProperty->GetClass()->GetFName();
		if(const FStructProperty* StructProperty = CastField<FStructProperty>(InSrcProperty))
		{
			PropertyTypeName = StructProperty->Struct->GetFName();
		}
		return ContainerRegistry->CreateContainer(InOwner, PropertyTypeName, InSrcProperty);
	}
}

void URCPropertyContainerBase::SetValue(const uint8* InData)
{
	this->Modify();

	FProperty* ValueProperty = GetClass()->FindPropertyByName("Value");
	uint8* DstData = ValueProperty->ContainerPtrToValuePtr<uint8>(this);
	FMemory::Memcpy(DstData, InData, ValueProperty->ElementSize);
}

void URCPropertyContainerBase::GetValue(uint8* OutData)
{
	FProperty* ValueProperty = GetClass()->FindPropertyByName("Value");
	uint8* SrcData = ValueProperty->ContainerPtrToValuePtr<uint8>(this);
	FMemory::Memcpy(OutData, SrcData, ValueProperty->ElementSize);
}

FName FRCPropertyContainerKey::ToClassName() const
{
	auto ValueTrimmed = ValueTypeName.ToString();
	ValueTrimmed.RemoveFromEnd("Property");
	return FName(FString::Printf(TEXT("PropertyContainer_%s"), *ValueTrimmed));	
}

URCPropertyContainerBase* URCPropertyContainerRegistry::CreateContainer(UObject* InOwner, const FName& InValueTypeName, const FProperty* InValueSrcProperty)
{
	check(InValueSrcProperty);

	const TSubclassOf<URCPropertyContainerBase> ClassForPropertyType = FindOrAddContainerClass(InValueTypeName, InValueSrcProperty);
	if (ClassForPropertyType)
	{
		return NewObject<URCPropertyContainerBase>(InOwner
            ? InOwner
            : static_cast<UObject*>(GetTransientPackage()), ClassForPropertyType);
	}
	else
	{
		UE_LOG(LogRemoteControlCommon, Warning, TEXT("Could not create PropertyContainer found for %s"), *InValueTypeName.ToString());
		return nullptr;
	}
}

TSubclassOf<URCPropertyContainerBase>& URCPropertyContainerRegistry::FindOrAddContainerClass(const FName& InValueTypeName, const FProperty* InValueSrcProperty)
{
	check(InValueSrcProperty);
	
	const FRCPropertyContainerKey Key = FRCPropertyContainerKey{InValueTypeName};
	if(TSubclassOf<URCPropertyContainerBase>* ExistingContainerClass = CachedContainerClasses.Find(Key))
	{
		return *ExistingContainerClass;
	}

	UPackage* Outer = GetOutermost();
	UClass* ContainerClass = NewObject<UClass>(Outer, Key.ToClassName(), RF_Public | RF_Transient);
	UClass* ParentClass = URCPropertyContainerBase::StaticClass();
	ContainerClass->SetSuperStruct(ParentClass);

	FProperty* ValueProperty = CastField<FProperty>(FField::Duplicate(InValueSrcProperty, ContainerClass, "Value"));
	FField::CopyMetaData(InValueSrcProperty, ValueProperty);
	ValueProperty->SetFlags(RF_Transient);
	ValueProperty->PropertyFlags |= CPF_Edit | CPF_BlueprintVisible;

	ContainerClass->AddCppProperty(ValueProperty);

	ContainerClass->Bind();
	ContainerClass->StaticLink(true);
	ContainerClass->AssembleReferenceTokenStream(true);

	return CachedContainerClasses.Add(Key, MoveTemp(ContainerClass));
}
