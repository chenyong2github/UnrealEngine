// Copyright Epic Games, Inc. All Rights Reserved.

#include "SUSDLayersTreeView.h"

#include "USDLayerUtils.h"
#include "USDMemory.h"
#include "USDStageActor.h"
#include "USDStageModule.h"
#include "USDTypesConversion.h"

#include "EditorStyleSet.h"
#include "Engine/World.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Modules/ModuleManager.h"
#include "Styling/SlateTypes.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"

#if USE_USD_SDK

#include "USDIncludesStart.h"

#include "pxr/pxr.h"
#include "pxr/usd/pcp/cache.h"
#include "pxr/usd/pcp/layerStack.h"
#include "pxr/usd/sdf/layer.h"
#include "pxr/usd/sdf/layerUtils.h"
#include "pxr/usd/sdf/layerTree.h"
#include "pxr/usd/usdUtils/dependencies.h"

#include "USDIncludesEnd.h"

#define LOCTEXT_NAMESPACE "SUSDLayersTreeView"

struct FUsdLayerData : public TSharedFromThis< FUsdLayerData >
{
	FText GetDisplayName() const { return DisplayName; }

	FText DisplayName;
	bool bIsEditTarget = false;
	bool bIsMuted = false;
};

class FUsdLayersTreeItem : public IUsdTreeViewItem
{
public:
	FUsdLayersTreeItem( FUsdLayersTreeItem* InParentItem, const TUsdStore< pxr::UsdStageRefPtr >& InUsdStage, const TUsdStore< std::string >& InLayerIdentifier )
		: LayerData( MakeShared< FUsdLayerData >() )
		, ParentItem( InParentItem )
		, UsdStage( InUsdStage )
		, LayerIdentifier( InLayerIdentifier )
	{
		RefreshData();
	}

	bool IsValid() const
	{
		return (bool)UsdStage.Get() && ( !ParentItem || ParentItem->LayerIdentifier.Get() != LayerIdentifier.Get() );
	}

	TArray< FUsdLayersTreeItemRef > GetChildren()
	{
		if ( !IsValid() )
		{
			return {};
		}

		bool bNeedsRefresh = false;

		{
			FScopedUsdAllocs UsdAllocs;

			pxr::SdfLayerHandle UsdLayer = GetLayerHandle().Get();

			if ( UsdLayer )
			{
				int32 SubLayerIndex = 0;
				for ( const std::string& SubLayerPath : UsdLayer->GetSubLayerPaths() )
				{
					std::string SubLayerIdentifier = pxr::SdfComputeAssetPathRelativeToLayer( UsdLayer, SubLayerPath );

					if ( !Children.IsValidIndex( SubLayerIndex ) || Children[ SubLayerIndex ]->LayerIdentifier.Get() != SubLayerIdentifier )
					{
						Children.Reset();
						bNeedsRefresh = true;
						break;
					}

					++SubLayerIndex;
				}

				if ( !bNeedsRefresh && SubLayerIndex < Children.Num() )
				{
					Children.Reset();
					bNeedsRefresh = true;
				}
			}
		}

		if ( bNeedsRefresh )
		{
			FillChildren();
		}

		return Children;
	}

	void FillChildren()
	{
		Children.Reset();

		if ( !IsValid() )
		{
			return;
		}

		{
			FScopedUsdAllocs UsdAllocs;

			pxr::SdfLayerHandle UsdLayer = GetLayerHandle().Get();

			if ( UsdLayer )
			{
				for ( std::string SubLayerPath : UsdLayer->GetSubLayerPaths() )
				{
					std::string AssetPathRelativeToLayer = pxr::SdfComputeAssetPathRelativeToLayer( UsdLayer, SubLayerPath );

					Children.Add( MakeShared< FUsdLayersTreeItem >( this, UsdStage, MakeUsdStore< std::string >( AssetPathRelativeToLayer ) ) );
				}
			}
		}
	}

	void RefreshData()
	{
		if ( !IsValid() )
		{
			return;
		}

		Children = GetChildren();

		FScopedUsdAllocs UsdAllocs;

		LayerData->DisplayName = FText::FromString( UsdToUnreal::ConvertString( pxr::SdfLayer::GetDisplayNameFromIdentifier( LayerIdentifier.Get() ) ) );
		LayerData->bIsMuted = UsdStage.Get()->IsLayerMuted( LayerIdentifier.Get() );
		LayerData->bIsEditTarget = ( UsdStage.Get()->GetEditTarget().GetLayer()->GetIdentifier() == LayerIdentifier.Get() );

		for ( FUsdLayersTreeItemRef Child : Children )
		{
			Child->RefreshData();
		}
	}

	TUsdStore< pxr::SdfLayerHandle > GetLayerHandle() const
	{
		return MakeUsdStore< pxr::SdfLayerHandle >( pxr::SdfLayer::FindOrOpen( LayerIdentifier.Get() ) );
	}

public:
	TSharedRef< FUsdLayerData > LayerData; // Data model
	FUsdLayersTreeItem* ParentItem;

	TUsdStore< pxr::UsdStageRefPtr > UsdStage;
	TUsdStore< std::string > LayerIdentifier;

	TArray< FUsdLayersTreeItemRef > Children;
};

class FUsdLayerNameColumn : public FUsdTreeViewColumn
{
public:
	virtual TSharedRef< SWidget > GenerateWidget( const TSharedPtr< IUsdTreeViewItem > InTreeItem ) override
	{
		FUsdLayersTreeItemPtr TreeItem = StaticCastSharedRef< FUsdLayersTreeItem >( InTreeItem.ToSharedRef() );

		TSharedRef< STextBlock > Item =
			SNew(STextBlock)
				.TextStyle( FEditorStyle::Get(), "LargeText" )
				.Text( TreeItem->LayerData, &FUsdLayerData::GetDisplayName )
				.Font( FEditorStyle::GetFontStyle( "ContentBrowser.SourceTreeItemFont" ) );

		return Item;
	}
};

class FUsdLayerMutedColumn : public FUsdTreeViewColumn, public TSharedFromThis< FUsdLayerMutedColumn >
{
public:
	FReply OnClicked( const FUsdLayersTreeItemRef TreeItem )
	{
		ToggleMuteLayer( TreeItem );

		return FReply::Handled();
	}

	const FSlateBrush* GetBrush( const FUsdLayersTreeItemRef TreeItem ) const
	{
		if ( !CanMuteLayer( TreeItem ) )
		{
			return nullptr;
		}
		else if ( TreeItem->LayerData->bIsMuted )
		{
			return FEditorStyle::GetBrush( "Level.NotVisibleIcon16x" );
		}
		else
		{
			return FEditorStyle::GetBrush( "Level.VisibleIcon16x" );
		}
	}

	virtual TSharedRef< SWidget > GenerateWidget( const TSharedPtr< IUsdTreeViewItem > InTreeItem ) override
	{
		if ( !InTreeItem )
		{
			return SNullWidget::NullWidget;
		}

		FUsdLayersTreeItemRef TreeItem = StaticCastSharedRef< FUsdLayersTreeItem >( InTreeItem.ToSharedRef() );

		TSharedRef< SWidget > Item =
			SNew( SButton )
			.ContentPadding(0)
			.ButtonStyle( FEditorStyle::Get(), "ToggleButton" )
			.OnClicked( this, &FUsdLayerMutedColumn::OnClicked, TreeItem )
			.HAlign( HAlign_Center )
			.VAlign( VAlign_Center )
			.Content()
			[
				SNew( SImage )
				.Image( this, &FUsdLayerMutedColumn::GetBrush, TreeItem )
			];
		
		return Item;
	}

protected:
	bool CanMuteLayer( FUsdLayersTreeItemRef LayerItem ) const
	{
		if ( !LayerItem->IsValid() )
		{
			return false;
		}

		FScopedUsdAllocs UsdAllocs;
		return ( ( LayerItem->UsdStage.Get()->GetRootLayer()->GetIdentifier() != LayerItem->LayerIdentifier.Get() ) && !LayerItem->LayerData->bIsEditTarget );
	}

	void ToggleMuteLayer( FUsdLayersTreeItemRef LayerItem )
	{
		if ( !LayerItem->IsValid() || !CanMuteLayer( LayerItem ) )
		{
			return;
		}

		FScopedUsdAllocs UsdAllocs;

		const std::string LayerIdentifier = LayerItem->LayerIdentifier.Get();

		if ( LayerItem->UsdStage.Get()->IsLayerMuted( LayerIdentifier ) )
		{
			LayerItem->UsdStage.Get()->UnmuteLayer( LayerIdentifier );
		}
		else
		{
			LayerItem->UsdStage.Get()->MuteLayer( LayerIdentifier );
		}

		LayerItem->RefreshData();
	}
};

class FUsdLayerEditColumn : public FUsdTreeViewColumn, public TSharedFromThis< FUsdLayerEditColumn >
{
public:
	const FSlateBrush* GetCheckedImage( const FUsdLayersTreeItemRef InTreeItem ) const
	{
		return InTreeItem->LayerData->bIsEditTarget ?
			&FEditorStyle::Get().GetWidgetStyle< FCheckBoxStyle >( "Checkbox" ).CheckedImage :
			nullptr;
	}

	virtual TSharedRef< SWidget > GenerateWidget( const TSharedPtr< IUsdTreeViewItem > InTreeItem ) override
	{
		const FUsdLayersTreeItemRef TreeItem = StaticCastSharedRef< FUsdLayersTreeItem >( InTreeItem.ToSharedRef() );

		TSharedRef< SWidget > Item =
			SNew(SImage)
				.Image( this, &FUsdLayerEditColumn::GetCheckedImage, TreeItem )
				.ColorAndOpacity( FEditorStyle::Get().GetWidgetStyle< FCheckBoxStyle >( "Checkbox" ).ForegroundColor );
		
		return Item;
	}
};

void SUsdLayersTreeView::Construct( const FArguments& InArgs, AUsdStageActor* UsdStageActor )
{
	SUsdTreeView::Construct( SUsdTreeView::FArguments() );

	OnContextMenuOpening = FOnContextMenuOpening::CreateSP( this, &SUsdLayersTreeView::ConstructLayerContextMenu );

	BuildUsdLayersEntries( UsdStageActor );
}

void SUsdLayersTreeView::Refresh( AUsdStageActor* UsdStageActor, bool bResync )
{
	if ( bResync )
	{
		BuildUsdLayersEntries( UsdStageActor );
	}
	else
	{
		for ( FUsdLayersTreeItemRef TreeItem :  RootItems )
		{
			TreeItem->RefreshData();
		}
	}

	RequestTreeRefresh();
}

TSharedRef< ITableRow > SUsdLayersTreeView::OnGenerateRow( FUsdLayersTreeItemRef InDisplayNode, const TSharedRef< STableViewBase >& OwnerTable )
{
	return SNew( SUsdTreeRow< FUsdLayersTreeItemRef >, InDisplayNode, OwnerTable, SharedData );
}

void SUsdLayersTreeView::OnGetChildren( FUsdLayersTreeItemRef InParent, TArray< FUsdLayersTreeItemRef >& OutChildren ) const
{
	for ( const FUsdLayersTreeItemRef& Child : InParent->GetChildren() )
	{
		OutChildren.Add( Child );
	}
}

void SUsdLayersTreeView::BuildUsdLayersEntries( AUsdStageActor* UsdStageActor )
{
	RootItems.Empty();

	if ( !UsdStageActor )
	{
		return;
	}

	const pxr::UsdStageRefPtr& UsdStage = UsdStageActor->GetUsdStage();

	if ( UsdStage )
	{
		FScopedUsdAllocs UsdAllocs;

		RootItems.Add( MakeSharedUnreal< FUsdLayersTreeItem >( nullptr, UsdStage, UsdStage->GetRootLayer()->GetIdentifier() ) );
		RootItems.Add( MakeSharedUnreal< FUsdLayersTreeItem >( nullptr, UsdStage, UsdStage->GetSessionLayer()->GetIdentifier() ) );
	}
}

void SUsdLayersTreeView::SetupColumns()
{
	HeaderRowWidget->ClearColumns();

	SHeaderRow::FColumn::FArguments LayerMutedColumnArguments;
	LayerMutedColumnArguments.FixedWidth( 24.f );

	TSharedRef< FUsdLayerMutedColumn > LayerMutedColumn = MakeShared< FUsdLayerMutedColumn >();
	AddColumn( TEXT("Mute"), FText(), LayerMutedColumn, LayerMutedColumnArguments );

	TSharedRef< FUsdLayerNameColumn > LayerNameColumn = MakeShared< FUsdLayerNameColumn >();
	LayerNameColumn->bIsMainColumn = true;

	AddColumn( TEXT("Layers"), LOCTEXT( "Layers", "Layers" ), LayerNameColumn );

	TSharedRef< FUsdLayerEditColumn > LayerEditColumn = MakeShared< FUsdLayerEditColumn >();
	AddColumn( TEXT("Edit"), LOCTEXT( "Edit", "Edit" ), LayerEditColumn );
}

TSharedPtr< SWidget > SUsdLayersTreeView::ConstructLayerContextMenu()
{
	TSharedRef< SWidget > MenuWidget = SNullWidget::NullWidget;

	FMenuBuilder LayerOptions( true, nullptr );
	LayerOptions.BeginSection( "Layer", LOCTEXT("Layer", "Layer") );
	{
		LayerOptions.AddMenuEntry(
			LOCTEXT("EditLayer", "Edit"),
			LOCTEXT("EditLayer_ToolTip", "Sets the layer as the edit target"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP( this, &SUsdLayersTreeView::OnEditSelectedLayer ),
				FCanExecuteAction::CreateSP( this, &SUsdLayersTreeView::CanEditSelectedLayer )
			),
			NAME_None,
			EUserInterfaceActionType::Button
		);
	}
	LayerOptions.EndSection();

	LayerOptions.BeginSection( "SubLayers", LOCTEXT("SubLayers", "SubLayers") );
	{
		LayerOptions.AddMenuEntry(
			LOCTEXT("AddExistingSubLayer", "Add Existing"),
			LOCTEXT("AddExistingSubLayer_ToolTip", "Adds a sublayer from an existing file to this layer"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP( this, &SUsdLayersTreeView::OnAddSubLayer ),
				FCanExecuteAction()
			),
			NAME_None,
			EUserInterfaceActionType::Button
		);

		LayerOptions.AddMenuEntry(
			LOCTEXT("AddNewSubLayer", "Add New"),
			LOCTEXT("AddNewSubLayer_ToolTip", "Adds a sublayer using a new file to this layer"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP( this, &SUsdLayersTreeView::OnNewSubLayer ),
				FCanExecuteAction()
			),
			NAME_None,
			EUserInterfaceActionType::Button
		);

		LayerOptions.AddMenuEntry(
			LOCTEXT("RemoveSubLayer", "Remove"),
			LOCTEXT("RemoveSubLayer_ToolTip", "Removes the sublayer from its owner"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP( this, &SUsdLayersTreeView::OnRemoveSelectedLayers ),
				FCanExecuteAction::CreateSP( this, &SUsdLayersTreeView::CanRemoveSelectedLayers )
			),
			NAME_None,
			EUserInterfaceActionType::Button
		);
	}
	LayerOptions.EndSection();

	MenuWidget = LayerOptions.MakeWidget();

	return MenuWidget;
}

bool SUsdLayersTreeView::CanEditLayer( FUsdLayersTreeItemRef LayerItem ) const
{
	return !LayerItem->LayerData->bIsMuted;
}

bool SUsdLayersTreeView::CanEditSelectedLayer() const
{
	bool bHasEditableLayer = false;

	TArray< FUsdLayersTreeItemRef > MySelectedItems = GetSelectedItems();

	for ( FUsdLayersTreeItemRef SelectedItem : MySelectedItems )
	{
		if ( CanEditLayer( SelectedItem ) )
		{
			bHasEditableLayer = true;
			break;
		}
	}

	return bHasEditableLayer;
}

void SUsdLayersTreeView::OnEditSelectedLayer()
{
	TArray< FUsdLayersTreeItemRef > MySelectedItems = GetSelectedItems();

	for ( FUsdLayersTreeItemRef SelectedItem : MySelectedItems )
	{
		TUsdStore< pxr::SdfLayerHandle > LayerHandle = SelectedItem->GetLayerHandle();
		if ( !LayerHandle.Get() || !CanEditLayer( SelectedItem ) )
		{
			continue;
		}

		SelectedItem->UsdStage.Get()->SetEditTarget( LayerHandle.Get() );
		SelectedItem->RefreshData();
		break;
	}
}

void SUsdLayersTreeView::OnAddSubLayer()
{
	TOptional< FString > LayerFile = UsdUtils::BrowseUsdFile( UsdUtils::EBrowseFileMode::Open, AsShared() );

	if ( !LayerFile )
	{
		return;
	}

	TArray< FUsdLayersTreeItemRef > MySelectedItems = GetSelectedItems();

	for ( FUsdLayersTreeItemRef SelectedItem : MySelectedItems )
	{
		FScopedUsdAllocs UsdAllocs;

		pxr::SdfLayerHandle LayerHandle = SelectedItem->GetLayerHandle().Get();
		if ( LayerHandle )
		{
			LayerHandle->InsertSubLayerPath( UnrealToUsd::ConvertString( *LayerFile.GetValue() ).Get() );
		}
		
		break;
	}

	RequestTreeRefresh();
}

void SUsdLayersTreeView::OnNewSubLayer()
{
	TOptional< FString > LayerFile = UsdUtils::BrowseUsdFile( UsdUtils::EBrowseFileMode::Save, AsShared() );

	if ( !LayerFile )
	{
		return;
	}

	TArray< FUsdLayersTreeItemRef > MySelectedItems = GetSelectedItems();

	{
		FScopedUsdAllocs UsdAllocs;
		for ( FUsdLayersTreeItemRef SelectedItem : MySelectedItems )
		{
			pxr::SdfLayerRefPtr SubLayerHandle = UsdUtils::CreateNewLayer( SelectedItem->UsdStage, *LayerFile.GetValue() ).Get();

			pxr::SdfLayerHandle LayerHandle = SelectedItem->GetLayerHandle().Get();

			if ( LayerHandle && SubLayerHandle )
			{
				LayerHandle->InsertSubLayerPath( SubLayerHandle->GetRealPath() );
			}
			
			break;
		}
	}

	RequestTreeRefresh();
}

bool SUsdLayersTreeView::CanRemoveLayer( FUsdLayersTreeItemRef LayerItem ) const
{
	// We can't remove root layers
	return ( LayerItem->IsValid() && LayerItem->ParentItem && LayerItem->ParentItem->IsValid() );
}

bool SUsdLayersTreeView::CanRemoveSelectedLayers() const
{
	bool bHasRemovableLayer = false;

	TArray< FUsdLayersTreeItemRef > SelectedLayers = GetSelectedItems();

	for ( FUsdLayersTreeItemRef SelectedLayer : SelectedLayers )
	{
		// We can't remove root layers
		if ( CanRemoveLayer( SelectedLayer ) )
		{
			bHasRemovableLayer = true;
			break;
		}
	}

	return bHasRemovableLayer;
}

void SUsdLayersTreeView::OnRemoveSelectedLayers()
{
	bool bLayerRemoved = false;

	TArray< FUsdLayersTreeItemRef > SelectedLayers = GetSelectedItems();

	for ( FUsdLayersTreeItemRef SelectedLayer : SelectedLayers )
	{
		if ( !CanRemoveLayer( SelectedLayer ) )
		{
			continue;
		}

		{
			FScopedUsdAllocs UsdAllocs;

			int32 SubLayerIndex = 0;
			for ( FUsdLayersTreeItemRef Child : SelectedLayer->ParentItem->Children )
			{
				if ( Child->LayerIdentifier.Get() == SelectedLayer->LayerIdentifier.Get() )
				{
					pxr::SdfLayerHandle ParentLayerHandle = SelectedLayer->ParentItem->GetLayerHandle().Get();

					if ( ParentLayerHandle )
					{
						ParentLayerHandle->RemoveSubLayerPath( SubLayerIndex );
						//SelectedLayer->ParentItem->Children.Remove( Child );
						bLayerRemoved = true;
					}
					break;
				}

				++SubLayerIndex;
			}
		}
	}

	if ( bLayerRemoved )
	{
		RequestTreeRefresh();
	}
}

#undef LOCTEXT_NAMESPACE

#endif // #if USE_USD_SDK
