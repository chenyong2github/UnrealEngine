// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundNodeInterface.h"
#include "MetasoundDataReference.h"
#include "MetasoundOperatorInterface.h"

typedef TUniqueFunction<TUniquePtr<Metasound::INode>(const ::Metasound::FInputNodeConstructorParams&)> FInputNodeConstructorCallback;
typedef TUniqueFunction<TUniquePtr<Metasound::INode>(const ::Metasound::FOutputNodeConstrutorParams&)> FOutputNodeConstructorCallback;

typedef TUniqueFunction<TUniquePtr<Metasound::INode>(const Metasound::FNodeInitData&)> FNodeGetterCallback;

namespace Metasound
{
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
	}
}

/*
static uint32 METASOUNDGRAPHCORE_API GetTypeHash(const Metasound::Frontend::FNodeRegistryKey& InKey)
{
	return InKey.NodeHash;
}
*/

/**
 * Singleton registry for all types and nodes.
 */
class METASOUNDFRONTEND_API FMetasoundFrontendRegistryContainer
{
public:
	static FMetasoundFrontendRegistryContainer* Get();
	static void ShutdownMetasoundFrontend();

	FMetasoundFrontendRegistryContainer(const FMetasoundFrontendRegistryContainer&) = delete;

	void InitializeFrontend();
	bool EnqueueInitCommand(TUniqueFunction<void()>&& InFunc);

	using FNodeRegistryKey = Metasound::Frontend::FNodeRegistryKey;
	using FNodeRegistryElement = Metasound::Frontend::FNodeRegistryElement;

	TMap<FNodeRegistryKey, FNodeRegistryElement>& GetExternalNodeRegistry();

	TUniquePtr<Metasound::INode> ConstructInputNode(const FName& InInputType, const Metasound::FInputNodeConstructorParams& InParams);
	TUniquePtr<Metasound::INode> ConstructOutputNode(const FName& InOutputType, const Metasound::FOutputNodeConstrutorParams& InParams);

	TUniquePtr<Metasound::INode> ConstructExternalNode(const FName& InNodeType, uint32 InNodeHash, const Metasound::FNodeInitData& InInitData);

	// Get the desired kind of literal for a given data type. Returns EConstructorArgType::Invalid if the data type couldn't be found.
	Metasound::ELiteralArgType GetDesiredLiteralTypeForDataType(FName InDataType) const;

	// Get whether we can build a literal of this specific type for InDataType.
	bool DoesDataTypeSupportLiteralType(FName InDataType, Metasound::ELiteralArgType InLiteralType) const;

	bool RegisterDataType(const ::Metasound::FDataTypeRegistryInfo& InDataInfo, FInputNodeConstructorCallback&& InputNodeConstructor, FOutputNodeConstructorCallback&& OutputNodeConstructor);
	bool RegisterExternalNode(FNodeGetterCallback&& InCallback);

	// Return any data types that can be used as a metasound input type or output type.
	TArray<FName> GetAllValidDataTypes();

	// Get info about a specific data type (what kind of literals we can use, etc.)
	// @returns false if InDataType wasn't found in the registry. 
	bool GetInfoForDataType(FName InDataType, Metasound::FDataTypeRegistryInfo& OutInfo);

private:
	FMetasoundFrontendRegistryContainer();

	static FMetasoundFrontendRegistryContainer* LazySingleton;

	// This buffer is used to enqueue nodes and datatypes to register when the module has been initialized,
	// in order to avoid bad behavior with ensures, logs, etc. on static initialization.
	// The bad news is that TInlineAllocator is the safest allocator to use on static init.
	// The good news is that none of these lambdas typically have captures, so this should have low memory overhead.
	static constexpr int32 MaxNumNodesAndDatatypesToInitialize = 8192;
	TArray<TUniqueFunction<void()>/*, TInlineAllocator<MaxNumNodesAndDatatypesToInitialize>*/> LazyInitCommands;
	
	FCriticalSection LazyInitCommandCritSection;
	bool bHasModuleBeenInitialized;

	// Registry in which we keep all information about nodes implemented in C++.
	TMap<FNodeRegistryKey, FNodeRegistryElement> ExternalNodeRegistry;

	struct FDataTypeRegistryElement
	{
		FInputNodeConstructorCallback InputNodeConstructor;
		FOutputNodeConstructorCallback  OutputNodeConstructor;
		Metasound::FDataTypeRegistryInfo Info;
	};

	TMap<FName, FDataTypeRegistryElement> DataTypeRegistry;
};


