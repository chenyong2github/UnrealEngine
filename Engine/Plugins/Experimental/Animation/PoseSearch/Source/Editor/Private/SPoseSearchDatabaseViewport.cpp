// Copyright Epic Games, Inc. All Rights Reserved.

#include "SPoseSearchDatabaseViewport.h"
#include "PoseSearchDatabaseViewportClient.h"
#include "PoseSearchDatabaseViewModel.h"
#include "PoseSearchDatabaseEditorToolkit.h"
#include "SPoseSearchDatabaseViewportToolbar.h"
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

	const FPoseSearchDatabaseEditorCommands& Commands = FPoseSearchDatabaseEditorCommands::Get();

	TSharedRef<FPoseSearchDatabaseViewModel> ViewModelRef = 
		AssetEditorToolkitPtr.Pin()->GetViewModelSharedPtr().ToSharedRef();

	CommandList->MapAction(
		Commands.ShowPoseFeaturesNone,
		FExecuteAction::CreateSP(
			ViewModelRef, 
			&FPoseSearchDatabaseViewModel::OnSetPoseFeaturesDrawMode, 
			EPoseSearchFeaturesDrawMode::None),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(
			ViewModelRef, 
			&FPoseSearchDatabaseViewModel::IsPoseFeaturesDrawMode, 
			EPoseSearchFeaturesDrawMode::None));

	CommandList->MapAction(
		Commands.ShowPoseFeaturesAll,
		FExecuteAction::CreateSP(
			ViewModelRef,
			&FPoseSearchDatabaseViewModel::OnSetPoseFeaturesDrawMode,
			EPoseSearchFeaturesDrawMode::All),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(
			ViewModelRef,
			&FPoseSearchDatabaseViewModel::IsPoseFeaturesDrawMode,
			EPoseSearchFeaturesDrawMode::All));

	CommandList->MapAction(
		Commands.ShowAnimationOriginalOnly,
		FExecuteAction::CreateSP(
			ViewModelRef,
			&FPoseSearchDatabaseViewModel::OnSetAnimationPreviewMode,
			EAnimationPreviewMode::OriginalOnly),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(
			ViewModelRef,
			&FPoseSearchDatabaseViewModel::IsAnimationPreviewMode,
			EAnimationPreviewMode::OriginalOnly));

	CommandList->MapAction(
		Commands.ShowAnimationOriginalAndMirrored,
		FExecuteAction::CreateSP(
			ViewModelRef,
			&FPoseSearchDatabaseViewModel::OnSetAnimationPreviewMode,
			EAnimationPreviewMode::OriginalAndMirrored),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(
			ViewModelRef,
			&FPoseSearchDatabaseViewModel::IsAnimationPreviewMode,
			EAnimationPreviewMode::OriginalAndMirrored));
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

TSharedPtr<SWidget> SPoseSearchDatabaseViewport::MakeViewportToolbar()
{
	return SAssignNew(ViewportToolbar, SPoseSearchDatabaseViewportToolBar, SharedThis(this));
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
