// Copyright Epic Games, Inc. All Rights Reserved.

#include "MoviePipelineWaveOutput.h"
#include "MoviePipeline.h"
#include "Sound/SampleBufferIO.h"
#include "SampleBuffer.h"
#include "MoviePipelineOutputSetting.h"
#include "MoviePipelineSetting.h"
#include "MoviePipelineMasterConfig.h"
#include "MovieRenderPipelineCoreModule.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

static bool IsMoviePipelineAudioOutputSupported()
{
	return FParse::Param(FCommandLine::Get(), TEXT("deterministicaudio"));
}

void UMoviePipelineWaveOutput::BeginFinalizeImpl()
{
	if (!IsEnabled() || !GetIsUserCustomized())
	{
		return;
	}

	if (!IsMoviePipelineAudioOutputSupported())
	{
		return;
	}

	ActiveWriters.Reset();

	// There should be no active submixes by the time we finalize - they should all have been converted to recorded samples.
	check(GetPipeline()->GetAudioState().ActiveSubmixes.Num() == 0);

	// If we didn't end up recording audio, don't try outputting any files.
	if (GetPipeline()->GetAudioState().FinishedSegments.Num() == 0)
	{
		return;
	}

	UMoviePipelineOutputSetting* OutputSetting = GetPipeline()->GetPipelineMasterConfig()->FindSetting<UMoviePipelineOutputSetting>();
	
	// Use our file name format on the end of the shared common directory.
	FString FileNameFormatString = OutputSetting->OutputDirectory.Path / FileNameFormat;

	// Place {file_dupe} onto the end of the format string so (2) gets added to the end (before ext is added).
	FileNameFormatString += TEXT("{file_dup}");

	// Check to see if we need to split our audio into more than one segment, otherwise we combine them all into one.
	const bool bSplitOnShots = FileNameFormatString.Contains(TEXT("{shot_name}"));
	const bool bSplitOnCameras = FileNameFormatString.Contains(TEXT("{camera_name}"));

	TArray<MoviePipeline::FAudioState::FAudioSegment> OutputBuffers;

	// Merge them based on their file format specifier.
	if (bSplitOnCameras)
	{
		// By default we store all all audio segments per camera cut, so we'll just pass it on as is.
		OutputBuffers = GetPipeline()->GetAudioState().FinishedSegments;
	}
	else if (bSplitOnShots)
	{
		// Create a mapping between shot indexes and all of their segments within them.
		TMap<int32, TArray<MoviePipeline::FAudioState::FAudioSegment>> ShotMap;
		for (const MoviePipeline::FAudioState::FAudioSegment& Segment : GetPipeline()->GetAudioState().FinishedSegments)
		{
			ShotMap.FindOrAdd(Segment.OutputState.ShotIndex).Add(Segment);
		}

		// Then convert the array of segments into combined segments
		for (TPair<int32, TArray<MoviePipeline::FAudioState::FAudioSegment>>& KVP : ShotMap)
		{
			MoviePipeline::FAudioState::FAudioSegment& CombinedSegment = OutputBuffers.AddDefaulted_GetRef();
			for (MoviePipeline::FAudioState::FAudioSegment& Segment : KVP.Value)
			{
				CombinedSegment.NumChannels = Segment.NumChannels;
				CombinedSegment.SampleRate = Segment.SampleRate;

				Audio::AlignedFloatBuffer& Buffer = Segment.SegmentData;
				CombinedSegment.SegmentData.Append(MoveTemp(Buffer));
			}
		}
	}
	else
	{
		MoviePipeline::FAudioState::FAudioSegment& CombinedSegment = OutputBuffers.AddDefaulted_GetRef();

		// Loop through all of our segments and combine them into one.
		for (const MoviePipeline::FAudioState::FAudioSegment& Segment : GetPipeline()->GetAudioState().FinishedSegments)
		{
			CombinedSegment.NumChannels = Segment.NumChannels;
			CombinedSegment.SampleRate = Segment.SampleRate;

			CombinedSegment.SegmentData.Append(Segment.SegmentData);
		}
	}

	// Write each segment to disk.
	for (const MoviePipeline::FAudioState::FAudioSegment& Segment : OutputBuffers)
	{
		const Audio::AlignedFloatBuffer& Buffer = Segment.SegmentData;
		
		double StartTime = FPlatformTime::Seconds();
		Audio::TSampleBuffer<int16> SampleBuffer = Audio::TSampleBuffer<int16>(Buffer.GetData(), Buffer.Num(), Segment.NumChannels, Segment.SampleRate);
		UE_LOG(LogMovieRenderPipeline, Log, TEXT("Audio Segment took %f seconds to convert to a sample buffer."), (FPlatformTime::Seconds() - StartTime));

		// We don't provide an extension override because the FSoundWavePCMWriter API forces their own.
		FStringFormatNamedArguments FormatOverrides;
		FormatOverrides.Add(TEXT("shot_name"), TEXT("Unsupported_Shot_Name_For_Output_File_BugIt"));
		FormatOverrides.Add(TEXT("camera_name"), TEXT("Unsupported_Camera_Name_For_Output_File_BugIt"));
		
		// Create a full absolute path
		FString FinalFilePath = GetPipeline()->ResolveFilenameFormatArguments(FileNameFormatString, Segment.OutputState, FormatOverrides);

		// Remove the .{ext} string added by resolving the name format. Works for all the other types, but incompatible with our API.
		if (FinalFilePath.EndsWith(TEXT(".{ext}")))
		{
			FinalFilePath.LeftChopInline(6);
		}
		
		FString FileName = "UnnamedAudio";
		FString FileFolder = FinalFilePath;

		// And then extract out just the filename to fit the API.
		FPaths::NormalizeFilename(FinalFilePath);

		int32 LastFolderSeparator;
		FinalFilePath.FindLastChar(TEXT('/'), LastFolderSeparator);
		if (LastFolderSeparator != INDEX_NONE)
		{
			FileFolder = FinalFilePath.Left(LastFolderSeparator);
			FileName = FinalFilePath.RightChop(LastFolderSeparator + 1);
		}

		const bool bIsRelativePath = FPaths::IsRelative(FileFolder);
		if (bIsRelativePath)
		{
			FileFolder = FPaths::ConvertRelativePathToFull(FileFolder);
		}
		
		OutstandingWrites++;
		TUniquePtr<Audio::FSoundWavePCMWriter> Writer = MakeUnique<Audio::FSoundWavePCMWriter>();
		Writer->BeginWriteToWavFile(SampleBuffer, FileName, FileFolder, [this]()
		{
			this->OutstandingWrites--;
		});

		ActiveWriters.Add(MoveTemp(Writer));
	}
}

bool UMoviePipelineWaveOutput::HasFinishedProcessingImpl()
{
	return OutstandingWrites == 0;
}

void UMoviePipelineWaveOutput::ValidateStateImpl()
{
	if (!IsMoviePipelineAudioOutputSupported())
	{
		ValidationState = EMoviePipelineValidationState::Warnings;
		ValidationResults.Add(NSLOCTEXT("MovieRenderPipeline", "WaveOutput_NotUsingDeterministicAudio", "Process must be launched with \"-deterministicaudio\" for this to work. Using a remote render will automatically add this argument."));
	}
}

void UMoviePipelineWaveOutput::BuildNewProcessCommandLineImpl(FString& InOutUnrealURLParams, FString& InOutCommandLineArgs) const
{
	// Always add this even if we're not disabled so that audio is muted, it'll never line up during preview anyways.
	InOutCommandLineArgs += " -deterministicaudio";
}
