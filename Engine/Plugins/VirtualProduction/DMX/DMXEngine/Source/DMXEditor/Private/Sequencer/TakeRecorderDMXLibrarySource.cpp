// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sequencer/TakeRecorderDMXLibrarySource.h"

#include "DMXEditorLog.h"
#include "Library/DMXLibrary.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Sequencer/MovieSceneDMXLibrarySection.h"
#include "Sequencer/MovieSceneDMXLibraryTrack.h"
#include "Sequencer/MovieSceneDMXLibraryTrackRecorder.h"

#include "LevelSequence.h"
#include "MovieSceneFolder.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Channels/MovieSceneChannelTraits.h"

#define LOCTEXT_NAMESPACE "TakeRecorderDMXLibrarySource"

UTakeRecorderDMXLibrarySource::UTakeRecorderDMXLibrarySource(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
	, DMXLibrary(nullptr)
	, bReduceKeys(false)
	, bUseSourceTimecode(true)
	, bDiscardSamplesBeforeStart(true)
{
	// DMX Tracks are blue
	TrackTint = FColor(0.0f, 125.0f, 255.0f, 65.0f);
}

void UTakeRecorderDMXLibrarySource::AddAllPatches()
{
	if (DMXLibrary == nullptr || !DMXLibrary->IsValidLowLevelFast())
	{
		return;
	}

	// Remove all PatchRefs to copy the ones from the library. This way we don't have to
	// use AddUnique on each one to avoid repeated Patches.
	FixturePatchRefs.Empty(FixturePatchRefs.Max()); // .Max() to not change allocated memory

	DMXLibrary->ForEachEntityOfType<UDMXEntityFixturePatch>([this](UDMXEntityFixturePatch* Patch)
		{
			FixturePatchRefs.Emplace(Patch);
		});
}

void UTakeRecorderDMXLibrarySource::OnEntitiesUpdated(UDMXLibrary* UpdatedLibrary)
{
	check(DMXLibrary);
	check(UpdatedLibrary == DMXLibrary);

	if (TrackRecorder)
	{
		DMXLibrary->ForEachEntityOfType<UDMXEntityFixturePatch>([this](UDMXEntityFixturePatch* Patch)
			{
				if (!FixturePatchRefs.Contains(Patch))
				{
					UE_LOG(LogDMXEditor, Error, TEXT("DMXLibrary %s edited while recording. Recording stopped."), *DMXLibrary->GetName());
					TrackRecorder->StopRecording();
					TrackRecorder->RefreshTracks();
					return;
				};
			});
	}
}

TArray<UTakeRecorderSource*> UTakeRecorderDMXLibrarySource::PreRecording(ULevelSequence* InSequence, ULevelSequence* InMasterSequence, FManifestSerializer* InManifestSerializer)
{
	if (DMXLibrary)
	{
		UMovieScene* MovieScene = InSequence->GetMovieScene();
		TrackRecorder = NewObject<UMovieSceneDMXLibraryTrackRecorder>();
		TrackRecorder->CreateTrack(MovieScene, DMXLibrary, FixturePatchRefs, bUseSourceTimecode, bDiscardSamplesBeforeStart, nullptr);
	}
	else
	{
		UE_LOG(LogDMXProtocol, Error, TEXT("No library specified for DMX Track Recorder."));
	}

	return TArray<UTakeRecorderSource*>();
}

void UTakeRecorderDMXLibrarySource::TickRecording(const FQualifiedFrameTime& CurrentTime)
{
	if (TrackRecorder)
	{
		TrackRecorder->RecordSample(CurrentTime);
	}
}

void UTakeRecorderDMXLibrarySource::StartRecording(const FTimecode& InSectionStartTimecode, const FFrameNumber& InSectionFirstFrame, class ULevelSequence* InSequence)
{
	if (TrackRecorder)
	{
		TrackRecorder->SetReduceKeys(bReduceKeys);
		TrackRecorder->SetSectionStartTimecode(InSectionStartTimecode, InSectionFirstFrame);
	}
}

void UTakeRecorderDMXLibrarySource::StopRecording(class ULevelSequence* InSequence)
{
	if (TrackRecorder)
	{
		TrackRecorder->StopRecording();
	}
}

TArray<UTakeRecorderSource*> UTakeRecorderDMXLibrarySource::PostRecording(class ULevelSequence* InSequence, ULevelSequence* InMasterSequence)
{
	if (TrackRecorder)
	{
		TrackRecorder->FinalizeTrack();
	}

	TrackRecorder = nullptr;
	return TArray<UTakeRecorderSource*>();
}

void UTakeRecorderDMXLibrarySource::AddContentsToFolder(UMovieSceneFolder* InFolder)
{
	if (CachedDMXLibraryTrack.IsValid())
	{
		InFolder->AddChildMasterTrack(CachedDMXLibraryTrack.Get());
	}
}

FText UTakeRecorderDMXLibrarySource::GetDisplayTextImpl() const
{
	if (DMXLibrary != nullptr && DMXLibrary->IsValidLowLevelFast())
	{
		return FText::FromString(DMXLibrary->GetName());
	}

	return LOCTEXT("Display Text", "Null DMX Library");
}

void UTakeRecorderDMXLibrarySource::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.GetPropertyName();
	
	if ((PropertyName == GET_MEMBER_NAME_CHECKED(UTakeRecorderDMXLibrarySource, FixturePatchRefs)
		&& PropertyChangedEvent.ChangeType == EPropertyChangeType::ArrayAdd)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UTakeRecorderDMXLibrarySource, AddAllPatchesDummy))
	{
		// Fix DMX Library reference on new Patch refs
		ResetPatchesLibrary();
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UTakeRecorderDMXLibrarySource, DMXLibrary))
	{
		// If the library has changed, delete all Patch refs. They won't be accessible
		// anymore since they were children of a different DMX Library
		if (FixturePatchRefs.Num() > 0)
		{
			if (FixturePatchRefs[0].DMXLibrary != DMXLibrary)
			{
				FixturePatchRefs.Empty();
			}
		}
	}
}

void UTakeRecorderDMXLibrarySource::PostLoad()
{
	Super::PostLoad();

	// Make sure the Refs don't display the DMX Library picker.
	ResetPatchesLibrary();
}

void UTakeRecorderDMXLibrarySource::ResetPatchesLibrary()
{
	for (FDMXEntityFixturePatchRef& PatchRef : FixturePatchRefs)
	{
		PatchRef.bDisplayLibraryPicker = false;
		PatchRef.DMXLibrary = DMXLibrary;
	}
}

#undef LOCTEXT_NAMESPACE
