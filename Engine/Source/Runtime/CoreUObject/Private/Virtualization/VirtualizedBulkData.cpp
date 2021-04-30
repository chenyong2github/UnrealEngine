// Copyright Epic Games, Inc. All Rights Reserved.

#include "Virtualization/VirtualizedBulkData.h"

#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/PackageSegment.h"
#include "Misc/SecureHash.h"
#include "Misc/ScopeLock.h"
#include "Serialization/BulkData.h"
#include "UObject/LinkerLoad.h"
#include "UObject/LinkerSave.h"
#include "UObject/Object.h"
#include "Virtualization/IVirtualizationSourceControlUtilities.h"

//#if WITH_EDITORONLY_DATA

namespace UE::Virtualization
{

/** This console variable should only exist for testing */
static TAutoConsoleVariable<bool> CVarShouldLoadFromSidecar(
	TEXT("Serialization.LoadFromSidecar"),
	false,
	TEXT("When true FVirtualizedUntypedBulkData will load from the sidecar file"));

static TAutoConsoleVariable<bool> CVarShouldValidatePayload(
	TEXT("Serialization.ValidatePayloads"),
	false,
	TEXT("When true FVirtualizedUntypedBulkData validate any payload loaded from the sidecar file"));

static TAutoConsoleVariable<bool> CVarShouldAllowSidecarSyncing(
	TEXT("Serialization.AllowSidecarSyncing"),
	false,
	TEXT("When true FVirtualizedUntypedBulkData will attempt to sync it's .upayload file via sourcecontrol if the first attempt to load from it fails"));

/** Wrapper around the config file option [Core.System.Experimental]EnablePackageSidecarSaving */
bool ShouldSaveToPackageSidecar()
{
	static const struct FSaveToPackageSidecar
	{
		bool bEnabled = false;

		FSaveToPackageSidecar()
		{
			GConfig->GetBool(TEXT("Core.System.Experimental"), TEXT("EnablePackageSidecarSaving"), bEnabled, GEngineIni);
		}
	} ConfigSetting;

	return ConfigSetting.bEnabled;
}

/** Utility for logging extended error messages when we fail to open a package for reading */
void LogPackageOpenFailureMessage(const FPackagePath& PackagePath, EPackageSegment PackageSegment)
{
	// TODO: Check the various error paths here again!
	const uint32 SystemError = FPlatformMisc::GetLastError();
	// If we have a system error we can give a more informative error message but don't output it if the error is zero as 
	// this can lead to very confusing error messages.
	if (SystemError != 0)
	{
		TCHAR SystemErrorMsg[2048] = { 0 };
		FPlatformMisc::GetSystemErrorMessage(SystemErrorMsg, sizeof(SystemErrorMsg), SystemError);
		UE_LOG(LogVirtualization, Error, TEXT("Could not open the file '%s' for reading due to system error: '%s' (%d))"), *PackagePath.GetDebugNameWithExtension(PackageSegment), SystemErrorMsg, SystemError);
	}
	else
	{
		UE_LOG(LogVirtualization, Error, TEXT("Could not open (%s) to read FVirtualizedUntypedBulkData with an unknown error"), *PackagePath.GetDebugNameWithExtension(PackageSegment));
	}
}

/** Utility for accessing IVirtualizationSourceControlUtilities from the modular feature system. */
IVirtualizationSourceControlUtilities* GetSourceControlInterface()
{
	return (IVirtualizationSourceControlUtilities*)IModularFeatures::Get().GetModularFeatureImplementation(FName("VirtualizationSourceControlUtilities"), 0);
}

FVirtualizedUntypedBulkData::FVirtualizedUntypedBulkData(const FVirtualizedUntypedBulkData& Other)
{
	*this = Other;
}

FVirtualizedUntypedBulkData& FVirtualizedUntypedBulkData::operator=(const FVirtualizedUntypedBulkData& Other)
{
	if (!BulkDataId.IsValid() && Other.BulkDataId.IsValid())
	{
		BulkDataId = FGuid::NewGuid();
	}

	PayloadContentId = Other.PayloadContentId;
	Payload = Other.Payload;
	PayloadSize = Other.PayloadSize;
	CompressionFormatToUse = Other.CompressionFormatToUse;
	OffsetInFile = Other.OffsetInFile;
	PackagePath = Other.PackagePath;
	PackageSegment = Other.PackageSegment;
	bWasKeyGuidDerived = Other.bWasKeyGuidDerived;
	Flags = Other.Flags;
	
	return *this;
}

void FVirtualizedUntypedBulkData::CreateFromBulkData(FUntypedBulkData& InBulkData, const FGuid& InGuid)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FVirtualizedUntypedBulkData::CreateFromBulkData);
	
	checkf(!BulkDataId.IsValid(), 
		TEXT("Calling ::CreateFromBulkData on a bulkdata object that already has a valid identifier! Package: '%s'"),
		*InBulkData.GetPackagePath().GetDebugName());

	Reset();

	auto AssignGuid = [this, &InBulkData](const FGuid& InGuid)
	{
		// Only use the guid if we will have a valid payload
		if (InBulkData.GetBulkDataSize() > 0)
		{
			BulkDataId = InGuid;
		}

		PayloadContentId = FPayloadId(InGuid);
	};

	if (InGuid.IsValid())
	{
		AssignGuid(InGuid);
	}
	else
	{
		UE_LOG(LogVirtualization, Warning,
			TEXT("CreateFromBulkData recieved an invalid FGuid. A temporary one will be generated until the package is next re-saved! Package: '%s'"),
			*InBulkData.GetPackagePath().GetDebugName());

		AssignGuid(FGuid::NewGuid());
	}	
	
	PayloadSize = InBulkData.GetBulkDataSize();
	
	bWasKeyGuidDerived = true;
	PackagePath = InBulkData.GetPackagePath();
	PackageSegment = InBulkData.GetPackageSegment();
	
	OffsetInFile = InBulkData.GetBulkDataOffsetInFile();

	// Mark that we are actually referencing a payload stored in an old bulkdata
	// format.
	EnumAddFlags(Flags,EFlags::ReferencesLegacyFile);

	if (InBulkData.IsStoredCompressedOnDisk())
	{
		EnumAddFlags(Flags, EFlags::LegacyFileIsCompressed);
	}
	else 
	{
		EnumAddFlags(Flags, EFlags::DisablePayloadCompression);
	}
}

void FVirtualizedUntypedBulkData::Serialize(FArchive& Ar, UObject* Owner)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FVirtualizedUntypedBulkData::Serialize);

	if (Ar.IsTransacting())
	{
		// Do not process the transaction if the owner is mid loading (see FUntypedBulkData::Serialize)
		bool bNeedsTransaction = Ar.IsSaving() && (!Owner || !Owner->HasAnyFlags(RF_NeedLoad));

		Ar << bNeedsTransaction;

		if (bNeedsTransaction)
		{
			Ar << Flags;
			Ar << BulkDataId;
			Ar << PayloadContentId;
			Ar << PayloadSize;		
			Ar << bWasKeyGuidDerived;
			Ar << PackagePath;
			Ar << PackageSegment;
			Ar << OffsetInFile;

			// TODO: We could consider compressing the payload so it takes up less space in the 
			// undo stack or even consider storing as a tmp file on disk rather than keeping it
			// in memory or some other caching system.
			// Serializing full 8k texture payloads to memory on each metadata change will empty
			// the undo stack very quickly.
			
			// Note that we will only serialize the payload if it is in memory. Otherwise we can
			// continue to load the payload as needed from disk or pull from the virtualization system
			bool bPayloadInArchive = Ar.IsSaving() ? !Payload.IsNull() : false;
			Ar << bPayloadInArchive;

			if (Ar.IsSaving())
			{
				if (bPayloadInArchive)
				{
					FCompressedBuffer CompressedPayload = FCompressedBuffer::Compress(NAME_None, Payload);
					SerializeData(Ar, CompressedPayload, Flags);
				}
			}
			else
			{
				FCompressedBuffer CompressedPayload;
				if (bPayloadInArchive)
				{
					SerializeData(Ar, CompressedPayload, Flags);
				}
				
				Payload = CompressedPayload.Decompress();	
			}
		}
	}
	else if (Ar.IsPersistent() && !Ar.IsObjectReferenceCollector() && !Ar.ShouldSkipBulkData())
	{
		// Check if we need to update the payload id before it is serialized
		if (Ar.IsSaving())
		{
			// TODO: Maybe only do this if we are saving with a FLinkerSave (ie package save to disk)
			UpdateKeyIfNeeded();
			checkf(bWasKeyGuidDerived == false, TEXT("bWasKeyGuidDerived should be cleared before saving")); // Sanity check
		}

		// Store the position in the archive of the flags incase that we need to update it later
		const int64 SavedFlagsPos = Ar.Tell();
		Ar << Flags;

		// TODO: Can probably remove these checks before UE5 release
		check(!Ar.IsSaving() || GetPayloadSize() == 0 || BulkDataId.IsValid()); // Sanity check to stop us saving out bad data
		check(!Ar.IsSaving() || GetPayloadSize() == 0 || PayloadContentId.IsValid()); // Sanity check to stop us saving out bad data
		
		Ar << BulkDataId;
		Ar << PayloadContentId;
		Ar << PayloadSize;

		// TODO: Can probably remove these checks before UE5 release
		check(!Ar.IsLoading() || GetPayloadSize() == 0 || BulkDataId.IsValid()); // Sanity check to stop us loading in bad data
		check(!Ar.IsLoading() || GetPayloadSize() == 0 || PayloadContentId.IsValid()); // Sanity check to stop us loading in bad data

		if (Ar.IsSaving())
		{
			checkf(Ar.IsCooking() == false, TEXT("FVirtualizedUntypedBulkData::Serialize should not be called during a cook"));

			FLinkerSave* LinkerSave = Cast<FLinkerSave>(Ar.GetLinker());

			const bool bCanAttemptVirtualization = LinkerSave != nullptr;

			if (bCanAttemptVirtualization)
			{
				PushData(); // Note this can change various members if we are going from non-virtualized to virtualized
			}

			EFlags UpdatedFlags = BuildFlagsForSerialization(Ar);

			// Go back in the archive and update the flags in the archive, we will only apply the updated flags to the current
			// object later if we detect that the package saved successfully.
			// TODO: Not a huge fan of this, might be better to find a way to build the flags during serialization and potential callbacks 
			// later then go back and update the flags in the Ar. Applying the updated flags only if we are saving a package to disk
			// and the save succeeds continues to make sense.
			const int64 RestorePos = Ar.Tell();
			Ar.Seek(SavedFlagsPos);
			Ar << UpdatedFlags;
			Ar.Seek(RestorePos);

			if (!IsDataVirtualized())
			{
				// Need to load the payload so that we can write it out
				FCompressedBuffer PayloadToSerialize = GetDataInternal();
				RecompressForSerialization(PayloadToSerialize, UpdatedFlags);

				// If we are expecting a valid payload but fail to find one something critical has broken so assert now
				// to prevent potentially bad data being saved to disk.
				checkf(PayloadToSerialize || GetPayloadSize() == 0, TEXT("Failed to acquire the payload for saving!"));

				// Write out a dummy value that we will write over once the payload is serialized
				int64 PlaceholderValue = INDEX_NONE;

				const int64 OffsetPos = Ar.Tell();
				Ar << PlaceholderValue; // OffsetInFile

				// The lambda is mutable so that PayloadToSerialize is not const (due to FArchive api not
				// accepting const values)
				auto SerializePayload = [this, OffsetPos, PayloadToSerialize, UpdatedFlags](FArchive& Ar) mutable
				{
					checkf(Ar.IsCooking() == false, TEXT("FVirtualizedUntypedBulkData::Serialize should not be called during a cook"));

					int64 PayloadOffset = Ar.Tell();

					SerializeData(Ar, PayloadToSerialize, UpdatedFlags);

					// Record the current archive offset (probably EOF but we cannot be sure)
					const int64 ReturnPos = Ar.Tell();

					// Update the offset/size entries that we set up during ::Serialize
					Ar.Seek(OffsetPos);
					Ar << PayloadOffset;
					// Restore the archive's offset
					Ar.Seek(ReturnPos);

					// If we are saving the package to disk (we have access to FLinkerSave and it's filepath is valid) 
					// then we should register a callback to be received once the package has actually been saved to 
					// disk so that we can update the object's members to be redirected to the saved file.
					FLinkerSave* LinkerSave = Cast<FLinkerSave>(Ar.GetLinker());
					if (LinkerSave != nullptr && !LinkerSave->GetFilename().IsEmpty())
					{	
						// At some point saving to the sidecar file will be mutually exclusive with saving to the asset file, at that point
						// we can split these code paths entirely for clarity. (might need to update ::BuildFlagsForSerialization at that point too!)
						if (ShouldSaveToPackageSidecar())
						{
							FLinkerSave::FSidecarStorageInfo& SidecarData = LinkerSave->SidecarDataToAppend.AddZeroed_GetRef();
							SidecarData.Identifier = PayloadContentId;
							SidecarData.Payload = PayloadToSerialize;
						}

						auto OnSavePackage = [this, PayloadOffset, UpdatedFlags](const FPackagePath& InPackagePath)
						{
							this->PackagePath = InPackagePath;

							if (!this->PackagePath.IsEmpty())
							{
								this->OffsetInFile = PayloadOffset;
								this->Flags = UpdatedFlags;
							}
							else
							{
								// If for what ever reason we no longer have a valid package path then reset these values
								// so that we get better error messages if they are used in the future.
								this->OffsetInFile = INDEX_NONE;
								EnumRemoveFlags(this->Flags, EFlags::ReferencesLegacyFile | EFlags::LegacyFileIsCompressed);
							}

							if (CanUnloadData())
							{
								this->Payload.Reset();
							}
						};

						LinkerSave->PostSaveCallbacks.Add(MoveTemp(OnSavePackage));
					}
				};

				// If we have a valid linker then we will defer serialization of the payload so that it will
				// be placed at the end of the output file so we don't have to seek past the payload on load.
				// If we do not have a linker then we might as well just serialize right away.
				if(LinkerSave != nullptr)
				{
					 LinkerSave->AdditionalDataToAppend.Add(MoveTemp(SerializePayload));
				}
				else
				{
					SerializePayload(Ar);
				}
			}

			if (CanUnloadData())
			{
				Payload.Reset();
			}
		}
		else if (Ar.IsLoading())
		{
			checkf(!IsReferencingOldBulkData(), TEXT("Virtualized bulkdata was saved with the ReferencesLegacyFile flag!"));

			if (IsDataVirtualized())
			{
				// We aren't going to use these members so reset them
				OffsetInFile = INDEX_NONE;

				PackagePath.Empty();
				PackageSegment = EPackageSegment::Header;
			}
			else
			{
				// If we can lazy load then find the PackagePath, otherwise we will want
				// to serialize immediately.
				if (Ar.IsAllowingLazyLoading())
				{
					PackagePath = GetPackagePathFromOwner(Owner, PackageSegment);
				}
				else
				{
					PackagePath.Empty();
					PackageSegment = EPackageSegment::Header;
				}
				
				OffsetInFile = INDEX_NONE;
				Ar << OffsetInFile;

				if (PackagePath.IsEmpty())
				{
					// If we have no packagepath then we need to load the data immediately
					// as we will not be able to load it on demand.
					FCompressedBuffer CompressedPayload;
					SerializeData(Ar, CompressedPayload, Flags);
					Payload = CompressedPayload.Decompress();
				}
			}
		}
	}
}

FCompressedBuffer FVirtualizedUntypedBulkData::LoadFromDisk() const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FVirtualizedUntypedBulkData::LoadFromDisk);

	if (PackagePath.IsEmpty())
	{
		UE_LOG(LogVirtualization, Error, TEXT("Cannot load a payload with an empty filename!"));
		return FCompressedBuffer();
	}

	if (HasPayloadSidecarFile() && CVarShouldLoadFromSidecar.GetValueOnAnyThread())
	{
		// Note that this code path is purely for debugging and not expected to be enabled by default
		if (CVarShouldValidatePayload.GetValueOnAnyThread())
		{
			UE_LOG(LogVirtualization, Verbose, TEXT("Validating payload loaded from sidecar file: '%s'"), *PackagePath.GetLocalFullPath(EPackageSegment::PayloadSidecar));

			// Load both payloads then generate a FPayloadId from them, since this identifier is a hash of the buffers content
			// we only need to verify them against PayloadContentId to be sure that the data is correct.
			FCompressedBuffer SidecarBuffer = LoadFromSidecarFile();
			FCompressedBuffer AssetBuffer = LoadFromPackageFile();

			FPayloadId SidecarId(SidecarBuffer.Decompress());
			FPayloadId AssetId(SidecarBuffer.Decompress());

			UE_CLOG(SidecarId != PayloadContentId, LogVirtualization, Error, TEXT("Sidecar content did not hash correctly! Found '%s' Expected '%s'"), *SidecarId.ToString(), *PayloadContentId.ToString());
			UE_CLOG(AssetId != PayloadContentId, LogVirtualization, Error, TEXT("Asset content did not hash correctly! Found '%s' Expected '%s'"), *SidecarId.ToString(), *PayloadContentId.ToString())

			return SidecarBuffer;
		}
		else
		{
			return LoadFromSidecarFile();
		}
	}
	else
	{
		return LoadFromPackageFile();	 
	}
}

FCompressedBuffer FVirtualizedUntypedBulkData::LoadFromPackageFile() const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FVirtualizedUntypedBulkData::LoadFromPackageFile);

	UE_LOG(LogVirtualization, Verbose, TEXT("Attempting to load payload from the package file: '%s'"), *PackagePath.GetLocalFullPath(PackageSegment));

	FCompressedBuffer PayloadFromDisk;

	// Open a reader to the file
	FOpenPackageResult Result = IPackageResourceManager::Get().OpenReadPackage(PackagePath, PackageSegment);
	if (Result.Archive.IsValid() && Result.Format == EPackageFormat::Binary)
	{
		checkf(OffsetInFile != INDEX_NONE, TEXT("Attempting to load '%s' from disk with an invalid OffsetInFile!"), *PackagePath.GetDebugNameWithExtension(PackageSegment));
		// Move the correct location of the data in the file
		Result.Archive->Seek(OffsetInFile);

		// Now we can actually serialize it
		SerializeData(*Result.Archive, PayloadFromDisk, Flags);
	}
	else
	{
		LogPackageOpenFailureMessage(PackagePath, PackageSegment);
	}

	return PayloadFromDisk;
}

FCompressedBuffer FVirtualizedUntypedBulkData::LoadFromSidecarFileInternal(ErrorVerbosity Verbosity) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FVirtualizedUntypedBulkData::LoadFromSidecarFileInternal);

	FOpenPackageResult Result = IPackageResourceManager::Get().OpenReadPackage(PackagePath, EPackageSegment::PayloadSidecar);
	if (Result.Archive.IsValid() && Result.Format == EPackageFormat::Binary)
	{
		uint32 Version = INDEX_NONE;
		*Result.Archive << Version;

		if (Version != FTocEntry::PayloadSidecarFileVersion)
		{
			UE_CLOG(Verbosity > ErrorVerbosity::None, LogVirtualization, Error, TEXT("Unknown version (%u) found in '%s'"), Version, *PackagePath.GetLocalFullPath(EPackageSegment::PayloadSidecar));
			return FCompressedBuffer();
		}

		// First we load the table of contents so we can find the payload in the file
		TArray<FTocEntry> TableOfContents;
		*Result.Archive << TableOfContents;

		const FTocEntry* Entry = TableOfContents.FindByPredicate([&PayloadContentId = PayloadContentId](const FTocEntry& Entry)
			{
				return Entry.Identifier == PayloadContentId;
			});

		if (Entry != nullptr)
		{
			if (Entry->OffsetInFile != INDEX_NONE)
			{
				// Move the correct location of the data in the file
				Result.Archive->Seek(Entry->OffsetInFile);

				// Now we can actually serialize it
				FCompressedBuffer PayloadFromDisk;
				SerializeData(*Result.Archive, PayloadFromDisk, EFlags::None);

				return PayloadFromDisk;
			}
			else if(Verbosity > ErrorVerbosity::None)
			{
				UE_LOG(LogVirtualization, Error, TEXT("Payload '%s' in '%s' has an invalid OffsetInFile!"), *PayloadContentId.ToString(), *PackagePath.GetLocalFullPath(EPackageSegment::PayloadSidecar));
			}
		}
		else if(Verbosity > ErrorVerbosity::None)
		{
			UE_LOG(LogVirtualization, Error, TEXT("Unable to find payload '%s' in '%s'"), *PayloadContentId.ToString(), *PackagePath.GetLocalFullPath(EPackageSegment::PayloadSidecar));
		}
	}
	else if(Verbosity > ErrorVerbosity::None)
	{
		LogPackageOpenFailureMessage(PackagePath, EPackageSegment::PayloadSidecar);
	}

	return FCompressedBuffer();
}

FCompressedBuffer FVirtualizedUntypedBulkData::LoadFromSidecarFile() const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FVirtualizedUntypedBulkData::LoadFromSidecarFile);

	UE_LOG(LogVirtualization, Verbose, TEXT("Attempting to load payload from the sidecar file: '%s'"), 
		*PackagePath.GetLocalFullPath(EPackageSegment::PayloadSidecar));

	if (CVarShouldAllowSidecarSyncing.GetValueOnAnyThread())
	{
		FCompressedBuffer PayloadFromDisk = LoadFromSidecarFileInternal(ErrorVerbosity::None);
		if (PayloadFromDisk.IsNull())
		{
			UE_LOG(LogVirtualization, Verbose, TEXT("Initial load from sidecar failed, attempting to sync the file: '%s'"), 
				*PackagePath.GetLocalFullPath(EPackageSegment::PayloadSidecar));

			if (IVirtualizationSourceControlUtilities* SourceControlInterface = GetSourceControlInterface())
			{
				// SyncPayloadSidecarFile should log failure cases, so there is no need for us to add log messages here
				if (SourceControlInterface->SyncPayloadSidecarFile(PackagePath))
				{
					PayloadFromDisk = LoadFromSidecarFileInternal(ErrorVerbosity::All);
				}
			}
			else
			{
				UE_LOG(LogVirtualization, Error, TEXT("Failed to find IVirtualizationSourceControlUtilities, unable to try and sync: '%s'"), 
					*PackagePath.GetLocalFullPath(EPackageSegment::PayloadSidecar));
			}
		}

		return PayloadFromDisk;
	}
	else
	{
		return LoadFromSidecarFileInternal(ErrorVerbosity::All);
	}
}

bool FVirtualizedUntypedBulkData::SerializeData(FArchive& Ar, FCompressedBuffer& InPayload, const EFlags PayloadFlags) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FVirtualizedUntypedBulkData::SerializeData);

	if (Ar.IsSaving()) // Saving to virtualized bulkdata format
	{
		for (const FSharedBuffer& Buffer : InPayload.GetCompressed().GetSegments())
		{
			// Const cast because FArchive requires a non-const pointer!
			Ar.Serialize(const_cast<void*>(Buffer.GetData()), static_cast<int64>(Buffer.GetSize()));
		}
		
		return true;
	}
	else if(Ar.IsLoading() && !EnumHasAnyFlags(PayloadFlags, EFlags::ReferencesLegacyFile)) // Loading from virtualized bulkdata format
	{
		InPayload = FCompressedBuffer::FromCompressed(Ar);
		return InPayload.IsNull();	
	}
	else if (Ar.IsLoading()) // Loading from old bulkdata format
	{
		const int64 Size = GetBulkDataSize();
		FUniqueBuffer LoadPayload = FUniqueBuffer::Alloc(Size);

		if (EnumHasAnyFlags(PayloadFlags, EFlags::LegacyFileIsCompressed))
		{
			Ar.SerializeCompressed(LoadPayload.GetData(), Size, NAME_Zlib, COMPRESS_NoFlags, false);
		}
		else
		{
			Ar.Serialize(LoadPayload.GetData(), Size);
		}

		InPayload = FCompressedBuffer::Compress(NAME_None, LoadPayload.MoveToShared());

		return true;
	}
	else
	{
		return false;
	}
}

void FVirtualizedUntypedBulkData::PushData()
{
	checkf(IsDataVirtualized() == false || Payload.IsNull(), TEXT("Cannot have a valid payload in memory if the payload is virtualized!")); // Sanity check

	// We only need to push if the payload if it actually has data and it is not 
	// currently virtualized (either we have an updated payload in memory or the 
	// payload is currently non-virtualized and stored on disk)
	if (!IsDataVirtualized() && GetPayloadSize() > 0)
	{ 
		TRACE_CPUPROFILER_EVENT_SCOPE(FVirtualizedUntypedBulkData::PushData);

		// We should only need to load from disk at this point if we are going from
		// a non-virtualized payload to a virtualized one. If the bulkdata is merely being
		// edited then we should have the payload in memory already and are just accessing a
		// reference to it.

		FCompressedBuffer PayloadToPush = GetDataInternal();
		// TODO: If the push fails we will end up potentially re-compressing this payload for
		// serialization, we need a better way to save the results of 'RecompressForSerialization'
		RecompressForSerialization(PayloadToPush, Flags);

		if (FVirtualizationManager::Get().PushData(PayloadContentId, PayloadToPush))
		{
			EnumAddFlags(Flags, EFlags::IsVirtualized);
			EnumRemoveFlags(Flags, EFlags::ReferencesLegacyFile | EFlags::LegacyFileIsCompressed);
			
			// Clear members associated with non-virtualized data and release the in-memory
			// buffer.
			PackagePath.Empty();
			PackageSegment = EPackageSegment::Header;
			OffsetInFile = INDEX_NONE;
		}
	}	
}

FCompressedBuffer FVirtualizedUntypedBulkData::PullData() const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FVirtualizedUntypedBulkData::PullData);

	FCompressedBuffer PulledPayload = FVirtualizationManager::Get().PullData(PayloadContentId);

	if (PulledPayload)
	{
		checkf(	PayloadSize == PulledPayload.GetRawSize(),
				TEXT("Mismatch between serialized length (%" INT64_FMT ") and virtualized data length (%" UINT64_FMT ")"),
				PayloadSize,
				PulledPayload.GetRawSize());
	}
	else
	{
		UE_LOG(LogVirtualization, Warning, TEXT("Failed to pull virtual data with guid (%s)"), *PayloadContentId.ToString());
	}

	return PulledPayload;
}

FPackagePath FVirtualizedUntypedBulkData::GetPackagePathFromOwner(UObject* Owner, EPackageSegment& OutPackageSegment) const
{
	OutPackageSegment = EPackageSegment::Header;
	if (Owner != nullptr)
	{
		UPackage* Package = Owner->GetOutermost();
		checkf(Package != nullptr, TEXT("Owner was not a valid UPackage!"));

		FLinkerLoad* Linker = FLinkerLoad::FindExistingLinkerForPackage(Package);
		checkf(Linker != nullptr, TEXT("UPackage did not have a valid FLinkerLoad!"));

		return Linker->GetPackagePath();
	}
	else
	{
		return FPackagePath();
	}
}

bool FVirtualizedUntypedBulkData::CanUnloadData() const
{
	// We cannot unload the data if are unable to reload it from a file
	return IsDataVirtualized() || PackagePath.IsEmpty() == false;
}

void FVirtualizedUntypedBulkData::Reset()
{
	// Note that we do not reset the BulkDataId
	PayloadContentId.Reset();
	Payload.Reset();
	PayloadSize = 0;
	CompressionFormatToUse = NAME_Default;
	OffsetInFile = INDEX_NONE;
	PackagePath.Empty();
	PackageSegment = EPackageSegment::Header;
	bWasKeyGuidDerived = false;
	Flags = EFlags::None;	
}

void FVirtualizedUntypedBulkData::UnloadData()
{
	if (CanUnloadData())
	{
		Payload.Reset();
	}
}

FGuid FVirtualizedUntypedBulkData::GetIdentifier() const
{
	if (GetPayloadSize() > 0)
	{
		checkf(BulkDataId.IsValid(), TEXT("If bulkdata has a valid payload then it should have a valid BulkDataId"));
		return BulkDataId;
	}
	else
	{
		return FGuid();
	}
}

FCompressedBuffer FVirtualizedUntypedBulkData::GetDataInternal() const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FVirtualizedUntypedBulkData::GetDataInternal);

	// Early out there isn't any data to actually load
	if (GetPayloadSize() == 0)
	{
		return FCompressedBuffer();
	}

	// Check if we already have the data in memory
	if (Payload)
	{
		return FCompressedBuffer::Compress(NAME_None, Payload);
	}

	if (IsDataVirtualized())
	{
		FCompressedBuffer Buffer = PullData();
		checkf(Payload.IsNull(), TEXT("Pulling data somehow assigned it to the bulk data object!")); //Make sure that we did not assign the buffer internally
		return Buffer;
	}
	else
	{
		FCompressedBuffer Buffer = LoadFromDisk();
		check(Payload.IsNull()); //Make sure that we did not assign the buffer internally
		return Buffer;
	}
}

TFuture<FSharedBuffer> FVirtualizedUntypedBulkData::GetPayload() const
{
	TPromise<FSharedBuffer> Promise;

	FSharedBuffer UncompressedPayload = GetDataInternal().Decompress();

	// TODO: Not actually async yet!
	Promise.SetValue(MoveTemp(UncompressedPayload));

	return Promise.GetFuture();
}

TFuture<FCompressedBuffer>FVirtualizedUntypedBulkData::GetCompressedPayload() const
{
	TPromise<FCompressedBuffer> Promise;

	FCompressedBuffer CompressedPayload = GetDataInternal();

	// TODO: Not actually async yet!
	Promise.SetValue(MoveTemp(CompressedPayload));

	return Promise.GetFuture();
}

void FVirtualizedUntypedBulkData::UpdatePayload(FSharedBuffer InPayload, FName InCompressionFormat)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FVirtualizedUntypedBulkData::UpdatePayload);

	UnloadData();

	// Make sure that we own the memory in the shared buffer
	Payload = InPayload.MakeOwned();
	PayloadSize = (int64)Payload.GetSize();
	
	EnumRemoveFlags(Flags,	EFlags::IsVirtualized | 
							EFlags::DisablePayloadCompression | 
							EFlags::ReferencesLegacyFile | 
							EFlags::LegacyFileIsCompressed);

	//TODO: Should we validate now or let FCompressedBuffer do that later?
	CompressionFormatToUse = InCompressionFormat;

	if (InCompressionFormat == NAME_None)
	{
		EnumAddFlags(Flags, EFlags::DisablePayloadCompression);
	}

	PackagePath.Empty();
	PackageSegment = EPackageSegment::Header;
	OffsetInFile = INDEX_NONE;

	if (!BulkDataId.IsValid() && PayloadSize > 0)
	{
		BulkDataId = FGuid::NewGuid();
	}

	PayloadContentId = FPayloadId(Payload);
}

FCustomVersionContainer FVirtualizedUntypedBulkData::GetCustomVersions(FArchive& InlineArchive)
{
	return InlineArchive.GetCustomVersions();
}

void FVirtualizedUntypedBulkData::UpdateKeyIfNeeded()
{
	// If this was created from old BulkData then the key is generated from an older FGuid, we
	// should recalculate it based off the payload to keep the key consistent in the future.
	if (bWasKeyGuidDerived)
	{
		checkf(IsDataVirtualized() == false, TEXT("Cannot have a virtualized payload if bWasCreatedFromOldBulkData is still true")); // Sanity check

		// Load the payload from disk (or memory) so that we can hash it
		FSharedBuffer InPayload = GetDataInternal().Decompress();
		PayloadContentId = FPayloadId(InPayload);

		// Store as the in memory payload, since this method is only called during saving 
		// we know it will get cleared anyway.
		Payload = InPayload;

		bWasKeyGuidDerived = false;
	}
}

void FVirtualizedUntypedBulkData::RecompressForSerialization(FCompressedBuffer& InOutPayload, EFlags PayloadFlags) const
{
	const FName CurrentMethod = InOutPayload.GetFormatName();

	FName TargetMethod;
	
	if (EnumHasAnyFlags(PayloadFlags, EFlags::DisablePayloadCompression))
	{
		TargetMethod = NAME_None;
	}
	else if (CompressionFormatToUse != NAME_Default)
	{
		check(!CompressionFormatToUse.IsNone()); // Should be caught by the DisablePayloadCompression flag
		TargetMethod = CompressionFormatToUse;
	}
	else
	{
		// TODO: Do we want to add more logic, min size etc?
		TargetMethod = NAME_LZ4;
	}
	
	if (TargetMethod != CurrentMethod) // No change in compression so we can just return it
	{
		FCompositeBuffer DecompressedBuffer = InOutPayload.DecompressToComposite();

		// If the buffer actually decompressed we can have both the compressed and the uncompressed version of the
		// payload in memory. Compressing it will create a third version so before doing that we should reset
		// the original compressed buffer in case that we can release it to reduce higher water mark pressure.
		InOutPayload.Reset();
		InOutPayload = FCompressedBuffer::Compress(TargetMethod, DecompressedBuffer);
	}
}

FVirtualizedUntypedBulkData::EFlags FVirtualizedUntypedBulkData::BuildFlagsForSerialization(FArchive& Ar) const
{
	if (Ar.IsSaving())
	{
		EFlags UpdatedFlags = Flags;

		// Now update any changes to the flags that we might need to make when serializing.
		// Note that these changes are not applied to the current object UNLESS we are saving
		// the package, in which case the newly modified flags will be applied once we confirm
		// that the package has saved.
		
		const FLinkerSave* LinkerSave = Cast<FLinkerSave>(Ar.GetLinker());
		if (LinkerSave != nullptr && !LinkerSave->GetFilename().IsEmpty() && ShouldSaveToPackageSidecar())
		{
			EnumAddFlags(UpdatedFlags, EFlags::HasPayloadSidecarFile);
		}
		else
		{
			EnumRemoveFlags(UpdatedFlags, EFlags::HasPayloadSidecarFile);
		}

		EnumRemoveFlags(UpdatedFlags,EFlags::ReferencesLegacyFile | EFlags::LegacyFileIsCompressed);

		return UpdatedFlags;
	}
	else
	{
		return Flags;
	}
}

} // namespace UE::Virtualization

//#endif //WITH_EDITORONLY_DATA
