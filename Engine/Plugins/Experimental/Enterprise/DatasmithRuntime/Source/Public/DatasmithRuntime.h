// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "DirectLink/DatasmithDeltaConsumer.h"
#include "GameFramework/Actor.h"

#include "DatasmithRuntime.generated.h"

class FDatasmithRuntimeSceneProvider;
class IDatasmithScene;

namespace DatasmithRuntime
{
	class FSceneImporter;
}

struct FUpdateContext
{
	TArray<TSharedPtr<IDatasmithElement>> Additions;
	TArray<TSharedPtr<IDatasmithElement>> Deletions;
	TArray<TSharedPtr<IDatasmithElement>> Updates;
};


// UHT doesn't really like operator ::
using FDatasmithDeltaConsumer_ISceneChangeListener = FDatasmithDeltaConsumer::ISceneChangeListener; 

UCLASS( MinimalAPI )
class ADatasmithRuntimeActor 
	: public AActor
	, public FDatasmithDeltaConsumer_ISceneChangeListener
{
	GENERATED_BODY()

public:
	ADatasmithRuntimeActor();

	// AActor overrides
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick( float DeltaSeconds ) override;
	// End AActor overrides

	// ISceneChangeListener interface
	virtual void OnOpenDelta() override {OnNewScene();}
	virtual void OnNewScene() override;
	virtual void OnAddElement(TSharedPtr<IDatasmithElement> Element) override;
	virtual void OnChangedElement(TSharedPtr<IDatasmithElement> Element) override {}
	virtual void OnRemovedElement(DirectLink::FSceneGraphId ElementId) override {}
	virtual void OnCloseDelta() override;
	// End ISceneChangeListener interface

	void OnDeleteElement(TSharedPtr<IDatasmithElement> Element);
	void OnUpdateElement(TSharedPtr<IDatasmithElement> Element);

	void SetScene(TSharedPtr<IDatasmithScene> SceneElement);

	UPROPERTY(Category="DatasmithRuntime HUD", EditDefaultsOnly, BlueprintReadOnly)
	float Progress;

	UPROPERTY(Category="DatasmithRuntime HUD", EditDefaultsOnly, BlueprintReadOnly)
	bool bBuilding;

	UPROPERTY(Category="DatasmithRuntime HUD", EditDefaultsOnly, BlueprintReadOnly)
	FString LoadedScene;

	void Reset();

	void StartReceivingDelta();

private:
	TSharedPtr< DatasmithRuntime::FSceneImporter > SceneImporter;

	TSharedPtr<FDatasmithRuntimeSceneProvider> SceneProvider;
	TAtomic<bool> bNewScene;
	TAtomic<double> ClosedDeltaWaitTime;

	FCriticalSection UpdateContextCriticalSection;
	FUpdateContext UpdateContext;
};

