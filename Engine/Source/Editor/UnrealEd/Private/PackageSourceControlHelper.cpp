// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================================================
 CommandletPackageHelper.cpp: Utility class that provides tools to handle packages & source control operations.
=============================================================================================================*/

#include "PackageSourceControlHelper.h"
#include "Logging/LogMacros.h"
#include "UObject/Linker.h"
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

	TArray<FString> PackageNames;
	PackageNames.Reserve(Packages.Num());
	
	for (UPackage* Package : Packages)
	{
		PackageNames.Add(Package->GetName());
		ResetLoaders(Package);
	}

	return Delete(PackageNames);
}

bool FPackageSourceControlHelper::AddToSourceControl(UPackage* Package) const
{
	if (UseSourceControl())
	{
		FString PackageFilename = SourceControlHelpers::PackageFilename(Package);
		return AddToSourceControl({ PackageFilename });
	}

	return true;
}

bool FPackageSourceControlHelper::AddToSourceControl(const TArray<FString>& PackageNames) const
{
	if (!UseSourceControl())
	{
		return true;
	}

	// Convert package names to package filenames
	TArray<FString> PackageFilenames = SourceControlHelpers::PackageFilenames(PackageNames);

	// Two-pass checkout mechanism
	TArray<FString> PackagesToAdd;
	PackagesToAdd.Reserve(PackageFilenames.Num());
	bool bSuccess = true;
	
	TArray<FSourceControlStateRef> SourceControlStates;
	ECommandResult::Type UpdateState = GetSourceControlProvider().GetState(PackageFilenames, SourceControlStates, EStateCacheUsage::ForceUpdate);

	if (UpdateState != ECommandResult::Succeeded)
	{
		UE_LOG(LogCommandletPackageHelper, Error, TEXT("Could not get source control state for packages"));
		return false;
	}
	
	for (FSourceControlStateRef& SourceControlState : SourceControlStates)
	{
		const FString& PackageFilename = SourceControlState->GetFilename();

		FString OtherCheckedOutUser;
		if (SourceControlState->IsCheckedOutOther(&OtherCheckedOutUser))
		{
			UE_LOG(LogCommandletPackageHelper, Error, TEXT("Overwriting package %s already checked out by %s, will not add"), *PackageFilename, *OtherCheckedOutUser);
			bSuccess = false;
		}
		else if (!SourceControlState->IsCurrent())
		{
			UE_LOG(LogCommandletPackageHelper, Error, TEXT("Overwriting package %s (not at head revision), will not add"), *PackageFilename);
			bSuccess = false;
		}
		else if (SourceControlState->IsAdded())
		{
			// Nothing to do
		}
		else if (!SourceControlState->IsSourceControlled())
		{
			PackagesToAdd.Add(PackageFilename);
		}
	}

	// Any error up to here will be an early out
	if (!bSuccess)
	{
		return false;
	}

	if (PackagesToAdd.Num())
	{
		return (GetSourceControlProvider().Execute(ISourceControlOperation::Create<FMarkForAdd>(), PackagesToAdd) == ECommandResult::Succeeded);
	}

	return true;
}

bool FPackageSourceControlHelper::Checkout(UPackage* Package) const
{
	return !Package || Checkout({ Package->GetName() });
}

bool FPackageSourceControlHelper::Checkout(const TArray<FString>& PackageNames) const
{
	const bool bUseSourceControl = UseSourceControl();

	// Convert package names to package filenames
	TArray<FString> PackageFilenames = SourceControlHelpers::PackageFilenames(PackageNames);

	// Two-pass checkout mechanism
	TArray<FString> PackagesToCheckout;
	PackagesToCheckout.Reserve(PackageFilenames.Num());
	bool bSuccess = true;

	// In the first pass, we will gather the packages to be checked out, or flag errors and return if we've found any
	if (bUseSourceControl)
	{
		TArray<FSourceControlStateRef> SourceControlStates;
		ECommandResult::Type UpdateState = GetSourceControlProvider().GetState(PackageFilenames, SourceControlStates, EStateCacheUsage::ForceUpdate);

		if (UpdateState != ECommandResult::Succeeded)
		{
			UE_LOG(LogCommandletPackageHelper, Error, TEXT("Could not get source control state for packages"));
			return false;
		}

		for(FSourceControlStateRef& SourceControlState : SourceControlStates)
		{
			const FString& PackageFilename = SourceControlState->GetFilename();

			FString OtherCheckedOutUser;
			if (SourceControlState->IsCheckedOutOther(&OtherCheckedOutUser))
			{
				UE_LOG(LogCommandletPackageHelper, Error, TEXT("Overwriting package %s already checked out by %s, will not checkout"), *PackageFilename, *OtherCheckedOutUser);
				bSuccess = false;
			}
			else if (!SourceControlState->IsCurrent())
			{
				UE_LOG(LogCommandletPackageHelper, Error, TEXT("Overwriting package %s (not at head revision), will not checkout"), *PackageFilename);
				bSuccess = false;
			}
			else if (SourceControlState->IsCheckedOut() || SourceControlState->IsAdded())
			{
				// Nothing to do
			}
			else if (SourceControlState->IsSourceControlled())
			{
				PackagesToCheckout.Add(PackageFilename);
			}
		}
	}
	else
	{
		for (const FString& PackageFilename : PackageFilenames)
		{
			if (!IPlatformFile::GetPlatformPhysical().FileExists(*PackageFilename))
			{
				UE_LOG(LogCommandletPackageHelper, Error, TEXT("File %s cannot be checked out as it does not exist"), *PackageFilename);
				bSuccess = false;
			}
			else if (IPlatformFile::GetPlatformPhysical().IsReadOnly(*PackageFilename))
			{
				PackagesToCheckout.Add(PackageFilename);
			}
		}
	}

	// Any error up to here will be an early out
	if (!bSuccess)
	{
		return false;
	}

	// In the second pass, we will perform the checkout operation
	if (PackagesToCheckout.Num() == 0)
	{
		return true;
	}
	else if (bUseSourceControl)
	{
		return GetSourceControlProvider().Execute(ISourceControlOperation::Create<FCheckOut>(), PackagesToCheckout) == ECommandResult::Succeeded;
	}
	else
	{
		int PackageIndex = 0;

		for (; PackageIndex < PackagesToCheckout.Num(); ++PackageIndex)
		{
			if (!IPlatformFile::GetPlatformPhysical().SetReadOnly(*PackagesToCheckout[PackageIndex], false))
			{
				UE_LOG(LogCommandletPackageHelper, Error, TEXT("Error setting %s writable"), *PackagesToCheckout[PackageIndex]);
				bSuccess = false;
				--PackageIndex;
				break;
			}
		}

		// If a file couldn't be made writeable, put back the files to their original state
		if (!bSuccess)
		{
			for (; PackageIndex >= 0; --PackageIndex)
			{
				IPlatformFile::GetPlatformPhysical().SetReadOnly(*PackagesToCheckout[PackageIndex], true);
			}
		}

		return bSuccess;
	}
}