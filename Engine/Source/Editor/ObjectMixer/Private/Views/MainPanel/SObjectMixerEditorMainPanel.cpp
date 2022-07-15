// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/MainPanel/SObjectMixerEditorMainPanel.h"

#include "ObjectMixerEditorLog.h"
#include "Views/List/ObjectMixerEditorList.h"
#include "Views/MainPanel/ObjectMixerEditorMainPanel.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "ContentBrowserModule.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IContentBrowserSingleton.h"
#include "ISinglePropertyView.h"
#include "Kismet2/SClassPickerDialog.h"
#include "PropertyEditorModule.h"
#include "SPositiveActionButton.h"
#include "Styling/StyleColors.h"

#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SSplitter.h"

#define LOCTEXT_NAMESPACE "ObjectMixerEditor"

void SObjectMixerEditorMainPanel::Construct(
	const FArguments& InArgs, const TSharedRef<FObjectMixerEditorMainPanel>& InMainPanel)
{
	check(InMainPanel->GetEditorList().IsValid());

	MainPanel = InMainPanel;
	
	ChildSlot
	[
		SNew(SVerticalBox)
			
		+SVerticalBox::Slot()
		.HAlign(HAlign_Fill)
		.AutoHeight()
		.Padding(FMargin(8.f, 0.f, 8.f, 0.f))
		[
			GeneratePanelToolbar()
		]

		+SVerticalBox::Slot()
		[
			SNew(SSplitter)
			.Orientation(Orient_Vertical)
			.ResizeMode(ESplitterResizeMode::FixedSize)

			+SSplitter::Slot()
			[
				MainPanel.Pin()->GetEditorList().Pin()->GetOrCreateWidget()
			]
		]
	];
}

SObjectMixerEditorMainPanel::~SObjectMixerEditorMainPanel()
{
	MainPanel.Reset();
	ToolbarHBox.Reset();
	ConcertButtonPtr.Reset();
}

TSharedRef<SWidget> SObjectMixerEditorMainPanel::GeneratePanelToolbar()
{
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FSinglePropertyParams Params;
	Params.NamePlacement = EPropertyNamePlacement::Hidden;
	
	return SAssignNew(ToolbarHBox, SHorizontalBox)
				
		// Add Console Variable button
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Fill)
		.HAlign(HAlign_Left)
		.Padding(FMargin(0, 4))
		[
			SNew(SPositiveActionButton)
			.Text(LOCTEXT("AddObject", "Add"))
			.OnClicked_Lambda([](){ return FReply::Handled(); })
		]

		// Filter Class Management Button
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Right)
		.Padding(FMargin(8.f, 4, 0.f, 4))
		[
			SNew(SComboButton)
			.ToolTipText(LOCTEXT("FilterClassManagementButton_Tooltip", "Select a filter class"))
			.ContentPadding(FMargin(4, 0.5f))
			.ComboButtonStyle(&FAppStyle::Get().GetWidgetStyle<FComboButtonStyle>("ComboButton"))
			.OnGetMenuContent(this, &SObjectMixerEditorMainPanel::OnGeneratePresetsMenu)
			.ForegroundColor(FStyleColors::Foreground)
			.ButtonContent()
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.Padding(0, 1, 4, 0)
				.AutoWidth()
				[
					SNew(SImage)
					.Image(FAppStyle::Get().GetBrush("Icons.Filter"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]

				+ SHorizontalBox::Slot()
				.Padding(0, 1, 0, 0)
				.AutoWidth()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("FilterClassToolbarButton", "Object Filter Class"))
				]
			]
		];
}

TSharedRef<SWidget> SObjectMixerEditorMainPanel::OnGeneratePresetsMenu()
{
	FMenuBuilder MenuBuilder(true, NULL);

	TArray<UClass*> DerivedClasses;
	GetDerivedClasses(UObjectMixerObjectFilter::StaticClass(), DerivedClasses, true);

	DerivedClasses.Remove(UObjectMixerObjectFilter::StaticClass());
	DerivedClasses.Remove(UObjectMixerBlueprintObjectFilter::StaticClass());

	if (DerivedClasses.Num())
	{
		const TSharedPtr<FObjectMixerEditorMainPanel, ESPMode::ThreadSafe> PinnedPanel = MainPanel.Pin();
		check(PinnedPanel);
		
		MenuBuilder.BeginSection(NAME_None, LOCTEXT("SelectClassMenuSection", "Select Class"));
		{
			for (UClass* DerivedClass : DerivedClasses)
			{
				MenuBuilder.AddMenuEntry(
				FText::FromName(DerivedClass->GetFName()),
					FText::GetEmpty(),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateSP(PinnedPanel.ToSharedRef(), &FObjectMixerEditorMainPanel::OnClassSelectionChanged, DerivedClass),
						FCanExecuteAction::CreateLambda([](){ return true; }),
						FIsActionChecked::CreateSP(PinnedPanel.ToSharedRef(), &FObjectMixerEditorMainPanel::IsClassSelected, DerivedClass)
					),
					NAME_None,
					EUserInterfaceActionType::RadioButton
				);
			}
		}
		MenuBuilder.EndSection();
	}
	else
	{
		MenuBuilder.AddMenuEntry(LOCTEXT("NoFilterClassesAvailable", "No filter classes available."), FText::GetEmpty(), FSlateIcon(), FUIAction());
	}

	TSharedRef<SWidget> Widget = MenuBuilder.MakeWidget();
	FChildren* ChildWidgets = Widget->GetChildren();
	for (int32 ChildItr = 0; ChildItr < ChildWidgets->Num(); ChildItr++)
	{
		TSharedRef<SWidget> Child = ChildWidgets->GetChildAt(ChildItr);

		Child->EnableToolTipForceField(false);
	}
	Widget->EnableToolTipForceField(false);
	
	return Widget;
}

#undef LOCTEXT_NAMESPACE
