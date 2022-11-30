// Copyright Epic Games, Inc. All Rights Reserved.

#include "OpenVDBImportWindow.h"

#include "Widgets/Input/SButton.h"
#include "SPrimaryButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#include "IDocumentation.h"
#include "Editor.h"
#include "SparseVolumeTextureOpenVDBUtility.h"
#include "OpenVDBImportOptions.h"

#define LOCTEXT_NAMESPACE "SOpenVDBImportWindow"

static FText GetGridComboBoxItemText(TSharedPtr<FOpenVDBGridInfo> InItem)
{
	return InItem ? FText::FromString(InItem->DisplayString) : LOCTEXT("NoneGrid", "<None>");
};

void SOpenVDBImportWindow::Construct(const FArguments& InArgs)
{
	ImportOptions = InArgs._ImportOptions;
	OpenVDBGridInfo = InArgs._OpenVDBGridInfo;
	WidgetWindow = InArgs._WidgetWindow;

	TSharedPtr<SBox> ImportTypeDisplay;
	TSharedPtr<SHorizontalBox> OpenVDBHeaderButtons;
	TSharedPtr<SBox> InspectorBox;
	this->ChildSlot
	[
		SNew(SBox)
		.MaxDesiredHeight(InArgs._MaxWindowHeight)
		.MaxDesiredWidth(InArgs._MaxWindowWidth)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(2)
			[
				SAssignNew(ImportTypeDisplay, SBox)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(2)
			[
				SNew(SBorder)
				.Padding(FMargin(3))
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("Import_CurrentFileTitle", "Current Asset: "))
					]
					+ SHorizontalBox::Slot()
					.Padding(5, 0, 0, 0)
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(InArgs._FullPath)
						.ToolTipText(InArgs._FullPath)
					]
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(2)
			[
				SAssignNew(InspectorBox, SBox)
				.MaxDesiredHeight(650.0f)
				.WidthOverride(400.0f)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(2)
			[
				SNew(SUniformGridPanel)
				.SlotPadding(2)
				+ SUniformGridPanel::Slot(1, 0)
				[
					SAssignNew(ImportButton, SPrimaryButton)
					.Text(LOCTEXT("OpenVDBImportWindow_Import", "Import"))
					.IsEnabled(this, &SOpenVDBImportWindow::CanImport)
					.OnClicked(this, &SOpenVDBImportWindow::OnImport)
				]
				+ SUniformGridPanel::Slot(2, 0)
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.Text(LOCTEXT("OpenVDBImportWindow_Cancel", "Cancel"))
					.ToolTipText(LOCTEXT("OpenVDBImportWindow_Cancel_ToolTip", "Cancels importing this OpenVDB file"))
					.OnClicked(this, &SOpenVDBImportWindow::OnCancel)
				]
			]
		]
	];

	InspectorBox->SetContent(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2)
		[
			CreateGridSelector(DensityGridComboBox, DensityGridCheckBox, ImportOptions->Density, LOCTEXT("DensityGridIndex", "Density"))
		]
	);

	SetDefaultGridAssignment();

	ImportTypeDisplay->SetContent(
		SNew(SBorder)
		.Padding(FMargin(3))
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(this, &SOpenVDBImportWindow::GetImportTypeDisplayText)
			]
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				IDocumentation::Get()->CreateAnchor(FString("Engine/Content/OpenVDB/ImportWindow"))
			]
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Right)
			[
				SAssignNew(OpenVDBHeaderButtons, SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(FMargin(2.0f, 0.0f))
				[
					SNew(SButton)
					.Text(LOCTEXT("OpenVDBImportWindow_ResetOptions", "Reset to Default"))
					.OnClicked(this, &SOpenVDBImportWindow::OnResetToDefaultClick)
				]
			]
		]
	);

	RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SOpenVDBImportWindow::SetFocusPostConstruct));
}

FReply SOpenVDBImportWindow::OnImport()
{
	bShouldImport = true;
	if (WidgetWindow.IsValid())
	{
		WidgetWindow.Pin()->RequestDestroyWindow();
	}
	return FReply::Handled();
}

FReply SOpenVDBImportWindow::OnCancel()
{
	bShouldImport = false;
	if (WidgetWindow.IsValid())
	{
		WidgetWindow.Pin()->RequestDestroyWindow();
	}
	return FReply::Handled();
}

bool SOpenVDBImportWindow::ShouldImport() const
{
	return bShouldImport;
}

EActiveTimerReturnType SOpenVDBImportWindow::SetFocusPostConstruct(double InCurrentTime, float InDeltaTime)
{
	if (ImportButton.IsValid())
	{
		FSlateApplication::Get().SetKeyboardFocus(ImportButton, EFocusCause::SetDirectly);
	}

	return EActiveTimerReturnType::Stop;
}

bool SOpenVDBImportWindow::CanImport() const
{
	return DensityGridCheckBox->IsChecked();
}

FReply SOpenVDBImportWindow::OnResetToDefaultClick()
{
	SetDefaultGridAssignment();
	return FReply::Handled();
}

FText SOpenVDBImportWindow::GetImportTypeDisplayText() const
{
	return LOCTEXT("OpenVDBImportWindow_ImportType", "Import Static OpenVDB");
}

void SOpenVDBImportWindow::SetDefaultGridAssignment()
{
	check(OpenVDBGridInfo);

	auto SetDefaultGrid = [](const TArray<TSharedPtr<FOpenVDBGridInfo>>& Grids, const FString& Name, EOpenVDBGridFormat Format, 
		TSharedPtr<SComboBox<TSharedPtr<FOpenVDBGridInfo>>> ComboBox, TSharedPtr<SCheckBox> CheckBox, FOpenVDBImportChannel& Channel)
	{
		TSharedPtr<FOpenVDBGridInfo> SelectedGrid = nullptr;
		// Search by name first...
		for (auto& Grid : Grids)
		{
			if (Grid->Name == Name)
			{
				SelectedGrid = Grid;
				break;
			}
		}

		// ...and if that fails, just take the first grid with matching format.
		if (!SelectedGrid)
		{
			for (auto& Grid : Grids)
			{
				if (Grid->Format == Format)
				{
					SelectedGrid = Grid;
					break;
				}
			}
		}

		if (SelectedGrid)
		{
			const bool bImport = SelectedGrid->Name == Name; // disable importing this grid if the name doesn't match
			ComboBox->SetSelectedItem(SelectedGrid);
			CheckBox->SetIsChecked(bImport);
			Channel.Name = SelectedGrid->Name;
			Channel.Index = SelectedGrid->Index;
			Channel.bImport = bImport;
		}
		else
		{
			ComboBox->ClearSelection();
			CheckBox->SetIsChecked(false);
			Channel.Name.Reset();
			Channel.Index = INDEX_NONE;
			Channel.bImport = false;
		}
	};

	SetDefaultGrid(*OpenVDBGridInfo, TEXT("density"), EOpenVDBGridFormat::Float, DensityGridComboBox, DensityGridCheckBox, ImportOptions->Density);
}

TSharedRef<SWidget> SOpenVDBImportWindow::CreateGridSelector(TSharedPtr<SComboBox<TSharedPtr<FOpenVDBGridInfo>>>& ComboBox, TSharedPtr<SCheckBox>& CheckBox, FOpenVDBImportChannel& Channel, const FText& Label)
{
	return
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.AutoWidth()
		.Padding(2.0f)
		[
			SAssignNew(CheckBox, SCheckBox)
			.IsChecked(true)
			.OnCheckStateChanged_Lambda([&Channel](ECheckBoxState CheckBoxState)
			{
				Channel.bImport = CheckBoxState == ECheckBoxState::Checked;
			})
		]

		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.FillWidth(1.0f)
		.Padding(2.0f)
		[
			SNew(STextBlock)
			.Text(Label)
		]

		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		.Padding(2.0f)
		[
			SNew(SBox)
			.WidthOverride(300.0f)
			[
				SAssignNew(ComboBox, SComboBox<TSharedPtr<FOpenVDBGridInfo>>)
				.OptionsSource(OpenVDBGridInfo)
				.IsEnabled_Lambda([&CheckBox]()
				{
					return CheckBox->IsChecked();
				})
				.OnGenerateWidget_Lambda([](TSharedPtr<FOpenVDBGridInfo> InItem)
				{
					return SNew(STextBlock)
					.Text(GetGridComboBoxItemText(InItem));
				})
				.OnSelectionChanged_Lambda([&Channel](TSharedPtr<FOpenVDBGridInfo> InItem, ESelectInfo::Type)
				{
					if (InItem)
					{
						Channel.Name = InItem->Name;
						Channel.Index = InItem->Index;
					}
					else
					{
						Channel.Name.Reset();
						Channel.Index = INDEX_NONE;
					}
				})
				[
					SNew(STextBlock)
					.Text_Lambda([&ComboBox]()
					{
						return GetGridComboBoxItemText(ComboBox->GetSelectedItem());
					})
				]
			]
		];
}

#undef LOCTEXT_NAMESPACE
