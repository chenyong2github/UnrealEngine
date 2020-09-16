// Copyright Epic Games, Inc. All Rights Reserved.

#include "ComposureActorLayerPropertyTypeCustomization.h"
#include "Algo/Accumulate.h"
#include "Modules/ModuleManager.h"
#include "PropertyHandle.h"
#include "DetailWidgetRow.h"
#include "LayersDragDropOp.h"
#include "Layers/Layer.h"
#include "Layers/LayersSubsystem.h"
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


#define LOCTEXT_NAMESPACE "ComposureActorLayerPropertyTypeCustomization"

void FComposureActorLayerPropertyTypeCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	PropertyHandle = StructPropertyHandle->GetChildHandle("Name");

	HeaderRow
	.NameContent()
	[
		StructPropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	.MaxDesiredWidth(TOptional<float>())
	.MaxDesiredWidth(TOptional<float>())
	[
		SNew(SDropTarget)
		.OnDrop(this, &FComposureActorLayerPropertyTypeCustomization::OnDrop)
		.OnAllowDrop(this, &FComposureActorLayerPropertyTypeCustomization::OnVerifyDrag)
		.OnIsRecognized(this, &FComposureActorLayerPropertyTypeCustomization::OnVerifyDrag)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(SImage)
				.Image(FEditorStyle::GetBrush(TEXT("Layer.Icon16x")))
				.ColorAndOpacity(FSlateColor::UseForeground())
			]

			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.Padding(3.0f, 0.0f)
			.FillWidth(1.0f)
			[
				SNew(SComboButton)
				.ToolTipText(LOCTEXT("ComboButtonTip", "Drag and drop a layer onto this property, or choose one from the drop down."))
				.OnGetMenuContent(this, &FComposureActorLayerPropertyTypeCustomization::OnGetLayerMenu)
				.ButtonStyle(FEditorStyle::Get(), "NoBorder")
				.ForegroundColor(FSlateColor::UseForeground())
				.ContentPadding(FMargin(0))
				.ButtonContent()
				[
					SNew(STextBlock)
					.Text(this, &FComposureActorLayerPropertyTypeCustomization::GetLayerText)
				]
			]

			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.Padding(1.0f, 0.0f)
			.AutoWidth()
			[
				SNew(SButton)
				.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
				.ToolTipText(LOCTEXT("SelectTip", "Select all actors in this layer"))
				.OnClicked(this, &FComposureActorLayerPropertyTypeCustomization::OnSelectLayer)
				.Visibility(this, &FComposureActorLayerPropertyTypeCustomization::GetSelectLayerVisibility)
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

FText GetLayerDescription(ULayer* InLayer)
{
	check(InLayer);

	int32 TotalNumActors = Algo::Accumulate(InLayer->ActorStats, 0, [](int32 Total, const FLayerActorStats& InStats){ return Total + InStats.Total; });
	return FText::Format(LOCTEXT("LayerNameFormat", "{0} ({1} {1}|plural(one=Actor, other=Actors))"), FText::FromName(InLayer->LayerName), TotalNumActors);
}

FText FComposureActorLayerPropertyTypeCustomization::GetLayerText() const
{
	FName LayerName;
	if (PropertyHandle->GetValue(LayerName) == FPropertyAccess::Success)
	{
		ULayersSubsystem* Layers = GEditor->GetEditorSubsystem<ULayersSubsystem>();
		ULayer* LayerImpl = Layers->GetLayer(LayerName);
		if (LayerImpl)
		{
			return GetLayerDescription(LayerImpl);
		}

		FText LayerNameText = FText::FromName(LayerName);
		if (LayerName == NAME_None)
		{
			return LayerNameText;
		}
		return FText::Format(LOCTEXT("InvalidLayerNameFormat", "<Invalid> ({0})"), LayerNameText);
	}

	return LOCTEXT("InvalidLayerName", "<Invalid>");
}

TSharedRef<SWidget> FComposureActorLayerPropertyTypeCustomization::OnGetLayerMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);

	FName LayerName;
	if (PropertyHandle->GetValue(LayerName) == FPropertyAccess::Success && LayerName != NAME_None)
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("ClearText", "Clear"),
			FText(),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &FComposureActorLayerPropertyTypeCustomization::AssignLayer, FName())
			)
		);
		MenuBuilder.AddMenuSeparator();
	}

	MenuBuilder.AddMenuEntry(
		LOCTEXT("OpenLayersBrowser", "Browse Layers..."),
		FText(),
		FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Layers"),
		FUIAction(
			FExecuteAction::CreateSP(this, &FComposureActorLayerPropertyTypeCustomization::OpenLayerBrowser)
		)
	);

	MenuBuilder.BeginSection(FName(), LOCTEXT("ExistingLayers", "Existing Layers"));
	{
		TArray<TWeakObjectPtr<ULayer>> AllLayers;
		ULayersSubsystem* Layers = GEditor->GetEditorSubsystem<ULayersSubsystem>();
		Layers->AddAllLayersTo(AllLayers);

		for (TWeakObjectPtr<ULayer> WeakLayer : AllLayers)
		{
			ULayer* Layer = WeakLayer.Get();
			if (Layer)
			{
				MenuBuilder.AddMenuEntry(
					GetLayerDescription(Layer),
					FText(),
					FSlateIcon(FEditorStyle::GetStyleSetName(), "Layer.Icon16x"),
					FUIAction(
						FExecuteAction::CreateSP(this, &FComposureActorLayerPropertyTypeCustomization::AssignLayer, Layer->LayerName)
					)
				);
			}
		}
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

EVisibility FComposureActorLayerPropertyTypeCustomization::GetSelectLayerVisibility() const
{
	FName LayerName;
	bool bIsVisible = PropertyHandle->GetValue(LayerName) == FPropertyAccess::Success && !LayerName.IsNone();
	return bIsVisible ? EVisibility::Visible : EVisibility::Collapsed;
}

FReply FComposureActorLayerPropertyTypeCustomization::OnSelectLayer()
{
	FName LayerName;
	if (PropertyHandle->GetValue(LayerName) == FPropertyAccess::Success)
	{
		GEditor->SelectNone(true, true);
		ULayersSubsystem* Layers = GEditor->GetEditorSubsystem<ULayersSubsystem>();
		Layers->SelectActorsInLayer(LayerName, true, true);
	}
	return FReply::Handled();
}

void FComposureActorLayerPropertyTypeCustomization::AssignLayer(FName InNewLayer)
{
	PropertyHandle->SetValue(InNewLayer);
}

void FComposureActorLayerPropertyTypeCustomization::OpenLayerBrowser()
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
	LevelEditorModule.GetLevelEditorTabManager()->TryInvokeTab(FTabId("LevelEditorLayerBrowser"));
}

FReply FComposureActorLayerPropertyTypeCustomization::OnDrop(TSharedPtr<FDragDropOperation> InDragDrop)
{
	if (InDragDrop.IsValid() && InDragDrop->IsOfType<FLayersDragDropOp>())
	{
		const TArray<FName>& LayerNames = StaticCastSharedPtr<FLayersDragDropOp>(InDragDrop)->Layers;
		if (ensure(LayerNames.Num() == 1))
		{
			AssignLayer(LayerNames[0]);
		}
	}
	return FReply::Handled();
}

bool FComposureActorLayerPropertyTypeCustomization::OnVerifyDrag(TSharedPtr<FDragDropOperation> InDragDrop)
{
	return InDragDrop.IsValid() && InDragDrop->IsOfType<FLayersDragDropOp>() && StaticCastSharedPtr<FLayersDragDropOp>(InDragDrop)->Layers.Num() == 1;
}

#undef LOCTEXT_NAMESPACE