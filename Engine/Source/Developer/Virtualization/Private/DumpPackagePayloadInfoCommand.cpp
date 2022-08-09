// Copyright Epic Games, Inc. All Rights Reserved.

#include "HAL/IConsoleManager.h"
#include "Logging/LogMacros.h"
#include "Misc/PackagePath.h"
#include "UObject/PackageTrailer.h"
#include "Misc/PackageName.h"

namespace UE
{

#if WITH_EDITORONLY_DATA

FString BytesToString(int64 SizeInBytes)
{
	if (SizeInBytes < (8 *1024))
	{
		return FString::Printf(TEXT("%4d bytes"), SizeInBytes);
	}
	else if (SizeInBytes < (1024 * 1024))
	{
		double SizeInKb = SizeInBytes / (1024.0);
		return FString::Printf(TEXT("%.2f KB"), SizeInKb);
	}
	else
	{
		double SizeInMB = SizeInBytes / (1024.0 * 1024.0);
		return FString::Printf(TEXT("%.2f MB"), SizeInMB);
	}
}

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
		FStringView ObjectPathString;

		if (FPackageName::ParseExportTextPath(Arg, nullptr /*OutClassName*/, &ObjectPathString))
		{
			ObjectPathString = FPackageName::ObjectPathToPackageName(ObjectPathString);
		}
		else
		{
			ObjectPathString = Arg;
		}

		if (FPackagePath::TryFromMountedName(ObjectPathString, Path))
		{
			FPackageTrailer Trailer;
			if (!FPackageTrailer::TryLoadFromPackage(Path, Trailer))
			{
				UE_LOG(LogVirtualization, Error, TEXT("Failed to find load the package trailer from: '%s'"), *Path.GetDebugName());
				continue;
			}

			TArray<FIoHash> LocalPayloadIds = Trailer.GetPayloads(UE::EPayloadStorageType::Local);
			TArray<FIoHash> VirtualizedPayloadIds = Trailer.GetPayloads(UE::EPayloadStorageType::Virtualized);
			
			UE_LOG(LogVirtualization, Display, TEXT("")); // Blank line to make the output easier to read
			UE_LOG(LogVirtualization, Display, TEXT("Package: '%s' has %d local and %d virtualized payloads"), *Path.GetDebugName(), LocalPayloadIds.Num(), VirtualizedPayloadIds.Num());
			
			if (LocalPayloadIds.Num() > 0)
			{
				UE_LOG(LogVirtualization, Display, TEXT("LocalPayloads:"));
				UE_LOG(LogVirtualization, Display, TEXT("Index | %-40s | SizeOnDisk | FilterReason"), TEXT("PayloadIdentifier"));
				for (int32 Index = 0; Index < LocalPayloadIds.Num(); ++Index)
				{
					FPayloadInfo Info = Trailer.GetPayloadInfo(LocalPayloadIds[Index]);
					UE_LOG(LogVirtualization, Display, TEXT("%02d    | %s | %-10s | %s"), 
						Index, 
						*LexToString(LocalPayloadIds[Index]), 
						*BytesToString(Info.CompressedSize),
						*LexToString(Info.FilterFlags));
				}
			}

			if (VirtualizedPayloadIds.Num() > 0)
			{
				UE_LOG(LogVirtualization, Display, TEXT("VirtualizedPayloads:"));
				UE_LOG(LogVirtualization, Display, TEXT("Index|\t%-40s|\tFilterReason"), TEXT("PayloadIdentifier"));
				for (int32 Index = 0; Index < VirtualizedPayloadIds.Num(); ++Index)
				{
					FPayloadInfo Info = Trailer.GetPayloadInfo(VirtualizedPayloadIds[Index]);
					UE_LOG(LogVirtualization, Display, TEXT("%02d:  |\t%s|\t%s"), Index, *LexToString(VirtualizedPayloadIds[Index]), *LexToString(Info.FilterFlags));
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