// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataLayerPropertyTypeCustomization.h"
#include "DataLayer/DataLayerDragDropOp.h"
#include "DataLayer/DataLayerEditorSubsystem.h"
#include "WorldPartition/DataLayer/DataLayer.h"
#include "Algo/Accumulate.h"
#include "Modules/ModuleManager.h"
#include "PropertyHandle.h"
#include "DetailWidgetRow.h"
#include "Widgets/SWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "SDropTarget.h"
#include "Editor.h"
#include "EditorFontGlyphs.h"
#include "LevelEditor.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"

#define LOCTEXT_NAMESPACE "DataLayer"

void FDataLayerPropertyTypeCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	PropertyHandle = StructPropertyHandle->GetChildHandle("Name");

	HeaderRow
	.NameContent()
	[
		StructPropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	.MaxDesiredWidth(TOptional<float>())
	[
		SNew(SDropTarget)
		.OnDrop(this, &FDataLayerPropertyTypeCustomization::OnDrop)
		.OnAllowDrop(this, &FDataLayerPropertyTypeCustomization::OnVerifyDrag)
		.OnIsRecognized(this, &FDataLayerPropertyTypeCustomization::OnVerifyDrag)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(SImage)
				.Image(FEditorStyle::GetBrush(TEXT("DataLayer.Icon16x")))
				.ColorAndOpacity(FSlateColor::UseForeground())
			]

			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.Padding(3.0f, 0.0f)
			.FillWidth(1.0f)
			[
				SNew(SComboButton)
				.ToolTipText(LOCTEXT("ComboButtonTip", "Drag and drop a DataLayer onto this property, or choose one from the drop down."))
				.OnGetMenuContent(this, &FDataLayerPropertyTypeCustomization::OnGetDataLayerMenu)
				.ButtonStyle(FEditorStyle::Get(), "NoBorder")
				.ForegroundColor(FSlateColor::UseForeground())
				.ContentPadding(FMargin(0))
				.ButtonContent()
				[
					SNew(STextBlock)
					.Text(this, &FDataLayerPropertyTypeCustomization::GetDataLayerText)
				]
			]

			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.Padding(1.0f, 0.0f)
			.AutoWidth()
			[
				SNew(SButton)
				.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
				.ToolTipText(LOCTEXT("SelectTip", "Select all actors in this DataLayer"))
				.OnClicked(this, &FDataLayerPropertyTypeCustomization::OnSelectDataLayer)
				.Visibility(this, &FDataLayerPropertyTypeCustomization::GetSelectDataLayerVisibility)
				.ForegroundColor(FSlateColor::UseForeground())
				[
					SNew(STextBlock)
					.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.11"))
					.Text(FEditorFontGlyphs::Sign_In)
				]
			]
		]
	];
}

FText FDataLayerPropertyTypeCustomization::GetDataLayerDescription(const UDataLayer* InDataLayer) const
{
	check(InDataLayer);
	return FText::FromName(InDataLayer->GetDataLayerLabel());
}

UDataLayer* FDataLayerPropertyTypeCustomization::GetDataLayerFromPropertyHandle() const
{
	FName DataLayerName;
	if (PropertyHandle->GetValue(DataLayerName) == FPropertyAccess::Success)
	{
		UDataLayer* DataLayer = UDataLayerEditorSubsystem::Get()->GetDataLayerFromName(DataLayerName);
		return DataLayer;
	}
	return nullptr;
}

FText FDataLayerPropertyTypeCustomization::GetDataLayerText() const
{
	const UDataLayer* DataLayer = GetDataLayerFromPropertyHandle();
	return DataLayer ? GetDataLayerDescription(DataLayer) : LOCTEXT("InvalidDataLayerLabel", "<Invalid>");
}

TSharedRef<SWidget> FDataLayerPropertyTypeCustomization::OnGetDataLayerMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("OpenDataLayersBrowser", "Browse DataLayers..."),
		FText(),
		FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.DataLayers"),
		FUIAction(
			FExecuteAction::CreateSP(this, &FDataLayerPropertyTypeCustomization::OpenDataLayerBrowser)
		)
	);

	MenuBuilder.BeginSection(FName(), LOCTEXT("ExistingDataLayers", "Existing DataLayers"));
	{
		TArray<TWeakObjectPtr<UDataLayer>> AllDataLayers;
		UDataLayerEditorSubsystem::Get()->AddAllDataLayersTo(AllDataLayers);

		for (const TWeakObjectPtr<UDataLayer>& WeakDataLayer : AllDataLayers)
		{
			if (const UDataLayer* DataLayerPtr = WeakDataLayer.Get())
			{
				MenuBuilder.AddMenuEntry(
					GetDataLayerDescription(DataLayerPtr),
					FText(),
					FSlateIcon(FEditorStyle::GetStyleSetName(), "DataLayer.Icon16x"),
					FUIAction(
						FExecuteAction::CreateSP(this, &FDataLayerPropertyTypeCustomization::AssignDataLayer, DataLayerPtr)
					)
				);
			}
		}
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

EVisibility FDataLayerPropertyTypeCustomization::GetSelectDataLayerVisibility() const
{
	const UDataLayer* DataLayer = GetDataLayerFromPropertyHandle();
	return DataLayer ? EVisibility::Visible : EVisibility::Collapsed;
}

FReply FDataLayerPropertyTypeCustomization::OnSelectDataLayer()
{
	if (UDataLayer* DataLayer = GetDataLayerFromPropertyHandle())
	{
		GEditor->SelectNone(true, true);
		UDataLayerEditorSubsystem::Get()->SelectActorsInDataLayer(DataLayer, true, true);
	}
	return FReply::Handled();
}

void FDataLayerPropertyTypeCustomization::AssignDataLayer(const UDataLayer* InDataLayer)
{
	if (GetDataLayerFromPropertyHandle() != InDataLayer)
	{
		PropertyHandle->SetValue(InDataLayer ? InDataLayer->GetFName() : NAME_None);
		UDataLayerEditorSubsystem::Get()->OnDataLayerChanged().Broadcast(EDataLayerAction::Reset, NULL, NAME_None);
	}
}

void FDataLayerPropertyTypeCustomization::OpenDataLayerBrowser()
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
	LevelEditorModule.GetLevelEditorTabManager()->TryInvokeTab(FTabId("LevelEditorDataLayerBrowser"));
}

FReply FDataLayerPropertyTypeCustomization::OnDrop(TSharedPtr<FDragDropOperation> InDragDrop)
{
	if (InDragDrop.IsValid() && InDragDrop->IsOfType<FDataLayerDragDropOp>())
	{
		TSharedPtr<const FDataLayerDragDropOp> DataLayerDragDropOp = InDragDrop->CastTo<FDataLayerDragDropOp>();
		if (DataLayerDragDropOp.IsValid())
		{
			const TArray<FName>& DataLayerLabels = DataLayerDragDropOp->DataLayerLabels;
			if (ensure(DataLayerLabels.Num() == 1))
			{
				if (const UDataLayer* DataLayerPtr = UDataLayerEditorSubsystem::Get()->GetDataLayerFromLabel(DataLayerLabels[0]))
				{
					AssignDataLayer(DataLayerPtr);
				}
			}
		}
	}
	return FReply::Handled();
}

bool FDataLayerPropertyTypeCustomization::OnVerifyDrag(TSharedPtr<FDragDropOperation> InDragDrop)
{
	if (InDragDrop.IsValid() && InDragDrop->IsOfType<FDataLayerDragDropOp>())
	{
		TSharedPtr<const FDataLayerDragDropOp> DataLayerDragDropOp = InDragDrop->CastTo<FDataLayerDragDropOp>();
		return DataLayerDragDropOp.IsValid() && DataLayerDragDropOp->DataLayerLabels.Num() == 1;
	}
	return false;
}

#undef LOCTEXT_NAMESPACE