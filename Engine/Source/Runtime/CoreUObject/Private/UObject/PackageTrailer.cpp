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
	// Access mode is now per payload and found in FLookupTableEntry 
	ACCESS_PER_PAYLOAD = 1,
	// Added EPayloadAccessMode to FLookupTableEntry
	PAYLOAD_FLAGS = 2,

	// -----<new versions can be added before this line>-------------------------------------------------
	// - this needs to be the last line (see note below)
	AUTOMATIC_VERSION_PLUS_ONE,
	AUTOMATIC_VERSION = AUTOMATIC_VERSION_PLUS_ONE - 1
};

// These asserts are here to make sure that any changes to the size of disk constants are intentional.
// If the change was intentional then just update the assert.
static_assert(FPackageTrailer::FHeader::StaticHeaderSizeOnDisk == 28, "FPackageTrailer::FHeader size has been changed, if this was intentional then update this assert");
static_assert(Private::FLookupTableEntry::SizeOnDisk == 49, "FLookupTableEntry size has been changed, if this was intentional then update this assert");
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
		UE_LOG(LogSerialization, Error, TEXT("Could not open the file '%s' for reading due to system error: '%s' (%d))"), *PackagePath.GetDebugName(), SystemErrorMsg, SystemError);
	}
	else
	{
		UE_LOG(LogSerialization, Error, TEXT("Could not open (%s) to read FPackageTrailer with an unknown error"), *PackagePath.GetDebugName());
	}
}

namespace Private
{

FLookupTableEntry::FLookupTableEntry(const FIoHash& InIdentifier, uint64 InRawSize)
	: Identifier(InIdentifier)
	, RawSize(InRawSize)
{

}

void FLookupTableEntry::Serialize(FArchive& Ar, EPackageTrailerVersion PackageTrailerVersion)
{
	Ar << Identifier;
	Ar << OffsetInFile;
	Ar << CompressedSize;
	Ar << RawSize;

	if (Ar.IsSaving() || PackageTrailerVersion >= EPackageTrailerVersion::PAYLOAD_FLAGS)
	{
		Ar << Flags;
	}

	if (Ar.IsSaving() || PackageTrailerVersion >= EPackageTrailerVersion::ACCESS_PER_PAYLOAD)
	{
		Ar << AccessMode;
	}
}

} // namespace Private

FPackageTrailerBuilder FPackageTrailerBuilder::CreateFromTrailer(const FPackageTrailer& Trailer, FArchive& Ar, const FName& PackageName)
{
	FPackageTrailerBuilder Builder(PackageName);

	for (const Private::FLookupTableEntry& Entry : Trailer.Header.PayloadLookupTable)
	{
		checkf(!Entry.Identifier.IsZero(), TEXT("PackageTrailer for package should not contain invalid FIoHash entry. Package '%s'"), *PackageName.ToString());

		switch (Entry.AccessMode)
		{
			case EPayloadAccessMode::Local:
			{
				FCompressedBuffer Payload = Trailer.LoadLocalPayload(Entry.Identifier, Ar);
				Builder.LocalEntries.Add(Entry.Identifier, LocalEntry(MoveTemp(Payload)));
			}
			break;

			case EPayloadAccessMode::Referenced:
			{
				Builder.ReferencedEntries.Add(Entry.Identifier, ReferencedEntry(Entry.OffsetInFile, Entry.CompressedSize, Entry.RawSize));
			}
			break;

			case EPayloadAccessMode::Virtualized:
			{
				Builder.VirtualizedEntries.Add(Entry.Identifier, VirtualizedEntry(Entry.RawSize));
			}
			break;

			default:
			{
				checkNoEntry();
			}
		}
	}

	return Builder;
}

TUniquePtr<UE::FPackageTrailerBuilder> FPackageTrailerBuilder::CreateReferenceToTrailer(const class FPackageTrailer& Trailer, const FName& PackageName)
{
	TUniquePtr<UE::FPackageTrailerBuilder> Builder = MakeUnique<UE::FPackageTrailerBuilder>(PackageName);

	for (const Private::FLookupTableEntry& Entry : Trailer.Header.PayloadLookupTable)
	{
		checkf(!Entry.Identifier.IsZero(), TEXT("PackageTrailer for package should not contain invalid FIoHash entry. Package '%s'"), *PackageName.ToString());

		switch (Entry.AccessMode)
		{
			case EPayloadAccessMode::Local:
			{
				const int64 AbsoluteOffset = Trailer.FindPayloadOffsetInFile(Entry.Identifier);
				checkf(AbsoluteOffset != INDEX_NONE, TEXT("PackageTrailer for package should not contain invalid payload offsets. Package '%s'"), *PackageName.ToString());

				Builder->ReferencedEntries.Add(Entry.Identifier, ReferencedEntry(AbsoluteOffset, Entry.CompressedSize, Entry.RawSize));
			}
			break;
			
			case EPayloadAccessMode::Referenced:
			{
				checkf(false, TEXT("Attempting to create a reference to a trailer that already contains reference payload entries. Package '%s'"), *PackageName.ToString());
			}
			break;
			
			case EPayloadAccessMode::Virtualized:
			{
				Builder->VirtualizedEntries.Add(Entry.Identifier, VirtualizedEntry(Entry.RawSize));
			}
			break;

			default:
			{
				checkNoEntry();
			}

		}
	}

	return Builder;
}

FPackageTrailerBuilder::FPackageTrailerBuilder(const FName& InPackageName)
	: PackageName(InPackageName)
{
}

void FPackageTrailerBuilder::AddPayload(const FIoHash& Identifier, FCompressedBuffer Payload, AdditionalDataCallback&& Callback)
{
	Callbacks.Emplace(MoveTemp(Callback));

	if (!Identifier.IsZero())
	{
		LocalEntries.FindOrAdd(Identifier, LocalEntry(MoveTemp(Payload)));
	}
}

void FPackageTrailerBuilder::AddVirtualizedPayload(const FIoHash& Identifier, int64 RawSize)
{
	if (!Identifier.IsZero())
	{
		VirtualizedEntries.FindOrAdd(Identifier, VirtualizedEntry(RawSize));
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

	const uint32 DynamicHeaderSizeOnDisk = (GetNumPayloads() * Private::FLookupTableEntry::SizeOnDisk); // Add the length of the lookup table
	
	Trailer.Header.HeaderLength = FPackageTrailer::FHeader::StaticHeaderSizeOnDisk + DynamicHeaderSizeOnDisk;
	
	Trailer.Header.PayloadsDataLength = 0;
	Trailer.Header.PayloadLookupTable.Reserve(LocalEntries.Num() + VirtualizedEntries.Num());

	for (const TPair<FIoHash, LocalEntry>& It : LocalEntries)
	{
		checkf(!It.Key.IsZero(), TEXT("PackageTrailer should not contain invalid FIoHash values. Package '%s'"), *PackageName.ToString());

		Private::FLookupTableEntry& Entry = Trailer.Header.PayloadLookupTable.AddDefaulted_GetRef();
		Entry.Identifier = It.Key;
		Entry.OffsetInFile = Trailer.Header.PayloadsDataLength;
		Entry.CompressedSize = It.Value.Payload.GetCompressedSize();
		Entry.RawSize = It.Value.Payload.GetRawSize();
		Entry.AccessMode = EPayloadAccessMode::Local;

		Trailer.Header.PayloadsDataLength += It.Value.Payload.GetCompressedSize();
	}

	for (const TPair<FIoHash, ReferencedEntry>& It : ReferencedEntries)
	{
		checkf(!It.Key.IsZero(), TEXT("PackageTrailer should not contain invalid FIoHash values. Package '%s'"), *PackageName.ToString());

		Private::FLookupTableEntry& Entry = Trailer.Header.PayloadLookupTable.AddDefaulted_GetRef();
		Entry.Identifier = It.Key;
		Entry.OffsetInFile = It.Value.Offset;
		Entry.CompressedSize = It.Value.CompressedSize;
		Entry.RawSize = It.Value.RawSize;
		Entry.AccessMode = EPayloadAccessMode::Referenced;
	}

	for (const TPair<FIoHash, VirtualizedEntry>& It : VirtualizedEntries)
	{
		checkf(!It.Key.IsZero(), TEXT("PackageTrailer should not contain invalid FIoHash values. Package '%s'"), *PackageName.ToString());

		Private::FLookupTableEntry& Entry = Trailer.Header.PayloadLookupTable.AddDefaulted_GetRef();
		Entry.Identifier = It.Key;
		Entry.OffsetInFile = INDEX_NONE;
		Entry.CompressedSize = INDEX_NONE;
		Entry.RawSize = It.Value.RawSize;
		Entry.AccessMode = EPayloadAccessMode::Virtualized;
	}

	// Now that we have the complete trailer we can serialize it to the archive

	Trailer.TrailerPositionInFile = DataArchive.Tell();

	DataArchive << Trailer.Header;

	checkf((Trailer.TrailerPositionInFile + Trailer.Header.HeaderLength) == DataArchive.Tell(), TEXT("Header length was calculated as %d bytes but we wrote %" INT64_FMT " bytes!"), Trailer.Header.HeaderLength, DataArchive.Tell() - Trailer.TrailerPositionInFile);

	const int64 PayloadPosInFile = DataArchive.Tell();

	for (TPair<FIoHash, LocalEntry>& It : LocalEntries)
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
	return LocalEntries.IsEmpty() && ReferencedEntries.IsEmpty() && VirtualizedEntries.IsEmpty();
}

bool FPackageTrailerBuilder::IsLocalPayloadEntry(const FIoHash& Identifier) const
{
	return LocalEntries.Find(Identifier) != nullptr;
}

bool FPackageTrailerBuilder::IsReferencedPayloadEntry(const FIoHash& Identifier) const
{
	return ReferencedEntries.Find(Identifier) != nullptr;
}

bool FPackageTrailerBuilder::IsVirtualizedPayloadEntry(const FIoHash& Identifier) const
{
	return VirtualizedEntries.Find(Identifier) != nullptr;
}

int32 FPackageTrailerBuilder::GetNumPayloads() const
{
	return GetNumLocalPayloads() + GetNumReferencedPayloads() + GetNumVirtualizedPayloads();
}

int32 FPackageTrailerBuilder::GetNumLocalPayloads() const
{
	return LocalEntries.Num();
}

int32 FPackageTrailerBuilder::GetNumReferencedPayloads() const
{
	return ReferencedEntries.Num();
}

int32 FPackageTrailerBuilder::GetNumVirtualizedPayloads() const
{
	return VirtualizedEntries.Num();
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

	EPayloadAccessMode LegacyAccessMode = EPayloadAccessMode::Local;
	if (Header.Version < (uint32)EPackageTrailerVersion::ACCESS_PER_PAYLOAD)
	{
		Ar << LegacyAccessMode;
	}

	int32 NumPayloads = 0;
	Ar << NumPayloads;

	Header.PayloadLookupTable.Reserve(NumPayloads);

	for (int32 Index = 0; Index < NumPayloads; ++Index)
	{
		Private::FLookupTableEntry& Entry = Header.PayloadLookupTable.AddDefaulted_GetRef();
		Entry.Serialize(Ar, (EPackageTrailerVersion)Header.Version);

		if (Header.Version < (uint32)EPackageTrailerVersion::ACCESS_PER_PAYLOAD)
		{
			Entry.AccessMode = Entry.OffsetInFile != INDEX_NONE ? LegacyAccessMode : EPayloadAccessMode::Virtualized;
		}
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

FCompressedBuffer FPackageTrailer::LoadLocalPayload(const FIoHash& Id, FArchive& Ar) const
{
	// TODO: This method should be able to load the payload in all cases, but we need a good way of passing the Archive/PackagePath 
	// to the trailer etc. Would work if we stored the package path in the trailer.
	const Private::FLookupTableEntry* Entry = Header.PayloadLookupTable.FindByPredicate([&Id](const Private::FLookupTableEntry& Entry)->bool
		{
			return Entry.Identifier == Id;
		});

	if (Entry == nullptr || Entry->AccessMode != EPayloadAccessMode::Local)
	{
		return FCompressedBuffer();
	}

	const int64 OffsetInFile = TrailerPositionInFile + Header.HeaderLength + Entry->OffsetInFile;
	Ar.Seek(OffsetInFile);

	FCompressedBuffer Payload;
	Ar << Payload;

	return Payload;
}

bool FPackageTrailer::UpdatePayloadAsVirtualized(const FIoHash& Identifier)
{
	Private::FLookupTableEntry* Entry = Header.PayloadLookupTable.FindByPredicate([&Identifier](const Private::FLookupTableEntry& Entry)->bool
		{
			return Entry.Identifier == Identifier;
		});

	if (Entry != nullptr)
	{
		Entry->AccessMode = EPayloadAccessMode::Virtualized;
		Entry->OffsetInFile = INDEX_NONE;
		Entry->CompressedSize = INDEX_NONE; // Once the payload is virtualized we cannot be sure on the compression
											// being used and so cannot know the compressed size
		return true;
	}
	else
	{
		return false;
	}
}

EPayloadStatus FPackageTrailer::FindPayloadStatus(const FIoHash& Id) const
{
	const Private::FLookupTableEntry* Entry = Header.PayloadLookupTable.FindByPredicate([&Id](const Private::FLookupTableEntry& Entry)->bool
		{
			return Entry.Identifier == Id;
		});

	if (Entry == nullptr)
	{
		return EPayloadStatus::NotFound;
	}

	switch (Entry->AccessMode)
	{
		case EPayloadAccessMode::Local:
			return EPayloadStatus::StoredLocally;
			break;

		case EPayloadAccessMode::Referenced:
			return EPayloadStatus::StoredAsReference;
			break;

		case EPayloadAccessMode::Virtualized:
			return EPayloadStatus::StoredVirtualized;
			break;

		default:
			checkNoEntry();
			return EPayloadStatus::NotFound;
			break;
	}
}

int64 FPackageTrailer::FindPayloadOffsetInFile(const FIoHash& Id) const
{
	if (!Id.IsZero())
	{
		const Private::FLookupTableEntry* Entry = Header.PayloadLookupTable.FindByPredicate([&Id](const Private::FLookupTableEntry& Entry)->bool
			{
				return Entry.Identifier == Id;
			});

		//TODO Better way to return an error?
		check(TrailerPositionInFile != INDEX_NONE);
		check(Header.PayloadsDataLength != INDEX_NONE);
		check(Entry != nullptr);

		switch (Entry->AccessMode)
		{
			case EPayloadAccessMode::Local:
				return TrailerPositionInFile + Header.HeaderLength + Entry->OffsetInFile;
				break;

			case EPayloadAccessMode::Referenced:
				return Entry->OffsetInFile;
				break;

			case EPayloadAccessMode::Virtualized:
				return INDEX_NONE;
				break;

			default:
				checkNoEntry();
				return INDEX_NONE;
				break;
		}
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

TArray<FIoHash> FPackageTrailer::GetPayloads(EPayloadFilter Type) const
{
	TArray<FIoHash> Identifiers;
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
			Count = Algo::CountIf(Header.PayloadLookupTable, [](const Private::FLookupTableEntry& Entry) { return Entry.AccessMode == EPayloadAccessMode::Local; });
			break;

		case EPayloadFilter::Referenced:
			Count = Algo::CountIf(Header.PayloadLookupTable, [](const Private::FLookupTableEntry& Entry) { return Entry.AccessMode == EPayloadAccessMode::Referenced; });
			break;

		case EPayloadFilter::Virtualized:
			Count = Algo::CountIf(Header.PayloadLookupTable, [](const Private::FLookupTableEntry& Entry) { return Entry.AccessMode == EPayloadAccessMode::Virtualized; });
			break;

		default:
			checkNoEntry();
	}
	
	return Count;
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
	// Make sure that we save the most up to date version	
	Header.Version = (int32)EPackageTrailerVersion::AUTOMATIC_VERSION;

	Ar << Header.Tag;
	Ar << Header.Version;
	Ar << Header.HeaderLength;
	Ar << Header.PayloadsDataLength;

	int32 NumPayloads = Header.PayloadLookupTable.Num();
	Ar << NumPayloads;

	for (Private::FLookupTableEntry& Entry : Header.PayloadLookupTable)
	{
		Entry.Serialize(Ar, (EPackageTrailerVersion)Header.Version);
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

bool FindPayloadsInPackageFile(const FPackagePath& PackagePath, EPayloadFilter Filter, TArray<FIoHash>& OutPayloadIds)
{
	if (FPackageName::IsTextPackageExtension(PackagePath.GetHeaderExtension()))
	{
		UE_LOG(LogSerialization, Warning, TEXT("Attempting to call 'FindPayloadsInPackageFile' on a text based asset '%s' this is not currently supported"), *PackagePath.GetDebugName());
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
			UE_LOG(LogSerialization, Warning, TEXT("Failed to parse the FPackageTrailer for '%s'"), *PackagePath.GetDebugName());
			return false;
		}	
	}
	else
	{
		UE_LOG(LogSerialization, Warning, TEXT("Unable to open '%s' for reading"), *PackagePath.GetDebugName());
		return false;
	}
}

} //namespace UE

