// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterLightCardEditorViewport.h"

#include "DisplayClusterLightCardEditorViewportClient.h"
#include "DisplayClusterLightCardEditorCommands.h"

#include "EditorViewportCommands.h"
#include "SEditorViewportToolBarMenu.h"
#include "STransformViewportToolbar.h"
#include "Slate/SceneViewport.h"

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
			.BorderImage(FEditorStyle::GetBrush("NoBorder"))
			.ForegroundColor(FEditorStyle::GetSlateColor(DefaultForegroundName))
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.0f, 2.0f)
				[
					SNew(SEditorViewportToolbarMenu)
					.ParentToolBar(SharedThis(this))
					.Cursor(EMouseCursor::Default)
					.Image("EditorViewportToolBar.MenuDropdown")
					.OnGetMenuContent(this, &SDisplayClusterLightCardEditorViewportToolBar::GeneratePreviewMenu)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.0f, 2.0f)
				[
					SNew( SEditorViewportToolbarMenu )
					.ParentToolBar( SharedThis( this ) )
					.Cursor( EMouseCursor::Default )
					.Label(this, &SDisplayClusterLightCardEditorViewportToolBar::GetCameraMenuLabel)
					.LabelIcon(this, &SDisplayClusterLightCardEditorViewportToolBar::GetCameraMenuLabelIcon)
					.OnGetMenuContent(this, &SDisplayClusterLightCardEditorViewportToolBar::GenerateCameraMenu)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.0f, 2.0f)
				[
					SNew( SEditorViewportToolbarMenu )
					.ParentToolBar( SharedThis( this ) )
					.Cursor( EMouseCursor::Default )
					.Label(this, &SDisplayClusterLightCardEditorViewportToolBar::GetViewMenuLabel)
					.LabelIcon(this, &SDisplayClusterLightCardEditorViewportToolBar::GetViewMenuLabelIcon)
					.OnGetMenuContent(this, &SDisplayClusterLightCardEditorViewportToolBar::GenerateViewMenu)
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
			PreviewOptionsMenuBuilder.BeginSection("BlueprintEditorPreviewOptions", NSLOCTEXT("BlueprintEditor", "PreviewOptionsMenuHeader", "Preview Viewport Options"));
			{
				PreviewOptionsMenuBuilder.AddMenuEntry(FDisplayClusterLightCardEditorCommands::Get().ResetCamera);
				PreviewOptionsMenuBuilder.AddMenuEntry(FEditorViewportCommands::Get().ToggleRealTime);
			}
			PreviewOptionsMenuBuilder.EndSection();
		}

		return PreviewOptionsMenuBuilder.MakeWidget();
	}

	FText GetCameraMenuLabel() const
	{
		if(EditorViewport.IsValid())
		{
			return GetCameraMenuLabelFromViewportType(EditorViewport.Pin()->GetViewportClient()->GetViewportType());
		}

		return NSLOCTEXT("BlueprintEditor", "CameraMenuTitle_Default", "Camera");
	}

	const FSlateBrush* GetCameraMenuLabelIcon() const
	{
		if(EditorViewport.IsValid())
		{
			return GetCameraMenuLabelIconFromViewportType( EditorViewport.Pin()->GetViewportClient()->GetViewportType() );
		}

		return FEditorStyle::GetBrush(NAME_None);
	}

	TSharedRef<SWidget> GenerateCameraMenu() const
	{
		TSharedPtr<const FUICommandList> CommandList = EditorViewport.IsValid()? EditorViewport.Pin()->GetCommandList(): nullptr;

		const bool bInShouldCloseWindowAfterMenuSelection = true;
		FMenuBuilder CameraMenuBuilder(bInShouldCloseWindowAfterMenuSelection, CommandList);

		CameraMenuBuilder.AddMenuEntry(FEditorViewportCommands::Get().Perspective);

		CameraMenuBuilder.BeginSection("LevelViewportCameraType_Ortho", NSLOCTEXT("BlueprintEditor", "CameraTypeHeader_Ortho", "Orthographic"));
			CameraMenuBuilder.AddMenuEntry(FEditorViewportCommands::Get().Top);
			CameraMenuBuilder.AddMenuEntry(FEditorViewportCommands::Get().Bottom);
			CameraMenuBuilder.AddMenuEntry(FEditorViewportCommands::Get().Left);
			CameraMenuBuilder.AddMenuEntry(FEditorViewportCommands::Get().Right);
			CameraMenuBuilder.AddMenuEntry(FEditorViewportCommands::Get().Front);
			CameraMenuBuilder.AddMenuEntry(FEditorViewportCommands::Get().Back);
		CameraMenuBuilder.EndSection();

		return CameraMenuBuilder.MakeWidget();
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

	const FSlateBrush* GetViewMenuLabelIcon() const
	{
		static FName LitModeIconName("EditorViewport.LitMode");
		static FName UnlitModeIconName("EditorViewport.UnlitMode");
		static FName WireframeModeIconName("EditorViewport.WireframeMode");

		FName Icon = NAME_None;

		if (EditorViewport.IsValid())
		{
			switch (EditorViewport.Pin()->GetViewportClient()->GetViewMode())
			{
			case VMI_Lit:
				Icon = LitModeIconName;
				break;

			case VMI_Unlit:
				Icon = UnlitModeIconName;
				break;

			case VMI_BrushWireframe:
				Icon = WireframeModeIconName;
				break;
			}
		}

		return FEditorStyle::GetBrush(Icon);
	}

	TSharedRef<SWidget> GenerateViewMenu() const
	{
		TSharedPtr<const FUICommandList> CommandList = EditorViewport.IsValid() ? EditorViewport.Pin()->GetCommandList() : nullptr;

		const bool bInShouldCloseWindowAfterMenuSelection = true;
		FMenuBuilder ViewMenuBuilder(bInShouldCloseWindowAfterMenuSelection, CommandList);

		ViewMenuBuilder.AddMenuEntry(FEditorViewportCommands::Get().LitMode, NAME_None, NSLOCTEXT("BlueprintEditor", "LitModeMenuOption", "Lit"));
		ViewMenuBuilder.AddMenuEntry(FEditorViewportCommands::Get().UnlitMode, NAME_None, NSLOCTEXT("BlueprintEditor", "UnlitModeMenuOption", "Unlit"));
		ViewMenuBuilder.AddMenuEntry(FEditorViewportCommands::Get().WireframeMode, NAME_None, NSLOCTEXT("BlueprintEditor", "WireframeModeMenuOption", "Wireframe"));

		return ViewMenuBuilder.MakeWidget();
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

	ViewportClient->SetSceneViewport(SceneViewport);
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
	Commands.ResetCamera,
	FExecuteAction::CreateSP(ViewportClient.Get(), &FDisplayClusterLightCardEditorViewportClient::ResetCamera));

	SEditorViewport::BindCommands();
}

#undef LOCTEXT_NAMESPACE
