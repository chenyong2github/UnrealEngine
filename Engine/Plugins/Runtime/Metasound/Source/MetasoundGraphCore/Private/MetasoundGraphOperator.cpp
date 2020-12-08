// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundGraphOperator.h"

#include "CoreMinimal.h"
#include "MetasoundDataReference.h"
#include "MetasoundDataReferenceCollection.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundExecutableOperator.h"

namespace Metasound
{

	FGraphOperator::~FGraphOperator()
	{
	}

	void FGraphOperator::AppendOperator(FOperatorPtr InOperator)
	{
		if (InOperator.IsValid())
		{
			if (nullptr != InOperator->GetExecuteFunction())
			{
				OperatorStack.Emplace(MoveTemp(InOperator));
			}
		}
	}

	void FGraphOperator::SetInputs(const FDataReferenceCollection& InCollection)
	{
		Inputs = InCollection;
	}

	void FGraphOperator::SetOutputs(const FDataReferenceCollection& InCollection)
	{
		Outputs = InCollection;
	}

	FDataReferenceCollection FGraphOperator::GetInputs() const
	{
		return Inputs;
	}

	FDataReferenceCollection FGraphOperator::GetOutputs() const
	{
		return Outputs;
	}

	IOperator::FExecuteFunction FGraphOperator::GetExecuteFunction()
	{
		return &FGraphOperator::ExecuteFunction;
	}

	void FGraphOperator::Execute()
	{
		FExecuter* StackPtr = OperatorStack.GetData();
		const int32 Num = OperatorStack.Num();
		for (int32 i = 0; i < Num; i++)
		{
			StackPtr[i].Execute();
		}
	}

	void FGraphOperator::ExecuteFunction(IOperator* InOperator)
	{
		FGraphOperator* Operator = static_cast<FGraphOperator*>(InOperator);

		check(nullptr != Operator);

		Operator->Execute();
	}
}
