// Copyright Epic Games, Inc. All Rights Reserved.

#include "UObject/PackageTrailer.h"

#include "Algo/Count.h"
#include "CoreGlobals.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/PackagePath.h"
#include "Misc/PackageSegment.h"
#include "UObject/Package.h"
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

FPackageTrailerBuilder FPackageTrailerBuilder::CreateFromTrailer(const FPackageTrailer& Trailer, FArchive& Ar, const FPackagePath& PackagePath)
{
	FPackageTrailerBuilder Builder(PackagePath.GetPackageFName());

	for (const Private::FLookupTableEntry& Entry : Trailer.Header.PayloadLookupTable)
	{
		checkf(Entry.Identifier.IsValid(), TEXT("PackageTrailer for package should not contain invalid FPayloadIds. Package '%s'"), *PackagePath.GetPackageName());

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

FPackageTrailerBuilder::FPackageTrailerBuilder(UPackage* Package)
{
	if (Package != nullptr)
	{
		PackageName = Package->GetFName();
	}
}

FPackageTrailerBuilder::FPackageTrailerBuilder(FName InPackageName)
	: PackageName(InPackageName)
{
}

void FPackageTrailerBuilder::AddPayload(const Virtualization::FPayloadId& Identifier, FCompressedBuffer Payload, AdditionalDataCallback&& Callback)
{
	Callbacks.Emplace(MoveTemp(Callback));

	if (Identifier.IsValid())
	{
		LocalEntries.Add(Identifier, LocalEntry(MoveTemp(Payload)));
	}
}

bool FPackageTrailerBuilder::BuildAndAppendTrailer(FLinkerSave* Linker, FArchive& DataArchive)
{
	// Note that we do not serialize containers directly as we want a file format that is 
	// 100% under our control. This will allow people to create external scripts that can
	// parse and manipulate the trailer without needing to worry that we might change how
	// our containers serialize. 
	
	// First we build a trailer structure
	FPackageTrailer Trailer;
	
	Trailer.Header.Tag = FPackageTrailer::FHeader::HeaderTag;
	Trailer.Header.Version = (int32)EPackageTrailerVersion::AUTOMATIC_VERSION;

	const uint32 DynamicHeaderSizeOnDisk = ((LocalEntries.Num() + VirtualizedEntries.Num()) * Private::FLookupTableEntry::SizeOnDisk); // Add the length of the lookup table
	
	Trailer.Header.HeaderLength = FPackageTrailer::FHeader::StaticHeaderSizeOnDisk + DynamicHeaderSizeOnDisk;
	
	Trailer.Header.PayloadsDataLength = 0;
	Trailer.Header.PayloadLookupTable.Reserve(LocalEntries.Num() + VirtualizedEntries.Num());

	for (const TPair<Virtualization::FPayloadId, LocalEntry>& It : LocalEntries)
	{
		checkf(It.Key.IsValid(), TEXT("PackageTrailer should not contain invalid FPayloadIds. Package '%s'"), *PackageName.ToString());

		Private::FLookupTableEntry& Entry = Trailer.Header.PayloadLookupTable.AddDefaulted_GetRef();
		Entry.Identifier = It.Key;
		Entry.OffsetInFile = Trailer.Header.PayloadsDataLength;
		Entry.CompressedSize = It.Value.Payload.GetCompressedSize();
		Entry.RawSize = It.Value.Payload.GetRawSize();

		Trailer.Header.PayloadsDataLength += It.Value.Payload.GetCompressedSize();
	}

	for (const TPair<Virtualization::FPayloadId, VirtualizedEntry>& It : VirtualizedEntries)
	{
		checkf(It.Key.IsValid(), TEXT("PackageTrailer should not contain invalid FPayloadIds. Package '%s'"), *PackageName.ToString());

		Private::FLookupTableEntry& Entry = Trailer.Header.PayloadLookupTable.AddDefaulted_GetRef();
		Entry.Identifier = It.Key;
		Entry.OffsetInFile = INDEX_NONE;
		Entry.CompressedSize = It.Value.CompressedSize;
		Entry.RawSize = It.Value.RawSize;
	}

	Trailer.Header.AccessMode = AccessMode;

	// Now that we have the complete trailer we can serialize it to the archive

	Trailer.TrailerPositionInFile = DataArchive.Tell();

	DataArchive << Trailer.Header;

	checkf((Trailer.TrailerPositionInFile + Trailer.Header.HeaderLength) == DataArchive.Tell(), TEXT("Header length was calculated as %d bytes but we wrote %" INT64_FMT " bytes!"), Trailer.Header.HeaderLength, DataArchive.Tell() - Trailer.TrailerPositionInFile);

	const int64 PayloadPosInFile = DataArchive.Tell();

	for (TPair<Virtualization::FPayloadId, LocalEntry>& It : LocalEntries)
	{
		DataArchive << It.Value.Payload;
	}

	checkf((PayloadPosInFile + Trailer.Header.PayloadsDataLength) == DataArchive.Tell(), TEXT("Total payload length was calculated as %" INT64_FMT " bytes but we wrote %" INT64_FMT " bytes!"), Trailer.Header.PayloadsDataLength, DataArchive.Tell() - PayloadPosInFile);

	FPackageTrailer::FFooter Footer = Trailer.CreateFooter();
	DataArchive << Footer;

	checkf((Trailer.TrailerPositionInFile + Footer.TrailerLength) == DataArchive.Tell(), TEXT("Trailer length was calculated as %" INT64_FMT " bytes but we wrote %" INT64_FMT " bytes!"), Footer.TrailerLength, DataArchive.Tell() - Trailer.TrailerPositionInFile);

	// Invoke any registered callbacks and pass in the trailer, this allows the callbacks to poll where 
	// in the output archive the payload has been stored.
	for (const AdditionalDataCallback& Callback : Callbacks)
	{
		Callback(*Linker, Trailer);
	}

	return !DataArchive.IsError();
}

bool FPackageTrailerBuilder::IsEmpty() const
{
	return LocalEntries.IsEmpty() && VirtualizedEntries.IsEmpty();
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

FPackageTrailer FPackageTrailer::CreateReference(const FPackageTrailer& OriginalTrailer)
{
	check(OriginalTrailer.TrailerPositionInFile != INDEX_NONE);

	FPackageTrailer ReferenceTrailer;
	ReferenceTrailer.Header = OriginalTrailer.Header;
	ReferenceTrailer.Header.AccessMode = EPayloadAccessMode::Referenced; // This trailer will reference payloads stored in another trailer
	ReferenceTrailer.Header.PayloadsDataLength = 0; // This trailer will contain no payload data

	// Convert the payload offsets into absolute offsets
	const int64 PayloadOffset = OriginalTrailer.TrailerPositionInFile + OriginalTrailer.Header.HeaderLength;
	for (Private::FLookupTableEntry& Entry : ReferenceTrailer.Header.PayloadLookupTable)
	{
		if (!Entry.IsVirtualized())
		{
			Entry.OffsetInFile += PayloadOffset;
		}
	}

	return ReferenceTrailer;
}

bool FPackageTrailer::TrySave(FArchive& Ar)
{
	if (Header.AccessMode != EPayloadAccessMode::Referenced)
	{
		// Can only save reference trailers directly.
		return false;
	}

	check(Header.PayloadsDataLength == 0);

	const int64 PositionInArchive = Ar.Tell();

	Ar << Header;
	checkf((PositionInArchive + Header.HeaderLength) == Ar.Tell(), TEXT("Header length was calculated as %d bytes but we wrote %" INT64_FMT " bytes!"), Header.HeaderLength, Ar.Tell() - PositionInArchive);

	FFooter Footer = CreateFooter();

	Ar << Footer;

	checkf((PositionInArchive + Footer.TrailerLength) == Ar.Tell(), TEXT("Trailer length was calculated as %" INT64_FMT " bytes but we wrote %" INT64_FMT " bytes!"), Footer.TrailerLength, Ar.Tell() - PositionInArchive);

	return true;
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

	if (Header.AccessMode == EPayloadAccessMode::Referenced)
	{
		TrailerPositionInFile = INDEX_NONE;
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

FCompressedBuffer FPackageTrailer::LoadPayload(const Virtualization::FPayloadId& Id, FArchive& Ar) const
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

bool FPackageTrailer::UpdatePayloadAsVirtualized(const Virtualization::FPayloadId& Identifier)
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

EPayloadStatus FPackageTrailer::FindPayloadStatus(const Virtualization::FPayloadId& Id) const
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

int64 FPackageTrailer::FindPayloadOffsetInFile(const Virtualization::FPayloadId& Id) const
{
	if (Id.IsValid())
	{
		const Private::FLookupTableEntry* Entry = Header.PayloadLookupTable.FindByPredicate([&Id](const Private::FLookupTableEntry& Entry)->bool
			{
				return Entry.Identifier == Id;
			});

		//TODO Better way to return an error?
		check(TrailerPositionInFile != INDEX_NONE);
		check(Header.PayloadsDataLength != INDEX_NONE);
		check(Entry != nullptr);

		return TrailerPositionInFile + Header.HeaderLength + Entry->OffsetInFile;
	}
	else
	{
		return INDEX_NONE;
	}
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

int32 FPackageTrailer::GetNumPayloads(EPayloadFilter Type) const
{
	int32 Count = 0;

	switch (Type)
	{
	case EPayloadFilter::All:
		Count = Header.PayloadLookupTable.Num();
		break;

	case EPayloadFilter::Local:
		Count = Algo::CountIf(Header.PayloadLookupTable, [](const Private::FLookupTableEntry& Entry) { return !Entry.IsVirtualized(); });
		break;

	case EPayloadFilter::Virtualized:
		Count = Algo::CountIf(Header.PayloadLookupTable, [](const Private::FLookupTableEntry& Entry) { return Entry.IsVirtualized(); });
		break;

	default:
		checkNoEntry();
	}
	
	return Count;
}

EPayloadAccessMode FPackageTrailer::GetAccessMode() const
{
	return Header.AccessMode;
}

FPackageTrailer::FFooter FPackageTrailer::CreateFooter() const
{
	FFooter Footer;

	Footer.Tag = FPackageTrailer::FFooter::FooterTag;
	Footer.TrailerLength = Header.HeaderLength + Header.PayloadsDataLength + FPackageTrailer::FFooter::SizeOnDisk;
	Footer.PackageTag = PACKAGE_FILE_TAG;

	return Footer;
}

FArchive& operator<<(FArchive& Ar, FPackageTrailer::FHeader& Header)
{
	Ar << Header.Tag;
	Ar << Header.Version;
	Ar << Header.HeaderLength;
	Ar << Header.PayloadsDataLength;
	Ar << Header.AccessMode;

	int32 NumPayloads = Header.PayloadLookupTable.Num();
	Ar << NumPayloads;

	for (Private::FLookupTableEntry& Entry : Header.PayloadLookupTable)
	{
		Ar << Entry;
	}

	return Ar;
}

FArchive& operator<<(FArchive& Ar, FPackageTrailer::FFooter& Footer)
{
	Ar << Footer.Tag;
	Ar << Footer.TrailerLength;
	Ar << Footer.PackageTag;

	return Ar;
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

