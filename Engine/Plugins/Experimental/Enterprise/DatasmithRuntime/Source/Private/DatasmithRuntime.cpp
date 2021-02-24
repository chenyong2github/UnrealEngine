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
#include "MasterMaterials/DatasmithMasterMaterialManager.h"
#include "MaterialSelectors/DatasmithRuntimeRevitMaterialSelector.h"

#if PLATFORM_WINDOWS && PLATFORM_64BITS
#include "DatasmithOpenNurbsImportOptions.h"
#endif

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

TSharedPtr<FDatasmithMasterMaterialSelector> ADatasmithRuntimeActor::ExistingRevitSelector;
TSharedPtr<FDatasmithMasterMaterialSelector> ADatasmithRuntimeActor::RuntimeRevitSelector;
TUniquePtr<DatasmithRuntime::FTranslationThread> ADatasmithRuntimeActor::TranslationThread;
TArray<TStrongObjectPtr<UDatasmithOptionsBase>> DatasmithRuntime::FTranslationThread::AllOptions;
FDatasmithTessellationOptions* DatasmithRuntime::FTranslationThread::TessellationOptions = nullptr;

void ADatasmithRuntimeActor::OnStartupModule(bool bCADRuntimeSupported)
{
	RuntimeRevitSelector = MakeShared< FDatasmithRuntimeRevitMaterialSelector >();

	using namespace DatasmithRuntime;

#if PLATFORM_WINDOWS && PLATFORM_64BITS
	if (bCADRuntimeSupported)
	{
		FTranslationThread::AllOptions.Add(Datasmith::MakeOptions<UDatasmithImportOptions>());

		TStrongObjectPtr<UDatasmithOpenNurbsImportOptions> OpenNurbsImportOptions = Datasmith::MakeOptions<UDatasmithOpenNurbsImportOptions>();
		OpenNurbsImportOptions->Options.Geometry = EDatasmithOpenNurbsBrepTessellatedSource::UseUnrealNurbsTessellation;
		FTranslationThread::AllOptions.Add(OpenNurbsImportOptions);

		TStrongObjectPtr<UDatasmithCommonTessellationOptions> CommonTessellationOptions = Datasmith::MakeOptions<UDatasmithCommonTessellationOptions>();
		FTranslationThread::AllOptions.Add(CommonTessellationOptions);
		FTranslationThread::TessellationOptions = &CommonTessellationOptions->Options;
	}
#endif

	TranslationThread = MakeUnique<FTranslationThread>();
}

void ADatasmithRuntimeActor::OnShutdownModule()
{
	ExistingRevitSelector.Reset();
	RuntimeRevitSelector.Reset();
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
	if (!RuntimeRevitSelector.IsValid())
	{
	}

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("DatasmithRuntimeComponent"));
	RootComponent->SetMobility(EComponentMobility::Movable);
	RootComponent->Bounds = DefaultBounds;

	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	PrimaryActorTick.TickInterval = 0.1f;

	TessellationOptions = FDatasmithTessellationOptions(0.3f, 0.0f, 30.0f, EDatasmithCADStitchingTechnique::StitchingSew);
}

void ADatasmithRuntimeActor::Tick(float DeltaTime)
{
	if (bReceivingStarted && bReceivingEnded)
	{
		UE_LOG(LogDatasmithRuntime, Log, TEXT("ADatasmithRuntimeActor::Tick - Process scene's changes"));
		if (!bImportingScene)
		{
			// Prevent any other DatasmithRuntime actors to import concurrently
			bImportingScene = true;

			if (TranslationResult.SceneElement.IsValid() && TranslationResult.Translator.IsValid())
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
				SceneImporter->SetTranslator(TranslationResult.Translator);
				SetScene(TranslationResult.SceneElement);

				TranslationResult.SceneElement.Reset();
				TranslationResult.Translator.Reset();
			}
			else if (bNewScene == true)
			{
				SetScene(DirectLinkHelper->GetScene());
			}
			else
			{
				EnableSelector(true);
				bBuilding = true;

				DumpDatasmithScene(DirectLinkHelper->GetScene().ToSharedRef(), TEXT("IncrementalUpdate"));

				SceneImporter->IncrementalUpdate(DirectLinkHelper->GetScene().ToSharedRef(), UpdateContext);
				UpdateContext.Additions.Empty();
				UpdateContext.Deletions.Empty();
				UpdateContext.Updates.Empty();
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
	// Unregister to DirectLink
	DirectLinkHelper->UnregisterDestination();
	DirectLinkHelper.Reset();

	// Delete scene importer
	SceneImporter.Reset();

	Super::EndPlay(EndPlayReason);
}

void ADatasmithRuntimeActor::OnOpenDelta(/*int32 ElementsCount*/)
{
	// Should not happen
	if (bReceivingStarted)
	{
		ensure(false);
		return;
	}

	UE_LOG(LogDatasmithRuntime, Log, TEXT("ADatasmithRuntimeActor::OnOpenDelta"));
	bNewScene = false;
	bReceivingStarted = DirectLinkHelper.IsValid();
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
	UE_LOG(LogDatasmithRuntime, Log, TEXT("ADatasmithRuntimeActor::OnAddElement"));
	Progress += ElementDeltaStep;
	if (bNewScene == false)
	{
		UpdateContext.Additions.Add(Element);
	}
}

void ADatasmithRuntimeActor::OnRemovedElement(DirectLink::FSceneGraphId ElementId)
{
	Progress += ElementDeltaStep;
	UE_LOG(LogDatasmithRuntime, Log, TEXT("ADatasmithRuntimeActor::OnRemovedElement"));
	UpdateContext.Deletions.Add(ElementId);
}

void ADatasmithRuntimeActor::OnChangedElement(DirectLink::FSceneGraphId ElementId, TSharedPtr<IDatasmithElement> Element)
{
	Progress += ElementDeltaStep;
	UE_LOG(LogDatasmithRuntime, Log, TEXT("ADatasmithRuntimeActor::OnUpdateElement"));
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

bool ADatasmithRuntimeActor::OpenConnectionWIndex(int32 SourceIndex)
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

void ADatasmithRuntimeActor::SetScene(TSharedPtr<IDatasmithScene> SceneElement)
{
	UE_LOG(LogDatasmithRuntime, Log, TEXT("ADatasmithRuntimeActor::SetScene"));
	if (SceneElement.IsValid())
	{
		TRACE_BOOKMARK(TEXT("Load started - %s"), *SceneElement->GetName());
		Reset();

		EnableSelector(true);

		bBuilding = true;
		LoadedScene = SceneElement->GetName();
		SceneImporter->StartImport( SceneElement.ToSharedRef() );
	}
}

void ADatasmithRuntimeActor::Reset()
{
	SceneImporter->Reset(true);

	// Reset called while importing a scene, update flag accordingly
	if (bBuilding || bReceivingStarted)
	{
		if (bBuilding)
		{
			EnableSelector(false);
		}

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
	TranslationResult.SceneElement.Reset();
	TranslationResult.Translator.Reset();

	EnableSelector(false);

	bBuilding = false;

	// Allow any other DatasmithRuntime actors to import concurrently
	bImportingScene = false;

	bReceivingStarted = false;
	bReceivingEnded = false;
}


void ADatasmithRuntimeActor::EnableSelector(bool bEnable)
{
	if (bEnable)
	{
		// Overwrite Revit material selector with the one of DatasmithRuntime
		ExistingRevitSelector = FDatasmithMasterMaterialManager::Get().GetSelector(TEXT("Revit"));
		FDatasmithMasterMaterialManager::Get().RegisterSelector(TEXT("Revit"), RuntimeRevitSelector);
	}
	else
	{
		// Restore Revit material selector
		FDatasmithMasterMaterialManager::Get().RegisterSelector(TEXT("Revit"), ExistingRevitSelector);
	}
}

bool ADatasmithRuntimeActor::LoadFile(const FString& FilePath)
{
	if( !FPaths::FileExists( FilePath ) )
	{
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

		FDatasmithTranslatableSceneSource TranslatableSceneSource(Source);
		if (!TranslatableSceneSource.IsTranslatable())
		{
			RuntimeActor->LoadedScene = TEXT("Loading failed");
			return false;
		}

		TSharedPtr<IDatasmithTranslator> Translator = TranslatableSceneSource.GetTranslator();
		if (!Translator.IsValid())
		{
			RuntimeActor->LoadedScene = TEXT("Loading failed");
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
			*FTranslationThread::TessellationOptions = RuntimeActor->TessellationOptions;

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

		RuntimeActor->TranslationResult.SceneElement = SceneElement;
		RuntimeActor->TranslationResult.Translator = Translator;

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
