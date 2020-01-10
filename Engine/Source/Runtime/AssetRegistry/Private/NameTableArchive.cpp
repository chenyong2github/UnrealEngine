// Copyright Epic Games, Inc. All Rights Reserved.

#include "NameTableArchive.h"
#include "HAL/FileManager.h"
#include "AssetRegistryPrivate.h"

class FNameTableErrorArchive : public FArchive
{
public:
	FNameTableErrorArchive()
	{
		SetError();
	}
};
static FNameTableErrorArchive GNameTableErrorArchive;

FNameTableArchiveReader::FNameTableArchiveReader(int32 SerializationVersion, const FString& Filename)
{
	this->SetIsLoading(true);

	FileAr = IFileManager::Get().CreateFileReader(*Filename, FILEREAD_Silent);
	if (FileAr && !FileAr->IsError() && FileAr->TotalSize() > 0)
	{
		ProxyAr = FileAr;

		int32 MagicNumber = 0;
		*this << MagicNumber;

		if (!IsError() && MagicNumber == PACKAGE_FILE_TAG)
		{
			int32 VersionNumber = 0;
			*this << VersionNumber;

			if (!IsError() && VersionNumber == SerializationVersion)
			{
				if (SerializeNameMap())
				{
					// Successfully loaded
					return;
				}
			}
		}
	}
	
	// If we got here it failed to load properly
	ProxyAr = &GNameTableErrorArchive;
	SetError();
}

FNameTableArchiveReader::FNameTableArchiveReader(FArchive& WrappedArchive)
	: FArchive()
	, ProxyAr(&WrappedArchive)
	, FileAr(nullptr)
{
	this->SetIsLoading(true);

	if (!SerializeNameMap())
	{
		ProxyAr = &GNameTableErrorArchive;
		SetError();
	}
}

FNameTableArchiveReader::~FNameTableArchiveReader()
{
	delete FileAr;
	FileAr = nullptr;
}

bool FNameTableArchiveReader::SerializeNameMap()
{
	int64 NameOffset = 0;
	*this << NameOffset;

	if (NameOffset > TotalSize())
	{
		// The file was corrupted. Return false to fail to load the cache and thus regenerate it.
		return false;
	}

	if( NameOffset > 0 )
	{
		int64 OriginalOffset = Tell();
		ProxyAr->Seek( NameOffset );

		int32 NameCount = 0;
		*this << NameCount;
		if (IsError() || NameCount < 0)
		{
			return false;
		}
		
		const int32 MinFNameEntrySize = sizeof(int32);
		int32 MaxReservation = (TotalSize() - Tell()) / MinFNameEntrySize;
		NameMap.Reserve(FMath::Min(NameCount, MaxReservation));
		for ( int32 NameMapIdx = 0; NameMapIdx < NameCount; ++NameMapIdx )
		{
			// Read the name entry from the file.
			FNameEntrySerialized NameEntry(ENAME_LinkerConstructor);
			*this << NameEntry;

			if (IsError())
			{
				return false;
			}

			NameMap.Add(FName(NameEntry).GetDisplayIndex());
		}

		ProxyAr->Seek(OriginalOffset);
	}

	return true;
}

void FNameTableArchiveReader::Serialize(void* V, int64 Length)
{
	ProxyAr->Serialize(V, Length);

	if (ProxyAr->IsError())
	{
		ProxyAr = &GNameTableErrorArchive;
		SetError();
	}
}

bool FNameTableArchiveReader::Precache(int64 PrecacheOffset, int64 PrecacheSize)
{
	if (!IsError())
	{
		return ProxyAr->Precache(PrecacheOffset, PrecacheSize);
	}

	return false;
}

void FNameTableArchiveReader::Seek(int64 InPos)
{
	if (!IsError())
	{
		ProxyAr->Seek(InPos);
	}
}

int64 FNameTableArchiveReader::Tell()
{
	return ProxyAr->Tell();
}

int64 FNameTableArchiveReader::TotalSize()
{
	return ProxyAr->TotalSize();
}

const FCustomVersionContainer& FNameTableArchiveReader::GetCustomVersions() const
{
	return ProxyAr->GetCustomVersions();
}

void FNameTableArchiveReader::SetCustomVersions(const FCustomVersionContainer& NewVersions)
{
	ProxyAr->SetCustomVersions(NewVersions);
}

void FNameTableArchiveReader::ResetCustomVersions()
{
	ProxyAr->ResetCustomVersions();
}

FArchive& FNameTableArchiveReader::operator<<(FName& OutName)
{
	int32 NameIndex;
	FArchive& Ar = *this;
	Ar << NameIndex;

	if (NameMap.IsValidIndex(NameIndex))
	{
		FNameEntryId MappedName = NameMap[NameIndex];

		int32 Number;
		Ar << Number;

		OutName = FName::CreateFromDisplayId(MappedName, MappedName ? Number : 0);
	}
	else
	{
		UE_LOG(LogAssetRegistry, Error, TEXT("Bad name index reading cache %i/%i"), NameIndex, NameMap.Num());

		ProxyAr = &GNameTableErrorArchive;
		SetError();

		OutName = FName();
	}

	return *this;
}

FNameTableArchiveWriter::FNameTableArchiveWriter(int32 SerializationVersion, const FString& Filename)
	: FArchive()
	, ProxyAr(nullptr)
	, FileAr(nullptr)
	, FinalFilename(Filename)
	, TempFilename(Filename + TEXT(".tmp"))
{
	this->SetIsSaving(true);

	// Save to a temp file first, then move to the destination to avoid corruption
	FileAr = IFileManager::Get().CreateFileWriter(*TempFilename, 0);
	if (FileAr)
	{
		ProxyAr = FileAr;

		int32 MagicNumber = PACKAGE_FILE_TAG;
		*this << MagicNumber;

		int32 VersionToWrite = SerializationVersion;
		*this << VersionToWrite;

		// Just write a 0 for the name table offset for now. This will be overwritten later when we are done serializing
		NameOffsetLoc = Tell();
		int64 NameOffset = 0;
		*this << NameOffset;
	}
	else
	{
		UE_LOG(LogAssetRegistry, Error, TEXT("Failed to open file for write %s"), *Filename);
		SetError();
	}
}

FNameTableArchiveWriter::FNameTableArchiveWriter(FArchive& WrappedArchive)
	: FArchive()
	, ProxyAr(&WrappedArchive)
	, FileAr(nullptr)
{
	this->SetIsSaving(true);

	// Just write a 0 for the name table offset for now. This will be overwritten later when we are done serializing
	NameOffsetLoc = Tell();
	int64 NameOffset = 0;
	*this << NameOffset;
}

FNameTableArchiveWriter::~FNameTableArchiveWriter()
{
	if (ProxyAr)
	{
		int64 ActualNameOffset = Tell();
		SerializeNameMap();

		int64 EndOffset = Tell();
		Seek(NameOffsetLoc);
		*this << ActualNameOffset;
		Seek(EndOffset);
	}

	if (FileAr)
	{
		delete FileAr;
		FileAr = nullptr;

		IFileManager::Get().Move(*FinalFilename, *TempFilename);
	}
}

void FNameTableArchiveWriter::SerializeNameMap()
{
	int32 NameCount = NameMap.Num();
	*this << NameCount;
	if( NameCount > 0 )
	{
		// Must still be sorted in add order
		int32 NameMapIdx = 0;
		for (auto& Pair : NameMap)
		{
			check(NameMapIdx == Pair.Value);
			FName::GetEntry(Pair.Key)->Write(*this);
			NameMapIdx++;
		}
	}
}

void FNameTableArchiveWriter::Serialize( void* V, int64 Length )
{
	if (ProxyAr)
	{
		ProxyAr->Serialize( V, Length );

		if (ProxyAr->IsError())
		{
			SetError();
		}
	}

}

bool FNameTableArchiveWriter::Precache( int64 PrecacheOffset, int64 PrecacheSize )
{
	if (ProxyAr)
	{
		return ProxyAr->Precache( PrecacheOffset, PrecacheSize );
	}
	
	return false;
}

void FNameTableArchiveWriter::Seek( int64 InPos )
{
	if (ProxyAr)
	{
		ProxyAr->Seek( InPos );
	}
}

int64 FNameTableArchiveWriter::Tell()
{
	if (ProxyAr)
	{
		return ProxyAr->Tell();
	}

	return 0.f;
}

int64 FNameTableArchiveWriter::TotalSize()
{
	if (ProxyAr)
	{
		return ProxyAr->TotalSize();
	}

	return 0.f;
}

const FCustomVersionContainer& FNameTableArchiveWriter::GetCustomVersions() const
{
	if (ProxyAr)
	{
		return ProxyAr->GetCustomVersions();
	}
	return FArchive::GetCustomVersions();
}

void FNameTableArchiveWriter::SetCustomVersions(const FCustomVersionContainer& NewVersions)
{
	if (ProxyAr)
	{
		ProxyAr->SetCustomVersions(NewVersions);
	}
}

void FNameTableArchiveWriter::ResetCustomVersions()
{
	if (ProxyAr)
	{
		ProxyAr->ResetCustomVersions();
	}
}

FArchive& FNameTableArchiveWriter::operator<<( FName& Name )
{
	int32* NameIndexPtr = NameMap.Find(Name.GetDisplayIndex());
	int32 NameIndex = NameIndexPtr ? *NameIndexPtr : INDEX_NONE;
	if ( NameIndex == INDEX_NONE )
	{
		NameIndex = NameMap.Num();
		NameMap.Add(Name.GetDisplayIndex(), NameIndex);
	}

	FArchive& Ar = *this;
	Ar << NameIndex;

	if (Name == NAME_None)
	{
		int32 TempNumber = 0;
		Ar << TempNumber;
	}
	else
	{
		int32 Number = Name.GetNumber();
		Ar << Number;
	}

	return *this;
}
