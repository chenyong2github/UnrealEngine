// Copyright Epic Games, Inc. All Rights Reserved. 
#include "InterchangeOpenFileDialog.h"

#include "Containers/UnrealString.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "InterchangeFilePickerBase.h"
#include "InterchangeManager.h"
#include "InterchangeTranslatorBase.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "Framework/Application/SlateApplication.h"

namespace UE::Interchange::Utilities::Private
{
	FString GetOpenFileDialogExtensions(const TArray<FString>& TranslatorFormats)
	{
		//Each format should be layout like this "fbx;Filmbox"
		if (TranslatorFormats.Num() == 0)
		{
			return FString();
		}
		TArray<FString> Extensions;
		TArray<FString> ExtensionDescriptions;
		Extensions.Reserve(TranslatorFormats.Num());
		ExtensionDescriptions.Reserve(TranslatorFormats.Num());
		for (const FString& Format : TranslatorFormats)
		{
			int32 DelimiterIndex;
			if (Format.FindChar(';', DelimiterIndex))
			{
				Extensions.Add(Format.Left(DelimiterIndex));
				ExtensionDescriptions.Add(Format.RightChop(DelimiterIndex + 1));
			}
		}
		const int32 ExtensionCount = Extensions.Num();
		if (ExtensionCount == 0)
		{
			return FString();
		}

		FString ExtensionStrAll = TEXT("All file extensions|");
		FString ExtensionStr;
		for (int32 ExtensionIndex = 0; ExtensionIndex < ExtensionCount; ++ExtensionIndex)
		{
			if (ExtensionIndex > 0)
			{
				ExtensionStrAll += TEXT(";*.");
			}
			else
			{
				ExtensionStrAll += TEXT("*.");
			}
			ExtensionStrAll += Extensions[ExtensionIndex];
			ExtensionStr += ExtensionDescriptions[ExtensionIndex] + TEXT("|*.") + Extensions[ExtensionIndex];
			if ((ExtensionIndex + 1) < ExtensionCount)
			{
				ExtensionStr += TEXT("|");
			}
		}
		ExtensionStr += TEXT("Any files|*.*");
		ExtensionStrAll += TEXT("|") + ExtensionStr;

		return ExtensionStrAll;
	}

	bool FilePickerDialog(const FString& Extensions, FInterchangeFilePickerParameters& Parameters, TArray<FString>& OutFilenames)
	{
		// First, display the file open dialog for selecting the file.
		IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
		if (DesktopPlatform)
		{
			FText PromptTitle = Parameters.Title.IsEmpty() ? NSLOCTEXT("InterchangeUtilities_OpenFileDialog", "FilePickerDialog", "Select a file") : Parameters.Title;

			const EFileDialogFlags::Type DialogFlags = Parameters.bAllowMultipleFiles ? EFileDialogFlags::Multiple : EFileDialogFlags::None;

			return DesktopPlatform->OpenFileDialog(
				FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
				PromptTitle.ToString(),
				Parameters.DefaultPath,
				TEXT(""),
				*Extensions,
				DialogFlags,
				OutFilenames
			);
		}

		return false;
	}
} //ns UE::Interchange::Utilities::Private

bool UInterchangeFilePickerGeneric::FilePickerForTranslatorAssetType(const EInterchangeTranslatorAssetType TranslatorAssetType, FInterchangeFilePickerParameters& Parameters, TArray<FString>& OutFilenames)
{
	FString Extensions = UE::Interchange::Utilities::Private::GetOpenFileDialogExtensions(UInterchangeManager::GetInterchangeManager().GetSupportedAssetTypeFormats(TranslatorAssetType));
	return UE::Interchange::Utilities::Private::FilePickerDialog(Extensions, Parameters, OutFilenames);
}

bool UInterchangeFilePickerGeneric::FilePickerForTranslatorType(const EInterchangeTranslatorType TranslatorType, FInterchangeFilePickerParameters& Parameters, TArray<FString>& OutFilenames)
{
	FString Extensions = UE::Interchange::Utilities::Private::GetOpenFileDialogExtensions(UInterchangeManager::GetInterchangeManager().GetSupportedFormats(TranslatorType));
	return UE::Interchange::Utilities::Private::FilePickerDialog(Extensions, Parameters, OutFilenames);
}
