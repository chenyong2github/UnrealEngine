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

	bool FilePickerDialog(const FString& Extensions, FString& OutFilename)
	{
		// First, display the file open dialog for selecting the file.
		TArray<FString> OpenFilenames;
		IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
		bool bOpen = false;
		if (DesktopPlatform)
		{
			FText PromptTitle = NSLOCTEXT("InterchangeUtilities_OpenFileDialog", "FilePickerDialog", "Select a file");
			bOpen = DesktopPlatform->OpenFileDialog(
				FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
				PromptTitle.ToString(),
				TEXT(""),
				TEXT(""),
				*Extensions,
				EFileDialogFlags::None,
				OpenFilenames
			);
		}
		FString SelectFilename;
		// Only continue if we pressed OK and have only one file selected.
		if (bOpen)
		{
			if (OpenFilenames.Num() >= 1)
			{
				OutFilename = OpenFilenames[0];
				return true;
			}
		}

		return false;
	}
} //ns UE::Interchange::Utilities::Private

bool UInterchangeFilePickerGeneric::FilePickerForTranslatorAssetType(const EInterchangeTranslatorAssetType TranslatorAssetType, FString& OutFilename)
{
	FString Extensions = UE::Interchange::Utilities::Private::GetOpenFileDialogExtensions(UInterchangeManager::GetInterchangeManager().GetSupportedAssetTypeFormats(TranslatorAssetType));
	return UE::Interchange::Utilities::Private::FilePickerDialog(Extensions, OutFilename);
}

bool UInterchangeFilePickerGeneric::FilePickerForTranslatorType(const EInterchangeTranslatorType TranslatorType, FString& OutFilename)
{
	FString Extensions = UE::Interchange::Utilities::Private::GetOpenFileDialogExtensions(UInterchangeManager::GetInterchangeManager().GetSupportedFormats(TranslatorType));
	return UE::Interchange::Utilities::Private::FilePickerDialog(Extensions, OutFilename);
}
