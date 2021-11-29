// Copyright Epic Games, Inc. All Rights Reserved.

#include "PackageSubmissionChecks.h"

#include "Containers/UnrealString.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Internationalization/Internationalization.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/VirtualizedBulkData.h"
#include "UObject/PackageResourceManager.h"
#include "UObject/PackageTrailer.h"
#include "Virtualization/VirtualizationSystem.h"

#define LOCTEXT_NAMESPACE "Virtualization"

#define UE_USE_LEGACY_SUBMIT 0
#define UE_VALIDATE_TRUNCATED_PACKAGE 1

namespace UE::Virtualization
{

#if UE_USE_LEGACY_SUBMIT
void OnPrePackageSubmission(const TArray<FString>& FilesToSubmit, TArray<FText>& DescriptionTags, TArray<FText>& Errors)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UE::Virtualization::OnPrePackageSubmission);

	IVirtualizationSystem& System = IVirtualizationSystem::Get();

	// TODO: We could check to see if the package is virtualized even if it is disabled for the project
	// as a safety feature?
	if (!System.IsEnabled())
	{
		return;
	}

	// Other systems may have added errors to this array, we need to check so later we can determine if this function added any additional errors.
	const int32 NumErrors = Errors.Num();

	for (const FString& AbsoluteFilePath : FilesToSubmit)
	{
		const FPackagePath PackagePath = FPackagePath::FromLocalPath(AbsoluteFilePath);

		if (FPackageName::IsPackageExtension(PackagePath.GetHeaderExtension()) || FPackageName::IsTextPackageExtension(PackagePath.GetHeaderExtension()))
		{
			TArray<FPayloadId> PayloadsInPackage;
			if (FindPayloadsInPackageFile(PackagePath, EPayloadFilter::Virtualized, PayloadsInPackage))
			{
				for (const FPayloadId& PayloadId : PayloadsInPackage)
				{
					FCompressedBuffer Payload = System.PullData(PayloadId);

					if (Payload)
					{
						if (!System.PushData(PayloadId, Payload, EStorageType::Persistent, PackagePath))
						{
							FText Message = FText::Format(LOCTEXT("Virtualization_PushFailure", "Failed to push payload '{0}' from the package '{1}'"),
								FText::FromString(PayloadId.ToString()),
								FText::FromString(PackagePath.GetDebugName()));

							Errors.Add(Message);
						}
					}
					else
					{
						FText Message = FText::Format(LOCTEXT("Virtualization_MissingPayload", "Unable to find the payload '{0}' for the package '{1}' in local storage"),
							FText::FromString(PayloadId.ToString()),
							FText::FromString(PackagePath.GetDebugName()));

						Errors.Add(Message);
					}
				}
			}
			else
			{
				FText Message = FText::Format(LOCTEXT("Virtualization_PkgParseFailed", "Unable to find info about the payload requirements for the package '{0}'"),
					FText::FromString(PackagePath.GetDebugName()));

				Errors.Add(Message);
			}
		}
	}

	// If we had no new errors add the validation tag to indicate that the packages are safe for submission. 
	// TODO: Currently this is a hard coded guid, it should be replaced by something unique to the FilesToSubmit collection that could be 
	// validated by the source control server. This functionality is currently pending.
	if (NumErrors == Errors.Num())
	{
		FText Tag = FText::FromString(TEXT("#virtualization c9c748e7-d00b-45af-bb61-9054abba503a"));
		DescriptionTags.Add(Tag);	
	}
}
#else

/** 
 * Creates a copy of the given package but the copy will not include the FPackageTrailer.
 * 
 * @param[in]	PackagePath	The path of the package to copy
 * @param[in]	CopyPath	The path where the copy should be created
 * @param[in]	Trailer		The trailer found in 'PackagePath' that is already loaded
 * @param[out]	Errors		Errors created by the function will be added here
 * 
 * @return Returns true if the package was copied correctly, false otherwise. Note even when returning false a file might have been created at 'CopyPath'
 */
bool TryCopyPackageWithoutTrailer(const FPackagePath PackagePath, const FString& CopyPath, const FPackageTrailer& Trailer, TArray<FText>& Errors)
{
	// TODO: Consider adding a custom copy routine to only copy the data we want, rather than copying the full file then truncating

	const FString PackageFilePath = PackagePath.GetLocalFullPath();

	if (IFileManager::Get().Copy(*CopyPath, *PackageFilePath) != ECopyResult::COPY_OK)
	{
		FText Message = FText::Format(	LOCTEXT("Virtualization_CopyFailed", "Unable to copy package file '{0}' for virtualization"),
										FText::FromString(PackagePath.GetDebugName()));
		Errors.Add(Message);
		return false;
	}

	const int64 PackageSizeWithoutTrailer = IFileManager::Get().FileSize(*PackageFilePath) - Trailer.GetTrailerLength();

	TUniquePtr<IFileHandle> TempFileHandle(FPlatformFileManager::Get().GetPlatformFile().OpenWrite(*CopyPath, true));
	if (!TempFileHandle.IsValid())
	{
		FText Message = FText::Format(	LOCTEXT("Virtualization_TruncOpenFailed", "Failed to open package file for truncation'{0}' when virtualizing"),
										FText::FromString(CopyPath));
		Errors.Add(Message);
		return false;
	}

	if (!TempFileHandle->Truncate(PackageSizeWithoutTrailer))
	{
		FText Message = FText::Format(	LOCTEXT("Virtualization_TruncFailed", "Failed to truncate '{0}' when virtualizing"),
										FText::FromString(CopyPath));
		Errors.Add(Message);
		return false;
	}

	return true;
}

/** 
 * Check that the given package ends with PACKAGE_FILE_TAG. Intended to be used to make sure that
 * we have truncated a package correctly when removing the trailers. 
 */
bool ValidatePackage(const FString& PackagePath, TArray<FText>& Errors)
{
	TUniquePtr<IFileHandle> TempFileHandle(FPlatformFileManager::Get().GetPlatformFile().OpenRead(*PackagePath));
	if (!TempFileHandle.IsValid())
	{
		FText ErrorMsg = FText::Format(	LOCTEXT("Virtualization_OpenValidationFailed", "Unable to open '{0}' so that it can be validated"), 
										FText::FromString(PackagePath));
		Errors.Add(ErrorMsg);
		return false;
	}

	TempFileHandle->SeekFromEnd(-4);

	uint32 PackageTag = INDEX_NONE;
	if (!TempFileHandle->Read((uint8*)&PackageTag, 4) || PackageTag != PACKAGE_FILE_TAG)
	{
		FText ErrorMsg = FText::Format(	LOCTEXT("Virtualization_ValidationFailed", "The package '{0}' does not end with a valid tag, the file is considered corrupt"),
										FText::FromString(PackagePath));
		Errors.Add(ErrorMsg);
		return false;
	}


	return true;
}

void OnPrePackageSubmission(const TArray<FString>& FilesToSubmit, TArray<FText>& DescriptionTags, TArray<FText>& Errors)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UE::Virtualization::OnPrePackageSubmission);

	IVirtualizationSystem& System = IVirtualizationSystem::Get();

	// TODO: We could check to see if the package is virtualized even if it is disabled for the project
	// as a safety feature?
	if (!System.IsEnabled())
	{
		return;
	}

	// Can't virtualize if the payload trailer system is disabled
	if (!FPackageTrailer::IsEnabled())
	{
		return;
	}

	// Other systems may have added errors to this array, we need to check so later we can determine if this function added any additional errors.
	const int32 NumErrors = Errors.Num();

	TArray<TPair<FPackagePath,FString>> PackagesToReplace;

	for (const FString& AbsoluteFilePath : FilesToSubmit)
	{
		if (NumErrors != Errors.Num())
		{
			break; // Give up if previous packages have had errors
		}

		const FPackagePath PackagePath = FPackagePath::FromLocalPath(AbsoluteFilePath);

		if (FPackageName::IsPackageExtension(PackagePath.GetHeaderExtension()) || FPackageName::IsTextPackageExtension(PackagePath.GetHeaderExtension()))
		{
			FPackageTrailer Trailer;
			if (FPackageTrailer::TryLoadFromPackage(PackagePath, Trailer))
			{
				const FString PackageFilePath = PackagePath.GetLocalFullPath();
				const FString BaseName = FPaths::GetBaseFilename(PackagePath.GetPackageName());
				const FString TempFilePath = FPaths::CreateTempFilename(*FPaths::ProjectSavedDir(), *BaseName.Left(32));

				// Create copy of package minus the trailer the trailer
				if (!TryCopyPackageWithoutTrailer(PackagePath, TempFilePath, Trailer, Errors))
				{
					continue;
				}			

#if UE_VALIDATE_TRUNCATED_PACKAGE
				// Validate we didn't break the package
				if (!ValidatePackage(TempFilePath, Errors))
				{
					continue;
				}
#endif //UE_VALIDATE_TRUNCATED_PACKAGE

				// Extract each payload and push
				{
					TUniquePtr<FArchive> PackageAr = IPackageResourceManager::Get().OpenReadExternalResource(EPackageExternalResource::WorkspaceDomainFile, PackagePath.GetPackageName());

					if (!PackageAr.IsValid())
					{
						FText Message = FText::Format(	LOCTEXT("Virtualization_PkgOpenm", "Failed to open the package '{1}' for reading"),
														FText::FromString(PackagePath.GetDebugName()));
						Errors.Add(Message);
						break;
					}

					bool bUpdatedTrailer = false;

					for (const FPayloadId& PayloadId : Trailer.GetPayloads(EPayloadFilter::Local))
					{
						if (PayloadId.IsValid())
						{
							FCompressedBuffer Payload = Trailer.LoadPayload(PayloadId, *PackageAr);

							if (PayloadId != FIoHash(Payload.GetRawHash()))
							{
								FText Message = FText::Format(LOCTEXT("Virtualization_WrongPayload", "Package {0} loaded an incorrect payload from the trailer. Expected '{1}' Loaded  '{2}'"),
									FText::FromString(PackagePath.GetDebugName()),
									FText::FromString(PayloadId.ToString()),
									FText::FromString(LexToString(Payload.GetRawHash())));
								Errors.Add(Message);
								break;
							}

							if (Payload)
							{
								if (!System.PushData(PayloadId, Payload, EStorageType::Persistent, PackagePath))
								{
									FText Message = FText::Format(LOCTEXT("Virtualization_PushFailure", "Failed to push payload '{0}' from the package '{1}'"),
										FText::FromString(PayloadId.ToString()),
										FText::FromString(PackagePath.GetDebugName()));
									Errors.Add(Message);
									break;
								}
							}
							else
							{
								FText Message = FText::Format(LOCTEXT("Virtualization_MissingPayload", "Unable to find the payload '{0}' in the local storage of package '{1}'"),
									FText::FromString(PayloadId.ToString()),
									FText::FromString(PackagePath.GetDebugName()));
								Errors.Add(Message);
								break;
							}

							if (Trailer.UpdatePayloadAsVirtualized(PayloadId))
							{
								bUpdatedTrailer = true;
							}
							else
							{
								FText Message = FText::Format(LOCTEXT("Virtualization_UpdateStatusFailed", "Unable to update the status for the payload '{0}' in the package '{1}'"),
									FText::FromString(PayloadId.ToString()),
									FText::FromString(PackagePath.GetDebugName()));
								Errors.Add(Message);
								break;
							}		
						}
					}

					if (bUpdatedTrailer)
					{
						TUniquePtr<FArchive> CopyAr(IFileManager::Get().CreateFileWriter(*TempFilePath, EFileWrite::FILEWRITE_Append));
						if (!CopyAr.IsValid())
						{
							FText Message = FText::Format(	LOCTEXT("Virtualization_TrailerAppendOpen", "Unable to open '{0}' to append the trailer'"),
															FText::FromString(TempFilePath));
							Errors.Add(Message);
							break;
						}

						FPackageTrailerBuilder TrailerBuilder = FPackageTrailerBuilder::Create(Trailer, *PackageAr);
						if (!TrailerBuilder.BuildAndAppendTrailer(nullptr, *CopyAr))
						{
							FText Message = FText::Format(	LOCTEXT("Virtualization_TrailerAppend", "Failed to append the trailer to '{0}'"),
															FText::FromString(TempFilePath));
							Errors.Add(Message);
							break;
						}

						// Now that we have successfully created a new version of the package with an updated trailer 
						// we need to mark that it should replace the original package.
						PackagesToReplace.Emplace(PackagePath, TempFilePath);
					}
				}
			}
		}
	}

	if (NumErrors == Errors.Num())
	{
		// TODO: Consider using the SavePackage model (move the original, then replace, so we can restore all of the original packages if needed)
		// having said that, once a package is in PackagesToReplace it should still be safe to submit so maybe we don't need this level of protection?

		// Since we had no errors we can now replace all of the packages that were virtualized data with the 
		// virtualized replacement file.
		for(const TPair<FPackagePath,FString>&  Iterator : PackagesToReplace)
		{
			const FString OriginalPackagePath = Iterator.Key.GetLocalFullPath();
			const FString& NewPackagePath = Iterator.Value;

			if (!IFileManager::Get().Move(*OriginalPackagePath, *NewPackagePath))
			{
				FText Message = FText::Format(	LOCTEXT("Virtualization_MoveFailed", "Unable to replace the package '{0}' with the virtualized version"),
												FText::FromString(Iterator.Key.GetDebugName()));
				Errors.Add(Message);
				continue;
			}
		}
	}

	// If we had no new errors add the validation tag to indicate that the packages are safe for submission. 
	// TODO: Currently this is a hard coded guid, it should be replaced by something unique to the FilesToSubmit collection that could be 
	// validated by the source control server. This functionality is currently pending.
	if (NumErrors == Errors.Num())
	{
		FText Tag = FText::FromString(TEXT("#virtualization c9c748e7-d00b-45af-bb61-9054abba503a"));
		DescriptionTags.Add(Tag);
	}
}
#endif 
} // namespace UE::Virtualization

#undef LOCTEXT_NAMESPACE

