// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================================================
 CommandletPackageHelper.cpp: Utility class that provides tools to handle packages & source control operations.
=============================================================================================================*/

#include "PackageSourceControlHelper.h"
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

bool FPackageSourceControlHelper::UseSourceControl() const
{
	return GetSourceControlProvider().IsEnabled();
}

ISourceControlProvider& FPackageSourceControlHelper::GetSourceControlProvider() const
{ 
	return ISourceControlModule::Get().GetProvider();
}

bool FPackageSourceControlHelper::Delete(const FString& PackageName) const
{
	TArray<FString> PackageNames = { PackageName };
	return Delete(PackageNames);
}

bool FPackageSourceControlHelper::Delete(const TArray<FString>& PackageNames) const
{
	// Early out when not using source control
	if (!UseSourceControl())
	{
		for (const FString& PackageName : PackageNames)
		{
			FString Filename = SourceControlHelpers::PackageFilename(PackageName);

			if (!IPlatformFile::GetPlatformPhysical().SetReadOnly(*Filename, false) ||
				!IPlatformFile::GetPlatformPhysical().DeleteFile(*Filename))
			{
				UE_LOG(LogCommandletPackageHelper, Error, TEXT("Error deleting %s"), *Filename);
				return false;
			}
		}

		return true;
	}

	TArray<FString> FilesToRevert;
	TArray<FString> FilesToDeleteFromDisk;
	TArray<FString> FilesToDeleteFromSCC;

	bool bSCCErrorsFound = false;

	// First: get latest state from source control
	TArray<FString> Filenames;
	TArray<FSourceControlStateRef> SourceControlStates;
	Filenames.Reset(PackageNames.Num());

	for (const FString& PackageName : PackageNames)
	{
		Filenames.Emplace(SourceControlHelpers::PackageFilename(PackageName));
	}

	ECommandResult::Type UpdateState = GetSourceControlProvider().GetState(Filenames, SourceControlStates, EStateCacheUsage::ForceUpdate);

	if (UpdateState != ECommandResult::Succeeded)
	{
		UE_LOG(LogCommandletPackageHelper, Error, TEXT("Could not get source control state for packages"));
		return false;
	}

	for(FSourceControlStateRef& SourceControlState : SourceControlStates)
	{
		const FString& Filename = SourceControlState->GetFilename();

		UE_LOG(LogCommandletPackageHelper, Verbose, TEXT("Deleting %s"), *Filename);

		if (SourceControlState->IsSourceControlled())
		{
			FString OtherCheckedOutUser;
			if (SourceControlState->IsCheckedOutOther(&OtherCheckedOutUser))
			{
				UE_LOG(LogCommandletPackageHelper, Error, TEXT("Overwriting package %s already checked out by %s, will not submit"), *Filename, *OtherCheckedOutUser);
				bSCCErrorsFound = true;
			}
			else if (!SourceControlState->IsCurrent())
			{
				UE_LOG(LogCommandletPackageHelper, Error, TEXT("Overwriting package %s (not at head revision), will not submit"), *Filename);
				bSCCErrorsFound = true;
			}
			else if (SourceControlState->IsAdded())
			{
				FilesToRevert.Add(Filename);
				FilesToDeleteFromDisk.Add(Filename);
			}
			else
			{
				if (SourceControlState->IsCheckedOut())
				{
					FilesToRevert.Add(Filename);
				}

				FilesToDeleteFromSCC.Add(Filename);
			}
		}
		else
		{
			FilesToDeleteFromDisk.Add(Filename);
		}
	}

	if (bSCCErrorsFound)
	{
		// Errors were found, we'll cancel everything
		return false;
	}

	// It's possible that not all files were in the source control cache, in which case we should still add them to the
	// files to delete on disk.
	if (Filenames.Num() != SourceControlStates.Num())
	{
		for (FSourceControlStateRef& SourceControlState : SourceControlStates)
		{
			Filenames.Remove(SourceControlState->GetFilename());
		}

		FilesToDeleteFromDisk.Append(Filenames);
	}

	// First, revert files from SCC
	if (FilesToRevert.Num() > 0)
	{
		if (GetSourceControlProvider().Execute(ISourceControlOperation::Create<FRevert>(), FilesToRevert) != ECommandResult::Succeeded)
		{
			UE_LOG(LogCommandletPackageHelper, Error, TEXT("Error reverting packages from source control"));
			return false;
		}
	}

	// Then delete files from SCC
	if (FilesToDeleteFromSCC.Num() > 0)
	{
		if (GetSourceControlProvider().Execute(ISourceControlOperation::Create<FDelete>(), FilesToDeleteFromSCC) != ECommandResult::Succeeded)
		{
			UE_LOG(LogCommandletPackageHelper, Error, TEXT("Error deleting packages from source control"));
			return false;
		}
	}

	// Then delete files on disk
	bool bDeleteOnDiskOk = true;
	for (const FString& Filename : FilesToDeleteFromDisk)
	{
		if (!IFileManager::Get().Delete(*Filename, false, true))
		{
			UE_LOG(LogCommandletPackageHelper, Error, TEXT("Error deleting package %s locally"), *Filename);
			bDeleteOnDiskOk = false;
		}
	}

	return bDeleteOnDiskOk;
}

bool FPackageSourceControlHelper::Delete(UPackage* Package) const
{
	TArray<UPackage*> Packages = { Package };
	return Delete(Packages);
}

bool FPackageSourceControlHelper::Delete(const TArray<UPackage*>& Packages) const
{
	if (Packages.IsEmpty())
	{
		return true;
	}

	// Store all packages names as we won't be able to retrieve them from the UPackages once they are unloaded
	TArray<FString> PackagesNames;
	PackagesNames.Reserve(Packages.Num());

	for (UPackage* Package : Packages)
	{
		PackagesNames.Add(Package->GetName());

		// Must clear dirty flag before unloading
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

bool FPackageSourceControlHelper::AddToSourceControl(UPackage* Package) const
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

bool FPackageSourceControlHelper::Checkout(UPackage* Package) const
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
		if (IPlatformFile::GetPlatformPhysical().IsReadOnly(*PackageFilename))
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
