// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusNode_ComputeKernel.h"
#include "OptimusNodePin.h"

#include "OptimusDataTypeRegistry.h"


UOptimusNode_ComputeKernel::UOptimusNode_ComputeKernel()
{
	UpdatePreamble();

	ShaderSource.ShaderEpilogue = TEXT("}\n");
}


UOptimusKernelSource* UOptimusNode_ComputeKernel::CreateComputeKernel(UObject *InOuter) const
{
	UOptimusKernelSource* KernelSource = NewObject<UOptimusKernelSource>(InOuter, NAME_None);

	KernelSource->SetSourceAndEntryPoint(ShaderSource.GetSource(), KernelName);

	auto CopyValueType = [](FShaderValueTypeHandle InValueType,  FShaderParamTypeDefinition& OutParamDef)
	{
		OutParamDef.ValueType = InValueType;

		OutParamDef.ArrayElementCount = 0;
		OutParamDef.FundamentalType = InValueType->Type;
		OutParamDef.DimType = InValueType->DimensionType;
		switch(OutParamDef.DimType)
		{
		case EShaderFundamentalDimensionType::Scalar:
			OutParamDef.VectorDimension = 0;
			break;
		case EShaderFundamentalDimensionType::Vector:
			OutParamDef.VectorDimension = InValueType->VectorElemCount;
			break;
		case EShaderFundamentalDimensionType::Matrix:
			OutParamDef.MatrixRowCount = InValueType->MatrixRowCount;
			OutParamDef.MatrixColumnCount = InValueType->MatrixColumnCount;
			break;
		}
		OutParamDef.ResetTypeDeclaration();
	};

	for (const FOptimus_ShaderBinding& Param: Parameters)
	{
		FShaderParamTypeDefinition ParamDef;
		ParamDef.Name = Param.Name.ToString();
		ParamDef.BindingType = EShaderParamBindingType::ConstantParameter;
		CopyValueType(Param.DataType->ShaderValueType, ParamDef);

		KernelSource->InputParams.Emplace(ParamDef);
	}

	FShaderParamTypeDefinition IndexParamDef;
	CopyValueType(FShaderValueType::Get(EShaderFundamentalType::Uint), IndexParamDef);
	
	for (const FOptimus_ShaderInputResourceBinding& Input: InputBindings)
	{
		FShaderFunctionDefinition FuncDef;
		FuncDef.Name = Input.Name.ToString();
		FuncDef.bHasReturnType = true;

		FShaderParamTypeDefinition ParamDef;
		CopyValueType(Input.DataType->ShaderValueType, ParamDef);

		FuncDef.ParamTypes.Emplace(ParamDef);

		if (Input.bUsesIndex)
		{
			FuncDef.ParamTypes.Add(IndexParamDef);
		}

		KernelSource->ExternalInputs.Emplace(FuncDef);
	}

	for (const FOptimus_ShaderBinding& Output: OutputBindings)
	{
		FShaderFunctionDefinition FuncDef;
		FuncDef.Name = Output.Name.ToString();
		FuncDef.bHasReturnType = false;

		FShaderParamTypeDefinition ParamDef;
		CopyValueType(Output.DataType->ShaderValueType, ParamDef);

		FuncDef.ParamTypes.Add(IndexParamDef);
		FuncDef.ParamTypes.Emplace(ParamDef);

		KernelSource->ExternalOutputs.Emplace(FuncDef);
	}

	return KernelSource;
}


void UOptimusNode_ComputeKernel::PostEditChangeProperty(
	FPropertyChangedEvent& PropertyChangedEvent
	)
{
	static const FName ParametersName = GET_MEMBER_NAME_STRING_CHECKED(UOptimusNode_ComputeKernel, Parameters);
	static const FName InputBindingsName = GET_MEMBER_NAME_STRING_CHECKED(UOptimusNode_ComputeKernel, InputBindings);
	static const FName OutputBindingsName = GET_MEMBER_NAME_STRING_CHECKED(UOptimusNode_ComputeKernel, OutputBindings);

	const FName BasePropertyName = (PropertyChangedEvent.MemberProperty ? PropertyChangedEvent.MemberProperty->GetFName() : NAME_None);
	const FName PropertyName = (PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None);

	if (PropertyChangedEvent.ChangeType & EPropertyChangeType::ValueSet)
	{
		if (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(UOptimusNode_ComputeKernel, KernelName) ||
		    PropertyName == GET_MEMBER_NAME_STRING_CHECKED(UOptimusNode_ComputeKernel, InvocationCount))
		{
			UpdatePreamble();
		}
		else if (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(FOptimus_ShaderBinding, Name))
		{
			if (BasePropertyName == ParametersName || BasePropertyName == InputBindingsName)
			{
				UpdatePinNames(EOptimusNodePinDirection::Input);
			}
			else if (BasePropertyName == OutputBindingsName)
			{
				UpdatePinNames(EOptimusNodePinDirection::Output);
			}
			UpdatePreamble();
		}
		else if (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(FOptimusDataTypeRef, TypeName))
		{
			if (BasePropertyName == ParametersName || BasePropertyName == InputBindingsName)
			{
				UpdatePinTypes(EOptimusNodePinDirection::Input);
			}
			else if (BasePropertyName == OutputBindingsName)
			{
				UpdatePinTypes(EOptimusNodePinDirection::Output);
			}
			UpdatePreamble();
		}
	}
	else if (PropertyChangedEvent.ChangeType & EPropertyChangeType::ArrayAdd)
	{
		EOptimusNodePinDirection Direction = EOptimusNodePinDirection::Unknown;
		FOptimus_ShaderBinding *Binding = nullptr;
		FString Name;
		UOptimusNodePin *BeforePin = nullptr;
		EOptimusNodePinStorageType StorageType = EOptimusNodePinStorageType::Resource;

		if (BasePropertyName == ParametersName)
		{
			Direction = EOptimusNodePinDirection::Input;
			Binding = &Parameters.Last();
			Name = FString::Printf(TEXT("Param%d"), Parameters.Num());
			StorageType = EOptimusNodePinStorageType::Value;

			if (!InputBindings.IsEmpty())
			{
				BeforePin = GetPins()[Parameters.Num()];
			}	
		}
		else if (BasePropertyName == InputBindingsName)
		{
			Direction = EOptimusNodePinDirection::Input;
			Binding = &InputBindings.Last();
			Name = FString::Printf(TEXT("Read%d"), InputBindings.Num());
			InputBindings.Last().bUsesIndex = true;
		}
		else if (BasePropertyName == OutputBindingsName)
		{
			Direction = EOptimusNodePinDirection::Output;
			Binding = &OutputBindings.Last();
			Name = FString::Printf(TEXT("Write%d"), OutputBindings.Num());
		}

		if (ensure(Binding))
		{
			Binding->Name = *Name;
			Binding->DataType = FOptimusDataTypeRegistry::Get().FindType(*FFloatProperty::StaticClass());

			AddPin(Binding->Name, Direction, StorageType, Binding->DataType, BeforePin);

			UpdatePreamble();
		}
	}
	else if (PropertyChangedEvent.ChangeType & EPropertyChangeType::ArrayRemove)
	{
		// FIXME: REMOVE THE PIN!
		UpdatePreamble();
	}
}


void UOptimusNode_ComputeKernel::UpdatePinTypes(
	EOptimusNodePinDirection InPinDirection
	)
{
	TArray<FOptimusDataTypeHandle> DataTypes;

	if (InPinDirection == EOptimusNodePinDirection::Input)
	{
		for (const FOptimus_ShaderBinding& Binding: Parameters)
		{
			DataTypes.Add(Binding.DataType.Resolve());
		}
		for (const FOptimus_ShaderBinding& Binding: InputBindings)
		{
			DataTypes.Add(Binding.DataType.Resolve());
		}
	}
	else if (InPinDirection == EOptimusNodePinDirection::Output)
	{
		for (const FOptimus_ShaderBinding& Binding: OutputBindings)
		{
			DataTypes.Add(Binding.DataType.Resolve());
		}
	}	
	
	// Let's try and figure out which pin got changed.
	const TArray<UOptimusNodePin *> KernelPins = GetKernelPins(InPinDirection);

	if (ensure(DataTypes.Num() == KernelPins.Num()))
	{
		for (int32 Index = 0; Index < KernelPins.Num(); Index++)
		{
			if (KernelPins[Index]->GetDataType() != DataTypes[Index])
			{
				SetPinDataType(KernelPins[Index], DataTypes[Index]);
			}
		}
	}
}


void UOptimusNode_ComputeKernel::UpdatePinNames(
	EOptimusNodePinDirection InPinDirection
	)
{
	// FIXME: Duplicate names.
	TArray<FName> Names;

	if (InPinDirection == EOptimusNodePinDirection::Input)
	{
		for (const FOptimus_ShaderBinding& Binding: Parameters)
		{
			Names.Add(Binding.Name);
		}
		for (const FOptimus_ShaderBinding& Binding: InputBindings)
		{
			Names.Add(Binding.Name);
		}
	}
	else if (InPinDirection == EOptimusNodePinDirection::Output)
	{
		for (const FOptimus_ShaderBinding& Binding: OutputBindings)
		{
			Names.Add(Binding.Name);
		}
	}	
	
	// Let's try and figure out which pin got changed.
	TArray<UOptimusNodePin*> KernelPins = GetKernelPins(InPinDirection);

	if (ensure(Names.Num() == KernelPins.Num()))
	{
		for (int32 Index = 0; Index < KernelPins.Num(); Index++)
		{
			if (KernelPins[Index]->GetFName() != Names[Index])
			{
				SetPinName(KernelPins[Index], Names[Index]);
			}
		}
	}
}


void UOptimusNode_ComputeKernel::UpdatePreamble()
{
	TSet<FString> StructsSeen;
	TArray<FString> Structs;

	auto CollectStructs = [&StructsSeen, &Structs](const auto& BindingArray)
	{
		for (const FOptimus_ShaderBinding &Binding: BindingArray)
		{
			const FShaderValueType& ValueType = *Binding.DataType->ShaderValueType;
			if (ValueType.Type == EShaderFundamentalType::Struct)
			{
				const FString StructName = ValueType.ToString();
				if (!StructsSeen.Contains(StructName))
				{
					Structs.Add(ValueType.GetTypeDeclaration() + TEXT("\n\n"));
					StructsSeen.Add(StructName);
				}
			}
		}
	};

	CollectStructs(Parameters);
	CollectStructs(InputBindings);
	CollectStructs(OutputBindings);
	
	TArray<FString> Buffers;

	for (const FOptimus_ShaderBinding& Binding: Parameters)
	{
		Buffers.Add(FString::Printf(TEXT("KERNEL_PARAM(%s, %s)"),
			*Binding.DataType->ShaderValueType->ToString(), *Binding.Name.ToString()));
	}
	if (!Parameters.IsEmpty())
	{
		Buffers.AddDefaulted();
	}

	for (const FOptimus_ShaderInputResourceBinding& Binding: InputBindings)
	{
		FString IndexVar;
		if (Binding.bUsesIndex)
		{
			IndexVar = TEXT(", uint");
		}
		Buffers.Add(FString::Printf(TEXT("KERNEL_EXTERN_READ(%s, %s%s)"),
			*Binding.Name.ToString(), *Binding.DataType->ShaderValueType->ToString(), *IndexVar));
	}
	if (!InputBindings.IsEmpty())
	{
		Buffers.AddDefaulted();
	}
	for (const FOptimus_ShaderBinding& Binding: OutputBindings)
	{
		Buffers.Add(FString::Printf(TEXT("KERNEL_EXTERN_WRITE(%s, uint, %s)"),
			*Binding.Name.ToString(), *Binding.DataType->ShaderValueType->ToString()));
	}
	
	FString Declarations;

	if (!Structs.IsEmpty())
	{
		Declarations += TEXT("// Type declarations\n");
		Declarations += FString::Join(Structs, TEXT("\n")) + TEXT("\n");
	}
	if (!Buffers.IsEmpty())
	{
		Declarations += TEXT("// Kernel params and read/write buffers\n");
		Declarations += FString::Join(Buffers, TEXT("\n")) + TEXT("\n\n");
	}

	FString Name = KernelName;
	if (Name.IsEmpty())
	{
		Name = "ComputeShader";
	}
	else
	{
		Name.RemoveSpacesInline();
	}

	ShaderSource.ShaderPreamble = FString::Printf(TEXT(
		"#include \"/Engine/Private/Common.ush\"\n"
		"#include \"/Engine/Private/ComputeKernelCommon.ush\"\n\n"
	    "%s"	// Buffers + declarations
	    "[numthreads(%d, 1, 1)]\n"
	    "KERNEL_ENTRY_POINT(%s)\n"
	    "{"), *Declarations, InvocationCount, *Name);	
}


TArray<UOptimusNodePin*> UOptimusNode_ComputeKernel::GetKernelPins(
	EOptimusNodePinDirection InPinDirection
	) const
{
	TArray<UOptimusNodePin*> KernelPins;
	for (UOptimusNodePin* Pin : GetPins())
	{
		if (InPinDirection == EOptimusNodePinDirection::Unknown || Pin->GetDirection() == InPinDirection)
		{
			KernelPins.Add(Pin);
		}
	}

	return KernelPins;
}
