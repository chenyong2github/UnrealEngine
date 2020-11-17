// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundEnvironment.h"
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
			virtual const TArray<FInputDataDestination>& GetInputDataDestinations() const = 0;
			
			/** Returns an array of sources associated with the error. */
			virtual const TArray<FOutputDataSource>& GetOutputDataSources() const = 0;

			/** Returns an array of Nodes associated with the error. */
			virtual const TArray<const INode*>& GetNodes() const = 0;

			/** Returns an array of edges associated with the error. */
			virtual const TArray<FDataEdge>& GetDataEdges() const = 0;
	};

	/** FCreateOperatorParams holds the parameters provided to operator factories
	 * during the creation of an IOperator
	 */
	struct FCreateOperatorParams
	{
		/** The node associated with this factory and the desired IOperator. */
		const INode& Node;

		/** General operator settings for the graph. */
		const FOperatorSettings& OperatorSettings;

		/** Collection of input parameters available for to an IOperator. */
		const FDataReferenceCollection& InputDataReferences;

		/** Environment settings available. */
		const FMetasoundEnvironment& Environment;

		FCreateOperatorParams(const INode& InNode, const FOperatorSettings& InOperatorSettings, const FDataReferenceCollection& InInputDataReferences, const FMetasoundEnvironment& InEnvironment)
		:	Node(InNode)
		,	OperatorSettings(InOperatorSettings)
		,	InputDataReferences(InInputDataReferences)
		,	Environment(InEnvironment)
		{
		}
	};

	typedef TArray<TUniquePtr<IOperatorBuildError>> FBuildErrorArray;

	/** Convenience template for adding build errors.
	 *
	 * The function can be used in the following way:
	 * 
	 * FBuildErrorArray MyErrorArray;
	 * AddBuildError<FMyBuildErrorType>(MyErrorArray, MyBuildErrorConstructorArgs...);
	 *
	 * @param OutErrors - Array which holds the errors.
	 * @param Args - Constructor arguments for the error.
	 */
	template<typename ErrorType, typename... ArgTypes>
	void AddBuildError(FBuildErrorArray& OutErrors, ArgTypes&&... Args)
	{
		OutErrors.Add(MakeUnique<ErrorType>(Forward<ArgTypes>(Args)...));
	}

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
			 * @param InParams - The parameters available for building an IOperator.
			 * @param OutErrors - An array of errors. Errors can be added if issues occur while creating the IOperator.
			 *
			 * @return A unique pointer to an IOperator. 
			 */
			virtual TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors) = 0;
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
			 * @param InOperatorSettings - Settings to be passed to all operators on creation.
			 * @param InEnvironment - The environment variables to use during construction. 
			 * @param OutErrors - An array of errors. Errors can be added if issues occur while creating the IOperator.
			 *
			 * @return A unique pointer to an IOperator. 
			 */
			virtual TUniquePtr<IOperator> BuildGraphOperator(const IGraph& InGraph, const FOperatorSettings& InOperatorSettings, const FMetasoundEnvironment& InEnvironment, TArray<FBuildErrorPtr>& OutErrors) = 0;
	};
}

