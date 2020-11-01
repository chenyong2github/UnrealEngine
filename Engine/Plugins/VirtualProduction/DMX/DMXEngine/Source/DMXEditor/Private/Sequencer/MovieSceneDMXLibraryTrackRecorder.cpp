// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sequencer/MovieSceneDMXLibraryTrackRecorder.h"

#include "DMXEditorLog.h"
#include "DMXProtocolSettings.h"
#include "DMXSubsystem.h"
#include "DMXTypes.h"
#include "Interfaces/IDMXProtocol.h"
#include "Library/DMXLibrary.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Library/DMXEntityFixtureType.h"
#include "Sequencer/MovieSceneDMXLibrarySection.h"
#include "Sequencer/MovieSceneDMXLibraryTrack.h"

#include "TakeRecorderSettings.h"
#include "Recorder/TakeRecorderParameters.h"
#include "Engine/Engine.h"
#include "Engine/TimecodeProvider.h"
#include "Features/IModularFeatures.h"
#include "Misc/App.h"
#include "Modules/ModuleManager.h"
#include "MovieSceneFolder.h"


void UMovieSceneDMXLibraryTrackRecorder::CreateTrack(UMovieScene* InMovieScene, UDMXLibrary* Library, const TArray<FDMXEntityFixturePatchRef>& InFixturePatchRefs, bool bInAlwaysUseTimecode, bool bInDiscardSamplesBeforeStart, UMovieSceneTrackRecorderSettings* InSettingsObject)
{
	check(Library);

	FixturePatchRefs = InFixturePatchRefs;

	// Only keep alive fixture patch refs
	FixturePatchRefs.RemoveAll([](const FDMXEntityFixturePatchRef& Ref) {
		UDMXEntityFixturePatch* FixturePatch = Ref.GetFixturePatch();
		return
			!FixturePatch ||
			!FixturePatch->IsValidLowLevel() ||
			!FixturePatch->ParentFixtureTypeTemplate ||
			!FixturePatch->ParentFixtureTypeTemplate->IsValidLowLevel();
		});

	MovieScene = InMovieScene;
	bUseSourceTimecode = bInAlwaysUseTimecode;
	// TODO? bDiscardSamplesBeforeStart = bInDiscardSamplesBeforeStart;

	DMXLibraryTrack = nullptr;
	DMXLibrarySection.Reset();

	if (MovieScene->GetMasterTracks().Num() == 0)
	{
		DMXLibraryTrack = MovieScene->AddMasterTrack<UMovieSceneDMXLibraryTrack>();
	}
	else
	{
		DMXLibraryTrack->RemoveAllAnimationData();
	}

	DMXLibraryTrack->SetDMXLibrary(Library);

	DMXLibrarySection = Cast<UMovieSceneDMXLibrarySection>(DMXLibraryTrack->CreateNewSection());
	if (DMXLibrarySection != nullptr)
	{
		DMXLibrarySection->SetIsActive(false);
		DMXLibraryTrack->AddSection(*DMXLibrarySection);
	}

	// Add each new Patch to the Track
	for (const FDMXEntityFixturePatchRef& PatchRef : FixturePatchRefs)
	{
		UDMXEntityFixturePatch* Patch = PatchRef.GetFixturePatch();

		if (Patch != nullptr && Patch->IsValidLowLevelFast())
		{
			DMXLibrarySection->AddFixturePatch(Patch);
		}
	}

	// Erase existing animation in the track related to the Fixture Patches we're going to record.
	// This way, the user can record different Patches incrementally, one at a time.
	DMXLibrarySection->ForEachPatchFunctionChannels(
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
	DMXLibrarySection->SetRange(DMXLibrarySection->GetAutoSizeRange().Get(TRange<FFrameNumber>(0, 0)));
	// Make sure it starts at frame 0, in case Auto Size removed a piece of the start
	DMXLibrarySection->ExpandToFrame(0);

	// Cache patches already in the DMX track to not add them again
	TArray<UDMXEntityFixturePatch*> TrackPatches = DMXLibrarySection->GetFixturePatches();

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
		DMXLibrarySection->AddFixturePatch(Patch);
	}

	// Mark the track as recording. Also prevents its evaluation from sending DMX data
	DMXLibrarySection->SetIsRecording(true);
}

void UMovieSceneDMXLibraryTrackRecorder::SetSectionStartTimecodeImpl(const FTimecode& InSectionStartTimecode, const FFrameNumber& InSectionFirstFrame)
{
	if (DMXLibrarySection.IsValid())
	{
		// Start listen to the patches
		DMXLibrarySection->ForEachPatchFunctionChannels(
			[&](UDMXEntityFixturePatch* Patch, TArray<FDMXFixtureFunctionChannel>& FunctionChannels)
			{
				Patch->OnFixturePatchReceivedDMX.RemoveAll(this);
				Patch->OnFixturePatchReceivedDMX.AddUObject(this, &UMovieSceneDMXLibraryTrackRecorder::OnReceiveDMX);
			});
		DMXLibrarySection->ForEachPatchFunctionChannels(
			[&](UDMXEntityFixturePatch* Patch, TArray<FDMXFixtureFunctionChannel>& FunctionChannels)
			{
				Patch->SetTickInEditor(true);
			});	

		DMXLibrarySection->TimecodeSource = FMovieSceneTimecodeSource(InSectionStartTimecode);

		FTakeRecorderParameters Parameters;
		Parameters.User = GetDefault<UTakeRecorderUserSettings>()->Settings;
		Parameters.Project = GetDefault<UTakeRecorderProjectSettings>()->Settings;

		FFrameRate TickResolution = MovieScene->GetTickResolution();
		FFrameRate DisplayRate = MovieScene->GetDisplayRate();

		RecordStartTime = FApp::GetCurrentTime();
		RecordStartFrame = Parameters.Project.bStartAtCurrentTimecode ? FFrameRate::TransformTime(FFrameTime(InSectionStartTimecode.ToFrameNumber(DisplayRate)), DisplayRate, TickResolution).FloorToFrame() : MovieScene->GetPlaybackRange().GetLowerBoundValue();
	}
}

UMovieSceneSection* UMovieSceneDMXLibraryTrackRecorder::GetMovieSceneSection() const
{
	return Cast<UMovieSceneSection>(DMXLibrarySection.Get());
}

void UMovieSceneDMXLibraryTrackRecorder::StopRecordingImpl()
{
	DMXLibrarySection->SetIsRecording(false);

	// Stop listen to the patches
	DMXLibrarySection->ForEachPatchFunctionChannels(
		[&](UDMXEntityFixturePatch* Patch, TArray<FDMXFixtureFunctionChannel>& FunctionChannels)
		{
			Patch->OnFixturePatchReceivedDMX.RemoveAll(this);
			Patch->OnFixturePatchReceivedDMX.AddUObject(this, &UMovieSceneDMXLibraryTrackRecorder::OnReceiveDMX);
		});
	DMXLibrarySection->ForEachPatchFunctionChannels(
		[&](UDMXEntityFixturePatch* Patch, TArray<FDMXFixtureFunctionChannel>& FunctionChannels)
		{
			Patch->SetTickInEditor(false);
		});
}

void UMovieSceneDMXLibraryTrackRecorder::FinalizeTrackImpl()
{
	if (DMXLibrarySection.IsValid())
	{
		FKeyDataOptimizationParams Params;
		Params.bAutoSetInterpolation = false;

		TOptional<TRange<FFrameNumber> > DefaultSectionLength = DMXLibrarySection->GetAutoSizeRange();
		if (DefaultSectionLength.IsSet())
		{
			DMXLibrarySection->SetRange(DefaultSectionLength.GetValue());
		}

		DMXLibrarySection->SetIsActive(true);
	}
}

void UMovieSceneDMXLibraryTrackRecorder::RecordSampleImpl(const FQualifiedFrameTime& CurrentFrameTime)
{
	if (Buffer.Num() == 0)
	{
		return;
	}

	TMap<FDMXAttributeName, int32> AttributeValueMap;
	DMXLibrarySection->ForEachPatchFunctionChannels([&](UDMXEntityFixturePatch* Patch, TArray<FDMXFixtureFunctionChannel>& FunctionChannels)
	{
		const TSharedPtr<FDMXSignal>* SignalPtr = Buffer.Find(Patch);

		if (SignalPtr)
		{
			const TSharedPtr<FDMXSignal>& Signal = *SignalPtr;
		
			FTakeRecorderParameters Parameters;
			Parameters.User = GetDefault<UTakeRecorderUserSettings>()->Settings;
			Parameters.Project = GetDefault<UTakeRecorderProjectSettings>()->Settings;

			FFrameRate TickResolution = MovieScene->GetTickResolution();
			FFrameRate DisplayRate = MovieScene->GetDisplayRate();

			const FFrameNumber SignalFrame = [this, &Parameters, &CurrentFrameTime, &TickResolution, &Signal]() -> FFrameNumber
			{
				if (Parameters.Project.bStartAtCurrentTimecode)
				{
					return ((Signal->Timestamp - RecordStartTime) * TickResolution).FloorToFrame() + RecordStartFrame;
				}
				else
				{
					return ((Signal->Timestamp - RecordStartTime) * TickResolution).FloorToFrame();
				}
			}();

			DMXLibrarySection->ExpandToFrame(SignalFrame);

			AttributeValueMap.Reset();
			for (FDMXFixtureFunctionChannel& Channel : FunctionChannels)
			{
				if (Channel.IsCellFunction())
				{
					FDMXFixtureMatrix MatrixProperties;
					if (!Patch->GetMatrixProperties(MatrixProperties))
					{
						continue;
					}

					for (const FDMXFixtureCellAttribute& CellAttribute : MatrixProperties.CellAttributes)
					{
						UDMXSubsystem* DMXSubsystem = UDMXSubsystem::GetDMXSubsystem_Pure();
						check(DMXSubsystem);

						TMap<FDMXAttributeName, int32> AttributeNameValueMap;
						Patch->GetMatrixCellValues(Channel.CellCoordinate, AttributeNameValueMap);

						int32* ValuePtr = AttributeNameValueMap.Find(Channel.AttributeName);
						if (ValuePtr)
						{
							if (Channel.Channel.GetValues().Num() > 0)
							{
								if (Channel.Channel.GetValues().Last().Value == static_cast<float>(*ValuePtr))
								{
									// Don't record unchanged values
									continue;
								}
								else
								{
									// Set the previous value one frame ahead to avoid blending between keys.
									Channel.Channel.AddLinearKey(SignalFrame - 1, static_cast<float>(Channel.Channel.GetValues().Last().Value));
								}
							}

							Channel.Channel.AddLinearKey(SignalFrame, static_cast<float>(*ValuePtr));
						}
					}
				}
				else
				{
					AttributeValueMap.Reset();
					Patch->GetAttributesValues(AttributeValueMap);

					const int32* ValuePtr = AttributeValueMap.Find(Channel.AttributeName);

					if (ValuePtr)
					{
						if (Channel.Channel.GetValues().Num() > 0)
						{
							if (Channel.Channel.GetValues().Last().Value == static_cast<float>(*ValuePtr))
							{
								// Don't record unchanged values
								continue;
							}
							else
							{
								// Set the previous value one frame ahead to avoid blending between keys.
								Channel.Channel.AddLinearKey(SignalFrame - 1, static_cast<float>(Channel.Channel.GetValues().Last().Value));
							}
						}

						Channel.Channel.AddLinearKey(SignalFrame, static_cast<float>(*ValuePtr));
					}
				}
			}
		}
	});

	Buffer.Reset();
}

void UMovieSceneDMXLibraryTrackRecorder::OnReceiveDMX(UDMXEntityFixturePatch* FixturePatch, const FDMXNormalizedAttributeValueMap& NormalizedValuePerAttribute)
{
	const TSharedPtr<FDMXSignal>& Signal = FixturePatch->GetLastReceivedDMXSignal();

	if (Signal.IsValid())
	{
		Buffer.FindOrAdd(FixturePatch) = Signal;
	}
}

void UMovieSceneDMXLibraryTrackRecorder::AddContentsToFolder(UMovieSceneFolder* InFolder)
{
	if (DMXLibraryTrack.IsValid())
	{
		InFolder->AddChildMasterTrack(DMXLibraryTrack.Get());
	}
}

void UMovieSceneDMXLibraryTrackRecorder::RefreshTracks()
{
	if (DMXLibrarySection.IsValid())
	{
		DMXLibrarySection->RefreshChannels();
	}
}

bool UMovieSceneDMXLibraryTrackRecorder::LoadRecordedFile(const FString& FileName, UMovieScene *InMovieScene, TMap<FGuid, AActor*>& ActorGuidToActorMap, TFunction<void()> InCompletionCallback)
{
	UE_LOG_DMXEDITOR(Warning, TEXT("Loading recorded file for DMX Library tracks is not supported."));
	return false;
}
