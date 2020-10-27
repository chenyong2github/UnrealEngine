// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "DirectLink/DatasmithSceneReceiver.h"
#include "GameFramework/Actor.h"

#include <atomic>

#include "DatasmithRuntime.generated.h"

class FDatasmithMasterMaterialSelector;
class IDatasmithScene;

namespace DatasmithRuntime
{
	class FSceneImporter;
	class FDestinationProxy;
}

struct FUpdateContext
{
	TArray<TSharedPtr<IDatasmithElement>> Additions;
	TArray<TSharedPtr<IDatasmithElement>> Updates;
	TArray<DirectLink::FSceneGraphId> Deletions;
};


// UHT doesn't really like operator ::
using FDatasmithSceneReceiver_ISceneChangeListener = FDatasmithSceneReceiver::ISceneChangeListener;

UCLASS(meta = (DisplayName = "Datasmith Destination"))
class DATASMITHRUNTIME_API ADatasmithRuntimeActor
	: public AActor
	, public FDatasmithSceneReceiver_ISceneChangeListener
{
	GENERATED_BODY()

public:
	ADatasmithRuntimeActor();

	// AActor overrides
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	// End AActor overrides

	// ISceneChangeListener interface
	virtual void OnOpenDelta(/*int32 ElementsCount*/) override;
	virtual void OnNewScene(const DirectLink::FSceneIdentifier& SceneId) override;
	virtual void OnAddElement(DirectLink::FSceneGraphId ElementId, TSharedPtr<IDatasmithElement> Element) override;
	virtual void OnChangedElement(DirectLink::FSceneGraphId ElementId, TSharedPtr<IDatasmithElement> Element) override;
	virtual void OnRemovedElement(DirectLink::FSceneGraphId ElementId) override;
	virtual void OnCloseDelta() override;
	// End ISceneChangeListener interface

	bool IsConnected();
	FString GetDestinationName() { return GetName(); }
	FString GetSourceName();
	bool OpenConnection(uint32 SourceHash);
	void CloseConnection();

	void SetScene(TSharedPtr<IDatasmithScene> SceneElement);

	UPROPERTY(Category = "DatasmithRuntime", EditDefaultsOnly, BlueprintReadOnly)
		float Progress;

	UPROPERTY(Category = "DatasmithRuntime", EditDefaultsOnly, BlueprintReadOnly)
		bool bBuilding;

	UPROPERTY(Category = "DatasmithRuntime", EditDefaultsOnly, BlueprintReadOnly)
		FString LoadedScene;

	UFUNCTION(BlueprintCallable, Category = "DatasmithRuntime")
		bool IsReceiving() { return bReceivingStarted; }

	void Reset();

	virtual void OnImportEnd();

	static void OnShutdownModule();
	static void OnStartupModule();

private:
	void EnableSelector(bool bEnable);

private:
	TSharedPtr< DatasmithRuntime::FSceneImporter > SceneImporter;

	TSharedPtr<DatasmithRuntime::FDestinationProxy> DirectLinkHelper;

	std::atomic_bool bNewScene;
	std::atomic_bool bReceivingStarted;
	std::atomic_bool bReceivingEnded;

	float ElementDeltaStep;

	static bool bImportingScene;
	FUpdateContext UpdateContext;

	static TSharedPtr< FDatasmithMasterMaterialSelector > ExistingRevitSelector;
	static TSharedPtr< FDatasmithMasterMaterialSelector > RuntimeRevitSelector;
};
