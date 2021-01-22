// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sequencer/MovieSceneDMXLibraryTrackRecorder.h"

#include "DMXEditorLog.h"
#include "DMXProtocolSettings.h"
#include "DMXStats.h"
#include "DMXSubsystem.h"
#include "DMXTypes.h"
#include "DMXUtils.h"
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
#include "Misc/ScopedSlowTask.h" 
#include "Modules/ModuleManager.h"
#include "MovieSceneFolder.h"


DECLARE_CYCLE_STAT(TEXT("Take recorder record sample"), STAT_DMXTakeRecorderRecordSample, STATGROUP_DMX);

////////////////////////////////////////////////
// Helpers to record a channel during RecordSampleImpl and only write it 
// when recording is finished. Used to opt for performance 4.26.

namespace
{
	struct FDMXSample
	{
		FFrameNumber Time;
		FMovieSceneFloatValue Value;
	};

	class FDMXChannelRecorder
	{
	public:
		FDMXChannelRecorder()
			: ChannelPtr(nullptr)
		{}

		FDMXChannelRecorder(FDMXFixtureFunctionChannel* InChannelPtr)
			: ChannelPtr(InChannelPtr)
		{}

		bool GetLastRecoredValue(float& OutValue) const
		{
			if (Samples.Num() > 0)
			{
				OutValue = LastRecordedValue;
				return true;
			}

			return false;
		}

		void Record(const FFrameNumber& Time, float Value)
		{
			LastRecordedValue = Value;

			FDMXSample Sample;

			Sample.Time = Time;

			FMovieSceneFloatValue MovieSceneFloatValue;
			MovieSceneFloatValue.InterpMode = ERichCurveInterpMode::RCIM_Linear;
			MovieSceneFloatValue.TangentMode = ERichCurveTangentMode::RCTM_None;
			MovieSceneFloatValue.Value = Value;

			Sample.Value = MovieSceneFloatValue;

			Samples.Add(Sample);
		}

		void WriteRecordingToChannel()
		{
			check(ChannelPtr);

			TArray<FFrameNumber> Times;
			TArray<FMovieSceneFloatValue> Values;

			for (const FDMXSample& Sample : Samples)
			{
				Times.Add(Sample.Time);
				Values.Add(Sample.Value);
			}

			ChannelPtr->Channel.AddKeys(Times, Values);
			ChannelPtr->Channel.AutoSetTangents();

			Samples.Reset();
		}

	private:
		float LastRecordedValue;

		FDMXFixtureFunctionChannel* ChannelPtr;
	
		TArray<FDMXSample> Samples;
	};

	class FDMXRecorder
		: public TSharedFromThis<FDMXRecorder>
	{
	public:
		bool GetLastValue(FDMXFixtureFunctionChannel* InChannelPtr, float& OutValue) const
		{
			const FDMXChannelRecorder* ChannelRecorderPtr = ChannelToChannelRecorderMap.Find(InChannelPtr);
			if (ChannelRecorderPtr)
			{
				return ChannelRecorderPtr->GetLastRecoredValue(OutValue);
			}

			return false;
		}

		void Record(FDMXFixtureFunctionChannel* ChannelPtr, const FFrameNumber& FrameNumber, float Value)
		{
			FDMXChannelRecorder& ChannelRecorder = ChannelToChannelRecorderMap.FindOrAdd(ChannelPtr, FDMXChannelRecorder(ChannelPtr));

			ChannelRecorder.Record(FrameNumber, Value);
		}

		void WriteAllChannels()
		{
			FScopedSlowTask WriteRecordedDMXDataTask(ChannelToChannelRecorderMap.Num(), NSLOCTEXT("DMXTakeRecorder", "WriteRecordedDMXData", "Write Recorded DMX data"));
			WriteRecordedDMXDataTask.MakeDialog(true, true);

			for (TTuple< FDMXFixtureFunctionChannel*, FDMXChannelRecorder>& ChannelToChannelRecorderKvp : ChannelToChannelRecorderMap)
			{
				if (WriteRecordedDMXDataTask.ShouldCancel())
				{
					break;
				}

				WriteRecordedDMXDataTask.EnterProgressFrame();

				ChannelToChannelRecorderKvp.Value.WriteRecordingToChannel();
			}

			ChannelToChannelRecorderMap.Reset();
		}

	private:
		TMap<FDMXFixtureFunctionChannel*, FDMXChannelRecorder> ChannelToChannelRecorderMap;
	};

	struct FDMXCellAttributeValues
	{
		FDMXCell Cell;

		TMap<FName, int32> AttributeNameToValueMap;
	};

	// 4.26 Optimized version to get all matrix cell values
	void GetAllMatrixCellValuesFast(UDMXEntityFixturePatch* FixturePatch, const TSharedPtr<FDMXSignal>& Signal, TArray<FDMXCellAttributeValues>& OutCellAttributeValues)
	{
		if (!FixturePatch ||
			!FixturePatch->ParentFixtureTypeTemplate ||
			!FixturePatch->ParentFixtureTypeTemplate->bFixtureMatrixEnabled)
		{
			return;
		}

		FDMXFixtureMatrix MatrixProperties;
		if (!FixturePatch->GetMatrixProperties(MatrixProperties))
		{
			return;
		}

		int32 XCells = MatrixProperties.XCells;
		int32 YCells = MatrixProperties.YCells;

		TArray<FDMXCell> AllCells;
		for (int32 YCell = 0; YCell < YCells; YCell++)
		{
			for (int32 XCell = 0; XCell < XCells; XCell++)
			{
				FDMXCell Cell;
				Cell.CellID = XCell + YCell * XCells;
				Cell.Coordinate = FIntPoint(XCell, YCell);

				AllCells.Add(Cell);
			}
		}

		TMap<const FDMXFixtureCellAttribute*, int32> AttributeToRelativeChannelOffsetMap;
		int32 CellDataSize = 0;
		int32 AttributeChannelOffset = 0;
		for (const FDMXFixtureCellAttribute& CellAttribute : MatrixProperties.CellAttributes)
		{
			AttributeToRelativeChannelOffsetMap.Add(&CellAttribute, AttributeChannelOffset);
			const int32 AttributeSize = UDMXEntityFixtureType::NumChannelsToOccupy(CellAttribute.DataType);

			CellDataSize += AttributeSize;
			AttributeChannelOffset += UDMXEntityFixtureType::NumChannelsToOccupy(CellAttribute.DataType);
		}

		OutCellAttributeValues.Reserve(XCells * YCells);
		for (int32 CellIndex = 0; CellIndex < AllCells.Num(); CellIndex++)
		{
			// Create a new cell attribute value struct
			FDMXCellAttributeValues CellAttributeValues;
			CellAttributeValues.Cell = AllCells[CellIndex];

			int32 StartingChannel = FixturePatch->GetStartingChannel() + MatrixProperties.FirstCellChannel - 1 + CellIndex * CellDataSize;
			for (const TTuple<const FDMXFixtureCellAttribute*, int32>& AttributeToRelativeChannelOffsetKvp : AttributeToRelativeChannelOffsetMap)
			{
				const int32 AttributeRelativeChannelOffset = AttributeToRelativeChannelOffsetKvp.Value;
 
				EDMXFixtureSignalFormat SignalFormat = AttributeToRelativeChannelOffsetKvp.Key->DataType;
				const bool bUseLSBMode = AttributeToRelativeChannelOffsetKvp.Key->bUseLSBMode;
				const int32 AbsoluteStartingChannelIndex = StartingChannel + AttributeRelativeChannelOffset - 1;

				const int32 AttributeValue = UDMXEntityFixtureType::BytesToInt(SignalFormat, bUseLSBMode, &Signal->ChannelData[AbsoluteStartingChannelIndex]);

				const FName AttributeName = AttributeToRelativeChannelOffsetKvp.Key->Attribute.Name;
				CellAttributeValues.AttributeNameToValueMap.Add(AttributeName, AttributeValue);
			}

			OutCellAttributeValues.Add(CellAttributeValues);
		}
	}

	/** DMX recorders currently active */
	TMap<UMovieSceneDMXLibraryTrackRecorder*, TSharedPtr<FDMXRecorder>> DMXRecorders;
}


////////////////////////////////////////////////
// UMovieSceneDMXLibraryTrackRecorder

TWeakObjectPtr<UMovieSceneDMXLibraryTrack> UMovieSceneDMXLibraryTrackRecorder::CreateTrack(UMovieScene* InMovieScene, UDMXLibrary* Library, const TArray<FDMXEntityFixturePatchRef>& InFixturePatchRefs, bool bInAlwaysUseTimecode, bool bInDiscardSamplesBeforeStart, UMovieSceneTrackRecorderSettings* InSettingsObject)
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

	if (!DMXLibraryTrack.IsValid())
	{
		DMXLibraryTrack = MovieScene->AddMasterTrack<UMovieSceneDMXLibraryTrack>();
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
		DMXLibrarySection->AddFixturePatch(PatchRef.GetFixturePatch());
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

	// Mark the track as recording. Also prevents its evaluation from sending DMX data
	DMXLibrarySection->SetIsRecording(true);

	return DMXLibraryTrack;
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
	TSharedPtr<FDMXRecorder>* MyRecorderPtr = DMXRecorders.Find(this);
	if (MyRecorderPtr)
	{
		TSharedPtr<FDMXRecorder>& MyRecorder = *MyRecorderPtr;
		MyRecorder->WriteAllChannels();
	}

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
	SCOPE_CYCLE_COUNTER(STAT_DMXTakeRecorderRecordSample);

	if (Buffer.Num() == 0)
	{
		return;
	}

	TSharedPtr<FDMXRecorder>* MyRecorderPtr = DMXRecorders.Find(this);
	if (!MyRecorderPtr)
	{
		DMXRecorders.Add(this, MakeShared<FDMXRecorder>());
	}

	TSharedPtr<FDMXRecorder>& MyRecorder = DMXRecorders.FindChecked(this);

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

			TArray<FDMXCellAttributeValues> CellAttributeValues;
			GetAllMatrixCellValuesFast(Patch, Signal, CellAttributeValues);

			for (FDMXFixtureFunctionChannel& Channel : FunctionChannels)
			{
				if (Channel.IsCellFunction())
				{
					const FDMXCellAttributeValues* CellAttributeValuesPtr = CellAttributeValues.FindByPredicate([&Channel](const FDMXCellAttributeValues& CellAttributeValues) {
						return
							CellAttributeValues.Cell.Coordinate.X == Channel.CellCoordinate.X &&
							CellAttributeValues.Cell.Coordinate.Y == Channel.CellCoordinate.Y;
						});
					
					if (CellAttributeValuesPtr)
					{
						const int32* ValuePtr = CellAttributeValuesPtr->AttributeNameToValueMap.Find(Channel.AttributeName);
						
						if (ValuePtr)
						{
							float PreviousValue;
							if (MyRecorder->GetLastValue(&Channel, PreviousValue))
							{
								if (PreviousValue == *ValuePtr)
								{
									// Don't record unchanged values
									continue;
								}
								else
								{
									MyRecorder->Record(&Channel, SignalFrame - 1, PreviousValue);
								}
							}

							MyRecorder->Record(&Channel, SignalFrame - 1, *ValuePtr);
						}
					}
				}
				else
				{
					TMap<FDMXAttributeName, int32> AttributeValueMap;
					Patch->GetAttributesValues(AttributeValueMap);

					const int32* ValuePtr = AttributeValueMap.Find(Channel.AttributeName);

					if (ValuePtr)
					{
						float PreviousValue;
						if (MyRecorder->GetLastValue(&Channel, PreviousValue))
						{
							if (PreviousValue == *ValuePtr)
							{
								// Don't record unchanged values
								continue;
							}
							else
							{
								MyRecorder->Record(&Channel, SignalFrame - 1, PreviousValue);
							}
						}

						MyRecorder->Record(&Channel, SignalFrame - 1, *ValuePtr);
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
