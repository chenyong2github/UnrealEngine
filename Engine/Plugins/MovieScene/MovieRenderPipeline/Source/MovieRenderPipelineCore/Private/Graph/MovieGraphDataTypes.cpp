// Copyright Epic Games, Inc. All Rights Reserved.

#include "Graph/MovieGraphDataTypes.h"
#include "Graph/MovieGraphLinearTimeStep.h"
#include "Graph/MovieGraphDefaultRenderer.h"
#include "Graph/MovieGraphDefaultDataCaching.h"
#include "Graph/MovieGraphSequenceTimeRangeBuilder.h"
#include "Graph/MovieGraphPipeline.h"

FMovieGraphInitConfig::FMovieGraphInitConfig()
{
	TimeStepClass = UMovieGraphLinearTimeStep::StaticClass();
	RendererClass = UMovieGraphDefaultRenderer::StaticClass();
	TimeRangeBuilderClass = UMovieGraphSequenceTimeRangeBuilder::StaticClass();
	DataCachingClass = UMovieGraphDefaultDataCaching::StaticClass();
	bRenderViewport = false;
}

UMovieGraphPipeline* UMovieGraphTimeStepBase::GetOwningGraph() const
{ 
	return GetTypedOuter<UMovieGraphPipeline>();
}

UMovieGraphPipeline* UMovieGraphRendererBase::GetOwningGraph() const
{ 
	return GetTypedOuter<UMovieGraphPipeline>();
}

UMovieGraphPipeline* UMovieGraphTimeRangeBuilderBase::GetOwningGraph() const
{
	return GetTypedOuter<UMovieGraphPipeline>();
}

UMovieGraphPipeline* UMovieGraphDataCachingBase::GetOwningGraph() const
{
	return GetTypedOuter<UMovieGraphPipeline>();
}