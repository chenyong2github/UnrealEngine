// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithRuntime.h"

#include "DatasmithRuntimeModule.h"
#include "DirectLinkUtils.h"
#include "LogCategory.h"
#include "SceneImporter.h"

#include "DirectLink/DatasmithDirectLinkTools.h"
#include "DirectLinkSceneSnapshot.h"

#include "DatasmithTranslatorModule.h"
#include "IDatasmithSceneElements.h"
#include "DatasmithSceneFactory.h"
#include "DatasmithTranslator.h"

#include "Async/Async.h"
#include "Math/BoxSphereBounds.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "UObject/Package.h"

#include "Misc/Paths.h"

#if WITH_EDITOR
#include "HAL/IConsoleManager.h"
#endif

const FBoxSphereBounds DefaultBounds(FVector::ZeroVector, FVector(2000), 1000);
const TCHAR* EmptyScene = TEXT("Nothing Loaded");

// Use to force sequential update of game content
std::atomic_bool ADatasmithRuntimeActor::bImportingScene(false);

TUniquePtr<DatasmithRuntime::FTranslationThread> ADatasmithRuntimeActor::TranslationThread;
TArray<TStrongObjectPtr<UDatasmithOptionsBase>> DatasmithRuntime::FTranslationThread::AllOptions;
FDatasmithTessellationOptions* DatasmithRuntime::FTranslationThread::TessellationOptions = nullptr;

void ADatasmithRuntimeActor::OnStartupModule(bool bCADRuntimeSupported)
{
	using namespace DatasmithRuntime;

#if PLATFORM_WINDOWS && PLATFORM_64BITS
	if (bCADRuntimeSupported)
	{
		FTranslationThread::AllOptions.Add(Datasmith::MakeOptions<UDatasmithImportOptions>());

		TStrongObjectPtr<UDatasmithCommonTessellationOptions> CommonTessellationOptions = Datasmith::MakeOptions<UDatasmithCommonTessellationOptions>();
		FTranslationThread::AllOptions.Add(CommonTessellationOptions);
		FTranslationThread::TessellationOptions = &CommonTessellationOptions->Options;
	}
#endif

	TranslationThread = MakeUnique<FTranslationThread>();
}

void ADatasmithRuntimeActor::OnShutdownModule()
{
	DatasmithRuntime::FTranslationThread::AllOptions.Empty();
	DatasmithRuntime::FTranslationThread::TessellationOptions = nullptr;
	TranslationThread.Reset();
}

ADatasmithRuntimeActor::ADatasmithRuntimeActor()
	: LoadedScene(EmptyScene)
	, bNewScene(false)
	, bReceivingStarted(false)
	, bReceivingEnded(false)
{
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("DatasmithRuntimeComponent"));
	AddInstanceComponent( RootComponent );
	RootComponent->SetMobility(EComponentMobility::Movable);
	RootComponent->Bounds = DefaultBounds;

	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	PrimaryActorTick.TickInterval = 0.1f;

	ImportOptions.TessellationOptions = FDatasmithTessellationOptions(0.3f, 0.0f, 30.0f, EDatasmithCADStitchingTechnique::StitchingSew);
}

void ADatasmithRuntimeActor::Tick(float DeltaTime)
{
	if (SceneElement.IsValid() && bReceivingStarted && bReceivingEnded)
	{
		UE_LOG(LogDatasmithRuntime, Log, TEXT("ADatasmithRuntimeActor::Tick - Process scene's changes"));
		if (!bImportingScene)
		{
			// Prevent any other DatasmithRuntime actors to import concurrently
			bImportingScene = true;

			if (Translator.IsValid())
			{

#if WITH_EDITOR
				if (EnableThreadedImport != MAX_int32)
				{
					IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.CADTranslator.EnableThreadedImport"));
					CVar->Set(EnableThreadedImport);
				}

				if (EnableCADCache != MAX_int32)
				{
					IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.CADTranslator.EnableCADCache"));
					CVar->Set(EnableCADCache);
				}
#endif
				SceneImporter->SetTranslator(Translator);
				ApplyNewScene();
			}
			else if (bNewScene == true)
			{
				ApplyNewScene();
			}
			else
			{
				bBuilding = true;

				SceneImporter->IncrementalUpdate(SceneElement.ToSharedRef(), UpdateContext);
				UpdateContext.Additions.Empty();
				UpdateContext.Deletions.Empty();
				UpdateContext.Updates.Empty();

				SceneElement.Reset();
			}

			bReceivingStarted = false;
			bReceivingEnded = false;
		}
	}

	Super::Tick(DeltaTime);
}

void ADatasmithRuntimeActor::BeginPlay()
{
	Super::BeginPlay();

	// Create scene importer
	SceneImporter = MakeShared< DatasmithRuntime::FSceneImporter >( this );

	// Register to DirectLink
	DirectLinkHelper = MakeShared< DatasmithRuntime::FDestinationProxy >( this );
	DirectLinkHelper->RegisterDestination(*GetName());
}

void ADatasmithRuntimeActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Reset();

	// Unregister to DirectLink
	DirectLinkHelper->UnregisterDestination();
	DirectLinkHelper.Reset();

	// Delete scene importer
	SceneImporter.Reset();
	SceneElement.Reset();
	Translator.Reset();

	Super::EndPlay(EndPlayReason);
}

void ADatasmithRuntimeActor::OnOpenDelta(/*int32 ElementsCount*/)
{
	// Block the DirectLink thread, if we are still processing the previous delta
	while (bReceivingStarted || bImportingScene)
	{
		FPlatformProcess::SleepNoStats(0.1f);
	}

	UE_LOG(LogDatasmithRuntime, Log, TEXT("ADatasmithRuntimeActor::OnOpenDelta"));
	bNewScene = false;
	bReceivingStarted = DirectLinkHelper.IsValid();
	if (bReceivingStarted)
	{
		SceneElement = DirectLinkHelper->GetScene();
	}
	bReceivingEnded = false;
	ElementDeltaStep = /*ElementsCount > 0 ? 1.f / (float)ElementsCount : 0.f*/0.f;
}

void ADatasmithRuntimeActor::OnNewScene(const DirectLink::FSceneIdentifier& SceneId)
{
	UE_LOG(LogDatasmithRuntime, Log, TEXT("ADatasmithRuntimeActor::OnNewScene"));
	bNewScene = true;
}

void ADatasmithRuntimeActor::OnAddElement(DirectLink::FSceneGraphId ElementId, TSharedPtr<IDatasmithElement> Element)
{
	Progress += ElementDeltaStep;
	if (bNewScene == false)
	{
		UpdateContext.Additions.Add(Element);
	}
}

void ADatasmithRuntimeActor::OnRemovedElement(DirectLink::FSceneGraphId ElementId)
{
	Progress += ElementDeltaStep;
	UpdateContext.Deletions.Add(ElementId);
}

void ADatasmithRuntimeActor::OnChangedElement(DirectLink::FSceneGraphId ElementId, TSharedPtr<IDatasmithElement> Element)
{
	Progress += ElementDeltaStep;
	UpdateContext.Updates.Add(Element);
}

bool ADatasmithRuntimeActor::IsConnected()
{
	return DirectLinkHelper->IsConnected();
}

FString ADatasmithRuntimeActor::GetSourceName()
{
	return DirectLinkHelper->GetSourceName();
}

bool ADatasmithRuntimeActor::OpenConnectionWithIndex(int32 SourceIndex)
{
	using namespace DatasmithRuntime;

	if (DirectLinkHelper->CanConnect())
	{
		const TArray<FDatasmithRuntimeSourceInfo>& SourcesList = FDestinationProxy::GetListOfSources();

		if (SourcesList.IsValidIndex(SourceIndex))
		{
			return DirectLinkHelper->OpenConnection(SourcesList[SourceIndex].SourceHandle);
		}
		else if (SourceIndex == INDEX_NONE)
		{
			CloseConnection();
			Reset();
			return true;
		}
	}

	return false;
}

int32 ADatasmithRuntimeActor::GetSourceIndex()
{
	using namespace DatasmithRuntime;

	if (DirectLinkHelper->IsConnected())
	{
		const TArray<FDatasmithRuntimeSourceInfo>& SourcesList = FDestinationProxy::GetListOfSources();

		const int32 SourceIndex = SourcesList.IndexOfByPredicate(
			[SourceHandle = DirectLinkHelper->GetConnectedSourceHandle()](const FDatasmithRuntimeSourceInfo& SourceInfo) -> bool
			{
				return SourceInfo.SourceHandle == SourceHandle;
			});

		return SourceIndex;
	}

	return INDEX_NONE;
}

void ADatasmithRuntimeActor::CloseConnection()
{
	if (DirectLinkHelper->IsConnected())
	{
		DirectLinkHelper->CloseConnection();
		Reset();
	}
}

void ADatasmithRuntimeActor::OnCloseDelta()
{
	// Something is wrong
	if (!bReceivingStarted)
	{
		ensure(false);
		return;
	}

	bReceivingEnded = DirectLinkHelper.IsValid();
}

void ADatasmithRuntimeActor::ApplyNewScene()
{
	TRACE_BOOKMARK(TEXT("Load started - %s"), *SceneElement->GetName());

	UE_LOG(LogDatasmithRuntime, Log, TEXT("ADatasmithRuntimeActor::ApplyNewScene"));

	SceneImporter->Reset(true);

	RootComponent->Bounds = DefaultBounds;

	bBuilding = true;
	Progress = 0.f;
	LoadedScene = SceneElement->GetName();
	SceneImporter->StartImport( SceneElement.ToSharedRef(), ImportOptions );

	SceneElement.Reset();
}

void ADatasmithRuntimeActor::Reset()
{
	SceneImporter->Reset(true);

	// Reset called while importing a scene, update flag accordingly
	if (bBuilding || bReceivingStarted)
	{
		bImportingScene = false;
	}

	bReceivingStarted = false;
	bReceivingEnded = false;

	bBuilding = false;
	Progress = 0.f;
	LoadedScene = EmptyScene;

	RootComponent->Bounds = DefaultBounds;
}

void ADatasmithRuntimeActor::OnImportEnd()
{
	Translator.Reset();

	bBuilding = false;

	// Allow any other DatasmithRuntime actors to import concurrently
	bImportingScene = false;

	bReceivingStarted = false;
	bReceivingEnded = false;
}

bool ADatasmithRuntimeActor::LoadFile(const FString& FilePath)
{
	if( !FPaths::FileExists( FilePath ) )
	{
		return false;
	}

	// Wait for any ongoing import to complete
	// #ue_datasmithruntime: To do add code to interrupt 
	while (bReceivingStarted || bImportingScene)
	{
		FPlatformProcess::SleepNoStats(0.1f);
	}

	// Temporarily manually disable load of ifc, gltf, PlmXml, Rhino and wire files
	FString Extension = FPaths::GetExtension(FilePath);
	bool bUnsupported = Extension.Equals(TEXT("3dm"), ESearchCase::IgnoreCase)
						|| Extension.Equals(TEXT("ifc"), ESearchCase::IgnoreCase)
						|| Extension.Equals(TEXT("glb"), ESearchCase::IgnoreCase)
						|| Extension.Equals(TEXT("gltf"), ESearchCase::IgnoreCase)
						|| Extension.Equals(TEXT("xml"), ESearchCase::IgnoreCase)
						|| Extension.Equals(TEXT("plmxml"), ESearchCase::IgnoreCase)
						|| Extension.Equals(TEXT("wire"), ESearchCase::IgnoreCase);
	if (bUnsupported)
	{
		UE_LOG(LogDatasmithRuntime, Log, TEXT("Extension %s is not supported yet."), *Extension);
		return false;
	}

#if WITH_EDITOR
	EnableThreadedImport = MAX_int32;
	if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.CADTranslator.EnableThreadedImport")))
	{
		EnableThreadedImport = CVar->GetInt();
		CVar->Set(0);
	}

	EnableCADCache = MAX_int32;
	if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.CADTranslator.EnableCADCache")))
	{
		EnableCADCache = CVar->GetInt();
		CVar->Set(0);
	}
#endif

	CloseConnection();

	Progress = 0.f;

	if (!TranslationThread->ThreadResult.IsValid())
	{
		TranslationThread->ThreadResult = Async(EAsyncExecution::Thread,
			[&]() -> void
			{
				FPlatformProcess::SetThreadName(TEXT("RuntimeTranslation"));
				TranslationThread->bKeepRunning = true;
				TranslationThread->ThreadEvent = FPlatformProcess::GetSynchEventFromPool();
				TranslationThread->Run();
			}
		);
	}

	TranslationThread->AddJob({ this, FilePath });

	// Set all import options to defaults for DatasmithRuntime
	return true;
}

namespace DatasmithRuntime
{
	bool FTranslationJob::Execute()
	{
		if (!RuntimeActor.IsValid()|| ThreadEvent == nullptr)
		{
			return false;
		}

		FDatasmithSceneSource Source;
		Source.SetSourceFile(FilePath);

		RuntimeActor->ExternalFile.Empty();

		FDatasmithTranslatableSceneSource TranslatableSceneSource(Source);
		if (!TranslatableSceneSource.IsTranslatable())
		{
			RuntimeActor->LoadedScene = Source.GetSourceFileExtension() + TEXT(" file format is not supported");
			return false;
		}

		TSharedPtr<IDatasmithTranslator> Translator = TranslatableSceneSource.GetTranslator();
		if (!Translator.IsValid())
		{
			RuntimeActor->LoadedScene = Source.GetSourceFileExtension() + TEXT(" file format is not supported");
			return false;
		}

		while(RuntimeActor->IsReceiving())
		{
			ThreadEvent->Wait(FTimespan::FromMilliseconds(50));
		}

		RuntimeActor->OnOpenDelta();

		FDatasmithTessellationOptions DefaultTessellation;
		if (FTranslationThread::AllOptions.Num() > 0)
		{
			DefaultTessellation = *FTranslationThread::TessellationOptions;
			*FTranslationThread::TessellationOptions = RuntimeActor->ImportOptions.TessellationOptions;

			Translator->SetSceneImportOptions(FTranslationThread::AllOptions);
		}

		RuntimeActor->LoadedScene = Source.GetSceneName();

		TSharedRef<IDatasmithScene> SceneElement = FDatasmithSceneFactory::CreateScene(*RuntimeActor->LoadedScene);

		if (!Translator->LoadScene( SceneElement ))
		{
			if (FTranslationThread::AllOptions.Num() > 0)
			{
				*FTranslationThread::TessellationOptions = DefaultTessellation;
			}

			RuntimeActor->LoadedScene = TEXT("Loading failed");
			return false;
		}

		if (FTranslationThread::AllOptions.Num() > 0)
		{
			*FTranslationThread::TessellationOptions = DefaultTessellation;
		}

		DirectLink::BuildIndexForScene(&SceneElement.Get());

		RuntimeActor->SceneElement = SceneElement;
		RuntimeActor->Translator = Translator;
		RuntimeActor->ExternalFile = FilePath;

		RuntimeActor->OnCloseDelta();

		return true;
	}

	void FTranslationThread::Run()
	{
		while (bKeepRunning)
		{
			FTranslationJob TranslationJob;
			if (JobQueue.Dequeue(TranslationJob))
			{
				TranslationJob.Execute();
				continue;
			}

			ThreadEvent->Wait(FTimespan::FromMilliseconds(50));
		}
	}

	FTranslationThread::~FTranslationThread()
	{
		if (bKeepRunning)
		{
			bKeepRunning = false;
			ThreadEvent->Trigger();
			ThreadResult.Get();
			FPlatformProcess::ReturnSynchEventToPool(ThreadEvent);
		}
	}
}
