// Copyright Epic Games, Inc. All Rights Reserved.

#include "SContextualAnimViewport.h"
#include "ContextualAnimViewportClient.h"
#include "SContextualAnimViewportToolbar.h"
#include "ContextualAnimAssetEditorCommands.h"
#include "ContextualAnimEditorStyle.h"
#include "ContextualAnimAssetEditorToolkit.h"

void SContextualAnimViewport::Construct(const FArguments& InArgs, const FContextualAnimViewportRequiredArgs& InRequiredArgs)
{
	PreviewScenePtr = InRequiredArgs.PreviewScene;
	AssetEditorToolkitPtr = InRequiredArgs.AssetEditorToolkit;

	SEditorViewport::Construct(
		SEditorViewport::FArguments()
		.IsEnabled(FSlateApplication::Get().GetNormalExecutionAttribute())
		.AddMetaData<FTagMetaData>(TEXT("AnimationTools.Viewport"))
	);
}

const FSlateBrush* SContextualAnimViewport::OnGetViewportBorderBrush() const
{
	// Highlight the border of the viewport when Simulate Mode is active
	if (AssetEditorToolkitPtr.Pin()->IsSimulateModeActive())
	{
		return FContextualAnimEditorStyle::Get().GetBrush("ContextualAnimEditor.Viewport.Border");
	}

	return nullptr;
}

FSlateColor SContextualAnimViewport::OnGetViewportBorderColorAndOpacity() const
{
	return FLinearColor::Yellow;
}

void SContextualAnimViewport::BindCommands()
{
	SEditorViewport::BindCommands();

	const FContextualAnimAssetEditorCommands& Commands = FContextualAnimAssetEditorCommands::Get();

	TSharedRef<FContextualAnimViewportClient> ViewportClientRef = ViewportClient.ToSharedRef();

	CommandList->MapAction(
		Commands.ShowIKTargetsDrawAll,
		FExecuteAction::CreateSP(ViewportClientRef, &FContextualAnimViewportClient::OnSetIKTargetsDrawMode, EShowIKTargetsDrawMode::All),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(ViewportClientRef, &FContextualAnimViewportClient::IsIKTargetsDrawModeSet, EShowIKTargetsDrawMode::All));

	CommandList->MapAction(
		Commands.ShowIKTargetsDrawSelected,
		FExecuteAction::CreateSP(ViewportClientRef, &FContextualAnimViewportClient::OnSetIKTargetsDrawMode, EShowIKTargetsDrawMode::Selected),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(ViewportClientRef, &FContextualAnimViewportClient::IsIKTargetsDrawModeSet, EShowIKTargetsDrawMode::Selected));

	CommandList->MapAction(
		Commands.ShowIKTargetsDrawNone,
		FExecuteAction::CreateSP(ViewportClientRef, &FContextualAnimViewportClient::OnSetIKTargetsDrawMode, EShowIKTargetsDrawMode::None),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(ViewportClientRef, &FContextualAnimViewportClient::IsIKTargetsDrawModeSet, EShowIKTargetsDrawMode::None));
}

TSharedRef<FEditorViewportClient> SContextualAnimViewport::MakeEditorViewportClient()
{
	ViewportClient = MakeShared<FContextualAnimViewportClient>(PreviewScenePtr.Pin().ToSharedRef(), SharedThis(this), AssetEditorToolkitPtr.Pin().ToSharedRef());
	ViewportClient->ViewportType = LVT_Perspective;
	ViewportClient->bSetListenerPosition = false;
	ViewportClient->SetViewLocation(EditorViewportDefs::DefaultPerspectiveViewLocation);
	ViewportClient->SetViewRotation(EditorViewportDefs::DefaultPerspectiveViewRotation);

	return ViewportClient.ToSharedRef();
}

TSharedPtr<SWidget> SContextualAnimViewport::MakeViewportToolbar()
{
	return SAssignNew(ViewportToolbar, SContextualAnimViewportToolBar, SharedThis(this));
}

TSharedRef<SEditorViewport> SContextualAnimViewport::GetViewportWidget()
{
	return SharedThis(this);
}

TSharedPtr<FExtender> SContextualAnimViewport::GetExtenders() const
{
	TSharedPtr<FExtender> Result(MakeShareable(new FExtender));
	return Result;
}

void SContextualAnimViewport::OnFloatingButtonClicked()
{
}