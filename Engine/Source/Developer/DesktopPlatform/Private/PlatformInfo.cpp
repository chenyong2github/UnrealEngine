// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlatformInfo.h"
#include "DesktopPlatformPrivate.h"
#include "Misc/DataDrivenPlatformInfoRegistry.h"
#include "HAL/FileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Misc/DelayedAutoRegister.h"

#define LOCTEXT_NAMESPACE "PlatformInfo"

#if DDPI_HAS_EXTENDED_PLATFORMINFO_DATA

namespace PlatformInfo
{
	TArray<FName> AllPlatformGroupNames;
	TArray<FName> AllVanillaPlatformNames;

namespace
{

TArray<FTargetPlatformInfo*> AllPlatformInfoArray;
TArray<FTargetPlatformInfo*> VanillaPlatformInfoArray;


// we don't need any of this without the editor, although we would ideally not even compile this outside of the editor
// @todo platplug: Figure out why this is compiled on target devices

void BuildPlatformInfo(FName InPlatformInfoName, FName InTargetPlatformName, const FText& InDisplayName, const EBuildTargetType InPlatformType, 
	const EPlatformFlags::Flags InPlatformFlags, FName InIniPlatformName, FName InUBTPlatformName)
{
	// some verification before we create the object
	FName VanillaName = NAME_None;
	FName FlavorName = NAME_None;
	FTargetPlatformInfo* VanillaInfo = nullptr;
	// See if this contains a flavor
	if (InPlatformFlags & EPlatformFlags::CookFlavor)
	{
		FString InPlatformInfoNameString = InPlatformInfoName.ToString();
		int32 UnderscoreLoc;
		if (InPlatformInfoNameString.FindChar(TEXT('_'), UnderscoreLoc))
		{
			// removing VanillaPlatformName from the TP info means we need to verify, as it assumed the vanilla platform name is 
			// if this assumption breaks, then we can store a private copy of this name to find the vanilla platform later
			VanillaName = *InPlatformInfoNameString.Mid(0, UnderscoreLoc);
			FlavorName = *InPlatformInfoNameString.Mid(UnderscoreLoc + 1);

			FTargetPlatformInfo** FoundInfo = AllPlatformInfoArray.FindByPredicate([VanillaName](const FTargetPlatformInfo* Item) -> bool
			{
				return Item->PlatformInfoName == VanillaName;
			});

			VanillaInfo = FoundInfo ? *FoundInfo : nullptr;
		}

		// make sure it was good
		if (VanillaInfo == nullptr)
		{
			UE_LOG(LogDesktopPlatform, Error, TEXT("TargetPlatformInfo %s is a flavor, but wasn't in the form Parent_Flavor (Parent needs to be specified before flavor in the .ini file)"), *InPlatformInfoName.ToString());
			return;
		}
	}

	// create the platform info
	FTargetPlatformInfo* PlatformInfo = new FTargetPlatformInfo();
	AllPlatformInfoArray.Add(PlatformInfo);

	PlatformInfo->PlatformInfoName = InPlatformInfoName;
	PlatformInfo->TargetPlatformName = InTargetPlatformName;
	PlatformInfo->DisplayName = InDisplayName;
	PlatformInfo->PlatformType = InPlatformType;
	PlatformInfo->PlatformFlags = InPlatformFlags;
	PlatformInfo->IniPlatformName = InIniPlatformName;
	PlatformInfo->PlatformFlavor = FlavorName;

	// add this object to either a parent, or the list of vanilla platforms
	if (VanillaInfo != nullptr)
	{
		PlatformInfo->VanillaInfo = VanillaInfo;
		VanillaInfo->Flavors.Add(PlatformInfo);
	}
	else
	{
		// if we are vanilla, then point to ourself (this way we can always just use VanillaInfo without checking for null)
		PlatformInfo->VanillaInfo = PlatformInfo;

		VanillaPlatformInfoArray.Add(PlatformInfo);
		PlatformInfo::AllVanillaPlatformNames.AddUnique(InPlatformInfoName);
	}

	// build up used platform group names
	PlatformInfo->DataDrivenPlatformInfo = &FDataDrivenPlatformInfoRegistry::GetPlatformInfo(InIniPlatformName.ToString());
	if (PlatformInfo->DataDrivenPlatformInfo->PlatformGroupName != NAME_None)
	{
		PlatformInfo::AllPlatformGroupNames.AddUnique(PlatformInfo->DataDrivenPlatformInfo->PlatformGroupName);
	}
}

// Gets a string from a section, or empty string if it didn't exist
FString GetSectionString(const FConfigSection& Section, FName Key)
{
	// look for a value prefixed with host:
	FName HostKey = *FString::Printf(TEXT("%s:%s"), ANSI_TO_TCHAR(FPlatformProperties::IniPlatformName()), *Key.ToString());
	const FConfigValue* HostValue = Section.Find(HostKey);
	return HostValue ? HostValue->GetValue() : Section.FindRef(Key).GetValue();
}

// Gets a bool from a section, or false if it didn't exist
bool GetSectionBool(const FConfigSection& Section, FName Key)
{
	return FCString::ToBool(*GetSectionString(Section, Key));
}

EPlatformFlags::Flags ConvertPlatformFlags(const FString& String)
{
	if (String == TEXT("") || String == TEXT("None")) { return EPlatformFlags::None; }
	if (String == TEXT("CookFlavor")) { return EPlatformFlags::CookFlavor; }
	if (String == TEXT("BuildFlavor")) { return EPlatformFlags::BuildFlavor; }

	UE_LOG(LogInit, Fatal, TEXT("Unknown platform flag %s in PlatformInfo"), *String);
	return EPlatformFlags::None;
}

namespace EPlatformSection
{
	const FName TargetPlatformName = FName(TEXT("TargetPlatformName"));
	const FName DisplayName = FName(TEXT("DisplayName"));
	const FName PlatformType = FName(TEXT("PlatformType"));
	const FName PlatformFlags = FName(TEXT("PlatformFlags"));
	const FName UATCommandLine = FName(TEXT("UATCommandLine"));
	const FName UBTPlatformName = FName(TEXT("UBTPlatformName"));
}

void ParseDataDrivenPlatformInfo(const TCHAR* Name, const FConfigSection& Section, FName IniPlatformName)
{
	FName TargetPlatformName = *GetSectionString(Section, EPlatformSection::TargetPlatformName);
	FString DisplayName = GetSectionString(Section, EPlatformSection::DisplayName);
	FString PlatformType = GetSectionString(Section, EPlatformSection::PlatformType);
	FString PlatformFlags = GetSectionString(Section, EPlatformSection::PlatformFlags);
	FString UATCommandLine = GetSectionString(Section, EPlatformSection::UATCommandLine);
	FString UBTTargetValue = GetSectionString(Section, EPlatformSection::UBTPlatformName);
	// in almost all cases the UBT platform name matches the IniPlatformName, so it's optional
	FName UBTPlatformName = UBTTargetValue.Len() == 0 ? IniPlatformName : FName(*UBTTargetValue);

	EBuildTargetType TargetType;
	if (!LexTryParseString(TargetType, *PlatformType))
	{
		TargetType = EBuildTargetType::Unknown;
	}

	BuildPlatformInfo(Name, TargetPlatformName, FText::FromString(DisplayName), TargetType, ConvertPlatformFlags(PlatformFlags), IniPlatformName, UBTPlatformName);
}

void LoadDataDrivenPlatforms()
{
	// look for the standard DataDriven ini files
	int32 NumDDInfoFiles = FDataDrivenPlatformInfoRegistry::GetNumDataDrivenIniFiles();
	for (int32 Index = 0; Index < NumDDInfoFiles; Index++)
	{
		FConfigFile IniFile;
		FString PlatformName;

		FDataDrivenPlatformInfoRegistry::LoadDataDrivenIniFile(Index, IniFile, PlatformName);

		FName PlatformFName = *PlatformName;

		// now walk over the file, looking for ShaderPlatformInfo sections
		for (auto Section : IniFile)
		{
			if (Section.Key.StartsWith(TEXT("PlatformInfo ")))
			{
				const FString& SectionName = Section.Key;
				ParseDataDrivenPlatformInfo(*SectionName.Mid(13), Section.Value, PlatformFName);
			}
		}
	}
}

FDelayedAutoRegisterHelper GPlatformInfoInit(EDelayedRegisterRunPhase::FileSystemReady, []()
{
	LoadDataDrivenPlatforms();
});


} // anonymous namespace

const FTargetPlatformInfo* FindPlatformInfo(const FName& InPlatformName)
{
	for(const FTargetPlatformInfo* PlatformInfo : AllPlatformInfoArray)
	{
		if(PlatformInfo->PlatformInfoName == InPlatformName)
		{
			return PlatformInfo;
		}
	}

	UE_LOG(LogDesktopPlatform, Warning, TEXT("Unable to find platform info for '%s'"), *InPlatformName.ToString());

	return nullptr;
}

const FTargetPlatformInfo* FindVanillaPlatformInfo(const FName& InPlatformName)
{
	const FTargetPlatformInfo* const FoundInfo = FindPlatformInfo(InPlatformName);
	return FoundInfo ? FoundInfo->VanillaInfo : nullptr;
}

void UpdatePlatformDisplayName(FString InPlatformName, FText InDisplayName)
{
	for (FTargetPlatformInfo* PlatformInfo : AllPlatformInfoArray)
	{
		if (PlatformInfo->TargetPlatformName == FName(*InPlatformName))
		{
			PlatformInfo->DisplayName = InDisplayName;
		}
	}
}

const TArray<FTargetPlatformInfo*>& GetPlatformInfoArray()
{
	return AllPlatformInfoArray;
}

const TArray<FTargetPlatformInfo*>& GetVanillaPlatformInfoArray()
{
	return VanillaPlatformInfoArray;
}

const TArray<FName>& GetAllPlatformGroupNames()
{
	return PlatformInfo::AllPlatformGroupNames;
}

const TArray<FName>& GetAllVanillaPlatformNames()
{
	return PlatformInfo::AllVanillaPlatformNames;
}

} // namespace PlatformInfo

#endif


#undef LOCTEXT_NAMESPACE
