// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "LandscapeEditorDetailCustomization_ProceduralLayers.h"
#include "IDetailChildrenBuilder.h"
#include "Framework/Commands/UIAction.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Misc/MessageDialog.h"
#include "Modules/ModuleManager.h"
#include "Brushes/SlateColorBrush.h"
#include "Layout/WidgetPath.h"
#include "SlateOptMacros.h"
#include "Framework/Application/MenuStack.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Notifications/SErrorText.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "EditorModeManager.h"
#include "EditorModes.h"
#include "LandscapeEditorModule.h"
#include "LandscapeEditorObject.h"
#include "Landscape.h"

#include "DetailLayoutBuilder.h"
#include "IDetailPropertyRow.h"
#include "DetailCategoryBuilder.h"
#include "PropertyCustomizationHelpers.h"

#include "SLandscapeEditor.h"
#include "Dialogs/DlgPickAssetPath.h"
#include "ObjectTools.h"
#include "ScopedTransaction.h"
#include "DesktopPlatformModule.h"
#include "AssetRegistryModule.h"

#include "LandscapeRender.h"
#include "Materials/MaterialExpressionLandscapeVisibilityMask.h"
#include "LandscapeEdit.h"
#include "IDetailGroup.h"
#include "Widgets/SBoxPanel.h"
#include "Editor/EditorStyle/Private/SlateEditorStyle.h"
#include "LandscapeEditorDetailCustomization_TargetLayers.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "LandscapeEditorCommands.h"

#define LOCTEXT_NAMESPACE "LandscapeEditor.Layers"

TSharedRef<IDetailCustomization> FLandscapeEditorDetailCustomization_ProceduralLayers::MakeInstance()
{
	return MakeShareable(new FLandscapeEditorDetailCustomization_ProceduralLayers);
}

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void FLandscapeEditorDetailCustomization_ProceduralLayers::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	IDetailCategoryBuilder& LayerCategory = DetailBuilder.EditCategory("Procedural Layers");

	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	if (LandscapeEdMode && LandscapeEdMode->CurrentToolMode != nullptr)
	{
		LayerCategory.AddCustomBuilder(MakeShareable(new FLandscapeEditorCustomNodeBuilder_ProceduralLayers(DetailBuilder.GetThumbnailPool().ToSharedRef())));
	}
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

//////////////////////////////////////////////////////////////////////////

FEdModeLandscape* FLandscapeEditorCustomNodeBuilder_ProceduralLayers::GetEditorMode()
{
	return (FEdModeLandscape*)GLevelEditorModeTools().GetActiveMode(FBuiltinEditorModes::EM_Landscape);
}

FLandscapeEditorCustomNodeBuilder_ProceduralLayers::FLandscapeEditorCustomNodeBuilder_ProceduralLayers(TSharedRef<FAssetThumbnailPool> InThumbnailPool)
	: ThumbnailPool(InThumbnailPool)
{
}

FLandscapeEditorCustomNodeBuilder_ProceduralLayers::~FLandscapeEditorCustomNodeBuilder_ProceduralLayers()
{
}

void FLandscapeEditorCustomNodeBuilder_ProceduralLayers::SetOnRebuildChildren(FSimpleDelegate InOnRegenerateChildren)
{
}

void FLandscapeEditorCustomNodeBuilder_ProceduralLayers::GenerateHeaderRowContent(FDetailWidgetRow& NodeRow)
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	
	if (LandscapeEdMode == NULL)
	{
		return;	
	}

	NodeRow.NameWidget
		[
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.Text(FText::FromString(TEXT("Layers")))
		];
}

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void FLandscapeEditorCustomNodeBuilder_ProceduralLayers::GenerateChildContent(IDetailChildrenBuilder& ChildrenBuilder)
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	if (LandscapeEdMode != NULL)
	{
		TSharedPtr<SDragAndDropVerticalBox> LayerList = SNew(SDragAndDropVerticalBox)
			.OnCanAcceptDrop(this, &FLandscapeEditorCustomNodeBuilder_ProceduralLayers::HandleCanAcceptDrop)
			.OnAcceptDrop(this, &FLandscapeEditorCustomNodeBuilder_ProceduralLayers::HandleAcceptDrop)
			.OnDragDetected(this, &FLandscapeEditorCustomNodeBuilder_ProceduralLayers::HandleDragDetected);

		LayerList->SetDropIndicator_Above(*FEditorStyle::GetBrush("LandscapeEditor.TargetList.DropZone.Above"));
		LayerList->SetDropIndicator_Below(*FEditorStyle::GetBrush("LandscapeEditor.TargetList.DropZone.Below"));

		ChildrenBuilder.AddCustomRow(FText::FromString(FString(TEXT("Procedural Layers"))))
			.Visibility(EVisibility::Visible)
			[
				LayerList.ToSharedRef()
			];

		InlineTextBlocks.Empty();
		InlineTextBlocks.AddDefaulted(LandscapeEdMode->GetProceduralLayerCount());
		for (int32 i = 0; i < LandscapeEdMode->GetProceduralLayerCount(); ++i)
		{
			TSharedPtr<SWidget> GeneratedRowWidget = GenerateRow(i);

			if (GeneratedRowWidget.IsValid())
			{
				LayerList->AddSlot()
					.AutoHeight()
					[
						GeneratedRowWidget.ToSharedRef()
					];
			}
		}
	}
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
TSharedPtr<SWidget> FLandscapeEditorCustomNodeBuilder_ProceduralLayers::GenerateRow(int32 InLayerIndex)
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	TSharedPtr<SWidget> RowWidget = SNew(SLandscapeEditorSelectableBorder)
		.Padding(0)
		.VAlign(VAlign_Center)
		.OnContextMenuOpening(this, &FLandscapeEditorCustomNodeBuilder_ProceduralLayers::OnLayerContextMenuOpening, InLayerIndex)
		.OnSelected(this, &FLandscapeEditorCustomNodeBuilder_ProceduralLayers::OnLayerSelectionChanged, InLayerIndex)
		.IsSelected(TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateSP(this, &FLandscapeEditorCustomNodeBuilder_ProceduralLayers::IsLayerSelected, InLayerIndex)))
		.Visibility(EVisibility::Visible)
		[
			SNew(SHorizontalBox)
			
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(FEditorStyle::Get(), "NoBorder")
				.OnClicked(this, &FLandscapeEditorCustomNodeBuilder_ProceduralLayers::OnToggleLock, InLayerIndex)
				.ToolTipText(LOCTEXT("FLandscapeEditorCustomNodeBuilder_ProceduralLayerLock", "Locks the current layer"))
				[
					SNew(SImage)
					.Image(this, &FLandscapeEditorCustomNodeBuilder_ProceduralLayers::GetLockBrushForLayer, InLayerIndex)
				]
			]

			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ContentPadding(0)
				.ButtonStyle(FEditorStyle::Get(), "NoBorder")
				.OnClicked(this, &FLandscapeEditorCustomNodeBuilder_ProceduralLayers::OnToggleVisibility, InLayerIndex)
				.ToolTipText(LOCTEXT("FLandscapeEditorCustomNodeBuilder_ProceduralLayerVisibility", "Toggle Layer Visibility"))
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.Content()
				[
					SNew(SImage)
					.Image(this, &FLandscapeEditorCustomNodeBuilder_ProceduralLayers::GetVisibilityBrushForLayer, InLayerIndex)
				]
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0)
			.VAlign(VAlign_Center)
			.Padding(4, 0)
			[
				SAssignNew(InlineTextBlocks[InLayerIndex], SInlineEditableTextBlock)
				.Text(this, &FLandscapeEditorCustomNodeBuilder_ProceduralLayers::GetLayerText, InLayerIndex)
				.ToolTipText(LOCTEXT("FLandscapeEditorCustomNodeBuilder_ProceduralLayers_tooltip", "Name of the Layer"))
				.OnVerifyTextChanged(FOnVerifyTextChanged::CreateSP(this, &FLandscapeEditorCustomNodeBuilder_ProceduralLayers::CanRenameProceduralLayerTo, InLayerIndex))
				.OnTextCommitted(FOnTextCommitted::CreateSP(this, &FLandscapeEditorCustomNodeBuilder_ProceduralLayers::SetProceduralLayerName, InLayerIndex))
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.Padding(0, 2)
			.HAlign(HAlign_Right)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.Padding(0)
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Left)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("FLandscapeEditorCustomNodeBuilder_ProceduralLayerAlpha", "Alpha"))
				]
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.Padding(0, 2)
				.HAlign(HAlign_Left)
				.FillWidth(1.0f)
				[
					SNew(SNumericEntryBox<float>)
					.AllowSpin(true)
					.MinValue(0.0f)
					.MaxValue(100.0f)
					.MaxSliderValue(100.0f)
					.MinDesiredValueWidth(60.0f)
					.Value(this, &FLandscapeEditorCustomNodeBuilder_ProceduralLayers::GetLayerAlpha, InLayerIndex)
					.OnValueChanged(this, &FLandscapeEditorCustomNodeBuilder_ProceduralLayers::SetLayerAlpha, InLayerIndex)
					.IsEnabled(true)
				]		
			]
		];	

	return RowWidget;
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

FText FLandscapeEditorCustomNodeBuilder_ProceduralLayers::GetLayerText(int32 InLayerIndex) const
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	if (LandscapeEdMode)
	{
		return FText::FromName(LandscapeEdMode->GetProceduralLayerName(InLayerIndex));
	}

	return FText::FromString(TEXT("None"));
}

bool FLandscapeEditorCustomNodeBuilder_ProceduralLayers::IsLayerSelected(int32 InLayerIndex)
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	if (LandscapeEdMode)
	{
		return LandscapeEdMode->GetCurrentProceduralLayerIndex() == InLayerIndex;
	}

	return false;
}

bool FLandscapeEditorCustomNodeBuilder_ProceduralLayers::CanRenameProceduralLayerTo(const FText& InNewText, FText& OutErrorMessage, int32 InLayerIndex)
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	if (LandscapeEdMode)
	{
		if (!LandscapeEdMode->CanRenameProceduralLayerTo(InLayerIndex, *InNewText.ToString()))
		{
			OutErrorMessage = LOCTEXT("RenameFailed_AlreadyExists", "This layer already exists");
			return false;
		}
	}
	return true;
}

void FLandscapeEditorCustomNodeBuilder_ProceduralLayers::SetProceduralLayerName(const FText& InText, ETextCommit::Type InCommitType, int32 InLayerIndex)
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	if (LandscapeEdMode)
	{
		const FScopedTransaction Transaction(LOCTEXT("Landscape_ProceduralLayers_Rename", "Rename Procedural Layer"));
		LandscapeEdMode->SetProceduralLayerName(InLayerIndex, *InText.ToString());
	}
}

TSharedPtr<SWidget> FLandscapeEditorCustomNodeBuilder_ProceduralLayers::OnLayerContextMenuOpening(int32 InLayerIndex)
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	ALandscape* Landscape = LandscapeEdMode ? LandscapeEdMode->GetLandscape() : nullptr;
	if (LandscapeEdMode && Landscape)
	{
		FProceduralLayer* Layer = LandscapeEdMode->GetProceduralLayer(InLayerIndex);
		TSharedRef<FLandscapeEditorCustomNodeBuilder_ProceduralLayers> SharedThis = AsShared();
		FMenuBuilder MenuBuilder(true, NULL);
		MenuBuilder.BeginSection("LandscapeEditorProceduralLayerActions", LOCTEXT("LandscapeEditorProceduralLayerActions.Heading", "Layers"));
		{
			// Create Layer
			FUIAction CreateLayerAction = FUIAction(FExecuteAction::CreateLambda([SharedThis] { SharedThis->CreateLayer(); }));
			MenuBuilder.AddMenuEntry(LOCTEXT("CreateLayer", "Create"), LOCTEXT("CreateLayerTooltip", "Create Layer"), FSlateIcon(), CreateLayerAction);

			if (Layer)
			{
				// Rename Layer
				FUIAction RenameLayerAction = FUIAction(FExecuteAction::CreateLambda([SharedThis, InLayerIndex] { SharedThis->RenameLayer(InLayerIndex); }));
				MenuBuilder.AddMenuEntry(LOCTEXT("RenameLayer", "Rename..."), LOCTEXT("RenameLayerTooltip", "Rename Layer"), FSlateIcon(), RenameLayerAction);

				if (!Layer->bLocked)
				{
					// Clear Layer
					FUIAction ClearLayerAction = FUIAction(FExecuteAction::CreateLambda([SharedThis, InLayerIndex] { SharedThis->ClearLayer(InLayerIndex); }));
					MenuBuilder.AddMenuEntry(LOCTEXT("ClearLayer", "Clear..."), LOCTEXT("ClearLayerTooltip", "Clear Layer"), FSlateIcon(), ClearLayerAction);

					if (Landscape->ProceduralLayers.Num() > 1)
					{
						// Delete Layer
						FUIAction DeleteLayerAction = FUIAction(FExecuteAction::CreateLambda([SharedThis, InLayerIndex] { SharedThis->DeleteLayer(InLayerIndex); } ));
						MenuBuilder.AddMenuEntry(LOCTEXT("DeleteLayer", "Delete..."), LOCTEXT("DeleteLayerTooltip", "Delete Layer"), FSlateIcon(), DeleteLayerAction);
					}
				}
			}
		}
		MenuBuilder.EndSection();

		MenuBuilder.BeginSection("LandscapeEditorProceduralLayerVisibility", LOCTEXT("LandscapeEditorProceduralLayerVisibility.Heading", "Visibility"));
		{
			if (Layer)
			{
				if (Layer->bVisible)
				{
					// Hide Selected Layer
					FUIAction HideSelectedLayerAction = FUIAction(FExecuteAction::CreateLambda([SharedThis, InLayerIndex] { SharedThis->OnToggleVisibility(InLayerIndex); }));
					MenuBuilder.AddMenuEntry(LOCTEXT("HideSelectedLayer", "Hide Selected"), LOCTEXT("HideSelectedLayerTooltip", "Hide Selected Layer"), FSlateIcon(), HideSelectedLayerAction);
				}
				else
				{
					// Show Selected Layer
					FUIAction ShowSelectedLayerAction = FUIAction(FExecuteAction::CreateLambda([SharedThis, InLayerIndex] { SharedThis->OnToggleVisibility(InLayerIndex); }));
					MenuBuilder.AddMenuEntry(LOCTEXT("ShowSelectedLayer", "Show Selected"), LOCTEXT("ShowSelectedLayerTooltip", "Show Selected Layer"), FSlateIcon(), ShowSelectedLayerAction);
				}

				// Show Only Selected Layer
				FUIAction ShowOnlySelectedLayerAction = FUIAction(FExecuteAction::CreateLambda([SharedThis, InLayerIndex] { SharedThis->ShowOnlySelectedLayer(InLayerIndex); }));
				MenuBuilder.AddMenuEntry(LOCTEXT("ShowOnlySelectedLayer", "Show Only Selected"), LOCTEXT("ShowOnlySelectedLayerTooltip", "Show Only Selected Layer"), FSlateIcon(), ShowOnlySelectedLayerAction);
			}

			// Show All Layers
			FUIAction ShowAllLayersAction = FUIAction(FExecuteAction::CreateLambda([SharedThis] { SharedThis->ShowAllLayers(); }));
			MenuBuilder.AddMenuEntry(LOCTEXT("ShowAllLayers", "Show All Layers"), LOCTEXT("ShowAllLayersTooltip", "Show All Layers"), FSlateIcon(), ShowAllLayersAction);
		}
		MenuBuilder.EndSection();

		return MenuBuilder.MakeWidget();
	}
	return NULL;
}

void FLandscapeEditorCustomNodeBuilder_ProceduralLayers::RenameLayer(int32 InLayerIndex)
{
	if (InlineTextBlocks.IsValidIndex(InLayerIndex) && InlineTextBlocks[InLayerIndex].IsValid())
	{
		InlineTextBlocks[InLayerIndex]->EnterEditingMode();
	}
}

void FLandscapeEditorCustomNodeBuilder_ProceduralLayers::ClearLayer(int32 InLayerIndex)
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	ALandscape* Landscape = LandscapeEdMode ? LandscapeEdMode->GetLandscape() : nullptr;
	if (Landscape)
	{
		FProceduralLayer* Layer = LandscapeEdMode->GetProceduralLayer(InLayerIndex);
		if (Layer)
		{
			EAppReturnType::Type Result = FMessageDialog::Open(EAppMsgType::YesNo, FText::Format(LOCTEXT("LandscapeMode_Message", "The layer {0} content will be completely cleared.  Continue?"), FText::FromName(Layer->Name)));
			if (Result == EAppReturnType::Yes)
			{
				const FScopedTransaction Transaction(LOCTEXT("Landscape_ProceduralLayers_Clean", "Clean Procedural Layer"));
				Landscape->ClearProceduralLayer(InLayerIndex);
			}
		}
	}
}

void FLandscapeEditorCustomNodeBuilder_ProceduralLayers::DeleteLayer(int32 InLayerIndex)
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	ALandscape* Landscape = LandscapeEdMode ? LandscapeEdMode->GetLandscape() : nullptr;
	if (Landscape && Landscape->ProceduralLayers.Num() > 1)
	{
		FProceduralLayer* Layer = LandscapeEdMode->GetProceduralLayer(InLayerIndex);
		if (Layer)
		{
			EAppReturnType::Type Result = FMessageDialog::Open(EAppMsgType::YesNo, FText::Format(LOCTEXT("LandscapeMode_Message", "The layer {0} will be deleted.  Continue?"), FText::FromName(Layer->Name)));
			if (Result == EAppReturnType::Yes)
			{
				const FScopedTransaction Transaction(LOCTEXT("Landscape_ProceduralLayers_Delete", "Delete Procedural Layer"));
				Landscape->DeleteProceduralLayer(InLayerIndex);
				int32 NewLayerSelectionIndex = Landscape->GetProceduralLayer(InLayerIndex) ? InLayerIndex : 0;
				OnLayerSelectionChanged(NewLayerSelectionIndex);
				LandscapeEdMode->RefreshDetailPanel();
			}
		}
	}
}

void FLandscapeEditorCustomNodeBuilder_ProceduralLayers::ShowOnlySelectedLayer(int32 InLayerIndex)
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	ALandscape* Landscape = LandscapeEdMode ? LandscapeEdMode->GetLandscape() : nullptr;
	if (Landscape)
	{
		const FScopedTransaction Transaction(LOCTEXT("ShowOnlySelectedLayer", "Show Only Selected Layer"));
		Landscape->ShowOnlySelectedProceduralLayer(InLayerIndex);
	}
}

void FLandscapeEditorCustomNodeBuilder_ProceduralLayers::ShowAllLayers()
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	ALandscape* Landscape = LandscapeEdMode ? LandscapeEdMode->GetLandscape() : nullptr;
	if (Landscape)
	{
		const FScopedTransaction Transaction(LOCTEXT("ShowAllLayers", "Show All Layers"));
		Landscape->ShowAllProceduralLayers();
	}
}

void FLandscapeEditorCustomNodeBuilder_ProceduralLayers::CreateLayer()
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	ALandscape* Landscape = LandscapeEdMode ? LandscapeEdMode->GetLandscape() : nullptr;
	if (Landscape)
	{
		{
			const FScopedTransaction Transaction(LOCTEXT("Landscape_ProceduralLayers_Create", "Create Procedural Layer"));
			Landscape->CreateProceduralLayer();
		}
		LandscapeEdMode->RefreshDetailPanel();
	}
}

void FLandscapeEditorCustomNodeBuilder_ProceduralLayers::OnLayerSelectionChanged(int32 InLayerIndex)
{	
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	if (LandscapeEdMode)
	{
		LandscapeEdMode->SetCurrentProceduralLayer(InLayerIndex);
		LandscapeEdMode->UpdateTargetList();
	}
}

TOptional<float> FLandscapeEditorCustomNodeBuilder_ProceduralLayers::GetLayerAlpha(int32 InLayerIndex) const
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();

	if (LandscapeEdMode)
	{
		return LandscapeEdMode->GetProceduralLayerAlpha(InLayerIndex);
	}

	return 1.0f;
}

void FLandscapeEditorCustomNodeBuilder_ProceduralLayers::SetLayerAlpha(float InAlpha, int32 InLayerIndex)
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();

	if (LandscapeEdMode)
	{
		const FScopedTransaction Transaction(LOCTEXT("Landscape_ProceduralLayers_SetAlpha", "Set Procedural Layer Alpha"));
		LandscapeEdMode->SetProceduralLayerAlpha(InLayerIndex, InAlpha);
	}
}

FReply FLandscapeEditorCustomNodeBuilder_ProceduralLayers::OnToggleVisibility(int32 InLayerIndex)
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	if (LandscapeEdMode)
	{
		LandscapeEdMode->SetProceduralLayerVisibility(!LandscapeEdMode->IsProceduralLayerVisible(InLayerIndex), InLayerIndex);
	}
	return FReply::Handled();
}

const FSlateBrush* FLandscapeEditorCustomNodeBuilder_ProceduralLayers::GetVisibilityBrushForLayer(int32 InLayerIndex) const
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	bool bIsVisible = LandscapeEdMode && LandscapeEdMode->IsProceduralLayerVisible(InLayerIndex);
	return bIsVisible ? FEditorStyle::GetBrush("Level.VisibleIcon16x") : FEditorStyle::GetBrush("Level.NotVisibleIcon16x");
}

FReply FLandscapeEditorCustomNodeBuilder_ProceduralLayers::OnToggleLock(int32 InLayerIndex)
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	if (LandscapeEdMode)
	{
		LandscapeEdMode->SetProceduralLayerLocked(InLayerIndex, !LandscapeEdMode->IsProceduralLayerLocked(InLayerIndex));
	}
	return FReply::Handled();
	}

const FSlateBrush* FLandscapeEditorCustomNodeBuilder_ProceduralLayers::GetLockBrushForLayer(int32 InLayerIndex) const
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	bool bIsLocked = LandscapeEdMode && LandscapeEdMode->IsProceduralLayerLocked(InLayerIndex);
	return bIsLocked ? FEditorStyle::GetBrush(TEXT("PropertyWindow.Locked")) : FEditorStyle::GetBrush(TEXT("PropertyWindow.Unlocked"));
}

FReply FLandscapeEditorCustomNodeBuilder_ProceduralLayers::HandleDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, int32 SlotIndex, SVerticalBox::FSlot* Slot)
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();

	if (LandscapeEdMode != nullptr)
	{
		// TODO: handle drag & drop
	}

	return FReply::Unhandled();
}

TOptional<SDragAndDropVerticalBox::EItemDropZone> FLandscapeEditorCustomNodeBuilder_ProceduralLayers::HandleCanAcceptDrop(const FDragDropEvent& DragDropEvent, SDragAndDropVerticalBox::EItemDropZone DropZone, SVerticalBox::FSlot* Slot)
{
	// TODO: handle drag & drop
	return TOptional<SDragAndDropVerticalBox::EItemDropZone>();
}

FReply FLandscapeEditorCustomNodeBuilder_ProceduralLayers::HandleAcceptDrop(FDragDropEvent const& DragDropEvent, SDragAndDropVerticalBox::EItemDropZone DropZone, int32 SlotIndex, SVerticalBox::FSlot* Slot)
{
	// TODO: handle drag & drop
	return FReply::Unhandled();
}

#undef LOCTEXT_NAMESPACE
