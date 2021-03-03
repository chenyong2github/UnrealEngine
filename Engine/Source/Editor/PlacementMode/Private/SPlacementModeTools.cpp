// Copyright Epic Games, Inc. All Rights Reserved.

#include "SPlacementModeTools.h"
#include "Application/SlateApplicationBase.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SScrollBar.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Input/SCheckBox.h"
#include "EditorStyleSet.h"
#include "EditorModeManager.h"
#include "EditorModes.h"
#include "AssetThumbnail.h"
#include "LevelEditor.h"
#include "PlacementMode.h"
#include "ContentBrowserDataDragDropOp.h"
#include "EditorClassUtils.h"
#include "Widgets/Input/SSearchBox.h"

#define LOCTEXT_NAMESPACE "PlacementMode"

namespace PlacementModeTools
{
	bool bItemInternalsInTooltip = false;
	FAutoConsoleVariableRef CVarItemInternalsInTooltip(TEXT("PlacementMode.ItemInternalsInTooltip"), bItemInternalsInTooltip, TEXT("Shows placeable item internal information in its tooltip"));
}

struct FSortPlaceableItems
{
	static bool SortItemsByOrderThenName(const TSharedPtr<FPlaceableItem>& A, const TSharedPtr<FPlaceableItem>& B)
	{
		if (A->SortOrder.IsSet())
		{
			if (B->SortOrder.IsSet())
			{
				return A->SortOrder.GetValue() < B->SortOrder.GetValue();
			}
			else
			{
				return true;
			}
		}
		else if (B->SortOrder.IsSet())
		{
			return false;
		}
		else
		{
			return SortItemsByName(A, B);
		}
	}

	static bool SortItemsByName(const TSharedPtr<FPlaceableItem>& A, const TSharedPtr<FPlaceableItem>& B)
	{
		return A->DisplayName.CompareTo(B->DisplayName) < 0;
	}
};

namespace PlacementViewFilter
{
	void GetBasicStrings(const FPlaceableItem& InPlaceableItem, TArray<FString>& OutBasicStrings)
	{
		OutBasicStrings.Add(InPlaceableItem.DisplayName.ToString());

		if (!InPlaceableItem.NativeName.IsEmpty())
		{
			OutBasicStrings.Add(InPlaceableItem.NativeName);
		}

		const FString* SourceString = FTextInspector::GetSourceString(InPlaceableItem.DisplayName);
		if (SourceString)
		{
			OutBasicStrings.Add(*SourceString);
		}
	}
} // namespace PlacementViewFilter

/**
 * These are the asset thumbnails.
 */
class SPlacementAssetThumbnail : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS( SPlacementAssetThumbnail )
		: _Width( 32 )
		, _Height( 32 )
		, _AlwaysUseGenericThumbnail( false )
		, _AssetTypeColorOverride()
	{}

	SLATE_ARGUMENT( uint32, Width )

	SLATE_ARGUMENT( uint32, Height )

	SLATE_ARGUMENT( FName, ClassThumbnailBrushOverride )

	SLATE_ARGUMENT( bool, AlwaysUseGenericThumbnail )

	SLATE_ARGUMENT( TOptional<FLinearColor>, AssetTypeColorOverride )
	SLATE_END_ARGS()

	void Construct( const FArguments& InArgs, const FAssetData& InAsset)
	{
		Asset = InAsset;

		FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>( "LevelEditor" );
		TSharedPtr<FAssetThumbnailPool> ThumbnailPool = LevelEditorModule.GetFirstLevelEditor()->GetThumbnailPool();

		Thumbnail = MakeShareable(new FAssetThumbnail(Asset, InArgs._Width, InArgs._Height, ThumbnailPool));

		FAssetThumbnailConfig Config;
		Config.bForceGenericThumbnail = InArgs._AlwaysUseGenericThumbnail;
		Config.ClassThumbnailBrushOverride = InArgs._ClassThumbnailBrushOverride;
		Config.AssetTypeColorOverride = InArgs._AssetTypeColorOverride;
		ChildSlot
		[
			Thumbnail->MakeThumbnailWidget( Config )
		];
	}

private:

	FAssetData Asset;
	TSharedPtr< FAssetThumbnail > Thumbnail;
};

void SPlacementAssetEntry::Construct(const FArguments& InArgs, const TSharedPtr<const FPlaceableItem>& InItem)
{	
	bIsPressed = false;

	Item = InItem;

	TSharedPtr< SHorizontalBox > ActorType = SNew( SHorizontalBox );

	const bool bIsClass = Item->AssetData.GetClass() == UClass::StaticClass();
	const bool bIsActor = bIsClass ? CastChecked<UClass>(Item->AssetData.GetAsset())->IsChildOf(AActor::StaticClass()) : false;

	AActor* DefaultActor = nullptr;
	if (Item->Factory != nullptr)
	{
		DefaultActor = Item->Factory->GetDefaultActor(Item->AssetData);
	}
	else if (bIsActor)
	{
		DefaultActor = CastChecked<AActor>(CastChecked<UClass>(Item->AssetData.GetAsset())->ClassDefaultObject);
	}

	TSharedPtr<IToolTip> AssetEntryToolTip;
	if (PlacementModeTools::bItemInternalsInTooltip)
	{
		AssetEntryToolTip = FSlateApplicationBase::Get().MakeToolTip(
			FText::Format(LOCTEXT("ItemInternalsTooltip", "Native Name: {0}\nAsset Path: {1}\nFactory Class: {2}"), 
			FText::FromString(Item->NativeName), 
			FText::FromName(Item->AssetData.ObjectPath),
			FText::FromString(Item->Factory ? Item->Factory->GetClass()->GetName() : TEXT("None"))));
	}

	UClass* DocClass = nullptr;
	if(DefaultActor != nullptr)
	{
		DocClass = DefaultActor->GetClass();
		if (!AssetEntryToolTip)
		{
			AssetEntryToolTip = FEditorClassUtils::GetTooltip(DefaultActor->GetClass());
		}
	}

	if (!AssetEntryToolTip)
	{
		AssetEntryToolTip = FSlateApplicationBase::Get().MakeToolTip(Item->DisplayName);
	}

	const FButtonStyle& ButtonStyle = FEditorStyle::GetWidgetStyle<FButtonStyle>( "PlacementBrowser.Asset" );

	NormalImage = &ButtonStyle.Normal;
	HoverImage = &ButtonStyle.Hovered;
	PressedImage = &ButtonStyle.Pressed; 

	// Create doc link widget if there is a class to link to
	TSharedRef<SWidget> DocWidget = SNew(SSpacer);
	if(DocClass != NULL)
	{
		DocWidget = FEditorClassUtils::GetDocumentationLinkWidget(DocClass);
		DocWidget->SetCursor( EMouseCursor::Default );
	}

	ChildSlot
	[
		SNew( SBorder )
		.BorderImage( this, &SPlacementAssetEntry::GetBorder )
		.Cursor( EMouseCursor::GrabHand )
		.ToolTip( AssetEntryToolTip )
		[
			SNew( SHorizontalBox )

			+ SHorizontalBox::Slot()
			.Padding( 0 )
			.AutoWidth()
			[
				// Drop shadow border
				SNew( SBorder )
				.Padding( 4 )
				.BorderImage( FEditorStyle::GetBrush( "ContentBrowser.ThumbnailShadow" ) )
				[
					SNew( SBox )
					.WidthOverride( 35 )
					.HeightOverride( 35 )
					[
						SNew( SPlacementAssetThumbnail, Item->AssetData )
						.ClassThumbnailBrushOverride( Item->ClassThumbnailBrushOverride )
						.AlwaysUseGenericThumbnail( Item->bAlwaysUseGenericThumbnail )
						.AssetTypeColorOverride( Item->AssetTypeColorOverride )
					]
				]
			]

			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.Padding(2, 0, 4, 0)
			[
				SNew( SVerticalBox )
				+SVerticalBox::Slot()
				.Padding(0, 0, 0, 1)
				.AutoHeight()
				[
					SNew( STextBlock )
					.TextStyle( FEditorStyle::Get(), "PlacementBrowser.Asset.Name" )
					.Text( Item->DisplayName )
					.HighlightText(InArgs._HighlightText)
				]
			]

			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				DocWidget
			]
		]
	];
}

FReply SPlacementAssetEntry::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if ( MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton )
	{
		bIsPressed = true;

		return FReply::Handled().DetectDrag( SharedThis( this ), MouseEvent.GetEffectingButton() );
	}

	return FReply::Unhandled();
}

FReply SPlacementAssetEntry::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if ( MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton )
	{
		bIsPressed = false;
	}

	return FReply::Unhandled();
}

FReply SPlacementAssetEntry::OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	bIsPressed = false;

	if (FEditorDelegates::OnAssetDragStarted.IsBound())
	{
		TArray<FAssetData> DraggedAssetDatas;
		DraggedAssetDatas.Add( Item->AssetData );
		FEditorDelegates::OnAssetDragStarted.Broadcast( DraggedAssetDatas, Item->Factory );
		return FReply::Handled();
	}

	if( MouseEvent.IsMouseButtonDown( EKeys::LeftMouseButton ) )
	{
		return FReply::Handled().BeginDragDrop( FContentBrowserDataDragDropOp::Legacy_New( MakeArrayView(&Item->AssetData, 1), TArrayView<const FString>(), Item->Factory ) );
	}
	else
	{
		return FReply::Handled();
	}
}

bool SPlacementAssetEntry::IsPressed() const
{
	return bIsPressed;
}

const FSlateBrush* SPlacementAssetEntry::GetBorder() const
{
	if ( IsPressed() )
	{
		return PressedImage;
	}
	else if ( IsHovered() )
	{
		return HoverImage;
	}
	else
	{
		return NormalImage;
	}
}

SPlacementModeTools::~SPlacementModeTools()
{
	if ( IPlacementModeModule::IsAvailable() )
	{
		IPlacementModeModule& PlacementModeModule = IPlacementModeModule::Get();
		PlacementModeModule.OnRecentlyPlacedChanged().RemoveAll(this);
		PlacementModeModule.OnAllPlaceableAssetsChanged().RemoveAll(this);
		PlacementModeModule.OnPlacementModeCategoryListChanged().RemoveAll(this);
		PlacementModeModule.OnPlaceableItemFilteringChanged().RemoveAll(this);
	}
}

void SPlacementModeTools::Construct( const FArguments& InArgs )
{
	bRefreshAllClasses = false;
	bRefreshRecentlyPlaced = false;
	bUpdateShownItems = true;

	ActiveTabName = FBuiltInPlacementCategories::Basic();

	FPlacementMode* PlacementEditMode = (FPlacementMode*)GLevelEditorModeTools().GetActiveMode( FBuiltinEditorModes::EM_Placement );
	if (PlacementEditMode)
	{
		PlacementEditMode->AddValidFocusTargetForPlacement(SharedThis(this));
	}

	SearchTextFilter = MakeShareable(new FPlacementAssetEntryTextFilter(
		FPlacementAssetEntryTextFilter::FItemToStringArray::CreateStatic(&PlacementViewFilter::GetBasicStrings)
		));

	Tabs = SNew(SVerticalBox).Visibility(this, &SPlacementModeTools::GetTabsVisibility);

	UpdatePlacementCategories();

	TSharedRef<SScrollBar> ScrollBar = SNew(SScrollBar)
		.Thickness(FVector2D(9.0f, 9.0f));

	ChildSlot
	[
		SNew( SVerticalBox )

		+ SVerticalBox::Slot()
		.Padding(4)
		.AutoHeight()
		[
			SAssignNew( SearchBoxPtr, SSearchBox )
			.HintText(LOCTEXT("SearchPlaceables", "Search Classes"))
			.OnTextChanged(this, &SPlacementModeTools::OnSearchChanged)
			.OnTextCommitted(this, &SPlacementModeTools::OnSearchCommitted)
		]

		+ SVerticalBox::Slot()
		.Padding( 0 )
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				Tabs.ToSharedRef()
			]

			+ SHorizontalBox::Slot()
			[
				SNew(SBorder)
				.Padding(FMargin(3))
				.BorderImage(FEditorStyle::GetBrush("ToolPanel.DarkGroupBorder"))
				[
					SNew(SOverlay)

					+ SOverlay::Slot()
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Fill)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("NoResultsFound", "No Results Found"))
						.Visibility(this, &SPlacementModeTools::GetFailedSearchVisibility)
					]

					+ SOverlay::Slot()
					[
						SAssignNew(CustomContent, SBox)
					]

					+ SOverlay::Slot()
					[
						SAssignNew(DataDrivenContent, SBox)
						[
							SNew(SHorizontalBox)

							+ SHorizontalBox::Slot()
							[
								SAssignNew(ListView, SListView<TSharedPtr<FPlaceableItem>>)
								.ListItemsSource(&FilteredItems)
								.OnGenerateRow(this, &SPlacementModeTools::OnGenerateWidgetForItem)
								.ExternalScrollbar(ScrollBar)
							]

							+ SHorizontalBox::Slot()
							.AutoWidth()
							[
								ScrollBar
							]
						]
					]
				]
			]
		]
	];

	IPlacementModeModule& PlacementModeModule = IPlacementModeModule::Get();
	PlacementModeModule.OnRecentlyPlacedChanged().AddSP(this, &SPlacementModeTools::RequestRefreshRecentlyPlaced);
	PlacementModeModule.OnAllPlaceableAssetsChanged().AddSP(this, &SPlacementModeTools::RequestRefreshAllClasses);
	PlacementModeModule.OnPlaceableItemFilteringChanged().AddSP(this, &SPlacementModeTools::RequestUpdateShownItems);
	PlacementModeModule.OnPlacementModeCategoryListChanged().AddSP(this, &SPlacementModeTools::UpdatePlacementCategories);
}

TSharedRef< SWidget > SPlacementModeTools::CreatePlacementGroupTab( const FPlacementCategoryInfo& Info )
{
	return SNew( SCheckBox )
	.Style( FEditorStyle::Get(), "PlacementBrowser.Tab" )
	.OnCheckStateChanged( this, &SPlacementModeTools::OnPlacementTabChanged, Info.UniqueHandle )
	.IsChecked( this, &SPlacementModeTools::GetPlacementTabCheckedState, Info.UniqueHandle )
	[
		SNew( SOverlay )

		+ SOverlay::Slot()
		.VAlign( VAlign_Center )
		[
			SNew(SSpacer)
			.Size( FVector2D( 1, 30 ) )
		]

		+ SOverlay::Slot()
		.Padding( FMargin(6, 0, 15, 0) )
		.VAlign( VAlign_Center )
		[
			SNew( STextBlock )
			.TextStyle( FEditorStyle::Get(), "PlacementBrowser.Tab.Text" )
			.Text( Info.DisplayName )
		]

		+ SOverlay::Slot()
		.VAlign( VAlign_Fill )
		.HAlign( HAlign_Left )
		[
			SNew(SImage)
			.Image( this, &SPlacementModeTools::PlacementGroupBorderImage, Info.UniqueHandle )
		]
	];
}

FName SPlacementModeTools::GetActiveTab() const
{
	return IsSearchActive() ? FBuiltInPlacementCategories::AllClasses() : ActiveTabName;
}

void SPlacementModeTools::SetActiveTab(FName TabName)
{
	if (TabName != ActiveTabName)
	{
		ActiveTabName = TabName;
		IPlacementModeModule::Get().RegenerateItemsForCategory(ActiveTabName);
		bUpdateShownItems = true;
	}
}

void SPlacementModeTools::UpdateShownItems()
{
	bUpdateShownItems = false;

	IPlacementModeModule& PlacementModeModule = IPlacementModeModule::Get();

	const FPlacementCategoryInfo* Category = PlacementModeModule.GetRegisteredPlacementCategory(GetActiveTab());
	if (!Category)
	{
		return;
	}
	else if (Category->CustomGenerator)
	{
		CustomContent->SetContent(Category->CustomGenerator());

		CustomContent->SetVisibility(EVisibility::Visible);
		DataDrivenContent->SetVisibility(EVisibility::Collapsed);
	}
	else
	{
		FilteredItems.Reset();
		
		if (IsSearchActive())
		{
			auto Filter = [&](const TSharedPtr<FPlaceableItem>& Item) { return SearchTextFilter->PassesFilter(*Item); };
			PlacementModeModule.GetFilteredItemsForCategory(Category->UniqueHandle, FilteredItems, Filter);
			
			if (Category->bSortable)
			{
				FilteredItems.Sort(&FSortPlaceableItems::SortItemsByName);
			}
		}
		else
		{
			PlacementModeModule.GetItemsForCategory(Category->UniqueHandle, FilteredItems);

			if (Category->bSortable)
			{
				FilteredItems.Sort(&FSortPlaceableItems::SortItemsByOrderThenName);
			}
		}

		CustomContent->SetVisibility(EVisibility::Collapsed);
		DataDrivenContent->SetVisibility(EVisibility::Visible);
		ListView->RequestListRefresh();
	}
}

bool SPlacementModeTools::IsSearchActive() const
{
	return !SearchTextFilter->GetRawFilterText().IsEmpty();
}

ECheckBoxState SPlacementModeTools::GetPlacementTabCheckedState( FName CategoryName ) const
{
	return ActiveTabName == CategoryName ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

EVisibility SPlacementModeTools::GetFailedSearchVisibility() const
{
	if (!IsSearchActive() || FilteredItems.Num())
	{
		return EVisibility::Collapsed;
	}
	return EVisibility::Visible;
}

EVisibility SPlacementModeTools::GetTabsVisibility() const
{
	return IsSearchActive() ? EVisibility::Collapsed : EVisibility::Visible;
}

TSharedRef<ITableRow> SPlacementModeTools::OnGenerateWidgetForItem(TSharedPtr<FPlaceableItem> InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STableRow<TSharedPtr<FPlaceableItem>>, OwnerTable)
		[
			SNew(SPlacementAssetEntry, InItem.ToSharedRef())
			.HighlightText(this, &SPlacementModeTools::GetHighlightText)
		];
}

void SPlacementModeTools::OnPlacementTabChanged( ECheckBoxState NewState, FName CategoryName )
{
	if ( NewState == ECheckBoxState::Checked )
	{
		SetActiveTab(CategoryName);
	}
}

const FSlateBrush* SPlacementModeTools::PlacementGroupBorderImage( FName CategoryName ) const
{
	if ( ActiveTabName == CategoryName )
	{
		static FName PlacementBrowserActiveTabBarBrush( "PlacementBrowser.ActiveTabBar" );
		return FEditorStyle::GetBrush( PlacementBrowserActiveTabBarBrush );
	}
	else
	{
		return nullptr;
	}
}

void SPlacementModeTools::RequestUpdateShownItems()
{
	bUpdateShownItems = true;
}

void SPlacementModeTools::RequestRefreshRecentlyPlaced( const TArray< FActorPlacementInfo >& RecentlyPlaced )
{
	if (GetActiveTab() == FBuiltInPlacementCategories::RecentlyPlaced())
	{
		bRefreshRecentlyPlaced = true;
	}
}

void SPlacementModeTools::RequestRefreshAllClasses()
{
	if (GetActiveTab() == FBuiltInPlacementCategories::AllClasses())
	{
		bRefreshAllClasses = true;
	}
}

void SPlacementModeTools::UpdatePlacementCategories()
{
	bool BasicTabExists = false;
	FName TabToActivate;

	Tabs->ClearChildren();

	TArray<FPlacementCategoryInfo> Categories;
	IPlacementModeModule::Get().GetSortedCategories(Categories);
	for (const FPlacementCategoryInfo& Category : Categories)
	{
		if (Category.UniqueHandle == FBuiltInPlacementCategories::Basic())
		{
			BasicTabExists = true;
		}

		if (Category.UniqueHandle == ActiveTabName)
		{
			TabToActivate = ActiveTabName;
		}

		Tabs->AddSlot()
			.AutoHeight()
			[
				CreatePlacementGroupTab(Category)
			];
	}

	if (TabToActivate.IsNone())
	{
		if (BasicTabExists)
		{
			TabToActivate = FBuiltInPlacementCategories::Basic();
		}
		else if (Categories.Num() > 0)
		{
			TabToActivate = Categories[0].UniqueHandle;
		}
	}

	SetActiveTab(TabToActivate);
}

void SPlacementModeTools::Tick( const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime )
{
	if (bRefreshAllClasses)
	{
		IPlacementModeModule::Get().RegenerateItemsForCategory(FBuiltInPlacementCategories::AllClasses());
		bRefreshAllClasses = false;
		bUpdateShownItems = true;
	}

	if (bRefreshRecentlyPlaced)
	{
		IPlacementModeModule::Get().RegenerateItemsForCategory(FBuiltInPlacementCategories::RecentlyPlaced());
		bRefreshRecentlyPlaced = false;
		bUpdateShownItems = true;
	}

	if (bUpdateShownItems)
	{
		UpdateShownItems();
	}
}

FReply SPlacementModeTools::OnKeyDown( const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent )
{
	FReply Reply = FReply::Unhandled();

	if ( InKeyEvent.GetKey() == EKeys::Escape )
	{
		FPlacementMode* PlacementEditMode = (FPlacementMode*)GLevelEditorModeTools().GetActiveMode( FBuiltinEditorModes::EM_Placement );
		// Catch potential nullptr
		if (ensureMsgf(PlacementEditMode, TEXT("PlacementEditMode was null, but SPlacementModeTools is still accepting KeyDown events")))
		{
			PlacementEditMode->StopPlacing();
		}
		Reply = FReply::Handled();
	}

	return Reply;
}

void SPlacementModeTools::OnSearchChanged(const FText& InFilterText)
{
	// If the search text was previously empty we do a full rebuild of our cached widgets
	// for the placeable widgets.
	if ( !IsSearchActive() )
	{
		bRefreshAllClasses = true;
	}
	else
	{
		bUpdateShownItems = true;
	}

	SearchTextFilter->SetRawFilterText( InFilterText );
	SearchBoxPtr->SetError( SearchTextFilter->GetFilterErrorText() );
}

void SPlacementModeTools::OnSearchCommitted(const FText& InFilterText, ETextCommit::Type InCommitType)
{
	OnSearchChanged(InFilterText);
}

FText SPlacementModeTools::GetHighlightText() const
{
	return SearchTextFilter->GetRawFilterText();
}

#undef LOCTEXT_NAMESPACE