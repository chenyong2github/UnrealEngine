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
#include "SparseVolumeTexture/SparseVolumeTexture.h"

#define LOCTEXT_NAMESPACE "SOpenVDBImportWindow"

#define VDB_GRID_ROW_NAME_GRID_INDEX TEXT("GridIndex")
#define VDB_GRID_ROW_NAME_GRID_TYPE TEXT("GridType")
#define VDB_GRID_ROW_NAME_GRID_NAME TEXT("GridName")
#define VDB_GRID_ROW_NAME_GRID_DIMS TEXT("GridDims")

static FText GetGridComboBoxItemText(TSharedPtr<FOpenVDBGridComponentInfo> InItem)
{
	return InItem ? FText::FromString(InItem->DisplayString) : LOCTEXT("NoneGrid", "<None>");
};

static FText GetFormatComboBoxItemText(TSharedPtr<ESparseVolumePackedDataFormat> InItem)
{
	const TCHAR* FormatStr = TEXT("<None>");
	if (InItem)
	{
		switch (*InItem)
		{
		case ESparseVolumePackedDataFormat::Unorm8: FormatStr = TEXT("8bit unorm"); break;
		case ESparseVolumePackedDataFormat::Float16: FormatStr = TEXT("16bit float"); break;
		case ESparseVolumePackedDataFormat::Float32: FormatStr = TEXT("32bit float"); break;
		}
	}
	return FText::FromString(FormatStr);
}

void SOpenVDBImportWindow::Construct(const FArguments& InArgs)
{
	PackedDataA = InArgs._PackedDataA;
	PackedDataB = InArgs._PackedDataB;
	DefaultAssignmentA = *PackedDataA;
	DefaultAssignmentB = *PackedDataB;
	bIsSequence = InArgs._NumFoundFiles > 1;
	OpenVDBGridInfo = InArgs._OpenVDBGridInfo;
	OpenVDBGridComponentInfo = InArgs._OpenVDBGridComponentInfo;
	OpenVDBSupportedTargetFormats = InArgs._OpenVDBSupportedTargetFormats;
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
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Right)
			.AutoWidth()
			.Padding(2.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ImportAsSequenceCheckBoxLabel", "Import Sequence"))
			]
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Right)
			.AutoWidth()
			.Padding(2.0f)
			[
				SAssignNew(ImportAsSequenceCheckBox, SCheckBox)
				.ToolTipText(LOCTEXT("ImportAsSequenceCheckBoxTooltip", "Import multiple sequentially labeled .vdb files as a single animated SparseVirtualTexture sequence."))
				.IsChecked(bIsSequence)
			]
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Right)
			.AutoWidth()
			.Padding(2.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Format(TEXT("Found {0} File(s)"), { InArgs._NumFoundFiles })))
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2)
		[
			SAssignNew(PackedDataAConfigurator, SOpenVDBPackedDataConfigurator)
			.PackedData(PackedDataA)
			.OpenVDBGridComponentInfo(OpenVDBGridComponentInfo)
			.OpenVDBSupportedTargetFormats(OpenVDBSupportedTargetFormats)
			.PackedDataName(LOCTEXT("OpenVDBImportWindow_PackedDataA", "Packed Data A"))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2)
		[
			SAssignNew(PackedDataBConfigurator, SOpenVDBPackedDataConfigurator)
			.PackedData(PackedDataB)
			.OpenVDBGridComponentInfo(OpenVDBGridComponentInfo)
			.OpenVDBSupportedTargetFormats(OpenVDBSupportedTargetFormats)
			.PackedDataName(LOCTEXT("OpenVDBImportWindow_PackedDataB", "Packed Data B"))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("OpenVDBImportWindow_FileInfo", "Source File Grid Info"))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2)
		[
			SNew(SBorder)
			.Padding(FMargin(3))
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			[
				SNew(SListView<TSharedPtr<FOpenVDBGridInfo>>)
				.ItemHeight(24)
				.ScrollbarVisibility(EVisibility::Visible)
				.ListItemsSource(OpenVDBGridInfo)
				.OnGenerateRow(this, &SOpenVDBImportWindow::GenerateGridInfoItemRow)
				.HeaderRow
				(
					SNew(SHeaderRow)
					+ SHeaderRow::Column(VDB_GRID_ROW_NAME_GRID_INDEX).DefaultLabel(LOCTEXT("GridIndex", "Index")).FillWidth(0.05f)
					+ SHeaderRow::Column(VDB_GRID_ROW_NAME_GRID_NAME).DefaultLabel(LOCTEXT("GridName", "Name")).FillWidth(0.15f)
					+ SHeaderRow::Column(VDB_GRID_ROW_NAME_GRID_TYPE).DefaultLabel(LOCTEXT("GridType", "Type")).FillWidth(0.1f)
					+ SHeaderRow::Column(VDB_GRID_ROW_NAME_GRID_DIMS).DefaultLabel(LOCTEXT("GridDims", "Dimensions")).FillWidth(0.25f)
				)
			]
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

bool SOpenVDBImportWindow::ShouldImportAsSequence() const
{
	return ImportAsSequenceCheckBox->IsChecked();
}

EActiveTimerReturnType SOpenVDBImportWindow::SetFocusPostConstruct(double InCurrentTime, float InDeltaTime)
{
	if (ImportButton.IsValid())
	{
		FSlateApplication::Get().SetKeyboardFocus(ImportButton, EFocusCause::SetDirectly);
	}

	return EActiveTimerReturnType::Stop;
}

TSharedRef<ITableRow> SOpenVDBImportWindow::GenerateGridInfoItemRow(TSharedPtr<FOpenVDBGridInfo> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SOpenVDBGridInfoTableRow, OwnerTable).OpenVDBGridInfo(Item);
}

bool SOpenVDBImportWindow::CanImport() const
{
	const FUintVector4& GridIndicesA = PackedDataA->SourceGridIndex;
	const FUintVector4& ComponentIndicesA = PackedDataA->SourceComponentIndex;
	const FUintVector4& GridIndicesB = PackedDataB->SourceGridIndex;
	const FUintVector4& ComponentIndicesB = PackedDataB->SourceComponentIndex;

	for (uint32 i = 0; i < 4; ++i)
	{
		const bool bGridAValid = GridIndicesA[i] != INDEX_NONE && ComponentIndicesA[i] != INDEX_NONE;
		const bool bGridBValid = GridIndicesB[i] != INDEX_NONE && ComponentIndicesB[i] != INDEX_NONE;
		if (bGridAValid || bGridBValid)
		{
			return true;
		}
	}
	return false;
}

FReply SOpenVDBImportWindow::OnResetToDefaultClick()
{
	SetDefaultGridAssignment();
	return FReply::Handled();
}

FText SOpenVDBImportWindow::GetImportTypeDisplayText() const
{
	return ShouldImportAsSequence() ? LOCTEXT("OpenVDBImportWindow_ImportTypeAnimated", "Import OpenVDB animation") : LOCTEXT("OpenVDBImportWindow_ImportTypeStatic", "Import static OpenVDB");
}

void SOpenVDBImportWindow::SetDefaultGridAssignment()
{
	check(OpenVDBGridComponentInfo);

	*PackedDataA = DefaultAssignmentA;
	*PackedDataB = DefaultAssignmentB;

	ImportAsSequenceCheckBox->SetIsChecked(bIsSequence ? ECheckBoxState::Checked : ECheckBoxState::Unchecked);
	PackedDataAConfigurator->RefreshUIFromData();
	PackedDataBConfigurator->RefreshUIFromData();
}

void SOpenVDBComponentPicker::Construct(const FArguments& InArgs)
{
	PackedData = InArgs._PackedData;
	ComponentIndex = InArgs._ComponentIndex;
	OpenVDBGridComponentInfo = InArgs._OpenVDBGridComponentInfo;
	
	check(ComponentIndex < 4);
	const TCHAR* ComponentLabels[] = { TEXT("X"), TEXT("Y"), TEXT("Z"), TEXT("W") };

	this->ChildSlot
		[
			SNew(SHorizontalBox)
			
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.FillWidth(1.0f)
			.Padding(2.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(ComponentLabels[ComponentIndex]))
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.Padding(2.0f)
			[
				SNew(SBox)
				.WidthOverride(300.0f)
				[
					SAssignNew(GridComboBox, SComboBox<TSharedPtr<FOpenVDBGridComponentInfo>>)
					.OptionsSource(OpenVDBGridComponentInfo)
					.OnGenerateWidget_Lambda([](TSharedPtr<FOpenVDBGridComponentInfo> InItem)
					{
						return SNew(STextBlock)
						.Text(GetGridComboBoxItemText(InItem));
					})
					.OnSelectionChanged_Lambda([this](TSharedPtr<FOpenVDBGridComponentInfo> InItem, ESelectInfo::Type)
					{
						if (InItem)
						{
							PackedData->SourceGridIndex[ComponentIndex] = InItem->Index;
							PackedData->SourceComponentIndex[ComponentIndex] = InItem->ComponentIndex;
						}
						else
						{
							PackedData->SourceGridIndex[ComponentIndex] = INDEX_NONE;
							PackedData->SourceComponentIndex[ComponentIndex] = INDEX_NONE;
						}
					})
					[
						SNew(STextBlock)
						.Text_Lambda([this]()
						{
							return GetGridComboBoxItemText(GridComboBox->GetSelectedItem());
						})
					]
				]
			]
		];
}

void SOpenVDBComponentPicker::RefreshUIFromData()
{
	for (const TSharedPtr<FOpenVDBGridComponentInfo>& Grid : *OpenVDBGridComponentInfo)
	{
		if (Grid->Index == PackedData->SourceGridIndex[ComponentIndex] && Grid->ComponentIndex == PackedData->SourceComponentIndex[ComponentIndex])
		{
			GridComboBox->SetSelectedItem(Grid);
			break;
		}
	}
}

void SOpenVDBPackedDataConfigurator::Construct(const FArguments& InArgs)
{
	PackedData = InArgs._PackedData;
	OpenVDBSupportedTargetFormats = InArgs._OpenVDBSupportedTargetFormats;

	for (uint32 ComponentIndex = 0; ComponentIndex < 4; ++ComponentIndex)
	{
		ComponentPickers[ComponentIndex] =
			SNew(SOpenVDBComponentPicker)
			.PackedData(PackedData)
			.ComponentIndex(ComponentIndex)
			.OpenVDBGridComponentInfo(InArgs._OpenVDBGridComponentInfo);
	}

	this->ChildSlot
		[
			SNew(SVerticalBox)
			
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(2)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.FillWidth(1.0f)
				.Padding(2.0f)
				[
					SNew(STextBlock)
					.Text(InArgs._PackedDataName)
				]

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				.Padding(2.0f)
				[
					SNew(SBox)
					.WidthOverride(50.0f)
					[
						SAssignNew(FormatComboBox, SComboBox<TSharedPtr<ESparseVolumePackedDataFormat>>)
						.OptionsSource(OpenVDBSupportedTargetFormats)
						.OnGenerateWidget_Lambda([](TSharedPtr<ESparseVolumePackedDataFormat> InItem)
						{
							return SNew(STextBlock)
							.Text(GetFormatComboBoxItemText(InItem));
						})
						.OnSelectionChanged_Lambda([this](TSharedPtr<ESparseVolumePackedDataFormat> InItem, ESelectInfo::Type)
						{
							PackedData->Format = InItem ? *InItem : ESparseVolumePackedDataFormat::Float32;
						})
						[
							SNew(STextBlock)
							.Text_Lambda([this]()
							{
								return GetFormatComboBoxItemText(FormatComboBox->GetSelectedItem());
							})
						]
					]
				]
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Right)
				.AutoWidth()
				.Padding(2.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("UnormRemapCheckBoxLabel", "Unorm Remap"))
					.IsEnabled_Lambda([this]()
					{
						return PackedData->Format == ESparseVolumePackedDataFormat::Unorm8;
					})
				]
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Right)
				.AutoWidth()
				.Padding(2.0f)
				[
					SAssignNew(RemapUnormCheckBox, SCheckBox)
					.OnCheckStateChanged_Lambda([this](ECheckBoxState CheckBoxState)
					{
						PackedData->bRemapInputForUnorm = CheckBoxState == ECheckBoxState::Checked;
					})
					.IsEnabled_Lambda([this]()
					{
						return PackedData->Format == ESparseVolumePackedDataFormat::Unorm8;
					})
					.ToolTipText(LOCTEXT("UnormRemapCheckBoxTooltip", "Remaps input values for unorm formats into the [0-1] range instead of clamping values outside this range."))
					.IsChecked(false)
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(2)
			[
				SNew(SBorder)
				.Padding(FMargin(3))
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(2)
					[
						ComponentPickers[0]->AsShared()
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(2)
					[
						ComponentPickers[1]->AsShared()
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(2)
					[
						ComponentPickers[2]->AsShared()
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(2)
					[
						ComponentPickers[3]->AsShared()
					]
				]
			]
		];
}

void SOpenVDBPackedDataConfigurator::RefreshUIFromData()
{
	for (auto& Format : *OpenVDBSupportedTargetFormats)
	{
		if (*Format == PackedData->Format)
		{
			FormatComboBox->SetSelectedItem(Format);
			break;
		}
	}
	for (uint32 i = 0; i < 4; ++i)
	{
		ComponentPickers[i]->RefreshUIFromData();
	}
	RemapUnormCheckBox->SetIsChecked(PackedData->bRemapInputForUnorm);
}

void SOpenVDBGridInfoTableRow::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTableView)
{
	OpenVDBGridInfo = InArgs._OpenVDBGridInfo;
	SMultiColumnTableRow<TSharedPtr<FOpenVDBGridInfo>>::Construct(FSuperRowType::FArguments(), OwnerTableView);
}

TSharedRef<SWidget> SOpenVDBGridInfoTableRow::GenerateWidgetForColumn(const FName& ColumnName)
{
	if (ColumnName == VDB_GRID_ROW_NAME_GRID_INDEX)
	{
		return SNew(SBox).Padding(2).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(FText::FromString(FString::Format(TEXT("{0}."), { OpenVDBGridInfo->Index })))
			];
	}
	else if (ColumnName == VDB_GRID_ROW_NAME_GRID_TYPE)
	{
		return SNew(SBox).Padding(2).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(FText::FromString(OpenVDBGridTypeToString(OpenVDBGridInfo->Type)))
			];
	}
	else if (ColumnName == VDB_GRID_ROW_NAME_GRID_NAME)
	{
		return SNew(SBox).Padding(2).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(FText::FromString(OpenVDBGridInfo->Name))
			];
	}
	else if (ColumnName == VDB_GRID_ROW_NAME_GRID_DIMS)
	{
		return SNew(SBox).Padding(2).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(FText::FromString(OpenVDBGridInfo->VolumeActiveDim.ToString()))
			];
	}
	return SNullWidget::NullWidget;
}

#undef LOCTEXT_NAMESPACE