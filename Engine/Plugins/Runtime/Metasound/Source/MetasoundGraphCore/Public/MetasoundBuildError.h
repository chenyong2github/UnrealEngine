// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "MetasoundBuilderInterface.h"
#include "MetasoundNodeInterface.h"
#include "MetasoundOperatorInterface.h"

namespace Metasound
{
	/** FBuildErrorBase
	 *
	 * A general build error which contains a error type and human readable 
	 * error description.
	 */
	class METASOUNDGRAPHCORE_API FBuildErrorBase : public IOperatorBuildError
	{
		public:

			FBuildErrorBase(const FName& InErrorType, const FText& InErrorDescription);

			virtual ~FBuildErrorBase() = default;

			/** Returns the type of error. */
			virtual const FName& GetErrorType() const override;

			/** Returns a human readable error description. */
			virtual const FText& GetErrorDescription() const override;

			/** Returns an array of edges associated with the error. */
			virtual const TArray<FDataEdge>& GetEdges() const override;

			/** Returns an array of Nodes associated with the error. */
			virtual const TArray<const INode*>& GetNodes() const override;

		protected:

			// Add edges to be associated with error.
			void AddEdge(const FDataEdge& InEdge);
			void AddEdges(TArrayView<const FDataEdge> InEdges);

			// Add nodes to be associated with error.
			void AddNode(const INode& InNode);
			void AddNodes(TArrayView<INode const* const> InNodes);

		private:

			FName ErrorType;
			FText ErrorDescription;

			TArray<const INode*> Nodes;
			TArray<FDataEdge> Edges;
	};

	/** FDanglingEdgeError
	 *
	 * Caused by FDataEdges pointing to invalid nodes.
	 */
	class METASOUNDGRAPHCORE_API FDanglingEdgeError : public FBuildErrorBase
	{
		public:
			static const FName ErrorType;

			FDanglingEdgeError(const FDataEdge& InEdge);

			virtual ~FDanglingEdgeError() = default;

		private:
	};

	/** FGraphCycleError
	 *
	 * Caused by circular paths in graph.
	 */
	class METASOUNDGRAPHCORE_API FGraphCycleError : public FBuildErrorBase
	{
		public:
			static const FName ErrorType;

			FGraphCycleError(TArrayView<INode const* const> InNodes, const TArray<FDataEdge>& InEdges);

			virtual ~FGraphCycleError() = default;

		private:
	};

	/** FInternalError
	 *
	 * Caused by internal state or logic errors. 
	 */
	class METASOUNDGRAPHCORE_API FInternalError : public FBuildErrorBase
	{
		public:
			static const FName ErrorType;

			FInternalError(const FString& InFileName, int32 InLineNumber);

			virtual ~FInternalError() = default;

			const FString& GetFileName() const;
			int32 GetLineNumber() const;

		private:
			FString FileName;
			int32 LineNumber;
	};

	/** FMissingInputDataReferenceError
	 *
	 * Caused by IOperators not exposing expected IDataReferences in their input
	 * FDataReferenceCollection
	 */
	class METASOUNDGRAPHCORE_API FMissingInputDataReferenceError : public FBuildErrorBase
	{
		public:

			static const FName ErrorType;
			
			FMissingInputDataReferenceError(const FInputDataDestination& InInputDataDestination);

			virtual ~FMissingInputDataReferenceError() = default;

			const FInputDataDestination& GetInputDataDestination() const;

		private:
			FInputDataDestination InputDataDestination;
	};

	/** FMissingOutputDataReferenceError
	 *
	 * Caused by IOperators not exposing expected IDataReferences in their output
	 * FDataReferenceCollection
	 */
	class METASOUNDGRAPHCORE_API FMissingOutputDataReferenceError : public FBuildErrorBase
	{
		public:

			static const FName ErrorType;
			
			FMissingOutputDataReferenceError(const FOutputDataSource& InOutputDataSource);

			virtual ~FMissingOutputDataReferenceError() = default;

			const FOutputDataSource& GetOutputDataSource() const;

		private:
			FOutputDataSource OutputDataSource;
	};

	/** FInvalidConnectionDataTypeError
	 *
	 * Caused when edges describe a connection between vertices with different
	 * data types.
	 */
	class METASOUNDGRAPHCORE_API FInvalidConnectionDataTypeError  : public FBuildErrorBase
	{
		public:

			static const FName ErrorType;

			FInvalidConnectionDataTypeError(const FDataEdge& InEdge);

			virtual ~FInvalidConnectionDataTypeError() = default;
	};
}
