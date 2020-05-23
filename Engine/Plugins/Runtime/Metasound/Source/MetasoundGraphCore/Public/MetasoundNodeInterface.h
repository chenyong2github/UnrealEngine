// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundDataReference.h"

namespace Metasound
{
	/** FDataVertexDescription
	 *
	 *  This describes a data vertex on a Node.
	 */
	struct METASOUNDGRAPHCORE_API FDataVertexDescription
	{
		/** Name of parameteer. */
		FString VertexName;

		/** Type name of parmaeter. */
		FName DataReferenceTypeName;

		/** Tooltip for parameter. */
		FText Tooltip;

		FDataVertexDescription()
		:	Tooltip(FText::GetEmpty())
		{
		}

		FDataVertexDescription(const FString& InVertexName, const FName& InDataReferenceTypeName, const FText& InTooltip)
		:	VertexName(InVertexName)
		,	DataReferenceTypeName(InDataReferenceTypeName)
		,	Tooltip(InTooltip)
		{
		}

		/** Check if two parameter descriptions are equal. */
		friend bool METASOUNDGRAPHCORE_API operator==(const FDataVertexDescription& LHS, const FDataVertexDescription& RHS);

		/** Check if two parameter descriptions are unequal. */
		friend bool METASOUNDGRAPHCORE_API operator!=(const FDataVertexDescription& LHS, const FDataVertexDescription& RHS);
	};

	struct FInputDataVertexDescription : FDataVertexDescription
	{
		using FDataVertexDescription::FDataVertexDescription;
	};

	struct FOutputDataVertexDescription : FDataVertexDescription
	{
		using FDataVertexDescription::FDataVertexDescription;
	};


	template<typename DataType>
	FInputDataVertexDescription MakeInputDataVertexDescription(const FString& InVertexName, const FText& InTooltip)
	{
		static const FName DataTypeName = FName(TDataReferenceTypeInfo<DataType>::TypeName);

		return FInputDataVertexDescription(InVertexName, DataTypeName, InTooltip);
	}

	template<typename DataType>
	FOutputDataVertexDescription MakeOutputDataVertexDescription(const FString& InVertexName, const FText& InTooltip)
	{
		static const FName DataTypeName = FName(TDataReferenceTypeInfo<DataType>::TypeName);

		return FOutputDataVertexDescription(InVertexName, DataTypeName, InTooltip);
	}


	// Forward declare.
	class INode;

	/** Represents a specific input data vertex from a specific node. */
	struct METASOUNDGRAPHCORE_API FInputDataVertex
	{
		/** Pointer to the node associated with the vertex. */
		INode* Node;

		/** The description of the Node's vertex. */
		FInputDataVertexDescription Description;

		FInputDataVertex()
		:	Node(nullptr)
		{
		}

		FInputDataVertex(INode* InNode, const FInputDataVertexDescription& InDescription)
		:	Node(InNode)
		,	Description(InDescription)
		{
		}
	};

	/** Represents a specific output data vertex from a specific node. */
	struct METASOUNDGRAPHCORE_API FOutputDataVertex
	{
		/** Pointer to the node associated with the vertex. */
		INode* Node;

		/** The description of the Node's vertex. */
		FOutputDataVertexDescription Description;

		FOutputDataVertex()
		:	Node(nullptr)
		{
		}

		FOutputDataVertex(INode* InNode, const FOutputDataVertexDescription& InDescription)
		:	Node(InNode)
		,	Description(InDescription)
		{
		}
	};

	/** FDataEdge
	 *
	 * An edge describes a connection between two Nodes. 
	 */
	struct METASOUNDGRAPHCORE_API FDataEdge
	{
		/** Vertex producing the parameter. */
		FOutputDataVertex From;

		/** Vertex consuming the parameter. */
		FInputDataVertex To;

		FDataEdge()
		{
		}

		FDataEdge(const FOutputDataVertex& InFrom, const FInputDataVertex& InTo)
		:	From(InFrom)
		,	To(InTo)
		{
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

			/** Return the name of this node. */
			virtual const FString& GetDescription() const = 0;

			/** Return the type name of this node. */
			virtual const FName& GetClassName() const = 0;

			/** Return an array of input parameter descriptions for this node. */
			virtual const TArray<FInputDataVertexDescription>& GetInputs() const = 0;

			/** Return an array of output parameter descriptions for this node. */
			virtual const TArray<FOutputDataVertexDescription>& GetOutputs() const = 0;
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

			/** Return a reference to the default operator factory. */
			virtual IOperatorFactory& GetDefaultOperatorFactory() = 0;

			/* Future implementations may support additional factory types and interfaces
				virtual bool DoesSupportOperatorFactory(const FString& InFractoryName) const = 0;
				virtual IOperatorFactory* GetOperatorFactory(const FString& InFactoryName) = 0;
				virtual ISpecialFactory* GetSpecialFactory() { return nullptr; }
			*/
	};

	/** Interface for graph of nodes. */
	class IGraph : public INodeBase
	{
		public:
			virtual ~IGraph() {}

			/** Retrieve all the edges associated with a graph. */
			virtual const TArray<FDataEdge>& GetDataEdges() const = 0;

			/** Get vertices which contain input parameters. */
			virtual const TArray<FInputDataVertex>& GetInputDataVertices() const = 0;

			/** Get vertices which contain output parameters. */
			virtual const TArray<FOutputDataVertex>& GetOutputDataVertices() const = 0;
	};


	// This one is not yet implemented. It's design will likely be driven by UX and object management requirements. 
	class INodeFactory
	{
		public:
			virtual ~INodeFactory() {}
			virtual TUniquePtr<INode> CreateNode(const FString& InJsonString) = 0;
	};

}
