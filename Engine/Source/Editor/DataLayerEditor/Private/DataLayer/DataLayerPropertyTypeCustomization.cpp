// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataLayerPropertyTypeCustomization.h"
#include "DataLayer/DataLayerPropertyTypeCustomizationHelper.h"
#include "DataLayer/DataLayerDragDropOp.h"
#include "DataLayer/DataLayerEditorSubsystem.h"
#include "DataLayerEditorModule.h"
#include "WorldPartition/DataLayer/DataLayer.h"
#include "DragAndDrop/CompositeDragDropOp.h"
#include "Algo/Accumulate.h"
#include "Modules/ModuleManager.h"
#include "PropertyHandle.h"
#include "DetailWidgetRow.h"
#include "Widgets/SWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Text/STextBlock.h"
#include "SDropTarget.h"
#include "Editor.h"
#include "LevelEditor.h"
#include "EditorFontGlyphs.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "PropertyCustomizationHelpers.h"
#include "SceneOutlinerStandaloneTypes.h"

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
		.OnDropped(this, &FDataLayerPropertyTypeCustomization::OnDrop)
		.OnAllowDrop(this, &FDataLayerPropertyTypeCustomization::OnVerifyDrag)
		.OnIsRecognized(this, &FDataLayerPropertyTypeCustomization::OnVerifyDrag)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(SImage)
				.Image(this, &FDataLayerPropertyTypeCustomization::GetDataLayerIcon)
				.ColorAndOpacity(this, &FDataLayerPropertyTypeCustomization::GetForegroundColor)
			]

			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.Padding(3.0f, 0.0f)
			.FillWidth(1.0f)
			[
				SNew(SComboButton)
				.IsEnabled_Lambda([this]
				{
					FPropertyAccess::Result PropertyAccessResult;
					const UDataLayer* DataLayer = GetDataLayerFromPropertyHandle(&PropertyAccessResult);
					return (!DataLayer || !DataLayer->IsLocked());
				})
				.ToolTipText(LOCTEXT("ComboButtonTip", "Drag and drop a Data Layer onto this property, or choose one from the drop down."))
				.OnGetMenuContent(this, &FDataLayerPropertyTypeCustomization::OnGetDataLayerMenu)
				.ButtonStyle(FEditorStyle::Get(), "NoBorder")
				.ForegroundColor(FSlateColor::UseForeground())
				.ContentPadding(FMargin(0))
				.ButtonContent()
				[
					SNew(STextBlock)
					.Text(this, &FDataLayerPropertyTypeCustomization::GetDataLayerText)
					.ColorAndOpacity(this, &FDataLayerPropertyTypeCustomization::GetForegroundColor)
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			[
				SNew(SImage)
				.Visibility_Lambda([this] 
				{
					FPropertyAccess::Result PropertyAccessResult;
					const UDataLayer* DataLayer = GetDataLayerFromPropertyHandle(&PropertyAccessResult);
					return (DataLayer && DataLayer->IsLocked()) ? EVisibility::Visible : EVisibility::Collapsed;
				})
				.ColorAndOpacity(this, &FDataLayerPropertyTypeCustomization::GetForegroundColor)
				.Image(FEditorStyle::GetBrush(TEXT("PropertyWindow.Locked")))
				.ToolTipText(LOCTEXT("LockedRuntimeDataLayerEditing", "Locked editing. (To allow editing, in Data Layer Outliner, go to Advanced -> Allow Runtime Data Layer Editing)"))
			]

			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.Padding(1.0f, 0.0f)
			.AutoWidth()
			[
				SNew(SButton)
				.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
				.ToolTipText(LOCTEXT("SelectTip", "Select all actors in this Data Layer"))
				.OnClicked(this, &FDataLayerPropertyTypeCustomization::OnSelectDataLayer)
				.Visibility(this, &FDataLayerPropertyTypeCustomization::GetSelectDataLayerVisibility)
				.ForegroundColor(FSlateColor::UseForeground())
			]

			+ SHorizontalBox::Slot()
			.Padding(2.0f, 0.0f)
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				PropertyCustomizationHelpers::MakeBrowseButton(FSimpleDelegate::CreateSP(this, &FDataLayerPropertyTypeCustomization::OnBrowse), LOCTEXT("BrowseDataLayer", "Browse in Data Layer Outliner"))
			]
		]
	];
	HeaderRow.IsEnabled(TAttribute<bool>(StructPropertyHandle, &IPropertyHandle::IsEditable));
}

void FDataLayerPropertyTypeCustomization::OnBrowse()
{
	FPropertyAccess::Result PropertyAccessResult;
	if (const UDataLayer* DataLayer = GetDataLayerFromPropertyHandle(&PropertyAccessResult))
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
		LevelEditorModule.GetLevelEditorTabManager()->TryInvokeTab(FTabId("LevelEditorDataLayerBrowser"));

		FDataLayerEditorModule& DataLayerEditorModule = FModuleManager::LoadModuleChecked<FDataLayerEditorModule>("DataLayerEditor");
		DataLayerEditorModule.SyncDataLayerBrowserToDataLayer(DataLayer);
	}
}

UDataLayer* FDataLayerPropertyTypeCustomization::GetDataLayerFromPropertyHandle(FPropertyAccess::Result* OutPropertyAccessResult) const
{
	FName DataLayerName;
	FPropertyAccess::Result Result = PropertyHandle->GetValue(DataLayerName);
	if (OutPropertyAccessResult)
	{
		*OutPropertyAccessResult = Result;
	}
	if (Result == FPropertyAccess::Success)
	{
		UDataLayer* DataLayer = UDataLayerEditorSubsystem::Get()->GetDataLayerFromName(DataLayerName);
		return DataLayer;
	}
	return nullptr;
}

const FSlateBrush* FDataLayerPropertyTypeCustomization::GetDataLayerIcon() const
{
	FPropertyAccess::Result PropertyAccessResult;
	const UDataLayer* DataLayer = GetDataLayerFromPropertyHandle(&PropertyAccessResult);
	if (!DataLayer)
	{
		return FEditorStyle::GetBrush(TEXT("DataLayer.Editor"));
	}
	if (PropertyAccessResult == FPropertyAccess::MultipleValues)
	{
		return FEditorStyle::GetBrush(TEXT("LevelEditor.Tabs.DataLayers"));
	}
	return FEditorStyle::GetBrush(DataLayer->GetDataLayerIconName());
}

FText FDataLayerPropertyTypeCustomization::GetDataLayerText() const
{
	FPropertyAccess::Result PropertyAccessResult;
	const UDataLayer* DataLayer = GetDataLayerFromPropertyHandle(&PropertyAccessResult);
	if (PropertyAccessResult == FPropertyAccess::MultipleValues)
	{
		return NSLOCTEXT("PropertyEditor", "MultipleValues", "Multiple Values");
	}
	return UDataLayer::GetDataLayerText(DataLayer);
}

FSlateColor FDataLayerPropertyTypeCustomization::GetForegroundColor() const
{
	FPropertyAccess::Result PropertyAccessResult;
	const UDataLayer* DataLayer = GetDataLayerFromPropertyHandle(&PropertyAccessResult);
	if (DataLayer && DataLayer->IsLocked())
	{
		return FSceneOutlinerCommonLabelData::DarkColor;
	}
	return FSlateColor::UseForeground();
}

TSharedRef<SWidget> FDataLayerPropertyTypeCustomization::OnGetDataLayerMenu()
{
	return FDataLayerPropertyTypeCustomizationHelper::CreateDataLayerMenu([this](const UDataLayer* DataLayer) { AssignDataLayer(DataLayer); });
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

FReply FDataLayerPropertyTypeCustomization::OnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent)
{
	TSharedPtr<const FDataLayerDragDropOp> DataLayerDragDropOp = GetDataLayerDragDropOp(InDragDropEvent.GetOperation());
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
	return FReply::Handled();
}

bool FDataLayerPropertyTypeCustomization::OnVerifyDrag(TSharedPtr<FDragDropOperation> InDragDrop)
{
	TSharedPtr<const FDataLayerDragDropOp> DataLayerDragDropOp = GetDataLayerDragDropOp(InDragDrop);
	return DataLayerDragDropOp.IsValid() && DataLayerDragDropOp->DataLayerLabels.Num() == 1;
}

TSharedPtr<const FDataLayerDragDropOp> FDataLayerPropertyTypeCustomization::GetDataLayerDragDropOp(TSharedPtr<FDragDropOperation> InDragDrop)
{
	TSharedPtr<const FDataLayerDragDropOp> DataLayerDragDropOp;
	if (InDragDrop.IsValid())
	{
		if (InDragDrop->IsOfType<FCompositeDragDropOp>())
		{
			TSharedPtr<const FCompositeDragDropOp> CompositeDragDropOp = StaticCastSharedPtr<const FCompositeDragDropOp>(InDragDrop);
			DataLayerDragDropOp = CompositeDragDropOp->GetSubOp<FDataLayerDragDropOp>();
		}
		else if (InDragDrop->IsOfType<FDataLayerDragDropOp>())
		{
			DataLayerDragDropOp = StaticCastSharedPtr<const FDataLayerDragDropOp>(InDragDrop);
		}
	}
	return DataLayerDragDropOp;
}

#undef LOCTEXT_NAMESPACE