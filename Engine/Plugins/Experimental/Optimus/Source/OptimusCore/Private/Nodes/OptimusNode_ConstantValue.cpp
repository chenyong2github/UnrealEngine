// Copyright Epic Games, Inc. All Rights Reserved.

#include "Nodes/OptimusNode_ConstantValue.h"

#include "OptimusNodePin.h"


void UOptimusNode_ConstantValueGeneratorClass::Link(FArchive& Ar, bool bRelinkExistingProperties)
{
	Super::Link(Ar, bRelinkExistingProperties);

	// Force assembly of the reference token stream so that we can be properly handled by the
	// garbage collector.
	AssembleReferenceTokenStream(/*bForce=*/true);
}


UClass* UOptimusNode_ConstantValueGeneratorClass::GetClassForType(UObject* InPackage, FOptimusDataTypeRef InDataType)
{
	const FString ClassName = TEXT("OptimusNode_ConstantValue_") + InDataType.TypeName.ToString();

	// Check if the package already owns this class.
	UOptimusNode_ConstantValueGeneratorClass *TypeClass = FindObject<UOptimusNode_ConstantValueGeneratorClass>(InPackage, *ClassName);
	if (!TypeClass)
	{
		UClass *ParentClass = UOptimusNode_ConstantValue::StaticClass();
		// Construct a value node class for this data type
		TypeClass = NewObject<UOptimusNode_ConstantValueGeneratorClass>(InPackage, *ClassName, RF_Standalone|RF_Public);
		TypeClass->SetSuperStruct(ParentClass);
		TypeClass->PropertyLink = ParentClass->PropertyLink;

		// Nodes of this type should not be listed in the node palette.
		TypeClass->ClassFlags |= CLASS_Hidden;

		// Stash the data type so that the node can return it later.
		TypeClass->DataType = InDataType;

		// Create the property chain that represents this value.
		FProperty* InputValueProp = InDataType->CreateProperty(TypeClass, "Value");
		InputValueProp->PropertyFlags |= CPF_Edit;
#if WITH_EDITOR
		InputValueProp->SetMetaData(UOptimusNode::PropertyMeta::Input, TEXT("1"));
		InputValueProp->SetMetaData(UOptimusNode::PropertyMeta::Category, TEXT("Value"));
#endif

		// Out value doesn't need storage or saving.
		FProperty* OutputValueProp = InDataType->CreateProperty(TypeClass, "Out");
		OutputValueProp->SetFlags(RF_Transient);
#if WITH_EDITOR
		OutputValueProp->SetMetaData(UOptimusNode::PropertyMeta::Output, TEXT("1"));
#endif

		// AddCppProperty chains backwards.
		TypeClass->AddCppProperty(OutputValueProp);
		TypeClass->AddCppProperty(InputValueProp);

		// Finalize the class
		TypeClass->Bind();
		TypeClass->StaticLink(true);
		TypeClass->AddToRoot();

		// Make sure the CDO exists.
		(void)TypeClass->GetDefaultObject();
	}
	return TypeClass;
}


FOptimusDataTypeRef UOptimusNode_ConstantValue::GetDataType() const
{
	UOptimusNode_ConstantValueGeneratorClass* Class = Cast<UOptimusNode_ConstantValueGeneratorClass>(GetClass());
	if (ensure(Class))
	{
		return Class->DataType;
	}
	return {};
}


TArray<uint8> UOptimusNode_ConstantValue::GetShaderValue() const
{
	const UOptimusNodePin *ValuePin = FindPinFromPath({TEXT("Value")});
	if (ensure(ValuePin))
	{
		const FProperty *ValueProperty = ValuePin->GetPropertyFromPin();
		FOptimusDataTypeRef DataType = GetDataType();
		if (ensure(ValueProperty) && ensure(DataType.IsValid()))
		{
			TArrayView<const uint8> ValueData(ValueProperty->ContainerPtrToValuePtr<uint8>(this), ValueProperty->GetSize());

			if (TArray<uint8> ValueResult;
				DataType->ConvertPropertyValueToShader(ValueData, ValueResult))
			{
				return ValueResult;
			}
		}
	}
	
	return {};
}


void UOptimusNode_ConstantValue::ConstructNode()
{
	SetDisplayName(FText::Format(FText::FromString(TEXT("{0} Constant")), GetDataType()->DisplayName));

	UOptimusNode::ConstructNode();
}
