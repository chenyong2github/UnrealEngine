// Copyright Epic Games, Inc. All Rights Reserved.

#include "EntitySystem/MovieSceneSequenceUpdaters.h"
#include "EntitySystem/MovieSceneSequenceInstance.h"
#include "EntitySystem/MovieSceneSequenceInstanceHandle.h"
#include "EntitySystem/MovieSceneEntitySystemLinker.h"
#include "EntitySystem/MovieSceneEntitySystemRunner.h"
#include "EntitySystem/MovieSceneEntitySystem.h"

#include "Templates/UniquePtr.h"
#include "Containers/BitArray.h"
#include "Containers/SortedMap.h"

#include "MovieSceneSequence.h"
#include "MovieSceneSequenceID.h"
#include "Evaluation/MovieScenePlayback.h"
#include "Compilation/MovieSceneCompiledDataManager.h"

#include "Evaluation/MovieSceneEvaluationTemplateInstance.h"

#include "MovieSceneTimeHelpers.h"

#include "Algo/IndexOf.h"
#include "Algo/Transform.h"

namespace UE
{
namespace MovieScene
{

/** Flat sequence updater (ie, no hierarchy) */
struct FSequenceUpdater_Flat : ISequenceUpdater
{
	explicit FSequenceUpdater_Flat(FMovieSceneCompiledDataID InCompiledDataID);
	~FSequenceUpdater_Flat();

	virtual void DissectContext(UMovieSceneEntitySystemLinker* Linker, IMovieScenePlayer* InPlayer, const FMovieSceneContext& Context, TArray<TRange<FFrameTime>>& OutDissections) override;
	virtual void Start(UMovieSceneEntitySystemLinker* Linker, FInstanceHandle InstanceHandle, IMovieScenePlayer* InPlayer, const FMovieSceneContext& InContext) override;
	virtual void Update(UMovieSceneEntitySystemLinker* Linker, FInstanceHandle InstanceHandle, IMovieScenePlayer* InPlayer, const FMovieSceneContext& Context) override;
	virtual void Finish(UMovieSceneEntitySystemLinker* Linker, FInstanceHandle InstanceHandle, IMovieScenePlayer* InPlayer) override;
	virtual void InvalidateCachedData(UMovieSceneEntitySystemLinker* Linker) override;
	virtual void Destroy(UMovieSceneEntitySystemLinker* Linker) override;
	virtual TUniquePtr<ISequenceUpdater> MigrateToHierarchical() override;
	virtual FInstanceHandle FindSubInstance(FMovieSceneSequenceID SubSequenceID) const override { return FInstanceHandle(); }

private:

	TRange<FFrameNumber> CachedEntityRange;

	TOptional<TArray<FFrameTime>> CachedDeterminismFences;
	FMovieSceneCompiledDataID CompiledDataID;
};

/** Hierarchical sequence updater */
struct FSequenceUpdater_Hierarchical : ISequenceUpdater
{
	explicit FSequenceUpdater_Hierarchical(FMovieSceneCompiledDataID InCompiledDataID);

	~FSequenceUpdater_Hierarchical();

	virtual void DissectContext(UMovieSceneEntitySystemLinker* Linker, IMovieScenePlayer* InPlayer, const FMovieSceneContext& Context, TArray<TRange<FFrameTime>>& OutDissections) override;
	virtual void Start(UMovieSceneEntitySystemLinker* Linker, FInstanceHandle InstanceHandle, IMovieScenePlayer* InPlayer, const FMovieSceneContext& InContext) override;
	virtual void Update(UMovieSceneEntitySystemLinker* Linker, FInstanceHandle InstanceHandle, IMovieScenePlayer* InPlayer, const FMovieSceneContext& Context) override;
	virtual void Finish(UMovieSceneEntitySystemLinker* Linker, FInstanceHandle InstanceHandle, IMovieScenePlayer* InPlayer) override;
	virtual void InvalidateCachedData(UMovieSceneEntitySystemLinker* Linker) override;
	virtual void Destroy(UMovieSceneEntitySystemLinker* Linker) override;
	virtual TUniquePtr<ISequenceUpdater> MigrateToHierarchical() override { return nullptr; }
	virtual FInstanceHandle FindSubInstance(FMovieSceneSequenceID SubSequenceID) const override { return SequenceInstances.FindRef(SubSequenceID); }

private:

	TRange<FFrameNumber> UpdateEntitiesForSequence(const FMovieSceneEntityComponentField* ComponentField, FFrameTime SequenceTime, FMovieSceneEvaluationFieldEntitySet& OutEntities);

	FInstanceHandle GetOrCreateSequenceInstance(IMovieScenePlayer* InPlayer, FInstanceRegistry* InstanceRegistry, FInstanceHandle RootInstanceHandle, FMovieSceneSequenceID SequenceID);

private:

	TRange<FFrameNumber> CachedEntityRange;

	TSortedMap<FMovieSceneSequenceID, FInstanceHandle, TInlineAllocator<8>> SequenceInstances;

	FMovieSceneCompiledDataID CompiledDataID;
};


void DissectRange(TArrayView<const FFrameTime> InDissectionTimes, const TRange<FFrameTime>& Bounds, TArray<TRange<FFrameTime>>& OutDissections)
{
	if (InDissectionTimes.Num() == 0)
	{
		return;
	}

	TRangeBound<FFrameTime> LowerBound = Bounds.GetLowerBound();

	for (int32 Index = 0; Index < InDissectionTimes.Num(); ++Index)
	{
		FFrameTime DissectionTime = InDissectionTimes[Index];
		if (Index > 0 && InDissectionTimes[Index-1] == DissectionTime)
		{
			// Skip duplicates
			continue;
		}

		TRange<FFrameTime> Dissection(LowerBound, TRangeBound<FFrameTime>::Exclusive(DissectionTime));
		ensureMsgf(Bounds.Contains(Dissection), TEXT("Dissection specified for a range outside of the current bounds"));

		OutDissections.Add(Dissection);

		LowerBound = TRangeBound<FFrameTime>::Inclusive(DissectionTime);
	}

	TRange<FFrameTime> TailRange(LowerBound, Bounds.GetUpperBound());
	if (!TailRange.IsEmpty())
	{
		OutDissections.Add(TailRange);
	}
}

TArrayView<const FFrameTime> GetFencesWithinRange(TArrayView<const FFrameTime> Fences, const TRange<FFrameNumber>& Boundary)
{
	if (Fences.Num() == 0 || Boundary.IsEmpty())
	{
		return TArrayView<const FFrameTime>();
	}

	// Take care to include or exclude the lower bound of the range if it's on a whole frame numbe
	const int32 StartFence = Boundary.GetLowerBound().IsClosed() ? Algo::LowerBound(Fences, DiscreteInclusiveLower(Boundary.GetLowerBoundValue())) : 0;
	if (StartFence >= Fences.Num())
	{
		return TArrayView<const FFrameTime>();
	}

	const int32 EndFence = Boundary.GetUpperBound().IsClosed() ? Algo::UpperBound(Fences, DiscreteExclusiveUpper(Boundary.GetUpperBoundValue())) : Fences.Num();
	const int32 NumFences = FMath::Max(0, EndFence - StartFence);
	if (NumFences == 0)
	{
		return TArrayView<const FFrameTime>();
	}

	return MakeArrayView(Fences.GetData() + StartFence, NumFences);
}


void ISequenceUpdater::FactoryInstance(TUniquePtr<ISequenceUpdater>& OutPtr, UMovieSceneCompiledDataManager* CompiledDataManager, FMovieSceneCompiledDataID CompiledDataID)
{
	const bool bHierarchical = CompiledDataManager->FindHierarchy(CompiledDataID) != nullptr;

	if (!OutPtr)
	{
		if (!bHierarchical)
		{
			OutPtr = MakeUnique<FSequenceUpdater_Flat>(CompiledDataID);
		}
		else
		{
			OutPtr = MakeUnique<FSequenceUpdater_Hierarchical>(CompiledDataID);
		}
	}
	else if (bHierarchical)
	{ 
		TUniquePtr<ISequenceUpdater> NewHierarchical = OutPtr->MigrateToHierarchical();
		if (NewHierarchical)
		{
			OutPtr = MoveTemp(NewHierarchical);
		}
	}
}

FSequenceUpdater_Flat::FSequenceUpdater_Flat(FMovieSceneCompiledDataID InCompiledDataID)
	: CompiledDataID(InCompiledDataID)
{
	CachedEntityRange = TRange<FFrameNumber>::Empty();
}

FSequenceUpdater_Flat::~FSequenceUpdater_Flat()
{
}

TUniquePtr<ISequenceUpdater> FSequenceUpdater_Flat::MigrateToHierarchical()
{
	return MakeUnique<FSequenceUpdater_Hierarchical>(CompiledDataID);
}

void FSequenceUpdater_Flat::DissectContext(UMovieSceneEntitySystemLinker* Linker, IMovieScenePlayer* InPlayer, const FMovieSceneContext& Context, TArray<TRange<FFrameTime>>& OutDissections)
{
	if (!CachedDeterminismFences.IsSet())
	{
		UMovieSceneCompiledDataManager* CompiledDataManager = InPlayer->GetEvaluationTemplate().GetCompiledDataManager();
		TArrayView<const FFrameTime>    DeterminismFences   = CompiledDataManager->GetEntry(CompiledDataID).DeterminismFences;

		if (DeterminismFences.Num() != 0)
		{
			CachedDeterminismFences = TArray<FFrameTime>(DeterminismFences.GetData(), DeterminismFences.Num());
		}
		else
		{
			CachedDeterminismFences.Emplace();
		}
	}

	if (CachedDeterminismFences->Num() != 0)
	{
		TArrayView<const FFrameTime> TraversedFences = GetFencesWithinRange(CachedDeterminismFences.GetValue(), Context.GetFrameNumberRange());
		UE::MovieScene::DissectRange(TraversedFences, Context.GetRange(), OutDissections);
	}
}

void FSequenceUpdater_Flat::Start(UMovieSceneEntitySystemLinker* Linker, FInstanceHandle InstanceHandle, IMovieScenePlayer* InPlayer, const FMovieSceneContext& InContext)
{
}

void FSequenceUpdater_Flat::Update(UMovieSceneEntitySystemLinker* Linker, FInstanceHandle InstanceHandle, IMovieScenePlayer* InPlayer, const FMovieSceneContext& Context)
{
	FSequenceInstance& SequenceInstance = Linker->GetInstanceRegistry()->MutateInstance(InstanceHandle);
	SequenceInstance.SetContext(Context);

	const FMovieSceneEntityComponentField* ComponentField = InPlayer->GetEvaluationTemplate().GetCompiledDataManager()->FindEntityComponentField(CompiledDataID);
	UMovieSceneSequence* Sequence = InPlayer->GetEvaluationTemplate().GetSequence(MovieSceneSequenceID::Root);
	if (Sequence == nullptr)
	{
		SequenceInstance.Ledger.UnlinkEverything(Linker);
		return;
	}

	FMovieSceneEvaluationFieldEntitySet EntitiesScratch;

	const bool bOutsideCachedRange = !CachedEntityRange.Contains(Context.GetTime().FrameNumber);
	if (bOutsideCachedRange)
	{
		if (ComponentField)
		{
			ComponentField->QueryPersistentEntities(Context.GetTime().FrameNumber, CachedEntityRange, EntitiesScratch);
		}
		else
		{
			CachedEntityRange = TRange<FFrameNumber>::All();
		}

		FEntityImportSequenceParams Params;
		Params.InstanceHandle = InstanceHandle;
		Params.DefaultCompletionMode = Sequence->DefaultCompletionMode;
		Params.HierarchicalBias = 0;

		SequenceInstance.Ledger.UpdateEntities(Linker, Params, ComponentField, EntitiesScratch);
	}

	// Update any one-shot entities for the current frame
	if (ComponentField && ComponentField->HasAnyOneShotEntities())
	{
		EntitiesScratch.Reset();
		ComponentField->QueryOneShotEntities(Context.GetFrameNumberRange(), EntitiesScratch);

		if (EntitiesScratch.Num() != 0)
		{
			FEntityImportSequenceParams Params;
			Params.InstanceHandle = InstanceHandle;
			Params.DefaultCompletionMode = Sequence->DefaultCompletionMode;
			Params.HierarchicalBias = 0;

			SequenceInstance.Ledger.UpdateOneShotEntities(Linker, Params, ComponentField, EntitiesScratch);
		}
	}
}

void FSequenceUpdater_Flat::Finish(UMovieSceneEntitySystemLinker* Linker, FInstanceHandle InstanceHandle, IMovieScenePlayer* InPlayer)
{
	InvalidateCachedData(Linker);
}


void FSequenceUpdater_Flat::Destroy(UMovieSceneEntitySystemLinker* Linker)
{
}

void FSequenceUpdater_Flat::InvalidateCachedData(UMovieSceneEntitySystemLinker* Linker)
{
	CachedEntityRange = TRange<FFrameNumber>::Empty();
	CachedDeterminismFences.Reset();
}





FSequenceUpdater_Hierarchical::FSequenceUpdater_Hierarchical(FMovieSceneCompiledDataID InCompiledDataID)
	: CompiledDataID(InCompiledDataID)
{
	CachedEntityRange = TRange<FFrameNumber>::Empty();
}

FSequenceUpdater_Hierarchical::~FSequenceUpdater_Hierarchical()
{
}

void FSequenceUpdater_Hierarchical::DissectContext(UMovieSceneEntitySystemLinker* Linker, IMovieScenePlayer* InPlayer, const FMovieSceneContext& Context, TArray<TRange<FFrameTime>>& OutDissections)
{
	UMovieSceneCompiledDataManager* CompiledDataManager = InPlayer->GetEvaluationTemplate().GetCompiledDataManager();

	TRange<FFrameNumber> TraversedRange = Context.GetFrameNumberRange();
	TArray<FFrameTime>   RootDissectionTimes;

	{
		TArrayView<const FFrameTime> RootDeterminismFences = CompiledDataManager->GetEntry(CompiledDataID).DeterminismFences;
		TArrayView<const FFrameTime> TraversedFences       = GetFencesWithinRange(RootDeterminismFences, Context.GetFrameNumberRange());

		UE::MovieScene::DissectRange(TraversedFences, Context.GetRange(), OutDissections);
	}

	// @todo: should this all just be compiled into the root hierarchy?
	if (const FMovieSceneSequenceHierarchy* Hierarchy = CompiledDataManager->FindHierarchy(CompiledDataID))
	{
		FMovieSceneEvaluationTreeRangeIterator SubSequenceIt = Hierarchy->GetTree().IterateFromLowerBound(TraversedRange.GetLowerBound());
		for ( ; SubSequenceIt && SubSequenceIt.Range().Overlaps(TraversedRange); ++SubSequenceIt)
		{
			TRange<FFrameTime> RootClampRange = TRange<FFrameTime>::Intersection(ConvertRange<FFrameNumber, FFrameTime>(SubSequenceIt.Range()), Context.GetRange());

			// When Context.GetRange() does not fall on whole frame boundaries, we can sometimes end up with a range that clamps to being empty, even though the range overlapped
			// the traversed range. ie if we evaluated range (1.5, 10], our traversed range would be [2, 11). If we have a sub sequence range of (10, 20), it would still be iterated here
			// because [2, 11) overlaps (10, 20), but when clamped to the evaluated range, the range is (10, 10], which is empty.
			if (!RootClampRange.IsEmpty())
			{
				continue;
			}

			for (FMovieSceneSubSequenceTreeEntry Entry : Hierarchy->GetTree().GetAllData(SubSequenceIt.Node()))
			{
				const FMovieSceneSubSequenceData* SubData = Hierarchy->FindSubData(Entry.SequenceID);
				checkf(SubData, TEXT("Sub data does not exist for a SequenceID that exists in the hierarchical tree - this indicates a corrupt compilation product."));

				UMovieSceneSequence*      SubSequence = SubData     ? SubData->GetSequence()                      : nullptr;
				FMovieSceneCompiledDataID SubDataID   = SubSequence ? CompiledDataManager->GetDataID(SubSequence) : FMovieSceneCompiledDataID();
				if (!SubDataID.IsValid())
				{
					continue;
				}

				TArrayView<const FFrameTime> SubDeterminismFences = CompiledDataManager->GetEntry(SubDataID).DeterminismFences;
				if (SubDeterminismFences.Num() > 0)
				{
					TRange<FFrameTime>   InnerRange           = SubData->RootToSequenceTransform.TransformRangeUnwarped(RootClampRange);
					TRange<FFrameNumber> InnerTraversedFrames = FMovieSceneEvaluationRange::TimeRangeToNumberRange(InnerRange);

					TArrayView<const FFrameTime> TraversedFences  = GetFencesWithinRange(SubDeterminismFences, InnerTraversedFrames);
					if (TraversedFences.Num() > 0)
					{
						FMovieSceneWarpCounter WarpCounter;
						FFrameTime Unused;
						SubData->RootToSequenceTransform.TransformTime(RootClampRange.GetLowerBoundValue(), Unused, WarpCounter);

						FMovieSceneTimeTransform InverseTransform = SubData->RootToSequenceTransform.InverseFromWarp(WarpCounter);
						Algo::Transform(TraversedFences, RootDissectionTimes, [InverseTransform](FFrameTime In){ return In * InverseTransform; });
					}
				}
			}
		}
	}

	if (RootDissectionTimes.Num() > 0)
	{
		Algo::Sort(RootDissectionTimes);
		UE::MovieScene::DissectRange(RootDissectionTimes, Context.GetRange(), OutDissections);
	}
}

FInstanceHandle FSequenceUpdater_Hierarchical::GetOrCreateSequenceInstance(IMovieScenePlayer* InPlayer, FInstanceRegistry* InstanceRegistry, FInstanceHandle RootInstanceHandle, FMovieSceneSequenceID SequenceID)
{
	FInstanceHandle InstanceHandle = SequenceInstances.FindRef(SequenceID);

	if (!InstanceHandle.IsValid())
	{
		InstanceHandle = InstanceRegistry->AllocateSubInstance(InPlayer, SequenceID, RootInstanceHandle);
		SequenceInstances.Add(SequenceID, InstanceHandle);
	}

	return InstanceHandle;
}

void FSequenceUpdater_Hierarchical::Start(UMovieSceneEntitySystemLinker* Linker, FInstanceHandle InstanceHandle, IMovieScenePlayer* InPlayer, const FMovieSceneContext& InContext)
{
}

void FSequenceUpdater_Hierarchical::Update(UMovieSceneEntitySystemLinker* Linker, FInstanceHandle InstanceHandle, IMovieScenePlayer* InPlayer, const FMovieSceneContext& Context)
{
	const FFrameNumber RootTime = Context.GetTime().FrameNumber;

	const bool bGatherEntities = !CachedEntityRange.Contains(RootTime);

	FInstanceRegistry* InstanceRegistry = Linker->GetInstanceRegistry();
	UMovieSceneCompiledDataManager* CompiledDataManager = InPlayer->GetEvaluationTemplate().GetCompiledDataManager();

	FMovieSceneEvaluationFieldEntitySet EntitiesScratch;

	// ------------------------------------------------------------------------------------------------
	// Handle the root sequence entities first
	{
		// Set the context for the root sequence instance
		FSequenceInstance& RootInstance = InstanceRegistry->MutateInstance(InstanceHandle);
		RootInstance.SetContext(Context);

		const FMovieSceneEntityComponentField* RootComponentField = CompiledDataManager->FindEntityComponentField(CompiledDataID);
		UMovieSceneSequence* RootSequence = InPlayer->GetEvaluationTemplate().GetSequence(MovieSceneSequenceID::Root);

		if (RootSequence == nullptr)
		{
			RootInstance.Ledger.UnlinkEverything(Linker);
		}
		else
		{
			// Update entities if necessary
			if (bGatherEntities)
			{
				CachedEntityRange = UpdateEntitiesForSequence(RootComponentField, RootTime, EntitiesScratch);

				FEntityImportSequenceParams Params;
				Params.InstanceHandle = InstanceHandle;
				Params.DefaultCompletionMode = RootSequence->DefaultCompletionMode;
				Params.HierarchicalBias = 0;

				RootInstance.Ledger.UpdateEntities(Linker, Params, RootComponentField, EntitiesScratch);
			}

			// Update any one-shot entities for the current root frame
			if (RootComponentField && RootComponentField->HasAnyOneShotEntities())
			{
				EntitiesScratch.Reset();
				RootComponentField->QueryOneShotEntities(Context.GetFrameNumberRange(), EntitiesScratch);

				if (EntitiesScratch.Num() != 0)
				{
					FEntityImportSequenceParams Params;
					Params.InstanceHandle = InstanceHandle;
					Params.DefaultCompletionMode = RootSequence->DefaultCompletionMode;
					Params.HierarchicalBias = 0;

					RootInstance.Ledger.UpdateOneShotEntities(Linker, Params, RootComponentField, EntitiesScratch);
				}
			}
		}
	}

	TArray<FMovieSceneSequenceID, TInlineAllocator<16>> ActiveSequences;

	// ------------------------------------------------------------------------------------------------
	// Handle sub sequence entities next
	const FMovieSceneSequenceHierarchy* Hierarchy = CompiledDataManager->FindHierarchy(CompiledDataID);
	if (Hierarchy)
	{
		FMovieSceneEvaluationTreeRangeIterator SubSequenceIt = Hierarchy->GetTree().IterateFromTime(RootTime);
		
		if (bGatherEntities)
		{
			CachedEntityRange = TRange<FFrameNumber>::Intersection(CachedEntityRange, SubSequenceIt.Range());
		}

		for (FMovieSceneSubSequenceTreeEntry Entry : Hierarchy->GetTree().GetAllData(SubSequenceIt.Node()))
		{
			ActiveSequences.Add(Entry.SequenceID);

			const FMovieSceneSubSequenceData* SubData = Hierarchy->FindSubData(Entry.SequenceID);
			checkf(SubData, TEXT("Sub data does not exist for a SequenceID that exists in the hierarchical tree - this indicates a corrupt compilation product."));

			UMovieSceneSequence* SubSequence = SubData->GetSequence();
			if (SubSequence == nullptr)
			{
				FInstanceHandle SubSequenceHandle = SequenceInstances.FindRef(Entry.SequenceID);
				if (SubSequenceHandle.IsValid())
				{
					FSequenceInstance& SubSequenceInstance = InstanceRegistry->MutateInstance(SubSequenceHandle);
					SubSequenceInstance.Ledger.UnlinkEverything(Linker);
				}
			}
			else
			{
				FMovieSceneCompiledDataID SubDataID = CompiledDataManager->GetDataID(SubSequence);

				// Set the context for the root sequence instance
				FInstanceHandle    SubSequenceHandle = GetOrCreateSequenceInstance(InPlayer, InstanceRegistry, InstanceHandle, Entry.SequenceID);
				FSequenceInstance& SubSequenceInstance = InstanceRegistry->MutateInstance(SubSequenceHandle);

				// Update the sub sequence's context
				FMovieSceneContext SubContext = Context.Transform(SubData->RootToSequenceTransform, SubData->TickResolution);
				SubContext.ReportOuterSectionRanges(SubData->PreRollRange.Value, SubData->PostRollRange.Value);
				SubContext.SetHierarchicalBias(SubData->HierarchicalBias);


				const bool bWasPreRoll  = SubSequenceInstance.GetContext().IsPreRoll();
				const bool bWasPostRoll = SubSequenceInstance.GetContext().IsPostRoll();
				const bool bIsPreRoll   = SubContext.IsPreRoll();
				const bool bIsPostRoll  = SubContext.IsPostRoll();

				if (bWasPreRoll != bIsPreRoll || bWasPostRoll != bIsPostRoll)
				{
					SubSequenceInstance.Ledger.UnlinkEverything(Linker);
				}

				SubSequenceInstance.SetContext(SubContext);
				SubSequenceInstance.SetFinished(false);

				const FMovieSceneEntityComponentField* SubComponentField = CompiledDataManager->FindEntityComponentField(SubDataID);

				// Update entities if necessary
				const FFrameTime SubSequenceTime = SubContext.GetTime();
				const FMovieSceneTimeTransform SequenceToRootTransform = SubContext.GetSequenceToRootTransform();

				FEntityImportSequenceParams Params;
				Params.InstanceHandle = SubSequenceHandle;
				Params.DefaultCompletionMode = SubSequence->DefaultCompletionMode;
				Params.HierarchicalBias = SubData->HierarchicalBias;
				Params.bPreRoll  = bIsPreRoll;
				Params.bPostRoll = bIsPostRoll;
				Params.bHasHierarchicalEasing = SubData->bHasHierarchicalEasing;

				if (bGatherEntities)
				{
					EntitiesScratch.Reset();

					TRange<FFrameNumber> SubEntityRange = UpdateEntitiesForSequence(SubComponentField, SubSequenceTime, EntitiesScratch);

					SubSequenceInstance.Ledger.UpdateEntities(Linker, Params, SubComponentField, EntitiesScratch);

					SubEntityRange *= SequenceToRootTransform;
					CachedEntityRange = TRange<FFrameNumber>::Intersection(CachedEntityRange, SubEntityRange);
				}

				// Update any one-shot entities for the sub sequence
				if (SubComponentField && SubComponentField->HasAnyOneShotEntities())
				{
					EntitiesScratch.Reset();
					SubComponentField->QueryOneShotEntities(SubContext.GetFrameNumberRange(), EntitiesScratch);

					if (EntitiesScratch.Num() != 0)
					{
						SubSequenceInstance.Ledger.UpdateOneShotEntities(Linker, Params, SubComponentField, EntitiesScratch);
					}
				}
			}
		}
	}

	FMovieSceneEntitySystemRunner* Runner = Linker->GetActiveRunner();
	check(Runner);

	for (auto InstanceIt = SequenceInstances.CreateIterator(); InstanceIt; ++InstanceIt)
	{
		FInstanceHandle SubInstanceHandle = InstanceIt.Value();
		Runner->MarkForUpdate(SubInstanceHandle);

		if (!ActiveSequences.Contains(InstanceIt.Key()))
		{
			// Remove all entities from this instance since it is no longer active
			InstanceRegistry->MutateInstance(SubInstanceHandle).Finish(Linker);
		}
	}
}

void FSequenceUpdater_Hierarchical::Finish(UMovieSceneEntitySystemLinker* Linker, FInstanceHandle InstanceHandle, IMovieScenePlayer* InPlayer)
{
	FInstanceRegistry* InstanceRegistry = Linker->GetInstanceRegistry();

	// Finish all sub sequences as well
	for (TPair<FMovieSceneSequenceID, FInstanceHandle> Pair : SequenceInstances)
	{
		InstanceRegistry->MutateInstance(Pair.Value).Finish(Linker);
	}

	InvalidateCachedData(Linker);
}

void FSequenceUpdater_Hierarchical::Destroy(UMovieSceneEntitySystemLinker* Linker)
{
	FInstanceRegistry* InstanceRegistry = Linker->GetInstanceRegistry();

	for (TPair<FMovieSceneSequenceID, FInstanceHandle> Pair : SequenceInstances)
	{
		InstanceRegistry->DestroyInstance(Pair.Value);
	}
}

void FSequenceUpdater_Hierarchical::InvalidateCachedData(UMovieSceneEntitySystemLinker* Linker)
{
	CachedEntityRange = TRange<FFrameNumber>::Empty();

	FInstanceRegistry* InstanceRegistry = Linker->GetInstanceRegistry();

	for (TPair<FMovieSceneSequenceID, FInstanceHandle> Pair : SequenceInstances)
	{
		InstanceRegistry->MutateInstance(Pair.Value).Ledger.Invalidate();
	}
}



TRange<FFrameNumber> FSequenceUpdater_Hierarchical::UpdateEntitiesForSequence(const FMovieSceneEntityComponentField* ComponentField, FFrameTime SequenceTime, FMovieSceneEvaluationFieldEntitySet& OutEntities)
{
	TRange<FFrameNumber> CachedRange = TRange<FFrameNumber>::All();

	if (ComponentField)
	{
		// Extract all the entities for the current time
		ComponentField->QueryPersistentEntities(SequenceTime.FrameNumber, CachedRange, OutEntities);
	}

	return CachedRange;
}


} // namespace MovieScene
} // namespace UE
