// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundDataReference.h"

namespace Metasound
{
	/** FDataVertex
	 *
	 *  This describes a data vertex on a Node.
	 */
	struct FDataVertex
	{
		/** Name of vertex. */
		FString VertexName;

		/** Type name of data reference. */
		FName DataReferenceTypeName;

		/** Description of the vertex. */
		FText Description;

		FDataVertex()
		:	Description(FText::GetEmpty())
		{
		}

		FDataVertex(const FString& InVertexName, const FName& InDataReferenceTypeName, const FText& InDescription)
		:	VertexName(InVertexName)
		,	DataReferenceTypeName(InDataReferenceTypeName)
		,	Description(InDescription)
		{
		}

		/** Check if two data vertex specs are equal. */
		friend bool METASOUNDGRAPHCORE_API operator==(const FDataVertex& LHS, const FDataVertex& RHS);

		/** Check if two data vertex specs are unequal. */
		friend bool METASOUNDGRAPHCORE_API operator!=(const FDataVertex& LHS, const FDataVertex& RHS);
	};

	struct FInputDataVertex: FDataVertex
	{
		using FDataVertex::FDataVertex;
	};

	struct FOutputDataVertex : FDataVertex
	{
		using FDataVertex::FDataVertex;
	};


	/** Create an input data vertex for a DataType. DataType refers to the C++
	 * type used to specialize the TMetasoundDataTypeInfo<> struct.
	 *
	 * @param InVertexName - Name of the input data vertex.
	 * @param InDescription - Description of the input data vertex.
	 *
	 * @return A new FInputDataVertex.
	 */
	template<typename DataType>
	FInputDataVertex MakeInputDataVertex(const FString& InVertexName, const FText& InDescription)
	{
		static const FName DataTypeName = FName(TDataReferenceTypeInfo<DataType>::TypeName);

		return FInputDataVertex(InVertexName, DataTypeName, InDescription);
	}

	/** Create an output data vertex for a DataType. DataType refers to the C++
	 * type used to specialize the TMetasoundDataTypeInfo<> struct.
	 *
	 * @param InVertexName - Name of the output data vertex.
	 * @param InDescription - Description of the output data vertex.
	 *
	 * @return A new FOutputDataVertex.
	 */
	template<typename DataType>
	FOutputDataVertex MakeOutputDataVertex(const FString& InVertexName, const FText& InDescription)
	{
		static const FName DataTypeName = FName(TDataReferenceTypeInfo<DataType>::TypeName);

		return FOutputDataVertex(InVertexName, DataTypeName, InDescription);
	}

	/** Key type for an FInputDataVertexColletion or 
	 * FOutputDataVertexCollection. 
	 */
	typedef FString FDataVertexKey;

	/** FInputDataVertexCollection contains multiple FInputDataVertexes mapped
	 * by FDataVertexKeys.
	 */
	typedef TMap<FDataVertexKey, FInputDataVertex> FInputDataVertexCollection;

	/** FOutputDataVertexCollection contains multiple FOutputDataVertexes mapped
	 * by FDataVertexKeys.
	 */
	typedef TMap<FDataVertexKey, FOutputDataVertex> FOutputDataVertexCollection;

	/** Create an FDataVertexKey from a FInputDataVertex. */
	FORCEINLINE FDataVertexKey MakeDataVertexKey(const FInputDataVertex& InVertex)
	{
		return InVertex.VertexName;
	}

	/** Create an FDataVertexKey from a FOutputDataVertex. */
	FORCEINLINE FDataVertexKey MakeDataVertexKey(const FOutputDataVertex& InVertex)
	{
		return InVertex.VertexName;
	}

	/** INodeBase
	 * 
	 * Interface for all nodes that can describe their name, type, inputs and outputs.
	 */
	class INodeBase
	{
		public:
			virtual ~INodeBase() {}

			/** Return the name of this node. */
			virtual const FString& GetDescription() const = 0;

			/** Return the type name of this node. */
			virtual const FName& GetClassName() const = 0;

			/** Return a collection of input parameter descriptions for this node. */
			virtual const FInputDataVertexCollection& GetInputDataVertices() const = 0;

			/** Return a collection of output parameter descriptions for this node. */
			virtual const FOutputDataVertexCollection& GetOutputDataVertices() const = 0;
	};

	// Forward declare
	class IOperatorFactory;

	/** INode 
	 * 
	 * Interface for all nodes that can create operators. 
	 */
	class INode : public INodeBase
	{
		public:
			virtual ~INode() {}

			// TODO: consider making this return a sharedref to allow for more flexible management of factories. 
			/** Return a reference to the default operator factory. */
			virtual IOperatorFactory& GetDefaultOperatorFactory() = 0;

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
		INode* Node = nullptr;

		/** Output data vertex. */
		FOutputDataVertex Vertex;

		FOutputDataSource()
		{
		}

		FOutputDataSource(INode& InNode, const FOutputDataVertex& InVertex)
		:	Node(&InNode)
		,	Vertex(InVertex)
		{
		}
	};

	/** FInputDataSource describes the destination of data which produced 
	 * external to the graph and read internal to the graph.
	 */
	struct FInputDataDestination
	{
		/** Node containing the input data vertex. */
		INode* Node = nullptr;

		/** Input data vertex of edge. */
		FInputDataVertex Vertex;

		FInputDataDestination()
		{
		}

		FInputDataDestination(INode& InNode, const FInputDataVertex& InVertex)
		:	Node(&InNode)
		,	Vertex(InVertex)
		{
		}
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
		return FNodeDataVertexKey(InSource.Node, InSource.Vertex.VertexName);
	}

	FORCEINLINE FNodeDataVertexKey MakeDestinationDataVertexKey(const FInputDataDestination& InDestination)
	{
		return FNodeDataVertexKey(InDestination.Node, InDestination.Vertex.VertexName);
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


	// This one is not yet implemented. It's design will likely be driven by UX and object management requirements. 
	class INodeFactory
	{
		public:
			virtual ~INodeFactory() {}
			virtual TUniquePtr<INode> CreateNode(const FString& InJsonString) = 0;
	};
}
