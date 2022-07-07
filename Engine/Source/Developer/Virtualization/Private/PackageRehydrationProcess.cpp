// Copyright Epic Games, Inc. All Rights Reserved.

#include "PackageRehydrationProcess.h"

#include "HAL/FileManager.h"
#include "Internationalization/Internationalization.h"
#include "Misc/PackageName.h"
#include "Misc/PackagePath.h"
#include "PackageUtils.h"
#include "UObject/Linker.h"
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
			FText Message = FText::Format(LOCTEXT("VAHydration_REadFailed", "Unable to open the package '{0}' for reading"),
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

			// TODO: Reset loaders here if we ever expose this path to be run from the editor

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
				// Technically the package could have local payloads that won't be virtualized due to filtering or min payload sizes and so the
				// following warning is misleading. This will be solved if we move that evaluation to the point of saving a package.
				// If not then we probably need to extend QueryPayloadStatuses to test filtering etc as well, then check for potential package
				// modification after that.
				// Long term, the stand alone tool should be able to request the UnrealEditor relinquish the lock on the package file so this becomes 
				// less of a problem.
				FText Message = FText::Format(
					LOCTEXT("VAHydration_PackageLocked", "The package file '{0}' has virtualized payloads but is locked for modification and cannot be hydrated, this package will be skipped!"),
					FText::FromString(FilePath));

				UE_LOG(LogVirtualization, Warning, TEXT("%s"), *Message.ToString());
			}
		}
	}
}

} // namespace UE::Virtualization

#undef LOCTEXT_NAMESPACE
