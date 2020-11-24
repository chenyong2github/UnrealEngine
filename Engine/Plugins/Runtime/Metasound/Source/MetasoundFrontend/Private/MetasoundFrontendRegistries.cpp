// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundFrontendRegistries.h"
#include "CoreMinimal.h"
#include "Misc/ScopeLock.h"
#include "HAL/PlatformTime.h"

#ifndef WITH_METASOUND_FRONTEND
#define WITH_METASOUND_FRONTEND 0
#endif

namespace MetasoundFrontendRegistryPrivate
{
	// All registry keys should be created through this function to ensure consistency.
	Metasound::Frontend::FNodeRegistryKey GetRegistryKey(const FName& InClassName, int32 InMajorVersion)
	{
		Metasound::Frontend::FNodeRegistryKey Key;

		Key.NodeName = InClassName;

		// NodeHash is hash of node name and major version.
		Key.NodeHash = FCrc::StrCrc32(*Key.NodeName.ToString());
		Key.NodeHash = HashCombine(Key.NodeHash, FCrc::TypeCrc32(InMajorVersion));

		return Key;
	}
}

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

TUniquePtr<Metasound::INode> FMetasoundFrontendRegistryContainer::ConstructInputNode(const FName& InInputType, Metasound::FInputNodeConstructorParams&& InParams)
{
	if (ensureAlwaysMsgf(DataTypeRegistry.Contains(InInputType), TEXT("Couldn't find data type %s!"), *InInputType.ToString()))
	{
		return DataTypeRegistry[InInputType].Callbacks.CreateInputNode(MoveTemp(InParams));
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
		return DataTypeRegistry[InOutputType].Callbacks.CreateOutputNode(InParams);
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
		 Audio::IProxyDataPtr ProxyPtr = DataTypeRegistry[InDataType].Callbacks.CreateAudioProxy(InObject);
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
				Audio::IProxyDataPtr ProxyPtr = DataTypeRegistry[InDataType].Callbacks.CreateAudioProxy(InObject);
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
		return ExternalNodeRegistry[RegistryKey].CreateNode(InInitData);
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

bool FMetasoundFrontendRegistryContainer::RegisterExternalNode(Metasound::FCreateMetasoundNodeFunction&& InCreateNode, Metasound::FCreateMetasoundClassDescriptionFunction&& InCreateDescription)
{
	FNodeRegistryElement RegistryElement = FNodeRegistryElement(MoveTemp(InCreateNode), MoveTemp(InCreateDescription));

	FNodeRegistryKey Key;

	if (ensure(FMetasoundFrontendRegistryContainer::GetRegistryKey(RegistryElement, Key)))
	{
		// check to see if an identical node was already registered, and log
		ensureAlwaysMsgf(!ExternalNodeRegistry.Contains(Key), TEXT("Node with identical name, inputs and outputs to node %s was already registered. The previously registered node will be overwritten. This could also happen because METASOUND_REGISTER_NODE is in a public header."), *Key.NodeName.ToString());

		ExternalNodeRegistry.Add(MoveTemp(Key), MoveTemp(RegistryElement));

		return true;
	}

	return false;
}

bool FMetasoundFrontendRegistryContainer::GetRegistryKey(const Metasound::Frontend::FNodeRegistryElement& InElement, Metasound::Frontend::FNodeRegistryKey& OutKey)
{
	if (InElement.CreateClassDescription)
	{
		FMetasoundClassDescription Description = InElement.CreateClassDescription();

		OutKey = MetasoundFrontendRegistryPrivate::GetRegistryKey(FName(Description.Metadata.NodeName), Description.Metadata.MajorVersion);

		return true;
	}

	return false;
}

Metasound::Frontend::FNodeRegistryKey FMetasoundFrontendRegistryContainer::GetRegistryKey(const FNodeInfo& InNodeMetadata)
{
	return MetasoundFrontendRegistryPrivate::GetRegistryKey(InNodeMetadata.ClassName, InNodeMetadata.MajorVersion);
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

