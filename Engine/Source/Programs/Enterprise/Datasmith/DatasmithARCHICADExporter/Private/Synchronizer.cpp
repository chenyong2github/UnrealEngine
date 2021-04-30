// Copyright Epic Games, Inc. All Rights Reserved.

#include "Synchronizer.h"
#include "MaterialsDatabase.h"
#include "Commander.h"
#include "Utils/SceneValidator.h"
#include "Utils/TimeStat.h"
#include "Utils/Error.h"

#include "DatasmithDirectLink.h"
#include "DatasmithSceneExporter.h"
#include "DatasmithSceneXmlWriter.h"
#include "IDirectLinkUI.h"
#include "IDatasmithExporterUIModule.h"

#ifdef TicksPerSecond
	#undef TicksPerSecond
#endif

DISABLE_SDK_WARNINGS_START

#include "FileManager.h"
#include "Paths.h"
#include "Version.h"

DISABLE_SDK_WARNINGS_END

BEGIN_NAMESPACE_UE_AC

#define UE_AC_FULL_TRACE 0

enum : GSType
{
	DatasmithDynamicLink = 'DsDL'
}; // Can be called by another Add-on

// Add menu to the menu bar and also add an item to palette menu
GSErrCode FSynchronizer::Register()
{
	return ACAPI_Register_SupportedService(DatasmithDynamicLink, 1L);
}

// Enable handlers of menu items
GSErrCode FSynchronizer::Initialize()
{
	GSErrCode GSErr = ACAPI_Install_ModulCommandHandler(DatasmithDynamicLink, 1L, SyncCommandHandler);
	if (GSErr != NoError)
	{
		UE_AC_DebugF("FSynchronizer::Initialize - ACAPI_Install_ModulCommandHandler error=%s\n", GetErrorName(GSErr));
	}
	return GSErr;
}

// Intra add-ons command handler
GSErrCode __ACENV_CALL FSynchronizer::SyncCommandHandler(GSHandle ParHdl, GSPtr /* ResultData */,
														 bool /* SilentMod */) noexcept
{
	return TryFunctionCatchAndAlert("FSynchronizer::DoSyncCommand",
									[ParHdl]() -> GSErrCode { return FSynchronizer::DoSyncCommand(ParHdl); });
}

static bool bPostSent = false;

// Process intra add-ons command
GSErrCode FSynchronizer::DoSyncCommand(GSHandle ParHdl)
{
	GSErrCode GSErr = NoError;

	if (ParHdl == nullptr)
	{
		return APIERR_GENERAL;
	}

	Int32 NbPars = 0;
	GSErr = ACAPI_Goodies(APIAny_GetMDCLParameterNumID, ParHdl, &NbPars);
	if (GSErr != NoError)
	{
		UE_AC_DebugF("FSynchronizer::DoSyncCommand - APIAny_GetMDCLParameterNumID error %s\n", GetErrorName(GSErr));
		return GSErr;
	}

	if (NbPars != 1)
	{
		UE_AC_DebugF("FSynchronizer::DoSyncCommand - Invalid number of parameters %d\n", NbPars);
		return APIERR_BADPARS;
	}

	API_MDCLParameter Param = {};
	Param.index = 1;
	GSErr = ACAPI_Goodies(APIAny_GetMDCLParameterID, ParHdl, &Param);
	if (GSErr != NoError)
	{
		UE_AC_DebugF("FSynchronizer::DoSyncCommand - APIAny_GetMDCLParameterID 1 error %s\n", GetErrorName(GSErr));
		return GSErr;
	}
	if (CHCompareCStrings(Param.name, "Reason", CS_CaseSensitive) != 0 || Param.type != MDCLPar_string)
	{
		UE_AC_DebugF("FSynchronizer::DoSyncCommand - Invalid parameters (type=%d) %s\n", Param.type, Param.name);
		return APIERR_BADPARS;
	}

	if (bPostSent == true)
	{
		bPostSent = false;
		if (Is3DCurrenWindow())
		{
			UE_AC_TraceF("FSynchronizer::DoSyncCommand - Auto Sync for %s\n", Param.string_par);
			FCommander::DoSnapshot();
		}
		else
		{
			PostDoSnapshot(Param.string_par);
		}
	}

	return GSErr;
}

// Schedule a Auto Sync snapshot to be executed from the main thread event loop.
void FSynchronizer::PostDoSnapshot(const utf8_t* InReason)
{
	if (bPostSent == false && FCommander::IsAutoSyncEnabled())
	{
		GSHandle  ParHdl = nullptr;
		GSErrCode GSErr = ACAPI_Goodies(APIAny_InitMDCLParameterListID, &ParHdl);
		if (GSErr == NoError)
		{
			API_MDCLParameter Param;
			Zap(&Param);
			Param.name = "Reason";
			Param.type = MDCLPar_string;
			Param.string_par = InReason;
			GSErr = ACAPI_Goodies(APIAny_AddMDCLParameterID, ParHdl, &Param);
			if (GSErr == NoError)
			{
				API_ModulID mdid;
				Zap(&mdid);
				mdid.developerID = kEpicGamesDevId;
				mdid.localID = kDatasmithExporterId;
				GSErr = ACAPI_Command_CallFromEventLoop(&mdid, DatasmithDynamicLink, 1, ParHdl, false, nullptr);
				if (GSErr == NoError)
				{
					ParHdl = nullptr;
					bPostSent = true; // Only one post at a time
				}
				else
				{
					UE_AC_DebugF("FSynchronizer::PostDoSnapshot - ACAPI_Command_CallFromEventLoop error %s\n",
								 GetErrorName(GSErr));
				}
			}
			else
			{
				UE_AC_DebugF("FSynchronizer::PostDoSnapshot - APIAny_AddMDCLParameterID error %s\n",
							 GetErrorName(GSErr));
			}

			if (ParHdl != nullptr)
			{
				GSErr = ACAPI_Goodies(APIAny_FreeMDCLParameterListID, &ParHdl);
				if (GSErr != NoError)
				{
					UE_AC_DebugF("FSynchronizer::PostDoSnapshot - APIAny_FreeMDCLParameterListID error %s\n",
								 GetErrorName(GSErr));
				}
			}
		}
		else
		{
			UE_AC_DebugF("FSynchronizer::PostDoSnapshot - APIAny_InitMDCLParameterListID error %s\n",
						 GetErrorName(GSErr));
		}
	}
}

static FSynchronizer* CurrentSynchonizer = nullptr;

// Return the synchronizer (create it if not already created)
FSynchronizer& FSynchronizer::Get()
{
	if (CurrentSynchonizer == nullptr)
	{
		CurrentSynchonizer = new FSynchronizer();
	}

	return *CurrentSynchonizer;
}

// Return the current synchronizer if any
FSynchronizer* FSynchronizer::GetCurrent()
{
	return CurrentSynchonizer;
}

// FreeData is called, so we must free all our stuff
void FSynchronizer::DeleteSingleton()
{
	if (CurrentSynchonizer)
	{
		delete CurrentSynchonizer;
		CurrentSynchonizer = nullptr;
	}
}

// Constructor
FSynchronizer::FSynchronizer()
	: DatasmithDirectLink(*new FDatasmithDirectLink)
	, SyncDatabase(nullptr)
{
}

// Destructor
FSynchronizer::~FSynchronizer()
{
	Reset("Synchronizer deleted");

	delete &DatasmithDirectLink;
}

// Delete the database (Usualy because document has changed)
void FSynchronizer::Reset(const utf8_t* InReason)
{
	if (FCommander::IsAutoSyncEnabled())
	{
		FCommander::ToggleAutoSync();
	}
	AttachObservers.Stop();

	UE_AC_TraceF("FSynchronizer::Reset - %s\n", InReason);
	if (SyncDatabase != nullptr)
	{
		delete SyncDatabase;
		SyncDatabase = nullptr;
	}
}

// Delete the database (Usualy because document has changed)
void FSynchronizer::ProjectOpen()
{
	if (SyncDatabase != nullptr)
	{
		UE_AC_DebugF("FSynchronizer::ProjectOpen - Previous project hasn't been closed before ???");
		Reset("Project Open");
	}

	// Create a new synchronization database
	GS::UniString ProjectPath;
	GS::UniString ProjectName;
	GetProjectPathAndName(&ProjectPath, &ProjectName);
	SyncDatabase = new FSyncDatabase(GSStringToUE(ProjectPath), GSStringToUE(ProjectName), *GetExportPath());

	// Announce it to potential receivers
#if ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION == 26
	TSharedRef< IDatasmithScene > ToBuildWith_4_26(SyncDatabase->GetScene());
	DatasmithDirectLink.InitializeForScene(ToBuildWith_4_26);
#else
	DatasmithDirectLink.InitializeForScene(SyncDatabase->GetScene());
#endif
}

// Inform that current project has been save (maybe name changed)
void FSynchronizer::ProjectSave()
{
	if (SyncDatabase != nullptr)
	{
		GS::UniString ProjectPath;
		GetProjectPathAndName(&ProjectPath, nullptr);

		FString SanitizedName(FDatasmithUtils::SanitizeObjectName(GSStringToUE(ProjectPath)));
		if (FCString::Strcmp(*SanitizedName, SyncDatabase->GetScene()->GetName()) == 0)
		{
			// Name is the same
			return;
		}

		UE_AC_TraceF("FSynchronizer::ProjectSave - Project saved under a new name");
		Reset("Project Renamed"); // There's no way to change to rename DirecLink connection
	}
	else
	{
		UE_AC_DebugF("FSynchronizer::ProjectSave - Project hasn't been open before ???");
	}

	ProjectOpen();
}

// Inform that the project has been closed
void FSynchronizer::ProjectClosed()
{
	Reset("Project Closed");
}

// Return the export path from the ExporterUIModule or a default one
FString FSynchronizer::GetExportPath()
{
	const TCHAR*				CacheDirectory = nullptr;
	IDatasmithExporterUIModule* DsExporterUIModule = IDatasmithExporterUIModule::Get();
	if (DsExporterUIModule != nullptr)
	{
		IDirectLinkUI* DLUI = DsExporterUIModule->GetDirectLinkExporterUI();
		if (DLUI != nullptr)
		{
			CacheDirectory = DLUI->GetDirectLinkCacheDirectory();
		}
	}
	if (CacheDirectory != nullptr)
	{
		return CacheDirectory;
	}
	else
	{
		return GSStringToUE(GetAddonDataDirectory());
	}
}

// Do a snapshot of the model 3D data
void FSynchronizer::DoSnapshot(const ModelerAPI::Model& InModel)
{
	FTimeStat DoSnapshotStart;

	// Setup our progression
	bool OutUserCancelled = false;
	int	 NbPhases = kCommonSetUpLights - kCommonProjectInfos + 1;
#if defined(DEBUG)
	++NbPhases;
#endif
	FProgression Progression(kStrListProgression, kSyncTitle, NbPhases, FProgression::kSetFlags, &OutUserCancelled);

	ViewState = FViewState();

	FString ExportPath = GetExportPath();

	// If we have a sync database validate it use the ExportPath
	if (SyncDatabase != nullptr)
	{
		if (ExportPath != SyncDatabase->GetAssetsFolderPath())
		{
			delete SyncDatabase;
			SyncDatabase = nullptr;
		}
	}

	// Insure we have a sync database and a snapshot scene
	if (SyncDatabase == nullptr)
	{
		GS::UniString ProjectPath;
		GS::UniString ProjectName;
		GetProjectPathAndName(&ProjectPath, &ProjectName);

		SyncDatabase = new FSyncDatabase(GSStringToUE(ProjectPath), GSStringToUE(ProjectName), *ExportPath);

#if ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION == 26
		TSharedRef< IDatasmithScene > ToBuildWith_4_26(SyncDatabase->GetScene());
		DatasmithDirectLink.InitializeForScene(ToBuildWith_4_26);
#else
		DatasmithDirectLink.InitializeForScene(SyncDatabase->GetScene());
#endif
	}
	// Synchronisation context
	FSyncContext SyncContext(true, InModel, *SyncDatabase, &Progression);

	SyncDatabase->SetSceneInfo();

	SyncDatabase->Synchronize(SyncContext);

	FTimeStat DoSnapshotSyncEnd;

	SyncDatabase->GetMaterialsDatabase().UpdateModified(SyncContext);

#ifdef DEBUG
	if (!FCommander::IsAutoSyncEnabled()) // In Auto Sync mode we don't do scene dump or validation
	{
		SyncContext.NewPhase(kDebugSaveScene);
		DumpScene(SyncDatabase->GetScene());
		FSceneValidator Validator(SyncDatabase->GetScene());
		Validator.CheckElementsName();
		Validator.CheckDependances();
		Validator.PrintReports(FSceneValidator::kVerbose);
	}
#endif

	FTimeStat DoSnapshotDumpAndValidatorEnd;

	SyncContext.NewPhase(kSyncSnapshot);

#if ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION == 26
	TSharedRef< IDatasmithScene > ToBuildWith_4_26(SyncDatabase->GetScene());
	DatasmithDirectLink.UpdateScene(ToBuildWith_4_26);
#else
	DatasmithDirectLink.UpdateScene(SyncDatabase->GetScene());
#endif

	SyncContext.Stats.Print();
	FTimeStat DoSnapshotEnd;
	DoSnapshotSyncEnd.PrintDiff("Synchronization", DoSnapshotStart);
#ifdef DEBUG
	if (!FCommander::IsAutoSyncEnabled()) // In Auto Sync mode we don't do scene dump or validation
	{
		DoSnapshotDumpAndValidatorEnd.PrintDiff("Dump & Validator", DoSnapshotSyncEnd);
	}
#endif
	DoSnapshotEnd.PrintDiff("DirectLink Update", DoSnapshotDumpAndValidatorEnd);
	DoSnapshotEnd.PrintDiff("Total DoSnapshot", DoSnapshotStart);

	AttachObservers.Start(&SyncDatabase->GetSceneSyncData());
}

void FSynchronizer::DoIdle(int* IOCount)
{
	// If we wait for a snapshoot to be processed
	if (bPostSent)
	{
		// We do nothing until we have processed the pending request
		return;
	}

	// If we need to schedule an Auto Sync
	if (NeedAutoSyncUpdate())
	{
		PostDoSnapshot("View or material modified");
		return;
	}

	// If we need to schedule an Auto Sync
	if (AttachObservers.ProcessUntil(FTimeStat::RealTimeClock() + 1.0 / 3.0))
	{
		PostDoSnapshot("Process detect modification");
		return;
	}

	// If we need to process more
	if (AttachObservers.NeedProcess())
	{
		*IOCount = 2;
	}
}

// Auto Sync related: If view changed shedule an update
bool FSynchronizer::NeedAutoSyncUpdate() const
{
	FViewState CurrentViewState;
	if (!(ViewState == CurrentViewState))
	{
		return true;
	};
	if (SyncDatabase != nullptr && SyncDatabase->GetMaterialsDatabase().CheckModify())
	{
		return true;
	}
	return false;
}

void FSynchronizer::GetProjectPathAndName(GS::UniString* OutPath, GS::UniString* OutName)
{
	API_ProjectInfo ProjectInfo;
	Zap(&ProjectInfo);
	GSErrCode GSErr = ACAPI_Environment(APIEnv_ProjectID, &ProjectInfo);
	if (GSErr == NoError)
	{
		if (ProjectInfo.location != nullptr)
		{
			if (OutPath != nullptr)
			{
				ProjectInfo.location->ToPath(OutPath);
			}
			if (OutName != nullptr)
			{
				IO::Name ProjectName;
				ProjectInfo.location->GetLastLocalName(&ProjectName);
				ProjectName.DeleteExtension();
				*OutName = ProjectName.ToString();
			}
			return;
		}
		else
		{
			// Maybe ArchiCAD is running as demo
			UE_AC_DebugF("CIdentity::GetFromProjectInfo - No project locations\n");
		}
	}
	else
	{
		UE_AC_DebugF("CIdentity::GetFromProjectInfo - Error(%d) when accessing project info\n", GSErr);
	}

	if (OutPath != nullptr)
	{
		*OutPath = "Nameless";
	}

	if (OutName != nullptr)
	{
		*OutName = "Nameless";
	}
}

void FSynchronizer::DumpScene(const TSharedRef< IDatasmithScene >& InScene)
{
	static bool bDoDump = true;
	if (!bDoDump) // To active dump with recompiling, set sDoDump to true with the debugger
	{
		return;
	}

	// Define a directory same name as scene.
	FString sceneName(InScene->GetName());
	if (sceneName.IsEmpty())
	{
		sceneName = TEXT("Unnamed");
	}
	FString FolderPath(FPaths::Combine(GSStringToUE(GetAddonDataDirectory()), *(FString("Dumps ") + sceneName)));

	// If we change scene, we delete and recreate the folder
	static int	   NbDumps = 0;
	static FString PreviousFolderPath;
	if (FolderPath != PreviousFolderPath)
	{
		NbDumps = 0;
		PreviousFolderPath = FolderPath;
		IFileManager::Get().DeleteDirectory(*FolderPath, false, true);
		IFileManager::Get().MakeDirectory(*FolderPath);
	}

	// Create dump file (starting from 0)
	FString ArchiveName = FPaths::Combine(*FolderPath, *FString::Printf(TEXT("Dump %d.xml"), NbDumps++));
	UE_AC_TraceF("Dump scene ---> %s\n", TCHAR_TO_UTF8(*ArchiveName));
	TUniquePtr< FArchive > archive(IFileManager::Get().CreateFileWriter(*ArchiveName));
	if (archive.IsValid())
	{
		FDatasmithSceneXmlWriter().Serialize(InScene, *archive);
	}
	else
	{
		UE_AC_DebugF("Dump scene Error can create archive file %s\n", TCHAR_TO_UTF8(*ArchiveName));
	}
}

END_NAMESPACE_UE_AC
