// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusNode_ComputeKernel.h"

#include "OptimusComputeDataInterface.h"
#include "OptimusNodePin.h"
#include "OptimusHelpers.h"

#include "OptimusDataTypeRegistry.h"
#include "OptimusDeveloperModule.h"
#include "OptimusNodeGraph.h"


UOptimusNode_ComputeKernel::UOptimusNode_ComputeKernel()
{
	UpdatePreamble();

}


static void CopyValueType(FShaderValueTypeHandle InValueType,  FShaderParamTypeDefinition& OutParamDef)
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
}


static FString GetShaderParamDefaultValueString(
	FOptimusDataTypeHandle InType
	)
{
	// FIXME: Move this to FShaderValueType
	const FShaderValueType& ValueType = *InType->ShaderValueType;

	FString FundamentalDefaultValue;
	switch(ValueType.Type)
	{
	case EShaderFundamentalType::Bool:
		FundamentalDefaultValue = TEXT("false");
		break;
	case EShaderFundamentalType::Int:
	case EShaderFundamentalType::Uint:
		FundamentalDefaultValue = TEXT("0");
		break;
	case EShaderFundamentalType::Float:
		FundamentalDefaultValue = TEXT("0.0f");
		break;
	case EShaderFundamentalType::Struct:
		checkf(ValueType.Type != EShaderFundamentalType::Struct, TEXT("Structs not supported yet."));
		break;
	}

	int32 ValueCount = 0;
	switch(ValueType.DimensionType)
	{
	case EShaderFundamentalDimensionType::Scalar:
		ValueCount = 1;
		break;
		
	case EShaderFundamentalDimensionType::Vector:
		ValueCount = ValueType.VectorElemCount;
		break;
		
	case EShaderFundamentalDimensionType::Matrix:
		ValueCount = ValueType.MatrixRowCount * ValueType.MatrixColumnCount;
		break;
	}

	TArray<FString> ValueArray;
	ValueArray.Init(FundamentalDefaultValue, ValueCount);

	return FString::Printf(TEXT("%s(%s)"), *ValueType.ToString(), *FString::Join(ValueArray, TEXT(", ")));
}


static FString GetShaderParamPinValueString(
	const UOptimusNodePin *InPin
	)
{
	return GetShaderParamDefaultValueString(InPin->GetDataType());
	
	// FIXME: Need property storage.
	const FShaderValueType& ValueType = *InPin->GetDataType()->ShaderValueType;
	const TArray<UOptimusNodePin*> &SubPins = InPin->GetSubPins();
	TArray<FString> ValueArray;
	if (SubPins.IsEmpty())
	{
		ValueArray.Add(InPin->GetValueAsString());
	}
	
	// FIXME: Support all types properly. Should probably be moved to a better place.
	return FString::Printf(TEXT("%s(%s)"), *ValueType.ToString(), *InPin->GetValueAsString());
}


UOptimusKernelSource* UOptimusNode_ComputeKernel::CreateComputeKernel(
	UObject *InKernelSourceOuter,
	const TMap<const UOptimusNode *, UOptimusComputeDataInterface *>& InNodeDataInterfaceMap,
	TMap<int32, TPair<UOptimusComputeDataInterface *, int32>>& OutInputDataBindings,
	TMap<int32, TPair<UOptimusComputeDataInterface *, int32>>& OutOutputDataBindings
	) const
{
	// FIXME: Add parameter map for unconnected parameters.
	
	UOptimusKernelSource* KernelSource = NewObject<UOptimusKernelSource>(InKernelSourceOuter, NAME_None);

	TArray<FString> BindingFunctions;

	FShaderParamTypeDefinition IndexParamDef;
	CopyValueType(FShaderValueType::Get(EShaderFundamentalType::Uint), IndexParamDef);
	
	// Figure out bindings for the pins.
	UOptimusNodeGraph *Graph = GetOwningGraph();

	TMap<EOptimusResourceContext, bool> SeenContexts;

	auto GetContextForBinding = [](const UOptimusNodePin* Pin, const TArray<FOptimus_ShaderContextBinding>& Bindings)
	{
		for (const FOptimus_ShaderContextBinding& Binding: Bindings)
		{
			if (Binding.Name == Pin->GetFName())
			{
				return Binding.Context;
			}
		}
		check(false);
		return EOptimusResourceContext::Vertex;
	};

	auto GetPinIndex = [](const UOptimusNodePin* InPin)
	{
		// FIXME: We're not handling sub-pins right now. 
		return InPin->GetNode()->GetPins().IndexOfByKey(InPin);
	};
	

	for (const UOptimusNodePin* Pin: GetPins())
	{
		TArray<UOptimusNodePin *> ConnectedPins = Graph->GetConnectedPins(Pin);
		check(ConnectedPins.Num() <= 1);
		const UOptimusNodePin *ConnectedPin = ConnectedPins.IsEmpty() ? nullptr : ConnectedPins[0];
		const UOptimusNode *ConnectedNode = ConnectedPin ? ConnectedPin->GetNode() : nullptr;

		const FShaderValueTypeHandle ValueType = Pin->GetDataType()->ShaderValueType;

		// FIXME: CONTEXT + MULTIPLE NODES
		if (ConnectedPin && ensure(InNodeDataInterfaceMap.Contains(ConnectedNode)))
		{
			UOptimusComputeDataInterface* DataInterface = InNodeDataInterfaceMap[ConnectedNode];

			// FIXME: Move this lookup to the UOptimusNode_DataInterface to ensure parity.
			int32 DataInterfaceDefPinIndex = GetPinIndex(ConnectedPin); 
			TArray<FOptimusCDIPinDefinition> PinDefs = DataInterface->GetPinDefinitions();

			if (Pin->GetDirection() == EOptimusNodePinDirection::Input)
			{
				FShaderFunctionDefinition FuncDef;

				FuncDef.Name = PinDefs[DataInterfaceDefPinIndex].DataFunctionName;
				FuncDef.bHasReturnType = true;
				
				FShaderParamTypeDefinition ParamDef;
				CopyValueType(ValueType, ParamDef);
				FuncDef.ParamTypes.Emplace(ParamDef);

				// FIXME: Value connections from value nodes.
				FString DataFunctionStr = PinDefs[DataInterfaceDefPinIndex].DataFunctionName;
				if (Pin->GetStorageType() == EOptimusNodePinStorageType::Value)
				{
					BindingFunctions.Add(
						FString::Printf(TEXT("%s Read%s() { return %s(); }"),
							*ValueType->ToString(), *Pin->GetName(), *PinDefs[DataInterfaceDefPinIndex].DataFunctionName));
				}
				else
				{
					BindingFunctions.Add(
						FString::Printf(TEXT("%s Read%s(uint Index) { return %s(Index); }"),
							*ValueType->ToString(), *Pin->GetName(), *PinDefs[DataInterfaceDefPinIndex].DataFunctionName));
					FuncDef.ParamTypes.Add(IndexParamDef);
				}

				TArray<FShaderFunctionDefinition> ReadFunctions;
				DataInterface->GetSupportedInputs(ReadFunctions);
				int32 DataInterfaceFuncIndex = ReadFunctions.IndexOfByPredicate([DataFunctionStr](const FShaderFunctionDefinition &InDef) { return DataFunctionStr == InDef.Name; });

				OutInputDataBindings.Add(KernelSource->ExternalInputs.Num(), MakeTuple(DataInterface, DataInterfaceFuncIndex));
				
				KernelSource->ExternalInputs.Emplace(FuncDef);
			}
			else if (Pin->GetDirection() == EOptimusNodePinDirection::Output)
			{
				FString DataFunctionStr = PinDefs[DataInterfaceDefPinIndex].DataFunctionName;
				
				BindingFunctions.Add(
					FString::Printf(TEXT("void Write%s(uint Index, %s Value) { %s(Index, Value); }"),
						*Pin->GetName(), *ValueType->ToString(), *DataFunctionStr));
				
				FShaderFunctionDefinition FuncDef;

				FuncDef.Name = PinDefs[DataInterfaceDefPinIndex].DataFunctionName;
				FuncDef.bHasReturnType = false;
				
				FShaderParamTypeDefinition ParamDef;
				CopyValueType(ValueType, ParamDef);
				FuncDef.ParamTypes.Add(IndexParamDef);
				FuncDef.ParamTypes.Emplace(ParamDef);

				TArray<FShaderFunctionDefinition> WriteFunctions;
				DataInterface->GetSupportedOutputs(WriteFunctions);
				int32 DataInterfaceFuncIndex = WriteFunctions.IndexOfByPredicate([DataFunctionStr](const FShaderFunctionDefinition &InDef) { return DataFunctionStr == InDef.Name; });
				
				OutOutputDataBindings.Add(KernelSource->ExternalOutputs.Num(), MakeTuple(DataInterface, DataInterfaceFuncIndex));
				
				KernelSource->ExternalOutputs.Emplace(FuncDef);
			}
		}
		else
		{
			if (Pin->GetDirection() == EOptimusNodePinDirection::Input)
			{
				FString ValueStr;
				FString OptionalParamStr;
				if (Pin->GetStorageType() == EOptimusNodePinStorageType::Value)
				{
					ValueStr = GetShaderParamPinValueString(Pin);
				}
				else
				{
					ValueStr = GetShaderParamDefaultValueString(Pin->GetDataType());
					OptionalParamStr = "uint Index";
				}

				BindingFunctions.Add(
					FString::Printf(TEXT("%s Read%s(%s) { return %s; }"),
						*ValueType->ToString(), *Pin->GetName(), *OptionalParamStr, *ValueStr));
			}
		}
	}

	FString WrappedSource = GetWrappedShaderSource();

	FString CookedSource;

	CookedSource =
		"#include \"/Engine/Private/Common.ush\"\n"
		"#include \"/Engine/Private/ComputeKernelCommon.ush\"\n\n";
	CookedSource += FString::Join(BindingFunctions, TEXT("\n"));
	CookedSource += "\n\n";
	CookedSource += WrappedSource;
	
	KernelSource->SetSourceAndEntryPoint(CookedSource, KernelName);

	UE_LOG(LogOptimusDeveloper, Display, TEXT("Cooked Source: %s"), *CookedSource);
	
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
		    PropertyName == GET_MEMBER_NAME_STRING_CHECKED(UOptimusNode_ComputeKernel, ThreadCount))
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
		FName Name;
		UOptimusNodePin *BeforePin = nullptr;
		EOptimusNodePinStorageType StorageType = EOptimusNodePinStorageType::Resource;

		if (BasePropertyName == ParametersName)
		{
			Direction = EOptimusNodePinDirection::Input;
			Binding = &Parameters.Last();
			Name = FName("Param");
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
			Name = FName("Input");
		}
		else if (BasePropertyName == OutputBindingsName)
		{
			Direction = EOptimusNodePinDirection::Output;
			Binding = &OutputBindings.Last();
			Name = FName("Output");
		}

		if (ensure(Binding))
		{
			Binding->Name = Optimus::GetUniqueNameForScopeAndClass(this, UOptimusNodePin::StaticClass(), Name);
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

	bool bNameChanged = false;
	if (ensure(Names.Num() == KernelPins.Num()))
	{
		for (int32 Index = 0; Index < KernelPins.Num(); Index++)
		{
			if (KernelPins[Index]->GetFName() != Names[Index])
			{
				FName NewName = Optimus::GetUniqueNameForScopeAndClass(this, UOptimusNodePin::StaticClass(), Names[Index]);

				SetPinName(KernelPins[Index], NewName);

				if (NewName != Names[Index])
				{
					bNameChanged = true;  
				}
			}
		}
	}

	if (bNameChanged)
	{
		if (InPinDirection == EOptimusNodePinDirection::Input)
		{
			for (int32 Index = 0; Index < Names.Num(); Index++)
			{
				if (Index < Parameters.Num())
				{
					Parameters[Index].Name = Names[Index];
				}
				else
				{
					InputBindings[Index - Parameters.Num()].Name = Names[Index];
				}
			}
		}
		else if (InPinDirection == EOptimusNodePinDirection::Output)
		{
			for (int32 Index = 0; Index < Names.Num(); Index++)
			{
				OutputBindings[Index].Name = Names[Index];
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
	
	TArray<FString> Declarations;

	for (const FOptimus_ShaderBinding& Binding: Parameters)
	{
		Declarations.Add(FString::Printf(TEXT("%s Read%s();"),
			*Binding.DataType->ShaderValueType->ToString(), *Binding.Name.ToString()));
	}
	if (!Parameters.IsEmpty())
	{
		Declarations.AddDefaulted();
	}

	// FIXME: Lump input/output functions together into single context.
	TSet<EOptimusResourceContext> SeenContexts;
	TArray<FOptimus_ShaderContextBinding> Bindings = InputBindings;
	Bindings.Sort([](const FOptimus_ShaderContextBinding& A, const FOptimus_ShaderContextBinding &B) { return A.Context < B.Context; });
	for (const FOptimus_ShaderContextBinding& Binding: Bindings)
	{
		if (SeenContexts.Contains(Binding.Context))
		{
			FString ContextName = StaticEnum<EOptimusResourceContext>()->GetAuthoredNameStringByIndex(static_cast<int64>(Binding.Context));

			Declarations.Add(FString::Printf(TEXT("// Context %s"), *ContextName));
			Declarations.Add(FString::Printf(TEXT("uint Get%sCount();"), *ContextName));
			SeenContexts.Add(Binding.Context);
		}
		
		Declarations.Add(FString::Printf(TEXT("%s Read%s(uint Index);"),
			*Binding.DataType->ShaderValueType->ToString(), *Binding.Name.ToString()));
	}

	Bindings = OutputBindings;
	Bindings.Sort([](const FOptimus_ShaderContextBinding& A, const FOptimus_ShaderContextBinding &B) { return A.Context < B.Context; });
	for (const FOptimus_ShaderContextBinding& Binding: Bindings)
	{
		if (SeenContexts.Contains(Binding.Context))
		{
			FString ContextName = StaticEnum<EOptimusResourceContext>()->GetAuthoredNameStringByIndex(static_cast<int64>(Binding.Context));

			Declarations.Add(FString::Printf(TEXT("// Context %s"), *ContextName));
			Declarations.Add(FString::Printf(TEXT("uint Get%sCount();"), *ContextName));
			SeenContexts.Add(Binding.Context);
		}
		
		Declarations.Add(FString::Printf(TEXT("void Write%s(uint Index, %s Value);"),
			*Binding.Name.ToString(), *Binding.DataType->ShaderValueType->ToString()));
	}

	ShaderSource.Declarations.Reset();
	if (!Structs.IsEmpty())
	{
		ShaderSource.Declarations += TEXT("// Type declarations\n");
		ShaderSource.Declarations += FString::Join(Structs, TEXT("\n")) + TEXT("\n");
	}
	if (!Declarations.IsEmpty())
	{
		ShaderSource.Declarations += TEXT("// Parameters and resource read/write functions\n");
		ShaderSource.Declarations += FString::Join(Declarations, TEXT("\n"));
	}
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


FString UOptimusNode_ComputeKernel::GetWrappedShaderSource() const
{
	// FIXME: Handle presence of KERNEL {} keyword
	return FString::Printf(
	TEXT(
		"[numthreads(%d,1,1)]\n"
		"void %s(uint3 DTid : SV_DispatchThreadID)\n"
		"{\n"
		"   uint Index = DTid.x;\n"
		"%s\n"
		"}\n"
		), ThreadCount, *KernelName, *ShaderSource.ShaderText);
}
