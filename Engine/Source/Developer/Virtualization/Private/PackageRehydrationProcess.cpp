// Copyright Epic Games, Inc. All Rights Reserved.

#include "PackageRehydrationProcess.h"

#include "HAL/FileManager.h"
#include "Internationalization/Internationalization.h"
#include "Misc/PackageName.h"
#include "Misc/PackagePath.h"
#include "Misc/ScopedSlowTask.h"
#include "PackageUtils.h"
#include "UObject/Linker.h"
#include "UObject/Package.h"
#include "UObject/PackageTrailer.h"
#include "UObject/UObjectGlobals.h"
#include "Virtualization/VirtualizationSystem.h"

#define LOCTEXT_NAMESPACE "Virtualization"

namespace UE::Virtualization
{

void RehydratePackages(const TArray<FString>& Packages, TArray<FText>& OutErrors)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UE::Virtualization::RehydratePackages);

	IVirtualizationSystem& System = IVirtualizationSystem::Get();

	if (!System.IsEnabled())
	{
		return;
	}

	FScopedSlowTask Progress(1.0f, LOCTEXT("VAHydration_Task", "Re-hydrating Assets..."));
	Progress.MakeDialog();

	for (const FString& FilePath : Packages)
	{
		if (!FPackageName::IsPackageFilename(FilePath))
		{
			continue; // Only rehydrate valid packages
		}
			
		FPackageTrailer Trailer;
		if (!FPackageTrailer::TryLoadFromFile(FilePath, Trailer))
		{
			continue; // Only rehydrate packages with package trailers
		}

		TArray<FIoHash> VirtualizedPayloads = Trailer.GetPayloads(EPayloadStorageType::Virtualized);
		if (VirtualizedPayloads.IsEmpty())
		{
			continue; // If the package has no virtualized payloads then we can skip the rest
		}

		TUniquePtr<FArchive> PackageAr(IFileManager::Get().CreateFileReader(*FilePath));
		if (!PackageAr.IsValid())
		{
			FText Message = FText::Format(LOCTEXT("VAHydration_ReadFailed", "Unable to open the package '{0}' for reading"),
				FText::FromString(FilePath));
			OutErrors.Add(Message);

			return;
		}

		FPackageTrailerBuilder Builder = FPackageTrailerBuilder::CreateFromTrailer(Trailer, *PackageAr, FilePath);
		PackageAr.Reset();

		int32 PayloadsHydrated = 0;

		for (const FIoHash& Id : VirtualizedPayloads)
		{
			FCompressedBuffer Payload = System.PullData(Id);
			if (!Payload.IsNull())
			{
				if (Builder.UpdatePayloadAsLocal(Id, Payload))
				{
					PayloadsHydrated++;
				}
				else
				{
					FText Message = FText::Format(LOCTEXT("VAHydration_UpdateStatusFailed", "Unable to update the status for the payload '{0}' in the package '{1}'"),
						FText::FromString(LexToString(Id)),
						FText::FromString(FilePath));
					OutErrors.Add(Message);

					return;
				}
			}
			else
			{
				FText Message = FText::Format(LOCTEXT("VAHydration_PullFailed", "Unable to pull the data for the payload '{0}' for the package '{1}'"),
					FText::FromString(LexToString(Id)),
					FText::FromString(FilePath));
				OutErrors.Add(Message);

				return;
			}
		}

		if (PayloadsHydrated)
		{
			const FString NewPackagePath = DuplicatePackageWithNewTrailer(FilePath, Trailer, Builder, OutErrors);

			FString PackageName;
			if (FPackageName::TryConvertFilenameToLongPackageName(FilePath, PackageName))
			{
				UPackage* Package = FindObjectFast<UPackage>(nullptr, *PackageName);
				if (Package != nullptr)
				{
					UE_LOG(LogVirtualization, Verbose, TEXT("Detaching '%s' from disk so that it can be rehydrated"), *FilePath);
					ResetLoadersForSave(Package, *FilePath);
				}
			}

			if (CanWriteToFile(FilePath))                     
			{
				if (!IFileManager::Get().Move(*FilePath, *NewPackagePath))
				{
					FText Message = FText::Format(LOCTEXT("VAHydration_MoveFailed", "Unable to replace the package '{0}' with the hydrated version"),
						FText::FromString(FilePath));
					OutErrors.Add(Message);

					return;
				}
			}
			else
			{
				FText Message = FText::Format(
					LOCTEXT("VAHydration_PackageLocked", "The package file '{0}' has virtualized payloads but is locked for modification and cannot be hydrated"),
					FText::FromString(FilePath));

				OutErrors.Add(Message);
				return;
			}
		}
	}
}

} // namespace UE::Virtualization

#undef LOCTEXT_NAMESPACE
