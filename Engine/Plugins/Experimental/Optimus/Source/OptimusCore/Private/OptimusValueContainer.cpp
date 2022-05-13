// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusValueContainer.h"
#include "OptimusHelpers.h"

FName UOptimusValueContainerGeneratorClass::ValuePropertyName = TEXT("Value");

void UOptimusValueContainerGeneratorClass::Link(FArchive& Ar, bool bRelinkExistingProperties)
{
	Super::Link(Ar, bRelinkExistingProperties);

	// Force assembly of the reference token stream so that we can be properly handled by the
	// garbage collector.
	AssembleReferenceTokenStream(/*bForce=*/true);
}


UClass* UOptimusValueContainerGeneratorClass::GetClassForType(UPackage* InPackage, FOptimusDataTypeRef InDataType)
{
	UObject* ClassOuter = Optimus::GetGeneratorClassOuter(InPackage);
	
	const FString ClassName = TEXT("OptimusValueContainer_") + InDataType.TypeName.ToString();

	// Check if the asset object already owns this class.
	UOptimusValueContainerGeneratorClass *TypeClass = FindObject<UOptimusValueContainerGeneratorClass>(ClassOuter, *ClassName);
	if (!TypeClass)
	{
		UClass *ParentClass = UOptimusValueContainer::StaticClass();
		// Construct a value node class for this data type
		TypeClass = NewObject<UOptimusValueContainerGeneratorClass>(ClassOuter, *ClassName, RF_Standalone|RF_Public);
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

		// Make sure the CDO exists.
		(void)TypeClass->GetDefaultObject();
	}
	return TypeClass;
}

void UOptimusValueContainer::PostLoad()
{
	Super::PostLoad();

	if (GetClass()->GetOuter()->IsA<UPackage>())
	{
		// This class should be parented to the asset object instead of the package
		// because the engine no longer supports multiple 'assets' per package
		// In the past, there were assets created with this class parented to the package directly
		if (UObject* AssetObject = Optimus::GetGeneratorClassOuter(this->GetPackage()))
		{
			AssetObject->Modify();
			Optimus::RenameObject(GetClass(), nullptr, AssetObject);
		}
	}
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
		TArrayView<const uint8> ValueData(ValueProperty->ContainerPtrToValuePtr<uint8>(this), ValueProperty->GetSize());
		TArray<uint8> ValueResult;
		ValueResult.SetNumUninitialized(DataType->ShaderValueSize);

		if (DataType->ConvertPropertyValueToShader(ValueData, ValueResult))
		{
			return ValueResult;
		}
	}
	
	return {};
}