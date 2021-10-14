// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/SnapshotVersion.h"

#include "SnapshotCustomVersion.h"

#include "Misc/EngineVersion.h"
#include "Serialization/CustomVersion.h"

void FSnapshotFileVersionInfo::Initialize()
{
	FileVersion = GPackageFileUEVersion.FileVersionUE4;
	FileVersionUE5 = GPackageFileUEVersion.FileVersionUE5;
	FileVersionLicensee = GPackageFileLicenseeUEVersion;
}

void FSnapshotEngineVersionInfo::Initialize(const FEngineVersion& InVersion)
{
	Major = InVersion.GetMajor();
	Minor = InVersion.GetMinor();
	Patch = InVersion.GetPatch();
	Changelist = InVersion.GetChangelist();
}

void FSnapshotCustomVersionInfo::Initialize(const FCustomVersion& InVersion)
{
	FriendlyName = InVersion.GetFriendlyName();
	Key = InVersion.Key;
	Version = InVersion.Version;
}

void FSnapshotVersionInfo::Initialize(bool bWithoutSnapshotVersion)
{
	FileVersion.Initialize();
	EngineVersion.Initialize(FEngineVersion::Current());

	CustomVersions.Empty(CustomVersions.Num());
	FCustomVersionContainer AllCurrentVersions = FCurrentCustomVersions::GetAll();
	for (const FCustomVersion& EngineCustomVersion : AllCurrentVersions.GetAllVersions())
	{
		FSnapshotCustomVersionInfo& CustomVersion = CustomVersions.AddDefaulted_GetRef();
		if (!bWithoutSnapshotVersion || (bWithoutSnapshotVersion && CustomVersion.Key != FSnapshotCustomVersion::GUID))
		{
			CustomVersion.Initialize(EngineCustomVersion);
		}
	}
}

bool FSnapshotVersionInfo::IsInitialized() const
{
	return CustomVersions.Num() > 0;
}

void FSnapshotVersionInfo::ApplyToArchive(FArchive& Archive) const
{
	FPackageFileVersion UEVersion(FileVersion.FileVersion, (EUnrealEngineObjectUE5Version)FileVersion.FileVersionUE5);

		Archive.SetUEVer(UEVersion);
		Archive.SetLicenseeUEVer(FileVersion.FileVersionLicensee);
		Archive.SetEngineVer(FEngineVersionBase(EngineVersion.Major, EngineVersion.Minor, EngineVersion.Patch, EngineVersion.Changelist));

		FCustomVersionContainer EngineCustomVersions;
		for (const FSnapshotCustomVersionInfo& CustomVersion : CustomVersions)
		{
			EngineCustomVersions.SetVersion(CustomVersion.Key, CustomVersion.Version, CustomVersion.FriendlyName);
		}
		Archive.SetCustomVersions(EngineCustomVersions);
}

int32 FSnapshotVersionInfo::GetSnapshotCustomVersion() const
{
	for (const FSnapshotCustomVersionInfo& CustomVersion : CustomVersions)
	{
		if (CustomVersion.Key == FSnapshotCustomVersion::GUID)
		{
			return CustomVersion.Version;
		}
	}

	return INDEX_NONE;
}
