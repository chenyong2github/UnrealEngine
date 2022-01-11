// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/StringFwd.h"
#include "DerivedDataBackendInterface.h"

namespace UE::DerivedData::CacheStore::PakFile
{

class IPakFileDerivedDataBackend : public FDerivedDataBackendInterface
{
public:
	virtual void Close() = 0;
	virtual bool SaveCache() = 0;
	virtual bool LoadCache(const TCHAR* InFilename) = 0;
	virtual void MergeCache(IPakFileDerivedDataBackend* OtherPak) = 0;
	virtual const FString& GetFilename() const = 0;

	static bool SortAndCopy(const FString& InputFilename, const FString& OutputFilename);
};

} // UE::DerivedData::CacheStore::PakFile
