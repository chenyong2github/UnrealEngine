// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundGraphOperator.h"

#include "CoreMinimal.h"
#include "MetasoundDataReference.h"
#include "MetasoundDataReferenceCollection.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundExecutableOperator.h"

namespace Metasound
{
	void FGraphOperator::AppendOperator(FOperatorPtr InOperator)
	{
		if (InOperator.IsValid())
		{
			bool bIsOperatorInAnyStack = false;

			if (FExecuteFunction ExecuteFunc = InOperator->GetExecuteFunction())
			{
				ExecuteStack.Emplace(*InOperator, ExecuteFunc);
				bIsOperatorInAnyStack = true;
			}

			if (FPostExecuteFunction PostExecuteFunc = InOperator->GetPostExecuteFunction())
			{
				PostExecuteStack.Emplace(*InOperator, PostExecuteFunc);
				bIsOperatorInAnyStack = true;
			}

			if (FResetFunction ResetFunc = InOperator->GetResetFunction())
			{
				ResetStack.Emplace(*InOperator, ResetFunc);
				bIsOperatorInAnyStack = true;
			}

			if (bIsOperatorInAnyStack)
			{
				ActiveOperators.Add(MoveTemp(InOperator));
			}
		}
	}

	void FGraphOperator::SetInputs(const FDataReferenceCollection& InCollection)
	{
		VertexData.GetInputs().Bind(InCollection);
	}

	void FGraphOperator::SetOutputs(const FDataReferenceCollection& InCollection)
	{
		VertexData.GetOutputs().Bind(InCollection);
	}

	void FGraphOperator::SetVertexInterfaceData(FVertexInterfaceData&& InVertexData)
	{
		VertexData = InVertexData;
	}

	FDataReferenceCollection FGraphOperator::GetInputs() const
	{
		return VertexData.GetInputs().ToDataReferenceCollection();
	}

	FDataReferenceCollection FGraphOperator::GetOutputs() const
	{
		return VertexData.GetOutputs().ToDataReferenceCollection();
	}

	void FGraphOperator::Bind(FVertexInterfaceData& InVertexData) const
	{
		InVertexData.Bind(VertexData);
	}

	IOperator::FPostExecuteFunction FGraphOperator::GetPostExecuteFunction()
	{
		return &StaticPostExecute;
	}

	void FGraphOperator::StaticPostExecute(IOperator* InOperator)
	{
		FGraphOperator* GraphOperator = static_cast<FGraphOperator*>(InOperator);
		check(GraphOperator);

		GraphOperator->PostExecute();
	}

	void FGraphOperator::Execute()
	{
		FExecuteEntry* StackPtr = ExecuteStack.GetData();
		const int32 Num = ExecuteStack.Num();
		for (int32 i = 0; i < Num; i++)
		{
			StackPtr[i].Execute();
		}
	}

	void FGraphOperator::PostExecute()
	{
		FPostExecuteEntry* StackPtr = PostExecuteStack.GetData();
		const int32 Num = PostExecuteStack.Num();
		for (int32 i = 0; i < Num; i++)
		{
			StackPtr[i].PostExecute();
		}
	}

	void FGraphOperator::Reset(const FGraphOperator::FResetParams& InParams)
	{
		FResetEntry* StackPtr = ResetStack.GetData();
		const int32 Num = ResetStack.Num();
		for (int32 i = 0; i < Num; i++)
		{
			StackPtr[i].Reset(InParams);
		}
	}

	FGraphOperator::FExecuteEntry::FExecuteEntry(IOperator& InOperator, FExecuteFunction InFunc)
	: Operator(&InOperator)
	, Function(InFunc)
	{
		check(Function);
	}

	void FGraphOperator::FExecuteEntry::Execute()
	{
		Function(Operator);
	}

	FGraphOperator::FPostExecuteEntry::FPostExecuteEntry(IOperator& InOperator, FPostExecuteFunction InFunc)
	: Operator(&InOperator)
	, Function(InFunc)
	{
		check(Function);
	}

	void FGraphOperator::FPostExecuteEntry::PostExecute()
	{
		Function(Operator);
	}

	FGraphOperator::FResetEntry::FResetEntry(IOperator& InOperator, FResetFunction InFunc)
	: Operator(&InOperator)
	, Function(InFunc)
	{
		check(Function);
	}

	void FGraphOperator::FResetEntry::Reset(const FGraphOperator::FResetParams& InParams)
	{
		Function(Operator, InParams);
	}

			static void StaticPostExecute(IOperator* Operator);

}
