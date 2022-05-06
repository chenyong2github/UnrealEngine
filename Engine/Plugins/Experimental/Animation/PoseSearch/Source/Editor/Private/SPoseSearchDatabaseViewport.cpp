// Copyright Epic Games, Inc. All Rights Reserved.

#include "SPoseSearchDatabaseViewport.h"
#include "PoseSearchDatabaseViewportClient.h"
#include "PoseSearchDatabaseViewModel.h"
#include "PoseSearchDatabaseEditorToolkit.h"
#include "SPoseSearchDatabaseViewportToolbar.h"
#include "PoseSearchDatabaseEditorCommands.h"

namespace UE::PoseSearch
{
	void SDatabaseViewport::Construct(
		const FArguments& InArgs,
		const FDatabaseViewportRequiredArgs& InRequiredArgs)
	{
		PreviewScenePtr = InRequiredArgs.PreviewScene;
		AssetEditorToolkitPtr = InRequiredArgs.AssetEditorToolkit;

		SEditorViewport::Construct(
			SEditorViewport::FArguments()
			.IsEnabled(FSlateApplication::Get().GetNormalExecutionAttribute())
			.AddMetaData<FTagMetaData>(TEXT("AnimationTools.Viewport"))
		);
	}

	void SDatabaseViewport::BindCommands()
	{
		SEditorViewport::BindCommands();

		const FDatabaseEditorCommands& Commands = FDatabaseEditorCommands::Get();

		TSharedRef<FDatabaseViewModel> ViewModelRef =
			AssetEditorToolkitPtr.Pin()->GetViewModelSharedPtr().ToSharedRef();

		CommandList->MapAction(
			Commands.ShowPoseFeaturesNone,
			FExecuteAction::CreateSP(
				ViewModelRef,
				&FDatabaseViewModel::OnSetPoseFeaturesDrawMode,
				EFeaturesDrawMode::None),
			FCanExecuteAction(),
			FIsActionChecked::CreateSP(
				ViewModelRef,
				&FDatabaseViewModel::IsPoseFeaturesDrawMode,
				EFeaturesDrawMode::None));

		CommandList->MapAction(
			Commands.ShowPoseFeaturesAll,
			FExecuteAction::CreateSP(
				ViewModelRef,
				&FDatabaseViewModel::OnSetPoseFeaturesDrawMode,
				EFeaturesDrawMode::All),
			FCanExecuteAction(),
			FIsActionChecked::CreateSP(
				ViewModelRef,
				&FDatabaseViewModel::IsPoseFeaturesDrawMode,
				EFeaturesDrawMode::All));

		CommandList->MapAction(
			Commands.ShowAnimationNone,
			FExecuteAction::CreateSP(
				ViewModelRef,
				&FDatabaseViewModel::OnSetAnimationPreviewMode,
				EAnimationPreviewMode::None),
			FCanExecuteAction(),
			FIsActionChecked::CreateSP(
				ViewModelRef,
				&FDatabaseViewModel::IsAnimationPreviewMode,
				EAnimationPreviewMode::None));

		CommandList->MapAction(
			Commands.ShowAnimationOriginalOnly,
			FExecuteAction::CreateSP(
				ViewModelRef,
				&FDatabaseViewModel::OnSetAnimationPreviewMode,
				EAnimationPreviewMode::OriginalOnly),
			FCanExecuteAction(),
			FIsActionChecked::CreateSP(
				ViewModelRef,
				&FDatabaseViewModel::IsAnimationPreviewMode,
				EAnimationPreviewMode::OriginalOnly));

		CommandList->MapAction(
			Commands.ShowAnimationOriginalAndMirrored,
			FExecuteAction::CreateSP(
				ViewModelRef,
				&FDatabaseViewModel::OnSetAnimationPreviewMode,
				EAnimationPreviewMode::OriginalAndMirrored),
			FCanExecuteAction(),
			FIsActionChecked::CreateSP(
				ViewModelRef,
				&FDatabaseViewModel::IsAnimationPreviewMode,
				EAnimationPreviewMode::OriginalAndMirrored));
	}

	TSharedRef<FEditorViewportClient> SDatabaseViewport::MakeEditorViewportClient()
	{
		ViewportClient = MakeShared<FDatabaseViewportClient>(
			PreviewScenePtr.Pin().ToSharedRef(),
			SharedThis(this),
			AssetEditorToolkitPtr.Pin().ToSharedRef());
		ViewportClient->ViewportType = LVT_Perspective;
		ViewportClient->bSetListenerPosition = false;
		ViewportClient->SetViewLocation(EditorViewportDefs::DefaultPerspectiveViewLocation);
		ViewportClient->SetViewRotation(EditorViewportDefs::DefaultPerspectiveViewRotation);

		return ViewportClient.ToSharedRef();
	}

	TSharedPtr<SWidget> SDatabaseViewport::MakeViewportToolbar()
	{
		return SAssignNew(ViewportToolbar, SPoseSearchDatabaseViewportToolBar, SharedThis(this));
	}


	TSharedRef<SEditorViewport> SDatabaseViewport::GetViewportWidget()
	{
		return SharedThis(this);
	}

	TSharedPtr<FExtender> SDatabaseViewport::GetExtenders() const
	{
		TSharedPtr<FExtender> Result(MakeShareable(new FExtender));
		return Result;
	}

	void SDatabaseViewport::OnFloatingButtonClicked()
	{
	}
}

