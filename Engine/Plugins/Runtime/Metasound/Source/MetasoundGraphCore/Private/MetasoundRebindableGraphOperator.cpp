// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundRebindableGraphOperator.h"

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
	FRebindableGraphOperator::FRebindableGraphOperator(TUniquePtr<DirectedGraphAlgo::FGraphOperatorData> InOperatorData)
	: GraphOperatorData(MoveTemp(InOperatorData))
	{
		using namespace DirectedGraphAlgo;

		if (GraphOperatorData.IsValid())
		{
			for (DirectedGraphAlgo::FOperatorID OperatorID : GraphOperatorData->OperatorOrder)
			{
				if (FGraphOperatorData::FOperatorInfo* OperatorInfo = GraphOperatorData->OperatorMap.Find(OperatorID))
				{
					check(OperatorInfo->Operator.IsValid());
					IOperator& Operator = *(OperatorInfo->Operator);

					if (IOperator::FExecuteFunction ExecuteFunc = Operator.GetExecuteFunction())
					{
						ExecuteStack.Emplace(FExecuteEntry{OperatorID, Operator, ExecuteFunc});
					}

					if (IOperator::FPostExecuteFunction PostExecuteFunc = Operator.GetPostExecuteFunction())
					{
						PostExecuteStack.Emplace(FPostExecuteEntry{OperatorID, Operator, PostExecuteFunc});
					}

					if (IOperator::FResetFunction ResetFunc = Operator.GetResetFunction())
					{
						ResetStack.Emplace(FResetEntry{OperatorID, Operator, ResetFunc});
					}
				}
			}
		}
	}

	FDataReferenceCollection FRebindableGraphOperator::GetInputs() const
	{
		checkNoEntry();
		return FDataReferenceCollection();
	}

	FDataReferenceCollection FRebindableGraphOperator::GetOutputs() const
	{
		checkNoEntry();
		return FDataReferenceCollection();
	}

	// Bind the graph's interface data references to FVertexInterfaceData.
	void FRebindableGraphOperator::BindInputs(FInputVertexInterfaceData& InOutVertexData)
	{
		using namespace DirectedGraphAlgo;

		if (GraphOperatorData.IsValid())
		{
			auto UpdateStackOnRebind = [&](FOperatorID InOperatorID, IOperator& InOperator) 
			{ 
				UpdateStacks(InOperatorID, InOperator);
			};

			FRebindGraphDataParams Params
			{
				GraphOperatorData->VertexData,
				GraphOperatorData->OperatorMap,
				GraphOperatorData->InputVertexMap,
				GraphOperatorData->OutputVertexMap
			};

			RebindGraphInputs(InOutVertexData, Params, UpdateStackOnRebind);
		}
	}

	void FRebindableGraphOperator::BindOutputs(FOutputVertexInterfaceData& InOutVertexData)
	{
		using namespace DirectedGraphAlgo;
		if (GraphOperatorData.IsValid())
		{
			auto UpdateStackOnRebind = [&](FOperatorID InOperatorID, IOperator& InOperator) 
			{ 
				UpdateStacks(InOperatorID, InOperator);
			};

			FRebindGraphDataParams Params
			{
				GraphOperatorData->VertexData,
				GraphOperatorData->OperatorMap,
				GraphOperatorData->InputVertexMap,
				GraphOperatorData->OutputVertexMap
			};

			RebindGraphOutputs(InOutVertexData, Params, UpdateStackOnRebind);
		}
	}


	void FRebindableGraphOperator::Execute()
	{
		for (FExecuteEntry& Entry : ExecuteStack)
		{
			Entry.Execute();
		}
	}

	void FRebindableGraphOperator::PostExecute()
	{
		for (FPostExecuteEntry& Entry : PostExecuteStack)
		{
			Entry.PostExecute();
		}
	}

	void FRebindableGraphOperator::Reset(const IOperator::FResetParams& InParams)
	{
		for (FResetEntry& Entry : ResetStack)
		{
			Entry.Reset(InParams);
		}
	}

	IOperator::FPostExecuteFunction FRebindableGraphOperator::GetPostExecuteFunction() 
	{
		return &StaticPostExecute;
	}

	void FRebindableGraphOperator::StaticPostExecute(IOperator* InOperator)
	{
		check(InOperator);
		static_cast<FRebindableGraphOperator*>(InOperator)->PostExecute();
	}

	template<typename EntryType, typename FunctionType>
	void UpdateOperatorAndStack(const TArray<DirectedGraphAlgo::FOperatorID>& InOperatorOrder, TArray<EntryType>& InStack, DirectedGraphAlgo::FOperatorID InOperatorID, IOperator& InOperator, FunctionType InFunction)
	{
		using namespace DirectedGraphAlgo;

		int32 EntryIndex = InStack.IndexOfByPredicate([&](const EntryType& InEntry) { return InEntry.OperatorID == InOperatorID; });

		bool bEntryShouldExist = (nullptr != InFunction);
		bool bEntryCurrentlyExists = (EntryIndex != INDEX_NONE);

		if (bEntryCurrentlyExists && bEntryShouldExist)
		{
			// Update the existing entry
			InStack[EntryIndex].Function = InFunction;
		}
		else if (bEntryCurrentlyExists)
		{
			// Remove the existing entry
			InStack.RemoveAt(EntryIndex);
		}
		else if (bEntryShouldExist)
		{
			// Add new entry
			int32 OperatorIndex = InOperatorOrder.Find(InOperatorID);
			if (INDEX_NONE == OperatorIndex)
			{
				UE_LOG(LogMetaSound, Error, TEXT("Failed to update operator stack. OperatorID not found in operator order array."));
				return;
			}

			int32 PreceedingOperatorIndex = OperatorIndex - 1;
			int32 InsertLocation = 0; // Default to inserting at the beginning
			while (PreceedingOperatorIndex >= 0)
			{
				FOperatorID PreceedingOperatorID = InOperatorOrder[PreceedingOperatorIndex];
				PreceedingOperatorIndex--;

				int32 PreceedingEntryIndex = InStack.IndexOfByPredicate([&](const EntryType& InEntry) { return InEntry.OperatorID == PreceedingOperatorID; });
				if (PreceedingEntryIndex != INDEX_NONE)
				{
					InsertLocation = PreceedingEntryIndex + 1;
					break;
				}
			}

			InStack.Insert(EntryType{InOperatorID, InOperator, InFunction}, InsertLocation);
		}
	}

	void FRebindableGraphOperator::UpdateStacks(DirectedGraphAlgo::FOperatorID InOperatorID, IOperator& InOperator)
	{
		check(GraphOperatorData);
		UpdateOperatorAndStack(GraphOperatorData->OperatorOrder, ExecuteStack, InOperatorID, InOperator, InOperator.GetExecuteFunction());
		UpdateOperatorAndStack(GraphOperatorData->OperatorOrder, PostExecuteStack, InOperatorID, InOperator, InOperator.GetPostExecuteFunction());
		UpdateOperatorAndStack(GraphOperatorData->OperatorOrder, ResetStack, InOperatorID, InOperator, InOperator.GetResetFunction());
	}

	FRebindableGraphOperator::FExecuteEntry::FExecuteEntry(DirectedGraphAlgo::FOperatorID InOperatorID, IOperator& InOperator, IOperator::FExecuteFunction InFunc)
	: OperatorID(InOperatorID)
	, Operator(&InOperator)
	, Function(InFunc)
	{
	}

	FRebindableGraphOperator::FPostExecuteEntry::FPostExecuteEntry(DirectedGraphAlgo::FOperatorID InOperatorID, IOperator& InOperator, IOperator::FPostExecuteFunction InFunc)
	: OperatorID(InOperatorID)
	, Operator(&InOperator)
	, Function(InFunc)
	{
	}

	FRebindableGraphOperator::FResetEntry::FResetEntry(DirectedGraphAlgo::FOperatorID InOperatorID, IOperator& InOperator, IOperator::FResetFunction InFunc)
	: OperatorID(InOperatorID)
	, Operator(&InOperator)
	, Function(InFunc)
	{
	}
}


