// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Containers/SortedMap.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundGraphAlgoPrivate.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundVertex.h"
#include "MetasoundVertexData.h"
#include "Misc/Guid.h"
#include "Templates/UniquePtr.h"

namespace Metasound
{
	class FRebindableGraphOperator : public TExecutableOperator<FRebindableGraphOperator>
	{
	public:

		FRebindableGraphOperator(TUniquePtr<DirectedGraphAlgo::FGraphOperatorData> InOperatorState);

		virtual FDataReferenceCollection GetInputs() const override;

		virtual FDataReferenceCollection GetOutputs() const override;

		// Bind the graph's interface data references to FVertexInterfaceData.
		virtual void BindInputs(FInputVertexInterfaceData& InOutVertexData) override;
		virtual void BindOutputs(FOutputVertexInterfaceData& InOutVertexData) override;

		void Execute();
		void PostExecute();
		void Reset(const IOperator::FResetParams& InParams);

		virtual IOperator::FPostExecuteFunction GetPostExecuteFunction() override;

	private:
		static void StaticPostExecute(IOperator* InOperator);

		void UpdateStacks(DirectedGraphAlgo::FOperatorID InOperatorID, IOperator& InOperator);

		struct FExecuteEntry
		{
			FExecuteEntry(DirectedGraphAlgo::FOperatorID InOperatorID, IOperator& InOperator, IOperator::FExecuteFunction InFunc);

			void Execute()
			{
				check(Operator);
				check(Function);
				Function(Operator);
			}

			DirectedGraphAlgo::FOperatorID OperatorID;
			IOperator* Operator;
			IOperator::FExecuteFunction Function;	
		};

		struct FPostExecuteEntry
		{
			FPostExecuteEntry(DirectedGraphAlgo::FOperatorID InOperatorID, IOperator& InOperator, IOperator::FPostExecuteFunction InFunc);

			void PostExecute()
			{
				check(Operator);
				check(Function);
				Function(Operator);
			}

			DirectedGraphAlgo::FOperatorID OperatorID;
			IOperator* Operator;
			IOperator::FPostExecuteFunction Function;	
		};

		struct FResetEntry
		{
			FResetEntry(DirectedGraphAlgo::FOperatorID InOperatorID, IOperator& InOperator, IOperator::FResetFunction InFunc);

			void Reset(const IOperator::FResetParams& InParams)
			{
				check(Operator);
				check(Function);
				Function(Operator, InParams);
			}

			DirectedGraphAlgo::FOperatorID OperatorID;
			IOperator* Operator;
			IOperator::FResetFunction Function;	
		};

		TArray<FExecuteEntry> ExecuteStack;
		TArray<FPostExecuteEntry> PostExecuteStack;
		TArray<FResetEntry> ResetStack;

		TUniquePtr<DirectedGraphAlgo::FGraphOperatorData> GraphOperatorData;
	};
}

