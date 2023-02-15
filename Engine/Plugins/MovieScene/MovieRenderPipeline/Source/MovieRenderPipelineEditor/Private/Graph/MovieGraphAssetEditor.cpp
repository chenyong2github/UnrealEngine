// Copyright Epic Games, Inc. All Rights Reserved.

#include "MovieGraphAssetEditor.h"
#include "MovieGraphAssetToolkit.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MovieGraphAssetEditor)

// Temporary cvar to enable/disable upgrading to a graph-based configuration
static TAutoConsoleVariable<bool> CVarMoviePipelineEnableRenderGraph(
	TEXT("MoviePipeline.EnableRenderGraph"),
	false,
	TEXT("Determines if the Render Graph feature is enabled in the UI. This is a highly experimental feature and is not ready for use.")
);

TSharedPtr<FBaseAssetToolkit> UMovieGraphAssetEditor::CreateToolkit()
{
	return MakeShared<FMovieGraphAssetToolkit>(this);
}

void UMovieGraphAssetEditor::GetObjectsToEdit(TArray<UObject*>& InObjectsToEdit)
{
	InObjectsToEdit.Add(ObjectToEdit);
}

void UMovieGraphAssetEditor::SetObjectToEdit(UObject* InObject)
{
	ObjectToEdit = InObject;
}
