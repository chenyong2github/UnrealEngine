// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundFrontendQuery.h"

#include "Algo/IsSorted.h"
#include "Algo/Sort.h"
#include "Algo/Transform.h"
#include "CoreMinimal.h"
#include "MetasoundFrontendDocument.h"
#include "Templates/TypeHash.h"

namespace Metasound
{
	namespace FrontendQueryPrivate
	{
		// Wrapper for step defined by function
		struct FStreamFunctionFrontendQueryStep: IFrontendQueryStreamStep
		{
			using FStreamFunction = FFrontendQueryStep::FStreamFunction;

			FStreamFunctionFrontendQueryStep(FStreamFunction&& InFunc)
			:	Func(MoveTemp(InFunc))
			{
			}

			void Stream(TArray<FFrontendQueryEntry>& OutEntries) override
			{
				Func(OutEntries);
			}

			private:

			FStreamFunction Func;
		};

		// Wrapper for step defined by function
		struct FTransformFunctionFrontendQueryStep: IFrontendQueryTransformStep
		{
			using FTransformFunction = FFrontendQueryStep::FTransformFunction;

			FTransformFunctionFrontendQueryStep(FTransformFunction InFunc)
			:	Func(InFunc)
			{
			}

			void Transform(FFrontendQueryEntry::FValue& InValue) const override
			{
				Func(InValue);
			}

			FTransformFunction Func;
		};

		// Wrapper for step defined by function
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

		// Wrapper for step defined by function
		struct FReduceFunctionFrontendQueryStep: IFrontendQueryReduceStep
		{
			using FReduceFunction = FFrontendQueryStep::FReduceFunction;

			FReduceFunctionFrontendQueryStep(FReduceFunction InFunc)
			:	Func(InFunc)
			{
			}

			void Reduce(FFrontendQueryEntry::FKey InKey, TArrayView<FFrontendQueryEntry * const>& InEntries, FReduceOutputView& OutResult) const override
			{
				return Func(InKey, InEntries, OutResult);
			}

			FReduceFunction Func;
		};

		// Wrapper for step defined by function
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

		// Wrapper for step defined by function
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

		// Wrapper for step defined by function
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

		// Wrapper for step defined by function
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

		// Base implementation of a query step execution.
		struct FStepExecuterBase : public FFrontendQueryStep::IStepExecuter
		{

			// Appends incremental results to the output results.
			EResultModificationState Append(const FFrontendQuerySelection& InIncremental, FFrontendQuerySelection& InOutResult) const
			{
				EResultModificationState ResultState = EResultModificationState::Unmodified;

				if (InIncremental.GetSelection().Num() > 0)
				{
					InOutResult.AppendToStorageAndSelection(InIncremental);
					ResultState = EResultModificationState::Modified;
				}

				return ResultState;
			}
		};

		// Implements execution of a source
		struct FSourceStepExecuter : public FStepExecuterBase
		{
			FSourceStepExecuter(TUniquePtr<IFrontendQuerySource>&& InSource)
			: Source(MoveTemp(InSource))
			{
			}

			// Get the latest entries from the source
			virtual EResultModificationState Increment(FFrontendQuerySelection& InOutResult) override
			{
				if (Source.IsValid())
				{
					TArray<FFrontendQueryEntry> NewEntries;
					Source->Stream(NewEntries);

					if (NewEntries.Num() > 0)
					{
						InOutResult.AppendToStorageAndSelection(NewEntries);
						return EResultModificationState::Modified;
					}
				}

				return EResultModificationState::Unmodified;
			}

			virtual EResultModificationState Merge(const FFrontendQuerySelection& InIncremental, FFrontendQuerySelection& InOutResult) override
			{
				return Append(InIncremental, InOutResult);
			}

			virtual bool IsMergeRequiredForIncremental() const override
			{
				return false;
			}

			virtual EResultModificationState Execute(FFrontendQuerySelection& InOutResult) override
			{
				Reset(); // Reset source to original state.
				return Increment(InOutResult);
			}

			virtual void Reset() override 
			{ 
				if (Source.IsValid())
				{
					Source->Reset();
				}
			}

		private:

			TUniquePtr<IFrontendQuerySource> Source;
		};

		template<typename StepType>
		struct TStepExecuter : public FStepExecuterBase
		{
			TStepExecuter(TUniquePtr<StepType>&& InStep)
			:	Step(MoveTemp(InStep))
			{
			}

			virtual void Reset() override { }

			virtual bool IsMergeRequiredForIncremental() const override { return false; }

		protected:

			TUniquePtr<StepType> Step;
		};

		struct FStreamStepExecuter : TStepExecuter<IFrontendQueryStreamStep>
		{
			using TStepExecuter<IFrontendQueryStreamStep>::TStepExecuter;

			virtual EResultModificationState Increment(FFrontendQuerySelection& InOutResult) override
			{
				EResultModificationState ResultState = EResultModificationState::Unmodified;

				if (Step.IsValid())
				{
					TArray<FFrontendQueryEntry> NewEntries;
					Step->Stream(NewEntries);

					if (NewEntries.Num() > 0)
					{
						ResultState = EResultModificationState::Modified;
						InOutResult.AppendToStorageAndSelection(NewEntries);
					}
				}

				return ResultState;
			}

			virtual EResultModificationState Merge(const FFrontendQuerySelection& InIncremental, FFrontendQuerySelection& InOutResult) override
			{
				return Append(InIncremental, InOutResult);
			}

			virtual EResultModificationState Execute(FFrontendQuerySelection& InOutResult) override
			{
				return Increment(InOutResult);
			}
		};

		struct FTransformStepExecuter : TStepExecuter<IFrontendQueryTransformStep>
		{
			using TStepExecuter<IFrontendQueryTransformStep>::TStepExecuter;

			virtual EResultModificationState Increment(FFrontendQuerySelection& InOutResult) override
			{
				EResultModificationState ResultState = EResultModificationState::Unmodified;

				if (Step.IsValid())
				{
					if (InOutResult.GetSelection().Num() > 0)
					{
						ResultState = EResultModificationState::Modified;
					}

					for (FFrontendQueryEntry* Entry : InOutResult.GetSelection())
					{
						if (nullptr != Entry)
						{
							Step->Transform(Entry->Value);
						}
					}
				}

				return ResultState;
			}

			virtual EResultModificationState Merge(const FFrontendQuerySelection& InIncremental, FFrontendQuerySelection& InOutResult) override
			{
				return Append(InIncremental, InOutResult);
			}

			virtual EResultModificationState Execute(FFrontendQuerySelection& InOutResult) override
			{
				return Increment(InOutResult);
			}
		};

		struct FMapStepExecuter : TStepExecuter<IFrontendQueryMapStep>
		{
			using TStepExecuter<IFrontendQueryMapStep>::TStepExecuter;

			virtual EResultModificationState Increment(FFrontendQuerySelection& InOutResult) override
			{
				EResultModificationState ResultState = EResultModificationState::Unmodified;

				if (Step.IsValid())
				{
					if (InOutResult.GetSelection().Num() > 0)
					{
						ResultState = EResultModificationState::Modified;
					}

					for (FFrontendQueryEntry* Entry : InOutResult.GetSelection())
					{
						if (nullptr != Entry)
						{
							Entry->Key = Step->Map(*Entry);
						}
					}
				}

				return ResultState;
			}

			virtual EResultModificationState Merge(const FFrontendQuerySelection& InIncremental, FFrontendQuerySelection& InOutResult) override
			{
				return Append(InIncremental, InOutResult);
			}

			virtual EResultModificationState Execute(FFrontendQuerySelection& InOutResult) override
			{
				return Increment(InOutResult);
			}
		};

		struct FReduceStepExecuter : TStepExecuter<IFrontendQueryReduceStep>
		{
			using FKey = FFrontendQueryEntry::FKey;

		private:
			static const FKey InvalidKey;

			static const FFrontendQueryEntry::FKey& GetKey(const FFrontendQueryEntry* InEntry)
			{
				if (nullptr != InEntry)
				{
					return InEntry->Key;
				}
				else
				{
					return InvalidKey;
				}
			}

			// Reduce entries.
			//
			// @parma InKey - The key associated with InEntriesToReduce
			// @parma InEntriesToReduce - Entries to either append or reduce.
			// @param OutResult - The output query selection to append to.
			void Reduce(const FFrontendQueryEntry::FKey& InKey, TArrayView<FFrontendQueryEntry* const> InEntriesToReduce, FFrontendQuerySelection& OutResult)
			{
				check(Step.IsValid());

				// Note: InEntriesToReduce is has valid storage in OutResult.
				FFrontendQuerySelection::FReduceOutputView ReduceResult(InEntriesToReduce);
				Step->Reduce(InKey, InEntriesToReduce, ReduceResult);
				OutResult.AppendToStorageAndSelection(MoveTemp(ReduceResult));
			}

			// Iterate over sorted entries.
			template<typename FuncType>
			void IterSortedEntries(TArrayView<FFrontendQueryEntry* const> SortedSelection, FuncType InFunc)
			{
				// input selection must be sorted by key.
				check(Algo::IsSortedBy(SortedSelection, FReduceStepExecuter::GetKey));

				if (SortedSelection.Num() > 0)
				{
					int32 StartIndex = 0;
					
					FFrontendQueryEntry::FKey CurrentKey = GetKey(SortedSelection[0]);

					const int32 Num = SortedSelection.Num();
					for (int32 EndIndex = 1; EndIndex < Num; EndIndex++)
					{
						FFrontendQueryEntry::FKey ThisKey = GetKey(SortedSelection[EndIndex]);
						if (CurrentKey != ThisKey)
						{
							InFunc(CurrentKey, SortedSelection.Slice(StartIndex, EndIndex - StartIndex));

							CurrentKey = ThisKey;
							StartIndex = EndIndex;
						}
					}

					InFunc(CurrentKey, SortedSelection.Slice(StartIndex, SortedSelection.Num() - StartIndex));
				}
			}

			TArray<FFrontendQueryEntry*> GetSortedSelection(TArrayView<FFrontendQueryEntry* const> InSelection)
			{
				TArray<FFrontendQueryEntry*> SortedSelection(InSelection.GetData(), InSelection.Num()); 
				Algo::SortBy(SortedSelection, GetKey);
				return SortedSelection;
			}

		public:

			using TStepExecuter<IFrontendQueryReduceStep>::TStepExecuter;

			virtual EResultModificationState Increment(FFrontendQuerySelection& InOutResult) override
			{
				check(IsMergeRequiredForIncremental());
				return Execute(InOutResult);
			}

			virtual EResultModificationState Merge(const FFrontendQuerySelection& InIncremental, FFrontendQuerySelection& InOutResult) override
			{
				EResultModificationState ResultState = EResultModificationState::Unmodified;

				if (Step.IsValid())
				{ 	
					if (InIncremental.GetSelection().Num() > 0)
					{
						TSet<FKey> NewKeys;

						Algo::Transform(InIncremental.GetSelection(), NewKeys, GetKey);

						InOutResult.AppendToStorageAndSelection(InIncremental);
						TArray<FFrontendQueryEntry*> SortedSelection = GetSortedSelection(InOutResult.GetSelection());
						InOutResult.ResetSelection();

						IterSortedEntries(SortedSelection, [&](const FKey& InKey, TArrayView<FFrontendQueryEntry* const> InEntriesToReduce)
						{
							if (NewKeys.Contains(InKey))
							{
								// Some of the entries re new. Need to re-reduce.
								this->Reduce(InKey, InEntriesToReduce, InOutResult);
							}
							else
							{
								// All these entries are owned by the result already.
								InOutResult.AppendToSelection(InEntriesToReduce);
							}
						});

						ResultState = EResultModificationState::Modified;
					}
				}

				return ResultState;
			}

			virtual EResultModificationState Execute(FFrontendQuerySelection& InOutResult) override
			{
				EResultModificationState ResultState = EResultModificationState::Unmodified;

				if (Step.IsValid())
				{ 	
					if (InOutResult.GetSelection().Num() > 0)
					{
						TArray<FFrontendQueryEntry*> SortedSelection = GetSortedSelection(InOutResult.GetSelection());
						InOutResult.ResetSelection();

						IterSortedEntries(SortedSelection, [&](const FKey& InKey, TArrayView<FFrontendQueryEntry* const> InEntriesToReduce)
						{
							this->Reduce(InKey, InEntriesToReduce, InOutResult);
						});

						ResultState = EResultModificationState::Modified;
					}
				}

				return ResultState;
			}


			virtual bool IsMergeRequiredForIncremental() const override
			{
				// Merge is required during an incremental update because
				// existing results and new results may share the same key and
				// need re-reducing.
				return true;
			}
		};

		const FReduceStepExecuter::FKey FReduceStepExecuter::InvalidKey;

		struct FFilterStepExecuter : TStepExecuter<IFrontendQueryFilterStep>
		{
			using TStepExecuter<IFrontendQueryFilterStep>::TStepExecuter;

			virtual EResultModificationState Increment(FFrontendQuerySelection& InOutResult) override
			{
				EResultModificationState ResultState = EResultModificationState::Unmodified;

				if (Step.IsValid())
				{
					auto FilterFunc = [&](const FFrontendQueryEntry* Entry)
					{
						return (nullptr != Entry) && Step->Filter(*Entry);
					};

					int32 NumRemoved = InOutResult.FilterSelection(FilterFunc);
					if (NumRemoved > 0)
					{
						ResultState = EResultModificationState::Modified;
					}
				}

				return ResultState;
			}

			virtual EResultModificationState Merge(const FFrontendQuerySelection& InIncremental, FFrontendQuerySelection& InOutResult) override
			{
				return Append(InIncremental, InOutResult);
			}

			virtual EResultModificationState Execute(FFrontendQuerySelection& InOutResult) override
			{
				return Increment(InOutResult);
			}
		};

		struct FScoreStepExecuter : TStepExecuter<IFrontendQueryScoreStep>
		{
			using TStepExecuter<IFrontendQueryScoreStep>::TStepExecuter;

			virtual EResultModificationState Increment(FFrontendQuerySelection& InOutResult) override
			{
				EResultModificationState ResultState = EResultModificationState::Unmodified;

				if (Step.IsValid())
				{
					if (InOutResult.GetSelection().Num() > 0)
					{
						ResultState = EResultModificationState::Modified;
					}

					for (FFrontendQueryEntry* Entry : InOutResult.GetSelection())
					{
						if (nullptr != Entry)
						{
							Entry->Score = Step->Score(*Entry);
						}
					}
				}

				return ResultState;
			}

			virtual EResultModificationState Merge(const FFrontendQuerySelection& InIncremental, FFrontendQuerySelection& InOutResult) override
			{
				return Append(InIncremental, InOutResult);
			}

			virtual EResultModificationState Execute(FFrontendQuerySelection& InOutResult) override
			{
				return Increment(InOutResult);
			}
		};
		
		struct FSortStepExecuter : TStepExecuter<IFrontendQuerySortStep>
		{
			using TStepExecuter<IFrontendQuerySortStep>::TStepExecuter;

			EResultModificationState Increment(FFrontendQuerySelection& InOutResult) override
			{
				check(IsMergeRequiredForIncremental());
				return Execute(InOutResult);
			}

			virtual EResultModificationState Merge(const FFrontendQuerySelection& InIncremental, FFrontendQuerySelection& InOutResult) override
			{
				EResultModificationState ResultState = EResultModificationState::Unmodified;

				if (Step.IsValid())
				{
					const bool bIncrementalHasNonEmptySelection = InIncremental.GetSelection().Num() > 0;

					if (bIncrementalHasNonEmptySelection)
					{
						// Append incremental data to this result
						InOutResult.AppendToStorageAndSelection(InIncremental);

						InOutResult.GetSelection().Sort(
							[&](const FFrontendQueryEntry& InLHS, const FFrontendQueryEntry& InRHS)
							{
								return Step->Sort(InLHS, InRHS);
							}
						);

						ResultState = EResultModificationState::Modified;
					}
				}

				return ResultState;
			}

			virtual EResultModificationState Execute(FFrontendQuerySelection& InOutResult) override
			{
				EResultModificationState ResultState = EResultModificationState::Unmodified;

				if (Step.IsValid())
				{
					if (InOutResult.GetSelection().Num() > 0)
					{
						InOutResult.GetSelection().Sort(
							[&](const FFrontendQueryEntry& InLHS, const FFrontendQueryEntry& InRHS)
							{
								return Step->Sort(InLHS, InRHS);
							}
						);

						ResultState = EResultModificationState::Modified;
					}
				}

				return ResultState;
			}

			virtual bool IsMergeRequiredForIncremental() const 
			{
				// Merging is required during incremental update because sort
				// order may change after merge.
				return true;
			}
		};

		struct FLimitStepExecuter : TStepExecuter<IFrontendQueryLimitStep>
		{
			using TStepExecuter<IFrontendQueryLimitStep>::TStepExecuter;

			EResultModificationState Increment(FFrontendQuerySelection& InOutResult) override
			{
				check(IsMergeRequiredForIncremental());
				return Execute(InOutResult);
			}

			virtual EResultModificationState Merge(const FFrontendQuerySelection& InIncremental, FFrontendQuerySelection& InOutResult) override
			{
				EResultModificationState ResultState = EResultModificationState::Unmodified;

				if (Step.IsValid())
				{
					int32 Limit = Step->Limit();

					if (InIncremental.GetSelection().Num() > 0)
					{
						ResultState = EResultModificationState::Modified;
						InOutResult.AppendToStorageAndSelection(InIncremental);

						if (InOutResult.GetSelection().Num() > Limit)
						{
							InOutResult.SetSelection(InOutResult.GetSelection().Slice(0, Limit));
						}
					}
				}

				return ResultState;
			}

			virtual EResultModificationState Execute(FFrontendQuerySelection& InOutResult) override
			{
				EResultModificationState ResultState = EResultModificationState::Unmodified;

				if (Step.IsValid())
				{
					int32 Limit = Step->Limit();

					if (InOutResult.GetSelection().Num() > Limit)
					{
						ResultState = EResultModificationState::Modified;
						InOutResult.SetSelection(InOutResult.GetSelection().Slice(0, Limit));
					}
				}

				return ResultState;
			}

			virtual bool IsMergeRequiredForIncremental() const 
			{
				// Merge is required because values under limit may be different
				// after merge.
				return true;
			}
		};
	}

	FFrontendQueryKey::FFrontendQueryKey()
	: Key(TInPlaceType<FFrontendQueryKey::FInvalid>())
	, Hash(INDEX_NONE)
	{}

	FFrontendQueryKey::FFrontendQueryKey(int32 InKey)
	: Key(TInPlaceType<int32>(), InKey)
	, Hash(::GetTypeHash(InKey))
	{
	}

	FFrontendQueryKey::FFrontendQueryKey(const FString& InKey)
	: Key(TInPlaceType<FString>(), InKey)
	, Hash(::GetTypeHash(InKey))
	{
	}

	FFrontendQueryKey::FFrontendQueryKey(const FName& InKey)
	: Key(TInPlaceType<FName>(), InKey)
	, Hash(::GetTypeHash(InKey))
	{
	}

	bool FFrontendQueryKey::IsValid() const
	{
		return Key.GetIndex() != FKeyType::IndexOfType<FFrontendQueryKey::FInvalid>();
	}

	bool operator==(const FFrontendQueryKey& InLHS, const FFrontendQueryKey& InRHS)
	{
		if (InLHS.Hash == InRHS.Hash)
		{
			if (InLHS.IsValid() && InRHS.IsValid())
			{
				if (InLHS.Key.GetIndex() == InRHS.Key.GetIndex())
				{
					switch(InLHS.Key.GetIndex())
					{
						case FFrontendQueryKey::FKeyType::IndexOfType<int32>():
							return InLHS.Key.Get<int32>() == InRHS.Key.Get<int32>();

						case FFrontendQueryKey::FKeyType::IndexOfType<FString>():
							return InLHS.Key.Get<FString>() == InRHS.Key.Get<FString>();

						case FFrontendQueryKey::FKeyType::IndexOfType<FName>():
							return InLHS.Key.Get<FName>() == InRHS.Key.Get<FName>();

						default:
							// Unhandled case type.
							checkNoEntry();
					}
				}
			}
		}

		return false;
	}

	bool operator!=(const FFrontendQueryKey& InLHS, const FFrontendQueryKey& InRHS)
	{
		return !(InLHS == InRHS);
	}

	bool operator<(const FFrontendQueryKey& InLHS, const FFrontendQueryKey& InRHS)
	{
		if (InLHS.Hash != InRHS.Hash)
		{
			return InLHS.Hash < InRHS.Hash;
		}

		if (InLHS.Key.GetIndex() != InRHS.Key.GetIndex())
		{
			return InLHS.Key.GetIndex() < InRHS.Key.GetIndex();
		}

		switch(InLHS.Key.GetIndex())
		{
			case FFrontendQueryKey::FKeyType::IndexOfType<int32>():
				return InLHS.Key.Get<int32>() < InRHS.Key.Get<int32>();

			case FFrontendQueryKey::FKeyType::IndexOfType<FString>():
				return InLHS.Key.Get<FString>() < InRHS.Key.Get<FString>();

			case FFrontendQueryKey::FKeyType::IndexOfType<FName>():
				return InLHS.Key.Get<FName>().FastLess(InRHS.Key.Get<FName>());

			default:
				// Unhandled case type.
				checkNoEntry();
		}

		return false;
	}

	uint32 GetTypeHash(const FFrontendQueryKey& InKey)
	{
		return InKey.Hash;
	}

	FFrontendQuerySelection::FFrontendQuerySelection(const FFrontendQuerySelection& InOther)
	{
		AppendToStorageAndSelection(InOther);
	}

	FFrontendQuerySelection& FFrontendQuerySelection::operator=(const FFrontendQuerySelection& InOther)
	{
		ResetStorageAndSelection();
		AppendToStorageAndSelection(InOther);

		return *this;
	}

	FFrontendQuerySelection::FReduceOutputView::FReduceOutputView(TArrayView<FFrontendQueryEntry* const> InEntries)
	:	InitialEntries(InEntries)
	{
	}

	void FFrontendQuerySelection::FReduceOutputView::Add(FFrontendQueryEntry& InResult)
	{
		if (!InitialEntries.Contains(&InResult))
		{
			SelectedNewEntries.Add(MakeUnique<FFrontendQueryEntry>(InResult));
		}
		else
		{
			SelectedExistingEntries.Add(&InResult);
		}
	}


	TArrayView<FFrontendQueryEntry*> FFrontendQuerySelection::GetSelection()
	{
		return Selection;
	}

	TArrayView<const FFrontendQueryEntry * const> FFrontendQuerySelection::GetSelection() const
	{
		return MakeArrayView(Selection);
	}

	void FFrontendQuerySelection::AppendToStorageAndSelection(const FFrontendQuerySelection& InSelection)
	{
		TArrayView<const FFrontendQueryEntry* const> InEntries = InSelection.GetSelection();
		AppendToStorageAndSelection(InSelection.GetSelection());
	}

	void FFrontendQuerySelection::AppendToStorageAndSelection(FFrontendQuerySelection&& InSelection)
	{
		InSelection.ShrinkStorageToSelection();
		AppendToStorageAndSelection(MoveTemp(InSelection.Storage));
	}

	void FFrontendQuerySelection::AppendToStorageAndSelection(TArrayView<const FFrontendQueryEntry> InEntries)
	{
		const int32 Num = InEntries.Num();

		if (Num >  0)
		{
			int32 StorageIndex = Storage.Num();
			int32 SelectionIndex = Selection.Num();

			for (const FFrontendQueryEntry& Entry : InEntries)
			{
				Storage.Add(MakeUnique<FFrontendQueryEntry>(Entry));
			}
			Selection.AddZeroed(Num);

			for (int32 i = 0; i < Num; i++)
			{
				Selection[SelectionIndex] = Storage[StorageIndex].Get();
				SelectionIndex++;
				StorageIndex++;
			}
		}
	}

	void FFrontendQuerySelection::AppendToStorageAndSelection(TArrayView<const FFrontendQueryEntry* const> InEntries)
	{
		if (InEntries.Num() >  0)
		{
			int32 StorageIndex = Storage.Num();
			int32 SelectionIndex = Selection.Num();

			int32 NumAdded = 0;
			for (const FFrontendQueryEntry* Entry : InEntries)
			{
				if (nullptr != Entry)
				{
					Storage.Add(MakeUnique<FFrontendQueryEntry>(*Entry));
					NumAdded++;
				}
			}

			if (NumAdded > 0)
			{
				Selection.AddZeroed(NumAdded);

				for (int32 i = 0; i < NumAdded; i++)
				{
					Selection[SelectionIndex] = Storage[StorageIndex].Get();
					SelectionIndex++;
					StorageIndex++;
				}
			}
		}
	}

	void FFrontendQuerySelection::AppendToStorageAndSelection(FReduceOutputView&& InReduceView)
	{
		AppendToStorageAndSelection(MoveTemp(InReduceView.SelectedNewEntries));
		AppendToSelection(InReduceView.SelectedExistingEntries);
	}

	void FFrontendQuerySelection::AppendToStorageAndSelection(TArray<TUniquePtr<FFrontendQueryEntry>>&& InEntries)
	{
		if (InEntries.Num() >  0)
		{
			int32 StorageIndex = Storage.Num();
			int32 SelectionIndex = Selection.Num();

			int32 NumAdded = 0;
			for (TUniquePtr<FFrontendQueryEntry>& Entry : InEntries)
			{
				if (Entry.IsValid())
				{
					Storage.Add(MoveTemp(Entry));
					NumAdded++;
				}
			}

			if (NumAdded > 0)
			{
				Selection.AddZeroed(NumAdded);

				for (int32 i = 0; i < NumAdded; i++)
				{
					Selection[SelectionIndex] = Storage[StorageIndex].Get();
					SelectionIndex++;
					StorageIndex++;
				}
			}
		}

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

	void FFrontendQuerySelection::AppendToSelection(TArrayView<FFrontendQueryEntry * const> InEntries)
	{
		Selection.Append(InEntries.GetData(), InEntries.Num());
	}

	void FFrontendQuerySelection::ShrinkStorageToSelection()
	{
		TSet<FFrontendQueryEntry*> SelectionSet(Selection);
		Storage.RemoveAll([&](const TUniquePtr<FFrontendQueryEntry>& InEntry)
		{
			return !SelectionSet.Contains(InEntry.Get());
		});
	}


	FFrontendQuerySelectionView::FFrontendQuerySelectionView(TSharedRef<const FFrontendQuerySelection, ESPMode::ThreadSafe> InResult)
	:	Result(InResult)
	{
	}

	TArrayView<const FFrontendQueryEntry* const> FFrontendQuerySelectionView::GetSelection() const
	{
		return Result->GetSelection();
	}

	FFrontendQueryStep::FFrontendQueryStep(FStreamFunction&& InFunc)
	:	StepExecuter(MakeUnique<FrontendQueryPrivate::FStreamStepExecuter>(MakeUnique<FrontendQueryPrivate::FStreamFunctionFrontendQueryStep>(MoveTemp(InFunc))))
	{
	}

	FFrontendQueryStep::FFrontendQueryStep(FTransformFunction&& InFunc)
	:	StepExecuter(MakeUnique<FrontendQueryPrivate::FTransformStepExecuter>(MakeUnique<FrontendQueryPrivate::FTransformFunctionFrontendQueryStep>(MoveTemp(InFunc))))
	{
	}

	FFrontendQueryStep::FFrontendQueryStep(FMapFunction&& InFunc)
	:	StepExecuter(MakeUnique<FrontendQueryPrivate::FMapStepExecuter>(MakeUnique<FrontendQueryPrivate::FMapFunctionFrontendQueryStep>(MoveTemp(InFunc))))
	{
	}

	FFrontendQueryStep::FFrontendQueryStep(FReduceFunction&& InFunc)
	:	StepExecuter(MakeUnique<FrontendQueryPrivate::FReduceStepExecuter>(MakeUnique<FrontendQueryPrivate::FReduceFunctionFrontendQueryStep>(MoveTemp(InFunc))))
	{
	}

	FFrontendQueryStep::FFrontendQueryStep(FFilterFunction&& InFunc)
	:	StepExecuter(MakeUnique<FrontendQueryPrivate::FFilterStepExecuter>(MakeUnique<FrontendQueryPrivate::FFilterFunctionFrontendQueryStep>(MoveTemp(InFunc))))
	{
	}
	
	FFrontendQueryStep::FFrontendQueryStep(FScoreFunction&& InFunc)
	:	StepExecuter(MakeUnique<FrontendQueryPrivate::FScoreStepExecuter>(MakeUnique<FrontendQueryPrivate::FScoreFunctionFrontendQueryStep>(MoveTemp(InFunc))))
	{
	}

	FFrontendQueryStep::FFrontendQueryStep(FSortFunction&& InFunc)
	:	StepExecuter(MakeUnique<FrontendQueryPrivate::FSortStepExecuter>(MakeUnique<FrontendQueryPrivate::FSortFunctionFrontendQueryStep>(MoveTemp(InFunc))))
	{
	}

	FFrontendQueryStep::FFrontendQueryStep(FLimitFunction&& InFunc)
	:	StepExecuter(MakeUnique<FrontendQueryPrivate::FLimitStepExecuter>(MakeUnique<FrontendQueryPrivate::FLimitFunctionFrontendQueryStep>(MoveTemp(InFunc))))
	{
	}

	FFrontendQueryStep::FFrontendQueryStep(TUniquePtr<IFrontendQuerySource>&& InSource)
	:	StepExecuter(MakeUnique<FrontendQueryPrivate::FSourceStepExecuter>(MoveTemp(InSource)))
	{
	}

	FFrontendQueryStep::FFrontendQueryStep(TUniquePtr<IFrontendQueryStreamStep>&& InStep)
	:	StepExecuter(MakeUnique<FrontendQueryPrivate::FStreamStepExecuter>(MoveTemp(InStep)))
	{
	}
	
	FFrontendQueryStep::FFrontendQueryStep(TUniquePtr<IFrontendQueryTransformStep>&& InStep)
	:	StepExecuter(MakeUnique<FrontendQueryPrivate::FTransformStepExecuter>(MoveTemp(InStep)))
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

	FFrontendQueryStep::EResultModificationState FFrontendQueryStep::Execute(FFrontendQuerySelection& InOutResult)
	{
		if (StepExecuter.IsValid())
		{
			return StepExecuter->Execute(InOutResult);
		}

		return EResultModificationState::Unmodified;
	}

	FFrontendQueryStep::EResultModificationState FFrontendQueryStep::Increment(FFrontendQuerySelection& InOutResult)
	{
		if (StepExecuter.IsValid())
		{
			return StepExecuter->Increment(InOutResult);
		}

		return EResultModificationState::Unmodified;
	}

	FFrontendQueryStep::EResultModificationState FFrontendQueryStep::Merge(const FFrontendQuerySelection& InIncremental, FFrontendQuerySelection& InOutResult)
	{
		if (StepExecuter.IsValid())
		{
			return StepExecuter->Merge(InIncremental, InOutResult);
		}

		return EResultModificationState::Unmodified;
	}

	bool FFrontendQueryStep::IsMergeRequiredForIncremental() const
	{
		if (StepExecuter.IsValid())
		{
			return StepExecuter->IsMergeRequiredForIncremental();
		}

		return false;
	}

	void FFrontendQueryStep::Reset()
	{
		if (StepExecuter.IsValid())
		{
			StepExecuter->Reset();
		}
	}

	FFrontendQuery::FFrontendQuery()
	: Result(MakeShared<FFrontendQuerySelection, ESPMode::ThreadSafe>())
	{
	}

	const TArray<TUniquePtr<FFrontendQueryStep>>& FFrontendQuery::GetSteps() const
	{
		return Steps;
	}


	FFrontendQuery& FFrontendQuery::AddStreamLambdaStep(FStreamFunction&& InFunc)
	{
		return AddFunctionStep(MoveTemp(InFunc));
	}

	FFrontendQuery& FFrontendQuery::AddTransformLambdaStep(FTransformFunction&& InFunc)
	{
		return AddFunctionStep(MoveTemp(InFunc));
	}

	FFrontendQuery& FFrontendQuery::AddMapLambdaStep(FMapFunction&& InFunc)
	{
		return AddFunctionStep(MoveTemp(InFunc));
	}

	FFrontendQuery& FFrontendQuery::AddReduceLambdaStep(FReduceFunction&& InFunc)
	{
		return AddFunctionStep(MoveTemp(InFunc));
	}

	FFrontendQuery& FFrontendQuery::AddFilterLambdaStep(FFilterFunction&& InFunc)
	{
		return AddFunctionStep(MoveTemp(InFunc));
	}

	FFrontendQuery& FFrontendQuery::AddScoreLambdaStep(FScoreFunction&& InFunc)
	{
		return AddFunctionStep(MoveTemp(InFunc));
	}

	FFrontendQuery& FFrontendQuery::AddSortLambdaStep(FSortFunction&& InFunc)
	{
		return AddFunctionStep(MoveTemp(InFunc));
	}

	FFrontendQuery& FFrontendQuery::AddLimitLambdaStep(FLimitFunction&& InFunc)
	{
		return AddFunctionStep(MoveTemp(InFunc));
	}

	FFrontendQuery& FFrontendQuery::AddStep(TUniquePtr<FFrontendQueryStep>&& InStep)
	{
		if (ensure(InStep.IsValid()))
		{
			int32 StepIndex = Steps.Num();
			if (InStep->IsMergeRequiredForIncremental())
			{
				StepResultCache.Add(StepIndex);
			}

			Steps.Add(MoveTemp(InStep));
		}
		return *this;
	}

	FFrontendQuerySelectionView FFrontendQuery::Execute()
	{
		UpdateResult();

		return FFrontendQuerySelectionView(Result);
	}

	FFrontendQuerySelectionView FFrontendQuery::Reset()
	{
		Result->ResetStorageAndSelection();

		for (int32 StepIndex = 0; StepIndex < Steps.Num(); StepIndex++)
		{
			if (Steps[StepIndex].IsValid())
			{
				Steps[StepIndex]->Reset();
			}
		}
		
		return FFrontendQuerySelectionView(Result);
	}

	FFrontendQuerySelectionView FFrontendQuery::GetSelection() const
	{
		return FFrontendQuerySelectionView(Result);
	}

	void FFrontendQuery::UpdateResult()
	{
		FFrontendQuerySelection IncrementalResult;

		EResultModificationState IncrementalResultState = EResultModificationState::Unmodified;

		// Perform incremental update sequentially
		for (int32 StepIndex = 0; StepIndex < Steps.Num(); StepIndex++)
		{
			if (ensure(Steps[StepIndex].IsValid()))
			{
				FFrontendQueryStep& Step = *Steps[StepIndex];

				IncrementalResultState = Step.Increment(IncrementalResult);

				const bool bMergeIncrementalResults = Step.IsMergeRequiredForIncremental() && (EResultModificationState::Modified == IncrementalResultState);

				if (bMergeIncrementalResults)
				{
					const bool bIsResultCacheEmpty = StepResultCache[StepIndex].GetSelection().Num() == 0;

					if (bIsResultCacheEmpty)
					{
						// If the prior result for this step is empty, we can continue
						// using the incremental path.
						StepResultCache[StepIndex] = IncrementalResult;
					}
					else
					{
						Step.Merge(IncrementalResult, StepResultCache[StepIndex]);

						*Result = StepResultCache[StepIndex];

						// After results are merged, downstream steps can no longer 
						// work on incremental data. Run downstream steps on full
						// result set.
						ExecuteSteps(StepIndex + 1);
						return;
					}
				}
			}
		}

		if (Steps.Num() > 0)
		{
			if (ensure(Steps.Last().IsValid()))
			{
				FFrontendQueryStep& Step = *Steps.Last();
				if (EResultModificationState::Modified == IncrementalResultState)
				{
					// If all incremental steps are performed and result is modified,
					// merge the incremental result with the output result.
					Step.Merge(IncrementalResult, *Result);
				}
			}
		}
	}

	void FFrontendQuery::ExecuteSteps(int32 InStartStepIndex)
	{
		for (int32 StepIndex = InStartStepIndex; StepIndex < Steps.Num(); StepIndex++)
		{
			if (ensure(Steps[StepIndex].IsValid()))
			{
				FFrontendQueryStep& Step = *Steps[StepIndex];

				EResultModificationState ExecuteResultState = Step.Execute(*Result);

				const bool bNeedsStepResultCached = (EResultModificationState::Modified == ExecuteResultState) && Step.IsMergeRequiredForIncremental();
				if (bNeedsStepResultCached)
				{
					StepResultCache[StepIndex] = *Result;
				}
			}
		}
	}
}

