// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusNode_ComputeKernelBase.h"

#include "OptimusNodeGraph.h"
#include "OptimusNodePin.h"

#include "OptimusKernelSource.h"
#include "DataInterfaces/DataInterfaceRawBuffer.h"


static FString GetShaderParamPinValueString(
	const UOptimusNodePin *InPin
	)
{
	return InPin->GetDataType()->ShaderValueType->GetZeroValueAsString();
	
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


static void CopyValueType(FShaderValueTypeHandle InValueType,  FShaderParamTypeDefinition& OutParamDef)
{
	OutParamDef.ValueType = InValueType;
	OutParamDef.ArrayElementCount = 0;
	OutParamDef.ResetTypeDeclaration();
}

// TODO: This belongs on the interface node. 
static int32 GetPinIndex(const UOptimusNodePin* InPin)
{
	return InPin->GetOwningNode()->GetPins().IndexOfByKey(InPin);
}

UOptimusKernelSource* UOptimusNode_ComputeKernelBase::CreateComputeKernel(
	UObject *InKernelSourceOuter,
	const FOptimusPinTraversalContext& InTraversalContext,
	const FOptimus_NodeToDataInterfaceMap& InNodeDataInterfaceMap,
	const FOptimus_PinToDataInterfaceMap& InLinkDataInterfaceMap,
	const TSet<const UOptimusNode *>& InValueNodeSet,
	FOptimus_KernelParameterBindingList& OutParameterBindings,
	FOptimus_InterfaceBindingMap& OutInputDataBindings, FOptimus_InterfaceBindingMap& OutOutputDataBindings
) const
{
	UOptimusKernelSource* KernelSource = NewObject<UOptimusKernelSource>(InKernelSourceOuter);

	// Wrap functions for unconnected resource pins (or value pins) that return default values
	// (for reads) or do nothing (for writes).
	TArray<FString> GeneratedFunctions;

	for (const UOptimusNodePin* Pin: GetPins())
	{
		TArray<UOptimusNodePin *> ConnectedPins;
		for (FOptimusRoutedNodePin ConnectedPin: Pin->GetConnectedPinsWithRouting(InTraversalContext))
		{
			ConnectedPins.Add(ConnectedPin.NodePin);
		}

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

	FString CookedSource =
		"#include \"/Engine/Private/Common.ush\"\n"
		"#include \"/Plugin/ComputeFramework/Private/ComputeKernelCommon.ush\"\n\n";
	CookedSource += FString::Join(GeneratedFunctions, TEXT("\n"));
	CookedSource += "\n\n";
	CookedSource += GetKernelSourceText();
	
	KernelSource->SetSourceAndEntryPoint(CookedSource, GetKernelName());

	// UE_LOG(LogOptimusCore, Log, TEXT("Cooked source:\n%s\n"), *CookedSource);
	
	return KernelSource;
}


FString UOptimusNode_ComputeKernelBase::GetCookedKernelSource(
	const FString& InShaderSource,
	const FString& InKernelName,
	int32 InThreadCount
	) const
{
	// FIXME: Create source range mappings so that we can go from error location to
	// our source.
	FString Source = InShaderSource;

#if PLATFORM_WINDOWS
	// Remove old-school stuff.
	Source.ReplaceInline(TEXT("\r"), TEXT(""));
#endif

	const bool bHasKernelKeyword = Source.Contains(TEXT("KERNEL"));
	
	const FString KernelFunc = FString::Printf(TEXT("[numthreads(%d,1,1)]\nvoid %s(uint3 DTid : SV_DispatchThreadID)"), InThreadCount, *InKernelName);
	
	if (bHasKernelKeyword)
	{
		Source.ReplaceInline(TEXT("KERNEL"), TEXT("void __kernel_func(uint Index)"));

		return FString::Printf(
			TEXT(
				"#line 1 \"%s\"\n"
				"%s\n\n"
				"%s { __kernel_func(DTid.x); }\n"
				), *GetPathName(), *Source, *KernelFunc);
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
			), *KernelFunc, *GetPathName(), *Source);
	}
}


void UOptimusNode_ComputeKernelBase::ProcessInputPinForComputeKernel(
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
	const UOptimusNode *OutputNode = InOutputPin ? InOutputPin->GetOwningNode() : nullptr;

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
				TArray<FName> LevelNames = InInputPin->GetDataDomainLevelNames();

				for (int32 Count = 0; Count < LevelNames.Num(); Count++)
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
			TArray<FName> LevelNames = InInputPin->GetDataDomainLevelNames();
			
			ValueStr = InInputPin->GetDataType()->ShaderValueType->GetZeroValueAsString();

			// No output connections, leave a stub function. The compiler will be in charge
			// of optimizing out anything that causes us to ends up here.
			TArray<FString> StubIndexes;
			
			for (const FString &IndexName: GetIndexNamesFromDataDomainLevels(LevelNames))
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


void UOptimusNode_ComputeKernelBase::ProcessOutputPinForComputeKernel(
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

	TArray<FName> LevelNames = InOutputPin->GetDataDomainLevelNames();
	TArray<FString> IndexNames = GetIndexNamesFromDataDomainLevels(LevelNames);
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
			const UOptimusNode *ConnectedNode = ConnectedPin->GetOwningNode();

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
			for (int32 Count = 0; Count < LevelNames.Num(); Count++)
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
