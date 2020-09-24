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

	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		SceneImporter = MakeShared< DatasmithRuntime::FSceneImporter >( this );
		DirectLinkHelper = MakeShared< DatasmithRuntime::FDestinationProxy >( this );
	}
}

void ADatasmithRuntimeActor::Tick(float DeltaTime)
{
	double CurrentTime = FPlatformTime::Seconds();

	if (ClosedDeltaWaitTime > 0. && CurrentTime > ClosedDeltaWaitTime)
	{
		UE_LOG(LogDatasmithRuntime, Log, TEXT("ADatasmithRuntimeActor::Tick"));
		SetActorTickEnabled(false);
		if (DirectLinkHelper.IsValid())
		{
			if (bNewScene == true)
			{
				SetScene(DirectLinkHelper->GetScene());
			}
			else
			{
				FScopeLock Lock(&UpdateContextCriticalSection);
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
	Register();
}

void ADatasmithRuntimeActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Unregister();
	Super::EndPlay(EndPlayReason);
}

void ADatasmithRuntimeActor::OnOpenDelta(/*int32 ElementsCount*/)
{
	UE_LOG(LogDatasmithRuntime, Log, TEXT("ADatasmithRuntimeActor::OnOpenDelta"));
	SetActorTickEnabled(true);
	bNewScene = false;
	ClosedDeltaWaitTime = -1.;
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
	Progress += ElementDeltaStep;
	if (bNewScene == false)
	{
		UE_LOG(LogDatasmithRuntime, Log, TEXT("ADatasmithRuntimeActor::OnAddElement"));
		FScopeLock Lock(&UpdateContextCriticalSection);
		UpdateContext.Additions.Add(Element);
	}
}

void ADatasmithRuntimeActor::OnRemovedElement(DirectLink::FSceneGraphId ElementId)
{
	Progress += ElementDeltaStep;
	UE_LOG(LogDatasmithRuntime, Log, TEXT("ADatasmithRuntimeActor::OnRemovedElement"));
	FScopeLock Lock(&UpdateContextCriticalSection);
	UpdateContext.Deletions.Add(ElementId);
}

void ADatasmithRuntimeActor::OnChangedElement(TSharedPtr<IDatasmithElement> Element)
{
	Progress += ElementDeltaStep;
	UE_LOG(LogDatasmithRuntime, Log, TEXT("ADatasmithRuntimeActor::OnUpdateElement"));
	FScopeLock Lock(&UpdateContextCriticalSection);
	UpdateContext.Updates.Add(Element);
}

void ADatasmithRuntimeActor::Register()
{
	DirectLinkHelper->RegisterDestination(*GetName());
}

void ADatasmithRuntimeActor::Unregister()
{
	DirectLinkHelper->UnregisterDestination();
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
	ClosedDeltaWaitTime = FPlatformTime::Seconds();
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
		SceneImporter->StartImport( SceneElement.ToSharedRef() );
	}
}

void ADatasmithRuntimeActor::Reset()
{
	bBuilding = false;
	Progress = 0.f;
	LoadedScene = EmptyScene;

	SceneImporter->Reset(true);

	RootComponent->Bounds = DefaultBounds;
}
