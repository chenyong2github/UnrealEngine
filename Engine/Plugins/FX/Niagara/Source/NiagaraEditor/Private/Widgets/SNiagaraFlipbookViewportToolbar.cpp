// Copyright Epic Games, Inc. All Rights Reserved.

#include "SNiagaraFlipbookViewportToolbar.h"
#include "SNiagaraFlipbookViewport.h"
#include "ViewModels/NiagaraFlipbookViewModel.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "EditorStyleSet.h"
#include "Slate/SceneViewport.h"
#include "ComponentReregisterContext.h"
#include "Runtime/Analytics/Analytics/Public/AnalyticsEventAttribute.h"
#include "Runtime/Analytics/Analytics/Public/Interfaces/IAnalyticsProvider.h"
#include "EngineAnalytics.h"
#include "Widgets/Docking/SDockTab.h"
#include "SEditorViewportToolBarMenu.h"
#include "SEditorViewportToolBarButton.h"

#define LOCTEXT_NAMESPACE "SNiagaraFlipbookViewportToolbar"

void SNiagaraFlipbookViewportToolbar::Construct(const FArguments& InArgs)
{
	static const FName DefaultForegroundName("DefaultForeground");
	const FMargin ToolbarSlotPadding(2.0f, 2.0f);
	const FMargin ToolbarButtonPadding(2.0f, 0.0f);

	WeakViewModel = InArgs._WeakViewModel;
	WeakViewport = InArgs._WeakViewport;

	TSharedPtr<SHorizontalBox> MainBoxPtr;
	SNiagaraFlipbookViewport* Viewport = WeakViewport.Pin().Get();
	check(Viewport != nullptr);

	ChildSlot
	[
		SNew( SBorder )
		.BorderImage( FEditorStyle::GetBrush("NoBorder") )
		// Color and opacity is changed based on whether or not the mouse cursor is hovering over the toolbar area
		.ColorAndOpacity( this, &SViewportToolBar::OnGetColorAndOpacity )
		.ForegroundColor( FEditorStyle::GetSlateColor(DefaultForegroundName) )
		[
			SNew( SVerticalBox )
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew( MainBoxPtr, SHorizontalBox )
			]
		]
	];

	// Toolbar
	MainBoxPtr->AddSlot()
	.AutoWidth()
	.Padding(ToolbarSlotPadding)
	[
		SNew(SEditorViewportToolbarMenu)
		.ParentToolBar(SharedThis(this))
		.Cursor(EMouseCursor::Default)
		.Image("EditorViewportToolBar.MenuDropdown")
		.OnGetMenuContent(this, &SNiagaraFlipbookViewportToolbar::GenerateOptionsMenu)
	];

	// Camera Selection
	MainBoxPtr->AddSlot()
	.AutoWidth()
	.Padding(ToolbarSlotPadding)
	[
		SNew(SEditorViewportToolbarMenu)
		.ParentToolBar(SharedThis(this))
		.Cursor(EMouseCursor::Default)
		.Label(Viewport, &SNiagaraFlipbookViewport::GetActiveCameraModeText)
		.LabelIcon(Viewport, &SNiagaraFlipbookViewport::GetActiveCameraModeIcon)
		.OnGetMenuContent(this, &SNiagaraFlipbookViewportToolbar::GenerateCameraMenu)
	];

	// Capture button
	MainBoxPtr->AddSlot()
	.AutoWidth()
	.Padding(ToolbarSlotPadding)
	[
		SNew(SEditorViewportToolBarButton)
		.Cursor(EMouseCursor::Default)
		.ButtonType(EUserInterfaceActionType::Button)
		.ButtonStyle(&FEditorStyle::Get().GetWidgetStyle<FButtonStyle>("EditorViewportToolBar.MenuButtonWarning"))
		.OnClicked(
			FOnClicked::CreateLambda(
				[WeakViewModel=WeakViewModel]()
				{
					if ( auto ViewModel = WeakViewModel.Pin() )
					{
						ViewModel->RenderFlipbook();
					}
					return FReply::Handled();
				}
			)
		)
		.ToolTipText(LOCTEXT("CaptureToolTip", "Captures the flipbook."))
		.Content()
		[
			SNew(STextBlock)
			.Font(FEditorStyle::GetFontStyle("EditorViewportToolBar.Font"))
			.Text(LOCTEXT("Capture", "Capture"))
			.ColorAndOpacity(FLinearColor::Black)
		]
	];
	
	SViewportToolBar::Construct(SViewportToolBar::FArguments());
}

TSharedRef<SWidget> SNiagaraFlipbookViewportToolbar::GenerateOptionsMenu() const
{
	const bool bInShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bInShouldCloseWindowAfterMenuSelection, nullptr);

	// Show options for preview & flip book
	MenuBuilder.BeginSection(NAME_None, LOCTEXT("ShowOptions", "Show Options"));
	{
		MenuBuilder.AddMenuEntry(
			FText(LOCTEXT("ShowInfoText", "Info Text")),
			FText(LOCTEXT("ShowInfoTextTooltip", "When enabled information will be overlaid on each display.")),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda(
					[WeakViewport=WeakViewport]()
					{
						if ( auto Viewport = WeakViewport.Pin() )
						{
							Viewport->SetInfoTextEnabled(!Viewport->IsInfoTextEnabled());
						}
					}
				),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda(
					[WeakViewport = WeakViewport]()
					{
						auto Viewport = WeakViewport.Pin();
						return Viewport && Viewport->IsInfoTextEnabled();
					}
				)
			),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);

		MenuBuilder.AddMenuEntry(
			FText(LOCTEXT("ShowPreview", "Live Preview")),
			FText(LOCTEXT("ShowPreviewTooltip", "When enabled shows a live preview of what will be rendered, this may not be accurate with all visualization modes.")),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda(
					[WeakViewport=WeakViewport]()
					{
						if ( auto Viewport = WeakViewport.Pin() )
						{
							Viewport->SetPreviewViewEnabled(!Viewport->IsPreviewViewEnabled());
						}
					}
				),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda(
					[WeakViewport = WeakViewport]()
					{
						auto Viewport = WeakViewport.Pin();
						return Viewport && Viewport->IsPreviewViewEnabled();
					}
				)
			),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);
		MenuBuilder.AddMenuEntry(
			FText(LOCTEXT("ShowFlipbook", "Flipbook")),
			FText(LOCTEXT("ShowFlipbookTooltip", "When enabled shows a the generated flipbook texture.")),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda(
					[WeakViewport=WeakViewport]()
					{ 
						if ( auto Viewport = WeakViewport.Pin())
						{
							Viewport->SetFlipbookViewEnabled(!Viewport->IsFlipbookViewEnabled());
						}
					}
				),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda(
					[WeakViewport = WeakViewport]()
					{
						auto Viewport = WeakViewport.Pin();
						return Viewport && Viewport->IsFlipbookViewEnabled();
					}
				)
			),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);
	}
	MenuBuilder.EndSection();

	// Texture Selection Control
	MenuBuilder.BeginSection(NAME_None, LOCTEXT("TextureSelection", "Texture Selection"));
	if (auto ViewModel = WeakViewModel.Pin())
	{
		const UNiagaraFlipbookSettings* FlipbookSettings = ViewModel->GetFlipbookSettings();
		for ( int32 i=0; i < FlipbookSettings->OutputTextures.Num(); ++i )
		{
			FString TextureName;
			if ( FlipbookSettings->OutputTextures[i].OutputName.IsNone() )
			{
				TextureName = FString::Printf(TEXT("Output Texture %d"), i);
			}
			else
			{
				TextureName = FlipbookSettings->OutputTextures[i].OutputName.ToString();
			}

			MenuBuilder.AddMenuEntry(
				FText::FromString(TextureName),
				FText(),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateLambda(
						[WeakViewModel=WeakViewModel, TextureIndex=i]()
						{ 
							if ( auto ViewModel = WeakViewModel.Pin() )
							{
								ViewModel->SetPreviewTextureIndex(TextureIndex);
							}
						}
					),
					FCanExecuteAction(),
					FIsActionChecked::CreateLambda(
						[WeakViewModel=WeakViewModel, TextureIndex=i]()
						{
							auto ViewModel = WeakViewModel.Pin();
							return ViewModel && ViewModel->GetPreviewTextureIndex() == TextureIndex;
						}
					)
				),
				NAME_None,
				EUserInterfaceActionType::RadioButton
			);
		}
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SNiagaraFlipbookViewportToolbar::GenerateCameraMenu() const
{
	SNiagaraFlipbookViewport* Viewport = WeakViewport.Pin().Get();
	check(Viewport != nullptr);

	const bool bInShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bInShouldCloseWindowAfterMenuSelection, nullptr);

	for ( int i=0; i < (int)ENiagaraFlipbookViewMode::Num; ++i )
	{
		ENiagaraFlipbookViewMode ViewMode = ENiagaraFlipbookViewMode(i);
		MenuBuilder.AddMenuEntry(
			Viewport->GetCameraModeText(ViewMode),
			FText(),
			FSlateIcon(FEditorStyle::GetStyleSetName(), Viewport->GetCameraModeIconName(ViewMode)),
			FUIAction(
				FExecuteAction::CreateSP(Viewport, &SNiagaraFlipbookViewport::SetCameraMode, ViewMode),
				FCanExecuteAction(),
				FIsActionChecked::CreateSP(Viewport, &SNiagaraFlipbookViewport::IsCameraMode, ViewMode)
			),
			NAME_None,
			EUserInterfaceActionType::RadioButton
		);
	}

	return MenuBuilder.MakeWidget();
}

#undef LOCTEXT_NAMESPACE
