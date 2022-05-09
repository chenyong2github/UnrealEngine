// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterLightcardEditorViewport.h"

#include "DisplayClusterLightCardEditorViewportClient.h"
#include "DisplayClusterLightCardEditorCommands.h"

#include "EditorViewportCommands.h"
#include "SEditorViewportToolBarMenu.h"
#include "STransformViewportToolbar.h"
#include "Slate/SceneViewport.h"

#include "Kismet2/DebuggerCommands.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "DisplayClusterLightcardEditorViewport"

class SDisplayClusterLightCardEditorViewportToolBar : public SViewportToolBar
{
public:
	SLATE_BEGIN_ARGS(SDisplayClusterLightCardEditorViewportToolBar){}
		SLATE_ARGUMENT(TWeakPtr<SDisplayClusterLightCardEditorViewport>, EditorViewport)
	SLATE_END_ARGS()

	/** Constructs this widget with the given parameters */
	void Construct(const FArguments& InArgs)
	{
		EditorViewport = InArgs._EditorViewport;
		static const FName DefaultForegroundName("DefaultForeground");

		this->ChildSlot
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("NoBorder"))
			.ForegroundColor(FAppStyle::GetSlateColor(DefaultForegroundName))
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.0f, 2.0f)
				[
					SNew(SEditorViewportToolbarMenu)
					.ParentToolBar(SharedThis(this))
					.Cursor(EMouseCursor::Default)
					.Image("EditorViewportToolBar.OptionsDropdown")
					.OnGetMenuContent(this, &SDisplayClusterLightCardEditorViewportToolBar::GeneratePreviewMenu)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.0f, 2.0f)
				[
					SNew(SEditorViewportToolbarMenu)
					.ParentToolBar(SharedThis(this))
					.Cursor(EMouseCursor::Default)
					.Label(this, &SDisplayClusterLightCardEditorViewportToolBar::GetProjectionMenuLabel)
					.OnGetMenuContent(this, &SDisplayClusterLightCardEditorViewportToolBar::GenerateProjectionMenu)
				]
				+ SHorizontalBox::Slot()
				.Padding( 3.0f, 1.0f )
				.HAlign( HAlign_Right )
				[
					SNew(STransformViewportToolBar)
					.Viewport(EditorViewport.Pin().ToSharedRef())
					.CommandList(EditorViewport.Pin()->GetCommandList())
				]
			]
		];

		SViewportToolBar::Construct(SViewportToolBar::FArguments());
	}

	/** Creates the preview menu */
	TSharedRef<SWidget> GeneratePreviewMenu() const
	{
		TSharedPtr<const FUICommandList> CommandList = EditorViewport.IsValid()? EditorViewport.Pin()->GetCommandList(): nullptr;

		const bool bInShouldCloseWindowAfterMenuSelection = true;

		FMenuBuilder PreviewOptionsMenuBuilder(bInShouldCloseWindowAfterMenuSelection, CommandList);
		{
			PreviewOptionsMenuBuilder.BeginSection("LightCardEditorViewportOptions", LOCTEXT("ViewportOptionsMenuHeader", "Viewport Options"));
			{
				PreviewOptionsMenuBuilder.AddMenuEntry(FDisplayClusterLightCardEditorCommands::Get().ResetCamera);
			}
			PreviewOptionsMenuBuilder.EndSection();
		}

		return PreviewOptionsMenuBuilder.MakeWidget();
	}

	FText GetProjectionMenuLabel() const
	{
		FText Label = LOCTEXT("ProjectionMenuTitle_Default", "Projection");

		if (EditorViewport.IsValid())
		{
			switch (EditorViewport.Pin()->GetLightCardEditorViewportClient()->GetProjectionMode())
			{
			case EDisplayClusterMeshProjectionType::Perspective:
				Label = LOCTEXT("ProjectionwMenuTitle_Perspective", "Perspective");
				break;

			case EDisplayClusterMeshProjectionType::Azimuthal:
				Label = LOCTEXT("ProjectionwMenuTitle_Azimuthal", "Azimuthal");
				break;
			}
		}

		return Label;
	}

	TSharedRef<SWidget> GenerateProjectionMenu() const
	{
		TSharedPtr<const FUICommandList> CommandList = EditorViewport.IsValid() ? EditorViewport.Pin()->GetCommandList() : nullptr;

		const bool bInShouldCloseWindowAfterMenuSelection = true;
		FMenuBuilder ViewMenuBuilder(bInShouldCloseWindowAfterMenuSelection, CommandList);

		ViewMenuBuilder.AddMenuEntry(FDisplayClusterLightCardEditorCommands::Get().PerspectiveProjection);
		ViewMenuBuilder.AddMenuEntry(FDisplayClusterLightCardEditorCommands::Get().AzimuthalProjection);

		return ViewMenuBuilder.MakeWidget();
	}

	FText GetViewMenuLabel() const
	{
		FText Label = NSLOCTEXT("BlueprintEditor", "ViewMenuTitle_Default", "View");

		if (EditorViewport.IsValid())
		{
			switch (EditorViewport.Pin()->GetViewportClient()->GetViewMode())
			{
			case VMI_Lit:
				Label = NSLOCTEXT("BlueprintEditor", "ViewMenuTitle_Lit", "Lit");
				break;

			case VMI_Unlit:
				Label = NSLOCTEXT("BlueprintEditor", "ViewMenuTitle_Unlit", "Unlit");
				break;

			case VMI_BrushWireframe:
				Label = NSLOCTEXT("BlueprintEditor", "ViewMenuTitle_Wireframe", "Wireframe");
				break;
			}
		}

		return Label;
	}

private:
	/** Reference to the parent viewport */
	TWeakPtr<SDisplayClusterLightCardEditorViewport> EditorViewport;
};

void SDisplayClusterLightCardEditorViewport::Construct(const FArguments& InArgs, TSharedPtr<SDisplayClusterLightCardEditor> InLightCardEditor)
{
	LightCardEditorPtr = InLightCardEditor;
	
	AdvancedPreviewScene = MakeShared<FAdvancedPreviewScene>(FPreviewScene::ConstructionValues());
	AdvancedPreviewScene->SetFloorVisibility(true);
		
	SEditorViewport::Construct(SEditorViewport::FArguments());
}

SDisplayClusterLightCardEditorViewport::~SDisplayClusterLightCardEditorViewport()
{
	if (ViewportClient.IsValid())
	{
		ViewportClient->Viewport = nullptr;
		ViewportClient.Reset();
	}
}

TSharedRef<SEditorViewport> SDisplayClusterLightCardEditorViewport::GetViewportWidget()
{
	return SharedThis(this);
}

TSharedPtr<FExtender> SDisplayClusterLightCardEditorViewport::GetExtenders() const
{
	TSharedPtr<FExtender> Result(MakeShareable(new FExtender));
	return Result;
}

void SDisplayClusterLightCardEditorViewport::OnFloatingButtonClicked()
{
}

void SDisplayClusterLightCardEditorViewport::SetRootActor(ADisplayClusterRootActor* NewRootActor)
{
	if (ViewportClient.IsValid())
	{
		ViewportClient->UpdatePreviewActor(NewRootActor);
	}
}

TSharedRef<FEditorViewportClient> SDisplayClusterLightCardEditorViewport::MakeEditorViewportClient()
{
	check(AdvancedPreviewScene.IsValid());
	
	ViewportClient = MakeShareable(new FDisplayClusterLightCardEditorViewportClient(*AdvancedPreviewScene.Get(),
		SharedThis(this), LightCardEditorPtr));
	return ViewportClient.ToSharedRef();
}

TSharedPtr<SWidget> SDisplayClusterLightCardEditorViewport::MakeViewportToolbar()
{
	return
		SNew(SDisplayClusterLightCardEditorViewportToolBar)
		.EditorViewport(SharedThis(this))
		.IsEnabled(FSlateApplication::Get().GetNormalExecutionAttribute());
}

void SDisplayClusterLightCardEditorViewport::PopulateViewportOverlays(TSharedRef<SOverlay> Overlay)
{
	SEditorViewport::PopulateViewportOverlays(Overlay);
}

void SDisplayClusterLightCardEditorViewport::BindCommands()
{
	const FDisplayClusterLightCardEditorCommands& Commands = FDisplayClusterLightCardEditorCommands::Get();

	CommandList->MapAction(
		FDisplayClusterLightCardEditorCommands::Get().PerspectiveProjection,
		FExecuteAction::CreateSP(this, &SDisplayClusterLightCardEditorViewport::SetProjectionMode, EDisplayClusterMeshProjectionType::Perspective),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &SDisplayClusterLightCardEditorViewport::IsProjectionModeSelected, EDisplayClusterMeshProjectionType::Perspective));

	CommandList->MapAction(
		FDisplayClusterLightCardEditorCommands::Get().AzimuthalProjection,
		FExecuteAction::CreateSP(this, &SDisplayClusterLightCardEditorViewport::SetProjectionMode, EDisplayClusterMeshProjectionType::Azimuthal),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &SDisplayClusterLightCardEditorViewport::IsProjectionModeSelected, EDisplayClusterMeshProjectionType::Azimuthal));

	CommandList->MapAction(
		Commands.ResetCamera,
		FExecuteAction::CreateSP(ViewportClient.Get(), &FDisplayClusterLightCardEditorViewportClient::ResetCamera, false));
	
	SEditorViewport::BindCommands();
}

void SDisplayClusterLightCardEditorViewport::SetProjectionMode(EDisplayClusterMeshProjectionType InProjectionMode)
{
	if (ViewportClient.IsValid())
	{
		ViewportClient->SetProjectionMode(InProjectionMode);
	}
}

bool SDisplayClusterLightCardEditorViewport::IsProjectionModeSelected(EDisplayClusterMeshProjectionType InProjectionMode) const
{
	if (ViewportClient.IsValid())
	{
		return ViewportClient->GetProjectionMode() == InProjectionMode;
	}

	return false;
}

#undef LOCTEXT_NAMESPACE
