// Copyright Epic Games, Inc. All Rights Reserved.

#include "ExtensionLibraries/MovieSceneSequenceExtensions.h"
#include "Tracks/MovieSceneCameraCutTrack.h"
#include "MovieSceneSequence.h"
#include "MovieScene.h"
#include "MovieSceneFolder.h"
#include "Algo/Find.h"
#include "Compilation/MovieSceneCompiledDataManager.h"

TArray<UMovieSceneTrack*> UMovieSceneSequenceExtensions::FilterTracks(TArrayView<UMovieSceneTrack* const> InTracks, UClass* DesiredClass, bool bExactMatch)
{
	TArray<UMovieSceneTrack*> Tracks;

	for (UMovieSceneTrack* Track : InTracks)
	{
		UClass* TrackClass = Track->GetClass();

		if ( TrackClass == DesiredClass || (!bExactMatch && TrackClass->IsChildOf(DesiredClass)) )
		{
			Tracks.Add(Track);
		}
	}

	return Tracks;
}

UMovieScene* UMovieSceneSequenceExtensions::GetMovieScene(UMovieSceneSequence* Sequence)
{
	return Sequence ? Sequence->GetMovieScene() : nullptr;
}

TArray<UMovieSceneTrack*> UMovieSceneSequenceExtensions::GetMasterTracks(UMovieSceneSequence* Sequence)
{
	TArray<UMovieSceneTrack*> Tracks;

	UMovieScene* MovieScene = GetMovieScene(Sequence);
	if (MovieScene)
	{
		Tracks = MovieScene->GetMasterTracks();

		if (UMovieSceneTrack* CameraCutTrack = MovieScene->GetCameraCutTrack())
		{
			Tracks.Add(CameraCutTrack);
		}
	}

	return Tracks;
}

TArray<UMovieSceneTrack*> UMovieSceneSequenceExtensions::FindMasterTracksByType(UMovieSceneSequence* Sequence, TSubclassOf<UMovieSceneTrack> TrackType)
{
	UMovieScene* MovieScene   = GetMovieScene(Sequence);
	UClass*      DesiredClass = TrackType.Get();

	if (MovieScene && DesiredClass)
	{
		bool bExactMatch = false;
		TArray<UMovieSceneTrack*> MatchedTracks = FilterTracks(MovieScene->GetMasterTracks(), TrackType.Get(), bExactMatch);

		// Have to check camera cut tracks separately since they're not in the master tracks array (why?)
		UMovieSceneTrack* CameraCutTrack = MovieScene->GetCameraCutTrack();
		if (CameraCutTrack && CameraCutTrack->GetClass()->IsChildOf(DesiredClass))
		{
			MatchedTracks.Add(CameraCutTrack);
		}

		return MatchedTracks;
	}

	return TArray<UMovieSceneTrack*>();
}

TArray<UMovieSceneTrack*> UMovieSceneSequenceExtensions::FindMasterTracksByExactType(UMovieSceneSequence* Sequence, TSubclassOf<UMovieSceneTrack> TrackType)
{
	UMovieScene* MovieScene   = GetMovieScene(Sequence);
	UClass*      DesiredClass = TrackType.Get();

	if (MovieScene && DesiredClass)
	{
		bool bExactMatch = true;
		TArray<UMovieSceneTrack*> MatchedTracks = FilterTracks(MovieScene->GetMasterTracks(), TrackType.Get(), bExactMatch);

		// Have to check camera cut tracks separately since they're not in the master tracks array (why?)
		UMovieSceneTrack* CameraCutTrack = MovieScene->GetCameraCutTrack();
		if (CameraCutTrack && CameraCutTrack->GetClass() == DesiredClass)
		{
			MatchedTracks.Add(CameraCutTrack);
		}

		return MatchedTracks;
	}

	return TArray<UMovieSceneTrack*>();
}

UMovieSceneTrack* UMovieSceneSequenceExtensions::AddMasterTrack(UMovieSceneSequence* Sequence, TSubclassOf<UMovieSceneTrack> TrackType)
{
	// @todo: sequencer-python: master track type compatibility with sequence. Currently that's really only loosely defined by track editors, which is not sufficient here.
	UMovieScene* MovieScene = GetMovieScene(Sequence);
	if (MovieScene)
	{
		if (TrackType->IsChildOf(UMovieSceneCameraCutTrack::StaticClass()))
		{
			return MovieScene->AddCameraCutTrack(TrackType);
		}
		else
		{
			return MovieScene->AddMasterTrack(TrackType);
		}
	}

	return nullptr;
}

bool UMovieSceneSequenceExtensions::RemoveMasterTrack(UMovieSceneSequence* Sequence, UMovieSceneTrack* MasterTrack)
{
	UMovieScene* MovieScene = GetMovieScene(Sequence);
	if (MovieScene)
	{
		if (MasterTrack->IsA(UMovieSceneCameraCutTrack::StaticClass()))
		{
			MovieScene->RemoveCameraCutTrack();
			return true;
		}
		else
		{
			return MovieScene->RemoveMasterTrack(*MasterTrack);
		}
	}

	return false;
}


FFrameRate UMovieSceneSequenceExtensions::GetDisplayRate(UMovieSceneSequence* Sequence)
{
	UMovieScene* MovieScene = GetMovieScene(Sequence);
	return MovieScene ? MovieScene->GetDisplayRate() : FFrameRate();
}

void UMovieSceneSequenceExtensions::SetDisplayRate(UMovieSceneSequence* Sequence, FFrameRate DisplayRate)
{
	UMovieScene* MovieScene = GetMovieScene(Sequence);
	if (MovieScene)
	{
		MovieScene->Modify();

		MovieScene->SetDisplayRate(DisplayRate);
	}
}

FFrameRate UMovieSceneSequenceExtensions::GetTickResolution(UMovieSceneSequence* Sequence)
{
	UMovieScene* MovieScene = GetMovieScene(Sequence);
	return MovieScene ? MovieScene->GetTickResolution() : FFrameRate();
}

void UMovieSceneSequenceExtensions::SetTickResolution(UMovieSceneSequence* Sequence, FFrameRate TickResolution)
{
	UMovieScene* MovieScene = GetMovieScene(Sequence);
	if (MovieScene)
	{
		MovieScene->Modify();

		UE::MovieScene::TimeHelpers::MigrateFrameTimes(MovieScene->GetTickResolution(), TickResolution, MovieScene);
	}
}

void UMovieSceneSequenceExtensions::SetTickResolutionDirectly(UMovieSceneSequence* Sequence, FFrameRate TickResolution)
{
	UMovieScene* MovieScene = GetMovieScene(Sequence);
	if (MovieScene)	
	{
		MovieScene->Modify();

		MovieScene->SetTickResolutionDirectly(TickResolution);
	}
}

FSequencerScriptingRange UMovieSceneSequenceExtensions::MakeRange(UMovieSceneSequence* Sequence, int32 StartFrame, int32 Duration)
{
	FFrameRate FrameRate = GetDisplayRate(Sequence);
	return FSequencerScriptingRange::FromNative(TRange<FFrameNumber>(StartFrame, StartFrame+Duration), FrameRate, FrameRate);
}

FSequencerScriptingRange UMovieSceneSequenceExtensions::MakeRangeSeconds(UMovieSceneSequence* Sequence, float StartTime, float Duration)
{
	FFrameRate FrameRate = GetDisplayRate(Sequence);
	return FSequencerScriptingRange::FromNative(TRange<FFrameNumber>((StartTime*FrameRate).FloorToFrame(), ((StartTime+Duration) * FrameRate).CeilToFrame()), FrameRate, FrameRate);
}

FSequencerScriptingRange UMovieSceneSequenceExtensions::GetPlaybackRange(UMovieSceneSequence* Sequence)
{
	UMovieScene* MovieScene = GetMovieScene(Sequence);
	if (MovieScene)
	{
		return FSequencerScriptingRange::FromNative(MovieScene->GetPlaybackRange(), GetTickResolution(Sequence), GetDisplayRate(Sequence));
	}
	else
	{
		return FSequencerScriptingRange();
	}
}

int32 UMovieSceneSequenceExtensions::GetPlaybackStart(UMovieSceneSequence* Sequence)
{
	UMovieScene* MovieScene = GetMovieScene(Sequence);
	if (MovieScene)
	{
		FFrameRate DisplayRate = GetDisplayRate(Sequence);
		return ConvertFrameTime(UE::MovieScene::DiscreteInclusiveLower(MovieScene->GetPlaybackRange()), GetTickResolution(Sequence), DisplayRate).FloorToFrame().Value;
	}
	else
	{
		return -1;
	}
}

float UMovieSceneSequenceExtensions::GetPlaybackStartSeconds(UMovieSceneSequence* Sequence)
{
	UMovieScene* MovieScene = GetMovieScene(Sequence);
	if (MovieScene)
	{
		FFrameRate DisplayRate = GetDisplayRate(Sequence);
		return DisplayRate.AsSeconds(ConvertFrameTime(UE::MovieScene::DiscreteInclusiveLower(MovieScene->GetPlaybackRange()), GetTickResolution(Sequence), DisplayRate));
	}

	return -1.f;
}

int32 UMovieSceneSequenceExtensions::GetPlaybackEnd(UMovieSceneSequence* Sequence)
{
	UMovieScene* MovieScene = GetMovieScene(Sequence);
	if (MovieScene)
	{
		FFrameRate DisplayRate = GetDisplayRate(Sequence);
		return ConvertFrameTime(UE::MovieScene::DiscreteExclusiveUpper(MovieScene->GetPlaybackRange()), GetTickResolution(Sequence), DisplayRate).FloorToFrame().Value;
	}
	else
	{
		return -1;
	}
}

float UMovieSceneSequenceExtensions::GetPlaybackEndSeconds(UMovieSceneSequence* Sequence)
{
	UMovieScene* MovieScene = GetMovieScene(Sequence);
	if (MovieScene)
	{
		FFrameRate DisplayRate = GetDisplayRate(Sequence);
		return DisplayRate.AsSeconds(ConvertFrameTime(UE::MovieScene::DiscreteExclusiveUpper(MovieScene->GetPlaybackRange()), GetTickResolution(Sequence), DisplayRate));
	}

	return -1.f;
}

void UMovieSceneSequenceExtensions::SetPlaybackStart(UMovieSceneSequence* Sequence, int32 StartFrame)
{
	UMovieScene* MovieScene = GetMovieScene(Sequence);
	if (MovieScene)
	{
		FFrameRate DisplayRate = GetDisplayRate(Sequence);

		TRange<FFrameNumber> NewRange = MovieScene->GetPlaybackRange();
		NewRange.SetLowerBoundValue(ConvertFrameTime(StartFrame, DisplayRate, GetTickResolution(Sequence)).FrameNumber);

		MovieScene->SetPlaybackRange(NewRange);
	}
}

void UMovieSceneSequenceExtensions::SetPlaybackStartSeconds(UMovieSceneSequence* Sequence, float StartTime)
{
	UMovieScene* MovieScene = GetMovieScene(Sequence);
	if (MovieScene)
	{
		TRange<FFrameNumber> NewRange = MovieScene->GetPlaybackRange();
		NewRange.SetLowerBoundValue((StartTime * GetTickResolution(Sequence)).RoundToFrame());

		MovieScene->SetPlaybackRange(NewRange);
	}
}

void UMovieSceneSequenceExtensions::SetPlaybackEnd(UMovieSceneSequence* Sequence, int32 EndFrame)
{
	UMovieScene* MovieScene = GetMovieScene(Sequence);
	if (MovieScene)
	{
		FFrameRate DisplayRate = GetDisplayRate(Sequence);

		TRange<FFrameNumber> NewRange = MovieScene->GetPlaybackRange();
		NewRange.SetUpperBoundValue(ConvertFrameTime(EndFrame, DisplayRate, GetTickResolution(Sequence)).FrameNumber);

		MovieScene->SetPlaybackRange(NewRange);
	}
}

void UMovieSceneSequenceExtensions::SetPlaybackEndSeconds(UMovieSceneSequence* Sequence, float EndTime)
{
	UMovieScene* MovieScene = GetMovieScene(Sequence);
	if (MovieScene)
	{
		TRange<FFrameNumber> NewRange = MovieScene->GetPlaybackRange();
		NewRange.SetUpperBoundValue((EndTime * GetTickResolution(Sequence)).RoundToFrame());

		MovieScene->SetPlaybackRange(NewRange);
	}
}

void UMovieSceneSequenceExtensions::SetViewRangeStart(UMovieSceneSequence* Sequence, float StartTimeInSeconds)
{
	UMovieScene* MovieScene = GetMovieScene(Sequence);
	if (MovieScene)
	{
#if WITH_EDITORONLY_DATA
		MovieScene->SetViewRange(StartTimeInSeconds, MovieScene->GetEditorData().ViewEnd);
#endif
	}
}

float UMovieSceneSequenceExtensions::GetViewRangeStart(UMovieSceneSequence* Sequence)
{
	UMovieScene* MovieScene = GetMovieScene(Sequence);
	if (MovieScene)
	{
#if WITH_EDITORONLY_DATA
		return MovieScene->GetEditorData().ViewStart;
#endif
	}
	return 0.f;
}

void UMovieSceneSequenceExtensions::SetViewRangeEnd(UMovieSceneSequence* Sequence, float EndTimeInSeconds)
{
	UMovieScene* MovieScene = GetMovieScene(Sequence);
	if (MovieScene)
	{
#if WITH_EDITORONLY_DATA
		MovieScene->SetViewRange(MovieScene->GetEditorData().ViewStart, EndTimeInSeconds);
#endif
	}
}

float UMovieSceneSequenceExtensions::GetViewRangeEnd(UMovieSceneSequence* Sequence)
{
	UMovieScene* MovieScene = GetMovieScene(Sequence);
	if (MovieScene)
	{
#if WITH_EDITORONLY_DATA
		return MovieScene->GetEditorData().ViewEnd;
#endif
	}
	return 0.f;
}

void UMovieSceneSequenceExtensions::SetWorkRangeStart(UMovieSceneSequence* Sequence, float StartTimeInSeconds)
{
	UMovieScene* MovieScene = GetMovieScene(Sequence);
	if (MovieScene)
	{
#if WITH_EDITORONLY_DATA
		MovieScene->SetWorkingRange(StartTimeInSeconds, MovieScene->GetEditorData().WorkEnd);
#endif
	}
}

float UMovieSceneSequenceExtensions::GetWorkRangeStart(UMovieSceneSequence* Sequence)
{
	UMovieScene* MovieScene = GetMovieScene(Sequence);
	if (MovieScene)
	{
#if WITH_EDITORONLY_DATA
		return MovieScene->GetEditorData().WorkStart;
#endif
	}
	return 0.f;
}

void UMovieSceneSequenceExtensions::SetWorkRangeEnd(UMovieSceneSequence* Sequence, float EndTimeInSeconds)
{
	UMovieScene* MovieScene = GetMovieScene(Sequence);
	if (MovieScene)
	{
#if WITH_EDITORONLY_DATA
		MovieScene->SetWorkingRange(MovieScene->GetEditorData().WorkStart, EndTimeInSeconds);
#endif
	}
}

float UMovieSceneSequenceExtensions::GetWorkRangeEnd(UMovieSceneSequence* Sequence)
{
	UMovieScene* MovieScene = GetMovieScene(Sequence);
	if (MovieScene)
	{
#if WITH_EDITORONLY_DATA
		return MovieScene->GetEditorData().WorkEnd;
#endif
	}
	return 0.f;
}

void UMovieSceneSequenceExtensions::SetEvaluationType(UMovieSceneSequence* Sequence, EMovieSceneEvaluationType InEvaluationType)
{
	UMovieScene* MovieScene = GetMovieScene(Sequence);
	if (MovieScene)
	{
		MovieScene->SetEvaluationType(InEvaluationType);
	}
}

EMovieSceneEvaluationType UMovieSceneSequenceExtensions::GetEvaluationType(UMovieSceneSequence* Sequence)
{
	UMovieScene* MovieScene = GetMovieScene(Sequence);
	if (MovieScene)
	{
		return MovieScene->GetEvaluationType();
	}

	return EMovieSceneEvaluationType::WithSubFrames;
}

void UMovieSceneSequenceExtensions::SetClockSource(UMovieSceneSequence* Sequence, EUpdateClockSource InClockSource)
{
	UMovieScene* MovieScene = GetMovieScene(Sequence);
	if (MovieScene)
	{
		MovieScene->SetClockSource(InClockSource);
	}
}

EUpdateClockSource UMovieSceneSequenceExtensions::GetClockSource(UMovieSceneSequence* Sequence)
{
	UMovieScene* MovieScene = GetMovieScene(Sequence);
	if (MovieScene)
	{
		return MovieScene->GetClockSource();
	}

	return EUpdateClockSource::Tick;
}

FTimecode UMovieSceneSequenceExtensions::GetTimecodeSource(UMovieSceneSequence* Sequence)
{
	UMovieScene* MovieScene = GetMovieScene(Sequence);
	if (MovieScene)
	{
#if WITH_EDITORONLY_DATA
		return MovieScene->TimecodeSource.Timecode;
#endif
		return FTimecode();
	}
	else
	{
		return FTimecode();
	}
}

FSequencerBindingProxy UMovieSceneSequenceExtensions::FindBindingByName(UMovieSceneSequence* Sequence, FString Name)
{
	UMovieScene* MovieScene = GetMovieScene(Sequence);
	if (MovieScene)
	{
		const FMovieSceneBinding* Binding = Algo::FindBy(MovieScene->GetBindings(), Name, &FMovieSceneBinding::GetName);
		if (Binding)
		{
			return FSequencerBindingProxy(Binding->GetObjectGuid(), Sequence);
		}
	}
	return FSequencerBindingProxy();
}

FSequencerBindingProxy UMovieSceneSequenceExtensions::FindBindingById(UMovieSceneSequence* Sequence, FGuid BindingId)
{
	UMovieScene* MovieScene = GetMovieScene(Sequence);
	if (MovieScene)
	{
		const FMovieSceneBinding* Binding = Algo::FindBy(MovieScene->GetBindings(), BindingId, &FMovieSceneBinding::GetObjectGuid);
		if (Binding)
		{
			return FSequencerBindingProxy(Binding->GetObjectGuid(), Sequence);
		}
	}
	return FSequencerBindingProxy();
}

TArray<FSequencerBindingProxy> UMovieSceneSequenceExtensions::GetBindings(UMovieSceneSequence* Sequence)
{
	TArray<FSequencerBindingProxy> AllBindings;

	UMovieScene* MovieScene = GetMovieScene(Sequence);
	if (MovieScene)
	{
		for (const FMovieSceneBinding& Binding : MovieScene->GetBindings())
		{
			AllBindings.Emplace(Binding.GetObjectGuid(), Sequence);
		}
	}

	return AllBindings;
}

TArray<FSequencerBindingProxy> UMovieSceneSequenceExtensions::GetSpawnables(UMovieSceneSequence* Sequence)
{
	TArray<FSequencerBindingProxy> AllSpawnables;

	UMovieScene* MovieScene = GetMovieScene(Sequence);
	if (MovieScene)
	{
		int32 Count = MovieScene->GetSpawnableCount();
		AllSpawnables.Reserve(Count);
		for (int32 i=0; i < Count; ++i)
		{
			AllSpawnables.Emplace(MovieScene->GetSpawnable(i).GetGuid(), Sequence);
		}
	}

	return AllSpawnables;
}

TArray<FSequencerBindingProxy> UMovieSceneSequenceExtensions::GetPossessables(UMovieSceneSequence* Sequence)
{
	TArray<FSequencerBindingProxy> AllPossessables;

	UMovieScene* MovieScene = GetMovieScene(Sequence);
	if (MovieScene)
	{
		int32 Count = MovieScene->GetPossessableCount();
		AllPossessables.Reserve(Count);
		for (int32 i = 0; i < Count; ++i)
		{
			AllPossessables.Emplace(MovieScene->GetPossessable(i).GetGuid(), Sequence);
		}
	}

	return AllPossessables;
}

FSequencerBindingProxy UMovieSceneSequenceExtensions::AddPossessable(UMovieSceneSequence* Sequence, UObject* ObjectToPossess)
{
	FGuid NewGuid = Sequence->CreatePossessable(ObjectToPossess);
	return FSequencerBindingProxy(NewGuid, Sequence);
}

FSequencerBindingProxy UMovieSceneSequenceExtensions::AddSpawnableFromInstance(UMovieSceneSequence* Sequence, UObject* ObjectToSpawn)
{
	FGuid NewGuid = Sequence->AllowsSpawnableObjects() ? Sequence->CreateSpawnable(ObjectToSpawn) : FGuid();
	return FSequencerBindingProxy(NewGuid, Sequence);
}

FSequencerBindingProxy UMovieSceneSequenceExtensions::AddSpawnableFromClass(UMovieSceneSequence* Sequence, UClass* ClassToSpawn)
{
	FGuid NewGuid = Sequence->AllowsSpawnableObjects() ? Sequence->CreateSpawnable(ClassToSpawn) : FGuid();
	return FSequencerBindingProxy(NewGuid, Sequence);
}

TArray<UObject*> UMovieSceneSequenceExtensions::LocateBoundObjects(UMovieSceneSequence* Sequence, const FSequencerBindingProxy& InBinding, UObject* Context)
{
	TArray<UObject*> Result;
	if (Sequence)
	{
		TArray<UObject*, TInlineAllocator<1>> OutObjects;
		Sequence->LocateBoundObjects(InBinding.BindingID, Context, OutObjects);
		Result.Append(OutObjects);
	}

	return Result;
}

FMovieSceneObjectBindingID UMovieSceneSequenceExtensions::MakeBindingID(UMovieSceneSequence* MasterSequence, const FSequencerBindingProxy& InBinding, EMovieSceneObjectBindingSpace Space)
{
	// This function was kinda flawed before - when ::Local was passed for the Space parameter,
	// and the sub sequence ID could not be found it would always fall back to a binding for ::Root without any Sequence ID
	FMovieSceneObjectBindingID BindingID = GetPortableBindingID(MasterSequence, MasterSequence, InBinding);
	if (Space == EMovieSceneObjectBindingSpace::Root)
	{
		BindingID.ReinterpretAsFixed();
	}
	return BindingID;
}

FMovieSceneObjectBindingID UMovieSceneSequenceExtensions::GetBindingID(const FSequencerBindingProxy& InBinding)
{
	return UE::MovieScene::FRelativeObjectBindingID(InBinding.BindingID);
}

FMovieSceneObjectBindingID UMovieSceneSequenceExtensions::GetPortableBindingID(UMovieSceneSequence* MasterSequence, UMovieSceneSequence* DestinationSequence, const FSequencerBindingProxy& InBinding)
{
	if (!MasterSequence || !DestinationSequence || !InBinding.Sequence)
	{
		FFrame::KismetExecutionMessage(TEXT("Invalid sequence sepcified."), ELogVerbosity::Error);
		return FMovieSceneObjectBindingID();
	}

	// If they are all the same sequence, we're dealing with a local binding - this requires no computation
	if (MasterSequence == DestinationSequence && MasterSequence == InBinding.Sequence)
	{
		return UE::MovieScene::FRelativeObjectBindingID(InBinding.BindingID);
	}

	// Destination is the destination for the BindingID to be serialized / resolved within
	// Target is the target sequence that contains the actual binding

	TOptional<FMovieSceneSequenceID> DestinationSequenceID;
	TOptional<FMovieSceneSequenceID> TargetSequenceID;

	if (MasterSequence == DestinationSequence)
	{
		DestinationSequenceID = MovieSceneSequenceID::Root;
	}
	if (MasterSequence == InBinding.Sequence)
	{
		TargetSequenceID = MovieSceneSequenceID::Root;
	}

	// We know that we have at least one sequence ID to find, otherwise we would have entered the ::Local branch above
	UMovieSceneCompiledDataManager*     CompiledDataManager = UMovieSceneCompiledDataManager::GetPrecompiledData();
	const FMovieSceneCompiledDataID     DataID              = CompiledDataManager->Compile(MasterSequence);
	const FMovieSceneSequenceHierarchy* Hierarchy           = CompiledDataManager->FindHierarchy(DataID);

	// If we have no hierarchy, the supplied MasterSequence does not have any sub-sequences so the callee has given us bogus parameters
	if (!Hierarchy)
	{
		FFrame::KismetExecutionMessage(*FString::Printf(TEXT("Master Sequence ('%s') does not have any sub-sequences."), *MasterSequence->GetPathName()), ELogVerbosity::Error);
		return FMovieSceneObjectBindingID();
	}

	// Find the destination and/or target sequence IDs as required.
	// This method is flawed if there is more than one instance of the sequence within the hierarchy
	// In this case we just pick the first one we find
	for (const TTuple<FMovieSceneSequenceID, FMovieSceneSubSequenceData>& Pair : Hierarchy->AllSubSequenceData())
	{
		if (UMovieSceneSequence* SubSequence = Pair.Value.GetSequence())
		{
			if (!TargetSequenceID.IsSet() && SubSequence == InBinding.Sequence)
			{
				TargetSequenceID = Pair.Key;
			}
			if (!DestinationSequenceID.IsSet() && SubSequence == DestinationSequence)
			{
				DestinationSequenceID = Pair.Key;
			}

			if (DestinationSequenceID.IsSet() && TargetSequenceID.IsSet())
			{
				break;
			}
		}
	}

	if (!DestinationSequenceID.IsSet())
	{
		FFrame::KismetExecutionMessage(*FString::Printf(TEXT("Unable to locate DestinationSequence ('%s') within Master Sequence hierarchy ('%s')."), *DestinationSequence->GetPathName(), *MasterSequence->GetPathName()), ELogVerbosity::Error);
		return FMovieSceneObjectBindingID();
	}

	if (!TargetSequenceID.IsSet())
	{
		FFrame::KismetExecutionMessage(*FString::Printf(TEXT("Unable to locate Sequence for InBinding ('%s') within Master Sequence hierarchy ('%s')."), *InBinding.Sequence->GetPathName(), *MasterSequence->GetPathName()), ELogVerbosity::Error);
		return FMovieSceneObjectBindingID();
	}

	return UE::MovieScene::FRelativeObjectBindingID(DestinationSequenceID.GetValue(), TargetSequenceID.GetValue(), InBinding.BindingID, Hierarchy);
}

FSequencerBindingProxy UMovieSceneSequenceExtensions::ResolveBindingID(UMovieSceneSequence* MasterSequence, FMovieSceneObjectBindingID InObjectBindingID)
{
	UMovieSceneSequence* Sequence = MasterSequence;

	FMovieSceneCompiledDataID DataID = UMovieSceneCompiledDataManager::GetPrecompiledData()->Compile(MasterSequence);

	const FMovieSceneSequenceHierarchy* Hierarchy = UMovieSceneCompiledDataManager::GetPrecompiledData()->FindHierarchy(DataID);
	if (Hierarchy)
	{
		if (UMovieSceneSequence* SubSequence = Hierarchy->FindSubSequence(InObjectBindingID.ResolveSequenceID(MovieSceneSequenceID::Root, Hierarchy)))
		{
			Sequence = SubSequence;
		}
	}

	return FSequencerBindingProxy(InObjectBindingID.GetGuid(), Sequence);
}

TArray<UMovieSceneFolder*> UMovieSceneSequenceExtensions::GetRootFoldersInSequence(UMovieSceneSequence* Sequence)
{
	TArray<UMovieSceneFolder*> Result;

#if WITH_EDITORONLY_DATA
	if (Sequence)
	{
		UMovieScene* Scene = Sequence->GetMovieScene();
		if (Scene)
		{
			Result = Scene->GetRootFolders();
		}
	}
#endif

	return Result;
}

UMovieSceneFolder* UMovieSceneSequenceExtensions::AddRootFolderToSequence(UMovieSceneSequence* Sequence, FString NewFolderName)
{
	UMovieSceneFolder* NewFolder = nullptr;
	
#if WITH_EDITORONLY_DATA
	if (Sequence)
	{
		UMovieScene* MovieScene = Sequence->GetMovieScene();
		if (MovieScene)
		{
			MovieScene->Modify();
			NewFolder = NewObject<UMovieSceneFolder>(MovieScene);
			NewFolder->SetFolderName(FName(*NewFolderName));
			MovieScene->GetRootFolders().Add(NewFolder);
		}
	}
#endif

	return NewFolder;
}

TArray<FMovieSceneMarkedFrame> UMovieSceneSequenceExtensions::GetMarkedFrames(UMovieSceneSequence* Sequence)
{
	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (MovieScene)
	{
		return MovieScene->GetMarkedFrames();
	}

	return TArray<FMovieSceneMarkedFrame>();
}

int32 UMovieSceneSequenceExtensions::AddMarkedFrame(UMovieSceneSequence* Sequence, const FMovieSceneMarkedFrame& InMarkedFrame)
{
	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (MovieScene)
	{
		MovieScene->Modify();

		return MovieScene->AddMarkedFrame(InMarkedFrame);
	}
	return INDEX_NONE;
}


void UMovieSceneSequenceExtensions::SetMarkedFrame(UMovieSceneSequence* Sequence, int32 InMarkIndex, FFrameNumber InFrameNumber)
{
	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (MovieScene)
	{
		return MovieScene->SetMarkedFrame(InMarkIndex, InFrameNumber);
	}
}


void UMovieSceneSequenceExtensions::DeleteMarkedFrame(UMovieSceneSequence* Sequence, int32 DeleteIndex)
{
	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (MovieScene)
	{
		MovieScene->Modify();

		MovieScene->DeleteMarkedFrame(DeleteIndex);
	}
}

void UMovieSceneSequenceExtensions::DeleteMarkedFrames(UMovieSceneSequence* Sequence)
{
	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (MovieScene)
	{
		MovieScene->Modify();

		MovieScene->DeleteMarkedFrames();
	}
}

void UMovieSceneSequenceExtensions::SortMarkedFrames(UMovieSceneSequence* Sequence)
{
	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (MovieScene)
	{
		return MovieScene->SortMarkedFrames();
	}
}

int32 UMovieSceneSequenceExtensions::FindMarkedFrameByLabel(UMovieSceneSequence* Sequence, const FString& InLabel)
{
	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (MovieScene)
	{
		return MovieScene->FindMarkedFrameByLabel(InLabel);
	}
	return INDEX_NONE;
}

int32 UMovieSceneSequenceExtensions::FindMarkedFrameByFrameNumber(UMovieSceneSequence* Sequence, FFrameNumber InFrameNumber)
{
	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (MovieScene)
	{
		return MovieScene->FindMarkedFrameByFrameNumber(InFrameNumber);
	}
	return INDEX_NONE;
}

int32 UMovieSceneSequenceExtensions::FindNextMarkedFrame(UMovieSceneSequence* Sequence, FFrameNumber InFrameNumber, bool bForward)
{
	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (MovieScene)
	{
		return MovieScene->FindNextMarkedFrame(InFrameNumber, bForward);
	}
	return INDEX_NONE;
}

void UMovieSceneSequenceExtensions::SetReadOnly(UMovieSceneSequence* Sequence, bool bInReadOnly)
{
#if WITH_EDITORONLY_DATA
	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (MovieScene)
	{
		MovieScene->SetReadOnly(bInReadOnly);
	}
#endif
}

bool UMovieSceneSequenceExtensions::IsReadOnly(UMovieSceneSequence* Sequence)
{
#if WITH_EDITORONLY_DATA
	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (MovieScene)
	{

		return MovieScene->IsReadOnly();
	}
#endif

	return false;
}
