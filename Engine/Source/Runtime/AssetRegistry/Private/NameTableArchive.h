// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Linker.h"

/** Case sensitive hashing function for TMap */
template <typename ValueType>
struct TNameTableStringHash : BaseKeyFuncs<ValueType, FString, /*bInAllowDuplicateKeys*/false>
{
	static FORCEINLINE const FString& GetSetKey(const TPair<FString, ValueType>& Element)
	{
		return Element.Key;
	}
	static FORCEINLINE bool Matches(const FString& A, const FString& B)
	{
		return A.Equals(B, ESearchCase::CaseSensitive);
	}
	static FORCEINLINE uint32 GetKeyHash(const FString& Key)
	{
		return FCrc::StrCrc32<TCHAR>(*Key);
	}
};

/** 
 * Reader for an FNameTableArchive.  An FNameTableArchive is like a normal archive but with a separate FName table embedded in it, and
 * FNames in it are serialized as indices into the Name table.
*/
class FNameTableArchiveReader : public FArchive
{
public:
	/** Create a reader for a file on disk */
	FNameTableArchiveReader(int32 SerializationVersion, const FString& Filename);
	
	/** Create a reader that wraps around an existing archive. Existing archive must support seek */
	FNameTableArchiveReader(FArchive& WrappedArchive);

	~FNameTableArchiveReader();
	
	// Farchive implementation to redirect requests to the wrapped archive
	virtual void Serialize(void* V, int64 Length) override;
	virtual bool Precache(int64 PrecacheOffset, int64 PrecacheSize) override;
	virtual void Seek(int64 InPos) override;
	virtual int64 Tell() override;
	virtual int64 TotalSize() override;
	virtual const FCustomVersionContainer& GetCustomVersions() const override;
	virtual void SetCustomVersions(const FCustomVersionContainer& NewVersions) override;
	virtual void ResetCustomVersions() override;
	virtual FArchive& operator<<(FName& Name);

private:
	/** Serializers for different package maps */
	bool SerializeNameMap();

	FArchive* ProxyAr;
	FArchive* FileAr;
	TArray<FNameEntryId> NameMap;
};

/**
 * Writer for an FNameTableArchive.  See the class comment on FNameTableArchiveReader.
 */
class FNameTableArchiveWriter : public FArchive
{
public:
	/** Create a writer for a file on disk */
	FNameTableArchiveWriter(int32 SerializationVersion, const FString& Filename);

	/** Create a writer that wraps around an existing archive. Existing archive must support seek */
	FNameTableArchiveWriter(FArchive& WrappedArchive);

	~FNameTableArchiveWriter();

	// Farchive implementation to redirect requests to the wrapped archive
	virtual void Serialize(void* V, int64 Length) override;
	virtual bool Precache(int64 PrecacheOffset, int64 PrecacheSize) override;
	virtual void Seek(int64 InPos) override;
	virtual int64 Tell() override;
	virtual int64 TotalSize() override;
	virtual const FCustomVersionContainer& GetCustomVersions() const override;
	virtual void SetCustomVersions(const FCustomVersionContainer& NewVersions) override;
	virtual void ResetCustomVersions() override;
	virtual FArchive& operator<<(FName& Name);

private:
	/** Serializers for different package maps */
	void SerializeNameMap();

	FArchive* ProxyAr;
	FArchive* FileAr;
	FString FinalFilename;
	FString TempFilename;
	TMap<FNameEntryId, int32> NameMap;
	int64 NameOffsetLoc;
};
