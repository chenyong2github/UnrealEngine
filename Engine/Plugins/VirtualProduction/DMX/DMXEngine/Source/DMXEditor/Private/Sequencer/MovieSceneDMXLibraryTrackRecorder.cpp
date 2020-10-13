// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sequencer/MovieSceneDMXLibraryTrackRecorder.h"

#include "DMXEditorLog.h"
#include "DMXProtocolSettings.h"
#include "DMXSubsystem.h"
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

	// Listen to DMX
	for (UDMXEntityController* Controller : Library->GetEntitiesTypeCast<UDMXEntityController>())
	{
		IDMXProtocolPtr Protocol = IDMXProtocol::Get(Controller->GetProtocol());
		Protocol->GetOnUniverseInputBufferUpdated().AddUObject(this, &UMovieSceneDMXLibraryTrackRecorder::OnReceiveDMX);
	}

	//check(DMXLibraryTrack->GetAllSections().Num() > 0);
	//UMovieSceneDMXLibrarySection* DMXSection = CastChecked<UMovieSceneDMXLibrarySection>(CachedDMXLibraryTrack->GetAllSections()[0]);

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

	// Mark the track as recording to prevent its evaluation from sending DMX data
	DMXLibrarySection->SetIsRecording(true);
}

void UMovieSceneDMXLibraryTrackRecorder::SetSectionStartTimecodeImpl(const FTimecode& InSectionStartTimecode, const FFrameNumber& InSectionFirstFrame)
{
	float Time = 0.0f;
	// SecondsDiff = FPlatformTime::Seconds() - Time;

	if (DMXLibrarySection.IsValid())
	{
		DMXLibrarySection->TimecodeSource = FMovieSceneTimecodeSource(InSectionStartTimecode);

		FTakeRecorderParameters Parameters;
		Parameters.User = GetDefault<UTakeRecorderUserSettings>()->Settings;
		Parameters.Project = GetDefault<UTakeRecorderProjectSettings>()->Settings;

		FFrameRate TickResolution = MovieScene->GetTickResolution();
		FFrameRate DisplayRate = MovieScene->GetDisplayRate();

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
	FFrameRate   TickResolution = DMXLibrarySection->GetTypedOuter<UMovieScene>()->GetTickResolution();
	FFrameNumber FrameTime = CurrentFrameTime.ConvertTo(TickResolution).FloorToFrame();

	double DeltaSeconds = static_cast<double>(CurrentFrameTime.Rate.Denominator) / static_cast<double>(CurrentFrameTime.Rate.Numerator / 1000.0f);
	
	// Accept to lag two frames behind, but no more
	double NextFrameTimeSeconds = FPlatformTime::Seconds() + DeltaSeconds * 2.0f;

	TSharedPtr<FDMXTrackRecorderSample, ESPMode::ThreadSafe> Sample;

	while (Buffer.Dequeue(Sample))
	{
		double CurrentTimeSeconds = FPlatformTime::Seconds();

		// Drop frames when running off sync.
		if (CurrentTimeSeconds > NextFrameTimeSeconds)
		{
			UE_LOG(LogDMXEditor, Warning, TEXT("Buffer overflow in DMX Track recorder, dropping frame %i"), CurrentFrameTime.Time.GetFrame().Value);
			Buffer.Empty();
			break;
		}

		FFrameNumber KeyTime = Sample->FrameTime.ConvertTo(TickResolution).FloorToFrame();
		DMXLibrarySection->ExpandToFrame(KeyTime);

		DMXLibrarySection->ForEachPatchFunctionChannels(
			[&](UDMXEntityFixturePatch* Patch, TArray<FDMXFixtureFunctionChannel>& FunctionChannels)
			{
				if (Patch->UniverseID != Sample->UniverseID)
				{
					return;
				}

				UDMXEntityFixtureType* ParentFixtureType = Patch->ParentFixtureTypeTemplate;

				const FDMXFixtureMode& Mode = ParentFixtureType->Modes[Patch->ActiveMode];
				const int32 PatchStartingChannel = Patch->GetStartingChannel() - 1;

				for (FDMXFixtureFunctionChannel& Channel : FunctionChannels)
				{
					if(Channel.IsCellFunction())
					{
						for (const FDMXFixtureCellAttribute& CellAttribute : Mode.FixtureMatrixConfig.CellAttributes)
						{
							UDMXSubsystem* DMXSubsystem = UDMXSubsystem::GetDMXSubsystem_Pure();
							check(DMXSubsystem);

							TMap<FDMXAttributeName, int32> AttributeNameValueMap;
							DMXSubsystem->GetMatrixCellValue(Patch, Channel.CellCoordinate, AttributeNameValueMap);

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
										Channel.Channel.AddLinearKey(KeyTime - 1, static_cast<float>(Channel.Channel.GetValues().Last().Value));
									}
								}

								Channel.Channel.AddLinearKey(KeyTime, static_cast<float>(*ValuePtr));
							}
						}
					}
					else
					{
						for (const FDMXFixtureFunction& Function : Mode.Functions)
						{
							if (Function.Attribute != Channel.AttributeName)
							{
								continue;
							}

							if (Function.Channel > DMX_MAX_ADDRESS)
							{
								UE_LOG(LogDMXEditor, Error, TEXT("%s: Function Channel %d is higher than %d"), __FUNCTION__, Function.Channel, DMX_MAX_ADDRESS);
								return;
							}

							if (!UDMXEntityFixtureType::IsFunctionInModeRange(Function, Mode, PatchStartingChannel))
							{
								// We reached the functions outside the valid channels for this mode
								break;
							}

							const int32 FunctionStartIndex = Function.Channel - 1 + PatchStartingChannel;
							const int32 FunctionLastIndex = FunctionStartIndex + UDMXEntityFixtureType::NumChannelsToOccupy(Function.DataType) - 1;
							check(FunctionLastIndex < Sample->Data.Num());

							const uint32 ChannelValue = UDMXEntityFixtureType::BytesToFunctionValue(Function, Sample->Data.GetData() + FunctionStartIndex);
							if (Channel.Channel.GetValues().Num() > 0)
							{
								if (Channel.Channel.GetValues().Last().Value == static_cast<float>(ChannelValue))
								{
									// Don't record unchanged values
									continue;
								}
								else
								{
									// Set the previous value one frame ahead to avoid blending between keys.
									Channel.Channel.AddLinearKey(KeyTime - 1, static_cast<float>(Channel.Channel.GetValues().Last().Value));
								}
							}

							Channel.Channel.AddLinearKey(KeyTime, static_cast<float>(ChannelValue));
						}
					}
				}
		});
	}
}

void UMovieSceneDMXLibraryTrackRecorder::OnReceiveDMX(FName ProtocolName, uint16 UniverseID, const TArray<uint8>& InputBuffer)
{
	if (!DMXLibrarySection->GetIsRecording())
	{
		return;
	}

	// Don't record when there's no changes in the buffer
	TArray<uint8>* PreviousBufferPtr = PreviousSamplesPerUniverse.Find(UniverseID);
	if (PreviousBufferPtr && *PreviousBufferPtr == InputBuffer)
	{
		return;
	}

	TSharedPtr<FDMXTrackRecorderSample, ESPMode::ThreadSafe> Sample = MakeShared<FDMXTrackRecorderSample, ESPMode::ThreadSafe>();

	Sample->UniverseID = UniverseID;
	Sample->Data = InputBuffer;

	GetFrameTimeThreadSafe(Sample->FrameTime);

	Buffer.Enqueue(Sample);

	if (PreviousBufferPtr)
	{
		PreviousSamplesPerUniverse[UniverseID] = InputBuffer;
	}
	else
	{
		PreviousSamplesPerUniverse.Add(UniverseID, InputBuffer);
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

void UMovieSceneDMXLibraryTrackRecorder::GetFrameTimeThreadSafe(FQualifiedFrameTime& OutFrameTime)
{
	FScopeLock Lock(&FrameTimeCritSec);

	OutFrameTime = FQualifiedFrameTime(FApp::GetTimecode(), FApp::GetTimecodeFrameRate());
}
