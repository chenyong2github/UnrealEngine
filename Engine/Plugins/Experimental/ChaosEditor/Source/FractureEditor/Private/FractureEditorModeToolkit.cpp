// Copyright Epic Games, Inc. All Rights Reserved.

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
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
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
#include "Layers/LayersSubsystem.h"
#include "StaticMeshAttributes.h"
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
	, ActiveTool(nullptr)
{
}

void FFractureEditorModeToolkit::Init(const TSharedPtr<IToolkitHost>& InitToolkitHost)
{
	FFractureEditorModule& FractureModule = FModuleManager::GetModuleChecked<FFractureEditorModule>("FractureEditor");

	const FFractureEditorCommands& Commands = FFractureEditorCommands::Get();

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

	SAssignNew(ExplodedViewWidget, SSpinBox<int32>)
	.Style(&FFractureEditorStyle::Get().GetWidgetStyle<FSpinBoxStyle>("FractureEditor.SpinBox"))
	.PreventThrottling(true)
	.Value_Lambda([=]() -> int32 { return (int32) (ExplodeAmount * 100.0f); })
	.OnValueChanged_Lambda( [=](int32 NewValue) { this->OnSetExplodedViewValue( (float)NewValue / 100.0f); } )
	.MinValue(0)
	.MaxValue(100)
	.Delta(1)
	.Font(FCoreStyle::GetDefaultFontStyle("Regular", 14))
	.MinDesiredWidth(36.f)
	.Justification(ETextJustify::Center)
	.ToolTipText(LOCTEXT("FractureEditor.Exploded_Tooltip", "How much to seperate the drawing of the bones to aid in setup.  Does not effect simulation"))
	;

	SAssignNew(LevelViewWidget, SComboButton)
	.ContentPadding(0)
	.ButtonStyle(FEditorStyle::Get(), "Toolbar.Button")
	.ForegroundColor(FEditorStyle::Get().GetSlateColor("ToolBar.SToolBarComboButtonBlock.ComboButton.Color"))
	.OnGetMenuContent(this, &FFractureEditorModeToolkit::GetLevelViewMenuContent) 
	.ButtonContent()
	[
		SNew(SBox)
		.WidthOverride(36)
		[
			SNew(STextBlock)
			.Justification(ETextJustify::Center)
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
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
			.ToolTipText(LOCTEXT("FractureEditor.Level_Tooltip", "Set the currently view level of the geometry collection"))
		]

	];

	SAssignNew(ShowBoneColorsWidget, SComboButton)
	.ContentPadding(0)
	.ButtonStyle(FEditorStyle::Get(), "ToolBar.Button")
	.ForegroundColor(FEditorStyle::Get().GetSlateColor("ToolBar.SToolBarComboButtonBlock.ComboButton.Color"))
	.OnGetMenuContent(this, &FFractureEditorModeToolkit::GetViewMenuContent)
	.ButtonContent()
	[
		SNew(SImage)
		.Image(FFractureEditorStyle::Get().GetBrush("FractureEditor.Visibility"))
		.ToolTipText(LOCTEXT("FractureEditor.Visibility_Tooltip", "Toggle showing the bone colours of the geometry collection"))
	];

	TSharedRef<SExpandableArea> OutlinerExpander = SNew(SExpandableArea)
	.AreaTitle(FText(LOCTEXT("Outliner", "Outliner")))
	.HeaderPadding(FMargin(2.0, 2.0))
	.Padding(FMargin(MorePadding))
	.BorderImage(FEditorStyle::Get().GetBrush("DetailsView.CategoryTop"))
	.BorderBackgroundColor( FLinearColor( .6,.6,.6, 1.0f ) )
	.BodyBorderBackgroundColor (FLinearColor( 1.0, 0.0, 0.0))
	.AreaTitleFont(FEditorStyle::Get().GetFontStyle("DetailsView.CategoryFontStyle"))
	.BodyContent()
	[
		SAssignNew(OutlinerView, SGeometryCollectionOutliner )
		.OnBoneSelectionChanged(this, &FFractureEditorModeToolkit::OnOutlinerBoneSelectionChanged)
	];

	TSharedRef<SExpandableArea> StatisticsExpander = SNew(SExpandableArea)
	.AreaTitle(FText(LOCTEXT("LevelStatistics", "Level Statistics")))
	.HeaderPadding(FMargin(2.0, 2.0))
	.Padding(FMargin(MorePadding))
	.BorderImage(FEditorStyle::Get().GetBrush("DetailsView.CategoryTop"))
	.BorderBackgroundColor( FLinearColor( .6,.6,.6, 1.0f ) )
	.BodyBorderBackgroundColor (FLinearColor( 1.0, 0.0, 0.0))
	.AreaTitleFont(FEditorStyle::Get().GetFontStyle("DetailsView.CategoryFontStyle"))
	.BodyContent()
	[
		SNew(STextBlock)
		.Text(this, &FFractureEditorModeToolkit::GetStatisticsSummary)
	];

	SAssignNew(ToolkitWidget, SBox)
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
		[
			SNew(SWidgetSwitcher)
			.WidgetIndex_Lambda( [this] () -> uint32 { return (GetActiveTool() != nullptr ? 1 : 0); })

			+ SWidgetSwitcher::Slot()
			[
				SNew(SSplitter)
				.Orientation( Orient_Vertical )

				+SSplitter::Slot()
				.SizeRule( TAttribute<SSplitter::ESizeRule>::Create( [this, OutlinerExpander] () { 
					return OutlinerExpander->IsExpanded() ? SSplitter::ESizeRule::FractionOfParent : SSplitter::ESizeRule::SizeToContent; 
				} ) )
				.Value(1.f)
				[
					OutlinerExpander
				]

				+SSplitter::Slot()
				.SizeRule( TAttribute<SSplitter::ESizeRule>::Create( [this, StatisticsExpander] () { 
					return StatisticsExpander->IsExpanded() ? SSplitter::ESizeRule::FractionOfParent : SSplitter::ESizeRule::SizeToContent; 
				} ) )
				.Value(0.25f)
				[
					StatisticsExpander
				]
			]

			+ SWidgetSwitcher::Slot()
			[

				SNew(SScrollBox)

				+SScrollBox::Slot()
				.Padding(0.0f, MorePadding)
				[
					DetailsView.ToSharedRef()
				]

				+SScrollBox::Slot()
				.Padding(16.f)
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
						.Text_Lambda( [this] () -> FText { return ActiveTool ? ActiveTool->GetApplyText() :  LOCTEXT("FractureApplyButton", "Apply"); })
					]

					+SHorizontalBox::Slot()
					.FillWidth(1)

					+SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SButton)
						.HAlign(HAlign_Center)
						.ContentPadding(FMargin(MorePadding, Padding))
						.OnClicked_Lambda( [this] () -> FReply { SetActiveTool(0); return FReply::Handled(); } )
						.Text(FText(LOCTEXT("FractureCancelButton", "Cancel")))
					]


					+SHorizontalBox::Slot()
					.FillWidth(1)
				]
			]
		]
	];

	// Bind Chaos Commands;
	BindCommands();

	FModeToolkit::Init(InitToolkitHost);

}

const TArray<FName> FFractureEditorModeToolkit::PaletteNames = { FName(TEXT("Fracture")), FName(TEXT("Cluster")) };

FText FFractureEditorModeToolkit::GetToolPaletteDisplayName(FName Palette) 
{ 
	return FText::FromName(Palette);
}

void FFractureEditorModeToolkit::BuildToolPalette(FName PaletteIndex, class FToolBarBuilder& ToolbarBuilder) 
{
	const FFractureEditorCommands& Commands = FFractureEditorCommands::Get();

	if (PaletteIndex == PaletteNames[0])
	{

		ToolbarBuilder.AddWidget(SNew(SBox).WidthOverride(4));

		ToolbarBuilder.AddToolBarButton(Commands.GenerateAsset);

		ToolbarBuilder.AddSeparator();
		ToolbarBuilder.AddToolBarButton(Commands.SelectAll);
		ToolbarBuilder.AddToolBarButton(Commands.SelectNone);
		ToolbarBuilder.AddToolBarButton(Commands.SelectNeighbors);
		ToolbarBuilder.AddToolBarButton(Commands.SelectSiblings);
		ToolbarBuilder.AddToolBarButton(Commands.SelectAllInCluster);
		ToolbarBuilder.AddToolBarButton(Commands.SelectInvert);

		ToolbarBuilder.AddSeparator();

		ToolbarBuilder.AddToolBarWidget(ExplodedViewWidget.ToSharedRef(), FText(LOCTEXT("FractureExplodedPercentage", "Explode")));
		ToolbarBuilder.AddToolBarWidget(LevelViewWidget.ToSharedRef(), LOCTEXT("FractureViewLevel", "Level"));
		// ToolbarBuilder.AddToolBarWidget(ShowBoneColorsWidget.ToSharedRef(), LOCTEXT("FractureViewOptions", "View"));
		ToolbarBuilder.AddToolBarButton(Commands.ToggleShowBoneColors);

		ToolbarBuilder.AddSeparator();
		ToolbarBuilder.AddToolBarButton(Commands.Uniform);
		ToolbarBuilder.AddToolBarButton(Commands.Clustered);
		ToolbarBuilder.AddToolBarButton(Commands.Radial);
		ToolbarBuilder.AddToolBarButton(Commands.Planar);
		ToolbarBuilder.AddToolBarButton(Commands.Slice);
		ToolbarBuilder.AddToolBarButton(Commands.Brick);
	}	

	else if (PaletteIndex == PaletteNames[1])
	{
		ToolbarBuilder.AddWidget(SNew(SBox).WidthOverride(4));
		ToolbarBuilder.AddToolBarButton(Commands.GenerateAsset);
		ToolbarBuilder.AddSeparator();

		ToolbarBuilder.AddToolBarButton(Commands.SelectAll);
		ToolbarBuilder.AddToolBarButton(Commands.SelectNone);
		ToolbarBuilder.AddToolBarButton(Commands.SelectNeighbors);
		ToolbarBuilder.AddToolBarButton(Commands.SelectSiblings);
		ToolbarBuilder.AddToolBarButton(Commands.SelectAllInCluster);
		ToolbarBuilder.AddToolBarButton(Commands.SelectInvert);

		ToolbarBuilder.AddSeparator();

		ToolbarBuilder.AddToolBarWidget(ExplodedViewWidget.ToSharedRef(), FText(LOCTEXT("FractureExplodedPercentage", "Explode")));
		ToolbarBuilder.AddToolBarWidget(LevelViewWidget.ToSharedRef(), LOCTEXT("FractureViewLevel", "Level"));
		// ToolbarBuilder.AddToolBarWidget(ShowBoneColorsWidget.ToSharedRef(), LOCTEXT("FractureViewOptions", "View"));
		ToolbarBuilder.AddToolBarButton(Commands.ToggleShowBoneColors);

		ToolbarBuilder.AddSeparator();

		ToolbarBuilder.AddToolBarButton(Commands.AutoCluster);
		ToolbarBuilder.AddSeparator();

		ToolbarBuilder.AddToolBarButton(Commands.Flatten);
		// ToolbarBuilder.AddToolBarButton(Commands.FlattenToLevel);
		ToolbarBuilder.AddToolBarButton(Commands.Cluster);
		ToolbarBuilder.AddToolBarButton(Commands.Uncluster);
		// ToolbarBuilder.AddToolBarButton(Commands.Merge);
		ToolbarBuilder.AddToolBarButton(Commands.MoveUp);

	}
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

void FFractureEditorModeToolkit::OnToolPaletteChanged(FName PaletteName) 
{
	if (GetActiveTool() != nullptr)
	{
		SetActiveTool(0);
	}
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
			TInlineComponentArray<UPrimitiveComponent*> Components;
			Actor->GetComponents(Components);
			for (UPrimitiveComponent* PrimitiveComponent : Components)
			{
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

	TArray<UObject*> Settings;
	if (ActiveTool != nullptr)
	{
		Settings = ActiveTool->GetSettingsObjects();

		ActiveTool->FractureContextChanged();
	}

	DetailsView->SetObjects(Settings);
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
		TInlineComponentArray<UGeometryCollectionComponent*> GeometryCollectionComponents;
		Actor->GetComponents(GeometryCollectionComponents);

		for (UGeometryCollectionComponent* GeometryCollectionComponent : GeometryCollectionComponents)
		{
			FScopedColorEdit EditBoneColor = GeometryCollectionComponent->EditBoneSelection();
			EditBoneColor.SelectBones(SelectionMode);
			SetBoneSelection(GeometryCollectionComponent, EditBoneColor.GetSelectedBones(), true);
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
			TInlineComponentArray<UGeometryCollectionComponent*> GeometryCollectionComponents;
			Actor->GetComponents(GeometryCollectionComponents);
			for (UGeometryCollectionComponent* GeometryCollectionComponent : GeometryCollectionComponents)
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

		SetActiveTool(0);

		float ProcessingTime = static_cast<float>(FPlatformTime::Seconds() - CacheStartTime);

		GCurrentLevelEditingViewportClient->Invalidate();
	}

	return FReply::Handled();
}

bool FFractureEditorModeToolkit::CanExecuteFracture() const
{

	if (!IsSelectedActorsInEditorWorld())
	{
		return false;
	}

	if (!IsGeometryCollectionSelected())
	{
		return false;
	}

	if (IsStaticMeshSelected())
	{
		return false;
	}

	if (ActiveTool != nullptr) 
	{
		return ActiveTool->CanExecuteFracture();
	}
	
	return false;
}


bool FFractureEditorModeToolkit::IsLeafBoneSelected() 
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
		TInlineComponentArray<UGeometryCollectionComponent*> GeometryCollectionComponents;
		Actor->GetComponents(GeometryCollectionComponents);
		GeomCompSelection.Append(GeometryCollectionComponents);
	}
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
	ULayersSubsystem* Layers = GEditor->GetEditorSubsystem<ULayersSubsystem>();
	Layers->SetLayersVisibility(Actor->Layers, true);

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
	if (ActiveTool != nullptr)
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
			if (Actor->FindComponentByClass<UGeometryCollectionComponent>())
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

bool FFractureEditorModeToolkit::IsSelectedActorsInEditorWorld()
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
#if WITH_EDITOR
	// If we're running PIE or SIE when this happens we should ignore the rebuild as the implicits will be in use.
	if(GEditor->bIsSimulatingInEditor || GEditor->GetPIEWorldContext() != nullptr)
	{
		return;
	}
#endif

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


FText FFractureEditorModeToolkit::GetStatisticsSummary() const
{

	TArray<const FGeometryCollection*> GeometryCollectionArray;
	if (USelection* SelectedActors = GEditor->GetSelectedActors())
	{
		for (FSelectionIterator Iter(*SelectedActors); Iter; ++Iter)
		{
			if (AGeometryCollectionActor* Actor = Cast<AGeometryCollectionActor>(*Iter))
			{
				const UGeometryCollection* RestCollection = Actor->GetGeometryCollectionComponent()->GetRestCollection();

				if(RestCollection)
				{
					const FGeometryCollection* GeometryCollection = RestCollection->GetGeometryCollection().Get();

					if(GeometryCollection != nullptr)
					{
						GeometryCollectionArray.Add(GeometryCollection);
					}
				}
			}
		}
	}


	FString Buffer;

	if (GeometryCollectionArray.Num() > 0)
	{
		TArray<int32> LevelTransformsAll;
		LevelTransformsAll.SetNumZeroed(10);
		int32 LevelMax = INT_MIN;

		for (int32 Idx = 0; Idx < GeometryCollectionArray.Num(); ++Idx)
		{
			const FGeometryCollection* GeometryCollection = GeometryCollectionArray[Idx];

			check(GeometryCollection);


			Buffer += FString::Printf(TEXT("Sum of the selected Geometry Collections\n\n"));

			if(GeometryCollection->HasAttribute("Level", FGeometryCollection::TransformGroup))
			{
				const TManagedArray<int32>& Levels = GeometryCollection->GetAttribute<int32>("Level", FGeometryCollection::TransformGroup);

				TArray<int32> LevelTransforms;
				for(int32 Element = 0, NumElement = Levels.Num(); Element < NumElement; ++Element)
				{
					const int32 NodeLevel = Levels[Element];
					while(LevelTransforms.Num() <= NodeLevel)
					{
						LevelTransforms.SetNum(NodeLevel + 1);
						LevelTransforms[NodeLevel] = 0;
					}
					++LevelTransforms[NodeLevel];
				}

				for(int32 Level = 0; Level < LevelTransforms.Num(); ++Level)
				{
					LevelTransformsAll[Level] += LevelTransforms[Level];
				}

				if(LevelTransforms.Num() > LevelMax)
				{
					LevelMax = LevelTransforms.Num();
				}
			}
		}

		for (int32 Level = 0; Level < LevelMax; ++Level)
		{
			Buffer += FString::Printf(TEXT("Level: %d \t - \t %d\n"), Level, LevelTransformsAll[Level]);
		}
	}

	return FText::FromString(Buffer);
}

#undef LOCTEXT_NAMESPACE
