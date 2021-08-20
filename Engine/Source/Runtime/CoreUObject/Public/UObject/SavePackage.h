// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Containers/Set.h"
#include "Containers/Map.h"
#include "UObject/NameTypes.h"
#include "Serialization/FileRegions.h"
#include "Serialization/PackageWriter.h"
#include "Misc/DateTime.h"
#include "ObjectMacros.h"

#if !defined(UE_WITH_SAVEPACKAGE)
#	define UE_WITH_SAVEPACKAGE 1
#endif

class FArchive;
class FIoBuffer;
struct FObjectSaveContextData;
class FPackagePath;
class FSavePackageContext;
class FArchiveDiffMap;
class FOutputDevice;
class IPackageWriter;

/**
 * Struct to encapsulate arguments specific to saving one package
 */
struct FPackageSaveInfo
{
	class UPackage* Package = nullptr;
	class UObject* Asset = nullptr;
	FString Filename;
};

/**
 * Struct to encapsulate UPackage::Save arguments. 
 * These arguments are shared between packages when saving multiple packages concurrently.
 */
struct FSavePackageArgs
{
	class ITargetPlatform* TargetPlatform = nullptr;
	EObjectFlags TopLevelFlags = RF_NoFlags;
	uint32 SaveFlags = 0;
	bool bForceByteSwapping = false; // for FLinkerSave
	bool bWarnOfLongFilename = false;
	bool bSlowTask = true;
	FDateTime FinalTimeStamp;
	FOutputDevice* Error = nullptr;
	FArchiveDiffMap* DiffMap = nullptr;
	FSavePackageContext* SavePackageContext = nullptr;
};

class FSavePackageContext
{
public:
	FSavePackageContext(const ITargetPlatform* InTargetPlatform, IPackageWriter* InPackageWriter, bool InbForceLegacyOffsets)
	: TargetPlatform(InTargetPlatform)
	, PackageWriter(InPackageWriter) 
	, bForceLegacyOffsets(InbForceLegacyOffsets)
	{
		if (PackageWriter)
		{
			PackageWriterCapabilities = PackageWriter->GetCapabilities();
		}
	}

	COREUOBJECT_API ~FSavePackageContext();

	const ITargetPlatform* const TargetPlatform;
	IPackageWriter* const PackageWriter;
	IPackageWriter::FCapabilities PackageWriterCapabilities;
	const bool bForceLegacyOffsets;
};

namespace UE
{
namespace SavePackageUtilities
{
	/**
	 * Return whether the given save parameters indicate the LoadedPath of the package being saved should be updated.
	 * This allows us to update the in-memory package when it is saved in editor to match its new save file.
	 */
	COREUOBJECT_API bool IsUpdatingLoadedPath(bool bIsCooking, const FPackagePath& TargetPackagePath, uint32 SaveFlags);

	/**
	 * Return whether the given save parameters indicate the package is a procedural save.
	 * Any save without the the possibility of user-generated edits to the package is a procedural save (Cooking, EditorDomain).
	 * This allows us to execute transforms that only need to be executed in response to new user data.
	 */
	COREUOBJECT_API bool IsProceduralSave(bool bIsCooking, const FPackagePath& TargetPackagePath, uint32 SaveFlags);

	/** Call the PreSave function on the given object and log a warning if there is an incorrect override. */
	COREUOBJECT_API void CallPreSave(UObject* Object, FObjectSaveContextData& ObjectSaveContext);

	/** Call the PreSaveRoot function on the given object. */
	COREUOBJECT_API void CallPreSaveRoot(UObject* Object, FObjectSaveContextData& ObjectSaveContext);

	/** Call the PostSaveRoot function on the given object. */
	COREUOBJECT_API void CallPostSaveRoot(UObject* Object, FObjectSaveContextData& ObjectSaveContext, bool bCleanupRequired);

	/** Add any required TopLevelFlags based on the save parameters. */
	COREUOBJECT_API EObjectFlags NormalizeTopLevelFlags(EObjectFlags TopLevelFlags, bool bIsCooking);

}
}