// Copyright Epic Games, Inc. All Rights Reserved.

#include "SPoseSearchDatabaseViewport.h"
#include "PoseSearchDatabaseViewportClient.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "PoseSearchDatabaseEditorCommands.h"

void SPoseSearchDatabaseViewport::Construct(
	const FArguments& InArgs, 
	const FPoseSearchDatabaseViewportRequiredArgs& InRequiredArgs)
{
	PreviewScenePtr = InRequiredArgs.PreviewScene;
	AssetEditorToolkitPtr = InRequiredArgs.AssetEditorToolkit;

	SEditorViewport::Construct(
		SEditorViewport::FArguments()
		.IsEnabled(FSlateApplication::Get().GetNormalExecutionAttribute())
		.AddMetaData<FTagMetaData>(TEXT("AnimationTools.Viewport"))
	);
}

void SPoseSearchDatabaseViewport::BindCommands()
{
	SEditorViewport::BindCommands();
}

TSharedRef<FEditorViewportClient> SPoseSearchDatabaseViewport::MakeEditorViewportClient()
{
	ViewportClient = MakeShared<FPoseSearchDatabaseViewportClient>(
		PreviewScenePtr.Pin().ToSharedRef(), 
		SharedThis(this), 
		AssetEditorToolkitPtr.Pin().ToSharedRef());
	ViewportClient->ViewportType = LVT_Perspective;
	ViewportClient->bSetListenerPosition = false;
	ViewportClient->SetViewLocation(EditorViewportDefs::DefaultPerspectiveViewLocation);
	ViewportClient->SetViewRotation(EditorViewportDefs::DefaultPerspectiveViewRotation);

	return ViewportClient.ToSharedRef();
}

TSharedRef<SEditorViewport> SPoseSearchDatabaseViewport::GetViewportWidget()
{
	return SharedThis(this);
}

TSharedPtr<FExtender> SPoseSearchDatabaseViewport::GetExtenders() const
{
	TSharedPtr<FExtender> Result(MakeShareable(new FExtender));
	return Result;
}

void SPoseSearchDatabaseViewport::OnFloatingButtonClicked()
{
}
