// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sequencer/TakeRecorderDMXLibrarySource.h"

#include "Library/DMXLibrary.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Sequencer/MovieSceneDMXLibraryTrack.h"
#include "Sequencer/MovieSceneDMXLibrarySection.h"
#include "DMXSubsystem.h"

#include "LevelSequence.h"
#include "MovieSceneFolder.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Channels/MovieSceneChannelTraits.h"

#define LOCTEXT_NAMESPACE "TakeRecorderDMXLibrarySource"

UTakeRecorderDMXLibrarySource::UTakeRecorderDMXLibrarySource(const FObjectInitializer& ObjInit)
	:Super(ObjInit)
	, DMXLibrary(nullptr)
{
	bReduceKeys = false;

	// DMX Tracks are blue
	TrackTint = FColor(0, 125, 255, 65);
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

TArray<UTakeRecorderSource*> UTakeRecorderDMXLibrarySource::PreRecording(ULevelSequence* InSequence, ULevelSequence* InMasterSequence, FManifestSerializer* InManifestSerializer)
{
	TArray<UTakeRecorderSource*> ReturnValue;
	CachedDMXLibraryTrack = nullptr;

	if (DMXLibrary == nullptr || !DMXLibrary->IsValidLowLevelFast())
	{
		return ReturnValue;
	}

	if (FixturePatchRefs.Num() == 0)
	{
		return ReturnValue;
	}

	// Search for an existing DMX Library track for the selected library
	UMovieScene* MovieScene = InSequence->GetMovieScene();
	for (UMovieSceneTrack* MasterTrack : MovieScene->GetMasterTracks())
	{
		if (UMovieSceneDMXLibraryTrack* DMXLibraryTrack = Cast<UMovieSceneDMXLibraryTrack>(MasterTrack))
		{
			if (DMXLibraryTrack->GetDMXLibrary() == DMXLibrary)
			{
				CachedDMXLibraryTrack = DMXLibraryTrack;
				break;
			}
		}
	}

	// If no DMX track was found, create one
	if (!CachedDMXLibraryTrack.IsValid())
	{
		CachedDMXLibraryTrack = MovieScene->AddMasterTrack<UMovieSceneDMXLibraryTrack>();
		check(CachedDMXLibraryTrack.IsValid());

		CachedDMXLibraryTrack->SetDMXLibrary(DMXLibrary);

		// The DMX track needs a single, permanent section
		UMovieSceneSection* DMXSection = CachedDMXLibraryTrack->CreateNewSection();
		CachedDMXLibraryTrack->AddSection(*DMXSection);
	}

	check(CachedDMXLibraryTrack->GetAllSections().Num() > 0);
	UMovieSceneDMXLibrarySection* DMXSection = CastChecked<UMovieSceneDMXLibrarySection>(CachedDMXLibraryTrack->GetAllSections()[0]);

	// Erase existing animation in the track related to the Fixture Patches we're going to record.
	// This way, the user can record different Patches incrementally, one at a time.
	DMXSection->ForEachPatchFunctionChannels(
		[this](UDMXEntityFixturePatch* Patch, TArray<FDMXFixtureFunctionChannel>& FunctionChannels)
		{
			if (FixturePatchRefs.Contains(Patch))
			{
				for (FDMXFixtureFunctionChannel& FunctionChannel : FunctionChannels)
				{
					FunctionChannel.Channel.Reset();
				}
			}
		}
	);

	// Resize the section to either it's remaining keyframes range or 0
	DMXSection->SetRange(DMXSection->GetAutoSizeRange().Get(TRange<FFrameNumber>(0, 0)));
	// Make sure it starts at frame 0, in case Auto Size removed a piece of the start
	DMXSection->ExpandToFrame(0);

	// Cache patches already in the DMX track to not add them again
	TArray<UDMXEntityFixturePatch*>&& TrackPatches = DMXSection->GetFixturePatches();

	// Cache unique patches from the ones the user selected to make sure
	// we're not adding repeated ones
	TArray<UDMXEntityFixturePatch*> UniquePatches;
	UniquePatches.Reserve(FixturePatchRefs.Num());

	for (const FDMXEntityFixturePatchRef& PatchRef : FixturePatchRefs)
	{
		UDMXEntityFixturePatch* Patch = PatchRef.GetFixturePatch();

		if (Patch != nullptr && Patch->IsValidLowLevelFast() && !TrackPatches.Contains(Patch))
		{
			UniquePatches.AddUnique(Patch);
		}
	}
	
	// Add each new Patch to the Track
	for (UDMXEntityFixturePatch* Patch : UniquePatches)
	{
		DMXSection->AddFixturePatch(Patch);
	}

	// Mark the track as recording to prevent its evaluation from sending DMX data
	DMXSection->SetIsRecording(true);

	return ReturnValue;
}

void UTakeRecorderDMXLibrarySource::TickRecording(const FQualifiedFrameTime& CurrentTime)
{
	if (!CachedDMXLibraryTrack.IsValid())
	{
		return;
	}

	// Get the DMX Library Section from the track
	check(CachedDMXLibraryTrack->GetAllSections().Num() > 0);
	UMovieSceneDMXLibrarySection* DMXSection = CastChecked<UMovieSceneDMXLibrarySection>(CachedDMXLibraryTrack->GetAllSections()[0]);

	// Expand the section's duration to the current frame time
	const FFrameRate	TickResolution = CachedDMXLibraryTrack->GetTypedOuter<UMovieScene>()->GetTickResolution();
	const FFrameNumber	CurrentFrame = CurrentTime.ConvertTo(TickResolution).FloorToFrame();
	DMXSection->ExpandToFrame(CurrentFrame);

	// Used to get the function->value map from each Patch
	UDMXSubsystem* DMXSubsystem = GEngine->GetEngineSubsystem<UDMXSubsystem>();
	if (DMXSubsystem == nullptr || !DMXSubsystem->IsValidLowLevelFast())
	{
		return;
	}
	
	// Iterate over the Patches to record
	DMXSection->ForEachPatchFunctionChannels(
		[&](UDMXEntityFixturePatch* Patch, TArray<FDMXFixtureFunctionChannel>& FunctionChannels)
		{
			if (Patch == nullptr || !Patch->IsValidLowLevelFast())
			{
				return;
			}

			// We're only recording patches selected by the user
			if (!FixturePatchRefs.Contains(Patch))
			{
				return;
			}

			// We need the Controllers to decide what protocol to use
			TArray<UDMXEntityController*>&& Controllers = Patch->GetRelevantControllers();
			if (!Controllers.Num())
			{
				return;
			}

			// Get the Patch's functions values input from DMX protocol
			TMap<FDMXAttributeName, int32> FunctionsMap;
			DMXSubsystem->GetFunctionsMap(Patch, FunctionsMap);
			auto FunctionsIterator = FunctionsMap.CreateConstIterator();

			// Add the value keyframe to each Function channel
			for (FDMXFixtureFunctionChannel& FunctionChannel : FunctionChannels)
			{
				if (!FunctionsIterator)
				{
					break;
				}

				// int32 is for Blueprint compatibility only. We need to
				// convert the value using uint32 to make it positive (as it would be
				// reading directly from the DMX buffer) and then to float.
				const float KeyValue = (float)(uint32)(FunctionsIterator->Value);
				++FunctionsIterator;

				FunctionChannel.Channel.AddLinearKey(CurrentFrame, KeyValue);
			}
		});
}

TArray<UTakeRecorderSource*> UTakeRecorderDMXLibrarySource::PostRecording(class ULevelSequence* InSequence, ULevelSequence* InMasterSequence)
{
	TArray<UTakeRecorderSource*> ReturnValue;

	// Get the DMX Library Section from the track
	check(CachedDMXLibraryTrack->GetAllSections().Num() > 0);
	UMovieSceneDMXLibrarySection* DMXSection = CastChecked<UMovieSceneDMXLibrarySection>(CachedDMXLibraryTrack->GetAllSections()[0]);

	// Re-enables track evaluation to send DMX data
	DMXSection->SetIsRecording(false);

	// Do we need to optimize?
	if (!bReduceKeys || !CachedDMXLibraryTrack.IsValid())
	{
		return ReturnValue;
	}

	DMXSection->ForEachPatchFunctionChannels(
		[&](UDMXEntityFixturePatch* Patch, TArray<FDMXFixtureFunctionChannel>& FunctionChannels)
		{
			if (Patch == nullptr || !Patch->IsValidLowLevelFast())
			{
				return;
			}

			// Optimize only recorded tracks
			if (!FixturePatchRefs.Contains(Patch))
			{
				return;
			}

			// Optimize the keyframes for each function channel, deleting repeated values
			FKeyDataOptimizationParams Params;
			for (FDMXFixtureFunctionChannel& FunctionChannel : FunctionChannels)
			{
				UE::MovieScene::Optimize(&FunctionChannel.Channel, Params);
			}
		}
	);

	return ReturnValue;
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
