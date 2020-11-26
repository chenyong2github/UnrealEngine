// Copyright Epic Games, Inc. All Rights Reserved.

#include "SFavoriteFilterList.h"

#include "LevelSnapshotFilters.h"
#include "SFavoriteFilter.h"

#include "Widgets/Layout/SWrapBox.h"

#define LOCTEXT_NAMESPACE "LevelSnapshotsEditor"

namespace
{
	void CreateMenuEntry(const TSubclassOf<ULevelSnapshotFilter>& FilterClass, FMenuBuilder& MenuBuilder, const TWeakObjectPtr<UFavoriteFilterContainer>& Filters)
	{
		check(FilterClass.Get());
		
		FUIAction AddFilterAction;
		AddFilterAction.ExecuteAction.BindLambda([Filters, FilterClass]()
		{
			if (ensure(Filters.IsValid()))
			{
				const bool bIsFavorite = Filters->GetFavorites().Contains(FilterClass);
				if (bIsFavorite)
				{
					Filters->RemoveFromFavorites(FilterClass);
				}
				else
				{
					Filters->AddToFavorites(FilterClass);
				}
			}
		});
		AddFilterAction.GetActionCheckState.BindLambda([Filters, FilterClass]() -> ECheckBoxState
		{
			if (ensure(Filters.IsValid()))
			{
				const bool bIsFavorite = Filters->GetFavorites().Contains(FilterClass);
				return bIsFavorite ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			}
			return ECheckBoxState::Undetermined;
		});
		
		MenuBuilder.AddMenuEntry(
			FilterClass->GetDisplayNameText(),
			FilterClass->GetDisplayNameText(),
			FSlateIcon(),
			AddFilterAction,
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);
	}
	
	TSharedRef<SWidget> MakeAddFilterMenu(const TWeakObjectPtr<UFavoriteFilterContainer>& FilterModel)
	{
		class FFilterMenuBuilder
		{
			FMenuBuilder MenuBuilder = FMenuBuilder(true, nullptr, nullptr, true);
			const TWeakObjectPtr<UFavoriteFilterContainer>& FilterModel;

			void CreateMenuEntry(const TSubclassOf<ULevelSnapshotFilter>& FilterClass)
			{
				::CreateMenuEntry(FilterClass, MenuBuilder, FilterModel);
			}
			
		public:

			FFilterMenuBuilder(const TWeakObjectPtr<UFavoriteFilterContainer>& InFilterModel)
				:
				FilterModel(InFilterModel)
			{}
			
			FFilterMenuBuilder& Build_ClearFilters()
			{
				UFavoriteFilterContainer* Filters = FilterModel.Get();
				if (!ensure(Filters))
				{
					return *this;
				}
				
				MenuBuilder.BeginSection("ContentBrowserClearFilters");
				{
					MenuBuilder.AddMenuEntry(
						LOCTEXT("FilterListClearFilters", "Clear Filters"),
						LOCTEXT("FilterListClearToolTip", "Clears current filter selection"),
						FSlateIcon(),
						FUIAction(
							FExecuteAction::CreateLambda([Filters = FilterModel]()
							{
								if (ensure(Filters.IsValid()))
								{
									Filters->ClearFavorites();
								}
							})
						)
						);
				}
				MenuBuilder.EndSection();
				
				return *this;
			}

			FFilterMenuBuilder& Build_CommonFilters()
			{
				UFavoriteFilterContainer* Filters = FilterModel.Get();
				if (!ensure(Filters))
				{
					return *this;
				}
				
				MenuBuilder.BeginSection("CommonFilters");
				for (const TSubclassOf<ULevelSnapshotFilter>& CommonFilter : Filters->GetCommonFilters())
				{
					CreateMenuEntry(CommonFilter);
				}
				MenuBuilder.EndSection();
				
				return *this;
			}

			FFilterMenuBuilder& Build_NativeAndBlueprintFilters()
			{
				UFavoriteFilterContainer* Filters = FilterModel.Get();
				if (!ensure(Filters))
				{
					return *this;
				}
				
				MenuBuilder.BeginSection("AdvancedFilters");
				// Blueprint filters
				{
					const FText ParentFilter = FText::FromString("Blueprints");
					MenuBuilder.AddSubMenu(
						TAttribute<FText>(ParentFilter),
						TAttribute<FText>(LOCTEXT("FilterByBlueprintTooltipPrefix", "Filter by Blueprints")),
						FNewMenuDelegate::CreateLambda([Filters = FilterModel](FMenuBuilder& SubmenuBuilder)
						{
							if (ensure(Filters.IsValid()))
							{
								const TArray<TSubclassOf<ULevelSnapshotBlueprintFilter>> BlueprintFilters = Filters->GetAvailableBlueprintFilters();
								for (const TSubclassOf<ULevelSnapshotBlueprintFilter>& Filter : BlueprintFilters)
								{
									::CreateMenuEntry(Filter, SubmenuBuilder, Filters);
								}
							}
						}),
						FUIAction(
							FExecuteAction::CreateLambda([Filters = FilterModel]()
							{
								if (ensure(Filters.IsValid()))
								{
									const bool bNewShouldIncludeAllBlueprints = !Filters->ShouldIncludeAllBlueprintClasses();
									Filters->SetIncludeAllBlueprintClasses(bNewShouldIncludeAllBlueprints);
								}
							}),
							FCanExecuteAction(),
							FGetActionCheckState::CreateLambda([Filters = FilterModel]()
							{
								if (ensure(Filters.IsValid()))
								{
									return Filters->ShouldIncludeAllBlueprintClasses() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
								}
								return ECheckBoxState::Undetermined;
							})
							),
						NAME_None,
						EUserInterfaceActionType::ToggleButton
					);
				}
				// C++ filters
				{
					const FText ParentFilter = FText::FromString("C++");
					MenuBuilder.AddSubMenu(
						TAttribute<FText>(ParentFilter),
						TAttribute<FText>(LOCTEXT("FilterByCppTooltipPrefix", "Filter by C++")),
						FNewMenuDelegate::CreateLambda([Filters = FilterModel](FMenuBuilder& SubmenuBuilder)
						{
							if (ensure(Filters.IsValid()))
							{
								const TArray<TSubclassOf<ULevelSnapshotFilter>> BlueprintFilters = Filters->GetAvailableNativeFilters();
								for (const TSubclassOf<ULevelSnapshotFilter>& Filter : BlueprintFilters)
								{
									::CreateMenuEntry(Filter, SubmenuBuilder, Filters);
								}
							}
						}),
						FUIAction(
							FExecuteAction::CreateLambda([Filters = FilterModel]()
							{
								if (ensure(Filters.IsValid()))
								{
									const bool bNewShouldIncludedNative = !Filters->ShouldIncludeAllNativeClasses();
									Filters->SetIncludeAllNativeClasses(bNewShouldIncludedNative);
								}
							}),
							FCanExecuteAction(),
							FGetActionCheckState::CreateLambda([Filters = FilterModel]()
							{
								if (ensure(Filters.IsValid()))
								{
									return Filters->ShouldIncludeAllNativeClasses() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
								}
								return ECheckBoxState::Undetermined;
							})
							),
						NAME_None,
						EUserInterfaceActionType::ToggleButton
					);
				}
				MenuBuilder.EndSection();
				
				return *this;
			}

			TSharedRef<SWidget> MakeWidget()
			{
				return MenuBuilder.MakeWidget();
			}
		};
		
		return SNew(SVerticalBox)
			+SVerticalBox::Slot()
			[
				FFilterMenuBuilder(FilterModel)
					.Build_ClearFilters()
					.Build_CommonFilters()
					.Build_NativeAndBlueprintFilters()
					.MakeWidget()
			];
	}
}

SFavoriteFilterList::~SFavoriteFilterList()
{
	FavoriteModel->OnFavoritesChanged.Remove(ChangedFavoritesDelegateHandle);
}

void SFavoriteFilterList::Construct(const FArguments& InArgs, UFavoriteFilterContainer* InModel, const TSharedRef<FLevelSnapshotsEditorFilters>& InFilters)
{
	if (!ensure(InModel))
	{
		return;
	}
	FavoriteModel = InModel;

	ChangedFavoritesDelegateHandle = InModel->OnFavoritesChanged.AddLambda(
		[this, InFilters]()
		{
			if (ensure(FavoriteModel.IsValid()) && ensure(FilterList.IsValid()))
			{
				FilterList->ClearChildren();
				
				const TArray<TSubclassOf<ULevelSnapshotFilter>>& FavoriteFilters = FavoriteModel->GetFavorites();
				for (const TSubclassOf<ULevelSnapshotFilter>& FavoriteFilter : FavoriteFilters)
				{
					FilterList->AddSlot()
						.Padding(3.f, 3.f)
						[
							SNew(SFavoriteFilter, FavoriteFilter, InFilters)
								.FilterName(FavoriteFilter->GetDisplayNameText())
						];
				}
			}
		}
	);
	
	ChildSlot
	[
		SNew(SVerticalBox)

		// Filter
		+ SVerticalBox::Slot() 
		.Padding(0.f, 0.f)
		.HAlign(HAlign_Left)
		.AutoHeight()
		[
			SNew(SComboButton)
			.ComboButtonStyle( FEditorStyle::Get(), "GenericFilters.ComboButtonStyle" )
			.ForegroundColor(FLinearColor::White)
			.ContentPadding(0)
			.ToolTipText( LOCTEXT( "SelectFilterToUseToolTip", "Select filters you want to use." ) )
			.OnGetMenuContent(FOnGetContent::CreateLambda([InModel]()
			{
				return MakeAddFilterMenu(InModel);
			}))
			.HasDownArrow( true )
			.ContentPadding( FMargin( 1, 0 ) )
			.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("ContentBrowserFiltersCombo"))) // TODO: this was copied over: is the needed?
			.Visibility(EVisibility::Visible)
			.ButtonContent()
			[
				SNew(SHorizontalBox)

				// TODO: this was copied over: is the needed?
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(STextBlock)
					.TextStyle(FEditorStyle::Get(), "GenericFilters.TextStyle")
					.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.9"))
					.Text(FText::FromString(FString(TEXT("\xf0b0"))) /*fa-filter*/)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(STextBlock)
					.TextStyle(FEditorStyle::Get(), "GenericFilters.TextStyle")
					.Text(LOCTEXT("FavoriteFilters", "Favorite filters"))
				]
			]
		]

		// Store favorites filters
		+ SVerticalBox::Slot()
		.Padding(2.f, 2.f)
		.AutoHeight()
		[
			SAssignNew(FilterList, SWrapBox)
			.UseAllottedSize(true)
		]
	];
}

#undef LOCTEXT_NAMESPACE