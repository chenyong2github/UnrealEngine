// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/DataDrivenPlatformInfoRegistry.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/ConfigCacheIni.h"
#include "Modules/ModuleManager.h"

#include "Async/Async.h"
#include "Misc/MonitoredProcess.h"

namespace 
{
	TMap<FName, FDataDrivenPlatformInfo> DataDrivenPlatforms;
	TArray<FName> SortedPlatformNames;
	TArray<const FDataDrivenPlatformInfo*> SortedPlatformInfos;
	FCriticalSection DDPILocker;
}

#if DDPI_HAS_EXTENDED_PLATFORMINFO_DATA
// NO AUTO-KICK-OFF - let the editor request it as needed
// delay the kick off of running UAT so we can check IsRunningCommandlet()
// FDelayedAutoRegisterHelper GPlatformInfoInit(EDelayedRegisterRunPhase::TaskGraphSystemReady, []()
// {
// 	FDataDrivenPlatformInfoRegistry::UpdateSdkStatus();
// });
#endif

static const TArray<FString>& GetDataDrivenIniFilenames()
{
	static bool bHasSearchedForFiles = false;
	static TArray<FString> DataDrivenIniFilenames;

	if (bHasSearchedForFiles == false)
	{
		bHasSearchedForFiles = true;

		// look for the special files in any congfig subdirectories
		IFileManager::Get().FindFilesRecursive(DataDrivenIniFilenames, *FPaths::EngineConfigDir(), TEXT("DataDrivenPlatformInfo.ini"), true, false);

		// manually look through the platform directories - we can't use GetExtensionDirs(), since that function uses the results of this function 
		TArray<FString> PlatformDirs;
		IFileManager::Get().FindFiles(PlatformDirs, *FPaths::Combine(FPaths::EnginePlatformExtensionsDir(), TEXT("*")), false, true);

		for (const FString& PlatformDir : PlatformDirs)
		{
			FString IniPath = FPaths::Combine(FPaths::EnginePlatformExtensionsDir(), PlatformDir, TEXT("Config/DataDrivenPlatformInfo.ini"));
			if (IFileManager::Get().FileExists(*IniPath))
			{
				DataDrivenIniFilenames.Add(IniPath);
			}
		}
	}

	return DataDrivenIniFilenames;
}

int32 FDataDrivenPlatformInfoRegistry::GetNumDataDrivenIniFiles()
{
	return GetDataDrivenIniFilenames().Num();
}

bool FDataDrivenPlatformInfoRegistry::LoadDataDrivenIniFile(int32 Index, FConfigFile& IniFile, FString& PlatformName)
{
	const TArray<FString>& IniFilenames = GetDataDrivenIniFilenames();
	if (Index < 0 || Index >= IniFilenames.Num())
	{
		return false;
	}

	// manually load a FConfigFile object from a source ini file so that we don't do any SavedConfigDir processing or anything
	// (there's a possibility this is called before the ProjectDir is set)
	FString IniContents;
	if (FFileHelper::LoadFileToString(IniContents, *IniFilenames[Index]))
	{
		IniFile.ProcessInputFileContents(IniContents);

		// platform extension paths are different (engine/platforms/platform/config, not engine/config/platform)
		if (IniFilenames[Index].StartsWith(FPaths::EnginePlatformExtensionsDir()))
		{
			PlatformName = FPaths::GetCleanFilename(FPaths::GetPath(FPaths::GetPath(IniFilenames[Index])));
		}
		else
		{
			// this could be 'Engine' for a shared DataDrivenPlatformInfo file
			PlatformName = FPaths::GetCleanFilename(FPaths::GetPath(IniFilenames[Index]));
		}

		return true;
	}

	return false;
}

static void DDPIIniRedirect(FString& StringData)
{
	TArray<FString> Tokens;
	StringData.ParseIntoArray(Tokens, TEXT(":"));
	if (Tokens.Num() != 5)
	{
		StringData = TEXT("");
		return;
	}

	// now load a local version of the ini hierarchy
	FConfigFile LocalIni;
	FConfigCacheIni::LoadLocalIniFile(LocalIni, *Tokens[1], true, *Tokens[2]);

	// and get the platform's value (if it's not found, return an empty string)
	FString FoundValue;
	LocalIni.GetString(*Tokens[3], *Tokens[4], FoundValue);
	StringData = FoundValue;
}

static FString DDPITryRedirect(const FConfigFile& IniFile, const TCHAR* Key, bool* OutHadBang=nullptr)
{
	FString StringData;
	bool bWasFound = false;
	if ((bWasFound = IniFile.GetString(TEXT("DataDrivenPlatformInfo"), Key, StringData)) == false)
	{
		bWasFound = IniFile.GetString(TEXT("DataDrivenPlatformInfo"), *FString::Printf(TEXT("%s:%s"), ANSI_TO_TCHAR(FPlatformProperties::IniPlatformName()), Key), StringData);
	}
	if (bWasFound)
	{
		if (StringData.StartsWith(TEXT("ini:")) || StringData.StartsWith(TEXT("!ini:")))
		{
			// check for !'ing a bool
			if (OutHadBang != nullptr)
			{
				*OutHadBang = StringData[0] == TEXT('!');
			}

			// replace the string, overwriting it
			DDPIIniRedirect(StringData);
		}
	}
	return StringData;
}

static void DDPIGetBool(const FConfigFile& IniFile, const TCHAR* Key, bool& OutBool)
{
	bool bHadNot = false;
	FString StringData = DDPITryRedirect(IniFile, Key, &bHadNot);

	// if we ended up with a string, convert it, otherwise leave it alone
	if (StringData.Len() > 0)
	{
		OutBool = bHadNot ? !StringData.ToBool() : StringData.ToBool();
	}
}

static void DDPIGetInt(const FConfigFile& IniFile, const TCHAR* Key, int32& OutInt)
{
	FString StringData = DDPITryRedirect(IniFile, Key);

	// if we ended up with a string, convert it, otherwise leave it alone
	if (StringData.Len() > 0)
	{
		OutInt = FCString::Atoi(*StringData);
	}
}

static void DDPIGetUInt(const FConfigFile& IniFile, const TCHAR* Key, uint32& OutInt)
{
	FString StringData = DDPITryRedirect(IniFile, Key);

	// if we ended up with a string, convert it, otherwise leave it alone
	if (StringData.Len() > 0)
	{
		OutInt = (uint32)FCString::Strtoui64(*StringData, nullptr, 10);
	}
}

static void DDPIGetName(const FConfigFile& IniFile, const TCHAR* Key, FName& OutName)
{
	FString StringData = DDPITryRedirect(IniFile, Key);

	// if we ended up with a string, convert it, otherwise leave it alone
	if (StringData.Len() > 0)
	{
		OutName = FName(*StringData);
	}
}

static void DDPIGetString(const FConfigFile& IniFile, const TCHAR* Key, FString& OutString)
{
	FString StringData = DDPITryRedirect(IniFile, Key);

	// if we ended up with a string, convert it, otherwise leave it alone
	if (StringData.Len() > 0)
	{
		OutString = StringData;
	}
}

static void DDPIGetStringArray(const FConfigFile& IniFile, const TCHAR* Key, TArray<FString>& OutArray)
{
	// we don't support redirecting arrays
	IniFile.GetArray(TEXT("DataDrivenPlatformInfo"), Key, OutArray);
}

// Gets a string from a section, or empty string if it didn't exist
static FString GetSectionString(const FConfigSection& Section, FName Key)
{
	return Section.FindRef(Key).GetValue();
}

static void ParsePreviewPlatforms(const FConfigFile& IniFile, FDataDrivenPlatformInfo& Info)
{
	// walk over the file looking for PreviewPlatform sections
	for (auto Section : IniFile)
	{
		if (Section.Key.StartsWith(TEXT("PreviewPlatform ")))
		{
			const FString& SectionName = Section.Key;
			FName PreviewPlatformName = *SectionName.Mid(16);

			// Early-out if enabled cvar is specified and not set
			TArray<FString> Tokens;
			GetSectionString(Section.Value, FName("EnabledCVar")).ParseIntoArray(Tokens, TEXT(":"));
			if (Tokens.Num() == 5)
			{
				// now load a local version of the ini hierarchy
				FConfigFile LocalIni;
				FConfigCacheIni::LoadLocalIniFile(LocalIni, *Tokens[1], true, *Tokens[2]);

				// and get the enabled cvar's value
				bool bEnabled = false;
				LocalIni.GetBool(*Tokens[3], *Tokens[4], bEnabled);
				if (!bEnabled)
				{
					continue;
				}
			}

			FName ShaderFormat = *GetSectionString(Section.Value, FName("ShaderFormat"));

			FPreviewPlatformMenuItem& Item = Info.PreviewPlatformMenuItems.FindOrAdd(ShaderFormat);

			Item.ActiveIconPath = GetSectionString(Section.Value, FName("ActiveIconPath"));
			Item.ActiveIconName = *GetSectionString(Section.Value, FName("ActiveIconName"));
			Item.InactiveIconPath = GetSectionString(Section.Value, FName("InactiveIconPath"));
			Item.InactiveIconName = *GetSectionString(Section.Value, FName("InactiveIconName"));
			FTextStringHelper::ReadFromBuffer(*GetSectionString(Section.Value, FName("MenuText")), Item.MenuText);
			FTextStringHelper::ReadFromBuffer(*GetSectionString(Section.Value, FName("MenuTooltip")), Item.MenuTooltip);
			FTextStringHelper::ReadFromBuffer(*GetSectionString(Section.Value, FName("IconText")), Item.IconText);
		}
	}
}

static void LoadDDPIIniSettings(const FConfigFile& IniFile, FDataDrivenPlatformInfo& Info, FName PlatformName)
{
	DDPIGetBool(IniFile, TEXT("bIsConfidential"), Info.bIsConfidential);
	DDPIGetBool(IniFile, TEXT("bIsFakePlatform"), Info.bIsFakePlatform);
	DDPIGetString(IniFile, TEXT("AudioCompressionSettingsIniSectionName"), Info.AudioCompressionSettingsIniSectionName);
	DDPIGetStringArray(IniFile, TEXT("AdditionalRestrictedFolders"), Info.AdditionalRestrictedFolders);
	
	DDPIGetBool(IniFile, TEXT("Freezing_b32Bit"), Info.Freezing_b32Bit);
	DDPIGetUInt(IniFile, Info.Freezing_b32Bit ? TEXT("Freezing_MaxFieldAlignment32") : TEXT("Freezing_MaxFieldAlignment64"), Info.Freezing_MaxFieldAlignment);
	DDPIGetBool(IniFile, TEXT("Freezing_bForce64BitMemoryImagePointers"), Info.Freezing_bForce64BitMemoryImagePointers);
	DDPIGetBool(IniFile, TEXT("Freezing_bAlignBases"), Info.Freezing_bAlignBases);
	DDPIGetBool(IniFile, TEXT("Freezing_bWithRayTracing"), Info.Freezing_bWithRayTracing);

	// NOTE: add more settings here!

#if DDPI_HAS_EXTENDED_PLATFORMINFO_DATA

	DDPIGetString(IniFile, TEXT("AutoSDKPath"), Info.AutoSDKPath);
	DDPIGetString(IniFile, TEXT("TutorialPath"), Info.SDKTutorial);
	DDPIGetName(IniFile, TEXT("PlatformGroupName"), Info.PlatformGroupName);
	DDPIGetName(IniFile, TEXT("PlatformSubMenu"), Info.PlatformSubMenu);
	DDPIGetString(IniFile, TEXT("PrepareForDebuggingOptions"), Info.PrepareForDebuggingOptions);


	DDPIGetString(IniFile, TEXT("NormalIconPath"), Info.IconPaths.NormalPath);
	DDPIGetString(IniFile, TEXT("LargeIconPath"), Info.IconPaths.LargePath);
	DDPIGetString(IniFile, TEXT("XLargeIconPath"), Info.IconPaths.XLargePath);
	if (Info.IconPaths.XLargePath == TEXT(""))
	{
		Info.IconPaths.XLargePath = Info.IconPaths.LargePath;
	}

	FString PlatformString = PlatformName.ToString();
	Info.IconPaths.NormalStyleName = *FString::Printf(TEXT("Launcher.Platform_%s"), *PlatformString);
	Info.IconPaths.LargeStyleName = *FString::Printf(TEXT("Launcher.Platform_%s.Large"), *PlatformString);
	Info.IconPaths.XLargeStyleName = *FString::Printf(TEXT("Launcher.Platform_%s.XLarge"), *PlatformString);

	Info.bCanUseCrashReporter = true; // not specified means true, not false
	DDPIGetBool(IniFile, TEXT("bCanUseCrashReporter"), Info.bCanUseCrashReporter);
	DDPIGetBool(IniFile, TEXT("bUsesHostCompiler"), Info.bUsesHostCompiler);
	DDPIGetBool(IniFile, TEXT("bUATClosesAfterLaunch"), Info.bUATClosesAfterLaunch);
	DDPIGetBool(IniFile, TEXT("bIsEnabled"), Info.bEnabledForUse);

	DDPIGetName(IniFile, TEXT("UBTPlatformName"), Info.UBTPlatformName);
	// if unspecified, use the ini platform name (only Win64 breaks this)
	if (Info.UBTPlatformName == NAME_None)
	{
		Info.UBTPlatformName = PlatformName;
	}
	Info.UBTPlatformString = Info.UBTPlatformName.ToString();
		
	
	// now that we have all targetplatforms in a single TP module per platform, just look for it (or a ShaderFormat for other tools that may want this)
	// we could look for Platform*, but then platforms that are a substring of another one could return a false positive (Windows* would find Windows31TargetPlatform)
	Info.bHasCompiledTargetSupport = FDataDrivenPlatformInfoRegistry::HasCompiledSupportForPlatform(PlatformName, FDataDrivenPlatformInfoRegistry::EPlatformNameType::TargetPlatform);

#endif

	ParsePreviewPlatforms(IniFile, Info);
}

/**
* Get the global set of data driven platform information
*/
const TMap<FName, FDataDrivenPlatformInfo>& FDataDrivenPlatformInfoRegistry::GetAllPlatformInfos()
{
	static bool bHasSearchedForPlatforms = false;

	// look on disk for special files
	if (bHasSearchedForPlatforms == false)
	{
		bHasSearchedForPlatforms = true;

		int32 NumFiles = FDataDrivenPlatformInfoRegistry::GetNumDataDrivenIniFiles();

		TMap<FString, FString> IniParents;
		for (int32 Index = 0; Index < NumFiles; Index++)
		{
			// load the .ini file
			FConfigFile IniFile;
			FString PlatformString;
			FDataDrivenPlatformInfoRegistry::LoadDataDrivenIniFile(Index, IniFile, PlatformString);

			FName PlatformName(*PlatformString);
			// platform info is registered by the platform name
			if (IniFile.Contains(TEXT("DataDrivenPlatformInfo")))
			{
				// cache info
				FDataDrivenPlatformInfo& Info = DataDrivenPlatforms.Add(PlatformName, FDataDrivenPlatformInfo());
				LoadDDPIIniSettings(IniFile, Info, PlatformName);

				// get the parent to build list later
				FString IniParent;
				IniFile.GetString(TEXT("DataDrivenPlatformInfo"), TEXT("IniParent"), IniParent);
				IniParents.Add(PlatformString, IniParent);
			}
		}

		// now that all are read in, calculate the ini parent chain, starting with parent-most
		for (auto& It : DataDrivenPlatforms)
		{
			// walk up the chain and build up the ini chain of parents
			for (FString CurrentPlatform = IniParents.FindRef(It.Key.ToString()); CurrentPlatform != TEXT(""); CurrentPlatform = IniParents.FindRef(CurrentPlatform))
			{
				// insert at 0 to reverse the order
				It.Value.IniParentChain.Insert(CurrentPlatform, 0);
			}
		}

		DataDrivenPlatforms.GetKeys(SortedPlatformNames);
		// now sort them into arrays of keys and values
		Algo::Sort(SortedPlatformNames, [](FName One, FName Two) -> bool
		{
			return One.Compare(Two) < 0;
		});

		// now build list of values from the sort
		SortedPlatformInfos.Empty(SortedPlatformNames.Num());
		for (int Index = 0; Index < SortedPlatformInfos.Num(); Index++)
		{
			SortedPlatformInfos[Index] = &DataDrivenPlatforms[SortedPlatformNames[Index]];
		}
	}

	return DataDrivenPlatforms;
}

const TArray<FName> FDataDrivenPlatformInfoRegistry::GetSortedPlatformNames()
{
	// make sure we've read in the inis
	GetAllPlatformInfos();

	return SortedPlatformNames;
}

const TArray<const FDataDrivenPlatformInfo*>& FDataDrivenPlatformInfoRegistry::GetSortedPlatformInfos()
{
	// make sure we've read in the inis
	GetAllPlatformInfos();

	return SortedPlatformInfos;
}


#if DDPI_HAS_EXTENDED_PLATFORMINFO_DATA

// some shared functionality
static void PrepForTurnkeyReport(FString& Command, FString& BaseCommandline, FString& ReportFilename)
{
 	static int ReportIndex = 0;

	FString LogFilename = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectIntermediateDir(), *FString::Printf(TEXT("TurnkeyLog_%d.log"), ReportIndex)));
	ReportFilename = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectIntermediateDir(), *FString::Printf(TEXT("TurnkeyReport_%d.log"), ReportIndex++)));

	// make sure intermediate directory exists
	IFileManager::Get().MakeDirectory(*FPaths::ProjectIntermediateDir());

	Command = TEXT("{EngineDir}Build/BatchFiles/RunuAT");
//	Command = TEXT("{EngineDir}/Binaries/DotNET/AutomationTool.exe");
	BaseCommandline = FString::Printf(TEXT("Turnkey -utf8output -WaitForUATMutex -command=VerifySdk -ReportFilename=\"%s\" -log=\"%s\""), *ReportFilename, *LogFilename);

	// convert into appropriate calls for the current platform
	FPlatformProcess::ModifyCreateProcParams(Command, BaseCommandline, FGenericPlatformProcess::ECreateProcHelperFlags::AppendPlatformScriptExtension | FGenericPlatformProcess::ECreateProcHelperFlags::RunThroughShell);
}

static FString ConvertToDDPIPlatform(const FString& Platform)
{
	FString  New = Platform.Replace(TEXT("Editor"), TEXT("")).Replace(TEXT("Client"), TEXT("")).Replace(TEXT("Server"), TEXT(""));
	if (New == TEXT("Win64"))
	{
		New = TEXT("Windows");
	}
	return New;
}

static FString ConvertToUATPlatform(const FString& Platform)
{
	FString New = ConvertToDDPIPlatform(Platform);
	if (New == TEXT("Windows"))
	{
		New = TEXT("Win64");
	}
	return New;
}

static FString ConvertToUATDeviceId(const FString& DeviceId)
{
	TArray<FString> PlatformAndDevice;
	DeviceId.ParseIntoArray(PlatformAndDevice, TEXT("@"), true);

	return FString::Printf(TEXT("%s@%s"), *ConvertToUATPlatform(PlatformAndDevice[0]), *PlatformAndDevice[1]);
}

static FString ConvertToDDPIDeviceId(const FString& DeviceId)
{
	TArray<FString> PlatformAndDevice;
	DeviceId.ParseIntoArray(PlatformAndDevice, TEXT("@"), true);

	return FString::Printf(TEXT("%s@%s"), *ConvertToDDPIPlatform(PlatformAndDevice[0]), *PlatformAndDevice[1]);
}

bool GetSdkInfoFromTurnkey(FString Line, FString& PlatformName, FString& DeviceId, FDDPISdkInfo& SdkInfo)
{
	int32 Colon = Line.Find(TEXT(": "));

	if (Colon < 0)
	{
		return false;
	}

	// break up the string
	PlatformName = Line.Mid(0, Colon);
	FString Info = Line.Mid(Colon + 2);

	int32 AtSign = PlatformName.Find(TEXT("@"));
	if (AtSign > 0)
	{
		// return the platform@name as the deviceId, then remove the @name part for the platform
		DeviceId = ConvertToDDPIDeviceId(PlatformName);
		PlatformName = PlatformName.Mid(0, AtSign);
	}

	// get the DDPI name
	PlatformName = ConvertToDDPIPlatform(PlatformName);

	// parse out the results from the (key=val, key=val) result from turnkey
	FString StatusString;
	FString FlagsString;
	FParse::Value(*Info, TEXT("Status="), StatusString);
	FParse::Value(*Info, TEXT("Flags="), FlagsString);
	FParse::Value(*Info, TEXT("Installed="), SdkInfo.InstalledVersion);
	FParse::Value(*Info, TEXT("AutoSDK="), SdkInfo.AutoSDKVersion);
	FParse::Value(*Info, TEXT("MinAllowed="), SdkInfo.MinAllowedVersion);
	FParse::Value(*Info, TEXT("MaxAllowed="), SdkInfo.MaxAllowedVersion);

	SdkInfo.Status = DDPIPlatformSdkStatus::Unknown;
	if (StatusString == TEXT("Valid"))
	{
		SdkInfo.Status = DDPIPlatformSdkStatus::Valid;
	}
	else
	{
		if (FlagsString.Contains(TEXT("AutoSdk_InvalidVersionExists")) || FlagsString.Contains(TEXT("InstalledSdk_InvalidVersionExists")))
		{
			SdkInfo.Status = DDPIPlatformSdkStatus::OutOfDate;
		}
		else
		{
			SdkInfo.Status = DDPIPlatformSdkStatus::NoSdk;
		}
	}

	return true;
}

void FDataDrivenPlatformInfoRegistry::UpdateSdkStatus()
{
	// make sure we've read in the inis
	GetAllPlatformInfos();

	// don't run UAT from commandlets (like the cooker) that are often launched from UAT and this will go poorly
	if (IsRunningCommandlet())
	{
		for (auto& It : DataDrivenPlatforms)
		{
			It.Value.SdkInfo.Status = DDPIPlatformSdkStatus::Unknown;

			// reset the per-device status when querying general Sdk status
			It.Value.ClearDeviceStatus();
		}

		return;
	}


	FString Command, BaseCommandline, ReportFilename;
	PrepForTurnkeyReport(Command, BaseCommandline, ReportFilename);
	FString Commandline = BaseCommandline + FString(TEXT(" -platform=")) + FString::JoinBy(DataDrivenPlatforms, TEXT("+"), [](TPair<FName, FDataDrivenPlatformInfo> Pair) { return ConvertToUATPlatform(Pair.Key.ToString()); });

	UE_LOG(LogInit, Log, TEXT("Running Turnkey SDK detection: '%s %s'"), *Command, *Commandline);

	{
		FScopeLock Lock(&DDPILocker);

		// reset status to unknown
		for (auto& It : DataDrivenPlatforms)
		{
			It.Value.SdkInfo.Status = DDPIPlatformSdkStatus::Querying;

			// reset the per-device status when querying general Sdk status
			It.Value.ClearDeviceStatus();
		}
	}

	FMonitoredProcess* TurnkeyProcess = new FMonitoredProcess(Command, Commandline, true, false);
	TurnkeyProcess->OnCompleted().BindLambda([ReportFilename, TurnkeyProcess](int32 ExitCode)
	{
		AsyncTask(ENamedThreads::GameThread, [ReportFilename, TurnkeyProcess, ExitCode]()
		{
			FScopeLock Lock(&DDPILocker);

			if (ExitCode == 0 || ExitCode == 10)
			{
				TArray<FString> Contents;
				if (FFileHelper::LoadFileToStringArray(Contents, *ReportFilename))
				{
					for (FString& Line : Contents)
					{
						UE_LOG(LogTemp, Log, TEXT("Turnkey Platform: %s"), *Line);

						// parse a Turnkey line
						FString PlatformName, Unused;
						FDDPISdkInfo SdkInfo;
						if (GetSdkInfoFromTurnkey(Line, PlatformName, Unused, SdkInfo) == false)
						{
							continue;
						}

						// check if we had already set a ManualSDK - and don't set it again. Because of the way AutoSDKs are activated in the editor after the first call to Turnkey,
						// future calls to Turnkey will inherit the AutoSDK env vars, and it won't be able to determine the manual SDK versions anymore. If we use the editor to
						// install an SDK via Turnkey, it will directly update the installed version based on the result of that command, not this Update operation

						FString OriginalManualInstallValue = DataDrivenPlatforms[*PlatformName].SdkInfo.InstalledVersion;

						// set it into the platform
						DataDrivenPlatforms[*PlatformName].SdkInfo = SdkInfo;

						// restore the original installed version if it set after the first time
						if (OriginalManualInstallValue.Len() > 0)
						{
							DataDrivenPlatforms[*PlatformName].SdkInfo.InstalledVersion = OriginalManualInstallValue;
						}


						UE_LOG(LogTemp, Log, TEXT("[TEST] Turnkey Platform: %s - %d, Installed: %s, AudoSDK: %s, Allowed: %s-%s"), *PlatformName, (int)SdkInfo.Status, *SdkInfo.InstalledVersion,
							*SdkInfo.AutoSDKVersion, *SdkInfo.MinAllowedVersion, *SdkInfo.MaxAllowedVersion);
					}
				}
			}
			else
			{
				for (auto& It : DataDrivenPlatforms)
				{
					It.Value.SdkInfo.Status = DDPIPlatformSdkStatus::Error;
					It.Value.SdkInfo.SdkErrorInformation = FText::Format(NSLOCTEXT("Turnkey", "TurnkeyError_ReturnedError", "Turnkey returned an error, code {0}"), { ExitCode });

					// @todo turnkey error description!
				}
			}


			for (auto& It : DataDrivenPlatforms)
			{
				if (It.Value.SdkInfo.Status == DDPIPlatformSdkStatus::Querying)
				{
					if (It.Value.bIsFakePlatform)
					{
						It.Value.SdkInfo.Status = DDPIPlatformSdkStatus::Unknown;
					}
					else
					{
						It.Value.SdkInfo.Status = DDPIPlatformSdkStatus::Error;
						It.Value.SdkInfo.SdkErrorInformation = NSLOCTEXT("Turnkey", "TurnkeyError_NotReturned", "The platform's Sdk status was not returned from Turnkey");
					}
				}
			}

			// cleanup
			delete TurnkeyProcess;
			IFileManager::Get().Delete(*ReportFilename);
		});
	});

	// run it
	TurnkeyProcess->Launch();
}

FDataDrivenPlatformInfo& FDataDrivenPlatformInfoRegistry::DeviceIdToInfo(FString DeviceId, FString* OutDeviceName)
{
	TArray<FString> PlatformAndDevice;
	DeviceId.ParseIntoArray(PlatformAndDevice, TEXT("@"), true);

	if (OutDeviceName)
	{
		*OutDeviceName = PlatformAndDevice[1];
	}

	FString DDPIPlatformName = ConvertToDDPIPlatform(PlatformAndDevice[0]);

	checkf(DataDrivenPlatforms.Contains(*DDPIPlatformName), TEXT("DataDrivenPlatforms map did not contain the DDPI Platform %s"), *DDPIPlatformName);

	// have to convert back to Windows from Win64
	return DataDrivenPlatforms[*DDPIPlatformName];

}

void FDataDrivenPlatformInfoRegistry::UpdateDeviceSdkStatus(TArray<FString> PlatformDeviceIds)
{
	FString Command, BaseCommandline, ReportFilename;
	PrepForTurnkeyReport(Command, BaseCommandline, ReportFilename);

	// the platform part of the Id may need to be converted to be turnkey (ie UBT) proper

	FString Commandline = BaseCommandline + FString(TEXT(" -Device=")) + FString::JoinBy(PlatformDeviceIds, TEXT("+"), [](FString Id) { return ConvertToUATDeviceId(Id); });

	UE_LOG(LogInit, Log, TEXT("Running Turnkey SDK detection: '%s %s'"), *Command, *Commandline);

	{
		FScopeLock Lock(&DDPILocker);

		// set status to querying
		FDDPISdkInfo DefaultInfo;
		DefaultInfo.Status = DDPIPlatformSdkStatus::Querying;
		for (const FString& Id : PlatformDeviceIds)
		{
			DeviceIdToInfo(Id).PerDeviceStatus.Add(ConvertToDDPIDeviceId(Id), DefaultInfo);
		}
	}

	FMonitoredProcess* TurnkeyProcess = new FMonitoredProcess(Command, Commandline, true, false);
	TurnkeyProcess->OnCompleted().BindLambda([ReportFilename, TurnkeyProcess, PlatformDeviceIds](int32 ExitCode)
	{
		AsyncTask(ENamedThreads::GameThread, [ReportFilename, TurnkeyProcess, PlatformDeviceIds, ExitCode]()
		{
			FScopeLock Lock(&DDPILocker);

			if (ExitCode == 0 || ExitCode == 10)
			{
				TArray<FString> Contents;
				if (FFileHelper::LoadFileToStringArray(Contents, *ReportFilename))
				{
					for (FString& Line : Contents)
					{
						FString PlatformName, DDPIDeviceId;
						FDDPISdkInfo SdkInfo;
						if (GetSdkInfoFromTurnkey(Line, PlatformName, DDPIDeviceId, SdkInfo) == false)
						{
							continue;
						}

						// skip over non-device lines
						if (DDPIDeviceId.Len() == 0)
						{
							continue;
						}

						UE_LOG(LogTemp, Log, TEXT("Turnkey Device: %s"), *Line);

						DeviceIdToInfo(DDPIDeviceId).PerDeviceStatus[DDPIDeviceId] = SdkInfo;

						UE_LOG(LogTemp, Log, TEXT("[TEST] Turnkey Device: %s - %d, Installed: %s, Allowed: %s-%s"), *DDPIDeviceId, (int)SdkInfo.Status, *SdkInfo.InstalledVersion,
							*SdkInfo.MinAllowedVersion, *SdkInfo.MaxAllowedVersion);
					}
				}
			}

			for (const FString& Id : PlatformDeviceIds)
			{
				FDataDrivenPlatformInfo& Info = DeviceIdToInfo(Id);
				
				FDDPISdkInfo& SdkInfo = Info.PerDeviceStatus[ConvertToDDPIDeviceId(Id)];
				if (SdkInfo.Status == DDPIPlatformSdkStatus::Querying)
				{
					SdkInfo.Status = DDPIPlatformSdkStatus::Error;
					SdkInfo.SdkErrorInformation = NSLOCTEXT("Turnkey", "TurnkeyError_DeviceNotReturned", "A device's Sdk status was not returned from Turnkey");
				}
			}

			// cleanup
			delete TurnkeyProcess;
			IFileManager::Get().Delete(*ReportFilename);
		});
	});

	// run it
	TurnkeyProcess->Launch();
}

void FDataDrivenPlatformInfoRegistry::ClearDeviceStatus(FName PlatformName)
{
	FScopeLock Lock(&DDPILocker);

	if (PlatformName != NAME_None)
	{
		for (auto It : DataDrivenPlatforms)
		{
			It.Value.ClearDeviceStatus();
		}
	}
	else
	{
		if (DataDrivenPlatforms.Contains(PlatformName))
		{
			DataDrivenPlatforms[PlatformName].ClearDeviceStatus();
		}
	}
}

DDPIPlatformSdkStatus FDataDrivenPlatformInfo::GetStatusForDeviceId(const FString& DeviceId) const
{
	// return the status, or Unknown if not known
	return PerDeviceStatus.FindRef(ConvertToDDPIDeviceId(DeviceId)).Status;
}

const FDDPISdkInfo& FDataDrivenPlatformInfo::GetSdkInfoForDeviceId(const FString& DeviceId) const
{
	return *PerDeviceStatus.Find(ConvertToDDPIDeviceId(DeviceId));
}


void FDataDrivenPlatformInfo::ClearDeviceStatus()
{
	for (auto& Pair : PerDeviceStatus)
	{
		Pair.Value.Status = DDPIPlatformSdkStatus::Unknown;
	}
}

#endif


const TArray<FString>& FDataDrivenPlatformInfoRegistry::GetValidPlatformDirectoryNames()
{
	static bool bHasSearchedForPlatforms = false;
	static TArray<FString> ValidPlatformDirectories;

	if (bHasSearchedForPlatforms == false)
	{
		bHasSearchedForPlatforms = true;

		// look for possible platforms
		const TMap<FName, FDataDrivenPlatformInfo>& Infos = GetAllPlatformInfos();
		for (auto Pair : Infos)
		{
#if DDPI_HAS_EXTENDED_PLATFORMINFO_DATA
			// if the editor hasn't compiled in support for the platform, it's not "valid"
			if (!HasCompiledSupportForPlatform(Pair.Key, EPlatformNameType::Ini))
			{
				continue;
			}
#endif

			// add ourself as valid
			ValidPlatformDirectories.AddUnique(Pair.Key.ToString());

			// now add additional directories
			for (FString& AdditionalDir : Pair.Value.AdditionalRestrictedFolders)
			{
				ValidPlatformDirectories.AddUnique(AdditionalDir);
			}
		}
	}

	return ValidPlatformDirectories;
}


const FDataDrivenPlatformInfo& FDataDrivenPlatformInfoRegistry::GetPlatformInfo(FName PlatformName)
{
	const FDataDrivenPlatformInfo* Info = GetAllPlatformInfos().Find(PlatformName);
	static FDataDrivenPlatformInfo Empty;
	return Info ? *Info : Empty;
}

const FDataDrivenPlatformInfo& FDataDrivenPlatformInfoRegistry::GetPlatformInfo(const FString& PlatformName)
{
	return GetPlatformInfo(FName(*PlatformName));
}

const FDataDrivenPlatformInfo& FDataDrivenPlatformInfoRegistry::GetPlatformInfo(const char* PlatformName)
{
	return GetPlatformInfo(FName(PlatformName));
}


const TArray<FName>& FDataDrivenPlatformInfoRegistry::GetConfidentialPlatforms()
{
	static bool bHasSearchedForPlatforms = false;
	static TArray<FName> FoundPlatforms;

	// look on disk for special files
	if (bHasSearchedForPlatforms == false)
	{
		for (auto It : GetAllPlatformInfos())
		{
			if (It.Value.bIsConfidential)
			{
				FoundPlatforms.Add(It.Key);
			}
		}

		bHasSearchedForPlatforms = true;
	}

	// return whatever we have already found
	return FoundPlatforms;
}


#if DDPI_HAS_EXTENDED_PLATFORMINFO_DATA
bool FDataDrivenPlatformInfoRegistry::HasCompiledSupportForPlatform(FName PlatformName, EPlatformNameType PlatformNameType)
{
	if (PlatformNameType == EPlatformNameType::Ini)
	{
		// get the DDPI info object
		const FDataDrivenPlatformInfo& Info = GetPlatformInfo(PlatformName);
		return Info.bHasCompiledTargetSupport;
	}
	else if (PlatformNameType == EPlatformNameType::UBT)
	{
		// find all the DataDrivenPlatformInfo objects and find a matching the UBT name
		for (auto& Pair : FDataDrivenPlatformInfoRegistry::GetAllPlatformInfos())
		{
			// if this platform matches the UBT platform name, check it's Ini name
			if (Pair.Value.UBTPlatformName == PlatformName)
			{
				return HasCompiledSupportForPlatform(Pair.Key, EPlatformNameType::Ini);
			}
		}

		return false;
	}
	else if (PlatformNameType == EPlatformNameType::TargetPlatform)
	{
		// was this TP compiled, or a shaderformat (useful for SCW if it ever calls this)
		return 
			FModuleManager::Get().ModuleExists(*FString::Printf(TEXT("%sTargetPlatform"), *PlatformName.ToString())) || 
			FModuleManager::Get().ModuleExists(*FString::Printf(TEXT("%sShaderFormat"), *PlatformName.ToString()));
	}

	return false;
}

#endif
