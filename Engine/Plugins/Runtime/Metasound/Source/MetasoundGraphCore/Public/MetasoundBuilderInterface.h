// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundNodeInterface.h"
#include "MetasoundOperatorInterface.h"

namespace Metasound
{
	/** IOperatorBuildError
	 *
	 * This interface is intended for errors encountered when building an GraphOperator.
	 */
	class IOperatorBuildError
	{
		public:
			virtual ~IOperatorBuildError() = default;

			/** Returns the type of error. */
			virtual const FName& GetErrorType() const = 0;

			/** Returns a human readable error description. */
			virtual const FText& GetErrorDescription() const = 0;

			/** Returns an array of destinations associated with the error. */
			virtual const TArray<FInputDataDestination>& GetDestinations() const = 0;
			
			/** Returns an array of sources associated with the error. */
			virtual const TArray<FOutputDataSource>& GetSources() const = 0;

			/** Returns an array of Nodes associated with the error. */
			virtual const TArray<const INode*>& GetNodes() const = 0;

			/** Returns an array of edges associated with the error. */
			virtual const TArray<FDataEdge>& GetEdges() const = 0;
	};


	/** IOperatorFactory
	 *
	 * IOperatorFactory defines an interface for building an IOperator from an INode.  In practice,
	 * each INode returns its own IOperatorFactory through the INode::GetDefaultOperatorFactory() 
	 * member function.
	 */
	class IOperatorFactory
	{
		public:
			virtual ~IOperatorFactory() = default;

			/** Create a new IOperator.
			 *
			 * @param InNode - The node associated with this factory and the desired IOperator.
			 * @param FOperatorSettings - General operator settings for the graph.
			 * @param InInputCollection - Collection of input parameters available for to this IOperator.
			 * @param OutErrors - An array of errors. Errors can be added if issues occur while creating the IOperator.
			 *
			 * @return A unique pointer to an IOperator. 
			 */
			virtual TUniquePtr<IOperator> CreateOperator(const INode& InNode, const FOperatorSettings& InOperatorSettings, const FDataReferenceCollection& InInputDataReferences, TArray<TUniquePtr<IOperatorBuildError>>& OutErrors) = 0;
	};


	/** IOperatorBuilder
	 *
	 * Defines an interface for building a graph of operators from a graph of nodes. 
	 */
	class IOperatorBuilder 
	{
		public:
			/** A TUniquePtr of an IOperatorBuildError */
			using FBuildErrorPtr = TUniquePtr<IOperatorBuildError>;

			virtual ~IOperatorBuilder() = default;

			/** Build a graph operator from a graph. 
			 *
			 * @params InGraph - The input graph object containing edges, input vertices and output vertices.
			 * @param OutErrors - An array of errors. Errors can be added if issues occur while creating the IOperator.
			 *
			 * @return A unique pointer to an IOperator. 
			 */
			virtual TUniquePtr<IOperator> BuildGraphOperator(const IGraph& InGraph, TArray<FBuildErrorPtr>& OutErrors) = 0;
	};
}

