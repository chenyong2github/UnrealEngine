// Copyright Epic Games, Inc. All Rights Reserved.

#include "SUSDStageTreeView.h"

#include "UnrealUSDWrapper.h"
#include "USDLayerUtils.h"
#include "USDStageActor.h"
#include "USDStageModule.h"
#include "USDTypesConversion.h"

#include "EditorStyleSet.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Modules/ModuleManager.h"
#include "ScopedTransaction.h"
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
#include "pxr/usd/usd/schemaRegistry.h"
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
		if ( !UsdPrim.Get() )
		{
			return;
		}

		RowData->Name = FText::FromString( UsdToUnreal::ConvertString( UsdPrim.Get().GetName() ) );
		RowData->bHasCompositionArcs = UsdUtils::HasCompositionArcs( UsdPrim.Get() );

		RowData->Type = FText::FromString( UsdToUnreal::ConvertString( UsdPrim.Get().GetTypeName().GetString() ) );
		RowData->bHasPayload = UsdPrim.Get().HasPayload();
		RowData->bIsLoaded = UsdPrim.Get().IsLoaded();

		bool bOldVisibility = RowData->bIsVisible;
		if ( pxr::UsdGeomImageable UsdGeomImageable = pxr::UsdGeomImageable( UsdPrim.Get() ) )
		{
			RowData->bIsVisible = ( UsdGeomImageable.ComputeVisibility() != pxr::UsdGeomTokens->invisible );
		}

		// If our visibility was enabled, it may be that the visibilities of all of our parents were enabled to accomplish
		// the target change, so we need to refresh them too. This happens when we manually change visibility on
		// a USceneComponent and write that to the USD Stage, for example
		if ( bOldVisibility == false && RowData->bIsVisible )
		{
			FUsdStageTreeItem* Item = ParentItem;
			while ( Item )
			{
				Item->RefreshData(false);
				Item = Item->ParentItem;
			}
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

	DECLARE_DELEGATE_ThreeParams( FOnPrimNameUpdated, const FUsdStageTreeItemRef&, const FText&, FText& );
	FOnPrimNameUpdated OnPrimNameUpdated;

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
			.OnVerifyTextChanged( this, &FUsdStageNameColumn::OnTextUpdated, TreeItem )
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

	bool OnTextUpdated(const FText& InPrimName, FText& ErrorMessage, TSharedPtr< FUsdStageTreeItem > TreeItem)
	{
		if (!TreeItem)
		{
			return false;
		}

		OnPrimNameUpdated.ExecuteIfBound( TreeItem.ToSharedRef(), InPrimName, ErrorMessage );

		return ErrorMessage.IsEmpty();
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
			FScopedTransaction Transaction( FText::Format(
				LOCTEXT( "VisibilityTransaction", "Toggle visibility of prim '{0}'" ),
				FText::FromString( UsdToUnreal::ConvertString( TreeItem->UsdPrim.Get().GetName() ) )
			) );

			if ( TreeItem->RowData->IsVisible() )
			{
				UsdGeomImageable.MakeInvisible();
			}
			else
			{
				UsdGeomImageable.MakeVisible();
			}

			TreeItem->RefreshData( false );
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
		FString SelectedPrimPath;

		if ( UsdStageTreeItem )
		{
			SelectedPrimPath = UsdToUnreal::ConvertPath( UsdStageTreeItem->UsdPrim.Get().GetPrimPath() );
		}

		this->OnPrimSelected.ExecuteIfBound( SelectedPrimPath );
	} );

	OnExpansionChanged = FOnExpansionChanged::CreateLambda([this]( const FUsdStageTreeItemPtr& UsdStageTreeItem, bool bIsExpanded)
	{
		if (!UsdStageTreeItem)
		{
			return;
		}

		const pxr::UsdPrim& Prim = UsdStageTreeItem->UsdPrim.Get();
		if (!Prim)
		{
			return;
		}

		TreeItemExpansionStates.Add(UsdToUnreal::ConvertPath(Prim.GetPrimPath()), bIsExpanded);
	});

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

	if (UsdStageActor.Get() != InUsdStageActor)
	{
		TreeItemExpansionStates.Reset();
	}

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

	RestoreExpansionStates();
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

	// Search for the corresponding tree item to update
	FUsdStageTreeItemPtr FoundItem = nullptr;
	for ( FUsdStageTreeItemRef RootItem : this->RootItems )
	{
		TUsdStore< pxr::SdfPath > PrimPathToSearch = UsdPrimPath;

		FoundItem = FindTreeItemFromPrimPath( PrimPathToSearch, RootItem );

		while ( !FoundItem.IsValid() )
		{
			TUsdStore< pxr::SdfPath > ParentPrimPath = PrimPathToSearch.Get().GetParentPath();
			if ( ParentPrimPath.Get() == PrimPathToSearch.Get() )
			{
				break;
			}
			PrimPathToSearch = MoveTemp( ParentPrimPath );

			FoundItem = FindTreeItemFromPrimPath( PrimPathToSearch, RootItem );
		}

		if (FoundItem.IsValid())
		{
			break;
		}
	}

	if (FoundItem.IsValid())
	{
		FoundItem->RefreshData( true );

		// Item doesn't match any prim, needs to be removed
		if (!FoundItem->UsdPrim.Get())
		{
			if (FoundItem->ParentItem)
			{
				FoundItem->ParentItem->RefreshData( true );
			}
			else
			{
				RootItems.Remove(FoundItem.ToSharedRef());
			}
		}
	}
	// We couldn't find the target prim, do a full refresh instead
	else
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
		PrimNameColumn->OnPrimNameUpdated.BindRaw( this, &SUsdStageTreeView::OnPrimNameUpdated );

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
				FCanExecuteAction::CreateSP( this, &SUsdStageTreeView::CanAddPrim )
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

	// Add a new child prim
	if (MySelectedItems.Num() > 0)
	{
		for ( FUsdStageTreeItemRef SelectedItem : MySelectedItems )
		{
			FUsdStageTreeItemRef TreeItem = MakeShared< FUsdStageTreeItem >( &SelectedItem.Get(), SelectedItem->UsdStage );
			SelectedItem->Children.Add( TreeItem );

			PendingRenameItem = TreeItem;
			ScrollItemIntoView( TreeItem );
		}
	}
	// Add a new top-level prim (direct child of the pseudo-root prim)
	else
	{
		FUsdStageTreeItemRef TreeItem = MakeShared< FUsdStageTreeItem >( nullptr, MakeUsdStore<pxr::UsdStageRefPtr>(UsdStageActor->GetUsdStage()) );
		RootItems.Add( TreeItem );

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
	if ( !UsdStageActor.IsValid() || !UsdStageActor->GetUsdStage() || !UsdStageActor->GetUsdStage()->GetEditTarget().IsValid() )
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
		FString AbsoluteFilePath = FPaths::ConvertRelativePathToFull( PickedFile.GetValue() );

		FScopedUsdAllocs UsdAllocs;

		const std::string UsdAbsoluteFilePath = UnrealToUsd::ConvertString( *AbsoluteFilePath ).Get();
		pxr::UsdReferences References = SelectedItem->UsdPrim.Get().GetReferences();

		pxr::SdfLayerRefPtr ReferenceLayer = pxr::SdfLayer::FindOrOpen( UsdAbsoluteFilePath );

		if ( ReferenceLayer )
		{
			pxr::SdfPrimSpecHandle DefaultPrimSpec = ReferenceLayer->GetPrimAtPath( pxr::SdfPath( ReferenceLayer->GetDefaultPrim() ) );

			if ( DefaultPrimSpec )
			{
				if ( !SelectedItem->UsdPrim.Get().IsA( pxr::UsdSchemaRegistry::GetTypeFromName( DefaultPrimSpec->GetTypeName() ) ) )
				{
					SelectedItem->UsdPrim.Get().SetTypeName( DefaultPrimSpec->GetTypeName() ); // Set the same prim type as its reference so that they are compatible
				}
			}
		}

		FString RelativePath = AbsoluteFilePath;

		pxr::SdfLayerHandle EditLayer = UsdStageActor->GetUsdStage()->GetEditTarget().GetLayer();

		std::string RepositoryPath = EditLayer->GetRepositoryPath().empty() ? EditLayer->GetRealPath() : EditLayer->GetRepositoryPath();
		FString LayerAbsolutePath = UsdToUnreal::ConvertString( RepositoryPath );
		FPaths::MakePathRelativeTo( RelativePath, *LayerAbsolutePath );

		References.AddReference( UnrealToUsd::ConvertString( *RelativePath ).Get() );
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

bool SUsdStageTreeView::CanAddPrim() const
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

	return true;
}

bool SUsdStageTreeView::CanExecutePrimAction() const
{
	if ( !UsdStageActor.IsValid() )
	{
		return false;
	}

	TUsdStore< pxr::UsdStageRefPtr > UsdStage =  UsdStageActor->GetUsdStage();

	if ( !UsdStage.Get() || !UsdStage.Get()->GetEditTarget().IsValid() )
	{
		return false;
	}

	bool bHasPrimSpec = false;

	TArray< FUsdStageTreeItemRef > MySelectedItems = GetSelectedItems();

	for ( FUsdStageTreeItemRef SelectedItem : MySelectedItems )
	{
		pxr::SdfPrimSpecHandle PrimSpec = UsdStage.Get()->GetEditTarget().GetLayer()->GetPrimAtPath( SelectedItem->UsdPrim.Get().GetPrimPath() );
		bHasPrimSpec = bHasPrimSpec || (bool)PrimSpec;

		if ( bHasPrimSpec )
		{
			break;
		}
	}

	return bHasPrimSpec;
}

void SUsdStageTreeView::RequestListRefresh()
{
	SUsdTreeView< FUsdStageTreeItemRef >::RequestListRefresh();
	RestoreExpansionStates();
}

void SUsdStageTreeView::RestoreExpansionStates()
{
	std::function<void(const FUsdStageTreeItemRef&)> SetExpansionRecursive = [&](const FUsdStageTreeItemRef& Item)
	{
		if (const pxr::UsdPrim& Prim = Item->UsdPrim.Get())
		{
			if (bool* bFoundExpansionState = TreeItemExpansionStates.Find(UsdToUnreal::ConvertPath(Prim.GetPrimPath())))
			{
				SetItemExpansion(Item, *bFoundExpansionState);
			}
		}

		for (const FUsdStageTreeItemRef& Child : Item->Children)
		{
			SetExpansionRecursive(Child);
		}
	};

	for (const FUsdStageTreeItemRef& RootItem : RootItems)
	{
		SetExpansionRecursive(RootItem);
	}
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
		// Escaped out of initially setting a prim name
		if (!TreeItem->UsdPrim.Get())
		{
			if (FUsdStageTreeItem* Parent = TreeItem->ParentItem)
			{
				TreeItem->ParentItem->Children.Remove( TreeItem );
			}
			else
			{
				RootItems.Remove(TreeItem);
			}

			RequestTreeRefresh();
		}
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

		// Renamed a child item
		if ( TreeItem->ParentItem )
		{
			TreeItem->ParentItem->Children.Remove( TreeItem );

			RefreshPrim( UsdToUnreal::ConvertPath( TreeItem->ParentItem->UsdPrim.Get().GetPrimPath() ), true );
		}
		// Renamed a root item
		else
		{
			RefreshPrim( UsdToUnreal::ConvertPath( TreeItem->UsdPrim.Get().GetPrimPath() ), true );
		}
	}
}

void SUsdStageTreeView::OnPrimNameUpdated(const FUsdStageTreeItemRef& TreeItem, const FText& InPrimName, FText& ErrorMessage)
{
	FString NameStr = InPrimName.ToString();
	IUsdPrim::IsValidPrimName(NameStr, ErrorMessage);
	if (!ErrorMessage.IsEmpty())
	{
		return;
	}

	{
		FScopedUsdAllocs Allocs;

		const pxr::UsdStageRefPtr& Stage = TreeItem->UsdStage.Get();
		if (!Stage)
		{
			return;
		}

		pxr::SdfPath ParentPrimPath;
		if (TreeItem->ParentItem)
		{
			ParentPrimPath = TreeItem->ParentItem->UsdPrim.Get().GetPrimPath();
		}
		else
		{
			ParentPrimPath = pxr::SdfPath::AbsoluteRootPath();
		}

		pxr::SdfPath NewPrimPath = ParentPrimPath.AppendChild(pxr::TfToken(UnrealToUsd::ConvertString(*NameStr).Get()));
		const pxr::UsdPrim& Prim = Stage->GetPrimAtPath(NewPrimPath);
		if (Prim)
		{
			ErrorMessage = LOCTEXT("DuplicatePrimName", "A Prim with this name already exists!");
			return;
		}
	}
}

#endif // #if USE_USD_SDK

#undef LOCTEXT_NAMESPACE
