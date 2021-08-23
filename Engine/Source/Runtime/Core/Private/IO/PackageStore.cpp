// Copyright Epic Games, Inc. All Rights Reserved.

#include "IO/PackageStore.h"
#include "Serialization/StructuredArchive.h"
#include "Serialization/CompactBinary.h"
#include "Serialization/CompactBinaryWriter.h"

FArchive& operator<<(FArchive& Ar, FPackageStoreExportInfo& ExportInfo)
{
	Ar << ExportInfo.ExportCount;
	Ar << ExportInfo.ExportBundleCount;

	return Ar;
}

FCbWriter& operator<<(FCbWriter& Writer, const FPackageStoreExportInfo& ExportInfo)
{
	Writer.BeginObject();
	Writer << "exportcount" << ExportInfo.ExportCount;
	Writer << "exportbundlecount" << ExportInfo.ExportBundleCount;
	Writer.EndObject();
	
	return Writer;
}

FPackageStoreExportInfo FPackageStoreExportInfo::FromCbObject(const FCbObject& Obj)
{
	FPackageStoreExportInfo ExportInfo;
	
	ExportInfo.ExportCount			= Obj["exportcount"].AsInt32();
	ExportInfo.ExportBundleCount	= Obj["exportbundlecount"].AsInt32();

	return ExportInfo;
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

FCbWriter& operator<<(FCbWriter& Writer, const FPackageStoreEntryResource& PackageStoreEntry)
{
	Writer.BeginObject();

	Writer << "flags" << static_cast<uint32>(PackageStoreEntry.Flags);
	Writer << "packagename" << PackageStoreEntry.PackageName.ToString();
	Writer << "sourcepackagename" << PackageStoreEntry.SourcePackageName.ToString();
	Writer << "region" << PackageStoreEntry.Region.ToString();
	Writer << "exportinfo" << PackageStoreEntry.ExportInfo;

	if (PackageStoreEntry.ImportedPackageIds.Num())
	{
		Writer.BeginArray("importedpackageids");
		for (const FPackageId& ImportedPackageId : PackageStoreEntry.ImportedPackageIds)
		{
			Writer << ImportedPackageId.Value();
		}
		Writer.EndArray();
	}

	if (PackageStoreEntry.ShaderMapHashes.Num())
	{
		Writer.BeginArray("shadermaphashes");
		for (const FSHAHash& ShaderMapHash : PackageStoreEntry.ShaderMapHashes)
		{
			Writer << ShaderMapHash.ToString();
		}
		Writer.EndArray();
	}

	Writer.EndObject();

	return Writer;
}

FPackageStoreEntryResource FPackageStoreEntryResource::FromCbObject(const FCbObject& Obj)
{
	FPackageStoreEntryResource Entry;

	Entry.Flags				= static_cast<EPackageStoreEntryFlags>(Obj["flags"].AsUInt32());
	Entry.PackageName		= FName(Obj["packagename"].AsString());
	Entry.SourcePackageName	= FName(Obj["sourcepackagename"].AsString());
	Entry.Region			= FName(Obj["region"].AsString());
	Entry.ExportInfo		= FPackageStoreExportInfo::FromCbObject(Obj["exportinfo"].AsObject());
	
	if (Obj["importedpackageids"])
	{
		for (FCbFieldView ArrayField : Obj["importedpackageids"])
		{
			Entry.ImportedPackageIds.Add(FPackageId::FromValue(ArrayField .AsUInt64()));
		}
	}
	
	if (Obj["shadermaphashes"])
	{
		for (FCbField& ArrayField : Obj["shadermaphashes"].AsArray())
		{
			FSHAHash& ShaderMapHash = Entry.ShaderMapHashes.AddDefaulted_GetRef();
			ShaderMapHash.FromString(FUTF8ToTCHAR(ArrayField.AsString()));
		}
	}

	return Entry;
}
