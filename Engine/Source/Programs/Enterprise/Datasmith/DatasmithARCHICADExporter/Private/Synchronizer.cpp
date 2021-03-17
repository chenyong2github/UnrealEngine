// Copyright Epic Games, Inc. All Rights Reserved.

#include "Synchronizer.h"

#include "DatasmithDirectLink.h"
#include "DatasmithSceneExporter.h"
#include "DatasmithSceneXmlWriter.h"

#ifdef TicksPerSecond
	#undef TicksPerSecond
#endif
#include "FileManager.h"
#include "Paths.h"

BEGIN_NAMESPACE_UE_AC

#define UE_AC_FULL_TRACE 0

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
	UE_AC_TraceF("FSynchronizer::Reset - %s\n", InReason);
	if (SyncDatabase != nullptr)
	{
		delete SyncDatabase;
		SyncDatabase = nullptr;
	}
}

// Inform that the project has been closed
void FSynchronizer::ProjectClosed()
{
	Reset("Project Closed");
}

// Do a snapshot of the model 3D data
void FSynchronizer::DoSnapshot(const ModelerAPI::Model& InModel)
{
	// Setup our progression
	bool OutUserCancelled = false;
	int	 NbPhases = kCommonSetUpLights - kCommonProjectInfos + 1;
#if defined(DEBUG)
	++NbPhases;
#endif
	FProgression Progression(kStrListProgression, kSyncTitle, NbPhases, FProgression::kSetFlags, &OutUserCancelled);

	// Insure we have a sync database and a snapshot scene
	if (SyncDatabase == nullptr)
	{
		SyncDatabase = new FSyncDatabase(GSStringToUE(GetProjectName()), GSStringToUE(GetAddonDataDirectory()));
	}
	TSharedRef< IDatasmithScene > Scene = SyncDatabase->GetScene();

	// Synchronisation context
	FSyncContext SyncContext(InModel, *SyncDatabase, &Progression);

	SyncDatabase->SetSceneInfo();

	DatasmithDirectLink.InitializeForScene(Scene);

	SyncDatabase->Synchronize(SyncContext);

	SyncContext.NewPhase(kDebugSaveScene);
	DumpScene(Scene);

	SyncContext.NewPhase(kSyncSnapshot);
	DatasmithDirectLink.UpdateScene(Scene);

	SyncContext.Stats.Print();
}

GS::UniString FSynchronizer::GetProjectName()
{
	API_ProjectInfo ProjectInfo;
	Zap(&ProjectInfo);
	GSErrCode GSErr = ACAPI_Environment(APIEnv_ProjectID, &ProjectInfo);
	if (GSErr == NoError)
	{
		if (ProjectInfo.location != nullptr)
		{
			IO::Name ProjectName;
			ProjectInfo.location->GetLastLocalName(&ProjectName);
			ProjectName.DeleteExtension();
			return ProjectName.ToString();
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

	return "Nameless";
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
	// FString FolderPath(FPaths::Combine(FGenericPlatformProcess::UserDir(), *(FString("Dump ") + sceneName)));
	FString FolderPath(FPaths::Combine(GSStringToUE(GetAddonDataDirectory()), *(FString("Dump ") + sceneName)));

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
	FString ArchiveName = FPaths::Combine(*FolderPath, *FString::Printf(TEXT("%s-%d.xml"), *sceneName, NbDumps++));
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
