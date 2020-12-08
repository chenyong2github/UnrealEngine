// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundDataReference.h"
#include "MetasoundDataReferenceCollection.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundExecutableOperator.h"

namespace Metasound
{
	class METASOUNDGRAPHCORE_API FGraphOperator : public IOperator
	{
		public:
			using FOperatorPtr = TUniquePtr<IOperator>;
			using FExecuteFunction = IOperator::FExecuteFunction;

			FGraphOperator() = default;

			virtual ~FGraphOperator();

			void AppendOperator(FOperatorPtr InOperator);

			void SetInputs(const FDataReferenceCollection& InCollection);

			void SetOutputs(const FDataReferenceCollection& InCollection);

			virtual FDataReferenceCollection GetInputs() const override;

			virtual FDataReferenceCollection GetOutputs() const override;

			virtual FExecuteFunction GetExecuteFunction() override;

			void Execute();

		private:
			// Delete copy operator because underlying types cannot be copied. 
			FGraphOperator& operator=(const FGraphOperator&) = delete;
			FGraphOperator(const FGraphOperator&) = delete;
			

			static void ExecuteFunction(IOperator* InOperator);

			TArray<FExecuter> OperatorStack;

			FDataReferenceCollection Inputs;
			FDataReferenceCollection Outputs;
	};
}
