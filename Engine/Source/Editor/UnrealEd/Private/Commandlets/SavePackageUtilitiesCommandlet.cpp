// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commandlets/SavePackageUtilitiesCommandlet.h"

#include "Editor.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Misc/CoreMisc.h"
#include "Misc/FeedbackContext.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/LinkerDiff.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectHash.h"

USavePackageUtilitiesCommandlet::USavePackageUtilitiesCommandlet(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

int32 USavePackageUtilitiesCommandlet::Main(const FString& Params)
{
	InitParameters(Params);

	auto GetPackageAsset = [](UPackage* InPackage) -> UObject*
	{
		UObject* Asset = nullptr;
		ForEachObjectWithPackage(InPackage, [&Asset](UObject* Object)
			{
				if (Object->IsAsset())
				{
					Asset = Object;
					return false;
				}
				return true;
			}, /*bIncludeNestedObjects*/ false);
		return Asset;
	};

	for (const FString& PackageName : PackageNames)
	{
		// Load Package
		UPackage* Package = LoadPackage(nullptr, *PackageName, LOAD_None);
		UObject* Asset = GetPackageAsset(Package);
		FString Filename = FPaths::CreateTempFilename(*FPaths::ProjectSavedDir());

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public;
		SaveArgs.SaveFlags = SAVE_CompareLinker;
		SaveArgs.TargetPlatform = TargetPlatform;

		// if not cooking add RF_Standalone to the top level flags
		if (TargetPlatform == nullptr)
		{
			SaveArgs.TopLevelFlags |= RF_Standalone;
		}

		static IConsoleVariable* EnableNewSave = IConsoleManager::Get().FindConsoleVariable(TEXT("SavePackage.EnableNewSave"));
		int32 EnableNewSavePreviousValue = EnableNewSave->GetInt();

		// Do the new save package first in case, number of serialization has a by product during saving
		// New Save Package
		FSavePackageResultStruct NewResult;
		{
			EnableNewSave->Set(1);
			NewResult = GEditor->Save(Package, Asset, SaveArgs.TopLevelFlags, *Filename,
				GError, nullptr, false, true, SaveArgs.SaveFlags, SaveArgs.TargetPlatform,
				FDateTime::MinValue(), false, /*DiffMap*/ nullptr,
				nullptr);

		}

		// Old Save Package
		FSavePackageResultStruct OldResult;
		{
			EnableNewSave->Set(0);
			OldResult = GEditor->Save(Package, Asset, SaveArgs.TopLevelFlags, *Filename,
				GError, nullptr, false, true, SaveArgs.SaveFlags, SaveArgs.TargetPlatform,
				FDateTime::MinValue(), false, /*DiffMap*/ nullptr,
				nullptr);
		}
		EnableNewSave->Set(EnableNewSavePreviousValue);

		if (OldResult.LinkerSave && NewResult.LinkerSave)
		{
			// Compare Linker Save info
			FLinkerDiff LinkerDiff = FLinkerDiff::CompareLinkers(OldResult.LinkerSave.Get(), NewResult.LinkerSave.Get());
			LinkerDiff.PrintDiff(*GWarn);
		}
		
		// Delete the temp filename
		IFileManager::Get().Delete(*Filename);
	}

	return 0;
}

void USavePackageUtilitiesCommandlet::InitParameters(const FString& Params)
{
	TArray<FString> Tokens;
	TArray<FString> Switches;
	ParseCommandLine(*Params, Tokens, Switches);

	FString SwitchValue;
	for (const FString& CurrentSwitch : Switches)
	{
		if (FParse::Value(*CurrentSwitch, TEXT("PACKAGE="), SwitchValue))
		{
			FString LongPackageName;
			FPackageName::SearchForPackageOnDisk(SwitchValue, &LongPackageName, nullptr);
			PackageNames.Add(MoveTemp(LongPackageName));

		}
		else if (FParse::Value(*CurrentSwitch, TEXT("PACKAGEFOLDER="), SwitchValue))
		{
			FPackageName::IteratePackagesInDirectory(SwitchValue, [this](const TCHAR* Filename)
				{
					PackageNames.Add(FPackageName::FilenameToLongPackageName(Filename));
					return true;
				});
		}
		else if (FParse::Value(*CurrentSwitch, TEXT("CookPlatform="), SwitchValue))
		{
			if (ITargetPlatformManagerModule* TPM = GetTargetPlatformManager())
			{
				TargetPlatform = TPM->FindTargetPlatform(SwitchValue);
				if (TargetPlatform == nullptr)
				{
					// @todo Error
				}

			}
			

		}
	}
}

