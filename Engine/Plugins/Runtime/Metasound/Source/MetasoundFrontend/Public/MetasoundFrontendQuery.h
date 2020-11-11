// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundFrontendDataLayout.h"
#include "Misc/Variant.h"

namespace Metasound
{
	/** FFrontendQueryEntry represents one value in the query. It contains a key,
	 * value and score. 
	 */
	struct FFrontendQueryEntry
	{
		using FValue = TVariant<FMetasoundDocument, FMetasoundClassDescription, FMetasoundNodeDescription, FMetasoundGraphDescription>;
		using FKey = int32;

		static constexpr FKey InvalidKey = 0xFFFFFFFF;

		FKey Key = InvalidKey;
		FValue Value;
		float Score = 0.f;

		template<typename... ArgTypes>
		FFrontendQueryEntry(ArgTypes&&... Args)
		:	Value(Forward<ArgTypes>(Args)...)
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
			FReduceOutputView(TArrayView<FFrontendQueryEntry*> InEntries);
			~FReduceOutputView() = default;

			/** Add a result to the selection */
			void Add(FFrontendQueryEntry& InResult);

			/** Get selection which was also in initial entries. */
			TArrayView<FFrontendQueryEntry*> GetSelectedInitialEntries();

			/** Get selection which was not in the initial entries. */
			TArrayView<FFrontendQueryEntry> GetSelectedNewEntries();

		private:

			TArray<FFrontendQueryEntry*> InitialEntries;

			TArray<FFrontendQueryEntry*> ExistingEntryPointers;
			TArray<FFrontendQueryEntry> NewEntryStorage;
		};

		/** Get an array of all entries stored in this selection. */
		TArrayView<FFrontendQueryEntry> GetStorage();

		/** Get an array of all entries stored in this selection. */
		TArrayView<const FFrontendQueryEntry> GetStorage() const;

		/** Get an array of all entries selected. */
		TArrayView<FFrontendQueryEntry*> GetSelection();

		/** Get an array of all entries selected. */
		TArrayView<const FFrontendQueryEntry * const> GetSelection() const;

		/** Add entries to both storage and current selection. */
		void AppendToStorageAndSelection(TArrayView<const FFrontendQueryEntry> InEntries);

		/** Add entries to both storage and current selection. */
		void AppendToStorageAndSelection(FReduceOutputView& InReduceView);

		/** Reset both storage and selection. */
		void ResetStorageAndSelection();

		/** Reset selection, but keep storage. */
		void ResetSelection();

		/** Set selection. All entries must already be in storage. */
		void SetSelection(TArrayView<FFrontendQueryEntry*> InEntries);

		/** Add to selection. The entry must already be in storage. */
		void AddToSelection(FFrontendQueryEntry* InEntry);

		/** Append entries to selection. Each selection must already be in storage. */
		void AppendToSelection(TArrayView<FFrontendQueryEntry*> InEntries);

	private:
		TArray<FFrontendQueryEntry*> Selection;
		TArray<FFrontendQueryEntry> Storage;
	};

	/** FFrontendQuerySelectionView holds the result of query. */
	class METASOUNDFRONTEND_API FFrontendQuerySelectionView
	{
		FFrontendQuerySelectionView() = delete;
	public:

		/** The FFrontendQuerySelectionView is constructed with an existing
		 * FFrontendQuerySelection.
		 */
		FFrontendQuerySelectionView(TUniquePtr<FFrontendQuerySelection>&& InResult);

		/** Get all the stored results. */
		TArrayView<const FFrontendQueryEntry> GetStorage() const;

		/** Get the selected results. */
		TArrayView<const FFrontendQueryEntry* const> GetSelection() const;

	private:
		TUniquePtr<FFrontendQuerySelection> Result;
	};

	/** Interface for an individual step in a query */
	class IFrontendQueryStep
	{
		public:
			virtual ~IFrontendQueryStep() = default;
	};

	/** Interface for a query step which generates entries. */
	class IFrontendQueryGenerateStep : public IFrontendQueryStep
	{
		public:
			virtual ~IFrontendQueryGenerateStep() = default;
			virtual void Generate(TArray<FFrontendQueryEntry>& OutEntries) const = 0;
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
			virtual void Reduce(FFrontendQueryEntry::FKey InKey, TArrayView<FFrontendQueryEntry*>& InEntries, FReduceOutputView& OutResult) const = 0;
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

		using FReduceOutputView = FFrontendQuerySelection::FReduceOutputView;

		using FGenerateFunction = TFunction<void (TArray<FFrontendQueryEntry>&)>;
		using FMapFunction = TFunction<FFrontendQueryEntry::FKey (const FFrontendQueryEntry&)>;
		using FReduceFunction = TFunction<void (FFrontendQueryEntry::FKey, TArrayView<FFrontendQueryEntry*>, FReduceOutputView& )>;
		using FFilterFunction = TFunction<bool (const FFrontendQueryEntry&)>;
		using FScoreFunction = TFunction<float (const FFrontendQueryEntry&)>;
		using FSortFunction = TFunction<bool (const FFrontendQueryEntry& InEntryLHS, const FFrontendQueryEntry& InEntryRHS)>;
		using FLimitFunction = TFunction<int32 ()>;

		/** Create query step using TFunction or lambda. */
		FFrontendQueryStep(FGenerateFunction InFunc);
		FFrontendQueryStep(FMapFunction InFunc);
		FFrontendQueryStep(FReduceFunction InFunc);
		FFrontendQueryStep(FFilterFunction InFilt);
		FFrontendQueryStep(FScoreFunction InScore);
		FFrontendQueryStep(FSortFunction InSort);
		FFrontendQueryStep(FLimitFunction InLimit);

		/** Create a query step using a IFrotnedQueryStep */
		FFrontendQueryStep(TUniquePtr<IFrontendQueryGenerateStep>&& InStep);
		FFrontendQueryStep(TUniquePtr<IFrontendQueryMapStep>&& InStep);
		FFrontendQueryStep(TUniquePtr<IFrontendQueryReduceStep>&& InStep);
		FFrontendQueryStep(TUniquePtr<IFrontendQueryFilterStep>&& InStep);
		FFrontendQueryStep(TUniquePtr<IFrontendQueryScoreStep>&& InStep);
		FFrontendQueryStep(TUniquePtr<IFrontendQuerySortStep>&& InStep);
		FFrontendQueryStep(TUniquePtr<IFrontendQueryLimitStep>&& InStep);

		/** Execute a step on a selection. */
		void ExecuteStep(FFrontendQuerySelection& InOutResult);

		struct IStepExecuter
		{
			virtual ~IStepExecuter() = default;
			virtual void ExecuteStep(FFrontendQuerySelection& InOutResult) const = 0;
		};

	private:

		TUniquePtr<IStepExecuter> StepExecuter;
	};

	/** FFrontendQuery contains a set of query steps which can be executed 
	 * to produce a FFrontendQuerySelectionView
	 */
	class METASOUNDFRONTEND_API FFrontendQuery
	{
	public:

		using FGenerateFunction = FFrontendQueryStep::FGenerateFunction;
		using FMapFunction = FFrontendQueryStep::FMapFunction;
		using FReduceFunction = FFrontendQueryStep::FReduceFunction;
		using FFilterFunction = FFrontendQueryStep::FFilterFunction;
		using FScoreFunction = FFrontendQueryStep::FScoreFunction;
		using FSortFunction = FFrontendQueryStep::FSortFunction;
		using FLimitFunction = FFrontendQueryStep::FLimitFunction;

		FFrontendQuery() = default;

		/** Return all steps in a query. */
		const TArray<TUniquePtr<FFrontendQueryStep>>& GetSteps() const;

		/** Add a step to the query. */
		template<typename StepType, typename... ArgTypes>
		FFrontendQuery& AddStep(ArgTypes&&... Args)
		{
			return AddStep(MakeUnique<FFrontendQueryStep>(MakeUnique<StepType>(Forward<ArgTypes>(Args)...)));
		}

		template<typename FuncType>
		FFrontendQuery& AddFunctionStep(TFunction<FuncType> InFunc)
		{
			return AddStep(MakeUnique<FFrontendQueryStep>(InFunc));
		}

		FFrontendQuery& AddGenerateLambdaStep(FGenerateFunction InFunc);
		FFrontendQuery& AddMapLambdaStep(FMapFunction InFunc);
		FFrontendQuery& AddReduceLambdaStep(FReduceFunction InFunc);
		FFrontendQuery& AddFilterLambdaStep(FFilterFunction InFunc);
		FFrontendQuery& AddScoreLambdaStep(FScoreFunction InFunc);
		FFrontendQuery& AddSortLambdaStep(FSortFunction InFunc);
		FFrontendQuery& AddLimitLambdaStep(FLimitFunction InFunc);

		/** Add a step to the query. */
		FFrontendQuery& AddStep(TUniquePtr<FFrontendQueryStep>&& InStep);

		/** Calls all steps in the query and returns the selection. */
		FFrontendQuerySelectionView ExecuteQuery();

	private:

		TUniquePtr<FFrontendQuerySelection> Result;

		TArray<TUniquePtr<FFrontendQueryStep>> Steps;
	};
}
