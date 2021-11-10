// Copyright Epic Games, Inc. All Rights Reserved.

#include "PackageSubmissionChecks.h"

#include "Containers/UnrealString.h"
#include "Internationalization/Internationalization.h"
#include "Misc/PackageName.h"
#include "Serialization/VirtualizedBulkData.h"
#include "Virtualization/VirtualizationSystem.h"

#define LOCTEXT_NAMESPACE "Virtualization"

namespace UE::Virtualization
{

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
			if (FindPayloadsInPackageFile(PackagePath, EPayloadType::Virtualized, PayloadsInPackage))
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
} // namespace UE::Virtualization

#undef LOCTEXT_NAMESPACE
