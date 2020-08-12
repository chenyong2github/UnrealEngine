// Copyright Epic Games, Inc. All Rights Reserved.


#include "ContentBrowserUtils.h"
#include "ContentBrowserSingleton.h"
#include "HAL/IConsoleManager.h"
#include "Misc/MessageDialog.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Misc/Paths.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FeedbackContext.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Modules/ModuleManager.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Layout/WidgetPath.h"
#include "SlateOptMacros.h"
#include "Framework/Application/MenuStack.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Input/SButton.h"
#include "EditorStyleSet.h"
#include "UnrealClient.h"
#include "Engine/World.h"
#include "Settings/ContentBrowserSettings.h"
#include "Settings/EditorExperimentalSettings.h"
#include "SourceControlOperations.h"
#include "ISourceControlModule.h"
#include "SourceControlHelpers.h"
#include "FileHelpers.h"
#include "ARFilter.h"
#include "AssetRegistryModule.h"
#include "IAssetTools.h"
#include "AssetToolsModule.h"
#include "Settings/EditorExperimentalSettings.h"

#include "PackagesDialog.h"
#include "PackageTools.h"
#include "ObjectTools.h"
#include "ImageUtils.h"
#include "Logging/MessageLog.h"
#include "Misc/EngineBuildSettings.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Interfaces/IPluginManager.h"
#include "SAssetView.h"
#include "SPathView.h"
#include "ContentBrowserLog.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Editor.h"

#include "IContentBrowserDataModule.h"
#include "ContentBrowserDataSource.h"
#include "ContentBrowserDataSubsystem.h"

#define LOCTEXT_NAMESPACE "ContentBrowser"

class SContentBrowserPopup : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS( SContentBrowserPopup ){}

		SLATE_ATTRIBUTE( FText, Message )

	SLATE_END_ARGS()

	/** Constructs this widget with InArgs */
	BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
	void Construct( const FArguments& InArgs )
	{
		ChildSlot
		[
			SNew(SBorder)
			.BorderImage(FEditorStyle::GetBrush("Menu.Background"))
			.Padding(10)
			.OnMouseButtonDown(this, &SContentBrowserPopup::OnBorderClicked)
			.BorderBackgroundColor(this, &SContentBrowserPopup::GetBorderBackgroundColor)
			[
				SNew(SHorizontalBox)

				+SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0, 0, 4, 0)
				[
					SNew(SImage) .Image( FEditorStyle::GetBrush("ContentBrowser.PopupMessageIcon") )
				]

				+SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(InArgs._Message)
					.WrapTextAt(450)
				]
			]
		];
	}
	END_SLATE_FUNCTION_BUILD_OPTIMIZATION

	static void DisplayMessage( const FText& Message, const FSlateRect& ScreenAnchor, TSharedRef<SWidget> ParentContent )
	{
		TSharedRef<SContentBrowserPopup> PopupContent = SNew(SContentBrowserPopup) .Message(Message);

		const FVector2D ScreenLocation = FVector2D(ScreenAnchor.Left, ScreenAnchor.Top);
		const bool bFocusImmediately = true;
		const FVector2D SummonLocationSize = ScreenAnchor.GetSize();

		TSharedPtr<IMenu> Menu = FSlateApplication::Get().PushMenu(
			ParentContent,
			FWidgetPath(),
			PopupContent,
			ScreenLocation,
			FPopupTransitionEffect( FPopupTransitionEffect::TopMenu ),
			bFocusImmediately,
			SummonLocationSize);

		PopupContent->SetMenu(Menu);
	}

private:
	void SetMenu(const TSharedPtr<IMenu>& InMenu)
	{
		Menu = InMenu;
	}

	FReply OnBorderClicked(const FGeometry& Geometry, const FPointerEvent& MouseEvent)
	{
		if (Menu.IsValid())
		{
			Menu.Pin()->Dismiss();
		}

		return FReply::Handled();
	}

	FSlateColor GetBorderBackgroundColor() const
	{
		return IsHovered() ? FLinearColor(0.5, 0.5, 0.5, 1) : FLinearColor::White;
	}

private:
	TWeakPtr<IMenu> Menu;
};

/** A miniture confirmation popup for quick yes/no questions */
class SContentBrowserConfirmPopup :  public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS( SContentBrowserConfirmPopup ) {}
			
		/** The text to display */
		SLATE_ARGUMENT(FText, Prompt)

		/** The Yes Button to display */
		SLATE_ARGUMENT(FText, YesText)

		/** The No Button to display */
		SLATE_ARGUMENT(FText, NoText)

		/** Invoked when yes is clicked */
		SLATE_EVENT(FOnClicked, OnYesClicked)

		/** Invoked when no is clicked */
		SLATE_EVENT(FOnClicked, OnNoClicked)

	SLATE_END_ARGS()

	BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
	void Construct( const FArguments& InArgs )
	{
		OnYesClicked = InArgs._OnYesClicked;
		OnNoClicked = InArgs._OnNoClicked;

		ChildSlot
		[
			SNew(SBorder)
			. BorderImage(FEditorStyle::GetBrush("Menu.Background"))
			. Padding(10)
			[
				SNew(SVerticalBox)
				+SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 0, 0, 5)
				.HAlign(HAlign_Center)
				[
					SNew(STextBlock)
						.Text(InArgs._Prompt)
				]

				+SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Center)
				[
					SNew(SUniformGridPanel)
					.SlotPadding(3)
					+ SUniformGridPanel::Slot(0, 0)
					.HAlign(HAlign_Fill)
					[
						SNew(SButton)
						.HAlign(HAlign_Center)
						.Text(InArgs._YesText)
						.OnClicked( this, &SContentBrowserConfirmPopup::YesClicked )
					]

					+ SUniformGridPanel::Slot(1, 0)
					.HAlign(HAlign_Fill)
					[
						SNew(SButton)
						.HAlign(HAlign_Center)
						.Text(InArgs._NoText)
						.OnClicked( this, &SContentBrowserConfirmPopup::NoClicked )
					]
				]
			]
		];
	}
	END_SLATE_FUNCTION_BUILD_OPTIMIZATION

	/** Opens the popup using the specified component as its parent */
	void OpenPopup(const TSharedRef<SWidget>& ParentContent)
	{
		// Show dialog to confirm the delete
		Menu = FSlateApplication::Get().PushMenu(
			ParentContent,
			FWidgetPath(),
			SharedThis(this),
			FSlateApplication::Get().GetCursorPos(),
			FPopupTransitionEffect( FPopupTransitionEffect::TopMenu )
			);
	}

private:
	/** The yes button was clicked */
	FReply YesClicked()
	{
		if ( OnYesClicked.IsBound() )
		{
			OnYesClicked.Execute();
		}

		if (Menu.IsValid())
		{
			Menu.Pin()->Dismiss();
		}

		return FReply::Handled();
	}

	/** The no button was clicked */
	FReply NoClicked()
	{
		if ( OnNoClicked.IsBound() )
		{
			OnNoClicked.Execute();
		}

		if (Menu.IsValid())
		{
			Menu.Pin()->Dismiss();
		}

		return FReply::Handled();
	}

	/** The IMenu prepresenting this popup */
	TWeakPtr<IMenu> Menu;

	/** Delegates for button clicks */
	FOnClicked OnYesClicked;
	FOnClicked OnNoClicked;
};


void ContentBrowserUtils::DisplayMessage(const FText& Message, const FSlateRect& ScreenAnchor, const TSharedRef<SWidget>& ParentContent)
{
	SContentBrowserPopup::DisplayMessage(Message, ScreenAnchor, ParentContent);
}

void ContentBrowserUtils::DisplayConfirmationPopup(const FText& Message, const FText& YesString, const FText& NoString, const TSharedRef<SWidget>& ParentContent, const FOnClicked& OnYesClicked, const FOnClicked& OnNoClicked)
{
	TSharedRef<SContentBrowserConfirmPopup> Popup = 
		SNew(SContentBrowserConfirmPopup)
		.Prompt(Message)
		.YesText(YesString)
		.NoText(NoString)
		.OnYesClicked( OnYesClicked )
		.OnNoClicked( OnNoClicked );

	Popup->OpenPopup(ParentContent);
}

void ContentBrowserUtils::CopyItemReferencesToClipboard(const TArray<FContentBrowserItem>& ItemsToCopy)
{
	TArray<FContentBrowserItem> SortedItems = ItemsToCopy;
	SortedItems.Sort([](const FContentBrowserItem& One, const FContentBrowserItem& Two)
	{
		return One.GetVirtualPath().Compare(Two.GetVirtualPath()) < 0;
	});

	FString ClipboardText;
	for (const FContentBrowserItem& Item : SortedItems)
	{
		Item.AppendItemReference(ClipboardText);
	}

	FPlatformApplicationMisc::ClipboardCopy(*ClipboardText);
}

void ContentBrowserUtils::CopyFilePathsToClipboard(const TArray<FContentBrowserItem>& ItemsToCopy)
{
	TArray<FContentBrowserItem> SortedItems = ItemsToCopy;
	SortedItems.Sort([](const FContentBrowserItem& One, const FContentBrowserItem& Two)
	{
		return One.GetVirtualPath().Compare(Two.GetVirtualPath()) < 0;
	});

	FString ClipboardText;
	for (const FContentBrowserItem& Item : SortedItems)
	{
		if (ClipboardText.Len() > 0)
		{
			ClipboardText += LINE_TERMINATOR;
		}

		FString ItemFilename;
		if (Item.GetItemPhysicalPath(ItemFilename) && FPaths::FileExists(ItemFilename))
		{
			ClipboardText += FPaths::ConvertRelativePathToFull(ItemFilename);
		}
		else
		{
			// Add a message for when a user tries to copy the path to a file that doesn't exist on disk of the form
			// <ItemName>: No file on disk
			ClipboardText += FString::Printf(TEXT("%s: No file on disk"), *Item.GetDisplayName().ToString());
		}
	}

	FPlatformApplicationMisc::ClipboardCopy(*ClipboardText);
}

bool ContentBrowserUtils::IsItemDeveloperContent(const FContentBrowserItem& InItem)
{
	const FContentBrowserItemDataAttributeValue IsDeveloperAttributeValue = InItem.GetItemAttribute(ContentBrowserItemAttributes::ItemIsDeveloperContent);
	return IsDeveloperAttributeValue.IsValid() && IsDeveloperAttributeValue.GetValue<bool>();
}

bool ContentBrowserUtils::IsItemLocalizedContent(const FContentBrowserItem& InItem)
{
	const FContentBrowserItemDataAttributeValue IsLocalizedAttributeValue = InItem.GetItemAttribute(ContentBrowserItemAttributes::ItemIsLocalizedContent);
	return IsLocalizedAttributeValue.IsValid() && IsLocalizedAttributeValue.GetValue<bool>();
}

bool ContentBrowserUtils::IsItemEngineContent(const FContentBrowserItem& InItem)
{
	const FContentBrowserItemDataAttributeValue IsEngineAttributeValue = InItem.GetItemAttribute(ContentBrowserItemAttributes::ItemIsEngineContent);
	return IsEngineAttributeValue.IsValid() && IsEngineAttributeValue.GetValue<bool>();
}

bool ContentBrowserUtils::IsItemProjectContent(const FContentBrowserItem& InItem)
{
	const FContentBrowserItemDataAttributeValue IsProjectAttributeValue = InItem.GetItemAttribute(ContentBrowserItemAttributes::ItemIsProjectContent);
	return IsProjectAttributeValue.IsValid() && IsProjectAttributeValue.GetValue<bool>();
}

bool ContentBrowserUtils::IsItemPluginContent(const FContentBrowserItem& InItem)
{
	const FContentBrowserItemDataAttributeValue IsPluginAttributeValue = InItem.GetItemAttribute(ContentBrowserItemAttributes::ItemIsPluginContent);
	return IsPluginAttributeValue.IsValid() && IsPluginAttributeValue.GetValue<bool>();
}

bool ContentBrowserUtils::IsCollectionPath(const FString& InPath, FName* OutCollectionName, ECollectionShareType::Type* OutCollectionShareType)
{
	static const FString CollectionsRootPrefix = TEXT("/Collections");
	if (InPath.StartsWith(CollectionsRootPrefix))
	{
		TArray<FString> PathParts;
		InPath.ParseIntoArray(PathParts, TEXT("/"));
		check(PathParts.Num() > 2);

		// The second part of the path is the share type name
		if (OutCollectionShareType)
		{
			*OutCollectionShareType = ECollectionShareType::FromString(*PathParts[1]);
		}

		// The third part of the path is the collection name
		if (OutCollectionName)
		{
			*OutCollectionName = FName(*PathParts[2]);
		}

		return true;
	}
	return false;
}

void ContentBrowserUtils::CountPathTypes(const TArray<FString>& InPaths, int32& OutNumAssetPaths, int32& OutNumClassPaths)
{
	static const FString ClassesRootPrefix = TEXT("/Classes_");

	OutNumAssetPaths = 0;
	OutNumClassPaths = 0;

	for(const FString& Path : InPaths)
	{
		if(Path.StartsWith(ClassesRootPrefix))
		{
			++OutNumClassPaths;
		}
		else
		{
			++OutNumAssetPaths;
		}
	}
}

void ContentBrowserUtils::CountPathTypes(const TArray<FName>& InPaths, int32& OutNumAssetPaths, int32& OutNumClassPaths)
{
	static const FString ClassesRootPrefix = TEXT("/Classes_");

	OutNumAssetPaths = 0;
	OutNumClassPaths = 0;

	for(const FName& Path : InPaths)
	{
		if(Path.ToString().StartsWith(ClassesRootPrefix))
		{
			++OutNumClassPaths;
		}
		else
		{
			++OutNumAssetPaths;
		}
	}
}

void ContentBrowserUtils::CountItemTypes(const TArray<FAssetData>& InItems, int32& OutNumAssetItems, int32& OutNumClassItems)
{
	OutNumAssetItems = 0;
	OutNumClassItems = 0;

	for(const FAssetData& Item : InItems)
	{
		if(Item.AssetClass == NAME_Class)
		{
			++OutNumClassItems;
		}
		else
		{
			++OutNumAssetItems;
		}
	}
}

FText ContentBrowserUtils::GetExploreFolderText()
{
	FFormatNamedArguments Args;
	Args.Add(TEXT("FileManagerName"), FPlatformMisc::GetFileManagerName());
	return FText::Format(NSLOCTEXT("GenericPlatform", "ShowInFileManager", "Show in {FileManagerName}"), Args);
}

template <typename OutputContainerType>
void ConvertLegacySelectionToVirtualPathsImpl(TArrayView<const FAssetData> InAssets, TArrayView<const FString> InFolders, const bool InUseFolderPaths, OutputContainerType& OutVirtualPaths)
{
	OutVirtualPaths.Reset();
	if (InAssets.Num() == 0 && InFolders.Num() == 0)
	{
		return;
	}

	UContentBrowserDataSubsystem* ContentBrowserData = IContentBrowserDataModule::Get().GetSubsystem();

	auto AppendVirtualPath = [&OutVirtualPaths](FName InPath)
	{
		OutVirtualPaths.Add(InPath);
		return true;
	};

	for (const FAssetData& Asset : InAssets)
	{
		ContentBrowserData->Legacy_TryConvertAssetDataToVirtualPaths(Asset, InUseFolderPaths, AppendVirtualPath);
	}

	for (const FString& Folder : InFolders)
	{
		ContentBrowserData->Legacy_TryConvertPackagePathToVirtualPaths(*Folder, AppendVirtualPath);
	}
}

void ContentBrowserUtils::ConvertLegacySelectionToVirtualPaths(TArrayView<const FAssetData> InAssets, TArrayView<const FString> InFolders, const bool InUseFolderPaths, TArray<FName>& OutVirtualPaths)
{
	ConvertLegacySelectionToVirtualPathsImpl(InAssets, InFolders, InUseFolderPaths, OutVirtualPaths);
}

void ContentBrowserUtils::ConvertLegacySelectionToVirtualPaths(TArrayView<const FAssetData> InAssets, TArrayView<const FString> InFolders, const bool InUseFolderPaths, TSet<FName>& OutVirtualPaths)
{
	ConvertLegacySelectionToVirtualPathsImpl(InAssets, InFolders, InUseFolderPaths, OutVirtualPaths);
}

void ContentBrowserUtils::AppendAssetFilterToContentBrowserFilter(const FARFilter& InAssetFilter, const TSharedPtr<FBlacklistNames>& InAssetClassBlacklist, const TSharedPtr<FBlacklistPaths>& InFolderBlacklist, FContentBrowserDataFilter& OutDataFilter)
{
	if (InAssetFilter.ObjectPaths.Num() > 0 || InAssetFilter.TagsAndValues.Num() > 0 || InAssetFilter.bIncludeOnlyOnDiskAssets)
	{
		FContentBrowserDataObjectFilter& ObjectFilter = OutDataFilter.ExtraFilters.FindOrAddFilter<FContentBrowserDataObjectFilter>();
		ObjectFilter.ObjectNamesToInclude = InAssetFilter.ObjectPaths;
		ObjectFilter.TagsAndValuesToInclude = InAssetFilter.TagsAndValues;
		ObjectFilter.bOnDiskObjectsOnly = InAssetFilter.bIncludeOnlyOnDiskAssets;
	}

	if (InAssetFilter.PackageNames.Num() > 0 || InAssetFilter.PackagePaths.Num() > 0 || (InFolderBlacklist && InFolderBlacklist->HasFiltering()))
	{
		FContentBrowserDataPackageFilter& PackageFilter = OutDataFilter.ExtraFilters.FindOrAddFilter<FContentBrowserDataPackageFilter>();
		PackageFilter.PackageNamesToInclude = InAssetFilter.PackageNames;
		PackageFilter.PackagePathsToInclude = InAssetFilter.PackagePaths;
		PackageFilter.bRecursivePackagePathsToInclude = InAssetFilter.bRecursivePaths;
		PackageFilter.PathBlacklist = InFolderBlacklist;
	}

	if (InAssetFilter.ClassNames.Num() > 0 || (InAssetClassBlacklist && InAssetClassBlacklist->HasFiltering()))
	{
		FContentBrowserDataClassFilter& ClassFilter = OutDataFilter.ExtraFilters.FindOrAddFilter<FContentBrowserDataClassFilter>();
		ClassFilter.ClassNamesToInclude = InAssetFilter.ClassNames;
		ClassFilter.bRecursiveClassNamesToInclude = InAssetFilter.bRecursiveClasses;
		if (InAssetFilter.bRecursiveClasses)
		{
			ClassFilter.ClassNamesToExclude = InAssetFilter.RecursiveClassesExclusionSet.Array();
			ClassFilter.bRecursiveClassNamesToExclude = false;
		}
		ClassFilter.ClassBlacklist = InAssetClassBlacklist;
	}
}

bool ContentBrowserUtils::CanDeleteFromAssetView(TWeakPtr<SAssetView> AssetView)
{
	if (TSharedPtr<SAssetView> AssetViewPin = AssetView.Pin())
	{
		const TArray<FContentBrowserItem> SelectedItems = AssetViewPin->GetSelectedItems();

		bool bCanDelete = false;
		for (const FContentBrowserItem& SelectedItem : SelectedItems)
		{
			bCanDelete |= SelectedItem.CanDelete();
		}
		return bCanDelete;
	}
	return false;
}

bool ContentBrowserUtils::CanRenameFromAssetView(TWeakPtr<SAssetView> AssetView)
{
	if (TSharedPtr<SAssetView> AssetViewPin = AssetView.Pin())
	{
		const TArray<FContentBrowserItem> SelectedItems = AssetViewPin->GetSelectedItems();
		return SelectedItems.Num() == 1 && SelectedItems[0].CanRename(nullptr) && !AssetViewPin->IsThumbnailEditMode();
	}
	return false;
}

bool ContentBrowserUtils::CanDeleteFromPathView(TWeakPtr<SPathView> PathView)
{
	if (TSharedPtr<SPathView> PathViewPin = PathView.Pin())
	{
		const TArray<FContentBrowserItem> SelectedItems = PathViewPin->GetSelectedFolderItems();

		bool bCanDelete = false;
		for (const FContentBrowserItem& SelectedItem : SelectedItems)
		{
			bCanDelete |= SelectedItem.CanDelete();
		}
		return bCanDelete;
	}
	return false;
}

bool ContentBrowserUtils::CanRenameFromPathView(TWeakPtr<SPathView> PathView)
{
	if (TSharedPtr<SPathView> PathViewPin = PathView.Pin())
	{
		const TArray<FContentBrowserItem> SelectedItems = PathViewPin->GetSelectedFolderItems();
		return SelectedItems.Num() == 1 && SelectedItems[0].CanRename(nullptr);
	}
	return false;
}

bool ContentBrowserUtils::IsFavoriteFolder(const FString& FolderPath)
{
	return FContentBrowserSingleton::Get().FavoriteFolderPaths.Contains(FolderPath);
}

void ContentBrowserUtils::AddFavoriteFolder(const FString& FolderPath, bool bFlushConfig /*= true*/)
{
	FContentBrowserSingleton::Get().FavoriteFolderPaths.AddUnique(FolderPath);
}

void ContentBrowserUtils::RemoveFavoriteFolder(const FString& FolderPath, bool bFlushConfig /*= true*/)
{
	TArray<FString> FoldersToRemove;
	FoldersToRemove.Add(FolderPath);
	
	// Find and remove any subfolders
	for (const FString& FavoritePath : FContentBrowserSingleton::Get().FavoriteFolderPaths)
	{
		if (FavoritePath.StartsWith(FolderPath + TEXT("/")))
		{
			FoldersToRemove.Add(FavoritePath);
		}
	}
	for (const FString& FolderToRemove : FoldersToRemove)
	{
		FContentBrowserSingleton::Get().FavoriteFolderPaths.Remove(FolderToRemove);
	}
	if (bFlushConfig)
	{
		GConfig->Flush(false, GEditorPerProjectIni);
	}
}

const TArray<FString>& ContentBrowserUtils::GetFavoriteFolders()
{
	return FContentBrowserSingleton::Get().FavoriteFolderPaths;
}

#undef LOCTEXT_NAMESPACE
