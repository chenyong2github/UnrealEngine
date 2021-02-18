// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundFrontendRegistries.h"

#include "CoreMinimal.h"
#include "MetasoundLog.h"
#include "Misc/ScopeLock.h"
#include "HAL/PlatformTime.h"

#ifndef WITH_METASOUND_FRONTEND
#define WITH_METASOUND_FRONTEND 0
#endif

namespace Metasound
{
	namespace MetasoundFrontendRegistryPrivate
	{
		// All registry keys should be created through this function to ensure consistency.
		Frontend::FNodeRegistryKey GetRegistryKey(const FName& InClassName, int32 InMajorVersion)
		{
			Frontend::FNodeRegistryKey Key;

			Key.NodeClassFullName = InClassName;

			// NodeHash is hash of node name and major version.
			Key.NodeHash = FCrc::StrCrc32(*Key.NodeClassFullName.ToString());
			Key.NodeHash = HashCombine(Key.NodeHash, FCrc::TypeCrc32(InMajorVersion));

			return Key;
		}

		// Return the compatible literal with the most descriptive type.
		// TODO: Currently TIsParsable<> allows for implicit conversion of
		// constructor arguments of integral types which can cause some confusion
		// here when trying to match a literal type to a constructor. For example:
		//
		// struct FBoolConstructibleType
		// {
		// 	FBoolConstructibleType(bool InValue);
		// };
		//
		// static_assert(TIsParsable<FBoolConstructible, double>::Value); 
		//
		// Implicit conversions are currently allowed in TIsParsable because this
		// is perfectly legal syntax.
		//
		// double Value = 10.0;
		// FBoolConstructibleType BoolConstructible = Value;
		//
		// There are some tricks to possibly disable implicit conversions when
		// checking for specific constructors, but they are yet to be implemented 
		// and are untested. Here's the basic idea.
		//
		// template<DataType, DesiredIntegralArgType>
		// struct TOnlyConvertIfIsSame
		// {
		// 		// Implicit conversion only defined if types match.
		// 		template<typename SuppliedIntegralArgType, std::enable_if<std::is_same<std::decay<SuppliedIntegralArgType>::type, DesiredIntegralArgType>::value, int> = 0>
		// 		operator DesiredIntegralArgType()
		// 		{
		// 			return DesiredIntegralArgType{};
		// 		}
		// };
		//
		// static_assert(false == std::is_constructible<FBoolConstructibleType, TOnlyConvertIfSame<double>>::value);
		// static_assert(true == std::is_constructible<FBoolConstructibleType, TOnlyConvertIfSame<bool>>::value);
		ELiteralType GetMostDescriptiveLiteralForDataType(const FDataTypeRegistryInfo& InDataTypeInfo)
		{
			if (InDataTypeInfo.bIsProxyArrayParsable)
			{
				return ELiteralType::UObjectProxyArray;
			}
			else if (InDataTypeInfo.bIsProxyParsable)
			{
				return ELiteralType::UObjectProxy;
			}
			else if (InDataTypeInfo.bIsEnum && InDataTypeInfo.bIsIntParsable)
			{
				return ELiteralType::Integer;
			}
			else if (InDataTypeInfo.bIsStringArrayParsable)
			{
				return ELiteralType::StringArray;
			}
			else if (InDataTypeInfo.bIsFloatArrayParsable)
			{
				return ELiteralType::FloatArray;
			}
			else if (InDataTypeInfo.bIsIntArrayParsable)
			{
				return ELiteralType::IntegerArray;
			}
			else if (InDataTypeInfo.bIsBoolArrayParsable)
			{
				return ELiteralType::BooleanArray;
			}
			else if (InDataTypeInfo.bIsStringParsable)
			{
				return ELiteralType::String;
			}
			else if (InDataTypeInfo.bIsFloatParsable)
			{
				return ELiteralType::Float;
			}
			else if (InDataTypeInfo.bIsIntParsable)
			{
				return ELiteralType::Integer;
			}
			else if (InDataTypeInfo.bIsBoolParsable)
			{
				return ELiteralType::Boolean;
			}
			else if (InDataTypeInfo.bIsDefaultArrayParsable)
			{
				return ELiteralType::NoneArray; 
			}
			else if (InDataTypeInfo.bIsDefaultParsable)
			{
				return ELiteralType::None;
			}
			else
			{
				// if we ever hit this, something has gone wrong with the REGISTER_METASOUND_DATATYPE macro.
				// we should have failed to compile if any of these are false.
				checkNoEntry();
				return ELiteralType::Invalid;
			}
		}
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

TUniquePtr<Metasound::INode> FMetasoundFrontendRegistryContainer::ConstructOutputNode(const FName& InOutputType, const Metasound::FOutputNodeConstructorParams& InParams)
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

Metasound::FLiteral FMetasoundFrontendRegistryContainer::GenerateLiteralForUObject(const FName& InDataType, UObject* InObject)
{
	if (ensureAlwaysMsgf(DataTypeRegistry.Contains(InDataType), TEXT("Couldn't find data type %s!"), *InDataType.ToString()))
	{
		 Audio::IProxyDataPtr ProxyPtr = DataTypeRegistry[InDataType].Callbacks.CreateAudioProxy(InObject);
		 if (ensureAlwaysMsgf(ProxyPtr.IsValid(), TEXT("UObject failed to create a valid proxy!")))
		 {
			 return Metasound::FLiteral(MoveTemp(ProxyPtr));
		 }
		 else
		 {
			 return Metasound::FLiteral();
		 }
	}
	else
	{
		return Metasound::FLiteral();
	}
}

Metasound::FLiteral FMetasoundFrontendRegistryContainer::GenerateLiteralForUObjectArray(const FName& InDataType, TArray<UObject*> InObjectArray)
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

		return Metasound::FLiteral(MoveTemp(ProxyArray));
	}
	else
	{
		return Metasound::FLiteral();
	}
}

TUniquePtr<Metasound::INode> FMetasoundFrontendRegistryContainer::ConstructExternalNode(const FName& InNodeClassFullName, uint32 InNodeHash, const Metasound::FNodeInitData& InInitData)
{
	Metasound::Frontend::FNodeRegistryKey RegistryKey;
	RegistryKey.NodeClassFullName = InNodeClassFullName;
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



Metasound::ELiteralType FMetasoundFrontendRegistryContainer::GetDesiredLiteralTypeForDataType(FName InDataType) const
{
	if (!DataTypeRegistry.Contains(InDataType))
	{
		return Metasound::ELiteralType::Invalid;
	}

	const FDataTypeRegistryElement& DataTypeInfo = DataTypeRegistry[InDataType];

	// If there's a designated preferred literal type for this datatype, use that.
	if (DataTypeInfo.Info.PreferredLiteralType != Metasound::ELiteralType::Invalid)
	{
		return DataTypeInfo.Info.PreferredLiteralType;
	}

	// Otherwise, we opt for the highest precision construction option available.
	return Metasound::MetasoundFrontendRegistryPrivate::GetMostDescriptiveLiteralForDataType(DataTypeInfo.Info);
}

UClass* FMetasoundFrontendRegistryContainer::GetLiteralUClassForDataType(FName InDataType) const
{
	if (!DataTypeRegistry.Contains(InDataType))
	{
		ensureAlwaysMsgf(false, TEXT("DataType %s not registered."), *InDataType.ToString());
		return nullptr;
	}
	else
	{
		return DataTypeRegistry[InDataType].Info.ProxyGeneratorClass;
	}
}

bool FMetasoundFrontendRegistryContainer::DoesDataTypeSupportLiteralType(FName InDataType, Metasound::ELiteralType InLiteralType) const
{
	if (!DataTypeRegistry.Contains(InDataType))
	{
		ensureAlwaysMsgf(false, TEXT("DataType %s not registered."), *InDataType.ToString());
		return false;
	}

	const FDataTypeRegistryElement& DataTypeInfo = DataTypeRegistry[InDataType];
	
	switch (InLiteralType)
	{
		case Metasound::ELiteralType::Boolean:
		{
			return DataTypeInfo.Info.bIsBoolParsable;
		}
		case Metasound::ELiteralType::Integer:
		{
			return DataTypeInfo.Info.bIsIntParsable;
		}
		case Metasound::ELiteralType::Float:
		{
			return DataTypeInfo.Info.bIsFloatParsable;
		}
		case Metasound::ELiteralType::String:
		{
			return DataTypeInfo.Info.bIsStringParsable;
		}
		case Metasound::ELiteralType::UObjectProxy:
		{
			return DataTypeInfo.Info.bIsProxyParsable;
		}
		case Metasound::ELiteralType::UObjectProxyArray:
		{
			return DataTypeInfo.Info.bIsProxyArrayParsable;
		}
		case Metasound::ELiteralType::None:
		{
			return DataTypeInfo.Info.bIsDefaultParsable;
		}
		case Metasound::ELiteralType::Invalid:
		default:
		{
			return false;
		}
	}
}

bool FMetasoundFrontendRegistryContainer::RegisterDataType(const ::Metasound::FDataTypeRegistryInfo& InDataInfo, const ::Metasound::FDataTypeConstructorCallbacks& InCallbacks)
{
	if (!ensureAlwaysMsgf(!DataTypeRegistry.Contains(InDataInfo.DataTypeName),
		TEXT("Name collision when trying to register Metasound Data Type %s! DataType must have "
			"unique name and REGISTER_METASOUND_DATATYPE cannot be called in a public header."),
			*InDataInfo.DataTypeName.ToString()))
	{
		// todo: capture callstack for previous declaration for non-shipping builds to help clarify who already registered this name for a type.
		return false;
	}
	else
	{
		FDataTypeRegistryElement InElement = { InCallbacks, InDataInfo };

		DataTypeRegistry.Add(InDataInfo.DataTypeName, InElement);

		Metasound::Frontend::FNodeRegistryKey InputNodeRegistryKey = GetRegistryKey(InCallbacks.CreateFrontendInputClass().Metadata);
		DataTypeNodeRegistry.Add(InputNodeRegistryKey, InElement);

		Metasound::Frontend::FNodeRegistryKey OutputNodeRegistryKey = GetRegistryKey(InCallbacks.CreateFrontendOutputClass().Metadata);
		DataTypeNodeRegistry.Add(OutputNodeRegistryKey, InElement);

		UE_LOG(LogMetasound, Display, TEXT("Registered Metasound Datatype %s."), *InDataInfo.DataTypeName.ToString());
		return true;
	}
}

bool FMetasoundFrontendRegistryContainer::RegisterEnumDataInterface(FName InDataType, TSharedPtr<IEnumDataTypeInterface>&& InInterface)
{
	if (FDataTypeRegistryElement* Element = DataTypeRegistry.Find(InDataType))
	{
		Element->EnumInterface = MoveTemp(InInterface);
		return true;
	}
	return false;
}

bool FMetasoundFrontendRegistryContainer::RegisterExternalNode(Metasound::FCreateMetasoundNodeFunction&& InCreateNode, Metasound::FCreateMetasoundFrontendClassFunction&& InCreateDescription)
{
	FNodeRegistryElement RegistryElement = FNodeRegistryElement(MoveTemp(InCreateNode), MoveTemp(InCreateDescription));

	FNodeRegistryKey Key;

	if (ensure(FMetasoundFrontendRegistryContainer::GetRegistryKey(RegistryElement, Key)))
	{
		// check to see if an identical node was already registered, and log
		ensureAlwaysMsgf(!ExternalNodeRegistry.Contains(Key), TEXT("Node with identical name, inputs and outputs to node %s was already registered. The previously registered node will be overwritten. This could also happen because METASOUND_REGISTER_NODE is in a public header."), *Key.NodeClassFullName.ToString());

		ExternalNodeRegistry.Add(MoveTemp(Key), MoveTemp(RegistryElement));

		return true;
	}

	return false;
}

bool FMetasoundFrontendRegistryContainer::GetRegistryKey(const Metasound::Frontend::FNodeRegistryElement& InElement, Metasound::Frontend::FNodeRegistryKey& OutKey)
{
	if (InElement.CreateFrontendClass)
	{
		FMetasoundFrontendClass FrontendClass = InElement.CreateFrontendClass();

		OutKey = GetRegistryKey(FrontendClass.Metadata);

		return true;
	}

	return false;
}

Metasound::Frontend::FNodeRegistryKey FMetasoundFrontendRegistryContainer::GetRegistryKey(const FNodeClassMetadata& InNodeMetadata)
{
	return Metasound::MetasoundFrontendRegistryPrivate::GetRegistryKey(InNodeMetadata.ClassName.GetFullName(), InNodeMetadata.MajorVersion);
}

Metasound::Frontend::FNodeRegistryKey FMetasoundFrontendRegistryContainer::GetRegistryKey(const FMetasoundFrontendClassMetadata& InNodeMetadata)
{
	return Metasound::MetasoundFrontendRegistryPrivate::GetRegistryKey(InNodeMetadata.ClassName.GetFullName(), InNodeMetadata.Version.Major);
}

bool FMetasoundFrontendRegistryContainer::GetFrontendClassFromRegistered(const FMetasoundFrontendClassMetadata& InMetadata, FMetasoundFrontendClass& OutClass)
{
	FMetasoundFrontendRegistryContainer* Registry = FMetasoundFrontendRegistryContainer::Get();

	if (ensure(nullptr != Registry))
	{
		Metasound::Frontend::FNodeRegistryKey RegistryKey = GetRegistryKey(InMetadata);

		if (InMetadata.Type == EMetasoundFrontendClassType::External)
		{
			if (Registry->ExternalNodeRegistry.Contains(RegistryKey))
			{
				OutClass = Registry->ExternalNodeRegistry[RegistryKey].CreateFrontendClass();
				return true;
			}
		}
		else if (InMetadata.Type == EMetasoundFrontendClassType::Input)
		{
			if (Registry->DataTypeNodeRegistry.Contains(RegistryKey))
			{
				OutClass = Registry->DataTypeNodeRegistry[RegistryKey].Callbacks.CreateFrontendInputClass();
				return true;
			}
		}
		else if (InMetadata.Type == EMetasoundFrontendClassType::Output)
		{
			if (Registry->DataTypeNodeRegistry.Contains(RegistryKey))
			{
				OutClass = Registry->DataTypeNodeRegistry[RegistryKey].Callbacks.CreateFrontendOutputClass();
				return true;
			}
		}
	}

	return false;
}

bool FMetasoundFrontendRegistryContainer::GetInputNodeClassMetadataForDataType(const FName& InDataTypeName, FMetasoundFrontendClassMetadata& OutMetadata)
{
	if (FMetasoundFrontendRegistryContainer* Registry = FMetasoundFrontendRegistryContainer::Get())
	{
		if (Registry->DataTypeRegistry.Contains(InDataTypeName))
		{
			OutMetadata = Registry->DataTypeRegistry[InDataTypeName].Callbacks.CreateFrontendInputClass().Metadata;
			return true;
		}
	}
	return false;
}

bool FMetasoundFrontendRegistryContainer::GetOutputNodeClassMetadataForDataType(const FName& InDataTypeName, FMetasoundFrontendClassMetadata& OutMetadata)
{
	if (FMetasoundFrontendRegistryContainer* Registry = FMetasoundFrontendRegistryContainer::Get())
	{
		if (Registry->DataTypeRegistry.Contains(InDataTypeName))
		{
			OutMetadata = Registry->DataTypeRegistry[InDataTypeName].Callbacks.CreateFrontendOutputClass().Metadata;
			return true;
		}
	}
	return false;
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


TSharedPtr<const Metasound::Frontend::IEnumDataTypeInterface>
FMetasoundFrontendRegistryContainer::GetEnumInterfaceForDataType(FName InDataType) const
{
	if (const FDataTypeRegistryElement* Element = DataTypeRegistry.Find(InDataType))
	{
		return Element->EnumInterface;
	}
	return nullptr;
}

