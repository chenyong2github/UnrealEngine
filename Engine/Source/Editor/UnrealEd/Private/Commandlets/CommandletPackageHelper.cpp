// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================================================
 CommandletPackageHelper.cpp: Utility class that provides tools to handle packages & source control operations.
=============================================================================================================*/

#include "Commandlets/CommandletPackageHelper.h"
#include "Logging/LogMacros.h"
#include "UObject/Package.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "PackageTools.h"
#include "ISourceControlOperation.h"
#include "ISourceControlModule.h"
#include "ISourceControlState.h"
#include "ISourceControlProvider.h"
#include "SourceControlOperations.h"

DEFINE_LOG_CATEGORY_STATIC(LogCommandletPackageHelper, Log, All);

FCommandletPackageHelper::FCommandletPackageHelper()
{
}

FCommandletPackageHelper::~FCommandletPackageHelper()
{
	UPackage::WaitForAsyncFileWrites();
}

bool FCommandletPackageHelper::UseSourceControl() const
{
	return ISourceControlModule::Get().GetProvider().IsEnabled();
}

ISourceControlProvider& FCommandletPackageHelper::GetSourceControlProvider() const
{ 
	return ISourceControlModule::Get().GetProvider();
}

bool FCommandletPackageHelper::Delete(const FString& PackageName) const
{
	FString Filename = SourceControlHelpers::PackageFilename(PackageName);

	UE_LOG(LogCommandletPackageHelper, Verbose, TEXT("Deleting %s"), *Filename);
	
	if (!UseSourceControl())
	{
		if (!IPlatformFile::GetPlatformPhysical().SetReadOnly(*Filename, false) ||
			!IPlatformFile::GetPlatformPhysical().DeleteFile(*Filename))
		{
			UE_LOG(LogCommandletPackageHelper, Error, TEXT("Error deleting %s"), *Filename);
			return false;
		}
	}
	else
	{
		FSourceControlStatePtr SourceControlState = GetSourceControlProvider().GetState(Filename, EStateCacheUsage::ForceUpdate);

		if (SourceControlState.IsValid() && SourceControlState->IsSourceControlled())
		{
			FString OtherCheckedOutUser;
			if (SourceControlState->IsCheckedOutOther(&OtherCheckedOutUser))
			{
				UE_LOG(LogCommandletPackageHelper, Error, TEXT("Overwriting package %s already checked out by %s, will not submit"), *Filename, *OtherCheckedOutUser);
				return false;
			}
			else if (!SourceControlState->IsCurrent())
			{
				UE_LOG(LogCommandletPackageHelper, Error, TEXT("Overwriting package %s (not at head revision), will not submit"), *Filename);
				return false;
			}
			else if (SourceControlState->IsAdded())
			{
				if (GetSourceControlProvider().Execute(ISourceControlOperation::Create<FRevert>(), Filename) != ECommandResult::Succeeded)
				{
					UE_LOG(LogCommandletPackageHelper, Error, TEXT("Error reverting package %s from source control"), *Filename);
					return false;
				}
			}
			else
			{
				UE_LOG(LogCommandletPackageHelper, Log, TEXT("Deleting package %s from source control"), *Filename);

				if (SourceControlState->IsCheckedOut())
				{
					if (GetSourceControlProvider().Execute(ISourceControlOperation::Create<FRevert>(), Filename) != ECommandResult::Succeeded)
					{
						UE_LOG(LogCommandletPackageHelper, Error, TEXT("Error reverting package %s from source control"), *Filename);
						return false;
					}
				}

				if (GetSourceControlProvider().Execute(ISourceControlOperation::Create<FDelete>(), Filename) != ECommandResult::Succeeded)
				{
					UE_LOG(LogCommandletPackageHelper, Error, TEXT("Error deleting package %s from source control"), *Filename);
					return false;
				}
			}
		}
		else
		{
			if (!IFileManager::Get().Delete(*Filename, false, true))
			{
				UE_LOG(LogCommandletPackageHelper, Error, TEXT("Error deleting package %s locally"), *Filename);
				return false;
			}
		}
	}

	return true;
}

bool FCommandletPackageHelper::Delete(UPackage* Package) const
{
	TArray<UPackage*> Packages = { Package };
	return Delete(Packages);
}

bool FCommandletPackageHelper::Delete(const TArray<FAssetData>& Assets) const
{
	bool bAllPackagesDeleted = true;
	for (const FAssetData& AssetToDelete : Assets)
	{
		bAllPackagesDeleted &= Delete(AssetToDelete.PackageName.ToString());
	}

	return bAllPackagesDeleted;
}

bool FCommandletPackageHelper::Delete(const TArray<UPackage*>& Packages) const
{
	// Store all packages names as we won't be able to retrieve them from the UPackages once they are unloaded
	TArray<FString> PackagesNames;
	PackagesNames.Reserve(Packages.Num());

	for (UPackage* Package : Packages)
	{
		PackagesNames.Add(Package->GetName());

		// Must clear dirty flag befor unloading
		Package->SetDirtyFlag(false);
	}

	// Unload packages so we can delete them
	FText ErrorMessage;
	if (!UPackageTools::UnloadPackages(Packages, ErrorMessage))
	{
		UE_LOG(LogCommandletPackageHelper, Error, TEXT("Error unloading package: %s"), *ErrorMessage.ToString());
		return false;
	}

	bool bAllPackagesDeleted = true;
	for (const FString& PackageName : PackagesNames)
	{
		bAllPackagesDeleted &= Delete(PackageName);
	}

	return bAllPackagesDeleted;
}

bool FCommandletPackageHelper::AddToSourceControl(UPackage* Package) const
{
	if (UseSourceControl())
	{
		FString PackageFilename = SourceControlHelpers::PackageFilename(Package);
		FSourceControlStatePtr SourceControlState = GetSourceControlProvider().GetState(PackageFilename, EStateCacheUsage::ForceUpdate);

		if (SourceControlState.IsValid() && !SourceControlState->IsSourceControlled())
		{
			UE_LOG(LogCommandletPackageHelper, Log, TEXT("Adding package %s to source control"), *PackageFilename);
			if (GetSourceControlProvider().Execute(ISourceControlOperation::Create<FMarkForAdd>(), Package) != ECommandResult::Succeeded)
			{
				UE_LOG(LogCommandletPackageHelper, Error, TEXT("Error adding %s to source control."), *PackageFilename);
				return false;
			}
		}
	}

	return true;
}

bool FCommandletPackageHelper::Save(UPackage* Package) const
{
	FString PackageFileName = SourceControlHelpers::PackageFilename(Package);
	if (!UPackage::SavePackage(Package, nullptr, RF_Standalone, *PackageFileName, GError, nullptr, false, true, SAVE_Async))
	{
		UE_LOG(LogCommandletPackageHelper, Error, TEXT("Error saving %s"), *PackageFileName);
		return false;
	}

	return true;
}

bool FCommandletPackageHelper::Checkout(UPackage* Package) const
{
	if (UseSourceControl())
	{
		FString PackageFilename = SourceControlHelpers::PackageFilename(Package);
		FSourceControlStatePtr SourceControlState = GetSourceControlProvider().GetState(PackageFilename, EStateCacheUsage::ForceUpdate);

		if (SourceControlState.IsValid())
		{
			FString OtherCheckedOutUser;
			if (SourceControlState->IsCheckedOutOther(&OtherCheckedOutUser))
			{
				UE_LOG(LogCommandletPackageHelper, Error, TEXT("Overwriting package %s already checked out by %s, will not submit"), *PackageFilename, *OtherCheckedOutUser);
				return false;
			}
			else if (!SourceControlState->IsCurrent())
			{
				UE_LOG(LogCommandletPackageHelper, Error, TEXT("Overwriting package %s (not at head revision), will not submit"), *PackageFilename);
				return false;
			}
			else if (SourceControlState->IsCheckedOut() || SourceControlState->IsAdded())
			{
				UE_LOG(LogCommandletPackageHelper, Log, TEXT("Skipping package %s (already checked out)"), *PackageFilename);
				return true;
			}
			else if (SourceControlState->IsSourceControlled())
			{
				UE_LOG(LogCommandletPackageHelper, Log, TEXT("Checking out package %s from source control"), *PackageFilename);
				return GetSourceControlProvider().Execute(ISourceControlOperation::Create<FCheckOut>(), Package) == ECommandResult::Succeeded;
			}
		}
	}
	else
	{
		FString PackageFilename = SourceControlHelpers::PackageFilename(Package);
		if (IPlatformFile::GetPlatformPhysical().FileExists(*PackageFilename))
		{
			if (!IPlatformFile::GetPlatformPhysical().SetReadOnly(*PackageFilename, false))
			{
				UE_LOG(LogCommandletPackageHelper, Error, TEXT("Error setting %s writable"), *PackageFilename);
				return false;
			}
		}
	}

	return true;
}
