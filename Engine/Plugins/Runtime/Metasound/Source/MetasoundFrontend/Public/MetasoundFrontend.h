// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundGraph.h"
#include "MetasoundFrontendBaseClasses.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundBuilderInterface.h"
#include "UObject/WeakObjectPtrTemplates.h"

// Forward Declarations
class FMetasoundAssetBase;

namespace Metasound
{	
	namespace Frontend
	{

		// Struct with the basics of a node class' information,
		// used to look up that node from our node browser functions,
		// and also used in FGraphHandle::AddNewNode.
		struct METASOUNDFRONTEND_API FNodeClassInfo
		{
			// The type for this node.
			EMetasoundFrontendClassType NodeType = EMetasoundFrontendClassType::Invalid;

			// The lookup key used for the internal node registry.
			FNodeRegistryKey LookupKey;
		};

		// Get all available nodes of any type.
		METASOUNDFRONTEND_API TArray<FNodeClassInfo> GetAllAvailableNodeClasses();

		/** Returns all metadata (name, description, author, what to say if it's missing) for a given node.
		 *
		 * @param InInfo - Class info for a already registered external node.
		 *
		 * @return Metadata for node.
		 */
		METASOUNDFRONTEND_API FMetasoundFrontendClassMetadata GenerateClassMetadata(const FNodeClassInfo& InInfo);

		/** Generates a new FMetasoundFrontendClass from Node Metadata 
		 *
		 * @param InNodeMetadata - Metadata describing an external node.
		 *
		 * @return Class description for external node.
		 */
		METASOUNDFRONTEND_API FMetasoundFrontendClass GenerateClassDescription(const FNodeClassMetadata& InNodeMetadata, EMetasoundFrontendClassType ClassType=EMetasoundFrontendClassType::External);

		/** Generates a new FMetasoundFrontendClass from node lookup info.
		 *
		 * @param InInfo - Class info for a already registered external node.
		 *
		 * @return Class description for external node.
		 */
		METASOUNDFRONTEND_API FMetasoundFrontendClass GenerateClassDescription(const FNodeClassInfo& InInfo);

		/** Generates a new FMetasoundFrontendClass from Node init data
		 *
		 * @tparam NodeType - Type of node to instantiate.
		 * @param InNodeInitData - Data used to call constructor of node.
		 *
		 * @return Class description for external node.
		 */
		template<typename NodeType>
		FMetasoundFrontendClass GenerateClassDescription(const FNodeInitData& InNodeInitData)
		{
			TUniquePtr<INode> Node = MakeUnique<NodeType>(InNodeInitData);

			if (ensure(Node.IsValid()))
			{
				return GenerateClassDescription(Node->GetMetadata());
			}

			return FMetasoundFrontendClass();
		}

		/** Generates a new FMetasoundFrontendClass from a NodeType
		 *
		 * @tparam NodeType - Type of node.
		 *
		 * @return Class description for external node.
		 */
		template<typename NodeType>
		FMetasoundFrontendClass GenerateClassDescription()
		{
			FNodeInitData InitData;
			InitData.InstanceName = FString(TEXT("GenerateClassDescriptionForNode"));

			return GenerateClassDescription<NodeType>(InitData);
		}


		template<typename DataType>
		FName GetDataTypeName()
		{
			static FName DataTypeName = FName(TDataReferenceTypeInfo<DataType>::TypeName);
			return DataTypeName;
		}

		// Returns a list of all available data types.
		METASOUNDFRONTEND_API TArray<FName> GetAllAvailableDataTypes();

		// outputs the traits for a given data type.
		// returns false if InDataType couldn't be found.
		METASOUNDFRONTEND_API bool GetTraitsForDataType(FName InDataType, FDataTypeRegistryInfo& OutInfo);

		// Takes a JSON string and deserializes it into a Metasound document struct.
		// @returns false if the file couldn't be found or parsed into a document.
		METASOUNDFRONTEND_API bool ImportJSONToMetasound(const FString& InJSON, FMetasoundFrontendDocument& OutMetasoundDocument);

		// Opens a json document at the given absolute path and deserializes it into a Metasound document struct.
		// @returns false if the file couldn't be found or parsed into a document.
		METASOUNDFRONTEND_API bool ImportJSONAssetToMetasound(const FString& InPath, FMetasoundFrontendDocument& OutMetasoundDocument);

		// Struct that indicates whether an input and an output can be connected,
		// and whether an intermediate node is necessary to connect the two.
		struct METASOUNDFRONTEND_API FConnectability
		{
			enum class EConnectable
			{
				Yes,
				No,
				YesWithConverterNode
			};

			EConnectable Connectable = EConnectable::No;

			// If Connectable is EConnectable::YesWithConverterNode,
			// this will be a populated list of nodes we can use 
			// to convert between the input and output.
			TArray<FConverterNodeInfo> PossibleConverterNodeClasses;
		};

	} // namespace Frontend
} // namespace Metasound
