// Copyright Epic Games, Inc. All Rights Reserved.

#include "SSaveAndLoadFilters.h"

#include "DisjunctiveNormalFormFilter.h"
#include "FilterLoader.h"

#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"

#include "SAssetSearchBox.h"
#include "SAssetDropTarget.h"
#include "Engine/AssetManager.h"

#define LOCTEXT_NAMESPACE "LevelSnapshotsEditor"

namespace
{
	constexpr ANSICHAR* ComboButtonStyle = "RoundButton";
	constexpr ANSICHAR* FontStyle = "GenericFilters.TextStyle";
	
	void SaveAs(TWeakObjectPtr<UFilterLoader> FilterLoader)
	{
		FilterLoader->SaveAs();
	}

	void SaveExisting(TWeakObjectPtr<UFilterLoader> FilterLoader)
	{
		FilterLoader->OverwriteExisting();
	}
	
	TSharedRef<SWidget> MakeSaveMenu(TWeakObjectPtr<UFilterLoader> FilterLoader)
	{
		class FFilterMenuBuilder
		{
			FMenuBuilder MenuBuilder = FMenuBuilder(true, nullptr, nullptr, true);
			const TWeakObjectPtr<UFilterLoader>& FilterLoader;
			
		public:

			FFilterMenuBuilder(const TWeakObjectPtr<UFilterLoader>& InFilterLoader)
				:
				FilterLoader(InFilterLoader)
			{}
			
			FFilterMenuBuilder& Build_SaveAs()
			{
				
				MenuBuilder.BeginSection("ContentBrowserClearFilters");
				{
					MenuBuilder.AddMenuEntry(
						LOCTEXT("SaveFiltersAs", "Save as..."),
						LOCTEXT("SaveFiltersAsToolTip", "Saves a new asset."),
						FSlateIcon(),
						FUIAction(
							FExecuteAction::CreateStatic(&SaveAs, FilterLoader)
						)
						);
				}
				MenuBuilder.EndSection();
				
				return *this;
			}

			FFilterMenuBuilder& Build_SaveExisting()
			{
				MenuBuilder.BeginSection("ContentBrowserClearFilters");
				{
					MenuBuilder.AddMenuEntry(
						LOCTEXT("SaveExistingFilters", "Overwrite existing"),
						LOCTEXT("SaveExistingFiltersToolTip", "Overwrite the asset you last loaded"),
						FSlateIcon(),
						FUIAction(
							FExecuteAction::CreateStatic(&SaveExisting, FilterLoader)
						)
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
				FFilterMenuBuilder(FilterLoader)
					.Build_SaveAs()
					.Build_SaveExisting()
					.MakeWidget()
			];
	}

	TSharedRef<SWidget> GenerateAssetPicker(TWeakObjectPtr<UFilterLoader> FilterLoader, FOnAssetSelected OnAssetSelected, FOnAssetEnterPressed OnEnterPressed)
	{
		FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

		FAssetPickerConfig AssetPickerConfig;
		AssetPickerConfig.Filter.ClassNames.Add(UDisjunctiveNormalFormFilter::StaticClass()->GetFName());
		AssetPickerConfig.bAllowNullSelection = false;
		AssetPickerConfig.Filter.bRecursiveClasses = true;
		AssetPickerConfig.OnAssetSelected = OnAssetSelected;
		AssetPickerConfig.OnAssetEnterPressed = OnEnterPressed;
		AssetPickerConfig.InitialAssetViewType = EAssetViewType::List;
		AssetPickerConfig.bAllowDragging = false;

		const TOptional<FAssetData> CurrentlyEdited = FilterLoader->GetAssetLastSavedOrLoaded();
		if (CurrentlyEdited.IsSet())
		{
			AssetPickerConfig.InitialAssetSelection = *CurrentlyEdited;
		}

		return
			SNew(SBox)
			.HeightOverride(300)
			.WidthOverride(300)
			[
				SNew(SBorder)
				.BorderImage(FEditorStyle::GetBrush("Menu.Background"))
				[
					ContentBrowserModule.Get().CreateAssetPicker(AssetPickerConfig)
				]
			];
	}
}

SSaveAndLoadFilters::~SSaveAndLoadFilters()
{
	if (ensure(FilterLoader.IsValid()))
	{
		FilterLoader->OnFilterWasSavedOrLoaded.Remove(OnEditedAssetChangedHandle);
	}
}

void SSaveAndLoadFilters::Construct(const FArguments& InArgs, UFilterLoader* InFilterLoader)
{
	if (!ensure(InFilterLoader))
	{
		return;
	}

	FilterLoader = InFilterLoader;
	OnEditedAssetChangedHandle = FilterLoader->OnFilterWasSavedOrLoaded.AddLambda([this]()
	{
		RebuildButtons();
	});
	
	RebuildButtons();
}

void SSaveAndLoadFilters::RebuildButtons()
{
	struct Local
	{
		static TSharedRef<SWidget> BuildSaveButton(const bool bIsFilterBeingEdited, const TWeakObjectPtr<UFilterLoader>& FilterLoader)
		{
			if (bIsFilterBeingEdited)
			{
				return SNew(SComboButton)
					.ButtonStyle(FEditorStyle::Get(), ComboButtonStyle)
					.ComboButtonStyle( FEditorStyle::Get(), "GenericFilters.ComboButtonStyle" )
					.ForegroundColor(FLinearColor::White)
					.ContentPadding(0)
					.ToolTipText( LOCTEXT( "SaveWithEditedToolTip", "Save the current filters." ) )
					.OnGetMenuContent(FOnGetContent::CreateStatic(&MakeSaveMenu, FilterLoader))
					.ContentPadding( FMargin( 1, 0 ) )
					.Visibility(EVisibility::Visible)
					.HasDownArrow(true)
					.ButtonContent()
					[
						SNew(STextBlock)
						.TextStyle(FEditorStyle::Get(), FontStyle)
						.Text(LOCTEXT("SaveWithEditedSave", "Save"))
					];
			}
			else
			{
				return SNew(SButton)
					.ButtonStyle(FEditorStyle::Get(), ComboButtonStyle)
					.ForegroundColor(FLinearColor::White)
					.ContentPadding(0)
					.ToolTipText( LOCTEXT( "SaveWithoutEditedToolTip", "Save the current filters." ) )
					.ContentPadding( FMargin( 1, 0 ) )
					.Visibility(EVisibility::Visible)
					.OnClicked_Lambda([FilterLoader]()
					{
						SaveAs(FilterLoader);
						return FReply::Handled();
					})
					.Content()
					[
						SNew(STextBlock)
						.TextStyle(FEditorStyle::Get(), FontStyle)
						.Text(LOCTEXT("SaveWithoutEditedSave", "Save as..."))
					];
			}
		}
	};
	
	if (!ensure(FilterLoader.IsValid()))
	{
		return;
	}
	
	const bool bIsFilterBeingEdited = FilterLoader->GetAssetLastSavedOrLoaded().IsSet();
	const TSharedRef<SWidget> SaveButton = Local::BuildSaveButton(bIsFilterBeingEdited, FilterLoader);
	
	ChildSlot
	[
		SNew(SHorizontalBox)

		// Save
		+ SHorizontalBox::Slot()
		.Padding(2.f)
		.AutoWidth()
		[
			SaveButton
		]

		// Load
		+ SHorizontalBox::Slot()
		.Padding(2.f)
		.AutoWidth()
		[
			SNew(SComboButton)
			.ButtonStyle(FEditorStyle::Get(), ComboButtonStyle)
			.ComboButtonStyle( FEditorStyle::Get(), "GenericFilters.ComboButtonStyle" )
			.ForegroundColor(FLinearColor::White)
			.ContentPadding(0)
			.ToolTipText( LOCTEXT( "LoadFiltersTooltip", "Loads a filter set you saved." ) )
			.OnGetMenuContent(
				FOnGetContent::CreateStatic(&GenerateAssetPicker, 
					FilterLoader,
					FOnAssetSelected::CreateLambda([this](const FAssetData& SelectedAssetData)
					{
						UObject* Loaded = SelectedAssetData.GetAsset();
						UDisjunctiveNormalFormFilter* AsFilter = Cast<UDisjunctiveNormalFormFilter>(Loaded);
						if (ensure(Loaded) && ensure(AsFilter))
						{
							FilterLoader->LoadAsset(AsFilter);
						}
					}),
					FOnAssetEnterPressed::CreateLambda([this](const TArray<FAssetData>& Assets)
					{
						
					})))
			.HasDownArrow( true )
			.ContentPadding( FMargin( 1, 0 ) )
			.Visibility(EVisibility::Visible)
			.HasDownArrow(true)
			.ButtonContent()
			[
				SNew(STextBlock)
				.TextStyle(FEditorStyle::Get(), FontStyle)
				.Text(LOCTEXT("LoadFilters", "Load"))
			]
		]
	];
}

#undef LOCTEXT_NAMESPACE
