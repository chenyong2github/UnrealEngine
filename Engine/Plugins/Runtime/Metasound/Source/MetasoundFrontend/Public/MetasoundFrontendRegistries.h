// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundNodeInterface.h"
#include "MetasoundDataReference.h"
#include "MetasoundOperatorInterface.h"
#include "IAudioProxyInitializer.h"

typedef TUniqueFunction<TUniquePtr<Metasound::INode>(::Metasound::FInputNodeConstructorParams&&)> FInputNodeConstructorCallback;
typedef TUniqueFunction<TUniquePtr<Metasound::INode>(const ::Metasound::FOutputNodeConstrutorParams&)> FOutputNodeConstructorCallback;

// This function is used to create a proxy from a datatype's base uclass.
typedef TUniqueFunction<Audio::IProxyDataPtr(UObject*)> FProxyGetterCallback;

typedef TUniqueFunction<TUniquePtr<Metasound::INode>(const Metasound::FNodeInitData&)> FNodeGetterCallback;

namespace Metasound
{
	// Various elements that we pass to the frontend registry based on templated type traits.
	struct FDataTypeRegistryInfo
	{
		// The name of the data type itself.
		FName DataTypeName;

		// What type we should default to using for literals.
		ELiteralArgType PreferredLiteralType = ELiteralArgType::Invalid;

		// This indicates the type can only be constructed with FOperatorSettings or the default constructor.
		bool bIsDefaultParsable = false;

		// These bools signify what basic
		// UProperty primitives we can use to describe this data type as a literal in a document.
		bool bIsBoolParsable = false;
		bool bIsIntParsable = false;
		bool bIsFloatParsable = false;
		bool bIsStringParsable = false;

		// these are used for using UObjects, or arrays of UObjects.
		bool bIsProxyParsable = false;
		bool bIsProxyArrayParsable = false;

		// If this datatype was registered with a specific UClass to use to filter with, that will be used here:
		UClass* ProxyGeneratorClass = nullptr;

	};

	namespace Frontend
	{
		struct METASOUNDFRONTEND_API FNodeRegistryKey
		{
			// The class name for the node.
			FName NodeName;

			// A hash generated from the input types and output types for this node.
			// TODO: Write up some tests to ensure this is deterministic.
			uint32 NodeHash;

			FORCEINLINE bool operator==(const FNodeRegistryKey& Other) const
			{
				return NodeHash == Other.NodeHash && NodeName == Other.NodeName;
			}

			friend uint32 GetTypeHash(const Metasound::Frontend::FNodeRegistryKey& InKey)
			{
				return InKey.NodeHash;
			}
		};


		struct METASOUNDFRONTEND_API FNodeRegistryElement
		{
			// This lambda can be used to get an INodeBase for this specific node class.
			FNodeGetterCallback GetterCallback;

			TArray<FName> InputTypes;
			TArray<FName> OutputTypes;

			FNodeRegistryElement(FNodeGetterCallback&& InCallback)
				: GetterCallback(MoveTemp(InCallback))
			{
			}
		};

		struct METASOUNDFRONTEND_API FConverterNodeRegistryKey
		{
			// The datatype one would like to convert from.
			FName FromDataType;

			// The datatype one would like to convert to.
			FName ToDataType;

			FORCEINLINE bool operator==(const FConverterNodeRegistryKey& Other) const
			{
				return FromDataType == Other.FromDataType && ToDataType == Other.ToDataType;
			}

			friend uint32 GetTypeHash(const ::Metasound::Frontend::FConverterNodeRegistryKey& InKey)
			{
				return HashCombine(GetTypeHash(InKey.FromDataType), GetTypeHash(InKey.ToDataType));
			}
		};

		struct METASOUNDFRONTEND_API FConverterNodeInfo
		{
			// If this node has multiple input pins, we use this to designate which pin should be used.
			FString PreferredConverterInputPin;

			// If this node has multiple output pins, we use this to designate which pin should be used.
			FString PreferredConverterOutputPin;

			// The key for this node in the node registry.
			FNodeRegistryKey NodeKey;

			FORCEINLINE bool operator==(const FConverterNodeInfo& Other) const
			{
				return NodeKey == Other.NodeKey;
			}
		};

		struct METASOUNDFRONTEND_API FConverterNodeRegistryValue
		{
			// A list of nodes that can perform a conversion between the two datatypes described in the FConverterNodeRegistryKey for this map element.
			TArray<FConverterNodeInfo> PotentialConverterNodes;
		};
	}

	struct FDataTypeConstructorCallbacks
	{
		// This constructs a TInputNode<> with the corresponding datatype.
		FInputNodeConstructorCallback InputNodeConstructor;

		// This constructs a TOutputNode<> with the corresponding datatype.
		FOutputNodeConstructorCallback OutputNodeConstructor;

		// For datatypes that use a UObject literal or a UObject literal array, this lambda generates a literal from the corresponding UObject.
		FProxyGetterCallback ProxyConstructor;
	};
}

/**
 * Singleton registry for all types and nodes.
 */
class METASOUNDFRONTEND_API FMetasoundFrontendRegistryContainer
{
	using FConverterNodeRegistryKey = ::Metasound::Frontend::FConverterNodeRegistryKey;
	using FConverterNodeRegistryValue = ::Metasound::Frontend::FConverterNodeRegistryValue;
	using FConverterNodeInfo = ::Metasound::Frontend::FConverterNodeInfo;

	using FNodeRegistryKey = Metasound::Frontend::FNodeRegistryKey;
	using FNodeRegistryElement = Metasound::Frontend::FNodeRegistryElement;

	using FDataTypeRegistryInfo = Metasound::FDataTypeRegistryInfo;
	using FDataTypeConstructorCallbacks = ::Metasound::FDataTypeConstructorCallbacks;

public:
	static FMetasoundFrontendRegistryContainer* Get();
	static void ShutdownMetasoundFrontend();

	FMetasoundFrontendRegistryContainer(const FMetasoundFrontendRegistryContainer&) = delete;

	// This is called on module startup. This invokes any registration commands enqueued by our registration macros.
	void InitializeFrontend();


	bool EnqueueInitCommand(TUniqueFunction<void()>&& InFunc);

	TMap<FNodeRegistryKey, FNodeRegistryElement>& GetExternalNodeRegistry();

	TUniquePtr<Metasound::INode> ConstructInputNode(const FName& InInputType, Metasound::FInputNodeConstructorParams&& InParams);
	TUniquePtr<Metasound::INode> ConstructOutputNode(const FName& InOutputType, const Metasound::FOutputNodeConstrutorParams& InParams);

	Metasound::FDataTypeLiteralParam GenerateLiteralForUObject(const FName& InDataType, UObject* InObject);
	Metasound::FDataTypeLiteralParam GenerateLiteralForUObjectArray(const FName& InDataType, TArray<UObject*> InObjectArray);

	// Create a new instance of a C++ implemented node from the registry.
	TUniquePtr<Metasound::INode> ConstructExternalNode(const FName& InNodeType, uint32 InNodeHash, const Metasound::FNodeInitData& InInitData);

	// Returns a list of possible nodes to use to convert from FromDataType to ToDataType.
	// Returns an empty array if none are available.
	TArray<FConverterNodeInfo> GetPossibleConverterNodes(const FName& FromDataType, const FName& ToDataType);

	// Get the desired kind of literal for a given data type. Returns EConstructorArgType::Invalid if the data type couldn't be found.
	Metasound::ELiteralArgType GetDesiredLiteralTypeForDataType(FName InDataType) const;

	UClass* GetLiteralUClassForDataType(FName InDataType) const;


	// Get whether we can build a literal of this specific type for InDataType.
	bool DoesDataTypeSupportLiteralType(FName InDataType, Metasound::ELiteralArgType InLiteralType) const;

	bool RegisterDataType(const FDataTypeRegistryInfo& InDataInfo, FDataTypeConstructorCallbacks&& InCallbacks);
	bool RegisterExternalNode(FNodeGetterCallback&& InCallback);
	bool RegisterConversionNode(const FConverterNodeRegistryKey& InNodeKey, const FConverterNodeInfo& InNodeInfo);

	template <typename TNodeType>
	static FNodeRegistryKey GetRegistryKeyForNode()
	{
		Metasound::FNodeInitData DummyInitData;

		TNodeType DummyNode = TNodeType(DummyInitData);

		return GetRegistryKeyForNodeInternal(DummyNode);
	}

	template <typename TNodeType>
	bool IsNodeRegistered()
	{
		return ExternalNodeRegistry.Contains(GetRegistryKeyForNode<TNodeType>());
	}

	// Return any data types that can be used as a metasound input type or output type.
	TArray<FName> GetAllValidDataTypes();

	// Get info about a specific data type (what kind of literals we can use, etc.)
	// @returns false if InDataType wasn't found in the registry. 
	bool GetInfoForDataType(FName InDataType, FDataTypeRegistryInfo& OutInfo);

private:
	FMetasoundFrontendRegistryContainer();

	// Internal convenience function for generating a hash for an INode.
	static FNodeRegistryKey GetRegistryKeyForNodeInternal(const Metasound::INode& InNode);

	static FMetasoundFrontendRegistryContainer* LazySingleton;

	// This buffer is used to enqueue nodes and datatypes to register when the module has been initialized,
	// in order to avoid bad behavior with ensures, logs, etc. on static initialization.
	// The bad news is that TInlineAllocator is the safest allocator to use on static init.
	// The good news is that none of these lambdas typically have captures, so this should have low memory overhead.
	static constexpr int32 MaxNumNodesAndDatatypesToInitialize = 8192;
	TArray<TUniqueFunction<void()>, TInlineAllocator<MaxNumNodesAndDatatypesToInitialize>> LazyInitCommands;
	
	FCriticalSection LazyInitCommandCritSection;
	bool bHasModuleBeenInitialized;

	// Registry in which we keep all information about nodes implemented in C++.
	TMap<FNodeRegistryKey, FNodeRegistryElement> ExternalNodeRegistry;

	// Registry in which we keep lists of possible nodes to use to convert between two datatypes
	TMap<FConverterNodeRegistryKey, FConverterNodeRegistryValue> ConverterNodeRegistry;

	struct FDataTypeRegistryElement
	{
		Metasound::FDataTypeConstructorCallbacks Callbacks;

		Metasound::FDataTypeRegistryInfo Info;
	};

	TMap<FName, FDataTypeRegistryElement> DataTypeRegistry;
};


