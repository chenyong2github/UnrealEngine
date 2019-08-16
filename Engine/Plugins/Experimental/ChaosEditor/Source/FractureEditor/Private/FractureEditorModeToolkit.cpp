// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "FractureEditorModeToolkit.h"

#include "AssetRegistryModule.h"
#include "EditorModeManager.h"
#include "Engine/Selection.h"
#include "FractureEditorMode.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/MultiBox/MultiBoxDefs.h"
#include "IDetailsView.h"
#include "IDetailRootObjectCustomization.h"
#include "PropertyEditorModule.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

#include "SCreateAssetFromObject.h"

#include "FractureTool.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionActor.h"
#include "GeometryCollection/GeometryCollection.h"

#include "EditorStyleSet.h"
#include "FractureEditor.h"
#include "FractureEditorCommands.h"
#include "FractureEditorStyle.h"
#include "ScopedTransaction.h"
#include "Modules/ModuleManager.h"
#include "AssetToolsModule.h"
#include "Layers/ILayers.h"
#include "MeshAttributes.h"
#include "GeometryCollection/GeometryCollectionConversion.h"

#include "PlanarCut.h"
#include "SGeometryCollectionOutliner.h"
#include "FractureSelectionTools.h"
#include "GeometryCollection/GeometryCollectionAlgo.h"
#include "GeometryCollection/GeometryCollectionClusteringUtility.h"
#include "Editor.h"
#include "LevelEditorViewport.h"

#define LOCTEXT_NAMESPACE "FFractureEditorModeToolkit"

class FFractureRootObjectCustomization : public IDetailRootObjectCustomization
{
public:
	/** IDetailRootObjectCustomization interface */
	virtual TSharedPtr<SWidget> CustomizeObjectHeader(const UObject* InRootObject) override { return SNullWidget::NullWidget; }
	virtual bool IsObjectVisible(const UObject* InRootObject) const override { return true; }
	virtual bool ShouldDisplayHeader(const UObject* InRootObject) const override { return false; }
};

TArray<UClass*> FindFractureToolClasses()
{
	TArray<UClass*> Classes;

	for (TObjectIterator<UClass> ClassIterator; ClassIterator; ++ClassIterator)
	{
		if (ClassIterator->IsChildOf(UFractureTool::StaticClass()) && !ClassIterator->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
		{
			Classes.Add(*ClassIterator);
		}
	}

	return Classes;
}

FFractureEditorModeToolkit::FFractureEditorModeToolkit()
	: ExplodeAmount(0.0f)
	, FractureLevel(-1)
	, AutoClusterMode(EFractureAutoClusterMode::BoundingBox)
	, AutoClusterSiteCount(10)
	, ActiveTool(nullptr)
	, bFractureGroupExpanded(true)
	, bOutlinerGroupExpanded(true)
{
}

void FFractureEditorModeToolkit::Init(const TSharedPtr<IToolkitHost>& InitToolkitHost)
{
	FFractureEditorModule& FractureModule = FModuleManager::GetModuleChecked<FFractureEditorModule>("FractureEditor");

	const FFractureEditorCommands& Commands = FFractureEditorCommands::Get();
	FToolBarBuilder SelectionBuilder(GetToolkitCommands(), FMultiBoxCustomization::None, nullptr, Orient_Horizontal, true);
	SelectionBuilder.AddToolBarButton(Commands.GenerateAsset);
	SelectionBuilder.AddSeparator();
	SelectionBuilder.AddToolBarButton(Commands.SelectAll);
	SelectionBuilder.AddToolBarButton(Commands.SelectNone);
	SelectionBuilder.AddToolBarButton(Commands.SelectNeighbors);
	SelectionBuilder.AddToolBarButton(Commands.SelectSiblings);
	SelectionBuilder.AddToolBarButton(Commands.SelectAllInCluster);
	SelectionBuilder.AddToolBarButton(Commands.SelectInvert);

	FToolBarBuilder ClusterToolBuilder(GetToolkitCommands(), FMultiBoxCustomization::None, nullptr, Orient_Horizontal, true);
	ClusterToolBuilder.AddToolBarButton(Commands.AutoCluster);
	ClusterToolBuilder.AddToolBarButton(Commands.Flatten);
	//ClusterToolBuilder.AddToolBarButton(Commands.FlattenToLevel);
	ClusterToolBuilder.AddToolBarButton(Commands.Cluster);
	ClusterToolBuilder.AddToolBarButton(Commands.Uncluster);
// 	ClusterToolBuilder.AddToolBarButton(Commands.Merge);
	ClusterToolBuilder.AddToolBarButton(Commands.MoveUp);

	// FToolBarBuilder ViewToolBuilder(GetToolkitCommands(), FMultiBoxCustomization::None, nullptr, Orient_Horizontal, true);
	// ViewToolBuilder.AddToolBarButton(Commands.ViewUpOneLevel);
	// ViewToolBuilder.AddToolBarButton(Commands.ViewDownOneLevel);

	TSharedPtr<FExtender> Extender = FractureModule.GetToolBarExtensibilityManager()->GetAllExtenders();
	FToolBarBuilder FractureToolBuilder(GetToolkitCommands(), FMultiBoxCustomization::None, Extender, Orient_Horizontal, true);// , true);
	FractureToolBuilder.AddToolBarButton(Commands.Uniform);
	FractureToolBuilder.AddToolBarButton(Commands.Clustered);
	FractureToolBuilder.AddToolBarButton(Commands.Radial);
	FractureToolBuilder.AddToolBarButton(Commands.Planar);
	FractureToolBuilder.AddToolBarButton(Commands.Slice);
	FractureToolBuilder.AddToolBarButton(Commands.Brick);
// 	FractureToolBuilder.AddToolBarButton(Commands.Texture);

	FPropertyEditorModule& EditModule = FModuleManager::Get().GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FDetailsViewArgs DetailsViewArgs(
		false,  //bUpdateFromSelection=
		false, //bLockable=
		false, //bAllowSearch=
		FDetailsViewArgs::HideNameArea,
		true, //bHideSelectionTip=
		nullptr, //InNotifyHook=
		false, //InSearchInitialKeyFocus=
		NAME_None); //InViewIdentifier=
	DetailsViewArgs.DefaultsOnlyVisibility = EEditDefaultsOnlyNodeVisibility::Automatic;
	DetailsViewArgs.bShowOptions = false;
	DetailsViewArgs.bAllowMultipleTopLevelObjects = true;
	DetailsViewArgs.bShowKeyablePropertiesOption = false;
	DetailsViewArgs.bShowModifiedPropertiesOption = false;
	DetailsViewArgs.bAllowFavoriteSystem = false;
	DetailsViewArgs.bShowAnimatedPropertiesOption = false;

	DetailsView = EditModule.CreateDetailView(DetailsViewArgs);
	DetailsView->SetRootObjectCustomizationInstance(MakeShareable(new FFractureRootObjectCustomization));

	float Padding = 4.0f;
	float MorePadding = 10.0f;

	SAssignNew(ToolkitWidget, SBorder)
	.Padding(8)
	[
		SNew(SVerticalBox)

		+SVerticalBox::Slot()
		.Padding(0.0f, Padding)
		.AutoHeight()
		.HAlign(HAlign_Center)
		[
			SNew(STextBlock)
			.Text(FText(LOCTEXT("FractureEditorPanelLabel", "Fracture Editor")))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
		]

		+SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SExpandableArea)
			.AreaTitle(FText(LOCTEXT("Select", "Select")))
			.HeaderPadding(FMargin(2.0, 2.0))
			.BorderImage(FEditorStyle::Get().GetBrush("DetailsView.CategoryTop"))
			.BorderBackgroundColor( FLinearColor( .6,.6,.6, 1.0f ) )
			.BodyBorderBackgroundColor (FLinearColor( 0.8, 0.8, 0.8))
			.AreaTitleFont(FEditorStyle::Get().GetFontStyle("DetailsView.CategoryFontStyle"))
			.Padding(FMargin(MorePadding))
			.BodyContent()
			[

				SNew(SVerticalBox)

				+SVerticalBox::Slot()
				.HAlign(HAlign_Center)
				[
					SelectionBuilder.MakeWidget()
				]
			]
		]

		+SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SExpandableArea)
			.AreaTitle(FText(LOCTEXT("ViewSettings", "View")))
			.HeaderPadding(FMargin(2.0, 2.0))
			.Padding(FMargin(MorePadding))
			.BorderImage(FEditorStyle::Get().GetBrush("DetailsView.CategoryTop"))
			.BorderBackgroundColor( FLinearColor( .6,.6,.6, 1.0f ) )
			.BodyBorderBackgroundColor (FLinearColor( 1.0, 0.0, 0.0))
			.AreaTitleFont(FEditorStyle::Get().GetFontStyle("DetailsView.CategoryFontStyle"))
			.BodyContent()
			[
				SNew(SHorizontalBox)

				+SHorizontalBox::Slot()
				.FillWidth(.5)

				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Bottom)
				.AutoWidth()
				[
					SNew(SImage)
					.Image(FFractureEditorStyle::Get().GetBrush("FractureEditor.Exploded"))
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SSpinBox<int32>)
					.Style(&FFractureEditorStyle::Get().GetWidgetStyle<FSpinBoxStyle>("FractureEditor.SpinBox"))
					.PreventThrottling(true)
					.Value_Lambda([=]() -> int32 { return (int32) (ExplodeAmount * 100.0f); })
					.OnValueChanged_Lambda( [=](int32 NewValue) { this->OnSetExplodedViewValue( (float)NewValue / 100.0f); } )
					.MinValue(0)
					.MaxValue(100)
					.Delta(1)
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 18))
					.ToolTipText(LOCTEXT("FractureEditor.Exploded_Tooltip", "How much to seperate the drawing of the bones to aid in setup.  Does not effect simulation"))
				]

				+SHorizontalBox::Slot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				.AutoWidth()
				[
					SNew(STextBlock)
					.Text(FText(LOCTEXT("FractureExplodedPercentage", "%")))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 16))
				]

				+SHorizontalBox::Slot()
				.FillWidth(1)


				+SHorizontalBox::Slot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Bottom)
				.AutoWidth()
				[
					SNew(STextBlock)
					.Text_Lambda( [=]() -> FText { return FText::Format(NSLOCTEXT("FractureEditor", "TotalLevelCount", "{0}"), FText::AsNumber(GetLevelCount())); } )
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
				]

				+SHorizontalBox::Slot()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Bottom)
				.AutoWidth()
				.Padding(Padding * 0.5, 0.0, 0.0, Padding * 0.5)
				[
					SNew(SImage)
					.Image(FFractureEditorStyle::Get().GetBrush("FractureEditor.Levels"))
				]

				+SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SComboButton)
					.ContentPadding(0)
					.ButtonStyle(FEditorStyle::Get(), "ToolBar.Button")
					.ForegroundColor(FEditorStyle::Get().GetSlateColor("ToolBar.SToolBarComboButtonBlock.ComboButton.Color"))
					.ButtonContent()
					[
						SNew(STextBlock)
						.Text_Lambda( [=]() -> FText {
							if (FractureLevel < 0)
							{
								return LOCTEXT("FractureViewAllLevels", "All");
							}
							else if (FractureLevel == 0)
							{
								return LOCTEXT("FractureViewRootLevel", "Root");
							}

							return FText::Format(NSLOCTEXT("FractureEditor", "CurrentLevel", "{0}"), FText::AsNumber(FractureLevel));

						})
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 18))
						.ToolTipText(LOCTEXT("FractureEditor.Level_Tooltip", "Set the currently view level of the geometry collection"))

					]
					.OnGetMenuContent(this, &FFractureEditorModeToolkit::GetLevelViewMenuContent)
				]


				+SHorizontalBox::Slot()
				.FillWidth(1.25)

				+SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SComboButton)
					.ContentPadding(0)
					.ButtonStyle(FEditorStyle::Get(), "ToolBar.Button")
					.ForegroundColor(FEditorStyle::Get().GetSlateColor("ToolBar.SToolBarComboButtonBlock.ComboButton.Color"))
					.ButtonContent()
					[
						SNew(SImage)
						.Image(FFractureEditorStyle::Get().GetBrush("FractureEditor.Visibility"))
						.ToolTipText(LOCTEXT("FractureEditor.Visibility_Tooltip", "Toggle showing the bone colours of the geometry collection"))
					]
					.OnGetMenuContent(this, &FFractureEditorModeToolkit::GetViewMenuContent)
				]
			]
		]

		+SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SExpandableArea)
			.AreaTitle(FText(LOCTEXT("Cluster", "Cluster")))
			.HeaderPadding(FMargin(2.0, 2.0))
			.Padding(FMargin(MorePadding))
			.BorderImage(FEditorStyle::Get().GetBrush("DetailsView.CategoryTop"))
			.BorderBackgroundColor( FLinearColor( .6,.6,.6, 1.0f ) )
			.BodyBorderBackgroundColor (FLinearColor( 1.0, 0.0, 0.0))
			.AreaTitleFont(FEditorStyle::Get().GetFontStyle("DetailsView.CategoryFontStyle"))
			.BodyContent()
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Center)
				[
					ClusterToolBuilder.MakeWidget()
				]

				+SVerticalBox::Slot()
				.Padding(0.0f, MorePadding, 0.0f, 0.0f)
				.AutoHeight()
				.HAlign(HAlign_Center)
				[
					SNew(SHorizontalBox)

					+SHorizontalBox::Slot()
					.HAlign(HAlign_Right)
					.VAlign(VAlign_Center)
					.AutoWidth()
					[
						SNew(STextBlock)
						.Text(FText(LOCTEXT("FractureAutoClusterMode", "Auto Cluster Mode")))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					]

					+SHorizontalBox::Slot()
					.Padding(MorePadding, 0.0f)
					.AutoWidth()
					[
						SNew(SComboButton)
						.OnGetMenuContent(this, &FFractureEditorModeToolkit::GetAutoClusterModesMenu)
						.IsEnabled(true)
						.ButtonContent()
						[
							SNew(STextBlock)
							.Text(this, &FFractureEditorModeToolkit::GetAutoClusterModeDisplayName)
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
						]
					]

					+SHorizontalBox::Slot()
					.HAlign(HAlign_Right)
					.VAlign(VAlign_Center)
					.Padding(MorePadding, 0.0f)
					.AutoWidth()
					[
						SNew(STextBlock)
						.Text(FText(LOCTEXT("FractureAutoClusterSites", "# Sites")))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					]

					+SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SSpinBox<uint32>)
						.IsEnabled(true)
						.MinValue(1)
						.MaxValue(9999)
						.Value(this, &FFractureEditorModeToolkit::GetAutoClusterSiteCount)
						.OnValueChanged(this, &FFractureEditorModeToolkit::SetAutoClusterSiteCount)
						.ToolTipText(LOCTEXT("AutoClusterSitesTooltip", "How many groups you would like to try to autocluster into"))

					]
				]
			]
		]

		+SVerticalBox::Slot()
		[
			SNew(SSplitter)
			.Orientation( Orient_Vertical )

			+SSplitter::Slot()
			.SizeRule(TAttribute<SSplitter::ESizeRule>(this, &FFractureEditorModeToolkit::GetFractureGroupSizeRule ))
			[
				SNew(SExpandableArea)
				.AreaTitle(FText(LOCTEXT("Fracture", "Fracture")))
				.HeaderPadding(FMargin(2.0, 2.0))
				.Padding(FMargin(MorePadding))
				.BorderImage(FEditorStyle::Get().GetBrush("DetailsView.CategoryTop"))
				.BorderBackgroundColor( FLinearColor( .6,.6,.6, 1.0f ) )
				.BodyBorderBackgroundColor (FLinearColor( 1.0, 0.0, 0.0))
				.AreaTitleFont(FEditorStyle::Get().GetFontStyle("DetailsView.CategoryFontStyle"))
				.OnAreaExpansionChanged(this, &FFractureEditorModeToolkit::OnFractureGroupExpansionChanged)
				.BodyContent()
				[
					SNew(SScrollBox)

					+SScrollBox::Slot()
					.HAlign(HAlign_Center)
					[
						FractureToolBuilder.MakeWidget()
					]

					+SScrollBox::Slot()
					.Padding(0.0f, MorePadding)
					[
						DetailsView.ToSharedRef()
					]

					+SScrollBox::Slot()
					[
						SNew(SHorizontalBox)
						+SHorizontalBox::Slot()
						.FillWidth(1)

						+SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew( SButton )
							.HAlign(HAlign_Center)
							.ContentPadding(FMargin(MorePadding, Padding))
							.OnClicked(this, &FFractureEditorModeToolkit::OnFractureClicked)
							.IsEnabled( this, &FFractureEditorModeToolkit::CanExecuteFracture)
							.Text(FText(LOCTEXT("FractureFractureButton", "Fracture")))
						]

						+SHorizontalBox::Slot()
						.FillWidth(1)
					]
				]
			]


			+SSplitter::Slot()
			.SizeRule(TAttribute<SSplitter::ESizeRule>(this, &FFractureEditorModeToolkit::GetOutlinerGroupSizeRule ))
			[
				SNew(SExpandableArea)
				.AreaTitle(FText(LOCTEXT("Outliner", "Outliner")))
				.HeaderPadding(FMargin(2.0, 2.0))
				.Padding(FMargin(MorePadding))
				.BorderImage(FEditorStyle::Get().GetBrush("DetailsView.CategoryTop"))
				.BorderBackgroundColor( FLinearColor( .6,.6,.6, 1.0f ) )
				.BodyBorderBackgroundColor (FLinearColor( 1.0, 0.0, 0.0))
				.AreaTitleFont(FEditorStyle::Get().GetFontStyle("DetailsView.CategoryFontStyle"))
				.OnAreaExpansionChanged(this, &FFractureEditorModeToolkit::OnOutlinerGroupExpansionChanged)
				.BodyContent()
				[
					SAssignNew(OutlinerView, SGeometryCollectionOutliner )
					.OnBoneSelectionChanged(this, &FFractureEditorModeToolkit::OnOutlinerBoneSelectionChanged)
				]
			]
		]
	];


	// Bind Chaos Commands;
	BindCommands();

	SetActiveTool(GetActiveTool());

	FModeToolkit::Init(InitToolkitHost);

}

void FFractureEditorModeToolkit::BindCommands()
{
	const FFractureEditorCommands& Commands = FFractureEditorCommands::Get();

	ToolkitCommands->MapAction(
		Commands.SelectAll,
		FExecuteAction::CreateSP(
			this,
			&FFractureEditorModeToolkit::OnSelectByMode,
			GeometryCollection::ESelectionMode::AllGeometry
			),
		FCanExecuteAction::CreateStatic(&FFractureEditorModeToolkit::IsGeometryCollectionSelected)
		);

	ToolkitCommands->MapAction(
		Commands.SelectNone,
		FExecuteAction::CreateSP(
			this,
			&FFractureEditorModeToolkit::OnSelectByMode,
			GeometryCollection::ESelectionMode::None
			),
		FCanExecuteAction::CreateStatic(&FFractureEditorModeToolkit::IsGeometryCollectionSelected)
		);

	ToolkitCommands->MapAction(
		Commands.SelectNeighbors,
		FExecuteAction::CreateSP(
			this,
			&FFractureEditorModeToolkit::OnSelectByMode,
			GeometryCollection::ESelectionMode::Neighbors
			),
		FCanExecuteAction::CreateStatic(&FFractureEditorModeToolkit::IsGeometryCollectionSelected)
		);

	ToolkitCommands->MapAction(
		Commands.SelectSiblings,
		FExecuteAction::CreateSP(
			this,
			&FFractureEditorModeToolkit::OnSelectByMode,
			GeometryCollection::ESelectionMode::Siblings
			),
		FCanExecuteAction::CreateStatic(&FFractureEditorModeToolkit::IsGeometryCollectionSelected)
		);

	ToolkitCommands->MapAction(
		Commands.SelectAllInCluster,
		FExecuteAction::CreateSP(
			this,
			&FFractureEditorModeToolkit::OnSelectByMode,
			GeometryCollection::ESelectionMode::AllInCluster
			),
		FCanExecuteAction::CreateStatic(&FFractureEditorModeToolkit::IsGeometryCollectionSelected)
		);

	ToolkitCommands->MapAction(
		Commands.SelectInvert,
		FExecuteAction::CreateSP(
			this,
			&FFractureEditorModeToolkit::OnSelectByMode,
			GeometryCollection::ESelectionMode::InverseGeometry
			),
		FCanExecuteAction::CreateStatic(&FFractureEditorModeToolkit::IsGeometryCollectionSelected)
		);

	ToolkitCommands->MapAction(
		Commands.AutoCluster,
		FExecuteAction::CreateSP(this, &FFractureEditorModeToolkit::OnAutoCluster),
		FCanExecuteAction::CreateStatic(&FFractureEditorModeToolkit::IsGeometryCollectionSelected)
	);

	ToolkitCommands->MapAction(
		Commands.Cluster,
		FExecuteAction::CreateSP(this, &FFractureEditorModeToolkit::OnCluster),
		FCanExecuteAction::CreateStatic(&FFractureEditorModeToolkit::IsGeometryCollectionSelected)
	);

	ToolkitCommands->MapAction(
		Commands.Uncluster,
		FExecuteAction::CreateSP(this, &FFractureEditorModeToolkit::OnUncluster),
		FCanExecuteAction::CreateStatic(&FFractureEditorModeToolkit::IsGeometryCollectionSelected)
	);

	ToolkitCommands->MapAction(
		Commands.Flatten,
		FExecuteAction::CreateSP(this, &FFractureEditorModeToolkit::OnFlatten),
		FCanExecuteAction::CreateStatic(&FFractureEditorModeToolkit::IsGeometryCollectionSelected)
	);

	ToolkitCommands->MapAction(
		Commands.FlattenToLevel,
		FExecuteAction::CreateSP(this, &FFractureEditorModeToolkit::OnFlattenToLevel),
		FCanExecuteAction::CreateLambda([] { return false; })
		);

// 	ToolkitCommands->MapAction(
// 		Commands.Merge,
// 		FExecuteAction::CreateSP(this, &FFractureEditorModeToolkit::OnMerge),
// 		FCanExecuteAction::CreateLambda([] { return false; })
// 		);

	ToolkitCommands->MapAction(
		Commands.MoveUp,
		FExecuteAction::CreateSP(this, &FFractureEditorModeToolkit::OnMoveUp),
		FCanExecuteAction::CreateLambda([] { return IsGeometryCollectionSelected(); })
	);

	ToolkitCommands->MapAction(
		Commands.GenerateAsset,
		FExecuteAction::CreateSP(this, &FFractureEditorModeToolkit::GenerateAsset),
		FCanExecuteAction::CreateLambda([] { return IsStaticMeshSelected(); })
		);


	TArray<UClass*> SourceClasses = FindFractureToolClasses();
	for (UClass* Class : SourceClasses)
	{
		TSubclassOf<UFractureTool> SubclassOf = Class;
		UFractureTool* FractureTool = SubclassOf->GetDefaultObject<UFractureTool>();

		// Only Bind Commands With Legitmately Set Commands
		if (FractureTool->GetUICommandInfo())
		{
			ToolkitCommands->MapAction(
				FractureTool->GetUICommandInfo(),
				FExecuteAction::CreateSP(this, &FFractureEditorModeToolkit::SetActiveTool, FractureTool),
				FCanExecuteAction(),//::CreateLambda([]() { return true; }),
				FIsActionChecked::CreateSP(this, &FFractureEditorModeToolkit::IsActiveTool, FractureTool)
			);

			// Explicitly set the active tool to the Uniform Fracture
			if (FractureTool->GetUICommandInfo() == Commands.Uniform)
				SetActiveTool(FractureTool);
		}
	}

	ToolkitCommands->MapAction(
		Commands.ToggleShowBoneColors,
		FExecuteAction::CreateSP(this, &FFractureEditorModeToolkit::OnSetShowBoneColors),
		FCanExecuteAction(),//::CreateLambda([]() { return true; }),
		FIsActionChecked::CreateSP(this, &FFractureEditorModeToolkit::GetShowBoneColors)
	);

	ToolkitCommands->MapAction(
		Commands.ViewUpOneLevel,
		FExecuteAction::CreateSP(this, &FFractureEditorModeToolkit::ViewUpOneLevel)//,
	);

	ToolkitCommands->MapAction(
		Commands.ViewDownOneLevel,
		FExecuteAction::CreateSP(this, &FFractureEditorModeToolkit::ViewDownOneLevel)//,
	);

	ToolkitCommands->MapAction(
		Commands.ExplodeMore,
		FExecuteAction::CreateLambda([=]() { this->OnSetExplodedViewValue( FMath::Min(1.0, ExplodeAmount + .1) ); } ),
		EUIActionRepeatMode::RepeatEnabled
	);


	ToolkitCommands->MapAction(
		Commands.ExplodeLess,
		FExecuteAction::CreateLambda([=]() { this->OnSetExplodedViewValue( FMath::Max(0.0, ExplodeAmount - .1) ); } ),
		EUIActionRepeatMode::RepeatEnabled
	);
}


FName FFractureEditorModeToolkit::GetToolkitFName() const
{
	return FName("FractureEditorMode");
}

FText FFractureEditorModeToolkit::GetBaseToolkitName() const
{
	return NSLOCTEXT("FractureEditorModeToolkit", "DisplayName", "FractureEditorMode Tool");
}

class FEdMode* FFractureEditorModeToolkit::GetEditorMode() const
{
	return GLevelEditorModeTools().GetActiveMode(FFractureEditorMode::EM_FractureEditorModeId);
}

void FFractureEditorModeToolkit::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(ActiveTool);
}

float FFractureEditorModeToolkit::GetExplodedViewValue() const
{
	return ExplodeAmount;
}

int32 FFractureEditorModeToolkit::GetLevelViewValue() const
{
	return FractureLevel;
}


bool FFractureEditorModeToolkit::GetShowBoneColors() const
{
	TSet<UGeometryCollectionComponent*> GeomCompSelection;
	GetSelectedGeometryCollectionComponents(GeomCompSelection);
	if (GeomCompSelection.Num() > 0)
	{
		UGeometryCollectionComponent* Comp = GeomCompSelection.Array()[0];
		FScopedColorEdit EditBoneColor = Comp->EditBoneSelection();
		return EditBoneColor.GetShowBoneColors();
	}

	return false;
}

void FFractureEditorModeToolkit::OnSetShowBoneColors()
{
	bool OldState = GetShowBoneColors();

	TSet<UGeometryCollectionComponent*> GeomCompSelection;
	GetSelectedGeometryCollectionComponents(GeomCompSelection);
	for (UGeometryCollectionComponent* Comp : GeomCompSelection)
	{
		FScopedColorEdit EditBoneColor = Comp->EditBoneSelection();
		EditBoneColor.SetShowBoneColors(!OldState);
		Comp->MarkRenderStateDirty();
		Comp->MarkRenderDynamicDataDirty();
	}
	GCurrentLevelEditingViewportClient->Invalidate();
}


void FFractureEditorModeToolkit::OnSetExplodedViewValue(float NewValue)
{
	if ( FMath::Abs<float>( ExplodeAmount - NewValue ) >= .01f)
	{
		ExplodeAmount = NewValue;

		USelection* SelectionSet = GEditor->GetSelectedActors();

		TArray<AActor*> SelectedActors;
		SelectedActors.Reserve(SelectionSet->Num());
		SelectionSet->GetSelectedObjects(SelectedActors);

		for (AActor* Actor : SelectedActors)
		{
			TArray<UActorComponent*> Components = Actor->GetComponentsByClass(UPrimitiveComponent::StaticClass());
			for (UActorComponent* Component : Components)
			{
				UPrimitiveComponent* PrimitiveComponent = CastChecked<UPrimitiveComponent>(Component);
				AGeometryCollectionActor* GeometryCollectionActor = Cast<AGeometryCollectionActor>(Actor);
				if(GeometryCollectionActor)
				{
					UGeometryCollectionComponent* GeometryCollectionComponent = Cast<UGeometryCollectionComponent>(PrimitiveComponent);
					FGeometryCollectionEdit RestCollection = GeometryCollectionComponent->EditRestCollection();
					UGeometryCollection* GeometryCollection = RestCollection.GetRestCollection();

					UpdateExplodedVectors(GeometryCollectionComponent);

					GeometryCollectionComponent->MarkRenderStateDirty();
				}
			}
		}

		GCurrentLevelEditingViewportClient->Invalidate();
	}
}


int32 FFractureEditorModeToolkit::GetLevelCount()
{
	TSet<UGeometryCollectionComponent*> GeomCompSelection;
	GetSelectedGeometryCollectionComponents(GeomCompSelection);

	int32 ReturnLevel = -1;
	for (UGeometryCollectionComponent* Comp : GeomCompSelection)
	{
		FGeometryCollectionEdit GCEdit = Comp->EditRestCollection(GeometryCollection::EEditUpdate::None);
		if (UGeometryCollection* GCObject = GCEdit.GetRestCollection())
		{
			TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = GCObject->GetGeometryCollection();
			if (FGeometryCollection* GeometryCollection = GeometryCollectionPtr.Get())
			{
				bool HasLevelAttribute = GeometryCollection->HasAttribute("Level", FTransformCollection::TransformGroup);
				if (HasLevelAttribute)
				{
					TManagedArray<int32>& Levels = GeometryCollection->GetAttribute<int32>("Level", FTransformCollection::TransformGroup);

					if(Levels.Num() > 0)
					{
						for (int32 Level : Levels)
						{
							if (Level > ReturnLevel)
							{
								ReturnLevel = Level;
							}
						}
					}
				}
			}
		}
	}
	return ReturnLevel + 1;
}


void FFractureEditorModeToolkit::OnSetLevelViewValue(int32 NewValue)
{
	FractureLevel = NewValue;//  * (float) GetLevelCount();

	USelection* SelectionSet = GEditor->GetSelectedActors();

	TArray<AActor*> SelectedActors;
	SelectedActors.Reserve(SelectionSet->Num());
	SelectionSet->GetSelectedObjects(SelectedActors);

	TSet<UGeometryCollectionComponent*> GeomCompSelection;
	GetSelectedGeometryCollectionComponents(GeomCompSelection);

	for (UGeometryCollectionComponent* Comp : GeomCompSelection)
	{
		FScopedColorEdit EditBoneColor = Comp->EditBoneSelection();
		if(EditBoneColor.GetViewLevel() != FractureLevel)
		{
			EditBoneColor.SetLevelViewMode(FractureLevel);
			EditBoneColor.ResetBoneSelection();
			UpdateExplodedVectors(Comp);
			Comp->MarkRenderStateDirty();
			Comp->MarkRenderDynamicDataDirty();
		}
	}
	SetOutlinerComponents(GeomCompSelection.Array());

	GCurrentLevelEditingViewportClient->Invalidate();
}

void FFractureEditorModeToolkit::ViewUpOneLevel()
{
	int32 CountMax = GetLevelCount() + 1;
	int32 NewLevel = ((FractureLevel + CountMax) % CountMax) - 1;
	OnSetLevelViewValue(NewLevel);
}

void FFractureEditorModeToolkit::ViewDownOneLevel()
{
	int32 CountMax = GetLevelCount() + 1;
	int32 NewLevel = ((FractureLevel + CountMax + 2 ) % CountMax) - 1;
	OnSetLevelViewValue(NewLevel);
}

TSharedRef<SWidget> FFractureEditorModeToolkit::GetAutoClusterModesMenu()
{
	FMenuBuilder MenuBuilder(true, NULL);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("AutoClusterModeBoundingBox", "Bounding Box"),
		LOCTEXT("AutoClusterModeBoundingBoxTooltip", "Use the Bounding Box method when Auto Clustering.  This uses the overlapping of the bones bounding boxes to determine which bones to connect."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateSP( this, &FFractureEditorModeToolkit::SetAutoClusterMode, EFractureAutoClusterMode::BoundingBox),
			FCanExecuteAction(),
			FIsActionChecked::CreateLambda( [=]() -> bool { return AutoClusterMode == EFractureAutoClusterMode::BoundingBox; } )
		),
		NAME_None,
		EUserInterfaceActionType::ToggleButton
	);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("AutoClusterModeProximity", "Proximity"),
		LOCTEXT("AutoClusterModeProximityTooltip", "Use the Proximity method when Auto Clustering.  This uses actual bone edge connectivity to determine which bones to connect."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateSP( this, &FFractureEditorModeToolkit::SetAutoClusterMode, EFractureAutoClusterMode::Proximity),
			FCanExecuteAction(),
			FIsActionChecked::CreateLambda( [=]() -> bool { return AutoClusterMode == EFractureAutoClusterMode::Proximity; } )
		),
		NAME_None,
		EUserInterfaceActionType::ToggleButton
	);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("AutoClusterModeDistance", "Distance"),
		LOCTEXT("AutoClusterModeDistanceTooltip", "Use the Distance method when Auto Clustering.  This uses a simple distance calculation to determine which bones to connect"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateSP( this, &FFractureEditorModeToolkit::SetAutoClusterMode, EFractureAutoClusterMode::Distance),
			FCanExecuteAction(),
			FIsActionChecked::CreateLambda( [=]() -> bool { return AutoClusterMode == EFractureAutoClusterMode::Distance; } )
		),
		NAME_None,
		EUserInterfaceActionType::ToggleButton
	);

	//MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

void FFractureEditorModeToolkit::SetAutoClusterMode(EFractureAutoClusterMode InAutoClusterMode)
{
	AutoClusterMode = InAutoClusterMode;
}


FText FFractureEditorModeToolkit::GetAutoClusterModeDisplayName() const
{
	switch (AutoClusterMode)
	{
		case EFractureAutoClusterMode::Proximity: return LOCTEXT("AutoClusterProximity", "Proximity");
		case EFractureAutoClusterMode::Distance: return LOCTEXT("AutoClusterDistance", "Distance");
		case EFractureAutoClusterMode::BoundingBox:
		default:
			return LOCTEXT("AutoClusterBoundingBox", "Bounding Box");
	}
	return LOCTEXT("AutoClusterBoundingBox", "Bounding Box");
}


void FFractureEditorModeToolkit::SetAutoClusterSiteCount(uint32 InSiteCount)
{
	if (InSiteCount >= 1)
		AutoClusterSiteCount = InSiteCount;
}

uint32 FFractureEditorModeToolkit::GetAutoClusterSiteCount() const
{
	return AutoClusterSiteCount;
}

TSharedRef<SWidget> FFractureEditorModeToolkit::GetLevelViewMenuContent()
{
	FMenuBuilder MenuBuilder(true, GetToolkitCommands());

	MenuBuilder.AddMenuEntry(
		LOCTEXT("LevelMenuAll", "All Levels"),
		LOCTEXT("LevelMenuAllTooltip", "View All Leaf Bones in this Geometry Collection"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateSP(this, &FFractureEditorModeToolkit::OnSetLevelViewValue, -1),
			FCanExecuteAction(),
			FGetActionCheckState::CreateLambda([=] {return FractureLevel == -1 ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;})
		)
	);

	MenuBuilder.AddMenuSeparator();

	for (int32 i = 0; i < GetLevelCount(); i++)
	{
		MenuBuilder.AddMenuEntry(
			FText::Format(LOCTEXT("LevelMenuN", "Level {0}"), FText::AsNumber(i)),
			FText::Format(LOCTEXT("LevelMenuNTooltip", "View Level {0} in this Geometry Collecdtion"), FText::AsNumber(i)),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &FFractureEditorModeToolkit::OnSetLevelViewValue, i),
				FCanExecuteAction(),
				FGetActionCheckState::CreateLambda([=] {return FractureLevel == -1 ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;})
			)
		);
	}

	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> FFractureEditorModeToolkit::GetViewMenuContent()
{
	const FFractureEditorCommands& Commands = FFractureEditorCommands::Get();

	FMenuBuilder MenuBuilder(false, GetToolkitCommands());
 	MenuBuilder.AddMenuEntry(Commands.ToggleShowBoneColors);

	return MenuBuilder.MakeWidget();
}

void FFractureEditorModeToolkit::SetActiveTool(UFractureTool* InActiveTool)
{
	ActiveTool = InActiveTool;

	UFractureCommonSettings* CommonSettings = GetMutableDefault<UFractureCommonSettings>();
	CommonSettings->OwnerTool = ActiveTool;

	if (ActiveTool != nullptr)
	{
		TArray<UObject*> Settings = ActiveTool->GetSettingsObjects();
		Settings.Insert(CommonSettings, 0);

		DetailsView->SetObjects(Settings);

		ActiveTool->FractureContextChanged();
	}
}


UFractureTool* FFractureEditorModeToolkit::GetActiveTool() const
{
	return ActiveTool;
}

bool FFractureEditorModeToolkit::IsActiveTool(UFractureTool* InActiveTool)
{
	return bool(ActiveTool == InActiveTool);
}

void FFractureEditorModeToolkit::SetOutlinerComponents(const TArray<UGeometryCollectionComponent*>& InNewComponents)
{
	for (UGeometryCollectionComponent* Component : InNewComponents)
	{
		FGeometryCollectionEdit RestCollection = Component->EditRestCollection(GeometryCollection::EEditUpdate::None);
		UGeometryCollection* FracturedGeometryCollection = RestCollection.GetRestCollection();

		if (FracturedGeometryCollection) // Prevents crash when GC is deleted from content browser and actor is selected.
		{
			TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = FracturedGeometryCollection->GetGeometryCollection();

			FGeometryCollectionClusteringUtility::UpdateHierarchyLevelOfChildren(GeometryCollectionPtr.Get(), -1);
			UpdateExplodedVectors(Component);
		}
	}

	if (OutlinerView)
	{
		OutlinerView->SetComponents(InNewComponents);
	}

	if (ActiveTool != nullptr)
	{
		ActiveTool->FractureContextChanged();
	}

	// Make sure all these selected componenets are set to view the correct viewing level


	//if (InNewComponents)
	//{
		//GeometryCollection = NewGeometryCollection;

	//}
	/*else
	{
		// Possibly not necessary here... have to get notification for when the collection changed.
		OutlinerView->UpdateGeometryCollection();
	}*/
}

void FFractureEditorModeToolkit::SetBoneSelection(UGeometryCollectionComponent* InRootComponent, const TArray<int32>& InSelectedBones, bool bClearCurrentSelection)
{
	OutlinerView->SetBoneSelection(InRootComponent, InSelectedBones, bClearCurrentSelection);
	if (ActiveTool != nullptr)
	{
		ActiveTool->FractureContextChanged();
	}
}

static bool CanFractureComponent(UPrimitiveComponent* PrimitiveComponent)
{
	// Don't bother with editor-only 'helper' actors, we never want to visualize or edit geometry on those
	return !PrimitiveComponent->IsEditorOnly() &&
			PrimitiveComponent->GetCollisionEnabled() != ECollisionEnabled::NoCollision &&
			(PrimitiveComponent->GetOwner() == nullptr || !PrimitiveComponent->GetOwner()->IsEditorOnly());
}

void FFractureEditorModeToolkit::OnSelectByMode(GeometryCollection::ESelectionMode SelectionMode)
{
	USelection* SelectionSet = GEditor->GetSelectedActors();

	TArray<AActor*> SelectedActors;
	SelectedActors.Reserve(SelectionSet->Num());
	SelectionSet->GetSelectedObjects(SelectedActors);

	for (AActor* Actor : SelectedActors)
	{
		TArray<UActorComponent*> Components = Actor->GetComponentsByClass(UPrimitiveComponent::StaticClass());
		for (UActorComponent* Component : Components)
		{
			UPrimitiveComponent* PrimitiveComponent = CastChecked<UPrimitiveComponent>(Component);
			UGeometryCollectionComponent* GeometryCollectionComponent = Cast<UGeometryCollectionComponent>(PrimitiveComponent);

			if (GeometryCollectionComponent)
			{
				FScopedColorEdit EditBoneColor = GeometryCollectionComponent->EditBoneSelection();
				EditBoneColor.SelectBones(SelectionMode);
				SetBoneSelection(GeometryCollectionComponent, EditBoneColor.GetSelectedBones(), true);
			}
		}
	}
}

void FFractureEditorModeToolkit::GetFractureContexts(TArray<FFractureContext>& FractureContexts)
{
	const UFractureCommonSettings* CommonSettings = GetDefault<UFractureCommonSettings>();
	FRandomStream RandomStream(CommonSettings->RandomSeed > -1 ? CommonSettings->RandomSeed : FMath::Rand());

	USelection* SelectedActors = GEditor->GetSelectedActors();
	for (FSelectionIterator Iter(*SelectedActors); Iter; ++Iter)
	{
		AActor* Actor = Cast<AActor>(*Iter);
		if (Actor)
		{
			TArray<UActorComponent*> Components = Actor->GetComponentsByClass(UPrimitiveComponent::StaticClass());
			for (UActorComponent* Component : Components)
			{
				UPrimitiveComponent* PrimitiveComponent = CastChecked<UPrimitiveComponent>(Component);
				if (UGeometryCollectionComponent* GeometryCollectionComponent = Cast<UGeometryCollectionComponent>(PrimitiveComponent))
				{
					FGeometryCollectionEdit RestCollection = GeometryCollectionComponent->EditRestCollection(GeometryCollection::EEditUpdate::None);
					UGeometryCollection* FracturedGeometryCollection = RestCollection.GetRestCollection();
					if (FracturedGeometryCollection == nullptr)
					{
						continue;
					}

					const TArray<int32>& SelectedBonesOriginal = GeometryCollectionComponent->GetSelectedBones();

					TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = FracturedGeometryCollection->GetGeometryCollection();
					FGeometryCollection* OutGeometryCollection = GeometryCollectionPtr.Get();

					const TManagedArray<TSet<int32>>& Children = OutGeometryCollection->GetAttribute<TSet<int32>>("Children", FGeometryCollection::TransformGroup);

					TArray<int32> SelectedBones;
					SelectedBones.Reserve(SelectedBonesOriginal.Num());
					for (int32 BoneIndex : SelectedBonesOriginal)
					{
						if (Children[BoneIndex].Num() == 0)
						{
							SelectedBones.Add(BoneIndex);
						}
					}

					const TManagedArray<FTransform>& Transform = OutGeometryCollection->GetAttribute<FTransform>("Transform", FGeometryCollection::TransformGroup);
					const TManagedArray<int32>& TransformToGeometryIndex = OutGeometryCollection->GetAttribute<int32>("TransformToGeometryIndex", FGeometryCollection::TransformGroup);

					const TManagedArray<FBox>& BoundingBoxes = OutGeometryCollection->GetAttribute<FBox>("BoundingBox", FGeometryCollection::GeometryGroup);

					TArray<FTransform> Transforms;
					GeometryCollectionAlgo::GlobalMatrices(Transform, OutGeometryCollection->Parent, Transforms);

					TMap<int32, FBox> BoundsToBone;

					for (int32 Idx = 0, ni = FracturedGeometryCollection->NumElements(FGeometryCollection::TransformGroup); Idx < ni; ++Idx)
					{
						if (TransformToGeometryIndex[Idx] > -1)
						{
							ensure(TransformToGeometryIndex[Idx] > -1);
							BoundsToBone.Add(Idx, BoundingBoxes[TransformToGeometryIndex[Idx]].TransformBy(Transforms[Idx]));
						}
					}

					if (CommonSettings->bGroupFracture)
					{
						FractureContexts.AddDefaulted();
						FFractureContext& FractureContext = FractureContexts.Last();
						FractureContext.RandomSeed = FMath::Rand();
						if (CommonSettings->RandomSeed > -1)
						{
							// make sure it's unique for each context if it's specified.
							FractureContext.RandomSeed = CommonSettings->RandomSeed + FractureContexts.Num();
						}

						FractureContext.OriginalActor = Actor;
						FractureContext.Transform = Actor->GetActorTransform();
						FractureContext.OriginalPrimitiveComponent = GeometryCollectionComponent;
						FractureContext.FracturedGeometryCollection = FracturedGeometryCollection;
						FractureContext.SelectedBones = SelectedBones;

						FractureContext.Bounds = FBox(ForceInit);
						for (int32 BoneIndex : FractureContext.SelectedBones)
						{
							if (FractureContext.SelectedBones.Num() > 1 && RandomStream.FRand() > CommonSettings->ChanceToFracture)
							{
								continue;
							}

							if (TransformToGeometryIndex[BoneIndex] > -1)
							{
								FractureContext.Bounds += BoundsToBone[BoneIndex];
							}
						}
					}
					else
					{
						for (int32 BoneIndex : SelectedBones)
						{
							if (SelectedBones.Num() > 1 && RandomStream.FRand() > CommonSettings->ChanceToFracture)
							{
								continue;
							}

							FractureContexts.AddDefaulted();
							FFractureContext& FractureContext = FractureContexts.Last();
							FractureContext.RandomSeed = FMath::Rand();
							if (CommonSettings->RandomSeed > -1)
							{
								// make sure it's unique for each context if it's specified.
								FractureContext.RandomSeed = CommonSettings->RandomSeed + FractureContexts.Num();
							}

							FractureContext.OriginalActor = Actor;
							FractureContext.Transform = Actor->GetActorTransform();
							FractureContext.OriginalPrimitiveComponent = GeometryCollectionComponent;
							FractureContext.FracturedGeometryCollection = FracturedGeometryCollection;
							FractureContext.SelectedBones = { BoneIndex };
							if(TransformToGeometryIndex[BoneIndex] > -1)
							{
								FractureContext.Bounds = BoundsToBone[BoneIndex];
							}
						}
					}
				}
			}
		}
	}
}

FReply FFractureEditorModeToolkit::OnFractureClicked()
{
	if (ActiveTool)
	{
		const double CacheStartTime = FPlatformTime::Seconds();

		TArray<FFractureContext> FractureContexts;
		GetFractureContexts(FractureContexts);

		FScopedTransaction Transaction(LOCTEXT("FractureMesh", "Fracture Mesh"));

		TArray<UGeometryCollectionComponent*> NewComponents;

		for (FFractureContext& FractureContext : FractureContexts)
		{
			ExecuteFracture(FractureContext);
			UGeometryCollectionComponent* GeometryCollectionComponent = Cast<UGeometryCollectionComponent>(FractureContext.OriginalPrimitiveComponent);
			NewComponents.AddUnique(GeometryCollectionComponent);
		}


		for (UGeometryCollectionComponent* GeometryCollectionComponent : NewComponents)
		{
			FGeometryCollectionEdit GCEdit = GeometryCollectionComponent->EditRestCollection();
			UGeometryCollection* GCObject = GCEdit.GetRestCollection();
			TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = GCObject->GetGeometryCollection();
			FGeometryCollectionClusteringUtility::UpdateHierarchyLevelOfChildren(GeometryCollectionPtr.Get(), -1);

			FScopedColorEdit EditBoneColor(GeometryCollectionComponent, true);

			EditBoneColor.SelectBones(GeometryCollection::ESelectionMode::None);
			SetBoneSelection(GeometryCollectionComponent, EditBoneColor.GetSelectedBones(), true);

			UpdateExplodedVectors(GeometryCollectionComponent);

			GeometryCollectionComponent->MarkRenderDynamicDataDirty();
			GeometryCollectionComponent->MarkRenderStateDirty();
		}

		SetOutlinerComponents(NewComponents);

		float ProcessingTime = static_cast<float>(FPlatformTime::Seconds() - CacheStartTime);

		GCurrentLevelEditingViewportClient->Invalidate();
	}

	return FReply::Handled();
}

bool FFractureEditorModeToolkit::CanExecuteFracture() const
{
	if((ActiveTool == nullptr) || !ActiveTool->CanExecuteFracture())
	{
		return false;
	}

	if (!IsSeletedActorsInEditorWorld())
	{
		return false;
	}

	if (IsGeometryCollectionSelected())
	{
		return IsLeafBoneSelected();
	}

	if (IsStaticMeshSelected())
	{
		return false;
	}
	
	return false;
}


bool FFractureEditorModeToolkit::IsLeafBoneSelected() const
{
	TSet<UGeometryCollectionComponent*> GeomCompSelection;
	GetSelectedGeometryCollectionComponents(GeomCompSelection);
	for (UGeometryCollectionComponent* GeometryCollectionComponent : GeomCompSelection)
	{
		const TArray<int32> SelectedBones = GeometryCollectionComponent->GetSelectedBones();

		if (SelectedBones.Num() > 0)
		{
			if (const UGeometryCollection* GCObject = GeometryCollectionComponent->GetRestCollection())
			{
				TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = GCObject->GetGeometryCollection();
				if (const FGeometryCollection* GeometryCollection = GeometryCollectionPtr.Get())
				{
					const TManagedArray<TSet<int32>>& Children = GeometryCollection->GetAttribute<TSet<int32>>("Children", FGeometryCollection::TransformGroup);

					for (int32 BoneIndex : SelectedBones)
					{
						if (Children[BoneIndex].Num() == 0)
						{
							return true;
						}
					}
				}
			}
		}
	}
	return false;
}

ULevel* FFractureEditorModeToolkit::GetSelectedLevel()
{
	USelection* SelectedActors = GEditor->GetSelectedActors();
	TArray<ULevel*> UniqueLevels;
	for (FSelectionIterator Iter(*SelectedActors); Iter; ++Iter)
	{
		AActor* Actor = Cast<AActor>(*Iter);
		if (Actor)
		{
			UniqueLevels.AddUnique(Actor->GetLevel());
		}
	}
	check(UniqueLevels.Num() == 1);
	return UniqueLevels[0];
}


void FFractureEditorModeToolkit::GetSelectedGeometryCollectionComponents(TSet<UGeometryCollectionComponent*>& GeomCompSelection)
{
	USelection* SelectionSet = GEditor->GetSelectedActors();
	TArray<AActor*> SelectedActors;
	SelectedActors.Reserve(SelectionSet->Num());
	SelectionSet->GetSelectedObjects(SelectedActors);

	GeomCompSelection.Empty(SelectionSet->Num());

	for (AActor* Actor : SelectedActors)
	{
		TArray<UActorComponent*> Components = Actor->GetComponentsByClass(UPrimitiveComponent::StaticClass());
		for (UActorComponent* Component : Components)
		{
			UPrimitiveComponent* PrimitiveComponent = CastChecked<UPrimitiveComponent>(Component);
			if (UGeometryCollectionComponent* GeometryCollectionComponent = Cast<UGeometryCollectionComponent>(PrimitiveComponent))
			{
				GeomCompSelection.Add(GeometryCollectionComponent);
			}
		}
	}
}


void FFractureEditorModeToolkit::OnAutoCluster()
{
	SetOutlinerComponents({});

	TSet<UGeometryCollectionComponent*> GeomCompSelection;
	GetSelectedGeometryCollectionComponents(GeomCompSelection);
	for (UGeometryCollectionComponent* GeometryCollectionComponent : GeomCompSelection)
	{
		FGeometryCollectionEdit GCEdit = GeometryCollectionComponent->EditRestCollection();
		if (UGeometryCollection* GCObject = GCEdit.GetRestCollection())
		{
			TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = GCObject->GetGeometryCollection();
			if (FGeometryCollection* GeometryCollection = GeometryCollectionPtr.Get())
			{
				FScopedColorEdit EditBoneColor = GeometryCollectionComponent->EditBoneSelection();
				int32 CurrentLevelView = EditBoneColor.GetViewLevel();
				UAutoClusterFractureCommand::ClusterChildBonesOfASingleMesh(GeometryCollectionComponent, AutoClusterMode, GetAutoClusterSiteCount());
				FGeometryCollectionClusteringUtility::UpdateHierarchyLevelOfChildren(GeometryCollection, -1);

				EditBoneColor.ResetBoneSelection();
				EditBoneColor.SetLevelViewMode(0);
				EditBoneColor.SetLevelViewMode(CurrentLevelView);
			}
		}
		GeometryCollectionComponent->MarkRenderDynamicDataDirty();
		GeometryCollectionComponent->MarkRenderStateDirty();
	}
	SetOutlinerComponents(GeomCompSelection.Array());
}

void FFractureEditorModeToolkit::OnCluster()
{
	TSet<UGeometryCollectionComponent*> GeomCompSelection;
	GetSelectedGeometryCollectionComponents(GeomCompSelection);
	for (UGeometryCollectionComponent* GeometryCollectionComponent : GeomCompSelection)
	{
		const TArray<int32> SelectedBones = GeometryCollectionComponent->GetSelectedBones();

		if (SelectedBones.Num() > 1)
		{
			FGeometryCollectionEdit GCEdit = GeometryCollectionComponent->EditRestCollection();
			if (UGeometryCollection* GCObject = GCEdit.GetRestCollection())
			{
				TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = GCObject->GetGeometryCollection();
				if (FGeometryCollection* GeometryCollection = GeometryCollectionPtr.Get())
				{
					const TManagedArray<TSet<int32>>& Children = GeometryCollection->GetAttribute<TSet<int32>>("Children", FGeometryCollection::TransformGroup);

					// sort the selection list so ClusterBonesUnderNewNode() happens in the correct order for leaf nodes
					TArray<int32> SortedSelectedBones;
					SortedSelectedBones.Reserve(SelectedBones.Num());
					for (int32 SelectedBone : SelectedBones)
					{
						if(Children[SelectedBone].Num() > 0)
						{
							SortedSelectedBones.Insert(SelectedBone, 0);
						}
						else
						{
							SortedSelectedBones.Add(SelectedBone);
						}
					}
					// cluster Selected Bones under the first selected bone
					int32 InsertAtIndex = SortedSelectedBones[0];

					FGeometryCollectionClusteringUtility::ClusterBonesUnderNewNode(GeometryCollection, InsertAtIndex, SortedSelectedBones, false);
					FGeometryCollectionClusteringUtility::UpdateHierarchyLevelOfChildren(GeometryCollection, -1);

					FScopedColorEdit EditBoneColor = GeometryCollectionComponent->EditBoneSelection();
					EditBoneColor.ResetBoneSelection();
					EditBoneColor.ResetHighlightedBones();
					GeometryCollectionComponent->MarkRenderDynamicDataDirty();
					GeometryCollectionComponent->MarkRenderStateDirty();
					SetBoneSelection(GeometryCollectionComponent, EditBoneColor.GetSelectedBones(), true);
				}
			}
		}
	}

	SetOutlinerComponents(GeomCompSelection.Array());
}



void FFractureEditorModeToolkit::OnUncluster()
{
	TSet<UGeometryCollectionComponent*> GeomCompSelection;
	GetSelectedGeometryCollectionComponents(GeomCompSelection);
	for (UGeometryCollectionComponent* GeometryCollectionComponent : GeomCompSelection)
	{
		// scoped edit of collection
		FGeometryCollectionEdit GeometryCollectionEdit = GeometryCollectionComponent->EditRestCollection();
		if (UGeometryCollection* GeometryCollectionObject = GeometryCollectionEdit.GetRestCollection())
		{
			TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = GeometryCollectionObject->GetGeometryCollection();
			if (FGeometryCollection* GeometryCollection = GeometryCollectionPtr.Get())
			{
				FGeometryCollectionClusteringUtility::UpdateHierarchyLevelOfChildren(GeometryCollection, -1);
				FGeometryCollectionClusteringUtility::CollapseSelectedHierarchy(FractureLevel, GeometryCollectionComponent->GetSelectedBones(), GeometryCollection);

				FScopedColorEdit EditBoneColor = GeometryCollectionComponent->EditBoneSelection();
				EditBoneColor.ResetBoneSelection();
				EditBoneColor.ResetHighlightedBones();
				GeometryCollectionComponent->MarkRenderDynamicDataDirty();
				GeometryCollectionComponent->MarkRenderStateDirty();
				SetBoneSelection(GeometryCollectionComponent, EditBoneColor.GetSelectedBones(), true);
			}
		}
	}
	SetOutlinerComponents(GeomCompSelection.Array());

}

void FFractureEditorModeToolkit::AddSingleRootNodeIfRequired(UGeometryCollection* GeometryCollectionObject)
{
	TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = GeometryCollectionObject->GetGeometryCollection();
	if (FGeometryCollection* GeometryCollection = GeometryCollectionPtr.Get())
	{
		if (FGeometryCollectionClusteringUtility::ContainsMultipleRootBones(GeometryCollection))
		{
			FGeometryCollectionClusteringUtility::ClusterAllBonesUnderNewRoot(GeometryCollection);
		}
	}
}

void FFractureEditorModeToolkit::AddAdditionalAttributesIfRequired(UGeometryCollection* GeometryCollectionObject)
{
	TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = GeometryCollectionObject->GetGeometryCollection();
	if (FGeometryCollection* GeometryCollection = GeometryCollectionPtr.Get())
	{
		if (!GeometryCollection->HasAttribute("Level", FGeometryCollection::TransformGroup))
		{
			FGeometryCollectionClusteringUtility::UpdateHierarchyLevelOfChildren(GeometryCollection, -1);
		}
	}
}


void FFractureEditorModeToolkit::OnFlatten()
{
	TSet<UGeometryCollectionComponent*> GeomCompSelection;
	GetSelectedGeometryCollectionComponents(GeomCompSelection);
	for (UGeometryCollectionComponent* GeometryCollectionComponent : GeomCompSelection)
	{
		FGeometryCollectionEdit GCEdit = GeometryCollectionComponent->EditRestCollection();
		if (UGeometryCollection* GCObject = GCEdit.GetRestCollection())
		{
			TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = GCObject->GetGeometryCollection();
			if (FGeometryCollection* GeometryCollection = GeometryCollectionPtr.Get())
			{
				AddAdditionalAttributesIfRequired(GCObject);
				AddSingleRootNodeIfRequired(GCObject);

				int32 NumElements = GCObject->NumElements(FGeometryCollection::TransformGroup);
				TArray<int32> Elements;
				Elements.Reserve(NumElements);

				for (int32 Element = 0; Element < NumElements; ++Element)
				{
					if (GeometryCollection->Parent[Element] != FGeometryCollection::Invalid)
					{
						Elements.Add(Element);
					}
				}

				if (Elements.Num() > 0)
				{
					FGeometryCollectionClusteringUtility::ClusterBonesUnderExistingRoot(GeometryCollection, Elements);
				}

				FGeometryCollectionClusteringUtility::UpdateHierarchyLevelOfChildren(GeometryCollection, -1);

				FScopedColorEdit EditBoneColor = GeometryCollectionComponent->EditBoneSelection();
				EditBoneColor.ResetBoneSelection();

				OnSetLevelViewValue(1);

				GeometryCollectionComponent->MarkRenderDynamicDataDirty();
				GeometryCollectionComponent->MarkRenderStateDirty();
			}
		}
	}

	SetOutlinerComponents(GeomCompSelection.Array());
}


void FFractureEditorModeToolkit::OnMoveUp()
{
	TSet<UGeometryCollectionComponent*> GeomCompSelection;
	GetSelectedGeometryCollectionComponents(GeomCompSelection);
	for (UGeometryCollectionComponent* GeometryCollectionComponent : GeomCompSelection)
	{
		FGeometryCollectionEdit GeometryCollectionEdit = GeometryCollectionComponent->EditRestCollection();
		if (UGeometryCollection* GeometryCollectionObject = GeometryCollectionEdit.GetRestCollection())
		{
			TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = GeometryCollectionObject->GetGeometryCollection();
			if (FGeometryCollection* GeometryCollection = GeometryCollectionPtr.Get())
			{
				TArray<int32> Selected = GeometryCollectionComponent->GetSelectedBones();
				FGeometryCollectionClusteringUtility::MoveUpOneHierarchyLevel(GeometryCollection, Selected);

				GeometryCollectionComponent->MarkRenderDynamicDataDirty();
				GeometryCollectionComponent->MarkRenderStateDirty();
			}
		}
	}
	SetOutlinerComponents(GeomCompSelection.Array());
}

void FFractureEditorModeToolkit::GenerateAsset()
{
	USelection* SelectionSet = GEditor->GetSelectedActors();

	TArray<AActor*> SelectedActors;
	SelectedActors.Reserve(SelectionSet->Num());

	FScopedTransaction Transaction(LOCTEXT("GenerateAsset", "Generate Geometry Collection Asset"));

	SelectionSet->GetSelectedObjects(SelectedActors);

	OpenGenerateAssetDialog(SelectedActors);
}


void FFractureEditorModeToolkit::OpenGenerateAssetDialog(TArray<AActor*>& Actors)
{
	TSharedPtr<SWindow> PickAssetPathWindow;

	SAssignNew(PickAssetPathWindow, SWindow)
	.Title(LOCTEXT("SelectPath", "Select Path"))
	.ToolTipText(LOCTEXT("SelectPathTooltip", "Select the path where the Geometry Collection will be created at"))
	.ClientSize(FVector2D(400, 400));

	// NOTE - the parent window has to completely exist before this one does so the parent gets set properly.
	// This is why we do not just put this in the Contents()[ ... ] of the Window above.
	TSharedPtr<SCreateAssetFromObject> CreateAssetDialog;
	PickAssetPathWindow->SetContent(
		SAssignNew(CreateAssetDialog, SCreateAssetFromObject, PickAssetPathWindow)
		.AssetFilenameSuffix(TEXT("GeometryCollection"))
		.HeadingText(LOCTEXT("CreateGeometryCollection_Heading", "Geometry Collection Name"))
		.CreateButtonText(LOCTEXT("CreateGeometryCollection_ButtonLabel", "Create Geometry Collection"))
		.OnCreateAssetAction(FOnPathChosen::CreateSP(this, &FFractureEditorModeToolkit::OnGenerateAssetPathChosen, Actors))
	);

	TSharedPtr<SWindow> RootWindow = FGlobalTabmanager::Get()->GetRootWindow();
	if (RootWindow.IsValid())
	{
		FSlateApplication::Get().AddWindowAsNativeChild(PickAssetPathWindow.ToSharedRef(), RootWindow.ToSharedRef());
	}
	else
	{
		FSlateApplication::Get().AddWindow(PickAssetPathWindow.ToSharedRef());
	}

}

void FFractureEditorModeToolkit::OnGenerateAssetPathChosen(const FString& InAssetPath, TArray<AActor*> Actors)
{
		UGeometryCollectionComponent* GeometryCollectionComponent = nullptr;//  = Cast<UGeometryCollectionComponent>(FractureContext.OriginalPrimitiveComponent);

		if (Actors.Num() > 0)
		{
			AActor* FirstActor = Actors[0];

			AGeometryCollectionActor* GeometryCollectionActor = Cast<AGeometryCollectionActor>(FirstActor);
			GeometryCollectionActor = ConvertStaticMeshToGeometryCollection(InAssetPath, Actors);

			GeometryCollectionComponent = GeometryCollectionActor->GetGeometryCollectionComponent();

			FScopedColorEdit EditBoneColor = GeometryCollectionComponent->EditBoneSelection();
			EditBoneColor.SetShowBoneColors(true);

			// Move GC actor to source actors position and remove source actor from scene
			const FVector ActorLocation(FirstActor->GetActorLocation());
			GeometryCollectionActor->SetActorLocation(ActorLocation);

			// Clear selection of mesh actor used to make GC before selecting, will cause details pane to not display geometry collection details.
			GEditor->SelectNone(true, true, false);
			GEditor->SelectActor(GeometryCollectionActor, true, true);

			EditBoneColor.SelectBones(GeometryCollection::ESelectionMode::AllGeometry);

			SetOutlinerComponents({ GeometryCollectionComponent });
			SetBoneSelection(GeometryCollectionComponent, EditBoneColor.GetSelectedBones(), true);

			GeometryCollectionComponent->MarkRenderDynamicDataDirty();
			GeometryCollectionComponent->MarkRenderStateDirty();

			for (AActor* Actor : Actors)
			{
				Actor->Destroy();
			}
		}
}


AActor* FFractureEditorModeToolkit::AddActor(ULevel* InLevel, UClass* Class)
{
	check(Class);

	UWorld* World = InLevel->OwningWorld;
	ULevel* DesiredLevel = InLevel;

	// Transactionally add the actor.
	AActor* Actor = NULL;
	{
		FScopedTransaction Transaction(NSLOCTEXT("UnrealEd", "AddActor", "Add Actor"));

		FActorSpawnParameters SpawnInfo;
		SpawnInfo.OverrideLevel = DesiredLevel;
		SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnInfo.ObjectFlags = RF_Transactional;
		const auto Location = FVector(0);
		const auto Rotation = FTransform(FVector(0)).GetRotation().Rotator();
		Actor = World->SpawnActor(Class, &Location, &Rotation, SpawnInfo);

		check(Actor);
		Actor->InvalidateLightingCache();
		Actor->PostEditMove(true);
	}

	// If this actor is part of any layers (set in its default properties), add them into the visible layers list.
	GEditor->Layers->SetLayersVisibility(Actor->Layers, true);

	// Clean up.
	Actor->MarkPackageDirty();
	ULevel::LevelDirtiedEvent.Broadcast();

	return Actor;
}


class AGeometryCollectionActor* FFractureEditorModeToolkit::CreateNewGeometryActor(const FString& InAssetPath, const FTransform& Transform, bool AddMaterials /*= false*/)
{

	FString UniquePackageName = InAssetPath;
	FString UniqueAssetName = FPackageName::GetLongPackageAssetName(InAssetPath);

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	AssetToolsModule.Get().CreateUniqueAssetName(UniquePackageName, TEXT(""), UniquePackageName, UniqueAssetName);

	UPackage* Package = CreatePackage(NULL, *UniquePackageName);
	UGeometryCollection* InGeometryCollection = static_cast<UGeometryCollection*>(NewObject<UGeometryCollection>(Package, UGeometryCollection::StaticClass(), FName(*UniqueAssetName), RF_Transactional | RF_Public | RF_Standalone));

	// Create the new Geometry Collection actor
	AGeometryCollectionActor* NewActor = Cast<AGeometryCollectionActor>(AddActor(GetSelectedLevel(), AGeometryCollectionActor::StaticClass()));
	check(NewActor->GetGeometryCollectionComponent());

	// Set the Geometry Collection asset in the new actor
	NewActor->GetGeometryCollectionComponent()->SetRestCollection(InGeometryCollection);

	// copy transform of original static mesh actor to this new actor
	NewActor->SetActorLabel(UniqueAssetName);
	NewActor->SetActorTransform(Transform);

	// Mark relevant stuff dirty
	FAssetRegistryModule::AssetCreated(InGeometryCollection);
	InGeometryCollection->MarkPackageDirty();
	Package->SetDirtyFlag(true);

	return NewActor;
}



void FFractureEditorModeToolkit::ExecuteFracture(FFractureContext& FractureContext)
{
	FractureContext.FracturedGeometryCollection->Modify();
	ActiveTool->ExecuteFracture(FractureContext);
	TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = FractureContext.FracturedGeometryCollection->GetGeometryCollection();
	FGeometryCollection* OutGeometryCollection = GeometryCollectionPtr.Get();
	FGeometryCollectionClusteringUtility::UpdateHierarchyLevelOfChildren(OutGeometryCollection, -1);

	UGeometryCollectionComponent* GeometryCollectionComponent = Cast<UGeometryCollectionComponent>(FractureContext.OriginalPrimitiveComponent);

	if (GeometryCollectionComponent != nullptr) // Create new GC actor from static mesh
	{
		FScopedColorEdit EditBoneColor = GeometryCollectionComponent->EditBoneSelection();

		EditBoneColor.SelectBones(GeometryCollection::ESelectionMode::None);
		SetBoneSelection(GeometryCollectionComponent, EditBoneColor.GetSelectedBones(), true);
	}

	GeometryCollectionComponent->MarkRenderDynamicDataDirty();
	GeometryCollectionComponent->MarkRenderStateDirty();
}


bool FFractureEditorModeToolkit::IsGeometryCollectionSelected()
{
	USelection* SelectedActors = GEditor->GetSelectedActors();
	TArray<ULevel*> UniqueLevels;
	for (FSelectionIterator Iter(*SelectedActors); Iter; ++Iter)
	{
		AActor* Actor = Cast<AActor>(*Iter);
		if (Actor)
		{
			TArray<UActorComponent*> Components = Actor->GetComponentsByClass(UGeometryCollectionComponent::StaticClass());
			if (Components.Num() > 0)
			{
				return true;
			}
		}
	}
	return false;
}

bool FFractureEditorModeToolkit::IsStaticMeshSelected()
{
	USelection* SelectedActors = GEditor->GetSelectedActors();
	for (FSelectionIterator Iter(*SelectedActors); Iter; ++Iter)
	{
		AActor* Actor = Cast<AActor>(*Iter);
		if (Actor)
		{
			TInlineComponentArray<UStaticMeshComponent*> StaticMeshComponents;
			Actor->GetComponents<UStaticMeshComponent>(StaticMeshComponents, true);

			if (StaticMeshComponents.Num() > 0)
			{
				return true;
			}
		}
	}
	return false;
}

bool FFractureEditorModeToolkit::IsSeletedActorsInEditorWorld()
{
	USelection* SelectedActors = GEditor->GetSelectedActors();
	for (FSelectionIterator Iter(*SelectedActors); Iter; ++Iter)
	{
		AActor* Actor = Cast<AActor>(*Iter);
		if (Actor)
		{
			check(Actor->GetWorld());
			if (Actor->GetWorld()->WorldType != EWorldType::Editor)
			{
				return false;
			}
		}
	}
	return true;
}

bool GetValidGeoCenter(const TManagedArray<int32>& TransformToGeometryIndex, const TArray<FTransform>& Transforms, const TManagedArray<TSet<int32>>& Children, const TManagedArray<FBox>& BoundingBox, int32 TransformIndex, FVector& OutGeoCenter )
{
	if (TransformToGeometryIndex[TransformIndex] > -1)
	{
		OutGeoCenter = Transforms[TransformIndex].TransformPosition(BoundingBox[TransformToGeometryIndex[TransformIndex]].GetCenter());

		return true;
	}
	else
	{
		FVector AverageCenter;
		int32 ValidVectors = 0;
		for(int32 ChildIndex : Children[TransformIndex])
		{

			if (GetValidGeoCenter(TransformToGeometryIndex, Transforms, Children, BoundingBox, ChildIndex, OutGeoCenter))
			{
				if (ValidVectors == 0)
				{
					AverageCenter = OutGeoCenter;
				}
				else
				{
					AverageCenter += OutGeoCenter;
				}
				++ValidVectors;
			}
		}

		if (ValidVectors > 0)
		{
			OutGeoCenter = AverageCenter / ValidVectors;
			return true;
		}
	}
	return false;
}

void FFractureEditorModeToolkit::UpdateExplodedVectors(UGeometryCollectionComponent* GeometryCollectionComponent) const
{
	TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = GeometryCollectionComponent->GetRestCollection()->GetGeometryCollection();
	const FGeometryCollection* OutGeometryCollectionConst = GeometryCollectionPtr.Get();

	if (FMath::IsNearlyEqual(ExplodeAmount, 0.0f))
	{
		if (OutGeometryCollectionConst->HasAttribute("ExplodedVector", FGeometryCollection::TransformGroup))
		{
			FGeometryCollectionEdit RestCollection = GeometryCollectionComponent->EditRestCollection(GeometryCollection::EEditUpdate::RestPhysicsDynamic);
			FGeometryCollection* OutGeometryCollection = GeometryCollectionPtr.Get();
			OutGeometryCollection->RemoveAttribute("ExplodedVector", FGeometryCollection::TransformGroup);
		}
	}
	else
	{
		FGeometryCollectionEdit RestCollection = GeometryCollectionComponent->EditRestCollection(GeometryCollection::EEditUpdate::RestPhysicsDynamic);
		UGeometryCollection* GeometryCollection = RestCollection.GetRestCollection();
		FGeometryCollection* OutGeometryCollection = GeometryCollectionPtr.Get();

		if (!OutGeometryCollection->HasAttribute("ExplodedVector", FGeometryCollection::TransformGroup))
		{
			OutGeometryCollection->AddAttribute<FVector>("ExplodedVector", FGeometryCollection::TransformGroup, FManagedArrayCollection::FConstructionParameters(FName(), false));
		}

		check(OutGeometryCollection->HasAttribute("ExplodedVector", FGeometryCollection::TransformGroup));

		TManagedArray<FVector>& ExplodedVectors = OutGeometryCollection->GetAttribute<FVector>("ExplodedVector", FGeometryCollection::TransformGroup);
		const TManagedArray<FTransform>& Transform = OutGeometryCollection->GetAttribute<FTransform>("Transform", FGeometryCollection::TransformGroup);
		const TManagedArray<int32>& TransformToGeometryIndex = OutGeometryCollection->GetAttribute<int32>("TransformToGeometryIndex", FGeometryCollection::TransformGroup);
		const TManagedArray<FBox>& BoundingBox = OutGeometryCollection->GetAttribute<FBox>("BoundingBox", FGeometryCollection::GeometryGroup);

		// Make sure we have valid "Level"
		AddAdditionalAttributesIfRequired(GeometryCollection);

		const TManagedArray<int32>& Levels = OutGeometryCollection->GetAttribute<int32>("Level", FTransformCollection::TransformGroup);
		const TManagedArray<int32>& Parent = OutGeometryCollection->GetAttribute<int32>("Parent", FTransformCollection::TransformGroup);
		const TManagedArray<TSet<int32>>& Children = OutGeometryCollection->GetAttribute<TSet<int32>>("Children", FGeometryCollection::TransformGroup);

		int32 ViewFractureLevel = GetLevelViewValue();

		int32 MaxFractureLevel = ViewFractureLevel;
		for (int32 Idx = 0, ni = GeometryCollection->NumElements(FGeometryCollection::TransformGroup); Idx < ni; ++Idx)
		{
			if (Levels[Idx] > MaxFractureLevel)
				MaxFractureLevel = Levels[Idx];
		}

		TArray<FTransform> Transforms;
		GeometryCollectionAlgo::GlobalMatrices(Transform, OutGeometryCollection->Parent, Transforms);

		TArray<FVector> TransformedCenters;
		TransformedCenters.SetNumUninitialized(Transforms.Num());

		int32 TransformsCount = 0;

		FVector Center(ForceInitToZero);
		for (int32 Idx = 0, ni = GeometryCollection->NumElements(FGeometryCollection::TransformGroup); Idx < ni; ++Idx)
		{
			ExplodedVectors[Idx] = FVector::ZeroVector;
			FVector GeoCenter;
			if (GetValidGeoCenter(TransformToGeometryIndex, Transforms, Children, BoundingBox, Idx, GeoCenter))
			{
				TransformedCenters[Idx] = GeoCenter;
				if ((ViewFractureLevel < 0) || Levels[Idx] == ViewFractureLevel)
				{
					Center += TransformedCenters[Idx];
					++TransformsCount;
				}
			}
		}

		Center /= TransformsCount;

		for (int Level = 1; Level <= MaxFractureLevel; Level++)
		{
			for (int32 Idx = 0, ni = GeometryCollection->NumElements(FGeometryCollection::TransformGroup); Idx < ni; ++Idx)
			{
				if ((ViewFractureLevel < 0) || Levels[Idx] == ViewFractureLevel)
				{
					ExplodedVectors[Idx] = (TransformedCenters[Idx] - Center) * ExplodeAmount;
				}
				else
				{
					if (Parent[Idx] > -1)
					{
						ExplodedVectors[Idx] = ExplodedVectors[Parent[Idx]];
					}
				}
			}
		}
	}
}

AGeometryCollectionActor*  FFractureEditorModeToolkit::ConvertStaticMeshToGeometryCollection(const FString& InAssetPath, TArray<AActor*>& Actors)
{
	ensure(Actors.Num() > 0);
	AActor* FirstActor = Actors[0];
	const FString& Name = FirstActor->GetActorLabel();
	const FVector FirstActorLocation(FirstActor->GetActorLocation());


	AGeometryCollectionActor* NewActor = CreateNewGeometryActor(InAssetPath, FTransform(), true);

	FGeometryCollectionEdit GeometryCollectionEdit = NewActor->GetGeometryCollectionComponent()->EditRestCollection(GeometryCollection::EEditUpdate::RestPhysicsDynamic);
	UGeometryCollection* FracturedGeometryCollection = GeometryCollectionEdit.GetRestCollection();

	for (AActor* Actor : Actors)
	{
		const FTransform ActorTransform(Actor->GetTransform());
		const FVector ActorOffset(Actor->GetActorLocation() - FirstActor->GetActorLocation());

		check(FracturedGeometryCollection);

		TArray<UStaticMeshComponent*> StaticMeshComponents;
		Actor->GetComponents<UStaticMeshComponent>(StaticMeshComponents, true);
		for (int32 ii = 0, ni = StaticMeshComponents.Num(); ii < ni; ++ii)
		{
			// We're partial to static mesh components, here
			UStaticMeshComponent* StaticMeshComponent = StaticMeshComponents[ii];
			if (StaticMeshComponent != nullptr)
			{
				UStaticMesh* ComponentStaticMesh = StaticMeshComponent->GetStaticMesh();
				FTransform ComponentTranform(StaticMeshComponent->GetComponentTransform());
				ComponentTranform.SetTranslation((ComponentTranform.GetTranslation() - ActorTransform.GetTranslation()) + ActorOffset);
				FGeometryCollectionConversion::AppendStaticMesh(ComponentStaticMesh, StaticMeshComponent, ComponentTranform, FracturedGeometryCollection, true);
			}
		}

		FracturedGeometryCollection->InitializeMaterials();
	}
	AddSingleRootNodeIfRequired(FracturedGeometryCollection);

	return NewActor;
}


void FFractureEditorModeToolkit::OnOutlinerBoneSelectionChanged(UGeometryCollectionComponent* RootComponent, TArray<int32>& SelectedBones)
{
	FScopedTransaction Transaction(FractureTransactionContexts::SelectBoneContext, LOCTEXT("SelectGeometryCollectionBoneTransaction", "Select Bone"), RootComponent);

	if(SelectedBones.Num())
	{
		FFractureSelectionTools::ToggleSelectedBones(RootComponent, SelectedBones, true);
	}
	else
	{
		FFractureSelectionTools::ClearSelectedBones(RootComponent);
	}

	if (ActiveTool != nullptr)
	{
		ActiveTool->FractureContextChanged();
	}

	RootComponent->MarkRenderStateDirty();
	RootComponent->MarkRenderDynamicDataDirty();
}


#undef LOCTEXT_NAMESPACE
