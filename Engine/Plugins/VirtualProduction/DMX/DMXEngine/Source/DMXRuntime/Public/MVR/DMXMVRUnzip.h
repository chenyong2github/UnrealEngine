// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Serialization/ArrayReader.h"


/** Provides access to the zipped content of an MVR file */
class DMXRUNTIME_API FDMXMVRUnzip
{
public:
	/** Creates a zip reader from data. Returns nullptr if not a valid zip */
	static TSharedPtr<FDMXMVRUnzip> CreateFromData(const uint8* DataPtr, const int64 DataNum);

	/** Creates a zip reader from file. Returns nullptr if not a valid zip */
	static TSharedPtr<FDMXMVRUnzip> CreateFromFile(const FString& FilePathAndName);

	/** Gets the data of a file in the zip */
	bool GetFileContent(const FString& FilenameInZip, TArray64<uint8>& OutData);

	/** Returns true if the zip contains the file with name */
	bool Contains(const FString& FilenameInZip) const;

	/** Returns the first file that matches the extension */
	FString GetFirstFilenameByExtension(const FString& Extension) const;

	/** Helper to unzip a file within the zip as temp file. Deletes the file when running out of scope. */
	struct DMXRUNTIME_API FDMXScopedUnzipToTempFile
	{
		FDMXScopedUnzipToTempFile(const TSharedRef<FDMXMVRUnzip>& MVRUnzip, const FString& FilenameInZip);
		~FDMXScopedUnzipToTempFile();

		/** Absolute File Path and Name of the Temp File */
		FString TempFilePathAndName;
	};

private:
	/** Constructs itself from data. Returns true on success. */
	bool InitializeFromDataInternal(const uint8* DataPtr, const int64 DataNum);

	/** Initializes itself from a file. Returns true on success. */
	bool InitializeFromFileInternal(const FString& FilePathAndName);

	TMap<FString, uint32> OffsetsMap;
	FArrayReader Data;
};
