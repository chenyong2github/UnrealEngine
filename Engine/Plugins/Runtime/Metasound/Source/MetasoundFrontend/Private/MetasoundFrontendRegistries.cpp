// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundFrontendRegistries.h"
#include "CoreMinimal.h"
#include "Misc/ScopeLock.h"
#include "HAL/PlatformTime.h"

#ifndef WITH_METASOUND_FRONTEND
#define WITH_METASOUND_FRONTEND 0
#endif

FMetasoundFrontendRegistryContainer::FMetasoundFrontendRegistryContainer()
	: bHasModuleBeenInitialized(false)
{
}

FMetasoundFrontendRegistryContainer::FNodeRegistryKey FMetasoundFrontendRegistryContainer::GetRegistryKeyForNodeInternal(const Metasound::INode& InNode)
{
	using FInputVertexInterface = ::Metasound::FInputVertexInterface;
	using FOutputVertexInterface = ::Metasound::FOutputVertexInterface;

	const FName NodeName = InNode.GetClassName();
	const FInputVertexInterface& Inputs = InNode.GetDefaultVertexInterface().GetInputInterface();
	const FOutputVertexInterface& Outputs = InNode.GetDefaultVertexInterface().GetOutputInterface();

	// Construct a hash using a combination of the class name, input names and output names.
	uint32 NodeHash = FCrc::StrCrc32(*NodeName.ToString());

	for (auto& InputTuple : Inputs)
	{
		NodeHash = HashCombine(NodeHash, FCrc::StrCrc32(*InputTuple.Value.GetVertexTypeName().ToString()));
	}

	for (auto& OutputTuple : Outputs)
	{
		NodeHash = HashCombine(NodeHash, FCrc::StrCrc32(*OutputTuple.Value.GetVertexTypeName().ToString()));
	}

	return { NodeName, NodeHash };
}

FMetasoundFrontendRegistryContainer* FMetasoundFrontendRegistryContainer::LazySingleton = nullptr;

FMetasoundFrontendRegistryContainer* FMetasoundFrontendRegistryContainer::Get()
{
	if (!LazySingleton)
	{
		LazySingleton = new FMetasoundFrontendRegistryContainer();
	}

	return LazySingleton;
}

void FMetasoundFrontendRegistryContainer::ShutdownMetasoundFrontend()
{
	if (LazySingleton)
	{
		delete LazySingleton;
		LazySingleton = nullptr;
	}
}

void FMetasoundFrontendRegistryContainer::InitializeFrontend()
{
	FScopeLock ScopeLock(&LazyInitCommandCritSection);

	if (bHasModuleBeenInitialized)
	{
		// this function should only be called once.
		checkNoEntry();
	}

	UE_LOG(LogTemp, Display, TEXT("Initializing Metasounds Frontend."));
	uint64 CurrentTime = FPlatformTime::Cycles64();

	for (TUniqueFunction<void()>& Command : LazyInitCommands)
	{
		Command();
	}

	LazyInitCommands.Empty();
	bHasModuleBeenInitialized = true;

	uint64 CyclesUsed = FPlatformTime::Cycles64() - CurrentTime;
	UE_LOG(LogTemp, Display, TEXT("Initializing Metasounds Frontend took %f seconds."), FPlatformTime::ToSeconds64(CyclesUsed));
}

bool FMetasoundFrontendRegistryContainer::EnqueueInitCommand(TUniqueFunction<void()>&& InFunc)
{
	// if the module has been initalized already, we can safely call this function now.
	if (bHasModuleBeenInitialized)
	{
		InFunc();
	}

	// otherwise, we enqueue the function to be executed after the frontend module has been initialized.

	if (LazyInitCommands.Num() >= MaxNumNodesAndDatatypesToInitialize)
	{
		UE_LOG(LogTemp, Warning, TEXT("Registering more that %d nodes and datatypes for metasounds! Consider increasing MetasoundFrontendRegistryContainer::MaxNumNodesAndDatatypesToInitialize."));
	}

	LazyInitCommands.Add(MoveTemp(InFunc));
	return true;
}

TMap<FMetasoundFrontendRegistryContainer::FNodeRegistryKey, FMetasoundFrontendRegistryContainer::FNodeRegistryElement>& FMetasoundFrontendRegistryContainer::GetExternalNodeRegistry()
{
	return ExternalNodeRegistry;
}

TUniquePtr<Metasound::INode> FMetasoundFrontendRegistryContainer::ConstructInputNode(const FName& InInputType, Metasound::FInputNodeConstructorParams&& InParams)
{
	if (ensureAlwaysMsgf(DataTypeRegistry.Contains(InInputType), TEXT("Couldn't find data type %s!"), *InInputType.ToString()))
	{
		return DataTypeRegistry[InInputType].Callbacks.InputNodeConstructor(MoveTemp(InParams));
	}
	else
	{
		return nullptr;
	}
}

TUniquePtr<Metasound::INode> FMetasoundFrontendRegistryContainer::ConstructOutputNode(const FName& InOutputType, const Metasound::FOutputNodeConstrutorParams& InParams)
{
	if (ensureAlwaysMsgf(DataTypeRegistry.Contains(InOutputType), TEXT("Couldn't find data type %s!"), *InOutputType.ToString()))
	{
		return DataTypeRegistry[InOutputType].Callbacks.OutputNodeConstructor(InParams);
	}
	else
	{
		return nullptr;
	}
}

Metasound::FDataTypeLiteralParam FMetasoundFrontendRegistryContainer::GenerateLiteralForUObject(const FName& InDataType, UObject* InObject)
{
	if (ensureAlwaysMsgf(DataTypeRegistry.Contains(InDataType), TEXT("Couldn't find data type %s!"), *InDataType.ToString()))
	{
		 Audio::IProxyDataPtr ProxyPtr = DataTypeRegistry[InDataType].Callbacks.ProxyConstructor(InObject);
		 if (ensureAlwaysMsgf(ProxyPtr.IsValid(), TEXT("UObject failed to create a valid proxy!")))
		 {
			 return Metasound::FDataTypeLiteralParam(MoveTemp(ProxyPtr));
		 }
		 else
		 {
			 return Metasound::FDataTypeLiteralParam();
		 }
	}
	else
	{
		return Metasound::FDataTypeLiteralParam();
	}
}

Metasound::FDataTypeLiteralParam FMetasoundFrontendRegistryContainer::GenerateLiteralForUObjectArray(const FName& InDataType, TArray<UObject*> InObjectArray)
{
	if (ensureAlwaysMsgf(DataTypeRegistry.Contains(InDataType), TEXT("Couldn't find data type %s!"), *InDataType.ToString()))
	{
		TArray<Audio::IProxyDataPtr> ProxyArray;

		for (UObject* InObject : InObjectArray)
		{
			if (InObject)
			{
				Audio::IProxyDataPtr ProxyPtr = DataTypeRegistry[InDataType].Callbacks.ProxyConstructor(InObject);
				ensureAlwaysMsgf(ProxyPtr.IsValid(), TEXT("UObject failed to create a valid proxy!"));
				ProxyArray.Add(MoveTemp(ProxyPtr));
			}
		}

		return Metasound::FDataTypeLiteralParam(MoveTemp(ProxyArray));
	}
	else
	{
		return Metasound::FDataTypeLiteralParam();
	}
}

TUniquePtr<Metasound::INode> FMetasoundFrontendRegistryContainer::ConstructExternalNode(const FName& InNodeType, uint32 InNodeHash, const Metasound::FNodeInitData& InInitData)
{
	Metasound::Frontend::FNodeRegistryKey RegistryKey;
	RegistryKey.NodeName = InNodeType;
	RegistryKey.NodeHash = InNodeHash;

	if (!ExternalNodeRegistry.Contains(RegistryKey))
	{
		return nullptr;
	}
	else
	{
		return ExternalNodeRegistry[RegistryKey].GetterCallback(InInitData);
	}
}

TArray<::Metasound::Frontend::FConverterNodeInfo> FMetasoundFrontendRegistryContainer::GetPossibleConverterNodes(const FName& FromDataType, const FName& ToDataType)
{
	FConverterNodeRegistryKey InKey = { FromDataType, ToDataType };
	if (!ConverterNodeRegistry.Contains(InKey))
	{
		return TArray<FConverterNodeInfo>();
	}
	else
	{
		return ConverterNodeRegistry[InKey].PotentialConverterNodes;
	}
}

Metasound::ELiteralArgType FMetasoundFrontendRegistryContainer::GetDesiredLiteralTypeForDataType(FName InDataType) const
{
	if (!DataTypeRegistry.Contains(InDataType))
	{
		return Metasound::ELiteralArgType::Invalid;
	}

	const FDataTypeRegistryElement& DataTypeInfo = DataTypeRegistry[InDataType];
	
	// If there's a designated preferred literal type for this datatype, use that.
	if (DataTypeInfo.Info.PreferredLiteralType != Metasound::ELiteralArgType::None)
	{
		return DataTypeInfo.Info.PreferredLiteralType;
	}

	// Otherwise, we opt for the highest precision construction option available.
	if (DataTypeInfo.Info.bIsStringParsable)
	{
		return Metasound::ELiteralArgType::String;
	}
	else if (DataTypeInfo.Info.bIsFloatParsable)
	{
		return Metasound::ELiteralArgType::Float;
	}
	else if (DataTypeInfo.Info.bIsIntParsable)
	{
		return Metasound::ELiteralArgType::Integer;
	}
	else if (DataTypeInfo.Info.bIsBoolParsable)
	{
		return Metasound::ELiteralArgType::Boolean;
	}
	else if (DataTypeInfo.Info.bIsDefaultParsable)
	{
		return Metasound::ELiteralArgType::None;
	}
	else
	{
		// if we ever hit this, something has gone terribly wrong with the REGISTER_METASOUND_DATATYPE macro.
		// we should have failed to compile if any of these are false.
		checkNoEntry();
		return Metasound::ELiteralArgType::Invalid;
	}
}

UClass* FMetasoundFrontendRegistryContainer::GetLiteralUClassForDataType(FName InDataType) const
{
	if (!DataTypeRegistry.Contains(InDataType))
	{
		ensureAlwaysMsgf(false, TEXT("couldn't find DataType %s in the registry."), *InDataType.ToString());
		return nullptr;
	}
	else
	{
		return DataTypeRegistry[InDataType].Info.ProxyGeneratorClass;
	}
}

bool FMetasoundFrontendRegistryContainer::DoesDataTypeSupportLiteralType(FName InDataType, Metasound::ELiteralArgType InLiteralType) const
{
	if (!DataTypeRegistry.Contains(InDataType))
	{
		ensureAlwaysMsgf(false, TEXT("couldn't find DataType %s in the registry."), *InDataType.ToString());
		return false;
	}

	const FDataTypeRegistryElement& DataTypeInfo = DataTypeRegistry[InDataType];
	
	switch (InLiteralType)
	{
		case Metasound::ELiteralArgType::Boolean:
		{
			return DataTypeInfo.Info.bIsBoolParsable;
		}
		case Metasound::ELiteralArgType::Integer:
		{
			return DataTypeInfo.Info.bIsIntParsable;
		}
		case Metasound::ELiteralArgType::Float:
		{
			return DataTypeInfo.Info.bIsFloatParsable;
		}
		case Metasound::ELiteralArgType::String:
		{
			return DataTypeInfo.Info.bIsStringParsable;
		}
		case Metasound::ELiteralArgType::UObjectProxy:
		{
			return DataTypeInfo.Info.bIsProxyParsable;
		}
		case Metasound::ELiteralArgType::UObjectProxyArray:
		{
			return DataTypeInfo.Info.bIsProxyArrayParsable;
		}
		case Metasound::ELiteralArgType::None:
		{
			return DataTypeInfo.Info.bIsDefaultParsable;
		}
		case Metasound::ELiteralArgType::Invalid:
		default:
		{
			return false;
		}
	}
}

bool FMetasoundFrontendRegistryContainer::RegisterDataType(const ::Metasound::FDataTypeRegistryInfo& InDataInfo, ::Metasound::FDataTypeConstructorCallbacks&& InCallbacks)
{
	if (!ensureAlwaysMsgf(!DataTypeRegistry.Contains(InDataInfo.DataTypeName), TEXT("Name collision when trying to register Metasound Data Type %s! Make sure that you created a unique name for your data type, and that REGISTER_METASOUND_DATATYPE isn't called in a public header."), *InDataInfo.DataTypeName.ToString()))
	{
		// todo: capture callstack for previous declaration for non-shipping builds to help clarify who already registered this name for a type.
		return false;
	}
	else
	{
		FDataTypeRegistryElement InElement = { MoveTemp(InCallbacks), InDataInfo };
		DataTypeRegistry.Add(InDataInfo.DataTypeName, MoveTemp(InElement));
		UE_LOG(LogTemp, Display, TEXT("Registered Metasound Datatype %s."), *InDataInfo.DataTypeName.ToString());
		return true;
	}
}

bool FMetasoundFrontendRegistryContainer::RegisterExternalNode(FNodeGetterCallback&& InCallback)
{
	using FVertexInterface = Metasound::FVertexInterface;
	using FInputVertexInterface = Metasound::FInputVertexInterface;
	using FOutputVertexInterface = Metasound::FOutputVertexInterface;

	Metasound::FNodeInitData DummyInitData;
	TUniquePtr<Metasound::INode> DummyNodePtr = InCallback(DummyInitData);
	if (!ensureAlwaysMsgf(DummyNodePtr.IsValid(), TEXT("Invalid getter registered!")))
	{
		return false;
	}

	Metasound::INode& DummyNode = *DummyNodePtr;

	FNodeRegistryKey InKey = GetRegistryKeyForNodeInternal(DummyNode);

	const FName NodeName = DummyNode.GetClassName();

	// check to see if an identical node was already registered, and log
	ensureAlwaysMsgf(!ExternalNodeRegistry.Contains(InKey), TEXT("Node with identical name, inputs and outputs to node %s was already registered. The previously registered node will be overwritten. This could also happen because METASOUND_REGISTER_NODE is in a public header."), *NodeName.ToString());

	UE_LOG(LogTemp, Display, TEXT("Registered Metasound Node %s"), *NodeName.ToString());

	using FInputVertexInterface = ::Metasound::FInputVertexInterface;
	using FOutputVertexInterface = ::Metasound::FOutputVertexInterface;

	const FInputVertexInterface& Inputs = DummyNode.GetDefaultVertexInterface().GetInputInterface();
	const FOutputVertexInterface& Outputs = DummyNode.GetDefaultVertexInterface().GetOutputInterface();

	const bool bShouldLogInputsAndOutputs = true;
	if (bShouldLogInputsAndOutputs)
	{
		UE_LOG(LogTemp, Display, TEXT("    %d inputs:"), Inputs.Num());
		for (auto& InputTuple : Inputs)
		{
			UE_LOG(LogTemp, Display, TEXT("      %s (of type %s)"), *InputTuple.Value.GetVertexName(), *InputTuple.Value.GetVertexTypeName().ToString());
		}

		UE_LOG(LogTemp, Display, TEXT("    %d outputs:"), Outputs.Num());
		for (auto& OutputTuple : Outputs)
		{
			UE_LOG(LogTemp, Display, TEXT("      %s (of type %s)"), *OutputTuple.Value.GetVertexName(), *OutputTuple.Value.GetVertexTypeName().ToString());
		}
	}

	TArray<FName> InputTypes;
	TArray<FName> OutputTypes;

	for (auto& InputTuple : Inputs)
	{
		InputTypes.Add(InputTuple.Value.GetVertexTypeName());
	}

	for (auto& OutputTuple : Outputs)
	{
		OutputTypes.Add(OutputTuple.Value.GetVertexTypeName());
	}

	FNodeRegistryElement RegistryElement = FNodeRegistryElement(MoveTemp(InCallback));
	RegistryElement.InputTypes = MoveTemp(InputTypes);
	RegistryElement.OutputTypes = MoveTemp(OutputTypes);

	ExternalNodeRegistry.Add(MoveTemp(InKey), MoveTemp(RegistryElement));

	return true;
}

bool FMetasoundFrontendRegistryContainer::RegisterConversionNode(const FConverterNodeRegistryKey& InNodeKey, const FConverterNodeInfo& InNodeInfo)
{
	if (!ConverterNodeRegistry.Contains(InNodeKey))
	{
		ConverterNodeRegistry.Add(InNodeKey);
	}

	FConverterNodeRegistryValue& ConverterNodeList = ConverterNodeRegistry[InNodeKey];

	if (ensureAlways(!ConverterNodeList.PotentialConverterNodes.Contains(InNodeInfo)))
	{
		ConverterNodeList.PotentialConverterNodes.Add(InNodeInfo);
		return true;
	}
	else
	{
		// If we hit this, someone attempted to add the same converter node to our list multiple times.
		return false;
	}
}

TArray<FName> FMetasoundFrontendRegistryContainer::GetAllValidDataTypes()
{
	TArray<FName> OutDataTypes;

	for (auto& DataTypeTuple : DataTypeRegistry)
	{
		OutDataTypes.Add(DataTypeTuple.Key);
	}

	return OutDataTypes;
}

bool FMetasoundFrontendRegistryContainer::GetInfoForDataType(FName InDataType, Metasound::FDataTypeRegistryInfo& OutInfo)
{
	if (!DataTypeRegistry.Contains(InDataType))
	{
		return false;
	}
	else
	{
		OutInfo = DataTypeRegistry[InDataType].Info;
		return true;
	}
}

