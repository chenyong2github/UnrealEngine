// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/DataDrivenPlatformInfoRegistry.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/ConfigCacheIni.h"
#include "Modules/ModuleManager.h"

#include "Async/Async.h"
#include "Misc/MonitoredProcess.h"


TMap<FString, FDataDrivenPlatformInfoRegistry::FPlatformInfo> FDataDrivenPlatformInfoRegistry::DataDrivenPlatforms;

#if DDPI_HAS_EXTENDED_PLATFORMINFO_DATA
// delay the kick off of running UAT so we can check IsRunningCommandlet()
FDelayedAutoRegisterHelper GPlatformInfoInit(EDelayedRegisterRunPhase::TaskGraphSystemReady, []()
{
	FDataDrivenPlatformInfoRegistry::UpdateSdkStatus();
});
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
		IFileManager::Get().FindFilesRecursive(DataDrivenIniFilenames, *FPaths::EnginePlatformExtensionsDir(), TEXT("DataDrivenPlatformInfo.ini"), true, false, false);
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
	if (IniFile.GetString(TEXT("DataDrivenPlatformInfo"), Key, StringData))
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

static void LoadDDPIIniSettings(const FConfigFile& IniFile, FDataDrivenPlatformInfoRegistry::FPlatformInfo& Info)
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
	// now look in the PlatformInfo objects in the file for TP and UBT names

	FName TargetPlatformKey("TargetPlatformName");
	FName UBTNameKey("UBTTargetID");
	for (auto& Pair : IniFile)
	{
		if (Pair.Key.StartsWith(TEXT("PlatformInfo")))
		{
			const FConfigValue* PlatformName = Pair.Value.Find(TargetPlatformKey);
			if (PlatformName)
			{
				Info.AllTargetPlatformNames.AddUnique(PlatformName->GetValue());
			}
			
			PlatformName = Pair.Value.Find(UBTNameKey);
			if (PlatformName)
			{
				Info.AllUBTPlatformNames.AddUnique(PlatformName->GetValue());
			}
		}
	}
#endif
}


/**
* Get the global set of data driven platform information
*/
const TMap<FString, FDataDrivenPlatformInfoRegistry::FPlatformInfo>& FDataDrivenPlatformInfoRegistry::GetAllPlatformInfos()
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
			FString PlatformName;
			FDataDrivenPlatformInfoRegistry::LoadDataDrivenIniFile(Index, IniFile, PlatformName);

			// platform info is registered by the platform name
			if (IniFile.Contains(TEXT("DataDrivenPlatformInfo")))
			{
				// cache info
				FDataDrivenPlatformInfoRegistry::FPlatformInfo& Info = DataDrivenPlatforms.Add(PlatformName, FDataDrivenPlatformInfoRegistry::FPlatformInfo());
				LoadDDPIIniSettings(IniFile, Info);

				// get the parent to build list later
				FString IniParent;
				IniFile.GetString(TEXT("DataDrivenPlatformInfo"), TEXT("IniParent"), IniParent);
				IniParents.Add(PlatformName, IniParent);
			}
		}

		// now that all are read in, calculate the ini parent chain, starting with parent-most
		for (auto& It : DataDrivenPlatforms)
		{
			// walk up the chain and build up the ini chain of parents
			for (FString CurrentPlatform = IniParents.FindRef(It.Key); CurrentPlatform != TEXT(""); CurrentPlatform = IniParents.FindRef(CurrentPlatform))
			{
				// insert at 0 to reverse the order
				It.Value.IniParentChain.Insert(CurrentPlatform, 0);
			}
		}
	}

	return DataDrivenPlatforms;
}

#if DDPI_HAS_EXTENDED_PLATFORMINFO_DATA

// some shared functionality
static void PrepForTurnkeyReport(FString& UAT, FString& ReportFilename)
{
	static int ReportIndex = 0;

	// get path to AutomationTool - skipping RunUAT for speed
//		FString AutomationTool = FPaths::Combine(FPaths::EngineDir(), TEXT("Binaries/DotNET/AutomationTool.exe"));
	UAT = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::EngineDir(), TEXT("Build/BatchFiles/RunUAT.bat")));

	ReportFilename = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectIntermediateDir(), *FString::Printf(TEXT("TurnkeyReport_%d.log"), ReportIndex++)));
}

static FString ConvertToDDPIPlatform(const FString& Platform)
{
	FString  New = Platform.Replace(TEXT("NoEditor"), TEXT("")).Replace(TEXT("Client"), TEXT("")).Replace(TEXT("Server"), TEXT(""));
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

void FDataDrivenPlatformInfoRegistry::UpdateSdkStatus()
{
	// don't run UAT from commandlets (like the cooker) that are often launched from UAT and this will go poorly
	if (IsRunningCommandlet())
	{
		for (auto& It : DataDrivenPlatforms)
		{
			It.Value.SdkStatus = DDPIPlatformSdkStatus::Unknown;

			// reset the per-device status when querying general Sdk status
			It.Value.ClearDeviceStatus();
		}

		return;
	}


	FString AutomationTool, ReportFilename;
	PrepForTurnkeyReport(AutomationTool, ReportFilename);

	// run Turnkey to get all of the platform statuses
// 	FString ProcessURL = TEXT("{EngineDir}/Binaries/DotNET/AutomationTool.exe");
// 	FString ProcessArguments = FString::Printf(TEXT("Turnkey -command=VerifySdk -platform=%s -ReportFilename=\"%s\""),
// 		*FString::JoinBy(DataDrivenPlatforms, TEXT("+"), [](TPair<FString, FDataDrivenPlatformInfoRegistry::FPlatformInfo> Pair) { return ConvertToUATPlatform(Pair.Key); }),
// 		*ReportFilename);

	FString ProcessURL = TEXT("{EngineDir}/Build/BatchFiles/RunuAT");
	FString ProcessArguments = FString::Printf(TEXT("Turnkey -command=VerifySdk -platform=%s -ReportFilename=\"%s\""),
		*FString::JoinBy(DataDrivenPlatforms, TEXT("+"), [](TPair<FString, FDataDrivenPlatformInfoRegistry::FPlatformInfo> Pair) { return ConvertToUATPlatform(Pair.Key); }),
		*ReportFilename);

	// convert into appropriate calls for the current platform
	FPlatformProcess::ModifyCreateProcParams(ProcessURL, ProcessArguments, FGenericPlatformProcess::ECreateProcHelperFlags::RunThroughShell | FGenericPlatformProcess::ECreateProcHelperFlags::AppendPlatformScriptExtension);

	// reset status to unknown
	for (auto& It : DataDrivenPlatforms)
	{
		It.Value.SdkStatus = DDPIPlatformSdkStatus::Querying;
		
		// reset the per-device status when querying general Sdk status
		It.Value.ClearDeviceStatus();
	}

	FMonitoredProcess* TurnkeyProcess = new FMonitoredProcess(ProcessURL, ProcessArguments, true, false);
	TurnkeyProcess->OnCompleted().BindLambda([ReportFilename, TurnkeyProcess](int32 ExitCode)
	{
		AsyncTask(ENamedThreads::GameThread, [ReportFilename, TurnkeyProcess, ExitCode]()
		{
			if (ExitCode == 0 || ExitCode == 10)
			{
				TArray<FString> Contents;
				if (FFileHelper::LoadFileToStringArray(Contents, *ReportFilename))
				{
					for (FString& Line : Contents)
					{
						TArray<FString> Tokens;
						Line.ParseIntoArray(Tokens, TEXT(": "), true);
						// Tokens [0] is the platform, [1] is the status, [2] is information

						DDPIPlatformSdkStatus Status = DDPIPlatformSdkStatus::Unknown;
						if (Tokens[1] == TEXT("Valid"))
						{
							Status = DDPIPlatformSdkStatus::Valid;
						}
						else
						{
							if (Tokens[2].Contains(TEXT("AutoSdk_InvalidVersionExists")) || Tokens[2].Contains(TEXT("InstalledSdk_InvalidVersionExists")))
							{
								Status = DDPIPlatformSdkStatus::OutOfDate;
							}
							else
							{
								Status = DDPIPlatformSdkStatus::NoSdk;
							}
						}

						// have to convert back to WIndows from Win64
						FString PlatformName = ConvertToDDPIPlatform(Tokens[0]);
						DataDrivenPlatforms[PlatformName].SdkStatus = Status;

						UE_LOG(LogTemp, Log, TEXT("Turnkey Platform: %s - %d - %s"), *PlatformName, (int)Status, *Tokens[2]);
					}
				}
			}
			else
			{
				for (auto& It : DataDrivenPlatforms)
				{
					It.Value.SdkStatus = DDPIPlatformSdkStatus::Error;
					// @todo turnkey error description!
				}
			}


			for (auto& It : DataDrivenPlatforms)
			{
				if (It.Value.SdkStatus == DDPIPlatformSdkStatus::Querying)
				{
					if (It.Value.bIsFakePlatform)
					{
						It.Value.SdkStatus = DDPIPlatformSdkStatus::Unknown;
					}
					else
					{
						It.Value.SdkStatus = DDPIPlatformSdkStatus::Error;
						It.Value.SdkErrorInformation = "The platform's Sdk status was not returned from Turnkey";
						//					It.Value.SdkErrorInformation = NSLOCTEXT("Turnkey", "PlatformNotReturned", "The platform's Sdk status was not returned from Turnkey");
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

FDataDrivenPlatformInfoRegistry::FPlatformInfo& FDataDrivenPlatformInfoRegistry::DeviceIdToInfo(FString DeviceId, FString* OutDeviceName)
{
	TArray<FString> PlatformAndDevice;
	DeviceId.ParseIntoArray(PlatformAndDevice, TEXT("@"), true);

	if (OutDeviceName)
	{
		*OutDeviceName = PlatformAndDevice[1];
	}

	// have to convert back to Windows from Win64
	return FDataDrivenPlatformInfoRegistry::DataDrivenPlatforms[ConvertToDDPIPlatform(PlatformAndDevice[0])];

}

void FDataDrivenPlatformInfoRegistry::UpdateDeviceSdkStatus(TArray<FString> PlatformDeviceIds)
{
	FString AutomationTool, ReportFilename;
	PrepForTurnkeyReport(AutomationTool, ReportFilename);

	// run Turnkey to get all of the platform statuses (turn the array into "Platform@Device+Platform@Device+...")
	FString Commandline = FString::Printf(TEXT("/c %s Turnkey -command=VerifySdk -Device=\"%s\" -ReportFilename=\"%s\""),
		*AutomationTool,
		*FString::JoinBy(PlatformDeviceIds, TEXT("+"), [](const FString& DeviceId) { return ConvertToUATDeviceId(DeviceId); }),
		*ReportFilename);

	// set status to querying
	for (const FString& Id : PlatformDeviceIds)
	{
		DeviceIdToInfo(Id).PerDeviceStatus.Add(ConvertToDDPIDeviceId(Id), DDPIPlatformSdkStatus::Querying);
	}

	FMonitoredProcess* TurnkeyProcess = new FMonitoredProcess(TEXT("cmd.exe"), Commandline, true, false);
	TurnkeyProcess->OnCompleted().BindLambda([ReportFilename, TurnkeyProcess, PlatformDeviceIds](int32 ExitCode)
	{
		AsyncTask(ENamedThreads::GameThread, [ReportFilename, TurnkeyProcess, PlatformDeviceIds, ExitCode]()
		{
			if (ExitCode == 0 || ExitCode == 10)
			{
				TArray<FString> Contents;
				if (FFileHelper::LoadFileToStringArray(Contents, *ReportFilename))
				{
					for (FString& Line : Contents)
					{
						TArray<FString> Tokens;
						Line.ParseIntoArray(Tokens, TEXT(": "), true);
						// Tokens [0] is platform:device, [1] is the status, [2] is information

						// token[0] without an @ is just platform status, which we don't care about now
						if (!Tokens[0].Contains(TEXT("@")))
						{
							continue;
						}

						TArray<FString> PlatformAndDevice;
						Tokens[0].ParseIntoArray(PlatformAndDevice, TEXT("@"), true);
						
						DDPIPlatformSdkStatus Status = Tokens[1] == TEXT("Valid") ? DDPIPlatformSdkStatus::FlashValid : DDPIPlatformSdkStatus::FlashOutOfDate;

						DeviceIdToInfo(Tokens[0]).PerDeviceStatus[ConvertToDDPIDeviceId(Tokens[0])] = Status;

						UE_LOG(LogTemp, Log, TEXT("Turnkey Device: %s - %d - %s"), *Tokens[0], (int)Status, *Tokens[2]);
					}
				}
			}

			for (const FString& Id : PlatformDeviceIds)
			{
				FDataDrivenPlatformInfoRegistry::FPlatformInfo& Info = DeviceIdToInfo(Id);
				
				DDPIPlatformSdkStatus& Status = Info.PerDeviceStatus[ConvertToDDPIDeviceId(Id)];
				if (Status == DDPIPlatformSdkStatus::Querying)
				{
					Status = DDPIPlatformSdkStatus::Error;
					Info.SdkErrorInformation = "A device's Sdk status was not returned from Turnkey";
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


DDPIPlatformSdkStatus FDataDrivenPlatformInfoRegistry::FPlatformInfo::GetStatusForDeviceId(const FString& DeviceId) const
{
	// return the status, or Unknown if not known
	return PerDeviceStatus.FindRef(ConvertToDDPIDeviceId(DeviceId));
}

void FDataDrivenPlatformInfoRegistry::FPlatformInfo::ClearDeviceStatus()
{
	PerDeviceStatus.Empty();
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
		const TMap<FString, FDataDrivenPlatformInfoRegistry::FPlatformInfo>& Infos = GetAllPlatformInfos();
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
			ValidPlatformDirectories.AddUnique(Pair.Key);

			// now add additional directories
			for (FString& AdditionalDir : Pair.Value.AdditionalRestrictedFolders)
			{
				ValidPlatformDirectories.AddUnique(AdditionalDir);
			}
		}
	}

	return ValidPlatformDirectories;
}


const FDataDrivenPlatformInfoRegistry::FPlatformInfo& FDataDrivenPlatformInfoRegistry::GetPlatformInfo(const FString& PlatformName)
{
	const FPlatformInfo* Info = GetAllPlatformInfos().Find(PlatformName);
	static FPlatformInfo Empty;
	return Info ? *Info : Empty;
}


const TArray<FString>& FDataDrivenPlatformInfoRegistry::GetConfidentialPlatforms()
{
	static bool bHasSearchedForPlatforms = false;
	static TArray<FString> FoundPlatforms;

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
bool FDataDrivenPlatformInfoRegistry::HasCompiledSupportForPlatform(const FString& PlatformName, EPlatformNameType PlatformNameType)
{
	if (PlatformNameType == EPlatformNameType::Ini)
	{
		// get the DDPI info object
		const FPlatformInfo& Info = GetPlatformInfo(PlatformName);

		// look to see if any of the TPs in the Info are valid - if at least one is, we are good
		for (const FString& TPName : Info.AllTargetPlatformNames)
		{
			if (HasCompiledSupportForPlatform(TPName, EPlatformNameType::TargetPlatform))
			{
				return true;
			}
		}
		return false;

	}
	else if (PlatformNameType == EPlatformNameType::UBT)
	{
		FName PlatformFName(*PlatformName);

		// find all the DataDrivenPlatformInfo objects and find a matching the UBT name
		for (auto& Pair : FDataDrivenPlatformInfoRegistry::GetAllPlatformInfos())
		{
			// if this platform contains the UBT platform name, then check the info for it's TPs
			// (we could be tricky and match UBT platforms with TPs just for thse UBT platforms, but that complexity does not seem needed)
			if (Pair.Value.AllUBTPlatformNames.Contains(PlatformName))
			{
				return HasCompiledSupportForPlatform(Pair.Key, EPlatformNameType::Ini);
			}
		}

		return false;
	}
	else if (PlatformNameType == EPlatformNameType::TargetPlatform)
	{
		// was this TP compiled?
		return FModuleManager::Get().ModuleExists(*FString::Printf(TEXT("%sTargetPlatform"), *PlatformName));
	}

	return false;
}
#endif
