// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithRuntime.h"

#include "DatasmithRuntimeModule.h"
#include "DirectLinkUtils.h"
#include "LogCategory.h"
#include "SceneImporter.h"

#include "IDatasmithSceneElements.h"

#include "Math/BoxSphereBounds.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "UObject/Package.h"

const FBoxSphereBounds DefaultBounds(FVector::ZeroVector, FVector(2000), 1000);
const TCHAR* EmptyScene = TEXT("Nothing Loaded");

// Use to force sequential update of game content
bool ADatasmithRuntimeActor::bImportingScene = false;

ADatasmithRuntimeActor::ADatasmithRuntimeActor()
	: LoadedScene(EmptyScene)
	, bNewScene(false)
{
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("DatasmithRuntimeComponent"));
	RootComponent->SetMobility(EComponentMobility::Movable);
	RootComponent->Bounds = DefaultBounds;

	PrimaryActorTick.bCanEverTick = true;
	// Don't start ticking. Ticking if when receiving scene elements thru UDP
	PrimaryActorTick.bStartWithTickEnabled = false;

	PrimaryActorTick.TickInterval = 0.1f;
}

void ADatasmithRuntimeActor::Tick(float DeltaTime)
{
	if (DirectLinkHelper.IsValid() && !bReceiving)
	{
		UE_LOG(LogDatasmithRuntime, Log, TEXT("ADatasmithRuntimeActor::Tick"));
		if (!bImportingScene)
		{
			// Prevent any other DatasmithRuntime actors to import concurrently
			bImportingScene = true;
			SetActorTickEnabled(false);

			if (bNewScene == true)
			{
				SetScene(DirectLinkHelper->GetScene());
			}
			else
			{
				SceneImporter->IncrementalUpdate(UpdateContext);
				UpdateContext.Additions.Empty();
				UpdateContext.Deletions.Empty();
				UpdateContext.Updates.Empty();
			}
		}
	}
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
	UE_LOG(LogDatasmithRuntime, Log, TEXT("ADatasmithRuntimeActor::OnOpenDelta"));
	SetActorTickEnabled(true);
	bNewScene = false;
	bReceiving = true;
	ElementDeltaStep = /*ElementsCount > 0 ? 1.f / (float)ElementsCount : 0.f*/0.f;
}

void ADatasmithRuntimeActor::OnNewScene(const DirectLink::FSceneIdentifier& SceneId)
{
	UE_LOG(LogDatasmithRuntime, Log, TEXT("ADatasmithRuntimeActor::OnNewScene"));
	bNewScene = true;
}

void ADatasmithRuntimeActor::OnAddElement(TSharedPtr<IDatasmithElement> Element)
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

void ADatasmithRuntimeActor::OnChangedElement(TSharedPtr<IDatasmithElement> Element)
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
	bReceiving = false;
}

void ADatasmithRuntimeActor::SetScene(TSharedPtr<IDatasmithScene> SceneElement)
{
	UE_LOG(LogDatasmithRuntime, Log, TEXT("ADatasmithRuntimeActor::SetScene"));
	if (SceneElement.IsValid())
	{
		TRACE_BOOKMARK(TEXT("Load started - %s"), *SceneElement->GetName());
		Reset();

		bBuilding = true;
		LoadedScene = SceneElement->GetName();
		SceneImporter->StartImport( SceneElement.ToSharedRef(), DirectLinkHelper->GetConnectedSourceHandle() );
	}
}

void ADatasmithRuntimeActor::Reset()
{
	SceneImporter->Reset(true);

	// Reset called while importing a scene, update flag accordingly
	if (bBuilding)
	{
		bImportingScene = false;
	}

	bBuilding = false;
	Progress = 0.f;
	LoadedScene = EmptyScene;

	RootComponent->Bounds = DefaultBounds;
}

void ADatasmithRuntimeActor::OnImportEnd()
{
	bBuilding = false;

	// Allow any other DatasmithRuntime actors to import concurrently
	bImportingScene = false;
}