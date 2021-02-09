// Copyright Epic Games, Inc. All Rights Reserved.

#include "FractureEditorModeToolkit.h"
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

#include "FractureTool.h"

#include "GeometryCollection/GeometryCollectionActor.h"
#include "GeometryCollection/GeometryCollection.h"

#include "EditorStyleSet.h"
#include "FractureEditor.h"
#include "FractureEditorCommands.h"
#include "FractureEditorStyle.h"
#include "ScopedTransaction.h"
#include "Modules/ModuleManager.h"

#include "PlanarCut.h"
#include "FractureToolAutoCluster.h" 
#include "SGeometryCollectionOutliner.h"
#include "SGeometryCollectionHistogram.h"
#include "FractureSelectionTools.h"
#include "GeometryCollection/GeometryCollectionAlgo.h"
#include "GeometryCollection/GeometryCollectionClusteringUtility.h"
#include "Editor.h"
#include "LevelEditorViewport.h"
#include "Widgets/Layout/SUniformGridPanel.h"

#include "Chaos/TriangleMesh.h"
#include "PhysicsProxy/GeometryCollectionPhysicsProxy.h"
#include "Chaos/MassProperties.h"

#include "DetailWidgetRow.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "PropertyHandle.h"

#include "FractureSettings.h"

#define LOCTEXT_NAMESPACE "FFractureEditorModeToolkit"

TArray<UClass*> FindFractureToolClasses()
{
	TArray<UClass*> Classes;

	for (TObjectIterator<UClass> ClassIterator; ClassIterator; ++ClassIterator)
	{
		if (ClassIterator->IsChildOf(UFractureActionTool::StaticClass()) && !ClassIterator->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
		{
			Classes.Add(*ClassIterator);
		}
	}

	return Classes;
}

FFractureViewSettingsCustomization::FFractureViewSettingsCustomization(FFractureEditorModeToolkit* InToolkit) 
	: Toolkit(InToolkit)
{

}

TSharedRef<IDetailCustomization> FFractureViewSettingsCustomization::MakeInstance(FFractureEditorModeToolkit* InToolkit)
{
	return MakeShareable(new FFractureViewSettingsCustomization(InToolkit));
}

void 
FFractureViewSettingsCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) 
{
	IDetailCategoryBuilder& ViewCategory = DetailBuilder.EditCategory("ViewSettings", FText::GetEmpty(), ECategoryPriority::TypeSpecific);

	TSharedRef<IPropertyHandle> LevelProperty = DetailBuilder.GetProperty("FractureLevel");

	ViewCategory.AddCustomRow(FText::GetEmpty())
	.NameContent()
	.HAlign(HAlign_Right)
	[
		SNew(STextBlock)
		.TextStyle( &FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>( "SmallText" ) )
		.Text(LOCTEXT("ShowBoneColors", "Show Bone Colors"))

	]
	.ValueContent()
	[
		SNew(SCheckBox)
		.IsChecked_Lambda([this]() -> ECheckBoxState { return this->Toolkit->GetShowBoneColors() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; } )
		.OnCheckStateChanged_Lambda([this] (ECheckBoxState CheckState) { this->Toolkit->OnSetShowBoneColors( CheckState == ECheckBoxState::Checked ); } )
	];


	ViewCategory.AddProperty(LevelProperty)
	.CustomWidget()
	.NameContent()
	.HAlign(HAlign_Right)
	[
		SNew(STextBlock)
		.TextStyle( &FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>( "SmallText" ) )
		.Text(LevelProperty->GetPropertyDisplayName())
	]
	.ValueContent()
	[
		SNew( SComboButton)
		.ContentPadding(0)
		.OnGetMenuContent(Toolkit, &FFractureEditorModeToolkit::GetLevelViewMenuContent, LevelProperty) 
		.ButtonContent()
		[
			SNew(STextBlock)
			.Justification(ETextJustify::Left)
			.Text_Lambda( [=]() -> FText {

				int32 FractureLevel = 5;
				LevelProperty->GetValue(FractureLevel);

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
		]
	];
};

FHistogramSettingsCustomization::FHistogramSettingsCustomization(FFractureEditorModeToolkit* InToolkit)
	: Toolkit(InToolkit)
{

}

TSharedRef<IDetailCustomization> FHistogramSettingsCustomization::MakeInstance(FFractureEditorModeToolkit* InToolkit)
{
	return MakeShareable(new FHistogramSettingsCustomization(InToolkit));
}

void
FHistogramSettingsCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
};

FOutlinerSettingsCustomization::FOutlinerSettingsCustomization(FFractureEditorModeToolkit* InToolkit)
	: Toolkit(InToolkit)
{

}

TSharedRef<IDetailCustomization> FOutlinerSettingsCustomization::MakeInstance(FFractureEditorModeToolkit* InToolkit)
{
	return MakeShareable(new FOutlinerSettingsCustomization(InToolkit));
}

void
FOutlinerSettingsCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
};



FFractureEditorModeToolkit::FFractureEditorModeToolkit()
	: ActiveTool(nullptr)
{
}

FFractureEditorModeToolkit::~FFractureEditorModeToolkit()
{
	FCoreUObjectDelegates::OnObjectPropertyChanged.RemoveAll(this);
}

void FFractureEditorModeToolkit::Init(const TSharedPtr<IToolkitHost>& InitToolkitHost)
{
	FFractureEditorModule& FractureModule = FModuleManager::GetModuleChecked<FFractureEditorModule>("FractureEditor");

	const FFractureEditorCommands& Commands = FFractureEditorCommands::Get();

	FPropertyEditorModule& EditModule = FModuleManager::Get().GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FCoreUObjectDelegates::OnObjectPropertyChanged.AddRaw(this, &FFractureEditorModeToolkit::OnObjectPostEditChange);

	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.bAllowSearch = false;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsViewArgs.bHideSelectionTip = true;
	DetailsViewArgs.DefaultsOnlyVisibility = EEditDefaultsOnlyNodeVisibility::Automatic;
	DetailsViewArgs.bShowOptions = false;
	DetailsViewArgs.bAllowMultipleTopLevelObjects = true;
	DetailsViewArgs.bShowKeyablePropertiesOption = false;
	DetailsViewArgs.bShowModifiedPropertiesOption = false;
	DetailsViewArgs.bAllowFavoriteSystem = false;
	DetailsViewArgs.bShowAnimatedPropertiesOption = false;

	DetailsView = EditModule.CreateDetailView(DetailsViewArgs);
	EditModule.RegisterCustomClassLayout("FractureSettings", FOnGetDetailCustomizationInstance::CreateStatic(&FFractureViewSettingsCustomization::MakeInstance, this));

	TArray<UObject*> Settings;
	Settings.Add(GetMutableDefault<UFractureSettings>());
	DetailsView->SetObjects(Settings);

	HistogramDetailsView = EditModule.CreateDetailView(DetailsViewArgs);
	EditModule.RegisterCustomClassLayout("HistogramSettings", FOnGetDetailCustomizationInstance::CreateStatic(&FHistogramSettingsCustomization::MakeInstance, this));
	HistogramDetailsView->SetObject(GetMutableDefault<UHistogramSettings>());

	OutlinerDetailsView = EditModule.CreateDetailView(DetailsViewArgs);
	EditModule.RegisterCustomClassLayout("OutlinerSettings", FOnGetDetailCustomizationInstance::CreateStatic(&FOutlinerSettingsCustomization::MakeInstance, this));
	OutlinerDetailsView->SetObject(GetMutableDefault<UOutlinerSettings>());

	float Padding = 4.0f;
	FMargin MorePadding = FMargin(10.0f, 2.0f);

	TSharedRef<SExpandableArea> HistogramExpander = SNew(SExpandableArea)
	.AreaTitle(FText(LOCTEXT("Histogram", "Histogram")))
	.HeaderPadding(FMargin(2.0, 2.0))
	.Padding(MorePadding)
	.BorderBackgroundColor(FLinearColor(.6, .6, .6, 1.0f))
	.BodyBorderBackgroundColor(FLinearColor(1.0, 0.0, 0.0))
	.AreaTitleFont(FEditorStyle::Get().GetFontStyle("HistogramDetailsView.CategoryFontStyle"))
	.InitiallyCollapsed(true)
	.BodyContent()
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			HistogramDetailsView.ToSharedRef()
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SButton)
			//.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
			//.TextStyle(FAppStyle::Get(), "ButtonText")
			.HAlign(HAlign_Center)
			.ContentPadding(FMargin(10.f, Padding))
			.OnClicked(this, &FFractureEditorModeToolkit::ResetHistogramSelection)
			.IsEnabled(this, &FFractureEditorModeToolkit::CanResetFilter)
			.Text(LOCTEXT("ResetFilterButton", "Reset Filter"))
		]
		+ SVerticalBox::Slot()
		[
			SAssignNew(HistogramView, SGeometryCollectionHistogram)
			.OnBoneSelectionChanged(this, &FFractureEditorModeToolkit::OnHistogramBoneSelectionChanged)
		]
	];

	TSharedRef<SExpandableArea> OutlinerExpander = SNew(SExpandableArea)
	.AreaTitle(FText(LOCTEXT("Outliner", "Outliner")))
	.HeaderPadding(FMargin(2.0, 2.0))
	.Padding(MorePadding)
	.BorderImage(FEditorStyle::Get().GetBrush("DetailsView.CategoryTop"))
	.BorderBackgroundColor( FLinearColor( .6,.6,.6, 1.0f ) )
	.BodyBorderBackgroundColor (FLinearColor( 1.0, 0.0, 0.0))
	.AreaTitleFont(FEditorStyle::Get().GetFontStyle("DetailsView.CategoryFontStyle"))
	.BodyContent()
	[
		SNew(SVerticalBox)
		+SVerticalBox::Slot()
		[
			SNew(SSplitter)
			.Orientation(Orient_Vertical)
			+ SSplitter::Slot()
			.SizeRule(TAttribute<SSplitter::ESizeRule>::Create([this, HistogramExpander]() {
				return HistogramExpander->IsExpanded() ? SSplitter::ESizeRule::FractionOfParent : SSplitter::ESizeRule::SizeToContent;
				}))
			.Value(1.f)
			[
				HistogramExpander
			]
			+ SSplitter::Slot()
			.SizeRule(SSplitter::ESizeRule::FractionOfParent)
			.Value(1.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					OutlinerDetailsView.ToSharedRef()
				]
				+ SVerticalBox::Slot()
				[
					SAssignNew(OutlinerView, SGeometryCollectionOutliner)
					.OnBoneSelectionChanged(this, &FFractureEditorModeToolkit::OnOutlinerBoneSelectionChanged)
				]
			]
		]	
	];

	TSharedRef<SExpandableArea> StatisticsExpander = SNew(SExpandableArea)
	.AreaTitle(FText(LOCTEXT("LevelStatistics", "Level Statistics")))
	.HeaderPadding(FMargin(2.0, 2.0))
	.Padding(MorePadding)
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
	[
		SNew(SVerticalBox)

		+SVerticalBox::Slot()
		[

			SNew(SSplitter)
			.Orientation( Orient_Vertical )
			+SSplitter::Slot()
			.SizeRule( TAttribute<SSplitter::ESizeRule>::Create( [this] () { 
				return (GetActiveTool() != nullptr) ? SSplitter::ESizeRule::FractionOfParent : SSplitter::ESizeRule::SizeToContent; 
			} ) )
			.Value(1.f)
			[
				SNew(SVerticalBox)
				+SVerticalBox::Slot()
				.FillHeight(1.0)
				[
					SNew(SScrollBox)
					+SScrollBox::Slot()
					[
						DetailsView.ToSharedRef()
					]
				]
				+SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					[
						SNew(SSpacer)	
					]	

					+SHorizontalBox::Slot()
					.Padding(4.0)
					.AutoWidth()
					[
						SNew( SButton )
						//.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
						//.TextStyle( FAppStyle::Get(), "ButtonText" )
						.HAlign(HAlign_Center)
						.ContentPadding(FMargin(10.f, Padding))
						.OnClicked(this, &FFractureEditorModeToolkit::OnModalClicked)
						.IsEnabled( this, &FFractureEditorModeToolkit::CanExecuteModal)
						.Text_Lambda( [this] () -> FText { return ActiveTool ? ActiveTool->GetApplyText().ToUpper() :  LOCTEXT("FractureApplyButton", "APPLY"); })
						.Visibility_Lambda( [this] () -> EVisibility { return (GetActiveTool() == nullptr) ? EVisibility::Collapsed : EVisibility::Visible; })
					]

					+ SHorizontalBox::Slot()
					.Padding(4.0)
					.AutoWidth()
					[
						SNew(SButton)
						//.ButtonStyle(FAppStyle::Get(), "Button")
						//.TextStyle( FAppStyle::Get(), "ButtonText" )
						.HAlign(HAlign_Center)
						.ContentPadding(FMargin(10.f, Padding))
						.OnClicked_Lambda( [this] () -> FReply { SetActiveTool(0); return FReply::Handled(); } )
						.Text(FText(LOCTEXT("FractureCancelButton", "CANCEL")))
						.Visibility_Lambda( [this] () -> EVisibility { return (GetActiveTool() == nullptr) ? EVisibility::Collapsed : EVisibility::Visible; })
					]
				]
			]

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
	];

	// Bind Chaos Commands;
	BindCommands();

	FModeToolkit::Init(InitToolkitHost);

}

void FFractureEditorModeToolkit::OnObjectPostEditChange( UObject* Object, FPropertyChangedEvent& PropertyChangedEvent )
{
	if (PropertyChangedEvent.Property)
	{
		if ( PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UFractureSettings, ExplodeAmount))
		{
			OnExplodedViewValueChanged();
		} 
		else if ( PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UFractureSettings, FractureLevel))
		{
			OnLevelViewValueChanged();
		}
		else if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UHistogramSettings, bSorted))
		{
			UHistogramSettings* HistogramSettings = GetMutableDefault<UHistogramSettings>();
			HistogramView->RefreshView(HistogramSettings->bSorted);
		}
		else if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UHistogramSettings, InspectedAttribute))
		{
			UHistogramSettings* HistogramSettings = GetMutableDefault<UHistogramSettings>();
			HistogramView->InspectAttribute(HistogramSettings->InspectedAttribute);
		}
		else if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UOutlinerSettings, ItemText))
		{
			UOutlinerSettings* OutlinerSettings = GetMutableDefault<UOutlinerSettings>();
			OutlinerView->RegenerateItems();
		}

	}
}

const TArray<FName> FFractureEditorModeToolkit::PaletteNames = { FName(TEXT("Generate")), FName(TEXT("Select")), FName(TEXT("Fracture")), FName(TEXT("Cluster")), FName(TEXT("Embed")), FName(TEXT("Properties")) };

FText FFractureEditorModeToolkit::GetToolPaletteDisplayName(FName Palette) const
{ 
	return FText::FromName(Palette);
}

void FFractureEditorModeToolkit::BuildToolPalette(FName PaletteIndex, class FToolBarBuilder& ToolbarBuilder) 
{
	const FFractureEditorCommands& Commands = FFractureEditorCommands::Get();

	if (PaletteIndex == TEXT("Generate"))
	{
		ToolbarBuilder.AddToolBarButton(Commands.GenerateAsset);
		ToolbarBuilder.AddToolBarButton(Commands.ResetAsset);
	}
	else if (PaletteIndex == TEXT("Select"))
	{
		ToolbarBuilder.AddToolBarButton(Commands.SelectAll);
		ToolbarBuilder.AddToolBarButton(Commands.SelectNone);
		ToolbarBuilder.AddToolBarButton(Commands.SelectNeighbors);
		ToolbarBuilder.AddToolBarButton(Commands.SelectSiblings);
		ToolbarBuilder.AddToolBarButton(Commands.SelectAllInCluster);
		ToolbarBuilder.AddToolBarButton(Commands.SelectInvert);
	}
	else if (PaletteIndex == TEXT("Fracture"))
	{
		ToolbarBuilder.AddToolBarButton(Commands.Uniform);
		ToolbarBuilder.AddToolBarButton(Commands.Clustered);
		ToolbarBuilder.AddToolBarButton(Commands.Radial);
		ToolbarBuilder.AddToolBarButton(Commands.Planar);
		ToolbarBuilder.AddToolBarButton(Commands.Slice);
		ToolbarBuilder.AddToolBarButton(Commands.Brick);
	}
	else if (PaletteIndex == TEXT("Cluster"))
	{
		ToolbarBuilder.AddToolBarButton(Commands.AutoCluster);
		ToolbarBuilder.AddToolBarButton(Commands.ClusterMagnet);
		ToolbarBuilder.AddToolBarButton(Commands.Flatten);
		ToolbarBuilder.AddToolBarButton(Commands.Cluster);
		ToolbarBuilder.AddToolBarButton(Commands.Uncluster);
		ToolbarBuilder.AddToolBarButton(Commands.MoveUp);
	}
	else if (PaletteIndex == TEXT("Embed"))
	{
		ToolbarBuilder.AddToolBarButton(Commands.AddEmbeddedGeometry);
		ToolbarBuilder.AddToolBarButton(Commands.DeleteEmbeddedGeometry);
	}
	else if (PaletteIndex == TEXT("Properties"))
	{
		ToolbarBuilder.AddToolBarButton(Commands.SetInitialDynamicState);
	}
}

void FFractureEditorModeToolkit::BindCommands()
{
	const FFractureEditorCommands& Commands = FFractureEditorCommands::Get();
	
	ToolkitCommands->MapAction(
		Commands.ToggleShowBoneColors,
		FExecuteAction::CreateSP(this, &FFractureEditorModeToolkit::OnToggleShowBoneColors),
		FCanExecuteAction(),
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
		FExecuteAction::CreateLambda([=]() { this->OnSetExplodedViewValue( FMath::Min(1.0, this->GetExplodedViewValue() + .1) ); } ),
		EUIActionRepeatMode::RepeatEnabled
	);

	ToolkitCommands->MapAction(
		Commands.ExplodeLess,
		FExecuteAction::CreateLambda([=]() { this->OnSetExplodedViewValue( FMath::Max(0.0, this->GetExplodedViewValue() - .1) ); } ),
		EUIActionRepeatMode::RepeatEnabled
	);

	// Map actions of all the Fracture Tools
	TArray<UClass*> SourceClasses = FindFractureToolClasses();
	for (UClass* Class : SourceClasses)
	{
		if (Class->IsChildOf(UFractureModalTool::StaticClass()))
		{
			TSubclassOf<UFractureModalTool> SubclassOf = Class;
			UFractureModalTool* FractureTool = SubclassOf->GetDefaultObject<UFractureModalTool>();

			// Only Bind Commands With Legitimately Set Commands
			if (FractureTool->GetUICommandInfo())
			{
				ToolkitCommands->MapAction(
					FractureTool->GetUICommandInfo(),
					FExecuteAction::CreateSP(this, &FFractureEditorModeToolkit::SetActiveTool, FractureTool),
					FCanExecuteAction(),
					FIsActionChecked::CreateSP(this, &FFractureEditorModeToolkit::IsActiveTool, FractureTool)
				);
			}
		}
		else
		{
			TSubclassOf<UFractureActionTool> SubclassOf = Class;
			UFractureActionTool* FractureTool = SubclassOf->GetDefaultObject<UFractureActionTool>();

			// Only Bind Commands With Legitimately Set Commands
			if (FractureTool->GetUICommandInfo())
			{
				ToolkitCommands->MapAction(
					FractureTool->GetUICommandInfo(),
					FExecuteAction::CreateSP(this, &FFractureEditorModeToolkit::ExecuteAction, FractureTool),
					FCanExecuteAction::CreateSP(this, &FFractureEditorModeToolkit::CanExecuteAction, FractureTool)
				);
			}
		}

	}
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
	UFractureSettings* FractureSettings = GetMutableDefault<UFractureSettings>();
	return FractureSettings->ExplodeAmount;
}

int32 FFractureEditorModeToolkit::GetLevelViewValue() const
{
	UFractureSettings* FractureSettings = GetMutableDefault<UFractureSettings>();
	return FractureSettings->FractureLevel;
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

void FFractureEditorModeToolkit::OnToggleShowBoneColors()
{
	OnSetShowBoneColors(!GetShowBoneColors());
}

void FFractureEditorModeToolkit::OnSetShowBoneColors(bool NewValue)
{
	TSet<UGeometryCollectionComponent*> GeomCompSelection;
	GetSelectedGeometryCollectionComponents(GeomCompSelection);
	for (UGeometryCollectionComponent* Comp : GeomCompSelection)
	{
		FScopedColorEdit EditBoneColor = Comp->EditBoneSelection();
		EditBoneColor.SetShowBoneColors(NewValue);
		Comp->MarkRenderStateDirty();
		Comp->MarkRenderDynamicDataDirty();
	}
	GCurrentLevelEditingViewportClient->Invalidate();
}

void FFractureEditorModeToolkit::OnSetExplodedViewValue(float NewValue)
{
	FScopedTransaction Transaction(LOCTEXT("SetExplodedViewValue", "Adjust Exploded View"));

	UFractureSettings* FractureSettings = GetMutableDefault<UFractureSettings>();
	if ( FMath::Abs<float>( FractureSettings->ExplodeAmount - NewValue ) >= .01f)
	{
		FractureSettings->ExplodeAmount = NewValue;
		OnExplodedViewValueChanged();
	}
}

void FFractureEditorModeToolkit::OnExplodedViewValueChanged()
{
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
	FScopedTransaction Transaction(LOCTEXT("SetLevelViewValue", "Adjust View Level"));

	UFractureSettings* FractureSettings = GetMutableDefault<UFractureSettings>();
	FractureSettings->FractureLevel = NewValue;
	OnLevelViewValueChanged();
}

void FFractureEditorModeToolkit::OnLevelViewValueChanged()
{
	int32 FractureLevel = GetLevelViewValue();

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
	int32 NewLevel = ((GetLevelViewValue() + CountMax) % CountMax) - 1;
	OnSetLevelViewValue(NewLevel);
}

void FFractureEditorModeToolkit::ViewDownOneLevel()
{
	int32 CountMax = GetLevelCount() + 1;
	int32 NewLevel = ((GetLevelViewValue() + CountMax + 2 ) % CountMax) - 1;
	OnSetLevelViewValue(NewLevel);
}

TSharedRef<SWidget> FFractureEditorModeToolkit::GetLevelViewMenuContent(TSharedRef<IPropertyHandle> PropertyHandle)
{
	int32 FractureLevel = GetLevelViewValue();

	FMenuBuilder MenuBuilder(true, GetToolkitCommands());

	MenuBuilder.AddMenuEntry(
		LOCTEXT("LevelMenuAll", "All Levels"),
		LOCTEXT("LevelMenuAllTooltip", "View All Leaf Bones in this Geometry Collection"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([=] { PropertyHandle->SetValue(-1); } ),
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
				FExecuteAction::CreateLambda([=] { PropertyHandle->SetValue(i); } ),
				FCanExecuteAction(),
				FGetActionCheckState::CreateLambda([=] {return FractureLevel == i ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;})
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

void FFractureEditorModeToolkit::ExecuteAction(UFractureActionTool* InActionTool)
{
	if (InActionTool)
	{
		InActionTool->Execute(StaticCastSharedRef<FFractureEditorModeToolkit>(AsShared()));
	}
}

bool FFractureEditorModeToolkit::CanExecuteAction(UFractureActionTool* InActionTool) const
{
	if (InActionTool)
	{
		return InActionTool->CanExecute();
	}
	else
	{
		return false;
	}
}

void FFractureEditorModeToolkit::SetActiveTool(UFractureModalTool* InActiveTool)
{
	ActiveTool = InActiveTool;

	UFractureToolSettings* ToolSettings = GetMutableDefault<UFractureToolSettings>();
	ToolSettings->OwnerTool = ActiveTool;

	TArray<UObject*> Settings;
	Settings.Add(GetMutableDefault<UFractureSettings>());

	if (ActiveTool != nullptr)
	{
		Settings.Append(ActiveTool->GetSettingsObjects());

		ActiveTool->FractureContextChanged();
	}

	DetailsView->SetObjects(Settings);
}


UFractureModalTool* FFractureEditorModeToolkit::GetActiveTool() const
{
	return ActiveTool;
}

bool FFractureEditorModeToolkit::IsActiveTool(UFractureModalTool* InActiveTool)
{
	return bool(ActiveTool == InActiveTool);
}

FText FFractureEditorModeToolkit::GetActiveToolDisplayName() const
{
	if (ActiveTool != nullptr)
	{
		return ActiveTool->GetDisplayText();	
	}
	return LOCTEXT("FractureNoTool", "Fracture Editor");
}

FText FFractureEditorModeToolkit::GetActiveToolMessage() const
{
	if (ActiveTool != nullptr)
	{
		return ActiveTool->GetTooltipText();	
	}
	return LOCTEXT("FractureNoToolMessage", "Select geometry and use “New+” to create a new Geometry Collection to begin fracturing.  Choose one of the fracture tools to break apart the selected Geometry Collection.");
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

		UpdateGeometryComponentAttributes(Component);
	}

	if (OutlinerView)
	{
		OutlinerView->SetComponents(InNewComponents);
	}

	if (HistogramView)
	{
		HistogramView->SetComponents(InNewComponents, GetLevelViewValue());
	}

	if (ActiveTool != nullptr)
	{
		ActiveTool->FractureContextChanged();
	}
}

void FFractureEditorModeToolkit::SetBoneSelection(UGeometryCollectionComponent* InRootComponent, const TArray<int32>& InSelectedBones, bool bClearCurrentSelection)
{
	OutlinerView->SetBoneSelection(InRootComponent, InSelectedBones, bClearCurrentSelection);
	
	if (ActiveTool != nullptr)
	{
		ActiveTool->FractureContextChanged();
	}
}

FReply FFractureEditorModeToolkit::OnModalClicked()
{
	if (ActiveTool)
	{
		const double CacheStartTime = FPlatformTime::Seconds();

		FScopedTransaction Transaction(LOCTEXT("FractureMesh", "Fracture Mesh"));

		ActiveTool->Execute(StaticCastSharedRef<FFractureEditorModeToolkit>(AsShared()));

		float ProcessingTime = static_cast<float>(FPlatformTime::Seconds() - CacheStartTime);

		GCurrentLevelEditingViewportClient->Invalidate();

	}

	return FReply::Handled();
}

bool FFractureEditorModeToolkit::CanExecuteModal() const
{

	if (!IsSelectedActorsInEditorWorld())
	{
		return false;
	}

	if (ActiveTool != nullptr) 
	{
		return ActiveTool->CanExecute();
	}
	
	return false;
}

FReply FFractureEditorModeToolkit::ResetHistogramSelection()
{
	HistogramView->ClearSelection();
	return FReply::Handled();
}

bool FFractureEditorModeToolkit::CanResetFilter() const 
{
	return HistogramView->IsSelected();
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


void FFractureEditorModeToolkit::UpdateGeometryComponentAttributes(UGeometryCollectionComponent* Component)
{
	if (Component)
	{
		const UGeometryCollection* RestCollection = Component->GetRestCollection();
		if (RestCollection)
		{
			FGeometryCollectionPtr GeometryCollection = RestCollection->GetGeometryCollection();

			if (!GeometryCollection->HasAttribute("Volume", FTransformCollection::TransformGroup))
			{
				GeometryCollection->AddAttribute<Chaos::FReal>("Volume", FTransformCollection::TransformGroup);
				UE_LOG(LogFractureTool, Warning, TEXT("Added Volume attribute to GeometryCollection."));
			}

			TArray<FTransform> Transform;
			GeometryCollectionAlgo::GlobalMatrices(GeometryCollection->Transform, GeometryCollection->Parent, Transform);

			const TManagedArray<FVector>& Vertex = GeometryCollection->Vertex;
			const TManagedArray<int32>& BoneMap = GeometryCollection->BoneMap;

			Chaos::TParticles<float, 3> MassSpaceParticles;
			MassSpaceParticles.AddParticles(Vertex.Num());
			for (int32 Idx = 0; Idx < Vertex.Num(); ++Idx)
			{
				MassSpaceParticles.X(Idx) = Transform[BoneMap[Idx]].TransformPosition(Vertex[Idx]);
			}

			// recalculate mass recursively from root nodes
			const TManagedArray<int32>& Parent = GeometryCollection->Parent;
			for (int32 Idx = 0; Idx < Parent.Num(); ++Idx)
			{
				if (Parent[Idx] == INDEX_NONE)
				{
					UpdateVolumes(GeometryCollection, MassSpaceParticles, Idx);
				}
			}
		}
	}
	
}

void FFractureEditorModeToolkit::UpdateVolumes(FGeometryCollectionPtr GeometryCollection, const Chaos::TParticles<float, 3>& MassSpaceParticles, int32 TransformIndex)
{
	const TManagedArray<TSet<int32>>& Children = GeometryCollection->Children;
	const TManagedArray<int32>& SimulationType = GeometryCollection->SimulationType;
	const TManagedArray<bool>& Visible = GeometryCollection->Visible;
	const TManagedArray<int32>& FaceCount = GeometryCollection->FaceCount;
	const TManagedArray<int32>& FaceStart = GeometryCollection->FaceStart;
	const TManagedArray<int32>& TransformToGeometryIndex = GeometryCollection->TransformToGeometryIndex;
	const TManagedArray<FIntVector>& Indices = GeometryCollection->Indices;
	TManagedArray<float>& Volumes = GeometryCollection->GetAttribute<float>("Volume", FTransformCollection::TransformGroup);

	if (SimulationType[TransformIndex] == FGeometryCollection::ESimulationTypes::FST_Rigid)
	{
		int32 GeometryIndex = TransformToGeometryIndex[TransformIndex];

		if (ensureMsgf(GeometryIndex > INDEX_NONE, TEXT("Leaf node %d expected to map to geometry but did not."), TransformIndex))
		{
			TUniquePtr<Chaos::FTriangleMesh> TriMesh(
				CreateTriangleMesh(
					FaceStart[GeometryIndex],
					FaceCount[GeometryIndex],
					Visible,
					Indices,
					true));

			float Volume = 0.0;
			Chaos::FVec3 CenterOfMass;
			Chaos::CalculateVolumeAndCenterOfMass(MassSpaceParticles, TriMesh->GetElements(), Volume, CenterOfMass);

			// Since we're only interested in relative mass, we assume density = 1.0
			Volumes[TransformIndex] = Volume;
		}	
	}
	else if (SimulationType[TransformIndex] == FGeometryCollection::ESimulationTypes::FST_Clustered)
	{
		// Recurse to children and sum the volumes for this node
		float LocalVolume = 0.0;
		for (int32 ChildIndex : Children[TransformIndex])
		{
			UpdateVolumes(GeometryCollection, MassSpaceParticles, ChildIndex);
			LocalVolume += Volumes[ChildIndex];
		}

		Volumes[TransformIndex] = LocalVolume;
	}
	else
	{
		// Node is embedded geometry, for which we do not require volume calculations.
		Volumes[TransformIndex] = 0.0;
	}
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

	float ExplodeAmount = GetExplodedViewValue();

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

void FFractureEditorModeToolkit::RegenerateOutliner()
{
	OutlinerView->UpdateGeometryCollection();
}

void FFractureEditorModeToolkit::RegenerateHistogram()
{
	HistogramView->RegenerateNodes(GetLevelViewValue());
}

void FFractureEditorModeToolkit::OnOutlinerBoneSelectionChanged(UGeometryCollectionComponent* RootComponent, TArray<int32>& SelectedBones)
{
	FScopedTransaction Transaction(FractureTransactionContexts::SelectBoneContext, LOCTEXT("SelectGeometryCollectionBoneTransaction", "Select Bone"), RootComponent);

	if(SelectedBones.Num())
	{
			
		FFractureSelectionTools::ToggleSelectedBones(RootComponent, SelectedBones, true);
		OutlinerView->SetBoneSelection(RootComponent, SelectedBones, true);
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

void FFractureEditorModeToolkit::OnHistogramBoneSelectionChanged(UGeometryCollectionComponent* RootComponent, TArray<int32>& SelectedBones)
{
	// If anything is selected in the Histogram, filter the appropriate Outliner component
	OutlinerView->SetHistogramSelection(RootComponent, SelectedBones);
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
		int32 EmbeddedCount = 0;

		for (int32 Idx = 0; Idx < GeometryCollectionArray.Num(); ++Idx)
		{
			const FGeometryCollection* GeometryCollection = GeometryCollectionArray[Idx];

			check(GeometryCollection);


			Buffer += FString::Printf(TEXT("Sum of the selected Geometry Collections\n\n"));

			if(GeometryCollection->HasAttribute("Level", FGeometryCollection::TransformGroup))
			{
				const TManagedArray<int32>& Levels = GeometryCollection->GetAttribute<int32>("Level", FGeometryCollection::TransformGroup);
				const TManagedArray<int32>& SimulationType = GeometryCollection->SimulationType;

				TArray<int32> LevelTransforms;
				for(int32 Element = 0, NumElement = Levels.Num(); Element < NumElement; ++Element)
				{
					if (SimulationType[Element] == FGeometryCollection::ESimulationTypes::FST_None)
					{
						++EmbeddedCount;
					}
					else
					{
						const int32 NodeLevel = Levels[Element];
						while (LevelTransforms.Num() <= NodeLevel)
						{
							LevelTransforms.SetNum(NodeLevel + 1);
							LevelTransforms[NodeLevel] = 0;
						}
						++LevelTransforms[NodeLevel];
					}		
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
		Buffer += FString::Printf(TEXT("\nEmbedded: %d"), EmbeddedCount);
	}

	return FText::FromString(Buffer);
}

#undef LOCTEXT_NAMESPACE
