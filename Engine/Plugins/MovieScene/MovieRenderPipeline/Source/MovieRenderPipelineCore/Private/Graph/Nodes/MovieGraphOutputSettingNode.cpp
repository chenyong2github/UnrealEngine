// Copyright Epic Games, Inc. All Rights Reserved.

#include "Graph/Nodes/MovieGraphOutputSettingNode.h"

UMovieGraphOutputSettingNode::UMovieGraphOutputSettingNode()
	: OutputResolution(FIntPoint(1920, 1080))
	, OutputFrameRate(FFrameRate(24, 1))
	, bOverwriteExistingOutput(true)
{

	FileNameFormat = TEXT("{sequence_name}.{frame_number}");
	OutputDirectory.Path = TEXT("{project_dir}/Saved/MovieRenders/");
}
