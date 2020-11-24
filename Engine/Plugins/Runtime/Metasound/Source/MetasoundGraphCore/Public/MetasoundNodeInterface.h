// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Internationalization/Text.h"
#include "MetasoundDataFactory.h"
#include "MetasoundDataReference.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundVertex.h"
#include "MetasoundLiteral.h"

namespace Metasound
{
	static const FText PluginAuthor = NSLOCTEXT("MetasoundGraphCore", "Metasound_DefaultAuthor", "Epic Games, Inc.");
	static const FText PluginNodeMissingPrompt = NSLOCTEXT("MetasoundGraphCore", "Metasound_DefaultMissingPrompt", "Make sure that the Metasound plugin is loaded.");

	/**
	 * This struct is used to pass in any arguments required for constructing a single node instance.
	 * because of this, all FNode implementations have to implement a constructor that takes an FNodeInitData instance.
	 */
	struct FNodeInitData
	{
		FString InstanceName;
		TMap<FName, FDataTypeLiteralParam> ParamMap;

		template<typename ParamType>
		ParamType GetParamValue(FName ParamName)
		{
			checkf(ParamMap.Contains(ParamName), TEXT("Tried to use node initialization parameter that didn't exist!"));

			return TDataTypeLiteralFactory<ParamType>::CreateAny(ParamMap[ParamName]);
		}
	};

	/** Provides metadata for a given node. */
	struct FNodeInfo
	{
		// TODO: rename this class to FNodeMetadata

		/** Name of class. Used for registration and lookup. */
		FName ClassName;

		/** Major version of node. Used for registration and lookup. */
		int32 MajorVersion = -1;

		/** Minor version of node. */
		int32 MinorVersion = -1;

		/** Human readable description of node. */
		FText Description;

		/** Author information. */
		FText Author;

		/** Human readable prompt for acquiring plugin in case node is not loaded. */
		FText PromptIfMissing;

		/** Default vertex interface for the node */
		FVertexInterface DefaultInterface;

		/** Hierarchy of categories for displaying node. */
		TArray<FText> CategoryHierarchy;

		/** List of keywords for contextual node searching. */
		TArray<FName> Keywords;

		/** Returns an empty FNodeInfo object. */
		static const FNodeInfo& GetEmpty()
		{
			static const FNodeInfo EmptyInfo;
			return EmptyInfo;
		}
	};

	/** INodeBase
	 * 
	 * Interface for all nodes that can describe their name, type, inputs and outputs.
	 */
	class INodeBase
	{
		public:
			virtual ~INodeBase() {}

			/** Return the name of this specific instance of the node class. */
			virtual const FString& GetInstanceName() const = 0;

			/** Return the type name of this node. */
			virtual const FNodeInfo& GetMetadata() const = 0;

			/** Return the current vertex interface. */
			virtual const FVertexInterface& GetVertexInterface() const = 0;

			/** Set the vertex interface. If the vertex was successfully changed, returns true. 
			 *
			 * @param InInterface - New interface for node. 
			 *
			 * @return True on success, false otherwise.
			 */
			virtual bool SetVertexInterface(const FVertexInterface& InInterface) = 0;

			/** Expresses whether a specific vertex interface is supported.
			 *
			 * @param InInterface - New interface. 
			 *
			 * @return True if the interface is supported, false otherwise. 
			 */
			virtual bool IsVertexInterfaceSupported(const FVertexInterface& InInterface) const = 0;
	};

	// Forward declare
	class IOperatorFactory;

	/** Shared ref type of operator factory. */
	typedef TSharedRef<IOperatorFactory, ESPMode::ThreadSafe> FOperatorFactorySharedRef;

	/** Convenience function for making operator factory references */
	template<typename FactoryType, typename... ArgTypes>
	TSharedRef<FactoryType, ESPMode::ThreadSafe> MakeOperatorFactoryRef(ArgTypes&&... Args)
	{
		return MakeShared<FactoryType, ESPMode::ThreadSafe>(Forward<ArgTypes>(Args)...);
	}


	/** INode 
	 * 
	 * Interface for all nodes that can create operators. 
	 */
	class INode : public INodeBase
	{
		public:
			virtual ~INode() {}

			/** Return a reference to the default operator factory. */
			virtual FOperatorFactorySharedRef GetDefaultOperatorFactory() const = 0;

			/* Future implementations may support additional factory types and interfaces
				virtual bool DoesSupportOperatorFactory(const FString& InFractoryName) const = 0;
				virtual IOperatorFactory* GetOperatorFactory(const FString& InFactoryName) = 0;
				virtual ISpecialFactory* GetSpecialFactory() { return nullptr; }
			*/
	};

	/** FOutputDataSource describes the source of data which is produced within
	 * a graph and exposed external to the graph. 
	 */
	struct FOutputDataSource
	{
		/** Node containing the output data vertex. */
		const INode* Node = nullptr;

		/** Output data vertex. */
		FOutputDataVertex Vertex;

		FOutputDataSource()
		{
		}

		FOutputDataSource(const INode& InNode, const FOutputDataVertex& InVertex)
		:	Node(&InNode)
		,	Vertex(InVertex)
		{
		}


		/** Check if two FOutputDataSources are equal. */
		friend bool METASOUNDGRAPHCORE_API operator==(const FOutputDataSource& InLeft, const FOutputDataSource& InRight);
		friend bool METASOUNDGRAPHCORE_API operator!=(const FOutputDataSource& InLeft, const FOutputDataSource& InRight);
		friend bool METASOUNDGRAPHCORE_API operator<(const FOutputDataSource& InLeft, const FOutputDataSource& InRight);
	};

	/** FInputDataSource describes the destination of data which produced 
	 * external to the graph and read internal to the graph.
	 */
	struct FInputDataDestination
	{
		/** Node containing the input data vertex. */
		const INode* Node = nullptr;

		/** Input data vertex of edge. */
		FInputDataVertex Vertex;

		FInputDataDestination()
		{
		}

		FInputDataDestination(const INode& InNode, const FInputDataVertex& InVertex)
		:	Node(&InNode)
		,	Vertex(InVertex)
		{
		}

		/** Check if two FInputDataDestinations are equal. */
		friend bool METASOUNDGRAPHCORE_API operator==(const FInputDataDestination& InLeft, const FInputDataDestination& InRight);
		friend bool METASOUNDGRAPHCORE_API operator!=(const FInputDataDestination& InLeft, const FInputDataDestination& InRight);
		friend bool METASOUNDGRAPHCORE_API operator<(const FInputDataDestination& InLeft, const FInputDataDestination& InRight);
	};

	/** Key type for an FOutputDataSource or FInputDataDestination. */
	typedef TTuple<const INode*, FString> FNodeDataVertexKey;

	/** FOutputDataSourceCollection contains multiple FOutputDataSources mapped 
	 * by FNodeDataVertexKeys.
	 */
	typedef TMap<FNodeDataVertexKey, FOutputDataSource> FOutputDataSourceCollection;

	/** FInputDataDestinationCollection contains multiple 
	 * FInputDataDestinations mapped by FNodeDataVertexKeys.
	 */
	typedef TMap<FNodeDataVertexKey, FInputDataDestination> FInputDataDestinationCollection;

	/** Make a FNodeDataVertexKey from an FOutputDataSource. */
	FORCEINLINE FNodeDataVertexKey MakeSourceDataVertexKey(const FOutputDataSource& InSource)
	{
		return FNodeDataVertexKey(InSource.Node, InSource.Vertex.GetVertexName());
	}

	FORCEINLINE FNodeDataVertexKey MakeDestinationDataVertexKey(const FInputDataDestination& InDestination)
	{
		return FNodeDataVertexKey(InDestination.Node, InDestination.Vertex.GetVertexName());
	}

	/** FDataEdge
	 *
	 * An edge describes a connection between two Nodes. 
	 */
	struct FDataEdge
	{
		FOutputDataSource From;

		FInputDataDestination To;

		FDataEdge()
		{
		}

		FDataEdge(const FOutputDataSource& InFrom, const FInputDataDestination& InTo)
		:	From(InFrom)
		,	To(InTo)
		{
		}

		/** Check if two FDataEdges are equal. */
		friend bool METASOUNDGRAPHCORE_API operator==(const FDataEdge& InLeft, const FDataEdge& InRight);
		friend bool METASOUNDGRAPHCORE_API operator!=(const FDataEdge& InLeft, const FDataEdge& InRight);
		friend bool METASOUNDGRAPHCORE_API operator<(const FDataEdge& InLeft, const FDataEdge& InRight);
	};


	/** Interface for graph of nodes. */
	class IGraph : public INodeBase
	{
		public:
			virtual ~IGraph() {}

			/** Retrieve all the edges associated with a graph. */
			virtual const TArray<FDataEdge>& GetDataEdges() const = 0;

			/** Get vertices which contain input parameters. */
			virtual const FInputDataDestinationCollection& GetInputDataDestinations() const = 0;

			/** Get vertices which contain output parameters. */
			virtual const FOutputDataSourceCollection& GetOutputDataSources() const = 0;
	};
}
