// Copyright Epic Games, Inc. All Rights Reserved.

#include "Compilation/MovieSceneCompiledVolatilityManager.h"
#include "Compilation/MovieSceneCompiledDataManager.h"
#include "MovieSceneFwd.h"

namespace UE
{
namespace MovieScene
{

#if WITH_EDITOR

int32 GVolatileSequencesInEditor = 1;
FAutoConsoleVariableRef CVarVolatileSequencesInEditor(
	TEXT("Sequencer.VolatileSequencesInEditor"),
	GVolatileSequencesInEditor,
	TEXT("(Default: 1) When non-zero, all assets will be treated as volatile in editor. Can be disabled to bypass volatility checks in-editor for more representative runtime performance metrics.\n"),
	ECVF_Default
);

#endif


FORCEINLINE EMovieSceneSequenceFlags GetEditorVolatilityFlags()
{
#if WITH_EDITOR
	return GVolatileSequencesInEditor ? EMovieSceneSequenceFlags::Volatile : EMovieSceneSequenceFlags::None;
#else
	return EMovieSceneSequenceFlags::None;
#endif
}


TUniquePtr<FCompiledDataVolatilityManager> FCompiledDataVolatilityManager::Construct(IMovieScenePlayer& Player, FMovieSceneCompiledDataID RootDataID, UMovieSceneCompiledDataManager* CompiledDataManager)
{
	EMovieSceneSequenceFlags SequenceFlags = CompiledDataManager->GetEntry(RootDataID).AccumulatedFlags | GetEditorVolatilityFlags();
	if (!EnumHasAnyFlags(SequenceFlags, EMovieSceneSequenceFlags::Volatile))
	{
		return nullptr;
	}

	TUniquePtr<FCompiledDataVolatilityManager> VolatilityManager = MakeUnique<FCompiledDataVolatilityManager>();
	VolatilityManager->ConditionalRecompile(Player, RootDataID, CompiledDataManager);
	return VolatilityManager;
}

bool FCompiledDataVolatilityManager::HasBeenRecompiled(FMovieSceneCompiledDataID RootDataID, UMovieSceneCompiledDataManager* CompiledDataManager) const
{
	if (HasSequenceBeenRecompiled(RootDataID, MovieSceneSequenceID::Root, CompiledDataManager))
	{
		return true;
	}

	if (const FMovieSceneSequenceHierarchy* Hierarchy = CompiledDataManager->FindHierarchy(RootDataID))
	{
		for (const TTuple<FMovieSceneSequenceID, FMovieSceneSubSequenceData>& Pair : Hierarchy->AllSubSequenceData())
		{
			FMovieSceneCompiledDataID SubDataID = CompiledDataManager->GetSubDataID(RootDataID, Pair.Key);
			if (HasSequenceBeenRecompiled(SubDataID, Pair.Key, CompiledDataManager))
			{
				return true;
			}
		}
	}

	return false;
}

bool FCompiledDataVolatilityManager::HasSequenceBeenRecompiled(FMovieSceneCompiledDataID DataID, FMovieSceneSequenceID SequenceID, UMovieSceneCompiledDataManager* CompiledDataManager) const
{
	const FGuid* CachedSignature = CachedCompilationSignatures.Find(SequenceID);

	FMovieSceneCompiledDataEntry CompiledEntry = CompiledDataManager->GetEntry(DataID);
	return CachedSignature == nullptr || *CachedSignature != CompiledEntry.CompiledSignature;
}

bool FCompiledDataVolatilityManager::ConditionalRecompile(IMovieScenePlayer& Player, FMovieSceneCompiledDataID RootDataID, UMovieSceneCompiledDataManager* CompiledDataManager)
{
	bool bRecompiled = false;

	if (CompiledDataManager->IsDirty(RootDataID))
	{
		CompiledDataManager->Compile(RootDataID);
		bRecompiled = true;
	}
	else
	{
		bRecompiled = HasBeenRecompiled(RootDataID, CompiledDataManager);
	}

	if (bRecompiled)
	{
		UpdateCachedSignatures(Player, RootDataID, CompiledDataManager);
	}

	return bRecompiled;
}

void FCompiledDataVolatilityManager::UpdateCachedSignatures(IMovieScenePlayer& Player, FMovieSceneCompiledDataID RootDataID, UMovieSceneCompiledDataManager* CompiledDataManager)
{
	CachedCompilationSignatures.Reset();

	const FMovieSceneCompiledDataEntry RootEntry = CompiledDataManager->GetEntry(RootDataID);
	CachedCompilationSignatures.Add(MovieSceneSequenceID::Root, RootEntry.CompiledSignature);

	UMovieSceneSequence* RootSequence = RootEntry.GetSequence();
	if (RootSequence)
	{
		Player.State.AssignSequence(MovieSceneSequenceID::Root, *RootSequence, Player);
	}

	if (const FMovieSceneSequenceHierarchy* Hierarchy = CompiledDataManager->FindHierarchy(RootDataID))
	{
		for (const TTuple<FMovieSceneSequenceID, FMovieSceneSubSequenceData>& SubData : Hierarchy->AllSubSequenceData())
		{
			const FMovieSceneCompiledDataID    SubDataID = CompiledDataManager->GetSubDataID(RootDataID, SubData.Key);
			const FMovieSceneCompiledDataEntry SubEntry  = CompiledDataManager->GetEntry(SubDataID);

			CachedCompilationSignatures.Add(SubData.Key, SubEntry.CompiledSignature);

			UMovieSceneSequence* Sequence = SubData.Value.GetSequence();
			if (Sequence)
			{
				Player.State.AssignSequence(SubData.Key, *Sequence, Player);
			}
		}
	}
}


} // namespace MovieScene
} // namespace UE
