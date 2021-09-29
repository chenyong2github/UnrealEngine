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
	EnableDynamicPins();
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

// FIXME: This should be a a direct request from the pin 
static TArray<FName> GetContextsFomPin(
	const UOptimusNodePin* Pin,
	const TArray<FOptimus_ShaderContextBinding>& InBindings
	)
{
	for (const FOptimus_ShaderContextBinding& Binding: InBindings)
	{
		if (Binding.Name == Pin->GetFName())
		{
			return Binding.Context.ContextNames;
		}
	}
	check(false);
	return TArray<FName>{};
}

static TArray<FString> GetIndexNamesFromContextNames(
	const TArray<FName> &InContextNames
	)
{
	TArray<FString> IndexNames;

	for (FName ContextName: InContextNames)
	{
		IndexNames.Add(FString::Printf(TEXT("%sIndex"), *ContextName.ToString()));
	}
	return IndexNames;
}

// TODO: This belongs on the interface node. 
static int32 GetPinIndex(const UOptimusNodePin* InPin)
{
	return InPin->GetNode()->GetPins().IndexOfByKey(InPin);
}


void UOptimusNode_ComputeKernel::ProcessInputPinForComputeKernel(
	const UOptimusNodePin* InInputPin,
	const UOptimusNodePin* InOutputPin,
	const FOptimus_NodeToDataInterfaceMap& InNodeDataInterfaceMap,
	const FOptimus_PinToDataInterfaceMap& InLinkDataInterfaceMap,
	const TSet<const UOptimusNode*>& InValueNodeSet,
	UOptimusKernelSource* InKernelSource,
	TArray<FString>& OutGeneratedFunctions,
	FOptimus_KernelParameterBindingList& OutParameterBindings,
	FOptimus_InterfaceBindingMap& OutInputDataBindings
	) const
{
	FShaderParamTypeDefinition IndexParamDef;
	CopyValueType(FShaderValueType::Get(EShaderFundamentalType::Uint), IndexParamDef);

	const FShaderValueTypeHandle ValueType = InInputPin->GetDataType()->ShaderValueType;
	const UOptimusNode *OutputNode = InOutputPin ? InOutputPin->GetNode() : nullptr;

	// For inputs, we only have to deal with a single read, because only one
	// link can connect into it. 
	if (InOutputPin)
	{
		UOptimusComputeDataInterface* DataInterface = nullptr;
		int32 DataInterfaceFuncIndex = INDEX_NONE;
		FString DataFunctionName;
		
		// Are we being connected from a scene data interface or a transient buffer?
		if (InLinkDataInterfaceMap.Contains(InOutputPin))
		{
			// For transient buffers we need the function index as given by the
			// ReadValue function. 
			DataInterface = InLinkDataInterfaceMap[InOutputPin];
			DataInterfaceFuncIndex = UTransientBufferDataInterface::ReadValueInputIndex;

			TArray<FShaderFunctionDefinition> ReadFunctions;
			DataInterface->GetSupportedInputs(ReadFunctions);

			DataFunctionName = ReadFunctions[DataInterfaceFuncIndex].Name;
		}
		else if(InNodeDataInterfaceMap.Contains(OutputNode))
		{
			// FIXME: Sub-pin read support.
			DataInterface = InNodeDataInterfaceMap[OutputNode];

			TArray<FOptimusCDIPinDefinition> PinDefs = DataInterface->GetPinDefinitions();

			int32 DataInterfaceDefPinIndex = GetPinIndex(InOutputPin); 
			DataFunctionName = PinDefs[DataInterfaceDefPinIndex].DataFunctionName;

			TArray<FShaderFunctionDefinition> ReadFunctions;
			DataInterface->GetSupportedInputs(ReadFunctions);
			DataInterfaceFuncIndex = ReadFunctions.IndexOfByPredicate([DataFunctionName](const FShaderFunctionDefinition &InDef) { return DataFunctionName == InDef.Name; });
		}
		else if (ensure(InValueNodeSet.Contains(OutputNode)))
		{
			FString ParameterName = TEXT("__") + InInputPin->GetName(); 
			
			FOptimus_KernelParameterBinding Binding;
			Binding.ValueNode = OutputNode;
			Binding.ParameterName = ParameterName;
			Binding.ValueType = InInputPin->GetDataType()->ShaderValueType;

			OutParameterBindings.Add(Binding);

			FShaderParamTypeDefinition ParameterDefinition;
			ParameterDefinition.Name = Binding.ParameterName;
			ParameterDefinition.ValueType = Binding.ValueType;
			ParameterDefinition.ResetTypeDeclaration();

			InKernelSource->InputParams.Add(ParameterDefinition);

			OutGeneratedFunctions.Add(
				FString::Printf(TEXT("%s Read%s() { return %s; }"),
					*Binding.ValueType->ToString(), *InInputPin->GetName(), *Binding.ParameterName));
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

			// For resources we need the index parameter.
			if (InInputPin->GetStorageType() == EOptimusNodePinStorageType::Resource)
			{
				TArray<FName> Contexts = GetContextsFomPin(InInputPin, InputBindings);

				for (int32 Count = 0; Count < Contexts.Num(); Count++)
				{
					FuncDef.ParamTypes.Add(IndexParamDef);
				}
			}

			FString WrapFunctionName = FString::Printf(TEXT("Read%s"), *InInputPin->GetName());
			OutInputDataBindings.Add(InKernelSource->ExternalInputs.Num(), {DataInterface, DataInterfaceFuncIndex, WrapFunctionName});
			
			InKernelSource->ExternalInputs.Emplace(FuncDef);
		}
	}
	else
	{
		// Nothing connected. Get the default value (for now).
		FString ValueStr;
		FString OptionalParamStr;
		if (InInputPin->GetStorageType() == EOptimusNodePinStorageType::Value)
		{
			ValueStr = GetShaderParamPinValueString(InInputPin);
		}
		else
		{
			TArray<FName> Contexts = GetContextsFomPin(InInputPin, InputBindings);
			
			ValueStr = GetShaderParamDefaultValueString(InInputPin->GetDataType());

			// No output connections, leave a stub function. The compiler will be in charge
			// of optimizing out anything that causes us to ends up here.
			TArray<FString> StubIndexes;
			
			for (const FString &IndexName: GetIndexNamesFromContextNames(Contexts))
			{
				StubIndexes.Add(*FString::Printf(TEXT("uint %s"), *IndexName));
			}
		
			OptionalParamStr = *FString::Join(StubIndexes, TEXT(", "));
		}

		OutGeneratedFunctions.Add(
			FString::Printf(TEXT("%s Read%s(%s) { return %s; }"),
				*ValueType->ToString(), *InInputPin->GetName(), *OptionalParamStr, *ValueStr));
	}
}


void UOptimusNode_ComputeKernel::ProcessOutputPinForComputeKernel(
	const UOptimusNodePin* InOutputPin,
	const TArray<UOptimusNodePin *>& InInputPins,
	const FOptimus_NodeToDataInterfaceMap& InNodeDataInterfaceMap,
	const FOptimus_PinToDataInterfaceMap& InLinkDataInterfaceMap,
	UOptimusKernelSource* InKernelSource,
	TArray<FString>& OutGeneratedFunctions,
	FOptimus_InterfaceBindingMap& OutOutputDataBindings
	) const
{
	FShaderParamTypeDefinition IndexParamDef;
	CopyValueType(FShaderValueType::Get(EShaderFundamentalType::Uint), IndexParamDef);

	TArray<FName> Contexts = GetContextsFomPin(InOutputPin, OutputBindings);
	TArray<FString> IndexNames = GetIndexNamesFromContextNames(Contexts);
	const FShaderValueTypeHandle ValueType = InOutputPin->GetDataType()->ShaderValueType;

	if (!InInputPins.IsEmpty())
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
		if (InLinkDataInterfaceMap.Contains(InOutputPin))
		{
			UOptimusComputeDataInterface* DataInterface = InLinkDataInterfaceMap[InOutputPin]; 

			TArray<FShaderFunctionDefinition> WriteFunctions;
			DataInterface->GetSupportedOutputs(WriteFunctions);

			// This is a horrible hack for detecting interlocked writes
			// TODO: Either express this via the kernel metadata or add full support for buffer data interface in graph editor.
			int32 WriteValueOutputIndex = UTransientBufferDataInterface::WriteValueOutputIndex;
			if (InOutputPin->GetName().Contains(TEXT("Interlocked")) && WriteFunctions.Num() > WriteValueOutputIndex + 1)
			{
				++WriteValueOutputIndex;
			}

			WriteConnectionDefs.Add({DataInterface, WriteFunctions[WriteValueOutputIndex].Name, TEXT("Transient")});
		}
		
		for (const UOptimusNodePin* ConnectedPin: InInputPins)
		{
			const UOptimusNode *ConnectedNode = ConnectedPin->GetNode();

			// Connected to a data interface node?
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
				WrapFunctionName = FString::Printf(TEXT("Write%sTo%s"), *InOutputPin->GetName(), *WriteConnectionDef.WriteToName);
				WrapFunctionNameCalls.Add(FString::Printf(TEXT("    %s(%s, Value)"), *WrapFunctionName, *FString::Join(IndexNames, TEXT(", "))));
			}
			else
			{
				WrapFunctionName = FString::Printf(TEXT("Write%s"), *InOutputPin->GetName());
			}
			OutOutputDataBindings.Add(InKernelSource->ExternalOutputs.Num(), {WriteConnectionDef.DataInterface, DataInterfaceFuncIndex, WrapFunctionName});
			InKernelSource->ExternalOutputs.Emplace(FuncDef);
		}

		if (!WrapFunctionNameCalls.IsEmpty())
		{
			TArray<FString> IndexParamNames;
			for (const FString& IndexName: IndexNames)
			{
				IndexParamNames.Add(FString::Printf(TEXT("uint %s"), *IndexName));
			}
			
			// Add a wrapper function that calls all the write functions in one shot.
			OutGeneratedFunctions.Add(
				FString::Printf(TEXT("void Write%s(%s, %s Value)\n{\n%s;\n}"),
					*InOutputPin->GetName(), *FString::Join(IndexParamNames, TEXT(", ")), *ValueType->ToString(), *FString::Join(WrapFunctionNameCalls, TEXT(";\n"))));
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
		
		OutGeneratedFunctions.Add(
			FString::Printf(TEXT("void Write%s(%s, %s) { }"),
				*InOutputPin->GetName(), *FString::Join(StubIndexes, TEXT(", ")), *ValueType->ToString()));
	}
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
	UOptimusKernelSource* KernelSource = NewObject<UOptimusKernelSource>(InKernelSourceOuter);
	
	// Figure out bindings for the pins.
	const UOptimusNodeGraph *Graph = GetOwningGraph();

	TMap<FName, bool> SeenContexts;

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
		
		if (Pin->GetDirection() == EOptimusNodePinDirection::Input)
		{
			ProcessInputPinForComputeKernel(
				Pin, (ConnectedPins.IsEmpty() ? nullptr : ConnectedPins[0]),
				InNodeDataInterfaceMap, InLinkDataInterfaceMap, InValueNodeSet,
				KernelSource, GeneratedFunctions, OutParameterBindings, OutInputDataBindings
				);
		}
		else if (Pin->GetDirection() == EOptimusNodePinDirection::Output)
		{
			ProcessOutputPinForComputeKernel(
				Pin, ConnectedPins,
				InNodeDataInterfaceMap, InLinkDataInterfaceMap,
				KernelSource, GeneratedFunctions, OutOutputDataBindings);
		}
	}

	FString WrappedSource = GetWrappedShaderSource();

	FString CookedSource =
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
	Super::PostEditChangeProperty(PropertyChangedEvent);
	
	static const FName ParametersName = GET_MEMBER_NAME_STRING_CHECKED(UOptimusNode_ComputeKernel, Parameters);
	static const FName InputBindingsName = GET_MEMBER_NAME_STRING_CHECKED(UOptimusNode_ComputeKernel, InputBindings);
	static const FName OutputBindingsName = GET_MEMBER_NAME_STRING_CHECKED(UOptimusNode_ComputeKernel, OutputBindings);

	const FName BasePropertyName = (PropertyChangedEvent.MemberProperty ? PropertyChangedEvent.MemberProperty->GetFName() : NAME_None);
	const FName PropertyName = (PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None);

	if (PropertyChangedEvent.ChangeType & EPropertyChangeType::ValueSet)
	{
		if (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(UOptimusNode_ComputeKernel, KernelName))
		{
			SetDisplayName(FText::FromString(KernelName));
			UpdatePreamble();
		}
		else if (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(UOptimusNode_ComputeKernel, ThreadCount))
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
		else if (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(FOptimusNestedResourceContext, ContextNames))
		{
			if (BasePropertyName == ParametersName || BasePropertyName == InputBindingsName)
			{
				UpdatePinResourceContexts(EOptimusNodePinDirection::Input);
			}
			else if (BasePropertyName == OutputBindingsName)
			{
				UpdatePinResourceContexts(EOptimusNodePinDirection::Output);
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
			StorageConfig = FOptimusNodePinStorageConfig({Optimus::ContextName::Vertex});
		}
		else if (BasePropertyName == OutputBindingsName)
		{
			Direction = EOptimusNodePinDirection::Output;
			Binding = &OutputBindings.Last();
			Name = FName("Output");

			StorageConfig = FOptimusNodePinStorageConfig({Optimus::ContextName::Vertex});
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
		auto GetFilteredPins = [this](EOptimusNodePinDirection InDirection, EOptimusNodePinStorageType InStorageType)
		{
			TMap<FName, UOptimusNodePin *> FilteredPins;
			for (UOptimusNodePin *Pin: GetPins())
			{
				if (Pin->GetDirection() == InDirection && Pin->GetStorageType() == InStorageType)
				{
					FilteredPins.Add(Pin->GetFName(), Pin);
				}
			}
			return FilteredPins;
		};

		TMap<FName, UOptimusNodePin *> RemovedPins;
		if (BasePropertyName == ParametersName)
		{
			RemovedPins = GetFilteredPins(EOptimusNodePinDirection::Input, EOptimusNodePinStorageType::Value);
			
			for (const FOptimus_ShaderBinding& Binding: Parameters)
			{
				RemovedPins.Remove(Binding.Name);
			}
		}
		else if (BasePropertyName == InputBindingsName)
		{
			RemovedPins = GetFilteredPins(EOptimusNodePinDirection::Input, EOptimusNodePinStorageType::Resource);
			
			for (const FOptimus_ShaderBinding& Binding: InputBindings)
			{
				RemovedPins.Remove(Binding.Name);
			}
		}
		else if (BasePropertyName == OutputBindingsName)
		{
			RemovedPins = GetFilteredPins(EOptimusNodePinDirection::Output, EOptimusNodePinStorageType::Resource);
			
			for (const FOptimus_ShaderBinding& Binding: OutputBindings)
			{
				RemovedPins.Remove(Binding.Name);
			}
		}

		if (ensure(RemovedPins.Num() == 1))
		{
			RemovePin(RemovedPins.CreateIterator().Value());

			UpdatePreamble();
		}
	}
}


void UOptimusNode_ComputeKernel::ConstructNode()
{
	// After a duplicate, the kernel node has no pins, so we need to reconstruct them from
	// the bindings. We can assume that all naming clashes have already been dealt with.
	for (const FOptimus_ShaderBinding& Binding: Parameters)
	{
		AddPinDirect(Binding.Name, EOptimusNodePinDirection::Input, {}, Binding.DataType);
	}
	for (const FOptimus_ShaderContextBinding& Binding: InputBindings)
	{
		const FOptimusNodePinStorageConfig StorageConfig(Binding.Context.ContextNames);
		AddPinDirect(Binding.Name, EOptimusNodePinDirection::Input, StorageConfig, Binding.DataType);
	}
	for (const FOptimus_ShaderContextBinding& Binding: OutputBindings)
	{
		const FOptimusNodePinStorageConfig StorageConfig(Binding.Context.ContextNames);
		AddPinDirect(Binding.Name, EOptimusNodePinDirection::Output, StorageConfig, Binding.DataType);
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

void UOptimusNode_ComputeKernel::UpdatePinResourceContexts(EOptimusNodePinDirection InPinDirection)
{
	TArray<TArray<FName>> PinResourceContexts;

	if (InPinDirection == EOptimusNodePinDirection::Input)
	{
		for (const FOptimus_ShaderBinding& Binding: Parameters)
		{
			PinResourceContexts.Add({});
		}
		for (const FOptimus_ShaderContextBinding& Binding: InputBindings)
		{
			PinResourceContexts.Add(Binding.Context.ContextNames);
		}
	}
	else if (InPinDirection == EOptimusNodePinDirection::Output)
	{
		for (const FOptimus_ShaderContextBinding& Binding: OutputBindings)
		{
			PinResourceContexts.Add(Binding.Context.ContextNames);
		}
	}	
	
	// Let's try and figure out which pin got changed.
	const TArray<UOptimusNodePin *> KernelPins = GetKernelPins(InPinDirection);

	if (ensure(PinResourceContexts.Num() == KernelPins.Num()))
	{
		for (int32 Index = 0; Index < KernelPins.Num(); Index++)
		{
			SetPinResourceContexts(KernelPins[Index], PinResourceContexts[Index]);
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
	auto ContextsPredicate = [](const FOptimus_ShaderContextBinding& A, const FOptimus_ShaderContextBinding &B)
	{
		for (int32 Index = 0; Index < FMath::Min(A.Context.ContextNames.Num(), B.Context.ContextNames.Num()); Index++)
		{
			if (A.Context.ContextNames[Index] != B.Context.ContextNames[Index])
			{
				return FNameLexicalLess()(A.Context.ContextNames[Index], B.Context.ContextNames[Index]);
			}
		}
		return false;
	};
	
	TSet<TArray<FName>> SeenContexts;
	TArray<FOptimus_ShaderContextBinding> Bindings = InputBindings;
	Bindings.Sort(ContextsPredicate);

	auto AddCountFunctionIfNeeded = [&Declarations, &SeenContexts](const TArray<FName>& InContextNames)
	{
		if (!SeenContexts.Contains(InContextNames))
		{
			FString CountNameInfix;

			for (FName ContextName: InContextNames)
			{
				CountNameInfix.Append(ContextName.ToString());
			}
			Declarations.Add(FString::Printf(TEXT("uint Get%sCount();"), *CountNameInfix));
			SeenContexts.Add(InContextNames);
		}
	};
	
	for (const FOptimus_ShaderContextBinding& Binding: Bindings)
	{
		AddCountFunctionIfNeeded(Binding.Context.ContextNames);
		
		TArray<FString> Indexes;
		for (FString IndexName: GetIndexNamesFromContextNames(Binding.Context.ContextNames))
		{
			Indexes.Add(FString::Printf(TEXT("uint %s"), *IndexName));
		}
		
		Declarations.Add(FString::Printf(TEXT("%s Read%s(%s);"),
			*Binding.DataType->ShaderValueType->ToString(), *Binding.Name.ToString(), *FString::Join(Indexes, TEXT(", "))));
	}

	Bindings = OutputBindings;
	Bindings.Sort(ContextsPredicate);
	for (const FOptimus_ShaderContextBinding& Binding: Bindings)
	{
		AddCountFunctionIfNeeded(Binding.Context.ContextNames);
		
		TArray<FString> Indexes;
		for (FString IndexName: GetIndexNamesFromContextNames(Binding.Context.ContextNames))
		{
			Indexes.Add(FString::Printf(TEXT("uint %s"), *IndexName));
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

		return FString::Printf(
			TEXT(
				"#line 1 \"%s\"\n"
				"%s\n\n"
				"%s { __kernel_func(DTid.x); }\n"
				), *GetName(), *Source, *KernelFunc);
	}
	else
	{
		return FString::Printf(
		TEXT(
			"%s\n"
			"{\n"
			"uint Index = DTid.x;\n"
			"#line 1 \"%s\"\n"
			"%s\n"
			"}\n"
			), *KernelFunc, *GetName(), *Source);
	}
}
