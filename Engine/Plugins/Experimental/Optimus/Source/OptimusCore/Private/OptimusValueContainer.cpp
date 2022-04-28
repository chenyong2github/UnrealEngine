// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusValueContainer.h"

FName UOptimusValueContainerGeneratorClass::ValuePropertyName = TEXT("Value");

void UOptimusValueContainerGeneratorClass::Link(FArchive& Ar, bool bRelinkExistingProperties)
{
	Super::Link(Ar, bRelinkExistingProperties);

	// Force assembly of the reference token stream so that we can be properly handled by the
	// garbage collector.
	AssembleReferenceTokenStream(/*bForce=*/true);
}


UClass* UOptimusValueContainerGeneratorClass::GetClassForType(UObject* InPackage, FOptimusDataTypeRef InDataType)
{
	const FString ClassName = TEXT("OptimusValueContainer_") + InDataType.TypeName.ToString();

	// Check if the package already owns this class.
	UOptimusValueContainerGeneratorClass *TypeClass = FindObject<UOptimusValueContainerGeneratorClass>(InPackage, *ClassName);
	if (!TypeClass)
	{
		UClass *ParentClass = UOptimusValueContainer::StaticClass();
		// Construct a value node class for this data type
		TypeClass = NewObject<UOptimusValueContainerGeneratorClass>(InPackage, *ClassName, RF_Standalone|RF_Public);
		TypeClass->SetSuperStruct(ParentClass);
		TypeClass->PropertyLink = ParentClass->PropertyLink;

		// Nodes of this type should not be listed in the node palette.
		TypeClass->ClassFlags |= CLASS_Hidden;

		// Stash the data type so that the node can return it later.
		TypeClass->DataType = InDataType;

		// Create the property chain that represents this value.
		FProperty* DefaultValueProp = InDataType->CreateProperty(TypeClass, ValuePropertyName);
		DefaultValueProp->PropertyFlags |= CPF_Edit;

#if WITH_EDITOR
		const FName CategoryMetaName = TEXT("Category");
		DefaultValueProp->SetMetaData(CategoryMetaName, TEXT("Value"));
#endif

		// AddCppProperty chains backwards.
		TypeClass->AddCppProperty(DefaultValueProp);

		// Finalize the class
		TypeClass->Bind();
		TypeClass->StaticLink(true);
		TypeClass->AddToRoot();

		// Make sure the CDO exists.
		(void)TypeClass->GetDefaultObject();
	}
	return TypeClass;
}

UOptimusValueContainer* UOptimusValueContainer::MakeValueContainer(UObject* InOwner, FOptimusDataTypeRef InDataTypeRef)
{
	const UClass* Class = UOptimusValueContainerGeneratorClass::GetClassForType(InOwner->GetPackage(), InDataTypeRef);

	return NewObject<UOptimusValueContainer>(InOwner, Class);
}

FOptimusDataTypeRef UOptimusValueContainer::GetValueType() const
{
	UOptimusValueContainerGeneratorClass* Class = Cast<UOptimusValueContainerGeneratorClass>(GetClass());
	if (ensure(Class))
	{
		return Class->DataType;
	}
	return {};	
}

TArray<uint8> UOptimusValueContainer::GetShaderValue() const
{
	UOptimusValueContainerGeneratorClass* Class = Cast<UOptimusValueContainerGeneratorClass>(GetClass());
	const FProperty* ValueProperty = Class->PropertyLink;
	
	FOptimusDataTypeRef DataType = GetValueType();
	if (ensure(ValueProperty) && ensure(DataType.IsValid()))
	{
		if (ensure(ValueProperty) && ensure(DataType.IsValid()))
		{
			TArrayView<const uint8> ValueData(ValueProperty->ContainerPtrToValuePtr<uint8>(this), ValueProperty->GetSize());
			TArray<uint8> ValueResult;
			ValueResult.SetNumUninitialized(DataType->ShaderValueSize);

			if (DataType->ConvertPropertyValueToShader(ValueData, ValueResult))
			{
				return ValueResult;
			}
		}
	}
	
	return {};
}