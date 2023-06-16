// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/SClothEditor3DViewportToolBar.h"
#include "ChaosClothAsset/SClothEditor3DViewport.h"
#include "ChaosClothAsset/ClothEditor3DViewportClient.h"
#include "ChaosClothAsset/ClothEditorSimulationVisualization.h"
#include "ChaosClothAsset/ClothEditorCommands.h"
#include "ChaosClothAsset/ClothEditorStyle.h"
#include "EditorViewportCommands.h"
#include "Styling/AppStyle.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "Editor/UnrealEd/Public/SEditorViewportToolBarMenu.h"
#include "SEditorViewportViewMenu.h"


#define LOCTEXT_NAMESPACE "SChaosClothAssetEditor3DViewportToolBar"

void SChaosClothAssetEditor3DViewportToolBar::Construct(const FArguments& InArgs, TSharedPtr<class SChaosClothAssetEditor3DViewport> InChaosClothAssetEditor3DViewport)
{
	SCommonEditorViewportToolbarBase::Construct(SCommonEditorViewportToolbarBase::FArguments(), InChaosClothAssetEditor3DViewport);

	ChaosClothAssetEditor3DViewportPtr = InChaosClothAssetEditor3DViewport;
	CommandList = InArgs._CommandList;

	const FMargin ToolbarSlotPadding(4.0f, 1.0f);
	TSharedPtr<SHorizontalBox> MainBoxPtr;

	ChildSlot
	[
		SNew( SBorder )
		.BorderImage(FAppStyle::Get().GetBrush("EditorViewportToolBar.Background"))
		.Cursor(EMouseCursor::Default)
		[
			SNew(SVerticalBox)
			+SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew( MainBoxPtr, SHorizontalBox )
			]
			+SVerticalBox::Slot()
			.Padding(FMargin(4.0f, 3.0f, 0.0f, 0.0f))
			[
				// Display text (e.g., item being previewed)
				SNew(SRichTextBlock)
				.DecoratorStyleSet(&FAppStyle::Get())
				.Text(this, &SChaosClothAssetEditor3DViewportToolBar::GetDisplayString)
				.TextStyle(&FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("AnimViewport.MessageText"))
			]
		]
	];

	MainBoxPtr->AddSlot()
		.AutoWidth()
		.Padding(ToolbarSlotPadding)
		[
			MakeOptionsMenu()
		];

	MainBoxPtr->AddSlot()
		.Padding(ToolbarSlotPadding)
		.HAlign(HAlign_Left)
		[
			MakeDisplayToolBar(InArgs._Extenders)
		];

	MainBoxPtr->AddSlot()
		.Padding(ToolbarSlotPadding)
		.HAlign(HAlign_Right)
		[
			MakeToolBar(InArgs._Extenders)
		];
}

TSharedRef<SWidget> SChaosClothAssetEditor3DViewportToolBar::MakeOptionsMenu()
{
	return SNew(SEditorViewportToolbarMenu)
		.ParentToolBar(SharedThis(this))
		.Cursor(EMouseCursor::Default)
		.Image("EditorViewportToolBar.OptionsDropdown")
		.OnGetMenuContent(this, &SChaosClothAssetEditor3DViewportToolBar::GenerateClothViewportOptionsMenu);
}

TSharedRef<SWidget> SChaosClothAssetEditor3DViewportToolBar::MakeDisplayToolBar(const TSharedPtr<FExtender> InExtenders)
{
	TSharedRef<SEditorViewport> ViewportRef = StaticCastSharedPtr<SEditorViewport>(ChaosClothAssetEditor3DViewportPtr.Pin()).ToSharedRef();

	return SNew(SEditorViewportViewMenu, ViewportRef, SharedThis(this))
		.Cursor(EMouseCursor::Default)
		.MenuExtenders(InExtenders);
}

TSharedRef<SWidget> SChaosClothAssetEditor3DViewportToolBar::MakeToolBar(const TSharedPtr<FExtender> InExtenders)
{
	using namespace UE::Chaos::ClothAsset;

	// The following is modeled after portions of STransformViewportToolBar, which gets 
	// used in SCommonEditorViewportToolbarBase.

	// The buttons are hooked up to actual functions via command bindings in SChaosClothAssetEditor3DViewport::BindCommands(),
	// and the toolbar gets built in SChaosClothAssetEditor3DViewport::MakeViewportToolbar().

	FSlimHorizontalToolBarBuilder ToolbarBuilder(CommandList, FMultiBoxCustomization::None, InExtenders);

	// Use a custom style
	FName ToolBarStyle = "EditorViewportToolBar";
	ToolbarBuilder.SetStyle(&FAppStyle::Get(), ToolBarStyle);
	ToolbarBuilder.SetLabelVisibility(EVisibility::Collapsed);

	ToolbarBuilder.BeginSection("Visualization");
	ToolbarBuilder.BeginBlockGroup();
	{
		// TODO: Add button to toggle between sim and render meshes

		ToolbarBuilder.AddToolBarButton(FChaosClothAssetEditorCommands::Get().TogglePreviewWireframe,
			NAME_None,
			TAttribute<FText>(),
			TAttribute<FText>(),
			TAttribute<FSlateIcon>(),
			TEXT("ToggleRenderMeshWireframe"));
	}
	ToolbarBuilder.EndBlockGroup();
	ToolbarBuilder.EndSection();

	ToolbarBuilder.BeginSection("Sim Controls");
	ToolbarBuilder.BeginBlockGroup();
	{
		ToolbarBuilder.AddToolBarButton(FChaosClothAssetEditorCommands::Get().HardResetSimulation,
			NAME_None,
			TAttribute<FText>(),
			TAttribute<FText>(),
			TAttribute<FSlateIcon>(),
			FName(*FChaosClothAssetEditorCommands::HardResetSimulationIdentifier));

		ToolbarBuilder.AddToolBarButton(FChaosClothAssetEditorCommands::Get().SoftResetSimulation,
			NAME_None,
			TAttribute<FText>(),
			TAttribute<FText>(),
			TAttribute<FSlateIcon>(),
			FName(FChaosClothAssetEditorCommands::SoftResetSimulationIdentifier));

		ToolbarBuilder.AddToolBarButton(FChaosClothAssetEditorCommands::Get().ToggleSimulationSuspended,
			NAME_None,
			TAttribute<FText>(),
			TAttribute<FText>(),
			TAttribute<FSlateIcon>(),
			FName(FChaosClothAssetEditorCommands::ToggleSimulationSuspendedIdentifier));
	}
	ToolbarBuilder.EndBlockGroup();
	ToolbarBuilder.EndSection();

	return ToolbarBuilder.MakeWidget();
}

FText SChaosClothAssetEditor3DViewportToolBar::GetDisplayString() const
{
	using namespace UE::Chaos::ClothAsset;
	TSharedRef<FChaosClothAssetEditor3DViewportClient> ViewportClient = StaticCastSharedPtr<FChaosClothAssetEditor3DViewportClient>(ChaosClothAssetEditor3DViewportPtr.Pin()->GetViewportClient()).ToSharedRef();
	if (FClothEditorSimulationVisualization* const Visualization = ViewportClient->GetSimulationVisualization().Pin().Get())
	{
		return Visualization->GetDisplayString(ViewportClient->GetPreviewClothComponent());
	}
	return FText();
}

void SChaosClothAssetEditor3DViewportToolBar::ExtendOptionsMenu(FMenuBuilder& OptionsMenuBuilder) const
{
	using namespace UE::Chaos::ClothAsset;

	constexpr bool bOpenSubMenuOnClick = false;
	constexpr bool bShouldCloseWindowAfterMenuSelection = false;
	OptionsMenuBuilder.AddSubMenu(
		LOCTEXT("ChaosClothAssetEditor_SimulationVisualization", "Simulation Visualization"),
		LOCTEXT("ChaosClothAssetEditor_SimulationVisualizationToolTip", "Options to control simulation visualization"),
		FNewMenuDelegate::CreateLambda([this](FMenuBuilder& MenuBuilder)
	{
		TSharedRef<FChaosClothAssetEditor3DViewportClient> ViewportClient = StaticCastSharedPtr<FChaosClothAssetEditor3DViewportClient>(ChaosClothAssetEditor3DViewportPtr.Pin()->GetViewportClient()).ToSharedRef();
		if (FClothEditorSimulationVisualization* const Visualization = ViewportClient->GetSimulationVisualization().Pin().Get())
		{
			Visualization->ExtendViewportShowMenu(MenuBuilder, ViewportClient);
		}
	}), bOpenSubMenuOnClick, FSlateIcon(), bShouldCloseWindowAfterMenuSelection);
}

#undef LOCTEXT_NAMESPACE
