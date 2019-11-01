// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SUSDStageTreeView.h"

#include "USDLayerUtils.h"
#include "USDStageActor.h"
#include "USDStageModule.h"
#include "USDTypesConversion.h"

#include "EditorStyleSet.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "UsdStageTreeView"

#if USE_USD_SDK

#include "USDIncludesStart.h"

#include "pxr/pxr.h"
#include "pxr/usd/sdf/path.h"
#include "pxr/usd/usd/modelAPI.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/references.h"
#include "pxr/usd/usdGeom/xform.h"

#include "USDIncludesEnd.h"

#include <iterator>

enum class EPayloadsTrigger
{
	Load,
	Unload,
	Toggle
};

class FUsdStageTreeRowData : public TSharedFromThis< FUsdStageTreeRowData >
{
public:
	FText GetName() const { return Name; }
	FText GetType() const { return Type; }
	bool HasPayload() const { return bHasPayload; }
	bool IsLoaded() const { return bIsLoaded; }
	bool HasCompositionArcs() const {return bHasCompositionArcs; }
	bool IsVisible() const { return bIsVisible; }

	FText Name;
	FText Type;
	bool bHasPayload = false;
	bool bIsLoaded = false;
	bool bHasCompositionArcs = false;
	bool bIsVisible = true;
};

class FUsdStageTreeItem : public IUsdTreeViewItem, public TSharedFromThis< FUsdStageTreeItem >
{
public:
	FUsdStageTreeItem( FUsdStageTreeItem* InParentItem, const TUsdStore< pxr::UsdStageRefPtr >& InUsdStage, const TUsdStore< pxr::UsdPrim >& InUsdPrim )
		: FUsdStageTreeItem( InParentItem, InUsdStage )
	{
		UsdPrim = InUsdPrim;

		RefreshData( false );
		FillChildren();
	}

	FUsdStageTreeItem( FUsdStageTreeItem* InParentItem, const TUsdStore< pxr::UsdStageRefPtr >& InUsdStage )
		: UsdStage( InUsdStage )
		, ParentItem( InParentItem )
		, RowData( MakeShared< FUsdStageTreeRowData >() )
	{
	}

	TArray< FUsdStageTreeItemRef >& UpdateChildren()
	{
		if ( !UsdPrim.Get() )
		{
			return Children;
		}

		bool bNeedsRefresh = false;

		pxr::UsdPrimSiblingRange PrimChildren = UsdPrim.Get().GetFilteredChildren( pxr::UsdTraverseInstanceProxies( pxr::UsdPrimAllPrimsPredicate ) );

		const int32 NumUsdChildren = (TArray< FUsdStageTreeItemRef >::SizeType )std::distance( PrimChildren.begin(), PrimChildren.end() );
		const int32 NumUnrealChildren = [&]()
			{
				int32 ValidPrims = 0;
				for ( const FUsdStageTreeItemRef& Child : Children )
				{
					if ( !Child->RowData->Name.IsEmpty() )
					{
						++ValidPrims;
					}
				}

				return ValidPrims;
			}();

		if ( NumUsdChildren != NumUnrealChildren )
		{
			Children.Reset();
			bNeedsRefresh = true;
		}
		else
		{
			int32 ChildIndex = 0;

			for ( const pxr::UsdPrim& Child : PrimChildren )
			{
				if ( !Children.IsValidIndex( ChildIndex ) || Children[ ChildIndex ]->UsdPrim.Get().GetPrimPath() != Child.GetPrimPath() )
				{
					Children.Reset();
					bNeedsRefresh = true;
					break;
				}
				
				++ChildIndex;
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
		pxr::UsdPrimSiblingRange PrimChildren = UsdPrim.Get().GetFilteredChildren( pxr::UsdTraverseInstanceProxies( pxr::UsdPrimAllPrimsPredicate ) );
		for ( pxr::UsdPrim Child : PrimChildren )
		{
			Children.Add( MakeShared< FUsdStageTreeItem >( this, UsdStage, Child ) );
		}
	}

	void RefreshData( bool bRefreshChildren )
	{
		FString PrimName;

		if ( !UsdPrim.Get() )
		{
			if ( ParentItem )
			{
				ParentItem->RefreshData( true );
			}

			RowData.Get() = FUsdStageTreeRowData();
			return;
		}
		
		RowData->Name = FText::FromString( UsdToUnreal::ConvertString( UsdPrim.Get().GetName() ) );
		RowData->bHasCompositionArcs = UsdUtils::HasCompositionArcs( UsdPrim.Get() );

		RowData->Type = FText::FromString( UsdToUnreal::ConvertString( UsdPrim.Get().GetTypeName().GetString() ) );
		RowData->bHasPayload = UsdPrim.Get().HasPayload();
		RowData->bIsLoaded = UsdPrim.Get().IsLoaded();

		if ( pxr::UsdGeomImageable UsdGeomImageable = pxr::UsdGeomImageable( UsdPrim.Get() ) )
		{
			RowData->bIsVisible = ( UsdGeomImageable.ComputeVisibility() != pxr::UsdGeomTokens->invisible );
		}

		if ( bRefreshChildren )
		{
			for ( FUsdStageTreeItemRef& Child : UpdateChildren() )
			{
				Child->RefreshData( bRefreshChildren );
			}
		}
	}

	/** Delegate for hooking up an inline editable text block to be notified that a rename is requested. */
	DECLARE_DELEGATE( FOnRenameRequest );

	/** Broadcasts whenever a rename is requested */
	FOnRenameRequest RenameRequestEvent;

public:
	TUsdStore< pxr::UsdStageRefPtr > UsdStage;
	TUsdStore< pxr::UsdPrim > UsdPrim;
	FUsdStageTreeItem* ParentItem;

	TArray< FUsdStageTreeItemRef > Children;
	
	TSharedRef< FUsdStageTreeRowData > RowData; // Data model
};

class FUsdStageNameColumn : public FUsdTreeViewColumn, public TSharedFromThis< FUsdStageNameColumn >
{
public:
	DECLARE_DELEGATE_TwoParams( FOnPrimNameCommitted, const FUsdStageTreeItemRef&, const FText& );
	FOnPrimNameCommitted OnPrimNameCommitted;

	TWeakPtr< SUsdStageTreeView > OwnerTree;

	virtual TSharedRef< SWidget > GenerateWidget( const TSharedPtr< IUsdTreeViewItem > InTreeItem ) override
	{
		if ( !InTreeItem )
		{
			return SNullWidget::NullWidget;
		}

		TSharedPtr< FUsdStageTreeItem > TreeItem = StaticCastSharedPtr< FUsdStageTreeItem >( InTreeItem );

		TSharedRef< SInlineEditableTextBlock > Item =
			SNew( SInlineEditableTextBlock )
			.Text( TreeItem->RowData, &FUsdStageTreeRowData::GetName )
			.Font( FEditorStyle::GetFontStyle( "ContentBrowser.SourceTreeItemFont" ) )
			.ColorAndOpacity( this, &FUsdStageNameColumn::GetTextColor, TreeItem )
			.OnTextCommitted( this, &FUsdStageNameColumn::OnTextCommitted, TreeItem )
			.IsReadOnly_Lambda( [ TreeItem ]()
			{
				return !TreeItem || TreeItem->UsdPrim.Get();
			} );

		TreeItem->RenameRequestEvent.BindSP( &Item.Get(), &SInlineEditableTextBlock::EnterEditingMode );

		return Item;
	}

protected:
	void OnTextCommitted( const FText& InPrimName, ETextCommit::Type InCommitInfo, TSharedPtr< FUsdStageTreeItem > TreeItem )
	{
		if ( !TreeItem )
		{
			return;
		}

		OnPrimNameCommitted.ExecuteIfBound( TreeItem.ToSharedRef(), InPrimName );
	}

	FSlateColor GetTextColor( TSharedPtr< FUsdStageTreeItem > TreeItem ) const
	{
		FSlateColor TextColor = FLinearColor::White;

		if ( TreeItem && TreeItem->RowData->HasCompositionArcs() )
		{
			const TSharedPtr< SUsdStageTreeView > OwnerTreePtr = OwnerTree.Pin();
			if ( OwnerTreePtr && OwnerTreePtr->IsItemSelected( TreeItem.ToSharedRef() ) )
			{
				TextColor = FSlateColor::UseForeground();
			}
			else
			{
				TextColor = FLinearColor( FColor::Orange );
			}
		}
		
		return TextColor;
	}
};

class FUsdStagePayloadColumn : public FUsdTreeViewColumn, public TSharedFromThis< FUsdStagePayloadColumn >
{
public:
	ECheckBoxState IsChecked( const FUsdStageTreeItemPtr InTreeItem ) const
	{
		ECheckBoxState CheckedState = ECheckBoxState::Unchecked;

		if ( InTreeItem && InTreeItem->RowData->HasPayload() )
		{
			CheckedState = InTreeItem->RowData->IsLoaded() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		}

		return CheckedState;
	}

	void OnCheckedPayload( ECheckBoxState NewCheckedState, const FUsdStageTreeItemPtr TreeItem )
	{
		if ( !TreeItem )
		{
			return;
		}

		switch ( NewCheckedState )
		{
		case ECheckBoxState::Checked :
			TreeItem->UsdPrim.Get().Load();
			break;
		case ECheckBoxState::Unchecked:
			TreeItem->UsdPrim.Get().Unload();
			break;
		default:
			break;
		}
	}

	virtual TSharedRef< SWidget > GenerateWidget( const TSharedPtr< IUsdTreeViewItem > InTreeItem ) override
	{
		bool bHasPayload = false;

		if ( InTreeItem )
		{
			const TSharedPtr< FUsdStageTreeItem > TreeItem = StaticCastSharedPtr< FUsdStageTreeItem >( InTreeItem );
			bHasPayload = TreeItem->RowData->HasPayload();
		}

		TSharedRef< SWidget > Item = SNullWidget::NullWidget;
		
		if ( bHasPayload )
		{
			Item = SNew( SCheckBox )
					.IsChecked( this, &FUsdStagePayloadColumn::IsChecked, StaticCastSharedPtr< FUsdStageTreeItem >( InTreeItem ) )
					.OnCheckStateChanged( this, &FUsdStagePayloadColumn::OnCheckedPayload, StaticCastSharedPtr< FUsdStageTreeItem >( InTreeItem ) );
		}
		
		return Item;
	}
};

class FUsdStageVisibilityColumn : public FUsdTreeViewColumn, public TSharedFromThis< FUsdStageVisibilityColumn >
{
public:
	TUsdStore< pxr::UsdGeomImageable > GetUsdGeomImageable( const FUsdStageTreeItemPtr TreeItem ) const
	{
		if ( !TreeItem || !TreeItem->UsdPrim.Get() )
		{
			return {};
		}

		return pxr::UsdGeomImageable( TreeItem->UsdPrim.Get() );
	}

	FReply OnToggleVisibility( const FUsdStageTreeItemPtr TreeItem )
	{
		if ( pxr::UsdGeomImageable UsdGeomImageable = GetUsdGeomImageable( TreeItem ).Get() )
		{
			if ( TreeItem->RowData->IsVisible() )
			{
				UsdGeomImageable.MakeInvisible();
			}
			else
			{
				UsdGeomImageable.MakeVisible();
			}
		}

		return FReply::Handled();
	}

	const FSlateBrush* GetBrush( const FUsdStageTreeItemPtr TreeItem ) const
	{
		if ( TreeItem->RowData->IsVisible() )
		{
			return FEditorStyle::GetBrush( "Level.VisibleIcon16x" );
		}
		else
		{
			return FEditorStyle::GetBrush( "Level.NotVisibleIcon16x" );
		}
	}

	virtual TSharedRef< SWidget > GenerateWidget( const TSharedPtr< IUsdTreeViewItem > InTreeItem ) override
	{
		const TSharedPtr< FUsdStageTreeItem > TreeItem = StaticCastSharedPtr< FUsdStageTreeItem >( InTreeItem );

		TSharedRef< SWidget > Item = SNew( SButton )
										.ContentPadding(0)
										.ButtonStyle( FEditorStyle::Get(), "ToggleButton" )
										.OnClicked( this, &FUsdStageVisibilityColumn::OnToggleVisibility, TreeItem )
										.HAlign( HAlign_Center )
										.VAlign( VAlign_Center )
										.Content()
										[
											SNew( SImage )
											.Image( this, &FUsdStageVisibilityColumn::GetBrush, TreeItem )
										];
		
		return Item;
	}
};

class FUsdStagePrimTypeColumn : public FUsdTreeViewColumn
{
public:
	virtual TSharedRef< SWidget > GenerateWidget( const TSharedPtr< IUsdTreeViewItem > InTreeItem ) override
	{
		TSharedPtr< FUsdStageTreeItem > TreeItem = StaticCastSharedPtr< FUsdStageTreeItem >( InTreeItem );

		TSharedRef< STextBlock > Item =
			SNew(STextBlock)
				.TextStyle( FEditorStyle::Get(), "LargeText" )
				.Text( TreeItem->RowData, &FUsdStageTreeRowData::GetType )
				.Font( FEditorStyle::GetFontStyle( "ContentBrowser.SourceTreeItemFont" ) );
		
		return Item;
	}
};

void SUsdStageTreeView::Construct( const FArguments& InArgs, AUsdStageActor* InUsdStageActor )
{
	SUsdTreeView::Construct( SUsdTreeView::FArguments() );

	OnContextMenuOpening = FOnContextMenuOpening::CreateSP( this, &SUsdStageTreeView::ConstructPrimContextMenu );

	OnSelectionChanged = FOnSelectionChanged::CreateLambda( [ this ]( FUsdStageTreeItemPtr UsdStageTreeItem, ESelectInfo::Type SelectionType )
	{
		if ( !UsdStageTreeItem )
		{
			return;
		}

		this->OnPrimSelected.ExecuteIfBound( UsdToUnreal::ConvertPath( UsdStageTreeItem->UsdPrim.Get().GetPrimPath() ) );
	} );

	OnPrimSelected = InArgs._OnPrimSelected;

	Refresh( InUsdStageActor );
}

TSharedRef< ITableRow > SUsdStageTreeView::OnGenerateRow( FUsdStageTreeItemRef InDisplayNode, const TSharedRef<STableViewBase>& OwnerTable )
{
	return SNew( SUsdTreeRow< FUsdStageTreeItemRef >, InDisplayNode, OwnerTable, SharedData );
}

void SUsdStageTreeView::OnGetChildren( FUsdStageTreeItemRef InParent, TArray< FUsdStageTreeItemRef >& OutChildren ) const
{
	for ( const FUsdStageTreeItemRef& Child : InParent->UpdateChildren() )
	{
		OutChildren.Add( Child );
	}
}

void SUsdStageTreeView::Refresh( AUsdStageActor* InUsdStageActor )
{
	RootItems.Empty();

	UsdStageActor = InUsdStageActor;

	if ( !UsdStageActor.IsValid() )
	{
		return;
	}

	TUsdStore< pxr::UsdStageRefPtr > UsdStage = UsdStageActor->GetUsdStage();

	if ( UsdStage.Get() )
	{
		if ( pxr::UsdPrim RootPrim = UsdStage.Get()->GetPseudoRoot() )
		{
			for ( const pxr::UsdPrim& Child : RootPrim.GetChildren() )
			{
				RootItems.Add( MakeShared< FUsdStageTreeItem >( nullptr, UsdStage, Child ) );
			}
		}
	}
}

void SUsdStageTreeView::RefreshPrim( const FString& PrimPath, bool bResync )
{
	FScopedUnrealAllocs UnrealAllocs; // RefreshPrim can be called by a delegate for which we don't know the active allocator

	TUsdStore< pxr::SdfPath > UsdPrimPath = UnrealToUsd::ConvertPath( *PrimPath );

	TFunction< FUsdStageTreeItemPtr( const TUsdStore< pxr::SdfPath >&, const FUsdStageTreeItemRef& ) > FindTreeItemFromPrimPath;
	FindTreeItemFromPrimPath = [ &FindTreeItemFromPrimPath ]( const TUsdStore< pxr::SdfPath >& UsdPrimPath, const FUsdStageTreeItemRef& ItemRef ) -> FUsdStageTreeItemPtr
	{
		if ( ItemRef->UsdPrim.Get().GetPrimPath() == UsdPrimPath.Get() )
		{
			return ItemRef;
		}
		else
		{
			for ( FUsdStageTreeItemRef ChildItem : ItemRef->Children )
			{
				if ( FUsdStageTreeItemPtr ChildValue = FindTreeItemFromPrimPath( UsdPrimPath, ChildItem ) )
				{
					return ChildValue;
				}
			}
		}

		return {};
	};
	
	bool bNeedsFullRefresh = false;

	for ( FUsdStageTreeItemRef RootItem : this->RootItems )
	{
		TUsdStore< pxr::SdfPath > PrimPathToSearch = UsdPrimPath;

		bool bFoundPrimToRefresh = false;

		while ( !bFoundPrimToRefresh )
		{
			if ( FUsdStageTreeItemPtr FoundItem = FindTreeItemFromPrimPath( PrimPathToSearch, RootItem ) )
			{
				FoundItem->RefreshData( true );
				bFoundPrimToRefresh = true;

				break;
			}
			else
			{
				TUsdStore< pxr::SdfPath > ParentPrimPath = PrimPathToSearch.Get().GetParentPath();

				if ( ParentPrimPath.Get() == PrimPathToSearch.Get() )
				{
					break;
				}
				else
				{
					PrimPathToSearch = MoveTemp( ParentPrimPath );
				}
			}
		}

		if ( !bFoundPrimToRefresh )
		{
			bNeedsFullRefresh = true; // Prim not found, refresh the whole tree
		}
	}

	if ( bNeedsFullRefresh )
	{
		Refresh( UsdStageActor.Get() );
	}

	if ( bResync )
	{
		RequestTreeRefresh();
	}
}

void SUsdStageTreeView::SetupColumns()
{
	HeaderRowWidget->ClearColumns();

	SHeaderRow::FColumn::FArguments VisibilityColumnArguments;
	VisibilityColumnArguments.FixedWidth( 24.f );

	AddColumn( TEXT( "Visibility" ), FText(), MakeShared< FUsdStageVisibilityColumn >(), VisibilityColumnArguments );
	
	{
		TSharedRef< FUsdStageNameColumn > PrimNameColumn = MakeShared< FUsdStageNameColumn >();
		PrimNameColumn->OwnerTree = SharedThis( this );
		PrimNameColumn->bIsMainColumn = true;
		PrimNameColumn->OnPrimNameCommitted.BindRaw( this, &SUsdStageTreeView::OnPrimNameCommitted );

		SHeaderRow::FColumn::FArguments PrimNameColumnArguments;
		PrimNameColumnArguments.FillWidth( 70.f );

		AddColumn( TEXT("Prim"), LOCTEXT( "Prim", "Prim" ), PrimNameColumn, PrimNameColumnArguments );
	}
	
	SHeaderRow::FColumn::FArguments TypeColumnArguments;
	TypeColumnArguments.FillWidth( 15.f );
	AddColumn( TEXT("Type"), LOCTEXT( "Type", "Type" ), MakeShared< FUsdStagePrimTypeColumn >(), TypeColumnArguments );

	SHeaderRow::FColumn::FArguments PayloadColumnArguments;
	PayloadColumnArguments.FillWidth( 15.f ).HAlignHeader( HAlign_Center ).HAlignCell( HAlign_Center );
	AddColumn( TEXT("Payload"), LOCTEXT( "Payload", "Payload" ), MakeShared< FUsdStagePayloadColumn >(), PayloadColumnArguments );
}

TSharedPtr< SWidget > SUsdStageTreeView::ConstructPrimContextMenu()
{
	TSharedRef< SWidget > MenuWidget = SNullWidget::NullWidget;

	FMenuBuilder PrimOptions( true, nullptr );
	PrimOptions.BeginSection( "Prim", LOCTEXT("Prim", "Prim") );
	{
		PrimOptions.AddMenuEntry(
			LOCTEXT("AddPrim", "Add Prim"),
			LOCTEXT("AddPrim_ToolTip", "Adds a new prim"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP( this, &SUsdStageTreeView::OnAddPrim ),
				FCanExecuteAction::CreateSP( this, &SUsdStageTreeView::CanExecutePrimAction )
			),
			NAME_None,
			EUserInterfaceActionType::Button
		);

		PrimOptions.AddMenuEntry(
			LOCTEXT("RemovePrim", "Remove Prim"),
			LOCTEXT("RemovePrim_ToolTip", "Removes the prim and its children"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP( this, &SUsdStageTreeView::OnRemovePrim ),
				FCanExecuteAction::CreateSP( this, &SUsdStageTreeView::CanExecutePrimAction )
			),
			NAME_None,
			EUserInterfaceActionType::Button
		);
	}
	PrimOptions.EndSection();

	PrimOptions.BeginSection( "Payloads", LOCTEXT("Payloads", "Payloads") );
	{
		PrimOptions.AddMenuEntry(
			LOCTEXT("TogglePayloads", "Toggle All Payloads"),
			LOCTEXT("TogglePayloads_ToolTip", "Toggles all payloads for this prim and its children"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP( this, &SUsdStageTreeView::OnToggleAllPayloads, EPayloadsTrigger::Toggle ),
				FCanExecuteAction()
			),
			NAME_None,
			EUserInterfaceActionType::Button
		);

		PrimOptions.AddMenuEntry(
			LOCTEXT("LoadPayloads", "Load All Payloads"),
			LOCTEXT("LoadPayloads_ToolTip", "Loads all payloads for this prim and its children"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP( this, &SUsdStageTreeView::OnToggleAllPayloads, EPayloadsTrigger::Load ),
				FCanExecuteAction()
			),
			NAME_None,
			EUserInterfaceActionType::Button
		);

		PrimOptions.AddMenuEntry(
			LOCTEXT("UnloadPayloads", "Unload All Payloads"),
			LOCTEXT("UnloadPayloads_ToolTip", "Unloads all payloads for this prim and its children"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP( this, &SUsdStageTreeView::OnToggleAllPayloads, EPayloadsTrigger::Unload ),
				FCanExecuteAction()
			),
			NAME_None,
			EUserInterfaceActionType::Button
		);
	}
	PrimOptions.EndSection();

	PrimOptions.BeginSection( "Composition", LOCTEXT("Composition", "Composition") );
	{
		PrimOptions.AddMenuEntry(
			LOCTEXT("AddReference", "Add Reference"),
			LOCTEXT("AddReference_ToolTip", "Adds a reference to this prim"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP( this, &SUsdStageTreeView::OnAddReference ),
				FCanExecuteAction::CreateSP( this, &SUsdStageTreeView::CanExecutePrimAction )
			),
			NAME_None,
			EUserInterfaceActionType::Button
		);

		PrimOptions.AddMenuEntry(
			LOCTEXT("ClearReferences", "Clear References"),
			LOCTEXT("ClearReferences_ToolTip", "Clears the references for this prim"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP( this, &SUsdStageTreeView::OnClearReferences ),
				FCanExecuteAction::CreateSP( this, &SUsdStageTreeView::CanExecutePrimAction )
			),
			NAME_None,
			EUserInterfaceActionType::Button
		);
	}
	PrimOptions.EndSection();

	MenuWidget = PrimOptions.MakeWidget();

	return MenuWidget;
}

void SUsdStageTreeView::OnAddPrim()
{
	if ( !UsdStageActor.IsValid() )
	{
		return;
	}

	TArray< FUsdStageTreeItemRef > MySelectedItems = GetSelectedItems();

	for ( FUsdStageTreeItemRef SelectedItem : MySelectedItems )
	{
		FUsdStageTreeItemRef TreeItem = MakeShared< FUsdStageTreeItem >( &SelectedItem.Get(), SelectedItem->UsdStage );
		SelectedItem->Children.Add( TreeItem );

		PendingRenameItem = TreeItem;
		ScrollItemIntoView( TreeItem );
	}

	RequestTreeRefresh();
}

void SUsdStageTreeView::OnRemovePrim()
{
	if ( !UsdStageActor.IsValid() )
	{
		return;
	}

	TArray< FUsdStageTreeItemRef > MySelectedItems = GetSelectedItems();

	for ( FUsdStageTreeItemRef SelectedItem : MySelectedItems )
	{
		UsdStageActor->GetUsdStage()->RemovePrim( SelectedItem->UsdPrim.Get().GetPrimPath() );
	}
}

void SUsdStageTreeView::OnAddReference()
{
	if ( !UsdStageActor.IsValid() )
	{
		return;
	}

	TOptional< FString > PickedFile = UsdUtils::BrowseUsdFile( UsdUtils::EBrowseFileMode::Open, AsShared() );

	if ( !PickedFile )
	{
		return;
	}

	TArray< FUsdStageTreeItemRef > MySelectedItems = GetSelectedItems();

	for ( FUsdStageTreeItemRef SelectedItem : MySelectedItems )
	{
		FScopedUsdAllocs UsdAllocs;

		pxr::UsdReferences References = SelectedItem->UsdPrim.Get().GetReferences();

		FString AbsoluteFilePath = FPaths::ConvertRelativePathToFull( PickedFile.GetValue() );

		FPaths::MakePathRelativeTo( AbsoluteFilePath, *UsdStageActor->RootLayer.FilePath );
		References.AddReference( UnrealToUsd::ConvertString( *AbsoluteFilePath ).Get() );
	}
}

void SUsdStageTreeView::OnClearReferences()
{
	if ( !UsdStageActor.IsValid() )
	{
		return;
	}

	TArray< FUsdStageTreeItemRef > MySelectedItems = GetSelectedItems();

	for ( FUsdStageTreeItemRef SelectedItem : MySelectedItems )
	{
		FScopedUsdAllocs UsdAllocs;

		pxr::UsdReferences References = SelectedItem->UsdPrim.Get().GetReferences();
		References.ClearReferences();
	}
}

bool SUsdStageTreeView::CanExecutePrimAction() const
{
	if ( !UsdStageActor.IsValid() )
	{
		return false;
	}

	TUsdStore< pxr::UsdStageRefPtr > UsdStage =  UsdStageActor->GetUsdStage();

	if ( !UsdStage.Get() )
	{
		return false;
	}

	bool bHasPrimSpec = false;

	TArray< FUsdStageTreeItemRef > MySelectedItems = GetSelectedItems();

	for ( FUsdStageTreeItemRef SelectedItem : MySelectedItems )
	{
		pxr::SdfPrimSpecHandle PrimSpec = UsdStage.Get()->GetRootLayer()->GetPrimAtPath( SelectedItem->UsdPrim.Get().GetPrimPath() );
		bHasPrimSpec = bHasPrimSpec || (bool)PrimSpec;

		if ( bHasPrimSpec )
		{
			break;
		}
	}

	return bHasPrimSpec;
}

void SUsdStageTreeView::OnToggleAllPayloads( EPayloadsTrigger PayloadsTrigger )
{
	TArray< FUsdStageTreeItemRef > MySelectedItems = GetSelectedItems();

	for ( FUsdStageTreeItemRef SelectedItem : MySelectedItems )
	{
		if ( SelectedItem->UsdPrim.Get() )
		{
			TFunction< void( FUsdStageTreeItemRef ) > RecursiveTogglePayloads;
			RecursiveTogglePayloads = [ &RecursiveTogglePayloads, PayloadsTrigger ]( FUsdStageTreeItemRef InSelectedItem ) -> void
			{
				pxr::UsdPrim& UsdPrim = InSelectedItem->UsdPrim.Get();

				if ( UsdPrim.HasPayload() )
				{
					if ( PayloadsTrigger == EPayloadsTrigger::Toggle )
					{
						if ( UsdPrim.IsLoaded() )
						{
							UsdPrim.Unload();
						}
						else
						{
							UsdPrim.Load();
						}
					}
					else if ( PayloadsTrigger == EPayloadsTrigger::Load && !UsdPrim.IsLoaded() )
					{
						UsdPrim.Load();
					}
					else if ( PayloadsTrigger == EPayloadsTrigger::Unload && UsdPrim.IsLoaded() )
					{
						UsdPrim.Unload();
					}
				}
				else
				{
					for ( FUsdStageTreeItemRef Child : InSelectedItem->UpdateChildren() )
					{
						RecursiveTogglePayloads( Child );
					}
				}
			};

			RecursiveTogglePayloads( SelectedItem );
		}
	}
}

void SUsdStageTreeView::ScrollItemIntoView( FUsdStageTreeItemRef TreeItem )
{
	FUsdStageTreeItem* Parent = TreeItem->ParentItem;
	while( Parent )
	{
		SetItemExpansion( Parent->AsShared(), true );
		Parent = Parent->ParentItem;
	}

	RequestScrollIntoView( TreeItem );
}

void SUsdStageTreeView::OnTreeItemScrolledIntoView( FUsdStageTreeItemRef TreeItem, const TSharedPtr<ITableRow>& Widget )
{
	if ( TreeItem == PendingRenameItem.Pin() )
	{
		PendingRenameItem = nullptr;
		TreeItem->RenameRequestEvent.ExecuteIfBound();
	}
}

void SUsdStageTreeView::OnPrimNameCommitted( const FUsdStageTreeItemRef& TreeItem, const FText& InPrimName )
{
	if ( InPrimName.IsEmptyOrWhitespace() )
	{
		return;
	}

	{
		FScopedUsdAllocs UsdAllocs;

		pxr::SdfPath ParentPrimPath;
		
		if ( TreeItem->ParentItem )
		{
			ParentPrimPath = TreeItem->ParentItem->UsdPrim.Get().GetPrimPath();
		}
		else
		{
			ParentPrimPath = pxr::SdfPath::AbsoluteRootPath();
		}

		pxr::SdfPath NewPrimPath = ParentPrimPath.AppendChild( pxr::TfToken( UnrealToUsd::ConvertString( *InPrimName.ToString() ).Get() ) );
		
		TreeItem->UsdPrim = pxr::UsdGeomXform::Define( TreeItem->UsdStage.Get(), NewPrimPath ).GetPrim();

		if ( TreeItem->ParentItem )
		{
			TreeItem->ParentItem->Children.Remove( TreeItem );

			RefreshPrim( UsdToUnreal::ConvertPath( TreeItem->ParentItem->UsdPrim.Get().GetPrimPath() ), true );
		}
	}
}

#endif // #if USE_USD_SDK

#undef LOCTEXT_NAMESPACE
