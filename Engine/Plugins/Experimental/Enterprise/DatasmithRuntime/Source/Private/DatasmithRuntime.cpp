// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithRuntime.h"

#include "DatasmithRuntimeModule.h"
#include "DirectLinkUtils.h"
#include "LogCategory.h"
#include "SceneImporter.h"

#include "DatasmithTranslatorModule.h"
#include "IDatasmithSceneElements.h"
#include "MasterMaterials/DatasmithMasterMaterialManager.h"
#include "MaterialSelectors/DatasmithRuntimeRevitMaterialSelector.h"

#include "Math/BoxSphereBounds.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "UObject/Package.h"

const FBoxSphereBounds DefaultBounds(FVector::ZeroVector, FVector(2000), 1000);
const TCHAR* EmptyScene = TEXT("Nothing Loaded");

// Use to force sequential update of game content
bool ADatasmithRuntimeActor::bImportingScene = false;

TSharedPtr<FDatasmithMasterMaterialSelector> ADatasmithRuntimeActor::ExistingRevitSelector;
TSharedPtr<FDatasmithMasterMaterialSelector> ADatasmithRuntimeActor::RuntimeRevitSelector;

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

			if (bNewScene == true)
			{
				SetScene(DirectLinkHelper->GetScene());
			}
			else
			{
				EnableSelector(true);
				bBuilding = true;

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

bool ADatasmithRuntimeActor::OpenConnection(uint32 SourceHash)
{
	return DirectLinkHelper->CanConnect() ? DirectLinkHelper->OpenConnection(SourceHash) : false;
}

void ADatasmithRuntimeActor::CloseConnection()
{
	if (DirectLinkHelper->IsConnected())
	{
		DirectLinkHelper->CloseConnection();
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

void ADatasmithRuntimeActor::OnStartupModule()
{
	RuntimeRevitSelector = MakeShared< FDatasmithRuntimeRevitMaterialSelector >();
}

void ADatasmithRuntimeActor::OnShutdownModule()
{
	ExistingRevitSelector.Reset();
	RuntimeRevitSelector.Reset();
}
