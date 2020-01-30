// Copyright Epic Games, Inc. All Rights Reserved.

#include "SPluginBrowser.h"
#include "Misc/App.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SToolTip.h"
#include "Framework/Docking/TabManager.h"
#include "EditorStyleSet.h"
#include "UnrealEdMisc.h"
#include "Interfaces/IPluginManager.h"
#include "PluginStyle.h"
#include "Widgets/Navigation/SBreadcrumbTrail.h"
#include "SPluginCategoryTree.h"
#include "Widgets/Input/SSearchBox.h"
#include "PluginBrowserModule.h"
#include "IDirectoryWatcher.h"
#include "DirectoryWatcherModule.h"
#include "Interfaces/IProjectManager.h"
#include "ProjectDescriptor.h"
#include "GameProjectGenerationModule.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

#define LOCTEXT_NAMESPACE "PluginsEditor"

SPluginBrowser::~SPluginBrowser()
{
	FDirectoryWatcherModule& DirectoryWatcherModule = FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>(TEXT("DirectoryWatcher"));
	for(auto& Pair: WatchDirectories)
	{
		DirectoryWatcherModule.Get()->UnregisterDirectoryChangedCallback_Handle(Pair.Key, Pair.Value);
	}

	FPluginBrowserModule::Get().OnNewPluginCreated().RemoveAll(this);
}

void SPluginBrowser::Construct( const FArguments& Args )
{
	// Get the root directories which contain plugins
	TArray<FString> WatchDirectoryNames;
	WatchDirectoryNames.Add(FPaths::EnginePluginsDir());
	if (FPaths::DirectoryExists(FPaths::EnterprisePluginsDir()))
	{
		WatchDirectoryNames.Add(FPaths::EnterprisePluginsDir());
	}
	if(FApp::HasProjectName())
	{
		const FString ProjectPluginsDir = FPaths::ProjectPluginsDir();
		if (FPaths::DirectoryExists(ProjectPluginsDir))
		{
			WatchDirectoryNames.Add(ProjectPluginsDir);
		}
		const FProjectDescriptor* Project = IProjectManager::Get().GetCurrentProject();
		if (Project != nullptr)
		{
			for (const FString& Path : Project->GetAdditionalPluginDirectories())
			{
				if (FPaths::DirectoryExists(Path))
				{
					WatchDirectoryNames.Add(Path);
				}
			}
		}
	}

	// Add watchers for any change events on those directories
	FDirectoryWatcherModule& DirectoryWatcherModule = FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>(TEXT("DirectoryWatcher"));
	for(const FString& WatchDirectoryName: WatchDirectoryNames)
	{
		FDelegateHandle Handle;
		if(DirectoryWatcherModule.Get()->RegisterDirectoryChangedCallback_Handle(WatchDirectoryName, IDirectoryWatcher::FDirectoryChanged::CreateRaw(this, &SPluginBrowser::OnPluginDirectoryChanged), Handle, IDirectoryWatcher::WatchOptions::IncludeDirectoryChanges))
		{
			WatchDirectories.Add(WatchDirectoryName, Handle);
		}
	}

	FPluginBrowserModule::Get().OnNewPluginCreated().AddSP(this, &SPluginBrowser::OnNewPluginCreated);

	RegisterActiveTimer (0.f, FWidgetActiveTimerDelegate::CreateSP (this, &SPluginBrowser::TriggerBreadcrumbRefresh));

	struct Local
	{
		static void PluginToStringArray( const IPlugin* Plugin, OUT TArray< FString >& StringArray )
		{
			// NOTE: Only the friendly name is searchable for now.  We don't display the actual plugin name in the UI.
			const FPluginDescriptor& Descriptor = Plugin->GetDescriptor();
			StringArray.Add( Descriptor.FriendlyName );
			StringArray.Add( Descriptor.Description );
		}
	};

	// Setup text filtering
	PluginTextFilter = MakeShareable( new FPluginTextFilter( FPluginTextFilter::FItemToStringArray::CreateStatic( &Local::PluginToStringArray ) ) );

	const float PaddingAmount = 2.0f;

	PluginCategories = SNew( SPluginCategoryTree, SharedThis( this ) );

	FMenuBuilder DetailViewOptions(true, nullptr);
	DetailViewOptions.AddMenuEntry(
		LOCTEXT("ShowOnlyEnabled", "Show Only Enabled"),
		LOCTEXT("ShowOnlyEnabled_ToolTip", "Displays only the plugins which are enabled"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this]
			{
				PluginCategories->ToggleFilterType(SPluginCategoryTree::EFilterType::OnlyEnabled);
			}),
			FCanExecuteAction(),
			FIsActionChecked::CreateLambda([this]() -> bool
			{
				return PluginCategories->IsFilterEnabled(SPluginCategoryTree::EFilterType::OnlyEnabled);
			})
		),
		NAME_None,
		EUserInterfaceActionType::ToggleButton
	);

	DetailViewOptions.AddMenuEntry(
		LOCTEXT("ShowOnlyDisabled", "Show Only Disabled"),
		LOCTEXT("ShowOnlyDisabled_ToolTip", "Displays only the plugins which are disabled"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this]
			{
				PluginCategories->ToggleFilterType(SPluginCategoryTree::EFilterType::OnlyDisabled);
			}),
			FCanExecuteAction(),
			FIsActionChecked::CreateLambda([this]() -> bool
			{
				return PluginCategories->IsFilterEnabled(SPluginCategoryTree::EFilterType::OnlyDisabled);
			})
		),
		NAME_None,
		EUserInterfaceActionType::ToggleButton
	);

	TSharedRef<SVerticalBox> MainContent = SNew( SVerticalBox )
	+SVerticalBox::Slot()
	[
		SNew( SSplitter )
		+SSplitter::Slot()
		.Value(.3f)
		[
			SNew(SBorder)
			.Padding(8.0f)
			.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
			[
				PluginCategories.ToSharedRef()
			]
		]
		+SSplitter::Slot()
		[
			SNew( SVerticalBox )

			+SVerticalBox::Slot()
			.Padding(FMargin(0.0f, 0.0f, 0.0f, PaddingAmount))
			.AutoHeight()
			[
				SNew( SHorizontalBox )

				+SHorizontalBox::Slot()
				.Padding( PaddingAmount )
				[
					SAssignNew( BreadcrumbTrail, SBreadcrumbTrail<TSharedPtr<FPluginCategory>> )
					.DelimiterImage( FPluginStyle::Get()->GetBrush( "Plugins.BreadcrumbArrow" ) ) 
					.ShowLeadingDelimiter( true )
					.OnCrumbClicked( this, &SPluginBrowser::BreadcrumbTrail_OnCrumbClicked )
				]
	
				+SHorizontalBox::Slot()
				.Padding( PaddingAmount )
				[
					SAssignNew( SearchBoxPtr, SSearchBox )
					.OnTextChanged( this, &SPluginBrowser::SearchBox_OnPluginSearchTextChanged )
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SComboButton)
					.ContentPadding(0)
					.ForegroundColor(FSlateColor::UseForeground())
					.ButtonStyle(FEditorStyle::Get(), "ToggleButton")
					.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("ViewOptions")))
					.MenuContent()
					[
						DetailViewOptions.MakeWidget()
					]
					.ButtonContent()
					[
						SNew(SImage)
						.Image(FEditorStyle::GetBrush("GenericViewButton"))
					]
				]
			]

			+SVerticalBox::Slot()
			[
				SAssignNew( PluginList, SPluginTileList, SharedThis( this ) )
			]

			+SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FMargin(PaddingAmount, PaddingAmount, PaddingAmount, 0.0f))
			[
				SNew(SBorder)
				.BorderBackgroundColor(FLinearColor::Yellow)
				.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
				.Padding(8.0f)
				.Visibility(this, &SPluginBrowser::HandleRestartEditorNoticeVisibility)
				[
					SNew( SHorizontalBox )

					+SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(FMargin(0.0f, 0.0f, 8.0f, 0.0f))
					[
						SNew(SImage)
						.Image(FPluginStyle::Get()->GetBrush("Plugins.Warning"))
					]

					+SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("PluginSettingsRestartNotice", "Unreal Editor must be restarted for the plugin changes to take effect."))
					]

					+SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.HAlign(HAlign_Right)
					[
						SNew(SButton)
						.Text(LOCTEXT("PluginSettingsRestartEditor", "Restart Now"))
						.OnClicked(this, &SPluginBrowser::HandleRestartEditorButtonClicked)
						.TextStyle(FEditorStyle::Get(), "LargeText")
						.ButtonStyle(FEditorStyle::Get(), "FlatButton.Default")
					]
				]
			]


			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FMargin(PaddingAmount, PaddingAmount, PaddingAmount, 0.0f))
			.HAlign(HAlign_Right)
			[
				SNew(SButton)
				.ContentPadding(5)
				.IsEnabled(true)
				.ToolTip(SNew(SToolTip).Text(LOCTEXT("NewPluginEnabled", "Click here to open the Plugin Creator dialog.")))
				.TextStyle(FEditorStyle::Get(), "LargeText")
				.ButtonStyle(FEditorStyle::Get(), "FlatButton.Success")
				.HAlign(HAlign_Center)
				.Text(LOCTEXT("NewPluginLabel", "New Plugin"))
				.OnClicked(this, &SPluginBrowser::HandleNewPluginButtonClicked)
			]
		]
	];

	ChildSlot
	[
		MainContent
	];
}


EVisibility SPluginBrowser::HandleRestartEditorNoticeVisibility() const
{
	return FPluginBrowserModule::Get().HasPluginsPendingEnable() ? EVisibility::Visible : EVisibility::Collapsed;
}

FReply SPluginBrowser::HandleRestartEditorButtonClicked() const
{
	const bool bWarn = false;
	FUnrealEdMisc::Get().RestartEditor(bWarn);
	return FReply::Handled();
}

void SPluginBrowser::SearchBox_OnPluginSearchTextChanged( const FText& NewText )
{
	PluginTextFilter->SetRawFilterText( NewText );
	SearchBoxPtr->SetError( PluginTextFilter->GetFilterErrorText() );
}


TSharedPtr< FPluginCategory > SPluginBrowser::GetSelectedCategory() const
{
	return PluginCategories->GetSelectedCategory();
}


void SPluginBrowser::OnCategorySelectionChanged()
{
	if( PluginList.IsValid() )
	{
		PluginList->SetNeedsRefresh();
	}

	// Breadcrumbs will need to be refreshed
	RegisterActiveTimer (0.f, FWidgetActiveTimerDelegate::CreateSP (this, &SPluginBrowser::TriggerBreadcrumbRefresh));
}

void SPluginBrowser::SetNeedsRefresh()
{
	if( PluginList.IsValid() )
	{
		PluginList->SetNeedsRefresh();
	}

	if( PluginCategories.IsValid() )
	{
		PluginCategories->SetNeedsRefresh();
	}

	// Breadcrumbs will need to be refreshed
	RegisterActiveTimer (0.f, FWidgetActiveTimerDelegate::CreateSP (this, &SPluginBrowser::TriggerBreadcrumbRefresh));
}

void SPluginBrowser::OnPluginDirectoryChanged(const TArray<struct FFileChangeData>&)
{
	if(UpdatePluginsTimerHandle.IsValid())
	{
		UnRegisterActiveTimer(UpdatePluginsTimerHandle.ToSharedRef());
	}
	UpdatePluginsTimerHandle = RegisterActiveTimer(2.0f, FWidgetActiveTimerDelegate::CreateSP(this, &SPluginBrowser::UpdatePluginsTimerCallback));
}

void SPluginBrowser::OnNewPluginCreated()
{
	if (UpdatePluginsTimerHandle.IsValid())
	{
		UnRegisterActiveTimer(UpdatePluginsTimerHandle.ToSharedRef());
	}
	UpdatePluginsTimerHandle = RegisterActiveTimer(2.0f, FWidgetActiveTimerDelegate::CreateSP(this, &SPluginBrowser::UpdatePluginsTimerCallback));
}

EActiveTimerReturnType SPluginBrowser::UpdatePluginsTimerCallback(double InCurrentTime, float InDeltaTime)
{
	IPluginManager::Get().RefreshPluginsList();
	SetNeedsRefresh();
	return EActiveTimerReturnType::Stop;
}

EActiveTimerReturnType SPluginBrowser::TriggerBreadcrumbRefresh(double InCurrentTime, float InDeltaTime)
{
	RefreshBreadcrumbTrail();
	return EActiveTimerReturnType::Stop;
}

void SPluginBrowser::RefreshBreadcrumbTrail()
{
	// Update breadcrumb trail
	if( BreadcrumbTrail.IsValid() )
	{
		TSharedPtr<FPluginCategory> SelectedCategory = PluginCategories->GetSelectedCategory();

		// Build up the list of categories, starting at the selected node and working our way backwards.
		TArray<TSharedPtr<FPluginCategory>> CategoryPath;
		if(SelectedCategory.IsValid())
		{
			for(TSharedPtr<FPluginCategory> NextCategory = SelectedCategory; NextCategory.IsValid(); NextCategory = NextCategory->ParentCategory.Pin())
			{
				CategoryPath.Insert( NextCategory, 0 );
			}
		}

		// Fill in the crumbs
		BreadcrumbTrail->ClearCrumbs();
		for(TSharedPtr<FPluginCategory>& Category: CategoryPath)
		{
			BreadcrumbTrail->PushCrumb( Category->DisplayName, Category );
		}
	}
}


void SPluginBrowser::BreadcrumbTrail_OnCrumbClicked( const TSharedPtr<FPluginCategory>& Category )
{
	if( PluginCategories.IsValid() )
	{
		PluginCategories->SelectCategory( Category );
	}
}

FReply SPluginBrowser::HandleNewPluginButtonClicked() const
{
	FGlobalTabmanager::Get()->InvokeTab( FPluginBrowserModule::PluginCreatorTabName );

	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
