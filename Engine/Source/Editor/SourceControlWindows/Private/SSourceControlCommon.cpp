// Copyright Epic Games, Inc. All Rights Reserved.

#include "SSourceControlCommon.h"

#include "AssetData.h"
#include "AssetToolsModule.h"
#include "EditorStyleSet.h"
#include "SourceControlHelpers.h"

#include "Widgets/SOverlay.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Images/SLayeredImage.h"
#include "Widgets/Layout/SBox.h"

#include "Engine/Level.h"

#define LOCTEXT_NAMESPACE "SourceControlChangelist"

//////////////////////////////////////////////////////////////////////////
FChangelistTreeItemPtr IChangelistTreeItem::GetParent() const
{
	return Parent;
}

const TArray<FChangelistTreeItemPtr>& IChangelistTreeItem::GetChildren() const
{
	return Children;
}

void IChangelistTreeItem::AddChild(FChangelistTreeItemRef Child)
{
	Child->Parent = AsShared();
	Children.Add(MoveTemp(Child));
}

void IChangelistTreeItem::RemoveChild(const FChangelistTreeItemRef& Child)
{
	if (Children.Remove(Child))
	{
		Child->Parent = nullptr;
	}
}

//////////////////////////////////////////////////////////////////////////
FText FShelvedChangelistTreeItem::GetDisplayText() const
{
	return LOCTEXT("SourceControl_ShelvedFiles", "Shelved Items");
}

//////////////////////////////////////////////////////////////////////////
FFileTreeItem::FFileTreeItem(FSourceControlStateRef InFileState, bool bBeautifyPaths, bool bIsShelvedFile)
	: FileState(InFileState)
{
	Type = (bIsShelvedFile ? IChangelistTreeItem::ShelvedFile : IChangelistTreeItem::File);
	CheckBoxState = ECheckBoxState::Checked;

	// Initialize asset data first
	FString Filename = FileState->GetFilename();

	if (!FileState->IsDeleted())
	{
		USourceControlHelpers::GetAssetData(Filename, Assets);
	}

	// For deleted items, the file is not on disk anymore so the only way we can get the asset data is by getting the file from the depot.
	// For shelved files, if the file still exists locally, it will have been found before, otherwise, the history of the shelved file state will point to the remote version
	if (bBeautifyPaths && (FileState->IsDeleted() || (bIsShelvedFile && Assets.Num() == 0)))
	{
		// At the moment, getting the asset data from non-external assets yields issues with the package path
		// so we will fall down to our recovery (below) instead
		if (Filename.Contains(ULevel::GetExternalActorsFolderName()))
		{
			const int64 MaxFetchSize = (1 << 20); // 1MB
			// In the case of shelved "marked for delete", we'll piggy back on the non-shelved file
			if (bIsShelvedFile && FileState->IsDeleted())
			{
				USourceControlHelpers::GetAssetDataFromFileHistory(Filename, Assets, nullptr, MaxFetchSize);
			}
			else
			{
				USourceControlHelpers::GetAssetDataFromFileHistory(FileState, Assets, nullptr, MaxFetchSize);
			}
		}
	}

	// Initialize display-related members
	FString TempAssetName = SSourceControlCommon::GetDefaultAssetName().ToString();
	FString TempAssetPath = Filename;
	FString TempAssetType = SSourceControlCommon::GetDefaultAssetType().ToString();
	FString TempPackageName = Filename;
	FColor TempAssetColor = FColor(		// Copied from ContentBrowserCLR.cpp
		127 + FColor::Red.R / 2,	// Desaturate the colors a bit (GB colors were too.. much)
		127 + FColor::Red.G / 2,
		127 + FColor::Red.B / 2,
		200); // Opacity

	if (Assets.Num() > 0)
	{
		TempAssetPath = Assets[0].ObjectPath.ToString();

		// Strip asset name from object path
		int32 LastDot = -1;
		if (TempAssetPath.FindLastChar('.', LastDot))
		{
			TempAssetPath.LeftInline(LastDot);
		}

		// Find name, asset type & color only if there is exactly one asset
		if (Assets.Num() == 1)
		{
			static FName NAME_ActorLabel(TEXT("ActorLabel"));
			if (Assets[0].FindTag(NAME_ActorLabel))
			{
				Assets[0].GetTagValue(NAME_ActorLabel, TempAssetName);
			}
			else
			{
				TempAssetName = Assets[0].AssetName.ToString();
			}

			TempAssetType = Assets[0].AssetClass.ToString();

			const FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
			const TSharedPtr<IAssetTypeActions> AssetTypeActions = AssetToolsModule.Get().GetAssetTypeActionsForClass(Assets[0].GetClass()).Pin();
			if (AssetTypeActions.IsValid())
			{
				TempAssetColor = AssetTypeActions->GetTypeColor();
			}
			else
			{
				TempAssetColor = FColor::White;
			}
		}
		else
		{
			TempAssetType = SSourceControlCommon::GetDefaultMultipleAsset().ToString();
			TempAssetColor = FColor::White;
		}

		// Beautify the package name
		TempPackageName = TempAssetPath + "." + TempAssetName;
	}
	else if (FPackageName::TryConvertFilenameToLongPackageName(Filename, TempPackageName))
	{
		// Fake asset name, asset path from the package name
		TempAssetPath = TempPackageName;

		int32 LastSlash = -1;
		if (TempPackageName.FindLastChar('/', LastSlash))
		{
			TempAssetName = TempPackageName;
			TempAssetName.RightChopInline(LastSlash + 1);
		}
	}
	else
	{
		TempAssetName = FPaths::GetCleanFilename(Filename);
		TempPackageName = Filename; // put back original package name if the try failed
		TempAssetType = FText::Format(SSourceControlCommon::GetDefaultUnknownAssetType(), FText::FromString(FPaths::GetExtension(Filename).ToUpper())).ToString();
	}

	// Finally, assign the temp variables to the member variables
	AssetName = FText::FromString(TempAssetName);
	AssetPath = FText::FromString(TempAssetPath);
	AssetType = FText::FromString(TempAssetType);
	AssetTypeColor = TempAssetColor;
	PackageName = FText::FromString(TempPackageName);
}

//////////////////////////////////////////////////////////////////////////

FOfflineFileTreeItem::FOfflineFileTreeItem(const FString& InFilename)
	: Assets()
	, PackageName(FText::FromString(InFilename)) 
	, AssetName(SSourceControlCommon::GetDefaultAssetName())
	, AssetPath()
	, AssetType(SSourceControlCommon::GetDefaultAssetType())
	, AssetTypeColor()
{
	Type = IChangelistTreeItem::OfflineFile;
	FString TempString;

	USourceControlHelpers::GetAssetData(InFilename, Assets);

	if (Assets.Num() > 0)
	{
		const FAssetData& AssetData = Assets[0];
		AssetPath = FText::FromName(AssetData.ObjectPath);

		// Find name, asset type & color only if there is exactly one asset
		if (Assets.Num() == 1)
		{
			static FName NAME_ActorLabel(TEXT("ActorLabel"));
			if (AssetData.FindTag(NAME_ActorLabel))
			{
				AssetData.GetTagValue(NAME_ActorLabel, AssetName);
			}
			else
			{
				AssetName = FText::FromName(AssetData.AssetName);
			}

			AssetType = FText::FromName(AssetData.AssetClass);

			const FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
			const TSharedPtr<IAssetTypeActions> AssetTypeActions = AssetToolsModule.Get().GetAssetTypeActionsForClass(AssetData.GetClass()).Pin();
			if (AssetTypeActions.IsValid())
			{
				AssetTypeColor = AssetTypeActions->GetTypeColor();
			}
			else
			{
				AssetTypeColor = FColor::White;
			}
		}
		else
		{
			AssetType = SSourceControlCommon::GetDefaultMultipleAsset();
			AssetTypeColor = FColor::White;
		}

		// Beautify the package name
		PackageName = AssetPath;
	}
	else if (FPackageName::TryConvertFilenameToLongPackageName(InFilename, TempString))
	{
		PackageName = FText::FromString(TempString);
		// Fake asset name, asset path from the package name
		AssetPath = PackageName;
	}
	else
	{
		AssetName = FText::FromString(FPaths::GetCleanFilename(InFilename));
		AssetType = FText::Format(SSourceControlCommon::GetDefaultUnknownAssetType(), FText::FromString(FPaths::GetExtension(InFilename).ToUpper()));
	}
}

//////////////////////////////////////////////////////////////////////////
namespace SSourceControlCommon
{

TSharedRef<SWidget> GetSCCFileWidget(FSourceControlStateRef InFileState, bool bIsShelvedFile)
{
	const FSlateBrush* IconBrush = FEditorStyle::GetBrush("ContentBrowser.ColumnViewAssetIcon");

	// Make icon overlays (eg, SCC and dirty status) a reasonable size in relation to the icon size (note: it is assumed this icon is square)
	const float ICON_SCALING_FACTOR = 0.7f;
	const float IconOverlaySize = IconBrush->ImageSize.X * ICON_SCALING_FACTOR;

	return SNew(SOverlay)
		// The actual icon
		+ SOverlay::Slot()
		[
			SNew(SImage)
			.Image(IconBrush)
			.ColorAndOpacity_Lambda([bIsShelvedFile]() -> FSlateColor {
				return FSlateColor(bIsShelvedFile ? FColor::Yellow : FColor::White);
			})
		]
		// Source control state
		+ SOverlay::Slot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Top)
		[
			SNew(SBox)
			.WidthOverride(IconOverlaySize)
			.HeightOverride(IconOverlaySize)
			[
				SNew(SLayeredImage, InFileState->GetIcon())
			]
		];
}

FText GetDefaultAssetName()
{
	return LOCTEXT("SourceControl_DefaultAssetName", "Unavailable");
}

FText GetDefaultAssetType()
{
	return LOCTEXT("SourceControl_DefaultAssetType", "Unknown");
}

FText GetDefaultUnknownAssetType()
{
	return LOCTEXT("SourceControl_FileTypeDefault", "{0} File");
}

FText GetDefaultMultipleAsset()
{
	return LOCTEXT("SourceCOntrol_ManyAssetType", "Multiple Assets");
}

} // end of namespace SSourceControlCommon

#undef LOCTEXT_NAMESPACE