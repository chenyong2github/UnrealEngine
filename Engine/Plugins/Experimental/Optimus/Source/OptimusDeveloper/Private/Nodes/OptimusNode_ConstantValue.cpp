// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusNode_ConstantValue.h"

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

		// Stash the data type so that the node can return it later.
		TypeClass->DataType = InDataType;

		// Create the property chain that represents this value.
		FProperty* InputValueProp = InDataType->CreateProperty(TypeClass, "Value");
		InputValueProp->PropertyFlags |= CPF_Edit;
		InputValueProp->SetMetaData(UOptimusNode::PropertyMeta::Input, TEXT("1"));
		InputValueProp->SetMetaData(UOptimusNode::PropertyMeta::Category, TEXT("Value"));

		// Out value doesn't need storage or saving.
		FProperty* OutputValueProp = InDataType->CreateProperty(TypeClass, "Out");
		OutputValueProp->SetFlags(RF_Transient);
		OutputValueProp->SetMetaData(UOptimusNode::PropertyMeta::Output, TEXT("1"));

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
	TArray<uint8> ValueResult;

	// FIXME: Check for value node chaining.
	// TODO: Move FProperty value conversion to type registry.

	const UOptimusNodePin *ValuePin = FindPinFromPath({TEXT("Value")});
	if (ensure(ValuePin))
	{
		const FProperty *ValueProperty = ValuePin->GetPropertyFromPin();
		if (ensure(ValueProperty))
		{
			const void* ValueData = ValueProperty->ContainerPtrToValuePtr<uint8>(this);
			
			if (const FBoolProperty *BoolProperty = CastField<FBoolProperty>(ValueProperty))
			{
				ValueResult.SetNumUninitialized(4);
				*reinterpret_cast<int32 *>(ValueResult.GetData()) = BoolProperty->GetPropertyValue(ValueData) ? 1 : 0; 
			}
			else if (const FIntProperty *Int32Property = CastField<FIntProperty>(ValueProperty))
			{
				ValueResult.SetNumUninitialized(4);
				*reinterpret_cast<int32 *>(ValueResult.GetData()) = Int32Property->GetPropertyValue(ValueData); 
			}
			else if (const FUInt32Property *UInt32Property = CastField<FUInt32Property>(ValueProperty))
			{
				ValueResult.SetNumUninitialized(4);
				*reinterpret_cast<uint32 *>(ValueResult.GetData()) = UInt32Property->GetPropertyValue(ValueData); 
			}
			else if (const FFloatProperty *FloatProperty = CastField<FFloatProperty>(ValueProperty))
			{
				ValueResult.SetNumUninitialized(4);
				*reinterpret_cast<float *>(ValueResult.GetData()) = FloatProperty->GetPropertyValue(ValueData); 
			}
			// ....
		}
	}
	
	return ValueResult;
}
