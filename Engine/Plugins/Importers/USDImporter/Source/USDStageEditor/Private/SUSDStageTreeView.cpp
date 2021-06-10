// Copyright Epic Games, Inc. All Rights Reserved.

#include "SUSDStageTreeView.h"

#include "SUSDStageEditorStyle.h"
#include "UnrealUSDWrapper.h"
#include "USDConversionUtils.h"
#include "USDLayerUtils.h"
#include "USDStageActor.h"
#include "USDStageModule.h"
#include "USDTypesConversion.h"

#include "UsdWrappers/SdfChangeBlock.h"
#include "UsdWrappers/SdfLayer.h"
#include "UsdWrappers/UsdPrim.h"
#include "UsdWrappers/UsdStage.h"

#include "EditorStyleSet.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Modules/ModuleManager.h"
#include "ScopedTransaction.h"
#include "Templates/Function.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "UsdStageTreeView"

#if USE_USD_SDK

enum class EPayloadsTrigger
{
	Load,
	Unload,
	Toggle
};

class FUsdStageNameColumn : public FUsdTreeViewColumn, public TSharedFromThis< FUsdStageNameColumn >
{
public:
	DECLARE_DELEGATE_TwoParams( FOnPrimNameCommitted, const FUsdPrimViewModelRef&, const FText& );
	FOnPrimNameCommitted OnPrimNameCommitted;

	DECLARE_DELEGATE_ThreeParams( FOnPrimNameUpdated, const FUsdPrimViewModelRef&, const FText&, FText& );
	FOnPrimNameUpdated OnPrimNameUpdated;

	TWeakPtr< SUsdStageTreeView > OwnerTree;

	virtual TSharedRef< SWidget > GenerateWidget( const TSharedPtr< IUsdTreeViewItem > InTreeItem ) override
	{
		if ( !InTreeItem )
		{
			return SNullWidget::NullWidget;
		}

		TSharedPtr< FUsdPrimViewModel > TreeItem = StaticCastSharedPtr< FUsdPrimViewModel >( InTreeItem );

		TSharedRef< SInlineEditableTextBlock > Item =
			SNew( SInlineEditableTextBlock )
			.Text( TreeItem->RowData, &FUsdPrimModel::GetName )
			.Font( FEditorStyle::GetFontStyle( "ContentBrowser.SourceTreeItemFont" ) )
			.ColorAndOpacity( this, &FUsdStageNameColumn::GetTextColor, TreeItem )
			.OnTextCommitted( this, &FUsdStageNameColumn::OnTextCommitted, TreeItem )
			.OnVerifyTextChanged( this, &FUsdStageNameColumn::OnTextUpdated, TreeItem )
			.IsReadOnly_Lambda( [ TreeItem ]()
			{
				return !TreeItem->bIsRenamingExistingPrim && (!TreeItem || TreeItem->UsdPrim);
			} );

		TreeItem->RenameRequestEvent.BindSP( &Item.Get(), &SInlineEditableTextBlock::EnterEditingMode );

		return SNew(SBox)
			.VAlign( VAlign_Center )
			[
				Item
			];
	}

protected:
	void OnTextCommitted( const FText& InPrimName, ETextCommit::Type InCommitInfo, TSharedPtr< FUsdPrimViewModel > TreeItem )
	{
		if ( !TreeItem )
		{
			return;
		}

		OnPrimNameCommitted.ExecuteIfBound( TreeItem.ToSharedRef(), InPrimName );
	}

	bool OnTextUpdated(const FText& InPrimName, FText& ErrorMessage, TSharedPtr< FUsdPrimViewModel > TreeItem)
	{
		if (!TreeItem)
		{
			return false;
		}

		OnPrimNameUpdated.ExecuteIfBound( TreeItem.ToSharedRef(), InPrimName, ErrorMessage );

		return ErrorMessage.IsEmpty();
	}

	FSlateColor GetTextColor( TSharedPtr< FUsdPrimViewModel > TreeItem ) const
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
	ECheckBoxState IsChecked( const FUsdPrimViewModelPtr InTreeItem ) const
	{
		ECheckBoxState CheckedState = ECheckBoxState::Unchecked;

		if ( InTreeItem && InTreeItem->RowData->HasPayload() )
		{
			CheckedState = InTreeItem->RowData->IsLoaded() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		}

		return CheckedState;
	}

	void OnCheckedPayload( ECheckBoxState NewCheckedState, const FUsdPrimViewModelPtr TreeItem )
	{
		if ( !TreeItem )
		{
			return;
		}

		switch ( NewCheckedState )
		{
		case ECheckBoxState::Checked :
			TreeItem->UsdPrim.Load();
			break;
		case ECheckBoxState::Unchecked:
			TreeItem->UsdPrim.Unload();
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
			const TSharedPtr< FUsdPrimViewModel > TreeItem = StaticCastSharedPtr< FUsdPrimViewModel >( InTreeItem );
			bHasPayload = TreeItem->RowData->HasPayload();
		}

		TSharedRef< SWidget > Item = SNullWidget::NullWidget;

		if ( bHasPayload )
		{
			Item = SNew( SCheckBox )
					.IsChecked( this, &FUsdStagePayloadColumn::IsChecked, StaticCastSharedPtr< FUsdPrimViewModel >( InTreeItem ) )
					.OnCheckStateChanged( this, &FUsdStagePayloadColumn::OnCheckedPayload, StaticCastSharedPtr< FUsdPrimViewModel >( InTreeItem ) );
		}

		return Item;
	}
};

class FUsdStageVisibilityColumn : public FUsdTreeViewColumn, public TSharedFromThis< FUsdStageVisibilityColumn >
{
public:
	FReply OnToggleVisibility( const FUsdPrimViewModelPtr TreeItem )
	{
		FScopedTransaction Transaction( FText::Format(
			LOCTEXT( "VisibilityTransaction", "Toggle visibility of prim '{0}'" ),
			FText::FromName( TreeItem->UsdPrim.GetName() )
		) );

		TreeItem->ToggleVisibility();

		return FReply::Handled();
	}

	const FSlateBrush* GetBrush( const FUsdPrimViewModelPtr TreeItem ) const
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
		const TSharedPtr< FUsdPrimViewModel > TreeItem = StaticCastSharedPtr< FUsdPrimViewModel >( InTreeItem );

		if ( !TreeItem->HasVisibilityAttribute() )
		{
			float ItemSize = FUsdStageEditorStyle::Get()->GetFloat( "UsdStageEditor.ListItemHeight" );

			return SNew(SBox)
				.HeightOverride( ItemSize )
				.WidthOverride( ItemSize )
				.Visibility( EVisibility::Visible )
				.ToolTip( SNew( SToolTip ).Text( LOCTEXT( "NoGeomImageable", "Only prims with the GeomImageable schema (or derived) have the visibility attribute!" ) ) );
		}

		TSharedRef< SWidget > Item = SNew( SButton )
			.ContentPadding( 0 )
			.ButtonStyle( FEditorStyle::Get(), "ToggleButton" )
			.OnClicked( this, &FUsdStageVisibilityColumn::OnToggleVisibility, TreeItem )
			.ToolTip( SNew( SToolTip ).Text( LOCTEXT( "GeomImageable", "Toggle the visibility of this prim" ) ) )
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
		TSharedPtr< FUsdPrimViewModel > TreeItem = StaticCastSharedPtr< FUsdPrimViewModel >( InTreeItem );

		return SNew(SBox)
			.VAlign( VAlign_Center )
			[
				SNew(STextBlock)
				.TextStyle( FEditorStyle::Get(), "LargeText" )
				.Text( TreeItem->RowData, &FUsdPrimModel::GetType )
				.Font( FEditorStyle::GetFontStyle( "ContentBrowser.SourceTreeItemFont" ) )
			];
	}
};

void SUsdStageTreeView::Construct( const FArguments& InArgs, AUsdStageActor* InUsdStageActor )
{
	SUsdTreeView::Construct( SUsdTreeView::FArguments() );

	OnContextMenuOpening = FOnContextMenuOpening::CreateSP( this, &SUsdStageTreeView::ConstructPrimContextMenu );

	OnSelectionChanged = FOnSelectionChanged::CreateLambda( [ this ]( FUsdPrimViewModelPtr UsdStageTreeItem, ESelectInfo::Type SelectionType )
	{
		FString SelectedPrimPath;

		if ( UsdStageTreeItem )
		{
			SelectedPrimPath = UsdToUnreal::ConvertPath( UsdStageTreeItem->UsdPrim.GetPrimPath() );
		}

		TArray<FString> SelectedPrimPaths = GetSelectedPrims();
		this->OnPrimSelectionChanged.ExecuteIfBound( SelectedPrimPaths );
	} );

	OnExpansionChanged = FOnExpansionChanged::CreateLambda([this]( const FUsdPrimViewModelPtr& UsdPrimViewModel, bool bIsExpanded)
	{
		if ( !UsdPrimViewModel )
		{
			return;
		}

		const UE::FUsdPrim& Prim = UsdPrimViewModel->UsdPrim;
		if ( !Prim )
		{
			return;
		}

		TreeItemExpansionStates.Add( Prim.GetPrimPath().GetString(), bIsExpanded );
	});

	OnPrimSelectionChanged = InArgs._OnPrimSelectionChanged;

	Refresh( InUsdStageActor );
}

TSharedRef< ITableRow > SUsdStageTreeView::OnGenerateRow( FUsdPrimViewModelRef InDisplayNode, const TSharedRef<STableViewBase>& OwnerTable )
{
	return SNew( SUsdTreeRow< FUsdPrimViewModelRef >, InDisplayNode, OwnerTable, SharedData );
}

void SUsdStageTreeView::OnGetChildren( FUsdPrimViewModelRef InParent, TArray< FUsdPrimViewModelRef >& OutChildren ) const
{
	for ( const FUsdPrimViewModelRef& Child : InParent->UpdateChildren() )
	{
		OutChildren.Add( Child );
	}
}

void SUsdStageTreeView::Refresh( AUsdStageActor* InUsdStageActor )
{
	UE::FUsdStage OldStage = RootItems.Num() > 0 ? RootItems[0]->UsdStage : UE::FUsdStage();
	UE::FUsdStage NewStage = InUsdStageActor ? static_cast< const AUsdStageActor* >( InUsdStageActor )->GetUsdStage() : UE::FUsdStage();

	RootItems.Empty();
	if ( UsdStageActor.Get() != InUsdStageActor || NewStage != OldStage )
	{
		// This is very important: Internally the tree will store FUsdPrimViewModelRef in its SparseItemInfos member if we have
		// any member manually expanded/collapsed. These can prevent the FUsdPrimViewModels from being collected, and prevent the
		// stage from being fully closed, so we must do this whenever the stage changes
		ClearExpandedItems();
		TreeItemExpansionStates.Reset();

		// Clear other things that may hold FUsdPrimViewModelRefs
		LinearizedItems.Empty();
		SelectorItem = SUsdStageTreeView::NullableItemType(nullptr);
		RangeSelectionStart = SUsdStageTreeView::NullableItemType(nullptr);
		ItemToScrollIntoView = SUsdStageTreeView::NullableItemType(nullptr);
		ItemToNotifyWhenInView = SUsdStageTreeView::NullableItemType(nullptr);
	}

	UsdStageActor = InUsdStageActor;
	if ( !UsdStageActor.IsValid() )
	{
		return;
	}

	if ( NewStage )
	{
		if ( UE::FUsdPrim RootPrim = NewStage.GetPseudoRoot() )
		{
			RootItems.Add( MakeShared< FUsdPrimViewModel >( nullptr, NewStage, RootPrim ) );
		}

		RestoreExpansionStates();
	}
}

void SUsdStageTreeView::RefreshPrim( const FString& PrimPath, bool bResync )
{
	FScopedUnrealAllocs UnrealAllocs; // RefreshPrim can be called by a delegate for which we don't know the active allocator

	FUsdPrimViewModelPtr FoundItem = GetItemFromPrimPath(PrimPath);

	if ( FoundItem.IsValid() )
	{
		FoundItem->RefreshData( true );

		// Item doesn't match any prim, needs to be removed
		if ( !FoundItem->UsdPrim )
		{
			if ( FoundItem->ParentItem )
			{
				FoundItem->ParentItem->RefreshData( true );
			}
			else
			{
				RootItems.Remove( FoundItem.ToSharedRef() );
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

FUsdPrimViewModelPtr SUsdStageTreeView::GetItemFromPrimPath( const FString& PrimPath )
{
	FScopedUnrealAllocs UnrealAllocs; // RefreshPrim can be called by a delegate for which we don't know the active allocator

	UE::FSdfPath UsdPrimPath( *PrimPath );

	TFunction< FUsdPrimViewModelPtr( const UE::FSdfPath&, const FUsdPrimViewModelRef& ) > FindTreeItemFromPrimPath;
	FindTreeItemFromPrimPath = [ &FindTreeItemFromPrimPath ]( const UE::FSdfPath& UsdPrimPath, const FUsdPrimViewModelRef& ItemRef ) -> FUsdPrimViewModelPtr
	{
		if ( ItemRef->UsdPrim.GetPrimPath() == UsdPrimPath )
		{
			return ItemRef;
		}
		else
		{
			for ( FUsdPrimViewModelRef ChildItem : ItemRef->Children )
			{
				if ( FUsdPrimViewModelPtr ChildValue = FindTreeItemFromPrimPath( UsdPrimPath, ChildItem ) )
				{
					return ChildValue;
				}
			}
		}

		return {};
	};

	// Search for the corresponding tree item to update
	FUsdPrimViewModelPtr FoundItem = nullptr;
	for ( FUsdPrimViewModelRef RootItem : this->RootItems )
	{
		UE::FSdfPath PrimPathToSearch = UsdPrimPath;

		FoundItem = FindTreeItemFromPrimPath( PrimPathToSearch, RootItem );

		while ( !FoundItem.IsValid() )
		{
			UE::FSdfPath ParentPrimPath = PrimPathToSearch.GetParentPath();
			if ( ParentPrimPath == PrimPathToSearch )
			{
				break;
			}
			PrimPathToSearch = MoveTemp( ParentPrimPath );

			FoundItem = FindTreeItemFromPrimPath( PrimPathToSearch, RootItem );
		}

		if ( FoundItem.IsValid() )
		{
			break;
		}
	}

	return FoundItem;
}

void SUsdStageTreeView::SelectPrims( const TArray<FString>& PrimPaths )
{
	ClearSelection();

	TArray< FUsdPrimViewModelRef > ItemsToSelect;
	ItemsToSelect.Reserve( PrimPaths.Num() );

	for ( const FString& PrimPath : PrimPaths )
	{
		if ( FUsdPrimViewModelPtr FoundItem = GetItemFromPrimPath( PrimPath ) )
		{
			ItemsToSelect.Add( FoundItem.ToSharedRef() );
		}
	}

	if ( ItemsToSelect.Num() > 0 )
	{
		const bool bSelected = true;
		SetItemSelection( ItemsToSelect, bSelected );
		ScrollItemIntoView( ItemsToSelect.Last() );
	}
}

TArray<FString> SUsdStageTreeView::GetSelectedPrims()
{
	TArray<FString> SelectedPrimPaths;
	SelectedPrimPaths.Reserve( GetNumItemsSelected() );

	for ( FUsdPrimViewModelRef SelectedItem : GetSelectedItems() )
	{
		SelectedPrimPaths.Add( SelectedItem->UsdPrim.GetPrimPath().GetString() );
	}

	return SelectedPrimPaths;
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
			LOCTEXT( "RenamePrim", "Rename Prim" ),
			LOCTEXT( "RenamePrim_ToolTip", "Renames the prim on all layers" ),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP( this, &SUsdStageTreeView::OnRenamePrim ),
				FCanExecuteAction::CreateSP( this, &SUsdStageTreeView::CanExecutePrimAction )
			),
			NAME_None,
			EUserInterfaceActionType::Button
		);

		PrimOptions.AddMenuEntry(
			LOCTEXT("RemovePrim", "Remove Prim"),
			LOCTEXT("RemovePrim_ToolTip", "Removes the prim and its children from the current edit target"),
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

	TArray< FUsdPrimViewModelRef > MySelectedItems = GetSelectedItems();

	// Add a new child prim
	if (MySelectedItems.Num() > 0)
	{
		for ( FUsdPrimViewModelRef SelectedItem : MySelectedItems )
		{
			FUsdPrimViewModelRef TreeItem = MakeShared< FUsdPrimViewModel >( &SelectedItem.Get(), SelectedItem->UsdStage );
			SelectedItem->Children.Add( TreeItem );

			PendingRenameItem = TreeItem;
			ScrollItemIntoView( TreeItem );
		}
	}
	// Add a new top-level prim (direct child of the pseudo-root prim)
	else
	{
		FUsdPrimViewModelRef TreeItem = MakeShared< FUsdPrimViewModel >( nullptr, UsdStageActor->GetOrLoadUsdStage() );
		RootItems.Add( TreeItem );

		PendingRenameItem = TreeItem;
		ScrollItemIntoView( TreeItem );
	}

	RequestTreeRefresh();
}

void SUsdStageTreeView::OnRenamePrim()
{
	if ( !UsdStageActor.IsValid() )
	{
		return;
	}

	TArray< FUsdPrimViewModelRef > MySelectedItems = GetSelectedItems();

	if ( MySelectedItems.Num() > 0 )
	{
		FUsdPrimViewModelRef TreeItem = MySelectedItems[ 0 ];

		TreeItem->bIsRenamingExistingPrim = true;
		PendingRenameItem = TreeItem;
		RequestScrollIntoView( TreeItem );
	}
}

void SUsdStageTreeView::OnRemovePrim()
{
	if ( !UsdStageActor.IsValid() )
	{
		return;
	}

	FScopedTransaction Transaction( LOCTEXT( "RemovePrimTransaction", "Remove prims'" ) );

	TArray< FUsdPrimViewModelRef > MySelectedItems = GetSelectedItems();

	for ( FUsdPrimViewModelRef SelectedItem : MySelectedItems )
	{
		UE::FUsdStage Stage = UsdStageActor->GetOrLoadUsdStage();

		UsdUtils::RemoveAllPrimSpecs( SelectedItem->UsdPrim, Stage.GetEditTarget() );
	}
}

void SUsdStageTreeView::OnAddReference()
{
	UE::FUsdStage& Stage = UsdStageActor->GetOrLoadUsdStage();
	if ( !UsdStageActor.IsValid() || !Stage || !Stage.IsEditTargetValid() )
	{
		return;
	}

	TOptional< FString > PickedFile = UsdUtils::BrowseUsdFile( UsdUtils::EBrowseFileMode::Open, AsShared() );

	if ( !PickedFile )
	{
		return;
	}

	// This transaction is important as adding a reference may trigger the creation of new unreal assets, which need to be
	// destroyed if we spam undo afterwards. Undoing won't remove the actual reference from the stage yet though, sadly...
	FScopedTransaction Transaction( FText::Format(
		LOCTEXT( "AddReferenceTransaction", "Add reference to file '{0}'" ),
		FText::FromString( PickedFile.GetValue() )
	) );

	const FString AbsoluteFilePath = FPaths::ConvertRelativePathToFull( PickedFile.GetValue() );

	TArray< FUsdPrimViewModelRef > MySelectedItems = GetSelectedItems();

	for ( FUsdPrimViewModelRef SelectedItem : MySelectedItems )
	{
		SelectedItem->AddReference( *AbsoluteFilePath );
	}
}

void SUsdStageTreeView::OnClearReferences()
{
	if ( !UsdStageActor.IsValid() )
	{
		return;
	}

	FScopedTransaction Transaction( LOCTEXT( "ClearReferenceTransaction", "Clear references to USD layers" ) );

	TArray< FUsdPrimViewModelRef > MySelectedItems = GetSelectedItems();

	for ( FUsdPrimViewModelRef SelectedItem : MySelectedItems )
	{
		SelectedItem->ClearReferences();
	}
}

bool SUsdStageTreeView::CanAddPrim() const
{
	if ( !UsdStageActor.IsValid() )
	{
		return false;
	}

	UE::FUsdStage UsdStage =  UsdStageActor->GetOrLoadUsdStage();
	if ( !UsdStage )
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

	UE::FUsdStage UsdStage =  UsdStageActor->GetOrLoadUsdStage();
	if ( !UsdStage || !UsdStage.IsEditTargetValid() )
	{
		return false;
	}

	bool bHasPrimSpec = false;

	TArray< FUsdPrimViewModelRef > MySelectedItems = GetSelectedItems();

	for ( FUsdPrimViewModelRef SelectedItem : MySelectedItems )
	{
		bHasPrimSpec = bHasPrimSpec || (bool)SelectedItem->CanExecutePrimAction();

		if ( bHasPrimSpec )
		{
			break;
		}
	}

	return bHasPrimSpec;
}

void SUsdStageTreeView::RequestListRefresh()
{
	SUsdTreeView< FUsdPrimViewModelRef >::RequestListRefresh();
	RestoreExpansionStates();
}

void SUsdStageTreeView::RestoreExpansionStates()
{
	TFunction< void( const FUsdPrimViewModelRef& ) > SetExpansionRecursive = [&]( const FUsdPrimViewModelRef& Item )
	{
		if ( const UE::FUsdPrim& Prim = Item->UsdPrim )
		{
			if (bool* bFoundExpansionState = TreeItemExpansionStates.Find( Prim.GetPrimPath().GetString() ) )
			{
				SetItemExpansion( Item, *bFoundExpansionState );
			}
			// Default to showing the root level expanded
			else if ( Prim.GetStage().GetPseudoRoot() == Prim )
			{
				const bool bShouldExpand = true;
				SetItemExpansion( Item, bShouldExpand );
			}
		}

		for ( const FUsdPrimViewModelRef& Child : Item->Children )
		{
			SetExpansionRecursive( Child );
		}
	};

	for ( const FUsdPrimViewModelRef& RootItem : RootItems )
	{
		SetExpansionRecursive( RootItem );
	}
}

void SUsdStageTreeView::OnToggleAllPayloads( EPayloadsTrigger PayloadsTrigger )
{
	TArray< FUsdPrimViewModelRef > MySelectedItems = GetSelectedItems();

	for ( FUsdPrimViewModelRef SelectedItem : MySelectedItems )
	{
		if ( SelectedItem->UsdPrim )
		{
			TFunction< void( FUsdPrimViewModelRef ) > RecursiveTogglePayloads;
			RecursiveTogglePayloads = [ &RecursiveTogglePayloads, PayloadsTrigger ]( FUsdPrimViewModelRef InSelectedItem ) -> void
			{
				UE::FUsdPrim& UsdPrim = InSelectedItem->UsdPrim;

				if ( UsdPrim.HasPayload() )
				{
					if ( PayloadsTrigger == EPayloadsTrigger::Toggle )
					{
						InSelectedItem->TogglePayload();
					}
					else if ( PayloadsTrigger == EPayloadsTrigger::Load && !UsdPrim.IsLoaded() )
					{
						InSelectedItem->TogglePayload();
					}
					else if ( PayloadsTrigger == EPayloadsTrigger::Unload && UsdPrim.IsLoaded() )
					{
						InSelectedItem->TogglePayload();
					}
				}
				else
				{
					for ( FUsdPrimViewModelRef Child : InSelectedItem->UpdateChildren() )
					{
						RecursiveTogglePayloads( Child );
					}
				}
			};

			RecursiveTogglePayloads( SelectedItem );
		}
	}
}

void SUsdStageTreeView::ScrollItemIntoView( FUsdPrimViewModelRef TreeItem )
{
	FUsdPrimViewModel* Parent = TreeItem->ParentItem;
	while( Parent )
	{
		SetItemExpansion( Parent->AsShared(), true );
		Parent = Parent->ParentItem;
	}

	RequestScrollIntoView( TreeItem );
}

void SUsdStageTreeView::OnTreeItemScrolledIntoView( FUsdPrimViewModelRef TreeItem, const TSharedPtr<ITableRow>& Widget )
{
	if ( TreeItem == PendingRenameItem.Pin() )
	{
		PendingRenameItem = nullptr;
		TreeItem->RenameRequestEvent.ExecuteIfBound();
	}
}

void SUsdStageTreeView::OnPrimNameCommitted( const FUsdPrimViewModelRef& ViewModel, const FText& InPrimName )
{
	// Reset this regardless of how we exit this function
	const bool bRenamingExistingPrim = ViewModel->bIsRenamingExistingPrim;
	ViewModel->bIsRenamingExistingPrim = false;

	if ( InPrimName.IsEmptyOrWhitespace() )
	{
		// Escaped out of initially setting a prim name
		if ( !ViewModel->UsdPrim )
		{
			if (FUsdPrimViewModel* Parent = ViewModel->ParentItem)
			{
				ViewModel->ParentItem->Children.Remove( ViewModel );
			}
			else
			{
				RootItems.Remove( ViewModel );
			}

			RequestTreeRefresh();
		}
		return;
	}

	if ( bRenamingExistingPrim )
	{
		FScopedTransaction Transaction( LOCTEXT( "RenamePrimTransaction", "Rename a prim" ) );

		// e.g. "/Root/OldPrim/"
		FString OldPath = ViewModel->UsdPrim.GetPrimPath().GetString();

		// e.g. "NewPrim"
		FString NewNameStr = InPrimName.ToString();

		// Preemptively preserve the prim's expansion state because RenamePrim will trigger notices from within itself
		// that will trigger refreshes of the tree view
		{
			// e.g. "/Root/NewPrim"
			FString NewPath = FString::Printf( TEXT( "%s/%s" ), *FPaths::GetPath( OldPath ), *NewNameStr );
			TMap<FString, bool> PairsToAdd;
			for ( TMap<FString, bool>::TIterator It( TreeItemExpansionStates ); It; ++It )
			{
				// e.g. "/Root/OldPrim/SomeChild"
				FString SomePrimPath = It->Key;
				if ( SomePrimPath.RemoveFromStart( OldPath ) )  // e.g. "/SomeChild"
				{
					// e.g. "/Root/NewPrim/SomeChild"
					SomePrimPath = NewPath + SomePrimPath;
					PairsToAdd.Add( SomePrimPath, It->Value );
				}
			}
			TreeItemExpansionStates.Append( PairsToAdd );
		}

		UsdUtils::RenamePrim( ViewModel->UsdPrim, *NewNameStr );
	}
	else
	{
		FScopedTransaction Transaction( LOCTEXT( "AddPrimTransaction", "Add a new prim" ) );

		ViewModel->DefinePrim( *InPrimName.ToString() );

		const bool bResync = true;

		// Renamed a child item
		if ( ViewModel->ParentItem )
		{
			ViewModel->ParentItem->Children.Remove( ViewModel );

			RefreshPrim( ViewModel->ParentItem->UsdPrim.GetPrimPath().GetString(), bResync );
		}
		// Renamed a root item
		else
		{
			RefreshPrim( ViewModel->UsdPrim.GetPrimPath().GetString(), bResync );
		}
	}
}

void SUsdStageTreeView::OnPrimNameUpdated(const FUsdPrimViewModelRef& TreeItem, const FText& InPrimName, FText& ErrorMessage)
{
	FString NameStr = InPrimName.ToString();
	IUsdPrim::IsValidPrimName(NameStr, ErrorMessage);
	if (!ErrorMessage.IsEmpty())
	{
		return;
	}

	{
		const UE::FUsdStage& Stage = TreeItem->UsdStage;
		if ( !Stage )
		{
			return;
		}

		UE::FSdfPath ParentPrimPath;
		if ( TreeItem->ParentItem )
		{
			ParentPrimPath = TreeItem->ParentItem->UsdPrim.GetPrimPath();
		}
		else
		{
			ParentPrimPath = UE::FSdfPath::AbsoluteRootPath();
		}

		UE::FSdfPath NewPrimPath = ParentPrimPath.AppendChild( *NameStr );
		const UE::FUsdPrim& Prim = Stage.GetPrimAtPath( NewPrimPath );
		if ( Prim && Prim != TreeItem->UsdPrim )
		{
			ErrorMessage = LOCTEXT("DuplicatePrimName", "A Prim with this name already exists!");
			return;
		}
	}
}

#endif // #if USE_USD_SDK

#undef LOCTEXT_NAMESPACE
