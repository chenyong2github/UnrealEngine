// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundFrontendQuery.h"

#include "Algo/Sort.h"
#include "CoreMinimal.h"
#include "MetasoundFrontendDataLayout.h"

namespace Metasound
{

	namespace FrontendQueryPrivate
	{
		struct FGenerateFunctionFrontendQueryStep: IFrontendQueryGenerateStep
		{
			using FGenerateFunction = FFrontendQueryStep::FGenerateFunction;

			FGenerateFunctionFrontendQueryStep(FGenerateFunction InFunc)
			:	Func(InFunc)
			{
			}

			void Generate(TArray<FFrontendQueryEntry>& OutEntries) const override
			{
				Func(OutEntries);
			}

			private:

			FGenerateFunction Func;
		};

		struct FMapFunctionFrontendQueryStep: IFrontendQueryMapStep
		{
			using FMapFunction = FFrontendQueryStep::FMapFunction;

			FMapFunctionFrontendQueryStep(FMapFunction InFunc)
			:	Func(InFunc)
			{
			}

			FFrontendQueryEntry::FKey Map(const FFrontendQueryEntry& InEntry) const override
			{
				return Func(InEntry);
			}

			FMapFunction Func;
		};

		struct FReduceFunctionFrontendQueryStep: IFrontendQueryReduceStep
		{
			using FReduceFunction = FFrontendQueryStep::FReduceFunction;

			FReduceFunctionFrontendQueryStep(FReduceFunction InFunc)
			:	Func(InFunc)
			{
			}

			void Reduce(FFrontendQueryEntry::FKey InKey, TArrayView<FFrontendQueryEntry*>& InEntries, FReduceOutputView& OutResult) const override
			{
				return Func(InKey, InEntries, OutResult);
			}

			FReduceFunction Func;
		};

		struct FFilterFunctionFrontendQueryStep: IFrontendQueryFilterStep
		{
			using FFilterFunction = FFrontendQueryStep::FFilterFunction;

			FFilterFunctionFrontendQueryStep(FFilterFunction InFunc)
			:	Func(InFunc)
			{
			}

			bool Filter(const FFrontendQueryEntry& InEntry) const override
			{
				return Func(InEntry);
			}

			FFilterFunction Func;
		};

		struct FScoreFunctionFrontendQueryStep: IFrontendQueryScoreStep
		{
			using FScoreFunction = FFrontendQueryStep::FScoreFunction;

			FScoreFunctionFrontendQueryStep(FScoreFunction InFunc)
			:	Func(InFunc)
			{
			}

			float Score(const FFrontendQueryEntry& InEntry) const override
			{
				return Func(InEntry);
			}

			FScoreFunction Func;
		};

		struct FSortFunctionFrontendQueryStep: IFrontendQuerySortStep
		{
			using FSortFunction = FFrontendQueryStep::FSortFunction;

			FSortFunctionFrontendQueryStep(FSortFunction InFunc)
			:	Func(InFunc)
			{
			}

			bool Sort(const FFrontendQueryEntry& InEntryLHS, const FFrontendQueryEntry& InEntryRHS) const override
			{
				return Func(InEntryLHS, InEntryRHS);
			}

			FSortFunction Func;
		};

		struct FLimitFunctionFrontendQueryStep: IFrontendQueryLimitStep
		{
			using FLimitFunction = FFrontendQueryStep::FLimitFunction;

			FLimitFunctionFrontendQueryStep(FLimitFunction InFunc)
			:	Func(InFunc)
			{
			}

			int32 Limit() const override
			{
				return Func();
			}

			FLimitFunction Func;
		};

		template<typename StepType>
		struct TStepExecuter : public FFrontendQueryStep::IStepExecuter
		{
			TStepExecuter(TUniquePtr<StepType>&& InStep)
			:	Step(MoveTemp(InStep))
			{
			}

			TUniquePtr<StepType> Step;
		};


		struct FGenerateStepExecuter : TStepExecuter<IFrontendQueryGenerateStep>
		{
			using TStepExecuter<IFrontendQueryGenerateStep>::TStepExecuter;

			void ExecuteStep(FFrontendQuerySelection& InOutResult) const override
			{
				if (Step.IsValid())
				{
					TArray<FFrontendQueryEntry> NewEntries;

					Step->Generate(NewEntries);

					InOutResult.AppendToStorageAndSelection(NewEntries);
				}
			}
		};

		struct FMapStepExecuter : TStepExecuter<IFrontendQueryMapStep>
		{
			using TStepExecuter<IFrontendQueryMapStep>::TStepExecuter;

			void ExecuteStep(FFrontendQuerySelection& InOutResult) const override
			{
				if (Step.IsValid())
				{
					for (FFrontendQueryEntry* Entry : InOutResult.GetSelection())
					{
						if (nullptr != Entry)
						{
							Entry->Key = Step->Map(*Entry);
						}
					}
				}
			}
		};

		struct FReduceStepExecuter : TStepExecuter<IFrontendQueryReduceStep>
		{
			using TStepExecuter<IFrontendQueryReduceStep>::TStepExecuter;

			void ExecuteStep(FFrontendQuerySelection& InOutResult) const override
			{
				if (Step.IsValid())
				{
					if (InOutResult.GetSelection().Num() > 0)
					{
						TArrayView<FFrontendQueryEntry* > SelectionView = InOutResult.GetSelection();

						TArray<FFrontendQueryEntry*> SortedSelection(SelectionView.GetData(), SelectionView.Num()); 

						InOutResult.ResetSelection();

						Algo::SortBy(SortedSelection, [](const FFrontendQueryEntry* InEntry)
							{
								if (nullptr == InEntry)
								{
									return FFrontendQueryEntry::InvalidKey;
								}

								return InEntry->Key;
							}
						);


						int32 Num = SortedSelection.Num();
						int32 StartIndex = 0;

						FFrontendQueryEntry::FKey CurrentKey = FFrontendQueryEntry::InvalidKey;

						auto ReduceFunc = [&](int32 Index)
						{
							FFrontendQueryEntry::FKey ThisKey = nullptr == SortedSelection[Index] ? FFrontendQueryEntry::InvalidKey : SortedSelection[Index]->Key;

							if (CurrentKey != ThisKey)
							{
								if (Index > StartIndex)
								{
									TArrayView<FFrontendQueryEntry*> ResultsToReduce(&SortedSelection[StartIndex], Index - StartIndex);

									FFrontendQuerySelection::FReduceOutputView ReduceResult(ResultsToReduce);

									Step->Reduce(CurrentKey, ResultsToReduce, ReduceResult);

									InOutResult.AppendToStorageAndSelection(ReduceResult);
								}

								CurrentKey = ThisKey;
								StartIndex = Index;
							}
						};

						for (int32 ResultIndex = 0; ResultIndex < Num; ResultIndex++)
						{
							ReduceFunc(ResultIndex);
						}

						ReduceFunc(Num - 1);
					}
				}
			}
		};

		struct FFilterStepExecuter : TStepExecuter<IFrontendQueryFilterStep>
		{
			using TStepExecuter<IFrontendQueryFilterStep>::TStepExecuter;

			void ExecuteStep(FFrontendQuerySelection& InOutResult) const override
			{
				if (Step.IsValid())
				{
					TArrayView<FFrontendQueryEntry*> Selection = InOutResult.GetSelection();

					InOutResult.ResetSelection();

					InOutResult.AppendToSelection(Selection.FilterByPredicate(
						[&](const FFrontendQueryEntry* Entry)
						{
							return (nullptr != Entry) && Step->Filter(*Entry);
						}
					));
				}
			}
		};

		struct FScoreStepExecuter : TStepExecuter<IFrontendQueryScoreStep>
		{
			using TStepExecuter<IFrontendQueryScoreStep>::TStepExecuter;

			void ExecuteStep(FFrontendQuerySelection& InOutResult) const override
			{
				if (Step.IsValid())
				{
					for (FFrontendQueryEntry* Entry : InOutResult.GetSelection())
					{
						if (nullptr != Entry)
						{
							Entry->Score = Step->Score(*Entry);
						}
					}
				}
			}
		};
		
		struct FSortStepExecuter : TStepExecuter<IFrontendQuerySortStep>
		{
			using TStepExecuter<IFrontendQuerySortStep>::TStepExecuter;

			void ExecuteStep(FFrontendQuerySelection& InOutResult) const override
			{
				if (Step.IsValid())
				{
					InOutResult.GetSelection().Sort(
						[&](const FFrontendQueryEntry& InLHS, const FFrontendQueryEntry& InRHS)
						{
							return Step->Sort(InLHS, InRHS);
						}
					);
				}
			}
		};

		struct FLimitStepExecuter : TStepExecuter<IFrontendQueryLimitStep>
		{
			using TStepExecuter<IFrontendQueryLimitStep>::TStepExecuter;

			void ExecuteStep(FFrontendQuerySelection& InOutResult) const override
			{
				if (Step.IsValid())
				{
					int32 Limit = Step->Limit();
					if (InOutResult.GetSelection().Num() > Limit)
					{
						InOutResult.SetSelection(InOutResult.GetSelection().Slice(0, Limit));
					}
				}
			}
		};
	}

	FFrontendQuerySelection::FReduceOutputView::FReduceOutputView(TArrayView<FFrontendQueryEntry*> InEntries)
	:	InitialEntries(InEntries)
	{
	}

	void FFrontendQuerySelection::FReduceOutputView::Add(FFrontendQueryEntry& InResult)
	{
		if (!InitialEntries.Contains(&InResult))
		{
			NewEntryStorage.Add(InResult);
		}
		else
		{
			ExistingEntryPointers.Add(&InResult);
		}
	}

	TArrayView<FFrontendQueryEntry*> FFrontendQuerySelection::FReduceOutputView::GetSelectedInitialEntries()
	{
		return ExistingEntryPointers;
	}

	TArrayView<FFrontendQueryEntry> FFrontendQuerySelection::FReduceOutputView::GetSelectedNewEntries() 
	{
		return NewEntryStorage;
	}

	TArrayView<FFrontendQueryEntry> FFrontendQuerySelection::GetStorage()
	{
		return Storage;
	}

	TArrayView<const FFrontendQueryEntry> FFrontendQuerySelection::GetStorage() const
	{
		return Storage;
	}

	TArrayView<FFrontendQueryEntry*> FFrontendQuerySelection::GetSelection()
	{
		return Selection;
	}

	TArrayView<const FFrontendQueryEntry * const> FFrontendQuerySelection::GetSelection() const
	{
		return MakeArrayView(Selection);
	}

	void FFrontendQuerySelection::AppendToStorageAndSelection(TArrayView<const FFrontendQueryEntry> InEntries)
	{
		const int32 Num = InEntries.Num();

		if (Num >  0)
		{
			int32 StorageIndex = Storage.Num();
			int32 SelectionIndex = Selection.Num();

			Storage.Append(InEntries.GetData(), InEntries.Num());
			Selection.AddZeroed(Num);

			for (int32 i = 0; i < Num; i++)
			{
				Selection[SelectionIndex] = &Storage[StorageIndex];
				SelectionIndex++;
				StorageIndex++;
			}
		}
	}

	void FFrontendQuerySelection::AppendToStorageAndSelection(FReduceOutputView& InReduceView)
	{
		AppendToStorageAndSelection(InReduceView.GetSelectedNewEntries());
		AppendToSelection(InReduceView.GetSelectedInitialEntries());
	}

	void FFrontendQuerySelection::ResetStorageAndSelection()
	{
		Storage.Reset();
		Selection.Reset();
	}

	void FFrontendQuerySelection::ResetSelection()
	{
		Selection.Reset();
	}

	void FFrontendQuerySelection::SetSelection(TArrayView<FFrontendQueryEntry*> InEntries)
	{
		Selection = InEntries;
	}

	void FFrontendQuerySelection::AddToSelection(FFrontendQueryEntry* InEntry)
	{
		if (nullptr != InEntry)
		{
			Selection.Add(InEntry);
		}
	}

	void FFrontendQuerySelection::AppendToSelection(TArrayView<FFrontendQueryEntry*> InEntries)
	{
		Selection.Append(InEntries.GetData(), InEntries.Num());
	}



	FFrontendQuerySelectionView::FFrontendQuerySelectionView(TUniquePtr<FFrontendQuerySelection>&& InResult)
	:	Result(MoveTemp(InResult))
	{
		if (!Result.IsValid())
		{
			Result = MakeUnique<FFrontendQuerySelection>();
		}
	}

	TArrayView<const FFrontendQueryEntry> FFrontendQuerySelectionView::GetStorage() const
	{
		return Result->GetStorage();
	}

	TArrayView<const FFrontendQueryEntry* const> FFrontendQuerySelectionView::GetSelection() const
	{
		return Result->GetSelection();
	}

	FFrontendQueryStep::FFrontendQueryStep(FGenerateFunction InFunc)
	:	StepExecuter(MakeUnique<FrontendQueryPrivate::FGenerateStepExecuter>(MakeUnique<FrontendQueryPrivate::FGenerateFunctionFrontendQueryStep>(InFunc)))
	{
	}

	FFrontendQueryStep::FFrontendQueryStep(FMapFunction InFunc)
	:	StepExecuter(MakeUnique<FrontendQueryPrivate::FMapStepExecuter>(MakeUnique<FrontendQueryPrivate::FMapFunctionFrontendQueryStep>(InFunc)))
	{
	}

	FFrontendQueryStep::FFrontendQueryStep(FReduceFunction InFunc)
	:	StepExecuter(MakeUnique<FrontendQueryPrivate::FReduceStepExecuter>(MakeUnique<FrontendQueryPrivate::FReduceFunctionFrontendQueryStep>(InFunc)))
	{
	}

	FFrontendQueryStep::FFrontendQueryStep(FFilterFunction InFunc)
	:	StepExecuter(MakeUnique<FrontendQueryPrivate::FFilterStepExecuter>(MakeUnique<FrontendQueryPrivate::FFilterFunctionFrontendQueryStep>(InFunc)))
	{
	}
	
	FFrontendQueryStep::FFrontendQueryStep(FScoreFunction InFunc)
	:	StepExecuter(MakeUnique<FrontendQueryPrivate::FScoreStepExecuter>(MakeUnique<FrontendQueryPrivate::FScoreFunctionFrontendQueryStep>(InFunc)))
	{
	}

	FFrontendQueryStep::FFrontendQueryStep(FSortFunction InFunc)
	:	StepExecuter(MakeUnique<FrontendQueryPrivate::FSortStepExecuter>(MakeUnique<FrontendQueryPrivate::FSortFunctionFrontendQueryStep>(InFunc)))
	{
	}

	FFrontendQueryStep::FFrontendQueryStep(FLimitFunction InFunc)
	:	StepExecuter(MakeUnique<FrontendQueryPrivate::FLimitStepExecuter>(MakeUnique<FrontendQueryPrivate::FLimitFunctionFrontendQueryStep>(InFunc)))
	{
	}

	FFrontendQueryStep::FFrontendQueryStep(TUniquePtr<IFrontendQueryGenerateStep>&& InStep)
	:	StepExecuter(MakeUnique<FrontendQueryPrivate::FGenerateStepExecuter>(MoveTemp(InStep)))
	{
	}
	
	FFrontendQueryStep::FFrontendQueryStep(TUniquePtr<IFrontendQueryMapStep>&& InStep)
	:	StepExecuter(MakeUnique<FrontendQueryPrivate::FMapStepExecuter>(MoveTemp(InStep)))
	{
	}

	FFrontendQueryStep::FFrontendQueryStep(TUniquePtr<IFrontendQueryReduceStep>&& InStep)
	:	StepExecuter(MakeUnique<FrontendQueryPrivate::FReduceStepExecuter>(MoveTemp(InStep)))
	{
	}

	FFrontendQueryStep::FFrontendQueryStep(TUniquePtr<IFrontendQueryFilterStep>&& InStep)
	:	StepExecuter(MakeUnique<FrontendQueryPrivate::FFilterStepExecuter>(MoveTemp(InStep)))
	{
	}

	FFrontendQueryStep::FFrontendQueryStep(TUniquePtr<IFrontendQueryScoreStep>&& InStep)
	:	StepExecuter(MakeUnique<FrontendQueryPrivate::FScoreStepExecuter>(MoveTemp(InStep)))
	{
	}

	FFrontendQueryStep::FFrontendQueryStep(TUniquePtr<IFrontendQuerySortStep>&& InStep)
	:	StepExecuter(MakeUnique<FrontendQueryPrivate::FSortStepExecuter>(MoveTemp(InStep)))
	{
	}

	FFrontendQueryStep::FFrontendQueryStep(TUniquePtr<IFrontendQueryLimitStep>&& InStep)
	:	StepExecuter(MakeUnique<FrontendQueryPrivate::FLimitStepExecuter>(MoveTemp(InStep)))
	{
	}

	void FFrontendQueryStep::ExecuteStep(FFrontendQuerySelection& InOutResult)
	{
		if (StepExecuter.IsValid())
		{
			StepExecuter->ExecuteStep(InOutResult);
		}
	}

	const TArray<TUniquePtr<FFrontendQueryStep>>& FFrontendQuery::GetSteps() const
	{
		return Steps;
	}

	FFrontendQuery& FFrontendQuery::AddGenerateLambdaStep(FGenerateFunction InFunc)
	{
		return AddFunctionStep(InFunc);
	}

	FFrontendQuery& FFrontendQuery::AddMapLambdaStep(FMapFunction InFunc)
	{
		return AddFunctionStep(InFunc);
	}

	FFrontendQuery& FFrontendQuery::AddReduceLambdaStep(FReduceFunction InFunc)
	{
		return AddFunctionStep(InFunc);
	}

	FFrontendQuery& FFrontendQuery::AddFilterLambdaStep(FFilterFunction InFunc)
	{
		return AddFunctionStep(InFunc);
	}

	FFrontendQuery& FFrontendQuery::AddScoreLambdaStep(FScoreFunction InFunc)
	{
		return AddFunctionStep(InFunc);
	}

	FFrontendQuery& FFrontendQuery::AddSortLambdaStep(FSortFunction InFunc)
	{
		return AddFunctionStep(InFunc);
	}

	FFrontendQuery& FFrontendQuery::AddLimitLambdaStep(FLimitFunction InFunc)
	{
		return AddFunctionStep(InFunc);
	}

	FFrontendQuery& FFrontendQuery::AddStep(TUniquePtr<FFrontendQueryStep>&& InStep)
	{
		Steps.Add(MoveTemp(InStep));
		return *this;
	}

	FFrontendQuerySelectionView FFrontendQuery::ExecuteQuery()
	{
		Result = MakeUnique<FFrontendQuerySelection>();

		for (int32 StepIndex = 0; StepIndex < Steps.Num(); StepIndex++)
		{
			if (!Steps[StepIndex].IsValid())
			{
				continue;
			}

			Steps[StepIndex]->ExecuteStep(*Result);
		}

		return FFrontendQuerySelectionView(MoveTemp(Result));
	}
}

