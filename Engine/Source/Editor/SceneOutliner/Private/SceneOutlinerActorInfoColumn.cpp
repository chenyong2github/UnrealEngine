// Copyright Epic Games, Inc. All Rights Reserved.

#include "SceneOutlinerActorInfoColumn.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "Misc/StringBuilder.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "EditorStyleSet.h"
#include "EditorClassUtils.h"
#include "SortHelper.h"
#include "ISceneOutliner.h"
#include "ActorTreeItem.h"
#include "ComponentTreeItem.h"
#include "FolderTreeItem.h"
#include "WorldTreeItem.h"
#include "WorldPartition/DataLayer/DataLayer.h"
#include "WorldPartition/DataLayer/WorldDataLayers.h"

#define LOCTEXT_NAMESPACE "SceneOutlinerActorInfoColumn"

struct FGetInfo
{
	SceneOutliner::ECustomColumnMode::Type CurrentMode;

	FGetInfo(SceneOutliner::ECustomColumnMode::Type InCurrentMode) : CurrentMode(InCurrentMode) {}

	FString operator()(const ISceneOutlinerTreeItem& Item) const
	{
		if (const FActorTreeItem* ActorItem = Item.CastTo<FActorTreeItem>())
		{
			AActor* Actor = ActorItem->Actor.Get();
			if (!Actor)
			{
				return FString();
			}

			switch (CurrentMode)
			{
			case SceneOutliner::ECustomColumnMode::Class:
				return Actor->GetClass()->GetName();

			case SceneOutliner::ECustomColumnMode::Level:
				return FPackageName::GetShortName(Actor->GetOutermostObject()->GetName());

			case SceneOutliner::ECustomColumnMode::Socket:
				return Actor->GetAttachParentSocketName().ToString();

			case SceneOutliner::ECustomColumnMode::InternalName:
				return Actor->GetFName().ToString();

			case SceneOutliner::ECustomColumnMode::UncachedLights:
				return FString::Printf(TEXT("%7d"), Actor->GetNumUncachedStaticLightingInteractions());

			case SceneOutliner::ECustomColumnMode::Layer:
			{
				FString Result;
				for (const auto& Layer : Actor->Layers)
				{
					if (Result.Len())
					{
						Result += TEXT(", ");
					}

					Result += Layer.ToString();
				}
				return Result;
			}

			case SceneOutliner::ECustomColumnMode::DataLayer:
			{
				TStringBuilder<128> Builder;
				for (const UDataLayer* DataLayer : Actor->GetDataLayerObjects())
				{
					if (Builder.Len())
					{
						Builder += TEXT(", ");
					}
					Builder += DataLayer->GetDataLayerLabel().ToString();
				}
				return Builder.ToString();
			}

		case SceneOutliner::ECustomColumnMode::Mobility:
			{
				FString Result;
				USceneComponent* RootComponent = Actor->GetRootComponent();
				if (RootComponent)
				{
					if( RootComponent->Mobility == EComponentMobility::Static )
					{
						Result = FString(TEXT("Static"));
					}
					if (RootComponent->Mobility == EComponentMobility::Stationary)
					{
						Result = FString(TEXT("Stationary"));
					}
					else if (RootComponent->Mobility == EComponentMobility::Movable)
					{
						Result = FString(TEXT("Movable"));
					}
				}
				return Result;
			}

			default:
				return FString();
			}
		}
		else if (Item.IsA<FFolderTreeItem>())
		{
			switch (CurrentMode)
			{
			case SceneOutliner::ECustomColumnMode::Class:
				return LOCTEXT("FolderTypeName", "Folder").ToString();

			default:
				return FString();
			}
		}
		else if (Item.IsA<FWorldTreeItem>())
		{
			switch (CurrentMode)
			{
			case SceneOutliner::ECustomColumnMode::Class:
				return LOCTEXT("WorldTypeName", "World").ToString();

			default:
				return FString();
			}
		}
		else if (const FComponentTreeItem* ComponentItem = Item.CastTo<FComponentTreeItem>())
		{
			UActorComponent* Component = ComponentItem->Component.Get();
			if (!Component)
			{
				return FString();
			}

			switch (CurrentMode)
			{
			case SceneOutliner::ECustomColumnMode::Class:
				return LOCTEXT("ComponentTypeName", "Component").ToString();

			case SceneOutliner::ECustomColumnMode::InternalName:
				return Component->GetFName().ToString();

			default:
				return FString();
			}
		}

		return FString();
	}
};


TArray< TSharedPtr< SceneOutliner::ECustomColumnMode::Type > > FActorInfoColumn::ModeOptions;

FActorInfoColumn::FActorInfoColumn( ISceneOutliner& Outliner, SceneOutliner::ECustomColumnMode::Type InDefaultCustomColumnMode )
	: CurrentMode( InDefaultCustomColumnMode )
	, SceneOutlinerWeak( StaticCastSharedRef<ISceneOutliner>(Outliner.AsShared()) )
{

}


FName FActorInfoColumn::GetColumnID()
{
	return GetID();
}


SHeaderRow::FColumn::FArguments FActorInfoColumn::ConstructHeaderRowColumn()
{
	if( ModeOptions.Num() == 0 )
	{
		for(SceneOutliner::ECustomColumnMode::Type CurMode : TEnumRange<SceneOutliner::ECustomColumnMode::Type>() )
		{
			ModeOptions.Add( MakeShareable( new SceneOutliner::ECustomColumnMode::Type( CurMode ) ) );
		}
	}

	/** Customizable actor data column */
	return SHeaderRow::Column( GetColumnID() )
		.FillWidth(2)
		.HeaderComboVisibility(EHeaderComboVisibility::OnHover)
		.MenuContent()
		[
			SNew(SBorder)
			.Padding(FMargin(5))
			.BorderImage(FEditorStyle::GetBrush("Menu.Background"))
			[
				SNew(SListView<TSharedPtr<SceneOutliner::ECustomColumnMode::Type>>)
				.ListItemsSource(&ModeOptions)
				.SelectionMode(ESelectionMode::Single)
				.OnGenerateRow( this, &FActorInfoColumn::MakeComboButtonItemWidget )
				.OnSelectionChanged( this, &FActorInfoColumn::OnModeChanged )
			]
		]
		.HeaderContent()
		[
			SNew( SHorizontalBox )

			+SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text( this, &FActorInfoColumn::GetSelectedMode )
			]
		];
}

const TSharedRef< SWidget > FActorInfoColumn::ConstructRowWidget( FSceneOutlinerTreeItemRef TreeItem, const STableRow<FSceneOutlinerTreeItemPtr>& Row )
{
	auto SceneOutliner = SceneOutlinerWeak.Pin();
	check(SceneOutliner.IsValid());

	TSharedRef<SHorizontalBox> HorizontalBox = SNew(SHorizontalBox);

	TSharedRef<STextBlock> MainText = SNew( STextBlock )
		.Text( this, &FActorInfoColumn::GetTextForItem, TWeakPtr<ISceneOutlinerTreeItem>(TreeItem) ) 
		.HighlightText( SceneOutliner->GetFilterHighlightText() )
		.ColorAndOpacity( FSlateColor::UseSubduedForeground() );

	HorizontalBox->AddSlot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	.Padding(8, 0, 0, 0)
	[
		MainText
	];

	auto Hyperlink = ConstructClassHyperlink(*TreeItem);
	if (Hyperlink.IsValid())
	{
		// If we got a hyperlink, disable hide default text, and show the hyperlink
		MainText->SetVisibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateSP(this, &FActorInfoColumn::GetColumnDataVisibility, false)));
		Hyperlink->SetVisibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateSP(this, &FActorInfoColumn::GetColumnDataVisibility, true)));

		HorizontalBox->AddSlot()
		.VAlign(VAlign_Center)
		.AutoWidth()
		.Padding(8, 0, 0, 0)
		[
			// Make sure that the hyperlink shows as black (by multiplying black * desired color) when selected so it is readable against the orange background even if blue/green/etc... normally
			SNew(SBorder)
			.BorderImage(FEditorStyle::GetBrush("NoBorder"))
			.ColorAndOpacity_Static([](TWeakPtr<const STableRow<FSceneOutlinerTreeItemPtr>> WeakRow)->FLinearColor{
				auto TableRow = WeakRow.Pin();
				return TableRow.IsValid() && TableRow->IsSelected() ? FLinearColor::Black : FLinearColor::White;
			}, TWeakPtr<const STableRow<FSceneOutlinerTreeItemPtr>>(StaticCastSharedRef<const STableRow<FSceneOutlinerTreeItemPtr>>(Row.AsShared())))
			[
				Hyperlink.ToSharedRef()
			]
		];
	}

	return HorizontalBox;
}

TSharedPtr<SWidget> FActorInfoColumn::ConstructClassHyperlink( ISceneOutlinerTreeItem& TreeItem )
{
	if (const FActorTreeItem* ActorItem = TreeItem.CastTo<FActorTreeItem>())
	{
		if (AActor* Actor = ActorItem->Actor.Get())
		{
			if (UClass* ActorClass = Actor->GetClass())
			{
				// Always show blueprints
				const bool bIsBlueprintClass = UBlueprint::GetBlueprintFromClass(ActorClass) != nullptr;

				// Also show game or game plugin native classes (but not engine classes as that makes the scene outliner pretty noisy)
				bool bIsGameClass = false;
				if (!bIsBlueprintClass)
				{
					UPackage* Package = ActorClass->GetOutermost();
					const FString ModuleName = FPackageName::GetShortName(Package->GetFName());

					FModuleStatus PackageModuleStatus;
					if (FModuleManager::Get().QueryModule(*ModuleName, /*out*/ PackageModuleStatus))
					{
						bIsGameClass = PackageModuleStatus.bIsGameModule;
					}
				}

				if (bIsBlueprintClass || bIsGameClass)
				{
					return FEditorClassUtils::GetSourceLink(ActorClass, Actor);
				}
			}
		}
	}

	return nullptr;
}

void FActorInfoColumn::PopulateSearchStrings( const ISceneOutlinerTreeItem& Item, TArray< FString >& OutSearchStrings ) const
{
	{
		FString String = FGetInfo(CurrentMode)(Item);
		if (String.Len())
		{
			OutSearchStrings.Add(String);
		}
	}

	// We always add the class
	if (CurrentMode != SceneOutliner::ECustomColumnMode::Class)
	{
		FString String = FGetInfo(SceneOutliner::ECustomColumnMode::Class)(Item);
		if (String.Len())
		{
			OutSearchStrings.Add(String);
		}
	}
}

bool FActorInfoColumn::SupportsSorting() const
{
	return CurrentMode != SceneOutliner::ECustomColumnMode::None;
}

void FActorInfoColumn::SortItems(TArray<FSceneOutlinerTreeItemPtr>& RootItems, const EColumnSortMode::Type SortMode) const
{
	FSceneOutlinerSortHelper<FString>()
		.Primary(FGetInfo(CurrentMode), SortMode)
		.Sort(RootItems);
}

void FActorInfoColumn::OnModeChanged( TSharedPtr< SceneOutliner::ECustomColumnMode::Type > NewSelection, ESelectInfo::Type /*SelectInfo*/ )
{
	CurrentMode = *NewSelection;

	// Refresh and refilter the list
	SceneOutlinerWeak.Pin()->Refresh();
	FSlateApplication::Get().DismissAllMenus();
}

EVisibility FActorInfoColumn::GetColumnDataVisibility( bool bIsClassHyperlink ) const
{
	if ( CurrentMode == SceneOutliner::ECustomColumnMode::Class )
	{
		return bIsClassHyperlink ? EVisibility::Visible : EVisibility::Collapsed;
	}
	else
	{
		return bIsClassHyperlink ? EVisibility::Collapsed : EVisibility::Visible;
	}
}

FText FActorInfoColumn::GetTextForItem( TWeakPtr<ISceneOutlinerTreeItem> TreeItem ) const
{
	auto Item = TreeItem.Pin();
	return Item.IsValid() ? FText::FromString(FGetInfo(CurrentMode)(*Item)) : FText::GetEmpty();
}

FText FActorInfoColumn::GetSelectedMode() const
{
	if (CurrentMode == SceneOutliner::ECustomColumnMode::None)
	{
		return FText();
	}

	return MakeComboText(CurrentMode);
}

FText FActorInfoColumn::MakeComboText( const SceneOutliner::ECustomColumnMode::Type& Mode ) const
{
	FText ModeName;

	switch( Mode )
	{
	case SceneOutliner::ECustomColumnMode::None:
		ModeName = LOCTEXT("CustomColumnMode_None", "None");
		break;

	case SceneOutliner::ECustomColumnMode::Class:
		ModeName = LOCTEXT("CustomColumnMode_Class", "Type");
		break;

	case SceneOutliner::ECustomColumnMode::Level:
		ModeName = LOCTEXT("CustomColumnMode_Level", "Level");
		break;

	case SceneOutliner::ECustomColumnMode::Layer:
		ModeName = LOCTEXT("CustomColumnMode_Layer", "Layer");
		break;

	case SceneOutliner::ECustomColumnMode::DataLayer:
		ModeName = LOCTEXT("CustomColumnMode_DataLayer", "Data Layer");
		break;

	case SceneOutliner::ECustomColumnMode::Socket:
		ModeName = LOCTEXT("CustomColumnMode_Socket", "Socket");
		break;

	case SceneOutliner::ECustomColumnMode::InternalName:
		ModeName = LOCTEXT("CustomColumnMode_InternalName", "ID Name");
		break;

	case SceneOutliner::ECustomColumnMode::UncachedLights:
		ModeName = LOCTEXT("CustomColumnMode_UncachedLights", "# Uncached Lights");
		break;

	case SceneOutliner::ECustomColumnMode::Mobility:
		ModeName = LOCTEXT("CustomColumnMode_Mobility", "Mobility");
		break;

	default:
		ensure(0);
		break;
	}

	return ModeName;
}


FText FActorInfoColumn::MakeComboToolTipText( const SceneOutliner::ECustomColumnMode::Type& Mode )
{
	FText ToolTipText;

	switch( Mode )
	{
	case SceneOutliner::ECustomColumnMode::None:
		ToolTipText = LOCTEXT("CustomColumnModeToolTip_None", "Hides all extra actor info");
		break;

	case SceneOutliner::ECustomColumnMode::Class:
		ToolTipText = LOCTEXT("CustomColumnModeToolTip_Class", "Displays the name of each actor's type");
		break;

	case SceneOutliner::ECustomColumnMode::Level:
		ToolTipText = LOCTEXT("CustomColumnModeToolTip_Level", "Displays the level each actor is in, and allows you to search by level name");
		break;

	case SceneOutliner::ECustomColumnMode::Layer:
		ToolTipText = LOCTEXT("CustomColumnModeToolTip_Layer", "Displays the layer each actor is in, and allows you to search by layer name");
		break;

	case SceneOutliner::ECustomColumnMode::DataLayer:
		ToolTipText = LOCTEXT("CustomColumnModeToolTip_DataLayer", "Displays the data layers each actor is in, and allows you to search by data layer label");
		break;

	case SceneOutliner::ECustomColumnMode::Socket:
		ToolTipText = LOCTEXT("CustomColumnModeToolTip_Socket", "Shows the socket the actor is attached to, and allows you to search by socket name");
		break;

	case SceneOutliner::ECustomColumnMode::InternalName:
		ToolTipText = LOCTEXT("CustomColumnModeToolTip_InternalName", "Shows the internal name of the actor (for diagnostics)");
		break;

	case SceneOutliner::ECustomColumnMode::UncachedLights:
		ToolTipText = LOCTEXT("CustomColumnModeToolTip_UncachedLights", "Shows the number of uncached static lights (missing in lightmap)");
		break;

	case SceneOutliner::ECustomColumnMode::Mobility:
		ToolTipText = LOCTEXT("CustomColumnModeToolTip_Mobility", "Shows the mobility of each actor");
		break;


	default:
		ensure(0);
		break;
	}

	return ToolTipText;
}


TSharedRef< ITableRow > FActorInfoColumn::MakeComboButtonItemWidget( TSharedPtr< SceneOutliner::ECustomColumnMode::Type > Mode, const TSharedRef<STableViewBase>& Owner )
{
	return
		SNew( STableRow< TSharedPtr<SceneOutliner::ECustomColumnMode::Type> >, Owner )
		.Style(FAppStyle::Get(), "ComboBox.Row")
		[
			SNew( STextBlock )
			.Text( MakeComboText( *Mode ) )
			.ToolTipText( MakeComboToolTipText( *Mode ) )
		];
}

#undef LOCTEXT_NAMESPACE
