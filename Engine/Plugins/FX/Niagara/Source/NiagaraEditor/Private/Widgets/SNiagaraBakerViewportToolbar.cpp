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

	FNiagaraBakerViewModel* ViewModel = WeakViewModel.Pin().Get();
	check(ViewModel != nullptr);

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

	// Camera Selection
	MainBoxPtr->AddSlot()
	.AutoWidth()
	.Padding(ToolbarSlotPadding)
	[
		SNew(SEditorViewportToolbarMenu)
		.ParentToolBar(SharedThis(this))
		.Cursor(EMouseCursor::Default)
		.Label(ViewModel, &FNiagaraBakerViewModel::GetCurrentCameraModeText)
		.LabelIcon(this, &SNiagaraBakerViewportToolbar::GetCurrentCameraModeBrush)
		.OnGetMenuContent(this, &SNiagaraBakerViewportToolbar::GenerateCameraMenu)
	];

	SViewportToolBar::Construct(SViewportToolBar::FArguments());
}

const FSlateBrush* SNiagaraBakerViewportToolbar::GetCurrentCameraModeBrush() const
{
	if ( FNiagaraBakerViewModel* ViewModel = WeakViewModel.Pin().Get() )
	{
		return FEditorStyle::GetBrush(ViewModel->GetCurrentCameraModeIconName());
	}
	return nullptr;
}

TSharedRef<SWidget> SNiagaraBakerViewportToolbar::GenerateCameraMenu() const
{
	FNiagaraBakerViewModel* ViewModel = WeakViewModel.Pin().Get();
	check(ViewModel != nullptr);

	const bool bInShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bInShouldCloseWindowAfterMenuSelection, nullptr);

	for ( int i=0; i < (int)ENiagaraBakerViewMode::Num; ++i )
	{
		ENiagaraBakerViewMode ViewMode = ENiagaraBakerViewMode(i);
		MenuBuilder.AddMenuEntry(
			FNiagaraBakerViewModel::GetCameraModeText(ViewMode),
			FText(),
			FNiagaraBakerViewModel::GetCameraModeIcon(ViewMode),
			FUIAction(
				FExecuteAction::CreateSP(ViewModel, &FNiagaraBakerViewModel::SetCameraViewMode, ViewMode),
				FCanExecuteAction(),
				FIsActionChecked::CreateSP(ViewModel, &FNiagaraBakerViewModel::IsCameraViewMode, ViewMode)
			),
			NAME_None,
			EUserInterfaceActionType::RadioButton
		);
	}

	return MenuBuilder.MakeWidget();
}

#undef LOCTEXT_NAMESPACE
