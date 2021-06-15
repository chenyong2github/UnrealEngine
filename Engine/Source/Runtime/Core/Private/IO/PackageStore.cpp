// Copyright Epic Games, Inc. All Rights Reserved.

#include "IO/PackageStore.h"
#include "Serialization/StructuredArchive.h"

FArchive& operator<<(FArchive& Ar, FPackageStoreExportInfo& ExportInfo)
{
	Ar << ExportInfo.ExportBundlesSize;
	Ar << ExportInfo.ExportCount;
	Ar << ExportInfo.ExportBundleCount;
	Ar << ExportInfo.LoadOrder;

	return Ar;
}

FArchive& operator<<(FArchive& Ar, FPackageStoreEntryResource& PackageStoreEntry)
{
	uint32 Flags = static_cast<uint32>(PackageStoreEntry.Flags);

	Ar << Flags;
	Ar << PackageStoreEntry.PackageName;
	Ar << PackageStoreEntry.SourcePackageName;
	Ar << PackageStoreEntry.Region;
	Ar << PackageStoreEntry.ExportInfo;
	Ar << PackageStoreEntry.ImportedPackageIds;

	if (Ar.IsLoading())
	{
		PackageStoreEntry.Flags = static_cast<EPackageStoreEntryFlags>(Flags);
	}

	return Ar;
}
