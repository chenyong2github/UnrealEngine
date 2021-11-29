// Copyright Epic Games, Inc. All Rights Reserved.

#include "UObject/PackageTrailer.h"

#include "CoreGlobals.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/PackagePath.h"
#include "Misc/PackageSegment.h"
#include "UObject/PackageResourceManager.h"

namespace UE
{

/** The version number for the FPackageTrailer format */
enum class EPackageTrailerVersion : uint32
{
	// The original trailer format when it was first added
	INITIAL = 0,

	// -----<new versions can be added before this line>-------------------------------------------------
	// - this needs to be the last line (see note below)
	AUTOMATIC_VERSION_PLUS_ONE,
	AUTOMATIC_VERSION = AUTOMATIC_VERSION_PLUS_ONE - 1
};

// These asserts are here to make sure that any changes to the size of disk constants are intentional.
// If the change was intentional then just update the assert.
static_assert(FPackageTrailer::FHeader::StaticHeaderSizeOnDisk == 29, "FPackageTrailer::FHeader size has been changed, if this was intentional then update this assert");
static_assert(Private::FLookupTableEntry::SizeOnDisk == 44, "FLookupTableEntry size has been changed, if this was intentional then update this assert");
static_assert(FPackageTrailer::FFooter::SizeOnDisk == 20, "FPackageTrailer::FFooter size has been changed, if this was intentional then update this assert");

/** Utility for recording failed package open reasons */
void LogPackageOpenFailureMessage(const FPackagePath& PackagePath)
{
	// TODO: Check the various error paths here again!
	const uint32 SystemError = FPlatformMisc::GetLastError();
	// If we have a system error we can give a more informative error message but don't output it if the error is zero as 
	// this can lead to very confusing error messages.
	if (SystemError != 0)
	{
		TCHAR SystemErrorMsg[2048] = { 0 };
		FPlatformMisc::GetSystemErrorMessage(SystemErrorMsg, sizeof(SystemErrorMsg), SystemError);
		UE_LOG(LogVirtualization, Error, TEXT("Could not open the file '%s' for reading due to system error: '%s' (%d))"), *PackagePath.GetDebugName(), SystemErrorMsg, SystemError);
	}
	else
	{
		UE_LOG(LogVirtualization, Error, TEXT("Could not open (%s) to read FVirtualizedUntypedBulkData with an unknown error"), *PackagePath.GetDebugName());
	}
}

namespace Private
{

FLookupTableEntry::FLookupTableEntry(const Virtualization::FPayloadId& InIdentifier, uint64 InRawSize)
	: Identifier(InIdentifier)
	, RawSize(InRawSize)
{

}

FArchive& operator<<(FArchive& Ar, FLookupTableEntry& Entry)
{
	Ar << Entry.Identifier;
	Ar << Entry.OffsetInFile;
	Ar << Entry.CompressedSize;
	Ar << Entry.RawSize;

	return Ar;
}

} // namespace Private

FPackageTrailerBuilder FPackageTrailerBuilder::Create(const FPackageTrailer& Trailer, FArchive& Ar)
{
	FPackageTrailerBuilder Builder;

	for (const Private::FLookupTableEntry& Entry : Trailer.Header.PayloadLookupTable)
	{
		if (Entry.IsVirtualized())
		{
			Builder.VirtualizedEntries.Add(Entry.Identifier, VirtualizedEntry(Entry.CompressedSize, Entry.RawSize));
		}
		else
		{
			FCompressedBuffer Payload = Trailer.LoadPayload(Entry.Identifier, Ar);
			Builder.LocalEntries.Add(Entry.Identifier, LocalEntry(MoveTemp(Payload)));
		}
	}

	return Builder;
}

int64 FPackageTrailerBuilder::FindPayloadOffset(const Virtualization::FPayloadId& Identifier) const
{
	check(TrailerPositionInFile != INDEX_NONE);
	check(PayloadPosInFile != INDEX_NONE);
	check(PayloadLookupTable.Num() == LocalEntries.Num() + VirtualizedEntries.Num());

	for (const Private::FLookupTableEntry& Entry : PayloadLookupTable)
	{
		if (Entry.Identifier == Identifier)
		{
			return PayloadPosInFile + Entry.OffsetInFile;
		}
	}

	return INDEX_NONE;
}

bool FPackageTrailerBuilder::BuildAndAppendTrailer(FLinkerSave* Linker, FArchive& DataArchive)
{
	checkf(TrailerPositionInFile == INDEX_NONE, TEXT("Attempting to build the same FPackageTrailer multiple times"));

	// Note that we do not serialize containers directly as we want a file format that is 
	// 100% under our control. This will allow people to create external scripts that can
	// parse and manipulate the trailer without needing to worry that we might change how
	// our containers serialize. 
	TrailerPositionInFile = DataArchive.Tell();

	uint64 HeaderTag = FPackageTrailer::FHeader::HeaderTag;
	DataArchive << HeaderTag;

	EPackageTrailerVersion Version = EPackageTrailerVersion::AUTOMATIC_VERSION;
	DataArchive << Version;

	const uint32 DynamicHeaderSizeOnDisk = ((LocalEntries.Num() + VirtualizedEntries.Num()) * Private::FLookupTableEntry::SizeOnDisk); // Add the length of the lookup table
	
	uint32 HeaderSizeOnDisk = FPackageTrailer::FHeader::StaticHeaderSizeOnDisk + DynamicHeaderSizeOnDisk;
	DataArchive << HeaderSizeOnDisk;

	int64 PayloadsDataLength = 0;
	PayloadLookupTable.Reserve(LocalEntries.Num() + VirtualizedEntries.Num());

	for (const TPair<Virtualization::FPayloadId, LocalEntry>& It : LocalEntries)
	{
		Private::FLookupTableEntry& Entry = PayloadLookupTable.AddDefaulted_GetRef();
		Entry.Identifier = It.Key;
		Entry.OffsetInFile = PayloadsDataLength;
		Entry.CompressedSize = It.Value.Payload.GetCompressedSize();
		Entry.RawSize = It.Value.Payload.GetRawSize();

		PayloadsDataLength += It.Value.Payload.GetCompressedSize();
	}

	for (const TPair<Virtualization::FPayloadId, VirtualizedEntry>& It : VirtualizedEntries)
	{
		Private::FLookupTableEntry& Entry = PayloadLookupTable.AddDefaulted_GetRef();
		Entry.Identifier = It.Key;
		Entry.OffsetInFile = INDEX_NONE;
		Entry.CompressedSize = It.Value.CompressedSize;
		Entry.RawSize = It.Value.RawSize;
	}

	DataArchive << PayloadsDataLength;

	// Currently we only support relative access!
	// Referenced access will come with the editor domain support.
	EPayloadAccessMode AccessMode = EPayloadAccessMode::Relative;
	DataArchive << AccessMode;

	int32 NumPayloads = PayloadLookupTable.Num();
	DataArchive << NumPayloads;

	for (Private::FLookupTableEntry& Entry : PayloadLookupTable)
	{
		DataArchive << Entry;
	}

	checkf((TrailerPositionInFile + HeaderSizeOnDisk) == DataArchive.Tell(), TEXT("Header length was calculated as %d bytes but we wrote %" INT64_FMT " bytes!"), HeaderSizeOnDisk, DataArchive.Tell() - TrailerPositionInFile);

	PayloadPosInFile = DataArchive.Tell();

	for (TPair<Virtualization::FPayloadId, LocalEntry>& It : LocalEntries)
	{
		DataArchive << It.Value.Payload;
	}

	checkf((PayloadPosInFile + PayloadsDataLength) == DataArchive.Tell(), TEXT("Total payload length was calculated as %" INT64_FMT " bytes but we wrote %" INT64_FMT " bytes!"), PayloadsDataLength, DataArchive.Tell() - PayloadPosInFile);

	uint64 FooterTag = FPackageTrailer::FFooter::FooterTag;
	DataArchive << FooterTag;

	int64 TrailerLength = HeaderSizeOnDisk + PayloadsDataLength + FPackageTrailer::FFooter::SizeOnDisk;
	DataArchive << TrailerLength;
	
	uint32 PackageTag = PACKAGE_FILE_TAG;
	DataArchive << PackageTag;

	checkf((TrailerPositionInFile + TrailerLength) == DataArchive.Tell(), TEXT("Trailer length was calculated as %" INT64_FMT " bytes but we wrote %" INT64_FMT " bytes!"), TrailerLength, DataArchive.Tell() - TrailerPositionInFile);

	for (const AdditionalDataCallback& Callback : Callbacks)
	{
		Callback(*Linker);
	}

	return !DataArchive.IsError();
}

bool FPackageTrailerBuilder::IsEmpty() const
{
	return LocalEntries.IsEmpty() && VirtualizedEntries.IsEmpty();
}

void FPackageTrailerBuilder::AddPayload(const Virtualization::FPayloadId& Identifier, FCompressedBuffer Payload, AdditionalDataCallback&& Callback)
{
	checkf(TrailerPositionInFile == INDEX_NONE, TEXT("Attempting to add payloads after the trailer has been built"));

	Callbacks.Emplace(MoveTemp(Callback));
	LocalEntries.Add(Identifier, LocalEntry(MoveTemp(Payload)));
}

bool FPackageTrailer::IsEnabled()
{
	static struct FUsePackageTrailer
	{
		bool bEnabled = true;

		FUsePackageTrailer()
		{		
			GConfig->GetBool(TEXT("Core.System"), TEXT("UsePackageTrailer"), bEnabled, GEngineIni);
			UE_LOG(LogSerialization, Log, TEXT("UsePackageTrailer: '%s'"), bEnabled ? TEXT("true") : TEXT("false"));

			// Check to make sure that the editor domain is not also enabled and assert if it is.
			// Currently the package trailer system will not work correctly with the editor domain and as
			// it is an opt in feature in development we should just prevent people running with both
			// options enabled.
			// We check the config values directly to avoid needing to introduce dependencies between modules.
			if (bEnabled)
			{
				FConfigFile PlatformEngineIni;
				FConfigCacheIni::LoadLocalIniFile(PlatformEngineIni, TEXT("Editor"), true);

				bool bEditorDomainEnabled = false;
				if (GConfig->GetBool(TEXT("CookSettings"), TEXT("EditorDomainEnabled"), bEditorDomainEnabled, GEditorIni))
				{
					checkf(!bEditorDomainEnabled, TEXT("The package trailer system does not yet work with the editor domain!"));
				}

				if (GConfig->GetBool(TEXT("EditorDomain"), TEXT("EditorDomainEnabled"), bEditorDomainEnabled, GEditorIni))
				{
					checkf(!bEditorDomainEnabled, TEXT("The package trailer system does not yet work with the editor domain!"));
				}
			}
		}
	} UsePackageTrailer;

	return UsePackageTrailer.bEnabled;
}

bool FPackageTrailer::TryLoadFromPackage(const FPackagePath& PackagePath, FPackageTrailer& OutTrailer)
{
	//TODO: Need to do this in a way that supports text based assets
	TUniquePtr<FArchive> PackageAr = IPackageResourceManager::Get().OpenReadExternalResource(EPackageExternalResource::WorkspaceDomainFile, PackagePath.GetPackageName());

	if (PackageAr.IsValid())
	{
		PackageAr->Seek(PackageAr->TotalSize());
		return OutTrailer.TryLoadBackwards(*PackageAr);
	}
	else
	{
		LogPackageOpenFailureMessage(PackagePath);
		return false;
	}	
}

bool FPackageTrailer::TryLoad(FArchive& Ar)
{
	check(Ar.IsLoading());

	TrailerPositionInFile = Ar.Tell();

	Ar << Header.Tag;

	// Make sure that we are parsing a valid FPackageTrailer
	if (Header.Tag != FPackageTrailer::FHeader::HeaderTag)
	{
		return false;
	}

	Ar << Header.Version;

	Ar << Header.HeaderLength;
	Ar << Header.PayloadsDataLength;
	Ar << Header.AccessMode;

	int32 NumPayloads = 0;
	Ar << NumPayloads;

	Header.PayloadLookupTable.Reserve(NumPayloads);

	for (int32 Index = 0; Index < NumPayloads; ++Index)
	{
		Private::FLookupTableEntry& Entry = Header.PayloadLookupTable.AddDefaulted_GetRef();
		Ar << Entry;
	}

	return !Ar.IsError();
}

bool FPackageTrailer::TryLoadBackwards(FArchive& Ar)
{
	check(Ar.IsLoading());

	Ar.Seek(Ar.Tell() - FFooter::SizeOnDisk);

	FFooter Footer;

	Ar << Footer.Tag;
	Ar << Footer.TrailerLength;
	Ar << Footer.PackageTag;

	// First check the package tag as this indicates if the file is corrupted or not
	if (Footer.PackageTag != PACKAGE_FILE_TAG)
	{
		return false;
	}

	// Now check the footer tag which will indicate if this is actually a FPackageTrailer that we are parsing
	if (Footer.Tag != FFooter::FooterTag)
	{
		return false;
	}

	Ar.Seek(Ar.Tell() - Footer.TrailerLength);

	return TryLoad(Ar);
}

FCompressedBuffer FPackageTrailer::LoadPayload(Virtualization::FPayloadId Id, FArchive& Ar) const
{
	const Private::FLookupTableEntry* Entry = Header.PayloadLookupTable.FindByPredicate([&Id](const Private::FLookupTableEntry& Entry)->bool
		{
			return Entry.Identifier == Id;
		});

	// TODO: Should we check if payload is correct here or let caller do that?

	if (Entry == nullptr)
	{
		return FCompressedBuffer();
	}

	const int64 OffsetInFile = TrailerPositionInFile + + Header.HeaderLength + Entry->OffsetInFile;
	Ar.Seek(OffsetInFile);

	FCompressedBuffer Payload;
	Ar << Payload;

	return Payload;
}

bool FPackageTrailer::UpdatePayloadAsVirtualized(Virtualization::FPayloadId Identifier)
{
	Private::FLookupTableEntry* Entry = Header.PayloadLookupTable.FindByPredicate([&Identifier](const Private::FLookupTableEntry& Entry)->bool
		{
			return Entry.Identifier == Identifier;
		});

	if (Entry != nullptr)
	{
		Entry->OffsetInFile = INDEX_NONE;
		return true;
	}
	else
	{
		return false;
	}
}

EPayloadStatus FPackageTrailer::FindPayloadStatus(Virtualization::FPayloadId Id) const
{
	const Private::FLookupTableEntry* Entry = Header.PayloadLookupTable.FindByPredicate([&Id](const Private::FLookupTableEntry& Entry)->bool
		{
			return Entry.Identifier == Id;
		});

	if (Entry == nullptr)
	{
		return EPayloadStatus::NotFound;
	}

	return Entry->IsVirtualized() ? EPayloadStatus::StoredVirtualized : EPayloadStatus::StoredLocally;
}

int64 FPackageTrailer::GetTrailerLength() const
{
	return Header.HeaderLength + Header.PayloadsDataLength + FFooter::SizeOnDisk;
}

TArray<Virtualization::FPayloadId> FPackageTrailer::GetPayloads(EPayloadFilter Type) const
{
	TArray<Virtualization::FPayloadId> Identifiers;
	Identifiers.Reserve(Header.PayloadLookupTable.Num());

	for (const Private::FLookupTableEntry& Entry : Header.PayloadLookupTable)
	{
		switch (Type)
		{
		case EPayloadFilter::All:
			Identifiers.Add(Entry.Identifier);
			break;

		case EPayloadFilter::Local:
			if (!Entry.IsVirtualized())
			{
				Identifiers.Add(Entry.Identifier);
			}
			break;

		case EPayloadFilter::Virtualized:
			if (Entry.IsVirtualized())
			{
				Identifiers.Add(Entry.Identifier);
			}
			break;

		default:
			checkNoEntry();
		}	
	}

	return Identifiers;
}

bool FindPayloadsInPackageFile(const FPackagePath& PackagePath, EPayloadFilter Filter, TArray<Virtualization::FPayloadId>& OutPayloadIds)
{
	if (FPackageName::IsTextPackageExtension(PackagePath.GetHeaderExtension()))
	{
		UE_LOG(LogVirtualization, Warning, TEXT("Attempting to call 'FindPayloadsInPackageFile' on a text based asset '%s' this is not currently supported"), *PackagePath.GetDebugName());
		return false;
	}

	TUniquePtr<FArchive> Ar = IPackageResourceManager::Get().OpenReadExternalResource(EPackageExternalResource::WorkspaceDomainFile, PackagePath.GetPackageName());

	if (Ar.IsValid())
	{
		Ar->Seek(Ar->TotalSize());

		FPackageTrailer Trailer;
		
		if (Trailer.TryLoadBackwards(*Ar))
		{
			OutPayloadIds = Trailer.GetPayloads(Filter);
			return true;
		}
		else
		{
			UE_LOG(LogVirtualization, Warning, TEXT("Failed to parse the FPackageTrailer for '%s'"), *PackagePath.GetDebugName());
			return false;
		}	
	}
	else
	{
		UE_LOG(LogVirtualization, Warning, TEXT("Unable to open '%s' for reading"), *PackagePath.GetDebugName());
		return false;
	}
}

} //namespace UE

