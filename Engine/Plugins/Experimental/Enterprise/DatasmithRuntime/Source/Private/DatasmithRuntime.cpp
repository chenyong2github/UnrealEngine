// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithRuntime.h"

#include "DatasmithRuntimeModule.h"
#include "LogCategory.h"
#include "SceneImporter.h"

#include "DirectLink/DirectLinkCommon.h"
#include "DirectLink/DirectLinkLog.h"
#include "DirectLink/Network/DirectLinkISceneProvider.h"
#include "IDatasmithSceneElements.h"

#include "Math/BoxSphereBounds.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"

const FBoxSphereBounds DefaultBounds(FVector::ZeroVector, FVector(2000), 1000);
const TCHAR* EmptyScene = TEXT("Nothing Loaded");

class FDatasmithRuntimeSceneProvider : public DirectLink::ISceneProvider
{
public:
	FDatasmithRuntimeSceneProvider(ADatasmithRuntimeActor* InDatasmithRuntimeActor)
		: DatasmithRuntimeActor(InDatasmithRuntimeActor)
	{
		if (DatasmithRuntimeActor.IsValid())
		{
			DeltaConsumer = MakeShared<FDatasmithDeltaConsumer>();
			DeltaConsumer->SetChangeListener(InDatasmithRuntimeActor);
		}
	}

	virtual DirectLink::ISceneProvider::ESceneStatus GetSceneStatus(const DirectLink::FSceneIdentifier& Scene) override
	{
		return DirectLink::ISceneProvider::ESceneStatus::CanCreateScene;
	}

	virtual TSharedPtr<DirectLink::IDeltaConsumer> GetDeltaConsumer(const DirectLink::FSceneIdentifier& Scene) override
	{
		// DirectLink server has received messages. Start receiving on actor's side
		if (DatasmithRuntimeActor.IsValid())
		{
			DatasmithRuntimeActor->StartReceivingDelta();
			return DeltaConsumer;
		}

		return TSharedPtr<DirectLink::IDeltaConsumer>();
	}

	virtual bool CanOpenNewConnection() override
	{
		return true;
	}

	TSharedPtr<IDatasmithScene> GetScene()
	{
		return DeltaConsumer.IsValid() ? DeltaConsumer->GetScene() : TSharedPtr<IDatasmithScene>();
	}

private:
	TWeakObjectPtr<ADatasmithRuntimeActor> DatasmithRuntimeActor;
	TSharedPtr<FDatasmithDeltaConsumer> DeltaConsumer;
};

ADatasmithRuntimeActor::ADatasmithRuntimeActor()
	: LoadedScene(EmptyScene)
{
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("DatasmithRuntimeComponent"));
	RootComponent->SetMobility(EComponentMobility::Movable);
	RootComponent->Bounds = DefaultBounds;

	PrimaryActorTick.bCanEverTick = true;
	// Don't start ticking. Ticking if when receiving scene elements thru UDP
	PrimaryActorTick.bStartWithTickEnabled = false;

	PrimaryActorTick.TickInterval = 0.1f;

	LogDatasmith.SetVerbosity( ELogVerbosity::Error );
	LogDirectLink.SetVerbosity( ELogVerbosity::Error );
	LogDirectLinkIndexer.SetVerbosity( ELogVerbosity::Error );
	LogDirectLinkNet.SetVerbosity( ELogVerbosity::Error );

	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		SceneImporter = MakeShared< DatasmithRuntime::FSceneImporter >( this );
	}
}

void ADatasmithRuntimeActor::Tick(float DeltaTime)
{
	double CurrentTime = FPlatformTime::Seconds();
	double LastClosedDeltaWaitTime = ClosedDeltaWaitTime.Load();

	if (LastClosedDeltaWaitTime > 0. && CurrentTime > LastClosedDeltaWaitTime)
	{
		SetActorTickEnabled(false);
		if (SceneProvider.IsValid())
		{
			if (bNewScene.Load())
			{
				SetScene(SceneProvider->GetScene());
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

	SceneProvider = MakeShared<FDatasmithRuntimeSceneProvider>(this);
	if (!IDatasmithRuntimeModuleInterface::Get().RegisterSceneProvider(SceneProvider))
	{
		SceneProvider.Reset();
	}
}

void ADatasmithRuntimeActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	IDatasmithRuntimeModuleInterface::Get().UnregisterSceneProvider(SceneProvider);
	SceneProvider.Reset();

	Super::EndPlay(EndPlayReason);
}

void ADatasmithRuntimeActor::StartReceivingDelta()
{
	SetActorTickEnabled(true);
	bNewScene.Store(false);
	ClosedDeltaWaitTime.Store(-1.);
}

void ADatasmithRuntimeActor::OnNewScene()
{
	bNewScene.Store(true);
}

void ADatasmithRuntimeActor::OnAddElement(TSharedPtr<IDatasmithElement> Element)
{
	if (!bNewScene.Load())
	{
		FScopeLock Lock(&UpdateContextCriticalSection);
		UpdateContext.Additions.Add(Element);
	}
}

void ADatasmithRuntimeActor::OnDeleteElement(TSharedPtr<IDatasmithElement> Element)
{
	FScopeLock Lock(&UpdateContextCriticalSection);
	UpdateContext.Deletions.Add(Element);
}

void ADatasmithRuntimeActor::OnUpdateElement(TSharedPtr<IDatasmithElement> Element)
{
	FScopeLock Lock(&UpdateContextCriticalSection);
	UpdateContext.Updates.Add(Element);
}

void ADatasmithRuntimeActor::OnCloseDelta()
{
	ClosedDeltaWaitTime.Store(FPlatformTime::Seconds());
}

void ADatasmithRuntimeActor::SetScene(TSharedPtr<IDatasmithScene> SceneElement)
{
	if (SceneElement.IsValid())
	{
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
