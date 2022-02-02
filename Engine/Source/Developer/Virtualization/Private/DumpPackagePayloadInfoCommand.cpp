// Copyright Epic Games, Inc. All Rights Reserved.

#include "HAL/IConsoleManager.h"
#include "Logging/LogMacros.h"
#include "Misc/PackagePath.h"
#include "UObject/PackageTrailer.h"

namespace UE
{

#if WITH_EDITORONLY_DATA

/**
 * This function is used to write information about package's payloads to the log file. This has no 
 * practical development use and should only be used for debugging purposes. 
 * 
 * @param Args	The function expects each arg to be a valid package path. Failure to provide a valid
 *				package path will result in errors being written to the log.
 */
void DumpPackagePayloadInfo(const TArray<FString>& Args)
{
	if (Args.Num() == 0)
	{
		UE_LOG(LogVirtualization, Error, TEXT("Command 'DumpPackagePayloadInfo' called without any arguments"));
		return;
	}

	for (const FString& Arg : Args)
	{
		FPackagePath Path;

		if (FPackagePath::TryFromMountedName(Arg, Path))
		{
			TArray<FIoHash> LocalPayloadIds;
			TArray<FIoHash> VirtualizedPayloadIds;

			if (!UE::FindPayloadsInPackageFile(Path, UE::EPayloadFilter::Local, LocalPayloadIds))
			{
				UE_LOG(LogVirtualization, Error, TEXT("Failed to find local payload information from package: '%s'"), *Path.GetDebugName());
				continue;
			}

			if (!UE::FindPayloadsInPackageFile(Path, UE::EPayloadFilter::Virtualized, VirtualizedPayloadIds))
			{
				UE_LOG(LogVirtualization, Error, TEXT("Failed to find virtualized payload information from package: '%s'"), *Path.GetDebugName());
				continue;
			}

			UE_LOG(LogVirtualization, Display, TEXT("Package: '%s' has %d local and %d virtualized payloads"), *Path.GetDebugName(), LocalPayloadIds.Num(), VirtualizedPayloadIds.Num());
			
			if (LocalPayloadIds.Num() > 0)
			{
				UE_LOG(LogVirtualization, Display, TEXT("LocalPayloads:"));
				for (int32 Index = 0; Index < LocalPayloadIds.Num(); ++Index)
				{
					UE_LOG(LogVirtualization, Display, TEXT("%02d: '%s'"), Index, *LexToString(LocalPayloadIds[Index]));
				}
			}

			if (VirtualizedPayloadIds.Num() > 0)
			{
				UE_LOG(LogVirtualization, Display, TEXT("VirtualizedPayloads:"));
				for (int32 Index = 0; Index < VirtualizedPayloadIds.Num(); ++Index)
				{
					UE_LOG(LogVirtualization, Display, TEXT("%02d: '%s'"), Index, *LexToString(VirtualizedPayloadIds[Index]));
				}
			}
		}
		else
		{
			UE_LOG(LogVirtualization, Error, TEXT("Arg '%s' could not be converted to a valid package path"), *Arg);
		}
	}
}

/** 
 * Note that this command is only valid when 'WITH_EDITORONLY_DATA 1' as virtualized payloads are not
 * expected to exist at runtime. 
 */
static FAutoConsoleCommand CCmdDumpPayloadToc = FAutoConsoleCommand(
	TEXT("DumpPackagePayloadInfo"),
	TEXT("Writes out information about a package's payloads to the log."),
	FConsoleCommandWithArgsDelegate::CreateStatic(DumpPackagePayloadInfo));

#endif //WITH_EDITORONLY_DATA

} // namespace UE