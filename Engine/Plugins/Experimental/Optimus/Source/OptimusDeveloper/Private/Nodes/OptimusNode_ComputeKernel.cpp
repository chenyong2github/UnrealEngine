// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusNode_ComputeKernel.h"

#include "OptimusComputeDataInterface.h"
#include "OptimusNodePin.h"
#include "OptimusHelpers.h"

#include "OptimusDataTypeRegistry.h"
#include "OptimusDeveloperModule.h"
#include "OptimusNodeGraph.h"
#include "DataInterfaces/DataInterfaceRawBuffer.h"


UOptimusNode_ComputeKernel::UOptimusNode_ComputeKernel()
{
	UpdatePreamble();
}


static void CopyValueType(FShaderValueTypeHandle InValueType,  FShaderParamTypeDefinition& OutParamDef)
{
	OutParamDef.ValueType = InValueType;
	OutParamDef.ArrayElementCount = 0;
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
	case EShaderFundamentalType::None:
		checkNoEntry();
		break;
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
	const FOptimus_NodeToDataInterfaceMap& InNodeDataInterfaceMap,
	const FOptimus_PinToDataInterfaceMap& InLinkDataInterfaceMap,
	const TSet<const UOptimusNode *>& InValueNodeSet,
	FOptimus_KernelParameterBindingList& OutParameterBindings,
	FOptimus_InterfaceBindingMap& OutInputDataBindings,
	FOptimus_InterfaceBindingMap& OutOutputDataBindings
	) const
{
	// TODO: This function is too big. Split up.
	
	UOptimusKernelSource* KernelSource = NewObject<UOptimusKernelSource>(InKernelSourceOuter, FName(*(KernelName + TEXT("_Src"))));

	FShaderParamTypeDefinition IndexParamDef;
	CopyValueType(FShaderValueType::Get(EShaderFundamentalType::Uint), IndexParamDef);
	
	// Figure out bindings for the pins.
	UOptimusNodeGraph *Graph = GetOwningGraph();

	TMap<EOptimusResourceContext, bool> SeenContexts;

	auto GetContextsForBinding = [](const UOptimusNodePin* Pin, const TArray<FOptimus_ShaderContextBinding>& Bindings)
	{
		for (const FOptimus_ShaderContextBinding& Binding: Bindings)
		{
			if (Binding.Name == Pin->GetFName())
			{
				return Binding.Contexts;
			}
		}
		check(false);
		return TArray<EOptimusResourceContext>{};
	};

	auto GetPinIndex = [](const UOptimusNodePin* InPin)
	{
		// FIXME: We're not handling sub-pins right now. 
		return InPin->GetNode()->GetPins().IndexOfByKey(InPin);
	};

	auto GetIndexNames = [](const TArray<EOptimusResourceContext> &Contexts)
	{
		TArray<FString> IndexNames;

		for (EOptimusResourceContext Context: Contexts)
		{
			if (Context != EOptimusResourceContext::Global)
			{
			FString ContextName = StaticEnum<EOptimusResourceContext>()->GetAuthoredNameStringByIndex(static_cast<int64>(Context));
			IndexNames.Add(FString::Printf(TEXT("%sIndex"), *ContextName));
		}
		}
		return IndexNames;
	};
	
	// Wrap functions for unconnected resource pins (or value pins) that return default values
	// (for reads) or do nothing (for writes).
	TArray<FString> GeneratedFunctions;

	for (const UOptimusNodePin* Pin: GetPins())
	{
		TArray<UOptimusNodePin *> ConnectedPins = Graph->GetConnectedPins(Pin);
		if (!ensure(Pin->GetDirection() == EOptimusNodePinDirection::Output || ConnectedPins.Num() <= 1))
		{
			continue;
		}
		
		const FShaderValueTypeHandle ValueType = Pin->GetDataType()->ShaderValueType;

		if (Pin->GetDirection() == EOptimusNodePinDirection::Input)
		{
			const UOptimusNodePin *ConnectedPin = ConnectedPins.IsEmpty() ? nullptr : ConnectedPins[0];
			const UOptimusNode *ConnectedNode = ConnectedPin ? ConnectedPin->GetNode() : nullptr;

			// For inputs, we only have to deal with a single read, because only one
			// link can connect into it. 
			if (ConnectedPin)
			{
				UOptimusComputeDataInterface* DataInterface = nullptr;
				int32 DataInterfaceFuncIndex = INDEX_NONE;
				FString DataFunctionName;
				
				// Are we being connected from a scene data interface or a transient buffer?
				if (InLinkDataInterfaceMap.Contains(ConnectedPin))
				{
					// For transient buffers we need the function index as given by the
					// ReadValue function. 
					DataInterface = InLinkDataInterfaceMap[ConnectedPin];
					DataInterfaceFuncIndex = UTransientBufferDataInterface::ReadValueInputIndex;

					TArray<FShaderFunctionDefinition> ReadFunctions;
					DataInterface->GetSupportedInputs(ReadFunctions);

					DataFunctionName = ReadFunctions[DataInterfaceFuncIndex].Name;
				}
				else if(InNodeDataInterfaceMap.Contains(ConnectedNode))
				{
					// FIXME: Sub-pin read support.
					DataInterface = InNodeDataInterfaceMap[ConnectedNode];

					TArray<FOptimusCDIPinDefinition> PinDefs = DataInterface->GetPinDefinitions();

					int32 DataInterfaceDefPinIndex = GetPinIndex(ConnectedPin); 
					DataFunctionName = PinDefs[DataInterfaceDefPinIndex].DataFunctionName;

					TArray<FShaderFunctionDefinition> ReadFunctions;
					DataInterface->GetSupportedInputs(ReadFunctions);
					DataInterfaceFuncIndex = ReadFunctions.IndexOfByPredicate([DataFunctionName](const FShaderFunctionDefinition &InDef) { return DataFunctionName == InDef.Name; });
				}
				else if (ensure(InValueNodeSet.Contains(ConnectedNode)))
				{
					FString ParameterName = TEXT("__") + Pin->GetName(); 
					
					FOptimus_KernelParameterBinding Binding;
					Binding.ValueNode = ConnectedNode;
					Binding.ParameterName = ParameterName;
					Binding.ValueType = Pin->GetDataType()->ShaderValueType;

					OutParameterBindings.Add(Binding);

					FShaderParamTypeDefinition ParameterDefinition;
					ParameterDefinition.Name = Binding.ParameterName;
					ParameterDefinition.ValueType = Binding.ValueType;
					ParameterDefinition.ResetTypeDeclaration();

					KernelSource->InputParams.Add(ParameterDefinition);

					GeneratedFunctions.Add(
						FString::Printf(TEXT("%s Read%s() { return %s; }"),
							*Binding.ValueType->ToString(), *Pin->GetName(), *Binding.ParameterName));
				}

				// If we are connected from a data interface, set the input binding up now.
				if (DataInterface)
				{
				// The shader function definition that exposes the function that we use to
				// read values to input into the kernel.
				FShaderFunctionDefinition FuncDef;
				FuncDef.Name = DataFunctionName;
				FuncDef.bHasReturnType = true;
				
				FShaderParamTypeDefinition ParamDef;
				CopyValueType(ValueType, ParamDef);
				FuncDef.ParamTypes.Emplace(ParamDef);

				// FIXME: Value connections from value nodes.

				// For resources we need the index parameter.
				if (Pin->GetStorageType() == EOptimusNodePinStorageType::Resource)
				{
					TArray<EOptimusResourceContext> Contexts = GetContextsForBinding(Pin, InputBindings);

					for (int32 Count = 0; Count < Contexts.Num(); Count++)
					{
						if (Contexts[Count] != EOptimusResourceContext::Global)
						{
						FuncDef.ParamTypes.Add(IndexParamDef);
					}
				}
				}

				FString WrapFunctionName = FString::Printf(TEXT("Read%s"), *Pin->GetName());
				OutInputDataBindings.Add(KernelSource->ExternalInputs.Num(), {DataInterface, DataInterfaceFuncIndex, WrapFunctionName});
				
				KernelSource->ExternalInputs.Emplace(FuncDef);
			}
			}
			else
			{
				// Nothing connected. Get the default value (for now).
				FString ValueStr;
				FString OptionalParamStr;
				if (Pin->GetStorageType() == EOptimusNodePinStorageType::Value)
				{
					ValueStr = GetShaderParamPinValueString(Pin);
				}
				else
				{
					TArray<EOptimusResourceContext> Contexts = GetContextsForBinding(Pin, InputBindings);
					
					ValueStr = GetShaderParamDefaultValueString(Pin->GetDataType());

					// No output connections, leave a stub function. The compiler will be in charge
					// of optimizing out anything that causes us to ends up here.
					TArray<FString> StubIndexes;
					
					for (const FString &IndexName: GetIndexNames(Contexts))
					{
						StubIndexes.Add(*FString::Printf(TEXT("uint %s"), *IndexName));
					}
				
					OptionalParamStr = *FString::Join(StubIndexes, TEXT(", "));
				}

				GeneratedFunctions.Add(
					FString::Printf(TEXT("%s Read%s(%s) { return %s; }"),
						*ValueType->ToString(), *Pin->GetName(), *OptionalParamStr, *ValueStr));
			}
		}
		else if (Pin->GetDirection() == EOptimusNodePinDirection::Output)
		{
			TArray<EOptimusResourceContext> Contexts = GetContextsForBinding(Pin, OutputBindings);
			
			TArray<FString> IndexNames = GetIndexNames(Contexts);

			if (!ConnectedPins.IsEmpty())
			{
				// If we have an output connection going to multiple data interfaces, then we
				// have to wrap them all up in a single proxy function to make it still transparent
				// to the kernel writer.
				struct FWriteConnectionDef
				{
					UOptimusComputeDataInterface* DataInterface = nullptr;
					FString DataFunctionName;
					FString WriteToName;
				};
				TArray<FWriteConnectionDef> WriteConnectionDefs;

				// If we're scheduled to write to a transient data interface, do that now.
				// There is only ever a single transient data interface per output pin.
				if (InLinkDataInterfaceMap.Contains(Pin))
				{
					UOptimusComputeDataInterface* DataInterface = InLinkDataInterfaceMap[Pin]; 

					TArray<FShaderFunctionDefinition> WriteFunctions;
					DataInterface->GetSupportedOutputs(WriteFunctions);

					WriteConnectionDefs.Add({DataInterface, WriteFunctions[UTransientBufferDataInterface::WriteValueOutputIndex].Name, TEXT("Transient")});
				}
				
				for (const UOptimusNodePin* ConnectedPin: ConnectedPins)
				{
					const UOptimusNode *ConnectedNode = ConnectedPin->GetNode();

					// Conneted to a data interface node?
					if(!InNodeDataInterfaceMap.Contains(ConnectedNode))
					{
						continue;
					}
					
					// FIXME: Sub-pin write support.
					UOptimusComputeDataInterface* DataInterface = InNodeDataInterfaceMap[ConnectedNode];
					int32 DataInterfaceDefPinIndex = GetPinIndex(ConnectedPin);
					TArray<FOptimusCDIPinDefinition> PinDefs = DataInterface->GetPinDefinitions();

					FString DataFunctionName = PinDefs[DataInterfaceDefPinIndex].DataFunctionName;
					
					WriteConnectionDefs.Add({DataInterface, DataFunctionName, ConnectedPin->GetName()});
				}

				TArray<FString> WrapFunctionNameCalls;

				for (const FWriteConnectionDef& WriteConnectionDef: WriteConnectionDefs)
				{
					const FString DataFunctionName = WriteConnectionDef.DataFunctionName;
					FShaderFunctionDefinition FuncDef;
					FuncDef.Name = DataFunctionName;
					FuncDef.bHasReturnType = false;
				
					FShaderParamTypeDefinition ParamDef;
					CopyValueType(ValueType, ParamDef);
					for (int32 Count = 0; Count < Contexts.Num(); Count++)
					{
						FuncDef.ParamTypes.Add(IndexParamDef);
					}
					FuncDef.ParamTypes.Emplace(ParamDef);

					TArray<FShaderFunctionDefinition> WriteFunctions;
					WriteConnectionDef.DataInterface->GetSupportedOutputs(WriteFunctions);
					int32 DataInterfaceFuncIndex = WriteFunctions.IndexOfByPredicate([DataFunctionName](const FShaderFunctionDefinition &InDef) { return DataFunctionName == InDef.Name; });
				
					FString WrapFunctionName;
					if (WriteConnectionDefs.Num() > 1)
					{
						WrapFunctionName = FString::Printf(TEXT("Write%sTo%s"), *Pin->GetName(), *WriteConnectionDef.WriteToName);
						WrapFunctionNameCalls.Add(FString::Printf(TEXT("    %s(%s, Value)"), *WrapFunctionName, *FString::Join(IndexNames, TEXT(", "))));
					}
					else
					{
						WrapFunctionName = FString::Printf(TEXT("Write%s"), *Pin->GetName());
					}
					OutOutputDataBindings.Add(KernelSource->ExternalOutputs.Num(), {WriteConnectionDef.DataInterface, DataInterfaceFuncIndex, WrapFunctionName});
					KernelSource->ExternalOutputs.Emplace(FuncDef);
				}

				if (!WrapFunctionNameCalls.IsEmpty())
				{
					TArray<FString> IndexParamNames;
					for (const FString& IndexName: IndexNames)
					{
						IndexParamNames.Add(FString::Printf(TEXT("uint %s"), *IndexName));
					}
					
					// Add a wrapper function that calls all the write functions in one shot.
					GeneratedFunctions.Add(
						FString::Printf(TEXT("void Write%s(%s, %s Value)\n{\n%s;\n}"),
							*Pin->GetName(), *FString::Join(IndexParamNames, TEXT(", ")), *ValueType->ToString(), *FString::Join(WrapFunctionNameCalls, TEXT(";\n"))));
				}
			}
			else
			{
				// No output connections, leave a stub function. The compiler will be in charge
				// of optimizing out anything that causes us to ends up here.
				TArray<FString> StubIndexes;

				for (const FString &IndexName: IndexNames)
				{
					StubIndexes.Add(*FString::Printf(TEXT("uint %s"), *IndexName));
				}
				
				GeneratedFunctions.Add(
					FString::Printf(TEXT("void Write%s(%s, %s) { }"),
						*Pin->GetName(), *FString::Join(StubIndexes, TEXT(", ")), *ValueType->ToString()));
			}
		}
	}

	FString WrappedSource = GetWrappedShaderSource();

	FString CookedSource;

	CookedSource =
		"#include \"/Engine/Private/Common.ush\"\n"
		"#include \"/Engine/Private/ComputeKernelCommon.ush\"\n\n";
	CookedSource += FString::Join(GeneratedFunctions, TEXT("\n"));
	CookedSource += "\n\n";
	CookedSource += WrappedSource;
	
	KernelSource->SetSourceAndEntryPoint(CookedSource, KernelName);

	// UE_LOG(LogOptimusDeveloper, Log, TEXT("Cooked source:\n%s\n"), *CookedSource);
	
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
		if (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(FOptimus_ShaderContextBinding, Contexts))
		{
			UpdatePinContextAndDimensionality(EOptimusNodePinDirection::Input);
			UpdatePreamble();
			return;
		}
		
		EOptimusNodePinDirection Direction = EOptimusNodePinDirection::Unknown;
		FOptimus_ShaderBinding *Binding = nullptr;
		FName Name;
		UOptimusNodePin *BeforePin = nullptr;
		FOptimusNodePinStorageConfig StorageConfig;

		if (BasePropertyName == ParametersName)
		{
			Direction = EOptimusNodePinDirection::Input;
			Binding = &Parameters.Last();
			Name = FName("Param");
			StorageConfig = {};

			if (!InputBindings.IsEmpty())
			{
				BeforePin = GetPins()[Parameters.Num() - 1];
			}
		}
		else if (BasePropertyName == InputBindingsName)
		{
			Direction = EOptimusNodePinDirection::Input;
			Binding = &InputBindings.Last();
			Name = FName("Input");

			// FIXME: Dimensionlity and context.
			StorageConfig = FOptimusNodePinStorageConfig(1, TEXT("Vertex"));
		}
		else if (BasePropertyName == OutputBindingsName)
		{
			Direction = EOptimusNodePinDirection::Output;
			Binding = &OutputBindings.Last();
			Name = FName("Output");

			StorageConfig = FOptimusNodePinStorageConfig(1, TEXT("Vertex"));
		}

		if (ensure(Binding))
		{
			Binding->Name = Optimus::GetUniqueNameForScopeAndClass(this, UOptimusNodePin::StaticClass(), Name);
			Binding->DataType = FOptimusDataTypeRegistry::Get().FindType(*FFloatProperty::StaticClass());

			AddPin(Binding->Name, Direction, StorageConfig, Binding->DataType, BeforePin);

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

void UOptimusNode_ComputeKernel::UpdatePinContextAndDimensionality(EOptimusNodePinDirection InPinDirection)
{
	TArray<int32> Dimensionalities;

	if (InPinDirection == EOptimusNodePinDirection::Input)
	{
		for (const FOptimus_ShaderBinding& Binding: Parameters)
		{
			Dimensionalities.Add(0);
		}
		for (const FOptimus_ShaderContextBinding& Binding: InputBindings)
		{
			Dimensionalities.Add(Binding.Contexts.Num());
		}
	}
	else if (InPinDirection == EOptimusNodePinDirection::Output)
	{
		for (const FOptimus_ShaderContextBinding& Binding: OutputBindings)
		{
			Dimensionalities.Add(Binding.Contexts.Num());
		}
	}	
	
	// Let's try and figure out which pin got changed.
	const TArray<UOptimusNodePin *> KernelPins = GetKernelPins(InPinDirection);

	if (ensure(Dimensionalities.Num() == KernelPins.Num()))
	{
		for (int32 Index = 0; Index < KernelPins.Num(); Index++)
		{
			if (KernelPins[Index]->GetResourceDimensionality() != Dimensionalities[Index])
			{
				SetPinContextAndDimensionality(KernelPins[Index], Dimensionalities[Index]);
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
	Bindings.Sort([](const FOptimus_ShaderContextBinding& A, const FOptimus_ShaderContextBinding &B) { return A.Contexts[0] < B.Contexts[0]; });
	for (const FOptimus_ShaderContextBinding& Binding: Bindings)
	{
		if (SeenContexts.Contains(Binding.Contexts[0]))
		{
			FString ContextName = StaticEnum<EOptimusResourceContext>()->GetAuthoredNameStringByIndex(static_cast<int64>(Binding.Contexts[0]));

			Declarations.Add(FString::Printf(TEXT("// Context %s"), *ContextName));
			Declarations.Add(FString::Printf(TEXT("uint Get%sCount();"), *ContextName));
			SeenContexts.Add(Binding.Contexts[0]);
		}

		TArray<FString> Indexes;
		for (const auto& Context: Binding.Contexts)
		{
			if (Context != EOptimusResourceContext::Global)
			{
			FString ContextName = StaticEnum<EOptimusResourceContext>()->GetAuthoredNameStringByIndex(static_cast<int64>(Context));
			Indexes.Add(FString::Printf(TEXT("uint %sIndex"), *ContextName));
		}
		}
		
		Declarations.Add(FString::Printf(TEXT("%s Read%s(%s);"),
			*Binding.DataType->ShaderValueType->ToString(), *Binding.Name.ToString(), *FString::Join(Indexes, TEXT(", "))));
	}

	Bindings = OutputBindings;
	Bindings.Sort([](const FOptimus_ShaderContextBinding& A, const FOptimus_ShaderContextBinding &B) { return A.Contexts[0] < B.Contexts[0]; });
	for (const FOptimus_ShaderContextBinding& Binding: Bindings)
	{
		if (SeenContexts.Contains(Binding.Contexts[0]))
		{
			FString ContextName = StaticEnum<EOptimusResourceContext>()->GetAuthoredNameStringByIndex(static_cast<int64>(Binding.Contexts[0]));

			Declarations.Add(FString::Printf(TEXT("// Context %s"), *ContextName));
			Declarations.Add(FString::Printf(TEXT("uint Get%sCount();"), *ContextName));
			SeenContexts.Add(Binding.Contexts[0]);
		}

		TArray<FString> Indexes;
		for (const auto& Context: Binding.Contexts)
		{
			FString ContextName = StaticEnum<EOptimusResourceContext>()->GetAuthoredNameStringByIndex(static_cast<int64>(Context));
			Indexes.Add(FString::Printf(TEXT("uint %sIndex"), *ContextName));
		}
		
		Declarations.Add(FString::Printf(TEXT("void Write%s(%s, %s Value);"),
			*Binding.Name.ToString(), *FString::Join(Indexes, TEXT(", ")), *Binding.DataType->ShaderValueType->ToString()));
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
	ShaderSource.Declarations += "\n// Resource Indexing\n";
	ShaderSource.Declarations += "uint Index;	// From SV_DispatchThreadID.x\n";
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
	// FIXME: Create source range mappings so that we can go from error location to
	// our source.
	FString Source = ShaderSource.ShaderText;

#if PLATFORM_WINDOWS
	// Remove old-school stuff.
	Source.ReplaceInline(TEXT("\r"), TEXT(""));
#endif

	const bool bHasKernelKeyword = Source.Contains(TEXT("KERNEL"));
	
	const FString KernelFunc = FString::Printf(TEXT("[numthreads(%d,1,1)]\nvoid %s(uint3 DTid : SV_DispatchThreadID)"), ThreadCount, *KernelName);
	
	if (bHasKernelKeyword)
	{
		Source.ReplaceInline(TEXT("KERNEL"), TEXT("void __kernel_func(uint Index)"));

		return FString::Printf(TEXT("#line 1 \"%s\"\n%s\n\n%s\n{\n    __kernel_func(DTid.x); \n}\n"), *GetName(), *Source, *KernelFunc);
	}
	else
	{
		TArray<FString> Lines;
		Source.ParseIntoArray(Lines, TEXT("\n"), false);
		
		for (FString& Line: Lines)
		{
			Line.InsertAt(0, TEXT("    "));
		}
	
		// FIXME: Handle presence of KERNEL {} keyword
		return FString::Printf(
		TEXT(
			"%s\n"
			"{\n"
			"   uint Index = DTid.x;\n"
			"#line 1 \"%s\"\n"
			"%s\n"
			"}\n"
			), *KernelFunc, *GetName(), *FString::Join(Lines, TEXT("\n")));
	}
}
