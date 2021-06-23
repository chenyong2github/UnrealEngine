// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/SnapshotVersion.h"
#include "Misc/EngineVersion.h"
#include "Serialization/CustomVersion.h"

void FSnapshotFileVersionInfo::Initialize()
{
	FileVersion = GPackageFileUEVersion;
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

void FSnapshotVersionInfo::Initialize()
{
	FileVersion.Initialize();
	EngineVersion.Initialize(FEngineVersion::Current());

	CustomVersions.Empty(CustomVersions.Num());
	FCustomVersionContainer AllCurrentVersions = FCurrentCustomVersions::GetAll();
	for (const FCustomVersion& EngineCustomVersion : AllCurrentVersions.GetAllVersions())
	{
		FSnapshotCustomVersionInfo& CustomVersion = CustomVersions.AddDefaulted_GetRef();
		CustomVersion.Initialize(EngineCustomVersion);
	}
}

bool FSnapshotVersionInfo::IsInitialized() const
{
	return CustomVersions.Num() > 0;
}

void FSnapshotVersionInfo::ApplyToArchive(FArchive& Archive) const
{
	if (ensure(Archive.IsLoading()))
	{
		Archive.SetUEVer(FileVersion.FileVersion);
		Archive.SetLicenseeUEVer(FileVersion.FileVersionLicensee);
		Archive.SetEngineVer(FEngineVersionBase(EngineVersion.Major, EngineVersion.Minor, EngineVersion.Patch, EngineVersion.Changelist));

		FCustomVersionContainer EngineCustomVersions;
		for (const FSnapshotCustomVersionInfo& CustomVersion : CustomVersions)
		{
			EngineCustomVersions.SetVersion(CustomVersion.Key, CustomVersion.Version, CustomVersion.FriendlyName);
		}
		Archive.SetCustomVersions(EngineCustomVersions);
	}
}
