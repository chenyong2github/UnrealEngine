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

TUniquePtr<Metasound::INode> FMetasoundFrontendRegistryContainer::ConstructInputNode(const FName& InInputType, const Metasound::FInputNodeConstructorParams& InParams)
{
	if (ensureAlwaysMsgf(DataTypeRegistry.Contains(InInputType), TEXT("Couldn't find data type %s!"), *InInputType.ToString()))
	{
		return DataTypeRegistry[InInputType].InputNodeConstructor(InParams);
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
		return DataTypeRegistry[InOutputType].OutputNodeConstructor(InParams);
	}
	else
	{
		return nullptr;
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
	else if (DataTypeInfo.Info.bIsConstructableWithSettings || DataTypeInfo.Info.bIsDefaultConstructible)
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
		case Metasound::ELiteralArgType::None:
		{
			return DataTypeInfo.Info.bIsConstructableWithSettings || DataTypeInfo.Info.bIsDefaultConstructible;
		}
		case Metasound::ELiteralArgType::Invalid:
		default:
		{
			return false;
		}
	}
}

bool FMetasoundFrontendRegistryContainer::RegisterDataType(const ::Metasound::FDataTypeRegistryInfo& InDataInfo, FInputNodeConstructorCallback&& InputNodeConstructor, FOutputNodeConstructorCallback&& OutputNodeConstructor)
{
	if (!ensureAlwaysMsgf(!DataTypeRegistry.Contains(InDataInfo.DataTypeName), TEXT("Name collision when trying to register Metasound Data Type %s! Make sure that you created a unique name for your data type, and that REGISTER_METASOUND_DATATYPE isn't called in a public header."), *InDataInfo.DataTypeName.ToString()))
	{
		// todo: capture callstack for previous declaration for non-shipping builds to help clarify who already registered this name for a type.
		return false;
	}
	else
	{
		FDataTypeRegistryElement InElement = { MoveTemp(InputNodeConstructor), MoveTemp(OutputNodeConstructor), InDataInfo };
		DataTypeRegistry.Add(InDataInfo.DataTypeName, MoveTemp(InElement));
		UE_LOG(LogTemp, Display, TEXT("Registered Metasound Datatype %s."), *InDataInfo.DataTypeName.ToString());
		return true;
	}
}

bool FMetasoundFrontendRegistryContainer::RegisterExternalNode(FNodeGetterCallback&& InCallback)
{
	using FInputDataVertexCollection = Metasound::FInputDataVertexCollection;
	using FOutputDataVertexCollection = Metasound::FOutputDataVertexCollection;

	Metasound::FNodeInitData DummyInitData;
	TUniquePtr<Metasound::INode> DummyNodePtr = InCallback(DummyInitData);
	if (!ensureAlwaysMsgf(DummyNodePtr.IsValid(), TEXT("Invalid getter registered!")))
	{
		return false;
	}

	Metasound::INode& DummyNode = *DummyNodePtr;

	// First, build the key.
	const FName NodeName = DummyNode.GetClassName();
	const FInputDataVertexCollection& Inputs = DummyNode.GetInputDataVertices();
	const FOutputDataVertexCollection& Outputs = DummyNode.GetOutputDataVertices();

	// Construct a hash using a combination of the class name, input names and output names.
	uint32 NodeHash = FCrc::StrCrc32(*NodeName.ToString());

	TArray<FName> InputTypes;
	TArray<FName> OutputTypes;

	for (auto& InputTuple : Inputs)
	{
		HashCombine(NodeHash, FCrc::StrCrc32(*InputTuple.Value.VertexName));
		InputTypes.Add(InputTuple.Value.DataReferenceTypeName);
	}

	for (auto& OutputTuple : Outputs)
	{
		HashCombine(NodeHash, FCrc::StrCrc32(*OutputTuple.Value.VertexName));
		OutputTypes.Add(OutputTuple.Value.DataReferenceTypeName);
	}

	FNodeRegistryKey InKey = { NodeName, NodeHash };

	// check to see if an identical node was already registered, and log
	ensureAlwaysMsgf(!ExternalNodeRegistry.Contains(InKey), TEXT("Node with identical name, inputs and outputs to node %s was already registered. The previously registered node will be overwritten. This could also happen because METASOUND_REGISTER_NODE is in a public header."), *NodeName.ToString());

	UE_LOG(LogTemp, Display, TEXT("Registered Metasound Node %s"), *NodeName.ToString());

	const bool bShouldLogInputsAndOutputs = true;
	if (bShouldLogInputsAndOutputs)
	{
		UE_LOG(LogTemp, Display, TEXT("    %d inputs:"), Inputs.Num());
		for (auto& InputTuple : Inputs)
		{
			UE_LOG(LogTemp, Display, TEXT("      %s (of type %s)"), *InputTuple.Value.VertexName, *InputTuple.Value.DataReferenceTypeName.ToString());
		}

		UE_LOG(LogTemp, Display, TEXT("    %d outputs:"), Outputs.Num());
		for (auto& OutputTuple : Outputs)
		{
			UE_LOG(LogTemp, Display, TEXT("      %s (of type %s)"), *OutputTuple.Value.VertexName, *OutputTuple.Value.DataReferenceTypeName.ToString());
		}
	}

	FNodeRegistryElement RegistryElement = FNodeRegistryElement(MoveTemp(InCallback));
	RegistryElement.InputTypes = MoveTemp(InputTypes);
	RegistryElement.OutputTypes = MoveTemp(OutputTypes);

	ExternalNodeRegistry.Add(MoveTemp(InKey), MoveTemp(RegistryElement));

	return true;
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

