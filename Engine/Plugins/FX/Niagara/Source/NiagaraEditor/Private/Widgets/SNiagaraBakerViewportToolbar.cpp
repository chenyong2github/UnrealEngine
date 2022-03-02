// Copyright Epic Games, Inc. All Rights Reserved.

#include "SNiagaraBakerViewportToolbar.h"
#include "SNiagaraBakerViewport.h"
#include "ViewModels/NiagaraBakerViewModel.h"

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

#define LOCTEXT_NAMESPACE "SNiagaraBakerViewportToolbar"

void SNiagaraBakerViewportToolbar::Construct(const FArguments& InArgs)
{
	static const FName DefaultForegroundName("DefaultForeground");
	const FMargin ToolbarSlotPadding(2.0f, 2.0f);
	const FMargin ToolbarButtonPadding(2.0f, 0.0f);

	WeakViewModel = InArgs._WeakViewModel;
	WeakViewport = InArgs._WeakViewport;

	TSharedPtr<SHorizontalBox> MainBoxPtr;
	SNiagaraBakerViewport* Viewport = WeakViewport.Pin().Get();
	check(Viewport != nullptr);

	ChildSlot
	[
		SNew( SBorder )
		.BorderImage( FEditorStyle::GetBrush("NoBorder") )
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
		.OnGetMenuContent(this, &SNiagaraBakerViewportToolbar::GenerateOptionsMenu)
	];

	// Camera Selection
	MainBoxPtr->AddSlot()
	.AutoWidth()
	.Padding(ToolbarSlotPadding)
	[
		SNew(SEditorViewportToolbarMenu)
		.ParentToolBar(SharedThis(this))
		.Cursor(EMouseCursor::Default)
		.Label(Viewport, &SNiagaraBakerViewport::GetActiveCameraModeText)
		.LabelIcon(Viewport, &SNiagaraBakerViewport::GetActiveCameraModeIcon)
		.OnGetMenuContent(this, &SNiagaraBakerViewportToolbar::GenerateCameraMenu)
	];

	// Capture button
	MainBoxPtr->AddSlot()
	.AutoWidth()
	.Padding(ToolbarSlotPadding)
	[
		SNew(SEditorViewportToolBarButton)
		.Cursor(EMouseCursor::Default)
		.ButtonType(EUserInterfaceActionType::Button)
		.ButtonStyle(&FEditorStyle::Get().GetWidgetStyle<FButtonStyle>("EditorViewportToolBar.WarningButton"))
		.OnClicked(
			FOnClicked::CreateLambda(
				[WeakViewModel=WeakViewModel]()
				{
					if ( auto ViewModel = WeakViewModel.Pin() )
					{
						ViewModel->RenderBaker();
					}
					return FReply::Handled();
				}
			)
		)
		.ToolTipText(LOCTEXT("BakeToolTip", "Runs the bake process."))
		.Content()
		[
			SNew(STextBlock)
			.Font(FEditorStyle::GetFontStyle("EditorViewportToolBar.Font"))
			.Text(LOCTEXT("Bake", "Bake"))
			.ColorAndOpacity(FLinearColor::White)
		]
	];
	
	SViewportToolBar::Construct(SViewportToolBar::FArguments());
}

TSharedRef<SWidget> SNiagaraBakerViewportToolbar::GenerateOptionsMenu() const
{
	const bool bInShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bInShouldCloseWindowAfterMenuSelection, nullptr);

	// Show options for preview & flip book
	MenuBuilder.BeginSection(NAME_None, LOCTEXT("ShowOptions", "Show Options"));
	{
		MenuBuilder.AddMenuEntry(
			FText(LOCTEXT("AlphaBlend", "Alpha Blend")),
			FText(LOCTEXT("AlphaBlendTooltip", "If we should use alpha blend or opaque to render previews.")),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda(
					[WeakViewport=WeakViewport]()
					{
						if ( auto Viewport = WeakViewport.Pin() )
						{
							Viewport->SetAlphaBlendEnabled(!Viewport->IsAlphaBlendEnabled());
						}
					}
				),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda(
					[WeakViewport = WeakViewport]()
					{
						auto Viewport = WeakViewport.Pin();
						return Viewport && Viewport->IsAlphaBlendEnabled();
					}
				)
			),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);

		MenuBuilder.AddMenuEntry(
			FText(LOCTEXT("Checkerboard", "Checkboard")),
			FText(LOCTEXT("CheckerboardTooltip", "Should the background be a checkerboard or not.")),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda(
					[WeakViewport=WeakViewport]()
					{
						if ( auto Viewport = WeakViewport.Pin() )
						{
							Viewport->SetCheckerboardEnabled(!Viewport->IsCheckerboardEnabled());
						}
					}
				),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda(
					[WeakViewport = WeakViewport]()
					{
						auto Viewport = WeakViewport.Pin();
						return Viewport && Viewport->IsCheckerboardEnabled();
					}
				)
			),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);

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
			FText(LOCTEXT("ShowBaker", "Baker")),
			FText(LOCTEXT("ShowBakerTooltip", "When enabled shows a the generated Baker texture.")),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda(
					[WeakViewport=WeakViewport]()
					{ 
						if ( auto Viewport = WeakViewport.Pin())
						{
							Viewport->SetBakerViewEnabled(!Viewport->IsBakerViewEnabled());
						}
					}
				),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda(
					[WeakViewport = WeakViewport]()
					{
						auto Viewport = WeakViewport.Pin();
						return Viewport && Viewport->IsBakerViewEnabled();
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
		const UNiagaraBakerSettings* BakerSettings = ViewModel->GetBakerSettings();
		for ( int32 i=0; i < BakerSettings->OutputTextures.Num(); ++i )
		{
			TStringBuilder<128> TextureName;
			TextureName.Appendf(TEXT("Texture(%d)"), i);
			if (UTexture2D* GeneratedTexture = BakerSettings->OutputTextures[i].GeneratedTexture)
			{
				TextureName.Appendf(TEXT(" - %s"), *BakerSettings->OutputTextures[i].GeneratedTexture->GetName());
			}
			
			MenuBuilder.AddMenuEntry(
				FText::FromStringView(TextureName.ToView()),
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

TSharedRef<SWidget> SNiagaraBakerViewportToolbar::GenerateCameraMenu() const
{
	SNiagaraBakerViewport* Viewport = WeakViewport.Pin().Get();
	check(Viewport != nullptr);

	const bool bInShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bInShouldCloseWindowAfterMenuSelection, nullptr);

	for ( int i=0; i < (int)ENiagaraBakerViewMode::Num; ++i )
	{
		ENiagaraBakerViewMode ViewMode = ENiagaraBakerViewMode(i);
		MenuBuilder.AddMenuEntry(
			Viewport->GetCameraModeText(ViewMode),
			FText(),
			FSlateIcon(FEditorStyle::GetStyleSetName(), Viewport->GetCameraModeIconName(ViewMode)),
			FUIAction(
				FExecuteAction::CreateSP(Viewport, &SNiagaraBakerViewport::SetCameraMode, ViewMode),
				FCanExecuteAction(),
				FIsActionChecked::CreateSP(Viewport, &SNiagaraBakerViewport::IsCameraMode, ViewMode)
			),
			NAME_None,
			EUserInterfaceActionType::RadioButton
		);
	}

	return MenuBuilder.MakeWidget();
}

#undef LOCTEXT_NAMESPACE
