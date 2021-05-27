// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Math/NumericLimits.h"
#include "MetasoundFrontendDocument.h"
#include "Misc/TVariant.h"

namespace Metasound
{
	/** FFrontendQueryEntry represents one value in the query. It contains a key,
	 * value and score. 
	 */
	struct FFrontendQueryEntry
	{
		using FValue = TVariant<FMetasoundFrontendDocument, FMetasoundFrontendClass, FMetasoundFrontendNode>;
		using FKey = int32;

		static constexpr FKey InvalidKey = TNumericLimits<int32>::Max();

		FKey Key = InvalidKey;
		FValue Value;
		float Score = 0.f;

		FFrontendQueryEntry(const FFrontendQueryEntry&) = default;
		FFrontendQueryEntry& operator=(const FFrontendQueryEntry&) = default;

		FFrontendQueryEntry(FValue&& InValue)
		:	Value(MoveTemp(InValue))
		{
		}

		FFrontendQueryEntry(const FValue& InValue)
		:	Value(InValue)
		{
		}
	};

	/** FFrontendQuerySelection represents a selection utilized during a query.
	 *
	 * Note: FFrontendQuerySelection owns the memory for the related entries. 
	 */
	class METASOUNDFRONTEND_API FFrontendQuerySelection
	{
	public:
		/** FReduceOutputView provides an interface for storing results during
		 * a "Reduce" query step.  It is primarily used to only expose operations
		 * which are acceptable to call on a FFrontendQuerySelection during a 
		 * call to "Reduce".
		 */
		class FReduceOutputView
		{
			// deleted constructors
			FReduceOutputView(const FReduceOutputView&) = delete;
			FReduceOutputView(FReduceOutputView&&) = delete;

			// deleted operators
			FReduceOutputView& operator=(const FReduceOutputView&) = delete;
			FReduceOutputView& operator=(FReduceOutputView&&) = delete;

		public:
			// Set initial entries which are stored elsewhere
			FReduceOutputView(TArrayView<FFrontendQueryEntry* const> InEntries);
			~FReduceOutputView() = default;

			/** Add a result to the selection */
			void Add(FFrontendQueryEntry& InResult);



		private:
			friend class FFrontendQuerySelection;

			TArray<FFrontendQueryEntry*> InitialEntries;

			TArray<FFrontendQueryEntry*> SelectedExistingEntries;
			TArray<TUniquePtr<FFrontendQueryEntry>> SelectedNewEntries;
		};

		FFrontendQuerySelection() = default;
		FFrontendQuerySelection(const FFrontendQuerySelection&);
		FFrontendQuerySelection& operator=(const FFrontendQuerySelection&);

		/** Get an array of all entries selected. */
		TArrayView<FFrontendQueryEntry*> GetSelection();

		/** Get an array of all entries selected. */
		TArrayView<const FFrontendQueryEntry * const> GetSelection() const;

		/** Add entries to both storage and current selection. */
		void AppendToStorageAndSelection(const FFrontendQuerySelection& InSelection);

		/** Add entries to both storage and current selection. */
		void AppendToStorageAndSelection(FFrontendQuerySelection&& InSelection);

		/** Add entries to both storage and current selection. */
		void AppendToStorageAndSelection(TArrayView<const FFrontendQueryEntry> InEntries);

		/** Add entries to both storage and current selection. */
		void AppendToStorageAndSelection(TArrayView<const FFrontendQueryEntry* const> InEntries);

		/** Add entries to both storage and current selection. */
		void AppendToStorageAndSelection(FReduceOutputView&& InReduceView);


		/** Reset both storage and selection. */
		void ResetStorageAndSelection();

		/** Reset selection, but keep storage. */
		void ResetSelection();

		/** Filters the selection by calling Func on each entry. If Func returns
		 * false, the entry is removed.
		 */
		template<typename FilterFuncType>
		int32 FilterSelection(FilterFuncType Func)
		{
			auto InvertedFilter = [&](const FFrontendQueryEntry* InEntry) -> bool
			{
				return !Func(InEntry);
			};
			return Selection.RemoveAll(InvertedFilter);
		}

		/** Set selection. All entries must already be in storage. */
		void SetSelection(TArrayView<FFrontendQueryEntry*> InEntries);

		/** Add to selection. The entry must already be in storage. */
		void AddToSelection(FFrontendQueryEntry* InEntry);

		/** Append entries to selection. Each selection must already be in storage. */
		void AppendToSelection(TArrayView<FFrontendQueryEntry * const> InEntries);

	private:
		void AppendToStorageAndSelection(TArray<TUniquePtr<FFrontendQueryEntry>>&& InEntries);
		void ShrinkStorageToSelection();

		TArray<FFrontendQueryEntry*> Selection;
		TArray<TUniquePtr<FFrontendQueryEntry>> Storage;
	};

	/** FFrontendQuerySelectionView holds the result of query. */
	class METASOUNDFRONTEND_API FFrontendQuerySelectionView
	{
		FFrontendQuerySelectionView() = delete;
	public:

		/** The FFrontendQuerySelectionView is constructed with an existing
		 * FFrontendQuerySelection.
		 */
		FFrontendQuerySelectionView(TSharedRef<const FFrontendQuerySelection, ESPMode::ThreadSafe> InResult);

		/** Get the selected results. */
		TArrayView<const FFrontendQueryEntry* const> GetSelection() const;

	private:
		TSharedRef<const FFrontendQuerySelection, ESPMode::ThreadSafe> Result;
	};

	class IFrontendQuerySource
	{
		public:
			virtual ~IFrontendQuerySource() = default;

			virtual void Stream(TArray<FFrontendQueryEntry>& OutEntries) = 0;
			virtual void Reset() = 0;
	};

	/** Interface for an individual step in a query */
	class IFrontendQueryStep
	{
		public:
			virtual ~IFrontendQueryStep() = default;
	};

	/** Interface for a query step which streams new entries. */
	class IFrontendQueryStreamStep : public IFrontendQueryStep
	{
		public:
			virtual ~IFrontendQueryStreamStep() = default;
			virtual void Stream(TArray<FFrontendQueryEntry>& OutEntries) = 0;
	};

	/** Interface for a query step which maps entries to keys. */
	class IFrontendQueryMapStep : public IFrontendQueryStep
	{
		public:
			virtual ~IFrontendQueryMapStep() = default;
			virtual FFrontendQueryEntry::FKey Map(const FFrontendQueryEntry& InEntry) const = 0;
	};

	/** Interface for a query step which reduces entries with the same key. */
	class IFrontendQueryReduceStep : public IFrontendQueryStep
	{
		public:
			using FReduceOutputView = FFrontendQuerySelection::FReduceOutputView;

			virtual ~IFrontendQueryReduceStep() = default;
			virtual void Reduce(FFrontendQueryEntry::FKey InKey, TArrayView<FFrontendQueryEntry * const>& InEntries, FReduceOutputView& OutResult) const = 0;
	};

	/** Interface for a query step which filters entries. */
	class IFrontendQueryFilterStep : public IFrontendQueryStep
	{
		public:
			virtual ~IFrontendQueryFilterStep() = default;
			virtual bool Filter(const FFrontendQueryEntry& InEntry) const = 0;
	};

	/** Interface for a query step which scores entries. */
	class IFrontendQueryScoreStep : public IFrontendQueryStep
	{
		public:
			virtual ~IFrontendQueryScoreStep() = default;
			virtual float Score(const FFrontendQueryEntry& InEntry) const = 0;
	};

	/** Interface for a query step which sorts entries. */
	class IFrontendQuerySortStep : public IFrontendQueryStep
	{
		public:
			virtual ~IFrontendQuerySortStep() = default;
			virtual bool Sort(const FFrontendQueryEntry& InEntryLHS, const FFrontendQueryEntry& InEntryRHS) const = 0;
	};

	class IFrontendQueryLimitStep : public IFrontendQueryStep
	{
		public:
			virtual ~IFrontendQueryLimitStep() = default;
			virtual int32 Limit() const = 0;
	};

	/** FFrontendQueryStep wraps all the support IFrontenQueryStep interfaces 
	 * and supplies unified `ExecuteStep(...)` member function.
	 */
	class METASOUNDFRONTEND_API FFrontendQueryStep
	{
		FFrontendQueryStep() = delete;

	public:
		enum class EResultModificationState : uint8
		{
			Modified,
			Unmodified,
		};

		/* Interface for executing a step in the query. */
		struct IStepExecuter
		{
			using EResultModificationState = FFrontendQueryStep::EResultModificationState;

			virtual ~IStepExecuter() = default;

			// Perform an incremental step. Assume a previous result already exists. 
			virtual EResultModificationState Increment(FFrontendQuerySelection& InOutResult) = 0;

			// Merge an incremental result with the prior result from this step.
			virtual EResultModificationState Merge(const FFrontendQuerySelection& InIncremental, FFrontendQuerySelection& InOutResult) = 0;

			// Execute step. Assume not other prior results exist.
			virtual EResultModificationState Execute(FFrontendQuerySelection& InOutResult) = 0;

			// Reset internal state.
			virtual void Reset() = 0;

			// Returns true if a merge is required for a modified incremental update.
			// 
			// If this is true, Merge() will be called after Incremental() if Incremental
			// returns EResultModificationState::Modified. 
			virtual bool IsMergeRequiredForIncremental() const = 0;
		};

		using FReduceOutputView = FFrontendQuerySelection::FReduceOutputView;

		using FStreamFunction = TUniqueFunction<void (TArray<FFrontendQueryEntry>&)>;
		using FMapFunction = TFunction<FFrontendQueryEntry::FKey (const FFrontendQueryEntry&)>;
		using FReduceFunction = TFunction<void (FFrontendQueryEntry::FKey, TArrayView<FFrontendQueryEntry * const>, FReduceOutputView& )>;
		using FFilterFunction = TFunction<bool (const FFrontendQueryEntry&)>;
		using FScoreFunction = TFunction<float (const FFrontendQueryEntry&)>;
		using FSortFunction = TFunction<bool (const FFrontendQueryEntry& InEntryLHS, const FFrontendQueryEntry& InEntryRHS)>;
		using FLimitFunction = TFunction<int32 ()>;

		/** Create query step using TFunction or lambda. */
		FFrontendQueryStep(FStreamFunction&& InFunc);
		FFrontendQueryStep(FMapFunction&& InFunc);
		FFrontendQueryStep(FReduceFunction&& InFunc);
		FFrontendQueryStep(FFilterFunction&& InFilt);
		FFrontendQueryStep(FScoreFunction&& InScore);
		FFrontendQueryStep(FSortFunction&& InSort);
		FFrontendQueryStep(FLimitFunction&& InLimit);

		/** Create a query step using a IFrotnedQueryStep */
		FFrontendQueryStep(TUniquePtr<IFrontendQuerySource>&& InSource);
		FFrontendQueryStep(TUniquePtr<IFrontendQueryStreamStep>&& InStep);
		FFrontendQueryStep(TUniquePtr<IFrontendQueryMapStep>&& InStep);
		FFrontendQueryStep(TUniquePtr<IFrontendQueryReduceStep>&& InStep);
		FFrontendQueryStep(TUniquePtr<IFrontendQueryFilterStep>&& InStep);
		FFrontendQueryStep(TUniquePtr<IFrontendQueryScoreStep>&& InStep);
		FFrontendQueryStep(TUniquePtr<IFrontendQuerySortStep>&& InStep);
		FFrontendQueryStep(TUniquePtr<IFrontendQueryLimitStep>&& InStep);


		// Perform an incremental step. Assume a previous result already exists. 
		EResultModificationState Increment(FFrontendQuerySelection& InOutResult);

		// Merge an incremental result with the prior result from this step.
		EResultModificationState Merge(const FFrontendQuerySelection& InIncremental, FFrontendQuerySelection& InOutResult);

		// Execute step. Assume not other prior results exist.
		EResultModificationState Execute(FFrontendQuerySelection& InOutResult);

		// Returns true if a merge is required for a modified incremental update.
		// 
		// If this is true, Merge() will be called after Incremental() if Incremental
		// returns EResultModificationState::Modified. 
		bool IsMergeRequiredForIncremental() const;

		// Reset internal state.
		void Reset();

	private:

		TUniquePtr<IStepExecuter> StepExecuter;
	};

	/** FFrontendQuery contains a set of query steps which can be executed 
	 * to produce a FFrontendQuerySelectionView
	 */
	class METASOUNDFRONTEND_API FFrontendQuery
	{
	public:

		using EResultModificationState = FFrontendQueryStep::EResultModificationState;
		using FStreamFunction = FFrontendQueryStep::FStreamFunction;
		using FMapFunction = FFrontendQueryStep::FMapFunction;
		using FReduceFunction = FFrontendQueryStep::FReduceFunction;
		using FFilterFunction = FFrontendQueryStep::FFilterFunction;
		using FScoreFunction = FFrontendQueryStep::FScoreFunction;
		using FSortFunction = FFrontendQueryStep::FSortFunction;
		using FLimitFunction = FFrontendQueryStep::FLimitFunction;

		FFrontendQuery();

		FFrontendQuery(const FFrontendQuery&) = delete;
		FFrontendQuery& operator=(const FFrontendQuery&) = delete;

		/** Return all steps in a query. */
		const TArray<TUniquePtr<FFrontendQueryStep>>& GetSteps() const;

		/** Add a step to the query. */
		template<typename StepType, typename... ArgTypes>
		FFrontendQuery& AddStep(ArgTypes&&... Args)
		{
			return AddStep(MakeUnique<FFrontendQueryStep>(MakeUnique<StepType>(Forward<ArgTypes>(Args)...)));
		}

		template<typename FuncType>
		FFrontendQuery& AddFunctionStep(FuncType&& InFunc)
		{
			return AddStep(MakeUnique<FFrontendQueryStep>(MoveTemp(InFunc)));
		}

		FFrontendQuery& AddStreamLambdaStep(FStreamFunction&& InFunc);
		FFrontendQuery& AddMapLambdaStep(FMapFunction&& InFunc);
		FFrontendQuery& AddReduceLambdaStep(FReduceFunction&& InFunc);
		FFrontendQuery& AddFilterLambdaStep(FFilterFunction&& InFunc);
		FFrontendQuery& AddScoreLambdaStep(FScoreFunction&& InFunc);
		FFrontendQuery& AddSortLambdaStep(FSortFunction&& InFunc);
		FFrontendQuery& AddLimitLambdaStep(FLimitFunction&& InFunc);

		/** Add a step to the query. */
		FFrontendQuery& AddStep(TUniquePtr<FFrontendQueryStep>&& InStep);

		/** Calls all steps in the query and returns the selection. */
		FFrontendQuerySelectionView Execute();

		/** Resets the query result by removing all entries. */
		FFrontendQuerySelectionView Reset();

		/** Returns the current result. */
		FFrontendQuerySelectionView GetSelection() const;

	private:

		void UpdateResult();
		void ExecuteSteps(int32 InStartStepIndex);

		TSharedRef<FFrontendQuerySelection, ESPMode::ThreadSafe> Result;

		TArray<TUniquePtr<FFrontendQueryStep>> Steps;
		TMap<int32, FFrontendQuerySelection> StepResultCache;
	};
}
