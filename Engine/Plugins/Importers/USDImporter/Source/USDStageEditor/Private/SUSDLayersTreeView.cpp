// Copyright Epic Games, Inc. All Rights Reserved.

#include "SUSDLayersTreeView.h"

#include "SUSDStageEditorStyle.h"
#include "USDClassesModule.h"
#include "USDLayersViewModel.h"
#include "USDLayerUtils.h"
#include "USDMemory.h"
#include "USDProjectSettings.h"
#include "USDStageActor.h"
#include "USDStageModule.h"
#include "USDTypesConversion.h"
#include "UsdWrappers/SdfLayer.h"
#include "UsdWrappers/UsdStage.h"

#include "DesktopPlatformModule.h"
#include "Engine/World.h"
#include "EngineAnalytics.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Notifications/NotificationManager.h"
#include "IDesktopPlatform.h"
#include "Modules/ModuleManager.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateTypes.h"
#include "Templates/SharedPointer.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SToolTip.h"

#if USE_USD_SDK

#define LOCTEXT_NAMESPACE "SUSDLayersTreeView"

namespace UE::USDLayersTreeViewImpl::Private
{
	void ExportLayerToPath( const UE::FSdfLayer& LayerToExport, const FString& TargetPath )
	{
		if ( !LayerToExport )
		{
			return;
		}

		// Clone the layer so that we don't modify the currently opened stage when we do the remapping below
		UE::FSdfLayer OutputLayer = UE::FSdfLayer::CreateNew( *TargetPath );
		OutputLayer.TransferContent( LayerToExport );

		// Update references to assets (e.g. textures) so that they're absolute and also work from the new file
		UsdUtils::ConvertAssetRelativePathsToAbsolute( OutputLayer, LayerToExport );

		// Convert layer references to absolute paths so that it still works at its target location
		FString LayerPath = LayerToExport.GetRealPath();
		FPaths::NormalizeFilename( LayerPath );
		const TSet<FString> AssetDependencies = OutputLayer.GetCompositionAssetDependencies();
		for ( const FString& Ref : AssetDependencies )
		{
			FString AbsRef = FPaths::ConvertRelativePathToFull( FPaths::GetPath( LayerPath ), Ref ); // Relative to the original file
			OutputLayer.UpdateCompositionAssetDependency( *Ref, *AbsRef );
		}

		bool bForce = true;
		OutputLayer.Save( bForce );
	}
}

class FUsdLayerNameColumn : public FUsdTreeViewColumn
{
public:
	virtual TSharedRef< SWidget > GenerateWidget( const TSharedPtr< IUsdTreeViewItem > InTreeItem, const TSharedPtr< ITableRow > TableRow ) override
	{
		FUsdLayerViewModelRef TreeItem = StaticCastSharedRef< FUsdLayerViewModel >( InTreeItem.ToSharedRef() );
		TWeakPtr<FUsdLayerViewModel> TreeItemWeak = TreeItem;

		return SNew( SBox )
			.VAlign( VAlign_Center )
			[
				SNew(STextBlock)
				.Text( TreeItem, &FUsdLayerViewModel::GetDisplayName )
				.ToolTipText_Lambda( [TreeItemWeak]
				{
					if ( TSharedPtr<FUsdLayerViewModel> PinnedTreeItem = TreeItemWeak.Pin() )
					{
						return FText::FromString( PinnedTreeItem->LayerIdentifier );
					}

					return FText::GetEmpty();
				})
			];
	}
};

class FUsdLayerMutedColumn : public FUsdTreeViewColumn, public TSharedFromThis< FUsdLayerMutedColumn >
{
public:
	FReply OnClicked( const FUsdLayerViewModelRef TreeItem )
	{
		ToggleMuteLayer( TreeItem );

		return FReply::Handled();
	}

	const FSlateBrush* GetBrush( const FUsdLayerViewModelRef TreeItem, const TSharedPtr< SButton > Button ) const
	{
		const bool bIsButtonHovered = Button.IsValid() && Button->IsHovered();

		if ( !CanMuteLayer( TreeItem ) )
		{
			return nullptr;
		}
		else if ( TreeItem->LayerModel->bIsMuted )
		{
			return bIsButtonHovered
				? FAppStyle::GetBrush( "Level.NotVisibleHighlightIcon16x" )
				: FAppStyle::GetBrush( "Level.NotVisibleIcon16x" );
		}
		else
		{
			return bIsButtonHovered
				? FAppStyle::GetBrush( "Level.VisibleHighlightIcon16x" )
				: FAppStyle::GetBrush( "Level.VisibleIcon16x" );
		}
	}

	FSlateColor GetForegroundColor( const FUsdLayerViewModelRef TreeItem, const TSharedPtr< ITableRow > TableRow, const TSharedPtr< SButton > Button ) const
	{
		if ( !TableRow.IsValid() || !Button.IsValid() )
		{
			return FSlateColor::UseForeground();
		}

		const bool bIsRowHovered = TableRow->AsWidget()->IsHovered();
		const bool bIsButtonHovered = Button->IsHovered();
		const bool bIsRowSelected = TableRow->IsItemSelected();
		const bool bIsLayerMuted = TreeItem->IsLayerMuted();

		if ( !bIsLayerMuted && !bIsRowHovered && !bIsRowSelected )
		{
			return FLinearColor::Transparent;
		}
		else if ( bIsButtonHovered && !bIsRowSelected )
		{
			return FAppStyle::GetSlateColor( TEXT( "Colors.ForegroundHover" ) );
		}

		return FSlateColor::UseForeground();
	}

	virtual TSharedRef< SWidget > GenerateWidget( const TSharedPtr< IUsdTreeViewItem > InTreeItem, const TSharedPtr< ITableRow > TableRow ) override
	{
		if ( !InTreeItem )
		{
			return SNullWidget::NullWidget;
		}

		FUsdLayerViewModelRef TreeItem = StaticCastSharedRef< FUsdLayerViewModel >( InTreeItem.ToSharedRef() );
		const float ItemSize = FUsdStageEditorStyle::Get()->GetFloat( "UsdStageEditor.ListItemHeight" );

		if ( !TreeItem->CanMuteLayer() )
		{
			return SNew( SBox )
				.HeightOverride( ItemSize )
				.WidthOverride( ItemSize )
				.Visibility( EVisibility::Visible )
				.ToolTip( SNew( SToolTip ).Text( LOCTEXT( "CantMuteLayerTooltip", "This layer cannot be muted!" ) ) );
		}

		TSharedPtr<SButton> Button = SNew( SButton )
			.ContentPadding( 0 )
			.ButtonStyle( FUsdStageEditorStyle::Get(), TEXT("NoBorder") )
			.OnClicked( this, &FUsdLayerMutedColumn::OnClicked, TreeItem )
			.ToolTip( SNew( SToolTip ).Text( LOCTEXT( "MuteLayerTooltip", "Mute or unmute this layer" ) ) )
			.HAlign( HAlign_Center )
			.VAlign( VAlign_Center );

		TSharedPtr<SImage> Image = SNew( SImage )
			.Image( this, &FUsdLayerMutedColumn::GetBrush, TreeItem, Button )
			.ColorAndOpacity( this, &FUsdLayerMutedColumn::GetForegroundColor, TreeItem, TableRow, Button );

		Button->SetContent( Image.ToSharedRef() );

		return SNew( SBox )
			.HeightOverride( ItemSize )
			.WidthOverride( ItemSize )
			.Visibility( EVisibility::Visible )
			[
				Button.ToSharedRef()
			];
	}

protected:
	bool CanMuteLayer( FUsdLayerViewModelRef LayerItem ) const
	{
		if ( !LayerItem->IsValid() )
		{
			return false;
		}

		return LayerItem->CanMuteLayer();
	}

	void ToggleMuteLayer( FUsdLayerViewModelRef LayerItem )
	{
		if ( !LayerItem->IsValid() || !CanMuteLayer( LayerItem ) )
		{
			return;
		}

		LayerItem->ToggleMuteLayer();
	}
};

class FUsdLayerEditColumn : public FUsdTreeViewColumn, public TSharedFromThis< FUsdLayerEditColumn >
{
public:
	const FSlateBrush* GetCheckedImage( const FUsdLayerViewModelRef InTreeItem ) const
	{
		return InTreeItem->LayerModel->bIsEditTarget
			? FUsdStageEditorStyle::Get()->GetBrush( "UsdStageEditor.CheckBoxImage" )
			: nullptr;
	}

	virtual TSharedRef< SWidget > GenerateWidget( const TSharedPtr< IUsdTreeViewItem > InTreeItem, const TSharedPtr< ITableRow > TableRow ) override
	{
		const FUsdLayerViewModelRef TreeItem = StaticCastSharedRef< FUsdLayerViewModel >( InTreeItem.ToSharedRef() );

		TSharedRef< SWidget > Item =
			SNew(SImage)
				.Image( this, &FUsdLayerEditColumn::GetCheckedImage, TreeItem );

		float ItemSize = FUsdStageEditorStyle::Get()->GetFloat( "UsdStageEditor.ListItemHeight" );

		return SNew( SBox )
			.HeightOverride( ItemSize )
			.WidthOverride( ItemSize )
			.HAlign( HAlign_Center )
			.VAlign( VAlign_Center )
			[
				Item
			];
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
		for ( FUsdLayerViewModelRef TreeItem :  RootItems )
		{
			TreeItem->RefreshData();
		}
	}

	RequestTreeRefresh();
}

TSharedRef< ITableRow > SUsdLayersTreeView::OnGenerateRow( FUsdLayerViewModelRef InDisplayNode, const TSharedRef< STableViewBase >& OwnerTable )
{
	return SNew( SUsdTreeRow< FUsdLayerViewModelRef >, InDisplayNode, OwnerTable, SharedData );
}

void SUsdLayersTreeView::OnGetChildren( FUsdLayerViewModelRef InParent, TArray< FUsdLayerViewModelRef >& OutChildren ) const
{
	for ( const FUsdLayerViewModelRef& Child : InParent->GetChildren() )
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

	// The cast here forces us to use the const version of GetUsdStage, that won't force-load the stage in case it isn't opened yet
	if ( const UE::FUsdStage& UsdStage = const_cast< const AUsdStageActor* >( UsdStageActor )->GetUsdStage() )
	{
		RootItems.Add( MakeSharedUnreal< FUsdLayerViewModel >( nullptr, UsdStage, UsdStage.GetRootLayer().GetIdentifier() ) );
		RootItems.Add( MakeSharedUnreal< FUsdLayerViewModel >( nullptr, UsdStage, UsdStage.GetSessionLayer().GetIdentifier() ) );
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

		LayerOptions.AddMenuEntry(
			LOCTEXT( "ClearLayer", "Clear" ),
			LOCTEXT( "ClearLayer_ToolTip", "Clears this layer of all data" ),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP( this, &SUsdLayersTreeView::OnClearSelectedLayers ),
				FCanExecuteAction::CreateSP( this, &SUsdLayersTreeView::CanClearSelectedLayers )
			),
			NAME_None,
			EUserInterfaceActionType::Button
		);

		LayerOptions.AddMenuEntry(
			LOCTEXT( "SaveLayer", "Save" ),
			LOCTEXT( "SaveLayer_ToolTip", "Saves the layer modifications to disk" ),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP( this, &SUsdLayersTreeView::OnSaveSelectedLayers ),
				FCanExecuteAction::CreateSP( this, &SUsdLayersTreeView::CanSaveSelectedLayers )
			),
			NAME_None,
			EUserInterfaceActionType::Button
		);

		LayerOptions.AddMenuEntry(
			LOCTEXT( "ExportLayer", "Export" ),
			LOCTEXT( "Export_ToolTip", "Export the selected layers, having the exported layers reference the original stage's layers" ),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP( this, &SUsdLayersTreeView::OnExportSelectedLayers ),
				FCanExecuteAction()
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
				FCanExecuteAction::CreateSP( this, &SUsdLayersTreeView::CanAddSubLayer )
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
				FCanExecuteAction::CreateSP( this, &SUsdLayersTreeView::CanAddSubLayer )
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

bool SUsdLayersTreeView::CanEditSelectedLayer() const
{
	bool bHasEditableLayer = false;

	TArray< FUsdLayerViewModelRef > MySelectedItems = GetSelectedItems();

	for ( FUsdLayerViewModelRef SelectedItem : MySelectedItems )
	{
		if ( SelectedItem->CanEditLayer() )
		{
			bHasEditableLayer = true;
			break;
		}
	}

	return bHasEditableLayer;
}

void SUsdLayersTreeView::OnEditSelectedLayer()
{
	TArray< FUsdLayerViewModelRef > MySelectedItems = GetSelectedItems();

	for ( FUsdLayerViewModelRef SelectedItem : MySelectedItems )
	{
		if ( SelectedItem->EditLayer() )
		{
			break;
		}
	}
}

void SUsdLayersTreeView::OnClearSelectedLayers()
{
	TArray< FUsdLayerViewModelRef > MySelectedItems = GetSelectedItems();

	// We'll show a confirmation toast, which is non-modal. So keep track of the original selected items so that
	// if anything changes by the time we accept the dialog we'll still know which layers to clear
	TArray<UE::FSdfLayerWeak> SelectedLayers;
	SelectedLayers.Reserve( MySelectedItems.Num() );

	FString LayerNames;
	const FString Separator = TEXT(", ");
	for ( FUsdLayerViewModelRef SelectedItem : MySelectedItems )
	{
		if ( UE::FSdfLayer Layer = SelectedItem->GetLayer() )
		{
			SelectedLayers.Add( Layer );
			LayerNames += Layer.GetDisplayName() + Separator;
		}
	}
	LayerNames.RemoveFromEnd( Separator );

	TFunction<void()> ClearSelectedLayersInner = [this, SelectedLayers]()
	{
		// This doesn't work very well USD-wise yet, and I don't know how we can possibly undo/redo
		// clearing a layer like this. We do need a transaction though, as this may trigger the creation
		// or deletion of UObjects
		FScopedTransaction Transaction( LOCTEXT( "ClearTransaction", "Clear selected layers" ) );

		for ( UE::FSdfLayerWeak Layer : SelectedLayers )
		{
			if ( Layer )
			{
				Layer.Clear();
			}
		}
	};

	const UUsdProjectSettings* Settings = GetDefault<UUsdProjectSettings>();
	if ( Settings && Settings->bShowConfirmationWhenClearingLayers )
	{
		static TWeakPtr<SNotificationItem> Notification;

		FNotificationInfo Toast( LOCTEXT( "ConfirmClearingLayer", "Clearing cannot be undone" ) );
		Toast.SubText = FText::Format(
			LOCTEXT( "ConfirmClearingLayer_Subtext", "Clearing USD layers cannot be undone.\nDo you wish to proceed clearing {0}|plural(one=layer,other=layers) '{1}' ?" ),
			SelectedLayers.Num(),
			FText::FromString(LayerNames)
		);
		Toast.Image = FCoreStyle::Get().GetBrush( TEXT( "MessageLog.Warning" ) );
		Toast.bUseLargeFont = false;
		Toast.bFireAndForget = false;
		Toast.FadeOutDuration = 0.0f;
		Toast.ExpireDuration = 0.0f;
		Toast.bUseThrobber = false;
		Toast.bUseSuccessFailIcons = false;

		Toast.ButtonDetails.Emplace(
			LOCTEXT( "ConfirmClearingLayerOkAll", "Always proceed" ),
			FText::GetEmpty(),
			FSimpleDelegate::CreateLambda( [ClearSelectedLayersInner]() {
			if ( TSharedPtr<SNotificationItem> PinnedNotification = Notification.Pin() )
			{
				PinnedNotification->SetCompletionState( SNotificationItem::CS_Success );
				PinnedNotification->ExpireAndFadeout();

				UUsdProjectSettings* Settings = GetMutableDefault<UUsdProjectSettings>();
				Settings->bShowConfirmationWhenClearingLayers = false;

				ClearSelectedLayersInner();
			}
		}));

		Toast.ButtonDetails.Emplace(
			LOCTEXT( "ConfirmClearingLayerOk", "Proceed" ),
			FText::GetEmpty(),
			FSimpleDelegate::CreateLambda( [ClearSelectedLayersInner]() {
			if ( TSharedPtr<SNotificationItem> PinnedNotification = Notification.Pin() )
			{
				PinnedNotification->SetCompletionState( SNotificationItem::CS_Success );
				PinnedNotification->ExpireAndFadeout();

				ClearSelectedLayersInner();
			}
		}));

		Toast.ButtonDetails.Emplace(
			LOCTEXT( "ConfirmClearingLayerCancel", "Cancel" ),
			FText::GetEmpty(),
			FSimpleDelegate::CreateLambda( []() {
			if ( TSharedPtr<SNotificationItem> PinnedNotification = Notification.Pin() )
			{
				PinnedNotification->SetCompletionState( SNotificationItem::CS_Fail );
				PinnedNotification->ExpireAndFadeout();
			}
		}));

		// Only show one at a time
		if ( !Notification.IsValid() )
		{
			Notification = FSlateNotificationManager::Get().AddNotification( Toast );
		}

		if ( TSharedPtr<SNotificationItem> PinnedNotification = Notification.Pin() )
		{
			PinnedNotification->SetCompletionState( SNotificationItem::CS_Pending );
		}
	}
	else
	{
		// Don't show prompt, just clear
		ClearSelectedLayersInner();
	}
}

bool SUsdLayersTreeView::CanClearSelectedLayers() const
{
	TArray< FUsdLayerViewModelRef > MySelectedItems = GetSelectedItems();

	for ( FUsdLayerViewModelRef SelectedItem : MySelectedItems )
	{
		if ( !SelectedItem->GetLayer().IsEmpty() )
		{
			return true;
		}
	}

	return false;
}

void SUsdLayersTreeView::OnSaveSelectedLayers()
{
	TArray< FUsdLayerViewModelRef > MySelectedItems = GetSelectedItems();

	for ( FUsdLayerViewModelRef SelectedItem : MySelectedItems )
	{
		if ( SelectedItem->IsLayerDirty() )
		{
			const bool bForce = true;
			SelectedItem->GetLayer().Save( bForce );
		}
	}
}

bool SUsdLayersTreeView::CanSaveSelectedLayers() const
{
	TArray< FUsdLayerViewModelRef > MySelectedItems = GetSelectedItems();

	for ( FUsdLayerViewModelRef SelectedItem : MySelectedItems )
	{
		if ( SelectedItem->IsLayerDirty() )
		{
			return true;
		}
	}

	return false;
}

void SUsdLayersTreeView::OnExportSelectedLayers() const
{
	TArray< FUsdLayerViewModelRef > MySelectedItems = GetSelectedItems();

	TArray<UE::FSdfLayer> LayersToExport;
	LayersToExport.Reserve( MySelectedItems.Num() );

	for ( FUsdLayerViewModelRef SelectedItem : MySelectedItems )
	{
		UE::FSdfLayer SelectedLayer = SelectedItem->GetLayer();
		if ( !SelectedLayer )
		{
			continue;
		}

		LayersToExport.Add( SelectedLayer );
	}

	double StartTime = FPlatformTime::Cycles64();
	FString Extension;

	// Single layer -> Allow picking the target layer filename
	if ( LayersToExport.Num() == 1 )
	{
		TOptional< FString > UsdFilePath = UsdUtils::BrowseUsdFile( UsdUtils::EBrowseFileMode::Save, AsShared() );
		if ( !UsdFilePath.IsSet() )
		{
			return;
		}

		StartTime = FPlatformTime::Cycles64();
		Extension = FPaths::GetExtension( UsdFilePath.GetValue() );

		UE::USDLayersTreeViewImpl::Private::ExportLayerToPath( LayersToExport[ 0 ], UsdFilePath.GetValue() );
	}
	// Multiple layers -> Pick folder and export them with the same name
	else if ( LayersToExport.Num() > 1 )
	{
		IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
		if ( !DesktopPlatform )
		{
			return;
		}

		TSharedPtr< SWindow > ParentWindow = FSlateApplication::Get().FindWidgetWindow( AsShared() );
		void* ParentWindowHandle = ( ParentWindow.IsValid() && ParentWindow->GetNativeWindow().IsValid() )
			? ParentWindow->GetNativeWindow()->GetOSWindowHandle()
			: nullptr;

		FString TargetFolderPath;
		if ( !DesktopPlatform->OpenDirectoryDialog( ParentWindowHandle, LOCTEXT( "ChooseFolder", "Choose output folder" ).ToString(), TEXT( "" ), TargetFolderPath ) )
		{
			return;
		}
		TargetFolderPath = FPaths::ConvertRelativePathToFull( TargetFolderPath );

		StartTime = FPlatformTime::Cycles64();
		Extension = FPaths::GetExtension( LayersToExport[ 0 ].GetDisplayName() );

		if ( FPaths::DirectoryExists( TargetFolderPath ) )
		{
			for ( const UE::FSdfLayer& LayerToExport : LayersToExport )
			{
				FString TargetFileName = FPaths::GetCleanFilename( LayerToExport.GetRealPath() );
				FString FullPath = FPaths::Combine( TargetFolderPath, TargetFileName );
				FString FinalFullPath = FullPath;

				uint32 Suffix = 0;
				while ( FPaths::FileExists( FinalFullPath ) )
				{
					FinalFullPath = FString::Printf( TEXT( "%s_%u" ), *FullPath, Suffix++ );
				}

				UE::USDLayersTreeViewImpl::Private::ExportLayerToPath( LayerToExport, FinalFullPath );
			}
		}
	}

	// Send analytics
	if ( FEngineAnalytics::IsAvailable() )
	{
		TArray<FAnalyticsEventAttribute> EventAttributes;

		EventAttributes.Emplace( TEXT( "NumLayersExported" ), LayersToExport.Num() );

		bool bAutomated = false;
		double ElapsedSeconds = FPlatformTime::ToSeconds64( FPlatformTime::Cycles64() - StartTime );
		IUsdClassesModule::SendAnalytics(
			MoveTemp( EventAttributes ),
			TEXT( "ExportSelectedLayers" ),
			bAutomated,
			ElapsedSeconds,
			LayersToExport.Num() > 0 ? UsdUtils::GetSdfLayerNumFrames( LayersToExport[ 0 ] ) : 0,
			Extension
		);
	}
}

bool SUsdLayersTreeView::CanAddSubLayer() const
{
	return GetSelectedItems().Num() > 0;
}

void SUsdLayersTreeView::OnAddSubLayer()
{
	TOptional< FString > SubLayerFile = UsdUtils::BrowseUsdFile( UsdUtils::EBrowseFileMode::Composition, AsShared() );

	if ( !SubLayerFile )
	{
		return;
	}

	TArray< FUsdLayerViewModelRef > MySelectedItems = GetSelectedItems();

	for ( FUsdLayerViewModelRef SelectedItem : MySelectedItems )
	{
		SelectedItem->AddSubLayer( *SubLayerFile.GetValue() );
		break;
	}

	RequestTreeRefresh();
}

void SUsdLayersTreeView::OnNewSubLayer()
{
	TOptional< FString > SubLayerFile = UsdUtils::BrowseUsdFile( UsdUtils::EBrowseFileMode::Save, AsShared() );

	if ( !SubLayerFile )
	{
		return;
	}

	TArray< FUsdLayerViewModelRef > MySelectedItems = GetSelectedItems();

	{
		FScopedUsdAllocs UsdAllocs;
		for ( FUsdLayerViewModelRef SelectedItem : MySelectedItems )
		{
			SelectedItem->NewSubLayer( *SubLayerFile.GetValue() );
			break;
		}
	}

	RequestTreeRefresh();
}

bool SUsdLayersTreeView::CanRemoveLayer( FUsdLayerViewModelRef LayerItem ) const
{
	// We can't remove root layers
	return ( LayerItem->IsValid() && LayerItem->ParentItem && LayerItem->ParentItem->IsValid() );
}

bool SUsdLayersTreeView::CanRemoveSelectedLayers() const
{
	bool bHasRemovableLayer = false;

	TArray< FUsdLayerViewModelRef > SelectedLayers = GetSelectedItems();

	for ( FUsdLayerViewModelRef SelectedLayer : SelectedLayers )
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

	TArray< FUsdLayerViewModelRef > SelectedLayers = GetSelectedItems();

	for ( FUsdLayerViewModelRef SelectedLayer : SelectedLayers )
	{
		if ( !CanRemoveLayer( SelectedLayer ) )
		{
			continue;
		}

		{
			FScopedUsdAllocs UsdAllocs;

			int32 SubLayerIndex = 0;
			for ( FUsdLayerViewModelRef Child : SelectedLayer->ParentItem->Children )
			{
				if ( Child->LayerIdentifier == SelectedLayer->LayerIdentifier )
				{
					if ( SelectedLayer->ParentItem )
					{
						bLayerRemoved = SelectedLayer->ParentItem->RemoveSubLayer( SubLayerIndex );
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
