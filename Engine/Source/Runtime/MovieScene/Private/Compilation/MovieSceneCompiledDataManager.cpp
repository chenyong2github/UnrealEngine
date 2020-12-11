// Copyright Epic Games, Inc. All Rights Reserved.

#include "Compilation/MovieSceneCompiledDataManager.h"
#include "Compilation/IMovieSceneTemplateGenerator.h"
#include "Compilation/IMovieSceneTrackTemplateProducer.h"
#include "EntitySystem/IMovieSceneEntityProvider.h"
#include "Evaluation/MovieSceneEvaluationCustomVersion.h"
#include "Evaluation/MovieSceneRootOverridePath.h"
#include "MovieSceneSequence.h"
#include "Sections/MovieSceneSubSection.h"
#include "Tracks/MovieSceneSubTrack.h"
#include "IMovieSceneModule.h"

#include "Containers/SortedMap.h"

#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"


FString GMovieSceneCompilerVersion = TEXT("7D4B98092FAC4A6B964ECF72D8279EF8");
FAutoConsoleVariableRef CVarMovieSceneCompilerVersion(
	TEXT("Sequencer.CompilerVersion"),
	GMovieSceneCompilerVersion,
	TEXT("Defines a global identifer for moviescene compiler logic.\n"),
	ECVF_Default
);


IMovieSceneModule& GetMovieSceneModule()
{
	static TWeakPtr<IMovieSceneModule> WeakMovieSceneModule;

	TSharedPtr<IMovieSceneModule> Shared = WeakMovieSceneModule.Pin();
	if (!Shared.IsValid())
	{
		WeakMovieSceneModule = IMovieSceneModule::Get().GetWeakPtr();
		Shared = WeakMovieSceneModule.Pin();
	}
	check(Shared.IsValid());

	return *Shared;
}


struct FMovieSceneCompileDataManagerGenerator : public IMovieSceneTemplateGenerator
{
	FMovieSceneCompileDataManagerGenerator(UMovieSceneCompiledDataManager* InCompiledDataManager)
	{
		CompiledDataManager = InCompiledDataManager;
		Entry               = nullptr;
		Template            = nullptr;
	}

	void Reset(FMovieSceneCompiledDataEntry* InEntry)
	{
		check(InEntry);

		Entry    = InEntry;
		Template = CompiledDataManager->TrackTemplates.Find(Entry->DataID.Value);
	}

	virtual void AddOwnedTrack(FMovieSceneEvaluationTrack&& InTrackTemplate, const UMovieSceneTrack& SourceTrack) override
	{
		check(Entry);

		if (!Template)
		{
			Template = &CompiledDataManager->TrackTemplates.FindOrAdd(Entry->DataID.Value);
		}

		Template->AddTrack(SourceTrack.GetSignature(), MoveTemp(InTrackTemplate));
	}

private:

	UMovieSceneCompiledDataManager* CompiledDataManager;
	FMovieSceneCompiledDataEntry*   Entry;
	FMovieSceneEvaluationTemplate*  Template;
};


struct FCompileOnTheFlyData
{
	/** Primary sort - group */
	uint16 GroupEvaluationPriority;
	/** Secondary sort - Hierarchical bias */
	int16 HierarchicalBias;
	/** Tertiary sort - Eval priority */
	int16 EvaluationPriority;
	/** Quaternary sort - Child priority */
	int16 ChildPriority;
	/**  */
	FName EvaluationGroup;
	/** Whether the track requires initialization or not */
	bool bRequiresInit;
	bool bPriorityTearDown;

	FMovieSceneEvaluationFieldTrackPtr Track;
	FMovieSceneFieldEntry_ChildTemplate Child;
};


/** Gathered data for a given time or range */
struct FMovieSceneGatheredCompilerData
{
	/** Tree of tracks to evaluate */
	TMovieSceneEvaluationTree<FCompileOnTheFlyData> TrackTemplates;
	/** Tree of active sequences */
	TMovieSceneEvaluationTree<FMovieSceneSequenceID> Sequences;
	FMovieSceneEntityComponentField* EntityField = nullptr;

	EMovieSceneSequenceFlags InheritedFlags = EMovieSceneSequenceFlags::None;
	EMovieSceneSequenceCompilerMask AccumulatedMask = EMovieSceneSequenceCompilerMask::None;
};

/** Parameter structure used for gathering entities for a given time or range */
struct FGatherParameters
{
	FGatherParameters()
		: SequenceID(MovieSceneSequenceID::Root)
		, RootClampRange(TRange<FFrameNumber>::All())
		, LocalClampRange(RootClampRange)
		, Flags(ESectionEvaluationFlags::None)
		, HierarchicalBias(0)
		, bHasHierarchicalEasing(false)
	{}

	FGatherParameters CreateForSubData(const FMovieSceneSubSequenceData& SubData, FMovieSceneSequenceID InSubSequenceID) const
	{
		FGatherParameters SubParams = *this;

		SubParams.RootToSequenceTransform   = SubData.RootToSequenceTransform;
		SubParams.HierarchicalBias          = SubData.HierarchicalBias;
		SubParams.SequenceID                = InSubSequenceID;
		SubParams.LocalClampRange           = SubData.RootToSequenceTransform.TransformRangeUnwarped(SubParams.RootClampRange);

		return SubParams;
	}

	void SetClampRange(TRange<FFrameNumber> InNewRootClampRange)
	{
		RootClampRange  = InNewRootClampRange;
		LocalClampRange = RootToSequenceTransform.TransformRangeUnwarped(InNewRootClampRange);
	}

	/** Clamp the specified range to the current clamp range (in root space) */
	TRange<FFrameNumber> ClampRoot(const TRange<FFrameNumber>& InRootRange) const
	{
		return TRange<FFrameNumber>::Intersection(RootClampRange, InRootRange);
	}

	/** The ID of the sequence being compiled */
	FMovieSceneSequenceID SequenceID;

	/** A range to clamp compilation to in the root's time-space */
	TRange<FFrameNumber> RootClampRange;
	/** A range to clamp compilation to in the current sequence's time-space */
	TRange<FFrameNumber> LocalClampRange;

	/** Evaluation flags for the current sequence */
	ESectionEvaluationFlags Flags;

	/** Transform from the root time-space to the current sequence's time-space */
	FMovieSceneSequenceTransform RootToSequenceTransform;

	/** Current accumulated hierarchical bias */
	int16 HierarchicalBias;

	/** Whether the current sequence is receiving hierarchical easing from some parent sequence */
	bool bHasHierarchicalEasing;

	EMovieSceneServerClientMask NetworkMask;
};

/** Parameter structure used for gathering entities for a given time or range */
struct FTrackGatherParameters : FGatherParameters
{
	FTrackGatherParameters(UMovieSceneCompiledDataManager* InCompiledDataManager)
		: TemplateGenerator(InCompiledDataManager)
	{}

	FTrackGatherParameters CreateForSubData(const FMovieSceneSubSequenceData& SubData, FMovieSceneSequenceID InSubSequenceID) const
	{
		FTrackGatherParameters SubParams = *this;
		static_cast<FGatherParameters&>(SubParams) = FGatherParameters::CreateForSubData(SubData, InSubSequenceID);

		return SubParams;
	}


	/** Store from which to retrieve templates */
	mutable FMovieSceneCompileDataManagerGenerator TemplateGenerator;
};


bool SortPredicate(const FCompileOnTheFlyData& A, const FCompileOnTheFlyData& B)
{
	if (A.GroupEvaluationPriority != B.GroupEvaluationPriority)
	{
		return A.GroupEvaluationPriority > B.GroupEvaluationPriority;
	}
	else if (A.HierarchicalBias != B.HierarchicalBias)
	{
		return A.HierarchicalBias < B.HierarchicalBias;
	}
	else if (A.EvaluationPriority != B.EvaluationPriority)
	{
		return A.EvaluationPriority > B.EvaluationPriority;
	}
	else
	{
		return A.ChildPriority > B.ChildPriority;
	}
}

void AddPtrsToGroup(
	FMovieSceneEvaluationGroup* OutGroup,
	TArray<FMovieSceneFieldEntry_EvaluationTrack>& InitTrackLUT,
	TArray<FMovieSceneFieldEntry_ChildTemplate>&   InitSectionLUT,
	TArray<FMovieSceneFieldEntry_EvaluationTrack>& EvalTrackLUT,
	TArray<FMovieSceneFieldEntry_ChildTemplate>&   EvalSectionLUT
	)
{
	if (!InitTrackLUT.Num() && !EvalTrackLUT.Num())
	{
		return;
	}

	FMovieSceneEvaluationGroupLUTIndex Index;
	Index.NumInitPtrs = InitTrackLUT.Num();
	Index.NumEvalPtrs = EvalTrackLUT.Num();

	OutGroup->LUTIndices.Add(Index);
	OutGroup->TrackLUT.Append(InitTrackLUT);
	OutGroup->TrackLUT.Append(EvalTrackLUT);

	OutGroup->SectionLUT.Append(InitSectionLUT);
	OutGroup->SectionLUT.Append(EvalSectionLUT);

	InitTrackLUT.Reset();
	InitSectionLUT.Reset();
	EvalTrackLUT.Reset();
	EvalSectionLUT.Reset();
}

FMovieSceneCompiledDataEntry::FMovieSceneCompiledDataEntry()
	: AccumulatedFlags(EMovieSceneSequenceFlags::None)
	, AccumulatedMask(EMovieSceneSequenceCompilerMask::None)
{}

UMovieSceneSequence* FMovieSceneCompiledDataEntry::GetSequence() const
{
	return CastChecked<UMovieSceneSequence>(SequenceKey.ResolveObjectPtr(), ECastCheckedType::NullAllowed);
}

UMovieSceneCompiledData::UMovieSceneCompiledData()
{
	AccumulatedMask = EMovieSceneSequenceCompilerMask::None;
	AllocatedMask = EMovieSceneSequenceCompilerMask::None;
	AccumulatedFlags = EMovieSceneSequenceFlags::None;
}

void UMovieSceneCompiledData::Reset()
{
	EvaluationTemplate = FMovieSceneEvaluationTemplate();
	Hierarchy = FMovieSceneSequenceHierarchy();
	EntityComponentField = FMovieSceneEntityComponentField();
	TrackTemplateField = FMovieSceneEvaluationField();
	DeterminismFences.Reset();
	CompiledSignature.Invalidate();
	CompilerVersion.Invalidate();
	AccumulatedMask = EMovieSceneSequenceCompilerMask::None;
	AllocatedMask = EMovieSceneSequenceCompilerMask::None;
	AccumulatedFlags = EMovieSceneSequenceFlags::None;
}

UMovieSceneCompiledDataManager::UMovieSceneCompiledDataManager()
{
	const bool bParsed = FGuid::Parse(GMovieSceneCompilerVersion, CompilerVersion);
	ensureMsgf(bParsed, TEXT("Invalid compiler version specific - this will break any persistent compiled data"));

	IConsoleManager::Get().RegisterConsoleVariableSink_Handle(FConsoleCommandDelegate::CreateUObject(this, &UMovieSceneCompiledDataManager::ConsoleVariableSink));

	ReallocationVersion = 0;
	NetworkMask = EMovieSceneServerClientMask::All;
}

void UMovieSceneCompiledDataManager::DestroyAllData()
{
	// Eradicate all compiled data
	for (int32 Index = 0; Index < CompiledDataEntries.GetMaxIndex(); ++Index)
	{
		if (CompiledDataEntries.IsAllocated(Index))
		{
			FMovieSceneCompiledDataEntry& Entry = CompiledDataEntries[Index];
			Entry.CompiledSignature       = FGuid();
			Entry.AccumulatedFlags        = EMovieSceneSequenceFlags::None;
			Entry.AccumulatedMask         = EMovieSceneSequenceCompilerMask::None;
		}
	}

	Hierarchies.Empty();
	TrackTemplates.Empty();
	TrackTemplateFields.Empty();
	EntityComponentFields.Empty();
}

void UMovieSceneCompiledDataManager::ConsoleVariableSink()
{
	FGuid NewCompilerVersion;
	const bool bParsed = FGuid::Parse(GMovieSceneCompilerVersion, NewCompilerVersion);
	ensureMsgf(bParsed, TEXT("Invalid compiler version specific - this will break any persistent compiled data"));

	if (CompilerVersion != NewCompilerVersion)
	{
		DestroyAllData();
	}
}

void UMovieSceneCompiledDataManager::CopyCompiledData(UMovieSceneSequence* Sequence)
{
	UMovieSceneCompiledData* CompiledData = Sequence->GetOrCreateCompiledData();
	CompiledData->Reset();

	FMovieSceneCompiledDataID DataID = GetDataID(Sequence);
	Compile(DataID, Sequence);

	if (const FMovieSceneSequenceHierarchy* Hierarchy = FindHierarchy(DataID))
	{
		CompiledData->Hierarchy = *Hierarchy;
		CompiledData->AllocatedMask.bHierarchy = true;
	}
	if (const FMovieSceneEvaluationTemplate* TrackTemplate = FindTrackTemplate(DataID))
	{
		CompiledData->EvaluationTemplate = *TrackTemplate;
		CompiledData->AllocatedMask.bEvaluationTemplate = true;
	}
	if (const FMovieSceneEvaluationField* TrackTemplateField = FindTrackTemplateField(DataID))
	{
		if (Sequence->IsPlayableDirectly())
		{
			CompiledData->TrackTemplateField = *TrackTemplateField;
			CompiledData->AllocatedMask.bEvaluationTemplateField = true;
		}
	}
	if (const FMovieSceneEntityComponentField* EntityComponentField = FindEntityComponentField(DataID))
	{
		CompiledData->EntityComponentField = *EntityComponentField;
		CompiledData->AllocatedMask.bEntityComponentField = true;
	}

	const FMovieSceneCompiledDataEntry& DataEntry = CompiledDataEntries[DataID.Value];
	CompiledData->DeterminismFences = DataEntry.DeterminismFences;
	CompiledData->CompiledSignature = Sequence->GetSignature();
	CompiledData->CompilerVersion = CompilerVersion;
	CompiledData->AccumulatedMask = DataEntry.AccumulatedMask;
	CompiledData->AccumulatedFlags = DataEntry.AccumulatedFlags;
}

void UMovieSceneCompiledDataManager::LoadCompiledData(UMovieSceneSequence* Sequence)
{
	// This can be called during Async Loads
	FScopeLock AsyncLoadLock(&AsyncLoadCriticalSection);

	UMovieSceneCompiledData* CompiledData = Sequence->GetCompiledData();
	if (CompiledData)
	{
		FMovieSceneCompiledDataID DataID = GetDataID(Sequence);

		if (CompiledData->CompilerVersion != CompilerVersion)
		{
			CompiledDataEntries[DataID.Value].AccumulatedFlags |= EMovieSceneSequenceFlags::Volatile;
			return;
		}

		if (CompiledData->AllocatedMask.bHierarchy)
		{
			Hierarchies.Add(DataID.Value, MoveTemp(CompiledData->Hierarchy));
		}
		if (CompiledData->AllocatedMask.bEvaluationTemplate)
		{
			TrackTemplates.Add(DataID.Value, MoveTemp(CompiledData->EvaluationTemplate));
		}
		if (CompiledData->AllocatedMask.bEvaluationTemplateField)
		{
			TrackTemplateFields.Add(DataID.Value, MoveTemp(CompiledData->TrackTemplateField));
		}
		if (CompiledData->AllocatedMask.bEntityComponentField)
		{
			EntityComponentFields.Add(DataID.Value, MoveTemp(CompiledData->EntityComponentField));
		}

		FMovieSceneCompiledDataEntry* EntryPtr = GetEntryPtr(DataID);

		EntryPtr->DeterminismFences = MoveTemp(CompiledData->DeterminismFences);
		EntryPtr->CompiledSignature = CompiledData->CompiledSignature;
		EntryPtr->AccumulatedMask = CompiledData->AccumulatedMask.AsEnum();
		EntryPtr->AccumulatedFlags = CompiledData->AccumulatedFlags;

		++ReallocationVersion;
	}
	else
	{
		Reset(Sequence);
	}
}

void UMovieSceneCompiledDataManager::SetEmulatedNetworkMask(EMovieSceneServerClientMask NewMask)
{
	DestroyAllData();
	NetworkMask = NewMask;
}

void UMovieSceneCompiledDataManager::Reset(UMovieSceneSequence* Sequence)
{
	FMovieSceneCompiledDataID DataID = SequenceToDataIDs.FindRef(Sequence);
	if (DataID.IsValid())
	{
		DestroyData(DataID);
		SequenceToDataIDs.Remove(Sequence);
	}
}

FMovieSceneCompiledDataID UMovieSceneCompiledDataManager::FindDataID(UMovieSceneSequence* Sequence) const
{
	return SequenceToDataIDs.FindRef(Sequence);
}

FMovieSceneCompiledDataID UMovieSceneCompiledDataManager::GetDataID(UMovieSceneSequence* Sequence)
{
	check(Sequence);

	FMovieSceneCompiledDataID ExistingDataID = FindDataID(Sequence);
	if (ExistingDataID.IsValid())
	{
		return ExistingDataID;
	}

	const int32 Index = CompiledDataEntries.Add(FMovieSceneCompiledDataEntry());

	ExistingDataID = FMovieSceneCompiledDataID { Index };
	FMovieSceneCompiledDataEntry& NewEntry = CompiledDataEntries[Index];

	NewEntry.SequenceKey = Sequence;
	NewEntry.DataID = ExistingDataID;
	NewEntry.AccumulatedFlags = Sequence->GetFlags();

	SequenceToDataIDs.Add(Sequence, ExistingDataID);
	return ExistingDataID;
}

FMovieSceneCompiledDataID UMovieSceneCompiledDataManager::GetSubDataID(FMovieSceneCompiledDataID DataID, FMovieSceneSequenceID SubSequenceID)
{
	if (SubSequenceID == MovieSceneSequenceID::Root)
	{
		return DataID;
	}

	const FMovieSceneSequenceHierarchy* Hierarchy = FindHierarchy(DataID);
	if (Hierarchy)
	{
		const FMovieSceneSubSequenceData* SubData     = Hierarchy->FindSubData(SubSequenceID);
		UMovieSceneSequence*              SubSequence = SubData ? SubData->GetSequence() : nullptr;

		if (SubSequence)
		{
			return GetDataID(SubSequence);
		}
	}

	return FMovieSceneCompiledDataID();
}


#if WITH_EDITOR

UMovieSceneCompiledDataManager* UMovieSceneCompiledDataManager::GetPrecompiledData(EMovieSceneServerClientMask EmulatedMask)
{
	ensureMsgf(!GExitPurge, TEXT("Attempting to access precompiled data manager during shutdown - this is undefined behavior since the manager may have already been destroyed, or could be unconstrictible"));

	if (EmulatedMask == EMovieSceneServerClientMask::Client)
	{
		static UMovieSceneCompiledDataManager* GEmulatedClientDataManager = NewObject<UMovieSceneCompiledDataManager>(GetTransientPackage(), "EmulatedClientDataManager", RF_MarkAsRootSet);
		GEmulatedClientDataManager->NetworkMask = EMovieSceneServerClientMask::Client;
		return GEmulatedClientDataManager;
	}

	if (EmulatedMask == EMovieSceneServerClientMask::Server)
	{
		static UMovieSceneCompiledDataManager* GEmulatedServerDataManager = NewObject<UMovieSceneCompiledDataManager>(GetTransientPackage(), "EmulatedServerDataManager", RF_MarkAsRootSet);
		GEmulatedServerDataManager->NetworkMask = EMovieSceneServerClientMask::Server;
		return GEmulatedServerDataManager;
	}

	static UMovieSceneCompiledDataManager* GPrecompiledDataManager = NewObject<UMovieSceneCompiledDataManager>(GetTransientPackage(), "PrecompiledDataManager", RF_MarkAsRootSet);
	return GPrecompiledDataManager;
}

#else // WITH_EDITOR

UMovieSceneCompiledDataManager* UMovieSceneCompiledDataManager::GetPrecompiledData()
{
	ensureMsgf(!GExitPurge, TEXT("Attempting to access precompiled data manager during shutdown - this is undefined behavior since the manager may have already been destroyed, or could be unconstrictible"));

	static UMovieSceneCompiledDataManager* GPrecompiledDataManager = NewObject<UMovieSceneCompiledDataManager>(GetTransientPackage(), "PrecompiledDataManager", RF_MarkAsRootSet);
	return GPrecompiledDataManager;
}

#endif // WITH_EDITOR

void UMovieSceneCompiledDataManager::DestroyData(FMovieSceneCompiledDataID DataID)
{
	check(DataID.IsValid() && CompiledDataEntries.IsValidIndex(DataID.Value));

	Hierarchies.Remove(DataID.Value);
	TrackTemplates.Remove(DataID.Value);
	TrackTemplateFields.Remove(DataID.Value);
	EntityComponentFields.Remove(DataID.Value);

	CompiledDataEntries.RemoveAt(DataID.Value);
}

void UMovieSceneCompiledDataManager::DestroyTemplate(FMovieSceneCompiledDataID DataID)
{
	check(DataID.IsValid() && CompiledDataEntries.IsValidIndex(DataID.Value));

	// Remove the lookup entry for this sequence/network mask combination
	const FMovieSceneCompiledDataEntry& Entry = CompiledDataEntries[DataID.Value];
	SequenceToDataIDs.Remove(Entry.SequenceKey);

	DestroyData(DataID);
}

bool UMovieSceneCompiledDataManager::IsDirty(const FMovieSceneCompiledDataEntry& Entry) const
{
	if (Entry.CompiledSignature != Entry.GetSequence()->GetSignature())
	{
		return true;
	}

	if (const FMovieSceneSequenceHierarchy* Hierarchy = FindHierarchy(Entry.DataID))
	{
		for (const TTuple<FMovieSceneSequenceID, FMovieSceneSubSequenceData>& Pair : Hierarchy->AllSubSequenceData())
		{
			if (UMovieSceneSequence* SubSequence = Pair.Value.GetSequence())
			{
				FMovieSceneCompiledDataID SubDataID = FindDataID(SubSequence);
				if (!SubDataID.IsValid() || CompiledDataEntries[SubDataID.Value].CompiledSignature != SubSequence->GetSignature())
				{
					return true;
				}
			}
			else
			{
				return true;
			}
		}
	}

	return false;
}

bool UMovieSceneCompiledDataManager::IsDirty(FMovieSceneCompiledDataID CompiledDataID) const
{
	check(CompiledDataID.IsValid() && CompiledDataEntries.IsValidIndex(CompiledDataID.Value));
	return IsDirty(CompiledDataEntries[CompiledDataID.Value]);
}

bool UMovieSceneCompiledDataManager::IsDirty(UMovieSceneSequence* Sequence) const
{
	FMovieSceneCompiledDataID ExistingDataID = FindDataID(Sequence);
	if (ExistingDataID.IsValid())
	{
		check(CompiledDataEntries.IsValidIndex(ExistingDataID.Value));
		FMovieSceneCompiledDataEntry Entry = CompiledDataEntries[ExistingDataID.Value];
		return IsDirty(Entry);
	}

	return true;
}


void UMovieSceneCompiledDataManager::Compile(FMovieSceneCompiledDataID DataID)
{
	check(DataID.IsValid() && CompiledDataEntries.IsValidIndex(DataID.Value));
	UMovieSceneSequence* Sequence = CompiledDataEntries[DataID.Value].GetSequence();
	check(Sequence);
	Compile(DataID, Sequence);
}

FMovieSceneCompiledDataID UMovieSceneCompiledDataManager::Compile(UMovieSceneSequence* Sequence)
{
	FMovieSceneCompiledDataID DataID = GetDataID(Sequence);
	Compile(DataID, Sequence);
	return DataID;
}

void UMovieSceneCompiledDataManager::Compile(FMovieSceneCompiledDataID DataID, UMovieSceneSequence* Sequence)
{
	check(DataID.IsValid() && CompiledDataEntries.IsValidIndex(DataID.Value));
	FMovieSceneCompiledDataEntry Entry = CompiledDataEntries[DataID.Value];
	if (!IsDirty(Entry))
	{
		return;
	}

	FMovieSceneGatheredCompilerData GatheredData;
	FTrackGatherParameters Params(this);

	Entry.AccumulatedFlags = Sequence->GetFlags();
	Params.TemplateGenerator.Reset(&Entry);
	Params.NetworkMask = NetworkMask;

	// ---------------------------------------------------------------------------------------------------
	// Step 1 - Always ensure the hierarchy information is completely up to date first
	FMovieSceneSequenceHierarchy NewHierarchy;
	const bool bHasHierarchy = CompileHierarchy(Sequence, Params, &NewHierarchy);


	TSet<FGuid> GatheredSignatures;

	{
		UMovieScene* MovieScene = Sequence->GetMovieScene();

		if (UMovieSceneTrack* Track = MovieScene->GetCameraCutTrack())
		{
			CompileTrack(&Entry, nullptr, Track, Params, &GatheredSignatures, &GatheredData);
		}

		for (UMovieSceneTrack* Track : MovieScene->GetMasterTracks())
		{
			CompileTrack(&Entry, nullptr, Track, Params, &GatheredSignatures, &GatheredData);
		}

		for (const FMovieSceneBinding& ObjectBinding : MovieScene->GetBindings())
		{
			for (UMovieSceneTrack* Track : ObjectBinding.GetTracks())
			{
				CompileTrack(&Entry, &ObjectBinding, Track, Params, &GatheredSignatures, &GatheredData);
			}
		}
	}

	// ---------------------------------------------------------------------------------------------------
	// Step 2 - Gather compilation data
	FMovieSceneEntityComponentField ThisSequenceEntityField;

	{
		GatheredData.EntityField = &ThisSequenceEntityField;
		Gather(Entry, Sequence, Params, &GatheredData);
		GatheredData.EntityField = nullptr;
	}

	// ---------------------------------------------------------------------------------------------------
	// Step 3 - Assign entity field from data gathered for _this sequence only_
	if (ThisSequenceEntityField.IsEmpty())
	{
		EntityComponentFields.Remove(DataID.Value);
	}
	else
	{
		// EntityComponent data is not flattened so we assign that now after the initial gather
		EntityComponentFields.FindOrAdd(DataID.Value) = MoveTemp(ThisSequenceEntityField);
		GatheredData.AccumulatedMask |= EMovieSceneSequenceCompilerMask::EntityComponentField;
	}

	// ---------------------------------------------------------------------------------------------------
	// Step 4 - If we have a hierarchy, perform a gather for sub sequences
	if (bHasHierarchy)
	{
		CompileSubSequences(NewHierarchy, Params, &GatheredData);
		Entry.AccumulatedFlags |= GatheredData.InheritedFlags;
		Entry.AccumulatedMask |= GatheredData.AccumulatedMask;
	}

	// ---------------------------------------------------------------------------------------------------
	// Step 5 - Consolidate track template data from gathered data
	if (FMovieSceneEvaluationTemplate* TrackTemplate = TrackTemplates.Find(Entry.DataID.Value))
	{
		TrackTemplate->RemoveStaleData(GatheredSignatures);
	}

	CompileTrackTemplateField(&Entry, NewHierarchy, &GatheredData);

	// ---------------------------------------------------------------------------------------------------
	// Step 6 - Reassign or remove the new hierarchy
	if (bHasHierarchy)
	{
		Hierarchies.FindOrAdd(DataID.Value) = MoveTemp(NewHierarchy);
	}
	else
	{
		Hierarchies.Remove(DataID.Value);
	}

	// ---------------------------------------------------------------------------------------------------
	// Step 7: Apply the final state to the entry
	Entry.CompiledSignature = Sequence->GetSignature();
	Entry.AccumulatedMask = GatheredData.AccumulatedMask;
	CompiledDataEntries[DataID.Value] = Entry;
	++ReallocationVersion;
}


void UMovieSceneCompiledDataManager::Gather(const FMovieSceneCompiledDataEntry& Entry, UMovieSceneSequence* Sequence, const FTrackGatherParameters& Params, FMovieSceneGatheredCompilerData* OutCompilerData) const
{
	const FMovieSceneEvaluationTemplate* TrackTemplate = FindTrackTemplate(Entry.DataID);

	UMovieScene* MovieScene = Sequence->GetMovieScene();

	if (UMovieSceneTrack* Track = MovieScene->GetCameraCutTrack())
	{
		GatherTrack(nullptr, Track, Params, TrackTemplate, OutCompilerData);
	}

	for (UMovieSceneTrack* Track : MovieScene->GetMasterTracks())
	{
		GatherTrack(nullptr, Track, Params, TrackTemplate, OutCompilerData);
	}

	for (const FMovieSceneBinding& ObjectBinding : MovieScene->GetBindings())
	{
		for (UMovieSceneTrack* Track : ObjectBinding.GetTracks())
		{
			GatherTrack(&ObjectBinding, Track, Params, TrackTemplate, OutCompilerData);
		}
	}
}

void UMovieSceneCompiledDataManager::CompileSubSequences(const FMovieSceneSequenceHierarchy& Hierarchy, const FTrackGatherParameters& Params, FMovieSceneGatheredCompilerData* OutCompilerData)
{
	OutCompilerData->AccumulatedMask |= EMovieSceneSequenceCompilerMask::Hierarchy;

	// Ensure all sub sequences are compiled
	for (const TTuple<FMovieSceneSequenceID, FMovieSceneSubSequenceData>& Pair : Hierarchy.AllSubSequenceData())
	{
		if (UMovieSceneSequence* SubSequence = Pair.Value.GetSequence())
		{
			Compile(SubSequence);
		}
	}

	const TMovieSceneEvaluationTree<FMovieSceneSubSequenceTreeEntry>& SubSequenceTree = Hierarchy.GetTree();

	// Start iterating the field from the lower bound of the compile range
	FMovieSceneEvaluationTreeRangeIterator SubSequenceIt = SubSequenceTree.IterateFromLowerBound(Params.RootClampRange.GetLowerBound());
	for ( ; SubSequenceIt && SubSequenceIt.Range().Overlaps(Params.RootClampRange); ++SubSequenceIt)
	{
		// Iterate all sub sequences in the current range
		for (FMovieSceneSubSequenceTreeEntry SubSequenceEntry : SubSequenceTree.GetAllData(SubSequenceIt.Node()))
		{
			const FMovieSceneSubSequenceData* SubData = Hierarchy.FindSubData(SubSequenceEntry.SequenceID);
			checkf(SubData, TEXT("Sub data could not be found for a sequence that exists in the sub sequence tree - this indicates an error while populating the sub sequence hierarchy tree."));

			UMovieSceneSequence* SubSequence = SubData->GetSequence();
			if (SubSequence)
			{
				FTrackGatherParameters SubSectionGatherParams = Params.CreateForSubData(*SubData, SubSequenceEntry.SequenceID);
				SubSectionGatherParams.Flags |= SubSequenceEntry.Flags;
				SubSectionGatherParams.SetClampRange(SubSequenceIt.Range());

				// Access the sub entry data after compilation
				FMovieSceneCompiledDataID SubDataID = GetDataID(SubSequence);
				check(SubDataID.IsValid());

				// Gather track template data for the sub sequence
				FMovieSceneCompiledDataEntry SubEntry = CompiledDataEntries[SubDataID.Value];
				if (TrackTemplates.Contains(SubDataID.Value))
				{
					Gather(SubEntry, SubSequence, SubSectionGatherParams, OutCompilerData);
				}

				// Inherit flags from sub sequences (if a sub sequence is volatile, so must this be)
				OutCompilerData->InheritedFlags |= (CompiledDataEntries[SubDataID.Value].AccumulatedFlags & EMovieSceneSequenceFlags::InheritedFlags);
				OutCompilerData->AccumulatedMask |= SubEntry.AccumulatedMask;
			}
		}
	}
}


void UMovieSceneCompiledDataManager::CompileTrackTemplateField(FMovieSceneCompiledDataEntry* OutEntry, const FMovieSceneSequenceHierarchy& Hierarchy, FMovieSceneGatheredCompilerData* InCompilerData)
{
	if (!EnumHasAnyFlags(InCompilerData->AccumulatedMask, EMovieSceneSequenceCompilerMask::EvaluationTemplate))
	{
		TrackTemplateFields.Remove(OutEntry->DataID.Value);
		return;
	}


	FMovieSceneEvaluationField* TrackTemplateField = &TrackTemplateFields.FindOrAdd(OutEntry->DataID.Value);

	// Wipe the current evaluation field for the template
	*TrackTemplateField = FMovieSceneEvaluationField();

	InCompilerData->AccumulatedMask |= EMovieSceneSequenceCompilerMask::EvaluationTemplateField;

	TArray<FCompileOnTheFlyData> CompileData;
	for (FMovieSceneEvaluationTreeRangeIterator It(InCompilerData->TrackTemplates); It; ++It)
	{
		CompileData.Reset();

		TRange<FFrameNumber> FieldRange = It.Range();
		for (const FCompileOnTheFlyData& TrackData : InCompilerData->TrackTemplates.GetAllData(It.Node()))
		{
			CompileData.Add(TrackData);
		}

		// Sort the compilation data based on (in order):
		//  1. Group
		//  2. Hierarchical bias
		//  3. Evaluation priority
		CompileData.Sort(SortPredicate);

		// Generate the evaluation group by gathering initialization and evaluation ptrs for each unique group
		FMovieSceneEvaluationGroup EvaluationGroup;
		PopulateEvaluationGroup(CompileData, &EvaluationGroup);

		// Compute meta data for this segment
		TMovieSceneEvaluationTreeDataIterator<FMovieSceneSubSequenceTreeEntry> SubSequences = Hierarchy.GetTree().GetAllData(Hierarchy.GetTree().IterateFromLowerBound(FieldRange.GetLowerBound()).Node());

		FMovieSceneEvaluationMetaData MetaData;
		PopulateMetaData(Hierarchy, CompileData, SubSequences, &MetaData);

		TrackTemplateField->Add(FieldRange, MoveTemp(EvaluationGroup), MoveTemp(MetaData));
	}
}


void UMovieSceneCompiledDataManager::PopulateEvaluationGroup(const TArray<FCompileOnTheFlyData>& SortedCompileData, FMovieSceneEvaluationGroup* OutGroup)
{
	check(OutGroup);
	if (SortedCompileData.Num() == 0)
	{
		return;
	}

	static TArray<FMovieSceneFieldEntry_EvaluationTrack> InitTrackLUT;
	static TArray<FMovieSceneFieldEntry_ChildTemplate>   InitSectionLUT;

	static TArray<FMovieSceneFieldEntry_EvaluationTrack> EvalTrackLUT;
	static TArray<FMovieSceneFieldEntry_ChildTemplate>   EvalSectionLUT;

	InitTrackLUT.Reset();
	InitSectionLUT.Reset();
	EvalTrackLUT.Reset();
	EvalSectionLUT.Reset();

	// Now iterate the tracks and insert indices for initialization and evaluation
	FName LastEvaluationGroup = SortedCompileData[0].EvaluationGroup;

	int32 Index = 0;
	while (Index < SortedCompileData.Num())
	{
		const FCompileOnTheFlyData& Data = SortedCompileData[Index];

		// Check for different evaluation groups
		if (Data.EvaluationGroup != LastEvaluationGroup)
		{
			// If we're now in a different flush group, add the ptrs to the group
			AddPtrsToGroup(OutGroup, InitTrackLUT, InitSectionLUT, EvalTrackLUT, EvalSectionLUT);
		}
		LastEvaluationGroup = Data.EvaluationGroup;

		// Add all subsequent entries that relate to the same track
		FMovieSceneEvaluationFieldTrackPtr MatchTrack = Data.Track;

		uint16 NumChildren = 0;
		for ( ; Index < SortedCompileData.Num() && SortedCompileData[Index].Track == MatchTrack; ++Index)
		{
			if (SortedCompileData[Index].Child.ChildIndex != uint16(-1))
			{
				++NumChildren;
				// If this track requires initialization, add it to the init array
				if (Data.bRequiresInit)
				{
					InitSectionLUT.Add(SortedCompileData[Index].Child);
				}
				EvalSectionLUT.Add(SortedCompileData[Index].Child);
			}
		}

		FMovieSceneFieldEntry_EvaluationTrack Entry{ Data.Track, NumChildren };
		if (Data.bRequiresInit)
		{
			InitTrackLUT.Add(Entry);
		}
		EvalTrackLUT.Add(Entry);
	}

	AddPtrsToGroup(OutGroup, InitTrackLUT, InitSectionLUT, EvalTrackLUT, EvalSectionLUT);
}


void UMovieSceneCompiledDataManager::PopulateMetaData(const FMovieSceneSequenceHierarchy& RootHierarchy, const TArray<FCompileOnTheFlyData>& SortedCompileData, TMovieSceneEvaluationTreeDataIterator<FMovieSceneSubSequenceTreeEntry> SubSequences, FMovieSceneEvaluationMetaData* OutMetaData)
{
	check(OutMetaData);
	OutMetaData->Reset();

	uint16 SetupIndex    = 0;
	uint16 TearDownIndex = 0;
	for (const FCompileOnTheFlyData& CompileData : SortedCompileData)
	{
		if (CompileData.bRequiresInit)
		{
			uint32 ChildIndex = CompileData.Child.ChildIndex == uint16(-1) ? uint32(-1) : CompileData.Child.ChildIndex;

			FMovieSceneEvaluationKey TrackKey(CompileData.Track.SequenceID, CompileData.Track.TrackIdentifier, ChildIndex);
			OutMetaData->ActiveEntities.Add(FMovieSceneOrderedEvaluationKey{ TrackKey, SetupIndex++, (CompileData.bPriorityTearDown ? TearDownIndex : uint16(MAX_uint16-TearDownIndex)) });
			++TearDownIndex;
		}
	}

	// Then all the eval tracks
	for (const FCompileOnTheFlyData& CompileData : SortedCompileData)
	{
		if (!CompileData.bRequiresInit)
		{
			uint32 ChildIndex = CompileData.Child.ChildIndex == uint16(-1) ? uint32(-1) : CompileData.Child.ChildIndex;

			FMovieSceneEvaluationKey TrackKey(CompileData.Track.SequenceID, CompileData.Track.TrackIdentifier, ChildIndex);
			OutMetaData->ActiveEntities.Add(FMovieSceneOrderedEvaluationKey{ TrackKey, SetupIndex++, (CompileData.bPriorityTearDown ? TearDownIndex : uint16(MAX_uint16-TearDownIndex)) });
			++TearDownIndex;
		}
	}

	Algo::SortBy(OutMetaData->ActiveEntities, &FMovieSceneOrderedEvaluationKey::Key);

	{
		OutMetaData->ActiveSequences.Reset();
		OutMetaData->ActiveSequences.Add(MovieSceneSequenceID::Root);

		for (FMovieSceneSubSequenceTreeEntry SubSequenceEntry : SubSequences)
		{
			OutMetaData->ActiveSequences.Add(SubSequenceEntry.SequenceID);
		}

		OutMetaData->ActiveSequences.Sort();
	}
}


void UMovieSceneCompiledDataManager::CompileTrack(FMovieSceneCompiledDataEntry* OutEntry, const FMovieSceneBinding* ObjectBinding, UMovieSceneTrack* Track, const FTrackGatherParameters& Params, TSet<FGuid>* OutCompiledSignatures, FMovieSceneGatheredCompilerData* OutCompilerData)
{
	using namespace UE::MovieScene;

	check(Track);
	check(OutCompiledSignatures);

	const bool bTrackMatchesFlags = ( Params.Flags == ESectionEvaluationFlags::None )
		|| ( EnumHasAnyFlags(Params.Flags, ESectionEvaluationFlags::PreRoll)  && Track->EvalOptions.bEvaluateInPreroll  )
		|| ( EnumHasAnyFlags(Params.Flags, ESectionEvaluationFlags::PostRoll) && Track->EvalOptions.bEvaluateInPostroll );

	if (!bTrackMatchesFlags)
	{
		return;
	}

	if (Track->IsEvalDisabled())
	{
		return;
	}

	UMovieSceneSequence* Sequence = OutEntry->GetSequence();
	check(Sequence);

	// -------------------------------------------------------------------------------------------------------------------------------------
	// Step 1 - ensure that track templates exist for any track that implements IMovieSceneTrackTemplateProducer
	FMovieSceneTrackIdentifier TrackIdentifier;
	FMovieSceneEvaluationTemplate* TrackTemplate = nullptr;
	if (const IMovieSceneTrackTemplateProducer* TrackTemplateProducer = Cast<const IMovieSceneTrackTemplateProducer>(Track))
	{
		TrackTemplate = &TrackTemplates.FindOrAdd(OutEntry->DataID.Value);

		check(TrackTemplate);

		TrackIdentifier = TrackTemplate->GetLedger().FindTrackIdentifier(Track->GetSignature());

		if (!TrackIdentifier)
		{
			// If the track doesn't exist - we need to generate it from scratch
			FMovieSceneTrackCompilerArgs Args(Track, &Params.TemplateGenerator);
			if (ObjectBinding)
			{
				Args.ObjectBindingId = ObjectBinding->GetObjectGuid();
			}

			Args.DefaultCompletionMode = Sequence->DefaultCompletionMode;

			TrackTemplateProducer->GenerateTemplate(Args);

			TrackIdentifier = TrackTemplate->GetLedger().FindTrackIdentifier(Track->GetSignature());
		}

		if (TrackIdentifier)
		{
			OutCompiledSignatures->Add(Track->GetSignature());
		}

		OutCompilerData->AccumulatedMask |= EMovieSceneSequenceCompilerMask::EvaluationTemplate;
	}
}

void UMovieSceneCompiledDataManager::GatherTrack(const FMovieSceneBinding* ObjectBinding, UMovieSceneTrack* Track, const FTrackGatherParameters& Params, const FMovieSceneEvaluationTemplate* TrackTemplate, FMovieSceneGatheredCompilerData* OutCompilerData) const
{
	using namespace UE::MovieScene;

	check(Track);

	const bool bTrackMatchesFlags = ( Params.Flags == ESectionEvaluationFlags::None )
		|| ( EnumHasAnyFlags(Params.Flags, ESectionEvaluationFlags::PreRoll)  && Track->EvalOptions.bEvaluateInPreroll  )
		|| ( EnumHasAnyFlags(Params.Flags, ESectionEvaluationFlags::PostRoll) && Track->EvalOptions.bEvaluateInPostroll );

	if (!bTrackMatchesFlags)
	{
		return;
	}

	if (Track->IsEvalDisabled())
	{
		return;
	}

	// Some tracks could want to do some custom pre-compilation things.
	Track->PreCompile();

	const FMovieSceneTrackEvaluationField& EvaluationField = Track->GetEvaluationField();

	// -------------------------------------------------------------------------------------------------------------------------------------
	// Step 1 - Handle any entity producers that exist within the field
	if (OutCompilerData->EntityField)
	{
		FMovieSceneEntityComponentFieldBuilder FieldBuilder(OutCompilerData->EntityField);

		if (ObjectBinding)
		{
			FieldBuilder.GetSharedMetaData().ObjectBindingID = ObjectBinding->GetObjectGuid();
		}

		for (const FMovieSceneTrackEvaluationFieldEntry& Entry : EvaluationField.Entries)
		{
			IMovieSceneEntityProvider* EntityProvider = Cast<IMovieSceneEntityProvider>(Entry.Section);
			if (!EntityProvider)
			{
				continue;
			}

			// This codepath should only ever execute for the highest level so we do not need to do any transformations
			TRange<FFrameNumber> EffectiveRange = TRange<FFrameNumber>::Intersection(Params.LocalClampRange, Entry.Range);
			if (!EffectiveRange.IsEmpty())
			{
				FMovieSceneEvaluationFieldEntityMetaData MetaData;

				MetaData.ForcedTime = Entry.ForcedTime;
				MetaData.Flags      = Entry.Flags;
				MetaData.bEvaluateInSequencePreRoll  = Track->EvalOptions.bEvaluateInPreroll;
				MetaData.bEvaluateInSequencePostRoll = Track->EvalOptions.bEvaluateInPostroll;

				if (!EntityProvider->PopulateEvaluationField(EffectiveRange, MetaData, &FieldBuilder))
				{
					const int32 EntityIndex   = FieldBuilder.FindOrAddEntity(Entry.Section, 0);
					const int32 MetaDataIndex = FieldBuilder.AddMetaData(MetaData);

					FieldBuilder.AddPersistentEntity(EffectiveRange, EntityIndex, MetaDataIndex);
				}
			}
		}
	}

	// -------------------------------------------------------------------------------------------------------------------------------------
	// Step 2 - Handle the track being a template producer
	FMovieSceneTrackIdentifier TrackIdentifier = TrackTemplate ? TrackTemplate->GetLedger().FindTrackIdentifier(Track->GetSignature()) : FMovieSceneTrackIdentifier();
	if (TrackIdentifier)
	{
		// Iterate everything in the field
		for (const FMovieSceneTrackEvaluationFieldEntry& Entry : EvaluationField.Entries)
		{
			FMovieSceneSequenceTransform SequenceToRootTransform  = Params.RootToSequenceTransform.InverseLinearOnly();
			TRange<FFrameNumber>         ClampedRangeRoot         = Params.ClampRoot(SequenceToRootTransform.TransformRangeUnwarped(Entry.Range));
			UMovieSceneSection*          Section                  = Entry.Section;

			if (ClampedRangeRoot.IsEmpty())
			{
				continue;
			}

			check(TrackTemplate);
			const FMovieSceneEvaluationTrack* EvaluationTrack = TrackTemplate->FindTrack(TrackIdentifier);
			check(EvaluationTrack);

			// Get the correct template for the sub sequence
			FCompileOnTheFlyData CompileData;

			CompileData.Track                   = FMovieSceneEvaluationFieldTrackPtr(Params.SequenceID, TrackIdentifier);
			CompileData.EvaluationPriority      = EvaluationTrack->GetEvaluationPriority();
			CompileData.EvaluationGroup         = EvaluationTrack->GetEvaluationGroup();
			CompileData.GroupEvaluationPriority = GetMovieSceneModule().GetEvaluationGroupParameters(CompileData.EvaluationGroup).EvaluationPriority;
			CompileData.HierarchicalBias        = Params.HierarchicalBias;
			CompileData.bPriorityTearDown       = EvaluationTrack->HasTearDownPriority();

			auto FindChildWithSection = [Section](FMovieSceneEvalTemplatePtr ChildTemplate)
			{
				return ChildTemplate.IsValid() && ChildTemplate->GetSourceSection() == Section;
			};

			const int32 ChildTemplateIndex = Section ? EvaluationTrack->GetChildTemplates().IndexOfByPredicate(FindChildWithSection) : INDEX_NONE;
			if (ChildTemplateIndex != INDEX_NONE)
			{
				check(ChildTemplateIndex >= 0 && ChildTemplateIndex < TNumericLimits<uint16>::Max());

				ESectionEvaluationFlags Flags = Params.Flags == ESectionEvaluationFlags::None ? Entry.Flags : Params.Flags;

				CompileData.ChildPriority = Entry.LegacySortOrder;
				CompileData.Child         = FMovieSceneFieldEntry_ChildTemplate((uint16)ChildTemplateIndex, Flags, Entry.ForcedTime);
				CompileData.bRequiresInit = EvaluationTrack->GetChildTemplate(ChildTemplateIndex).RequiresInitialization();
			}
			else
			{
				CompileData.ChildPriority = 0;
				CompileData.Child         = FMovieSceneFieldEntry_ChildTemplate{};
				CompileData.bRequiresInit = false;
			}

			OutCompilerData->TrackTemplates.Add(ClampedRangeRoot, CompileData);
		}
	}
}

bool UMovieSceneCompiledDataManager::CompileHierarchy(UMovieSceneSequence* Sequence, FMovieSceneSequenceHierarchy* InOutHierarchy, EMovieSceneServerClientMask InNetworkMask)
{
	FGatherParameters Params;
	Params.NetworkMask = InNetworkMask;
	return CompileHierarchy(Sequence, Params, InOutHierarchy);
}

bool UMovieSceneCompiledDataManager::CompileHierarchy(UMovieSceneSequence* Sequence, const FGatherParameters& Params, FMovieSceneSequenceHierarchy* InOutHierarchy)
{
	FMovieSceneRootOverridePath RootPath;
	return CompileHierarchyImpl(Sequence, Params, FMovieSceneEvaluationOperand(), &RootPath, InOutHierarchy);
}

bool UMovieSceneCompiledDataManager::CompileHierarchyImpl(UMovieSceneSequence* Sequence, const FGatherParameters& Params, const FMovieSceneEvaluationOperand& Operand, FMovieSceneRootOverridePath* RootPath, FMovieSceneSequenceHierarchy* InOutHierarchy)
{
	UMovieScene* MovieScene = Sequence->GetMovieScene();
	check(MovieScene && RootPath && InOutHierarchy);

	bool bContainsSubSequences = false;

	for (UMovieSceneTrack* Track : MovieScene->GetMasterTracks())
	{
		if (UMovieSceneSubTrack* SubTrack = Cast<UMovieSceneSubTrack>(Track))
		{
			bContainsSubSequences |= CompileSubTrackHierarchy(SubTrack, Params, Operand, RootPath, InOutHierarchy);
		}
	}

	for (const FMovieSceneBinding& ObjectBinding : MovieScene->GetBindings())
	{
		for (UMovieSceneTrack* Track : ObjectBinding.GetTracks())
		{
			if (UMovieSceneSubTrack* SubTrack = Cast<UMovieSceneSubTrack>(Track))
			{
				const FMovieSceneEvaluationOperand ChildOperand(Params.SequenceID, ObjectBinding.GetObjectGuid());

				bContainsSubSequences |= CompileSubTrackHierarchy(SubTrack, Params, ChildOperand, RootPath, InOutHierarchy);
			}
		}
	}

	return bContainsSubSequences;
}

bool UMovieSceneCompiledDataManager::CompileSubTrackHierarchy(UMovieSceneSubTrack* SubTrack, const FGatherParameters& Params, const FMovieSceneEvaluationOperand& Operand, FMovieSceneRootOverridePath* RootPath, FMovieSceneSequenceHierarchy* InOutHierarchy)
{
	bool bContainsSubSequences = false;

	check(SubTrack && RootPath);

	const FMovieSceneSequenceID ParentSequenceID = Params.SequenceID;

	TSortedMap<UMovieSceneSection*, FMovieSceneSequenceID, TInlineAllocator<16>> SectionToID;

	// ---------------------------------------------------------------------------------------------------------------------------
	// Step 1 - Add structural information for the sequence
	for (UMovieSceneSection* Section : SubTrack->GetAllSections())
	{
		UMovieSceneSubSection* SubSection  = Cast<UMovieSceneSubSection>(Section);
		if (!SubSection)
		{
			continue;
		}

		// Note: we always compile FMovieSceneSubSequenceData for all entries of a hierarchy, even if excluded from the network mask
		// to ensure that hierarchical information is still available when emulating different network masks

		UMovieSceneSequence* SubSequence = SubSection->GetSequence();
		if (!SubSequence)
		{
			continue;
		}

		const FMovieSceneSequenceID InnerSequenceID = RootPath->Remap(SubSection->GetSequenceID());

		SectionToID.Add(SubSection, InnerSequenceID);

		FSubSequenceInstanceDataParams InstanceParams{ InnerSequenceID, Operand };
		FMovieSceneSubSequenceData     NewSubData = SubSection->GenerateSubSequenceData(InstanceParams);

		// LocalClampRange here is in SubTrack's space, so we need to multiply that by the OuterToInnerTransform (which is the same as RootToSequenceTransform here before we transform it)
		TRange<FFrameNumber> InnerClampRange = NewSubData.RootToSequenceTransform.TransformRangeUnwarped(Params.LocalClampRange);

		NewSubData.PlayRange               = TRange<FFrameNumber>::Intersection(InnerClampRange, NewSubData.PlayRange.Value);
		NewSubData.RootToSequenceTransform = NewSubData.RootToSequenceTransform * Params.RootToSequenceTransform;
		NewSubData.HierarchicalBias        = Params.HierarchicalBias + NewSubData.HierarchicalBias;
		NewSubData.bHasHierarchicalEasing  = Params.bHasHierarchicalEasing || NewSubData.bHasHierarchicalEasing;

		// Add the sub data to the root hierarchy
		InOutHierarchy->Add(NewSubData, InnerSequenceID, ParentSequenceID);
		bContainsSubSequences = true;
	}

	// ---------------------------------------------------------------------------------------------------------------------------
	// Step 2 - add entries to the tree for each sub sequence in the range
	const bool bTrackMatchesFlags = ( Params.Flags == ESectionEvaluationFlags::None )
		|| ( EnumHasAnyFlags(Params.Flags, ESectionEvaluationFlags::PreRoll)  && SubTrack->EvalOptions.bEvaluateInPreroll  )
		|| ( EnumHasAnyFlags(Params.Flags, ESectionEvaluationFlags::PostRoll) && SubTrack->EvalOptions.bEvaluateInPostroll );

	const bool bIsEvalDisabled = SubTrack->IsEvalDisabled();

	if (bTrackMatchesFlags && !bIsEvalDisabled)
	{
		for (const FMovieSceneTrackEvaluationFieldEntry& Entry : SubTrack->GetEvaluationField().Entries)
		{
			UMovieSceneSubSection* SubSection  = Cast<UMovieSceneSubSection>(Entry.Section);
			if (!SubSection || SubSection->GetSequence() == nullptr)
			{
				continue;
			}

			EMovieSceneServerClientMask NewMask = Params.NetworkMask & SubSection->GetNetworkMask();
			if (NewMask == EMovieSceneServerClientMask::None)
			{
				continue;
			}

			TRange<FFrameNumber> EffectiveRange = Params.ClampRoot(Entry.Range * Params.RootToSequenceTransform.InverseLinearOnly());
			if (EffectiveRange.IsEmpty())
			{
				continue;
			}

			const FMovieSceneSequenceID       SubSequenceID = SectionToID.FindChecked(Entry.Section);
			const FMovieSceneSubSequenceData* SubData       = InOutHierarchy->FindSubData(SubSequenceID);

			checkf(SubData, TEXT("Unable to locate sub-data for a sub section that appears in the track's evaluation field - this indicates that the section is being evaluated even though it is not active"));

			// Add the sub sequence to the tree
			InOutHierarchy->AddRange(SubSequenceID, EffectiveRange, Entry.Flags | Params.Flags);

			// Iterate into the sub sequence
			FGatherParameters SubParams = Params.CreateForSubData(*SubData, SubSequenceID);
			SubParams.SetClampRange(EffectiveRange);
			SubParams.Flags |= Entry.Flags;
			SubParams.NetworkMask = NewMask;

			RootPath->Push(SubData->DeterministicSequenceID);
			CompileHierarchyImpl(SubData->GetSequence(), SubParams, Operand, RootPath, InOutHierarchy);
			RootPath->Pop();
		}
	}

	return bContainsSubSequences;
}
