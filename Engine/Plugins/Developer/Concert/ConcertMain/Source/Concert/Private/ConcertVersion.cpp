// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ConcertVersion.h"
#include "Misc/EngineVersion.h"
#include "Serialization/CustomVersion.h"

#define LOCTEXT_NAMESPACE "ConcertVersion"

namespace ConcertVersionUtil
{

bool ValidateVersion(const int32 InCurrent, const int32 InOther, const FText InVersionDisplayName, const EConcertVersionValidationMode InValidationMode, FText* OutFailureReason)
{
	switch (InValidationMode)
	{
	case EConcertVersionValidationMode::Identical:
		if (InOther != InCurrent)
		{
			if (OutFailureReason)
			{
				*OutFailureReason = FText::Format(LOCTEXT("Error_InvalidIdenticalVersionFmt", "Invalid version for '{0}' (expected '{1}', got '{2}')"), InVersionDisplayName, InCurrent, InOther);
			}
			return false;
		}
		break;

	case EConcertVersionValidationMode::Compatible:
		if (InOther < InCurrent)
		{
			if (OutFailureReason)
			{
				*OutFailureReason = FText::Format(LOCTEXT("Error_InvalidCompatibleVersionFmt", "Invalid version for '{0}' (expected '{1}' or greater, got '{2}')"), InVersionDisplayName, InCurrent, InOther);
			}
			return false;
		}
		break;

	default:
		checkf(false, TEXT("Unknown EConcertVersionValidationMode!"));
		break;
	}
	
	return true;
}

} // namespace ConcertVersionUtil


void FConcertFileVersionInfo::Initialize()
{
	FileVersionUE4 = GPackageFileUE4Version;
	FileVersionLicenseeUE4 = GPackageFileLicenseeUE4Version;
}

bool FConcertFileVersionInfo::Validate(const FConcertFileVersionInfo& InOther, const EConcertVersionValidationMode InValidationMode, FText* OutFailureReason) const
{
	return ConcertVersionUtil::ValidateVersion(FileVersionUE4, InOther.FileVersionUE4, LOCTEXT("PackageVersionName", "Package Version"), InValidationMode, OutFailureReason)
		&& ConcertVersionUtil::ValidateVersion(FileVersionLicenseeUE4, InOther.FileVersionLicenseeUE4, LOCTEXT("LicenseePackageVersionName", "Licensee Package Version"), InValidationMode, OutFailureReason);
}


void FConcertEngineVersionInfo::Initialize(const FEngineVersion& InVersion)
{
	Major = InVersion.GetMajor();
	Minor = InVersion.GetMinor();
	Patch = InVersion.GetPatch();
	Changelist = InVersion.GetChangelist();
}

bool FConcertEngineVersionInfo::Validate(const FConcertEngineVersionInfo& InOther, const EConcertVersionValidationMode InValidationMode, FText* OutFailureReason) const
{
	return ConcertVersionUtil::ValidateVersion(Major, InOther.Major, LOCTEXT("MajorEngineVersionName", "Major Engine Version"), InValidationMode, OutFailureReason)
		&& ConcertVersionUtil::ValidateVersion(Minor, InOther.Minor, LOCTEXT("MinorEngineVersionName", "Minor Engine Version"), InValidationMode, OutFailureReason)
		&& ConcertVersionUtil::ValidateVersion(Patch, InOther.Patch, LOCTEXT("PatchEngineVersionName", "Patch Engine Version"), InValidationMode, OutFailureReason)
		&& ConcertVersionUtil::ValidateVersion(Changelist, InOther.Changelist, LOCTEXT("ChangelistEngineVersionName", "Changelist Engine Version"), InValidationMode, OutFailureReason);
}


void FConcertCustomVersionInfo::Initialize(const FCustomVersion& InVersion)
{
	Key = InVersion.Key;
	Version = InVersion.Version;
}

bool FConcertCustomVersionInfo::Validate(const FConcertCustomVersionInfo& InOther, const EConcertVersionValidationMode InValidationMode, FText* OutFailureReason) const
{
	check(Key == InOther.Key);
	const FCustomVersion* EngineCustomVersion = FCustomVersionContainer::GetRegistered().GetVersion(Key);
	return ConcertVersionUtil::ValidateVersion(Version, InOther.Version, EngineCustomVersion ? FText::AsCultureInvariant(EngineCustomVersion->GetFriendlyName().ToString()) : FText::AsCultureInvariant(Key.ToString()), InValidationMode, OutFailureReason);
}


void FConcertSessionVersionInfo::Initialize()
{
	FileVersion.Initialize();
	CompatibleEngineVersion.Initialize(FEngineVersion::CompatibleWith());

	for (const FCustomVersion& EngineCustomVersion : FCustomVersionContainer::GetRegistered().GetAllVersions())
	{
		FConcertCustomVersionInfo& CustomVersion = CustomVersions.AddDefaulted_GetRef();
		CustomVersion.Initialize(EngineCustomVersion);
	}
}

bool FConcertSessionVersionInfo::Validate(const FConcertSessionVersionInfo& InOther, const EConcertVersionValidationMode InValidationMode, FText* OutFailureReason) const
{
	if (!FileVersion.Validate(InOther.FileVersion, InValidationMode, OutFailureReason))
	{
		return false;
	}
	
	if (!CompatibleEngineVersion.Validate(InOther.CompatibleEngineVersion, InValidationMode, OutFailureReason))
	{
		return false;
	}

	for (const FConcertCustomVersionInfo& CustomVersion : CustomVersions)
	{
		const FConcertCustomVersionInfo* OtherCustomVersion = InOther.CustomVersions.FindByPredicate([&CustomVersion](const FConcertCustomVersionInfo& PotentialCustomVersion)
		{
			return PotentialCustomVersion.Key == CustomVersion.Key;
		});

		if (!OtherCustomVersion)
		{
			if (OutFailureReason)
			{
				const FCustomVersion* EngineCustomVersion = FCustomVersionContainer::GetRegistered().GetVersion(CustomVersion.Key);
				*OutFailureReason = FText::Format(LOCTEXT("Error_MissingVersionFmt", "Invalid version for '{0}' (expected '{1}', got '<none>')"), EngineCustomVersion ? FText::AsCultureInvariant(EngineCustomVersion->GetFriendlyName().ToString()) : FText::AsCultureInvariant(CustomVersion.Key.ToString()), CustomVersion.Version);
			}
			return false;
		}

		check(OtherCustomVersion && CustomVersion.Key == OtherCustomVersion->Key);
		if (!CustomVersion.Validate(*OtherCustomVersion, InValidationMode, OutFailureReason))
		{
			return false;
		}
	}

	if (InValidationMode == EConcertVersionValidationMode::Identical && InOther.CustomVersions.Num() > CustomVersions.Num())
	{
		// The identical check also requires that there are no extra versions (missing versions would have been caught by the loop above)
		// We only need to bother figuring out which version is extra if we're reporting back error information, as this is already an error condition
		if (OutFailureReason)
		{
			for (const FConcertCustomVersionInfo& OtherCustomVersion : InOther.CustomVersions)
			{
				const FConcertCustomVersionInfo* CustomVersion = CustomVersions.FindByPredicate([&OtherCustomVersion](const FConcertCustomVersionInfo& PotentialCustomVersion)
				{
					return PotentialCustomVersion.Key == OtherCustomVersion.Key;
				});

				if (!CustomVersion)
				{
					const FCustomVersion* EngineCustomVersion = FCustomVersionContainer::GetRegistered().GetVersion(OtherCustomVersion.Key);
					*OutFailureReason = FText::Format(LOCTEXT("Error_ExtraCustomVersionFmt", "Invalid version for '{0}' (expected '<none>', got '{1}')"), EngineCustomVersion ? FText::AsCultureInvariant(EngineCustomVersion->GetFriendlyName().ToString()) : FText::AsCultureInvariant(OtherCustomVersion.Key.ToString()), OtherCustomVersion.Version);
					break;
				}
			}
		}
		return false;
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
