// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "DatasmithImportOptions.h"
#include "DatasmithTranslatableSource.h"
#include "DirectLink/DatasmithSceneReceiver.h"

#include "Async/Future.h"
#include "Containers/Queue.h"
#include "GameFramework/Actor.h"

#include <atomic>

#include "DatasmithRuntime.generated.h"

class ADatasmithRuntimeActor;
class FDatasmithMasterMaterialSelector;
class FEvent;
class IDatasmithScene;
class IDatasmithTranslator;
class UDatasmithCommonTessellationOptions;
class UDatasmithOptionsBase;

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

namespace DatasmithRuntime
{
	struct FTranslationResult
	{
		TSharedPtr<IDatasmithScene>      SceneElement;
		TSharedPtr<IDatasmithTranslator> Translator;
	};

	class FTranslationJob
	{
	public:
		FTranslationJob(ADatasmithRuntimeActor* InActor, const FString& InFilePath)
			: RuntimeActor(InActor)
			, FilePath(InFilePath)
			, ThreadEvent(nullptr)
		{
		}

		FTranslationJob() : ThreadEvent(nullptr)
		{
		}

		bool Execute();

		void SetEvent(FEvent* InThreadEvent) { ThreadEvent = InThreadEvent; }

	private:
		TWeakObjectPtr<ADatasmithRuntimeActor> RuntimeActor;
		FString FilePath;
		FEvent* ThreadEvent;
	};

	class FTranslationThread
	{
	public:
		FTranslationThread() 
			: bKeepRunning(false)
			, ThreadEvent(nullptr)
		{}

		~FTranslationThread();

		void Run();

		void AddJob(FTranslationJob&& Job)
		{
			Job.SetEvent(ThreadEvent);
			JobQueue.Enqueue(MoveTemp(Job));
		}

		std::atomic_bool bKeepRunning;
		TFuture<void> ThreadResult;
		FEvent* ThreadEvent;
		TQueue< FTranslationJob, EQueueMode::Mpsc > JobQueue;

		static TArray<TStrongObjectPtr<UDatasmithOptionsBase>> AllOptions;
		static FDatasmithTessellationOptions* TessellationOptions;
	};
}

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

	UFUNCTION(BlueprintCallable, Category = "DatasmithRuntime")
	bool IsConnected();

	UFUNCTION(BlueprintCallable, Category = "DatasmithRuntime")
	FString GetDestinationName() { return GetName(); }

	UFUNCTION(BlueprintCallable, Category = "DatasmithRuntime")
	FString GetSourceName();

	UFUNCTION(BlueprintCallable, Category = "DatasmithRuntime")
	bool OpenConnectionWIndex(int32 SourceIndex);

	UFUNCTION(BlueprintCallable, Category = "DatasmithRuntime")
	void CloseConnection();

	UFUNCTION(BlueprintCallable, Category = "DatasmithRuntime")
	int32 GetSourceIndex();

	UPROPERTY(Category = "DatasmithRuntime", EditDefaultsOnly, BlueprintReadOnly)
	float Progress;

	UPROPERTY(Category = "DatasmithRuntime", EditDefaultsOnly, BlueprintReadOnly)
	bool bBuilding;

	UPROPERTY(Category = "DatasmithRuntime", EditDefaultsOnly, BlueprintReadOnly)
	FString LoadedScene;

	UPROPERTY(Category = "DatasmithRuntime", EditDefaultsOnly, BlueprintReadOnly)
	FDatasmithTessellationOptions TessellationOptions;

	UFUNCTION(BlueprintCallable, Category = "DatasmithRuntime")
	bool IsReceiving() { return bReceivingStarted; }

	UFUNCTION(BlueprintCallable, Category = "DatasmithRuntime")
	void Reset();

	UFUNCTION(BlueprintCallable, Category = "DatasmithRuntime")
	bool LoadFile(const FString& FilePath);

	void SetScene(TSharedPtr<IDatasmithScene> SceneElement);

	virtual void OnImportEnd();

	static void OnShutdownModule();
	static void OnStartupModule(bool bCADRuntimeSupported);

private:
	void EnableSelector(bool bEnable);

private:
	TSharedPtr< DatasmithRuntime::FSceneImporter > SceneImporter;

	TSharedPtr<DatasmithRuntime::FDestinationProxy> DirectLinkHelper;

	DatasmithRuntime::FTranslationResult TranslationResult;

	std::atomic_bool bNewScene;
	std::atomic_bool bReceivingStarted;
	std::atomic_bool bReceivingEnded;

	float ElementDeltaStep;

	static std::atomic_bool bImportingScene;
	FUpdateContext UpdateContext;

#if WITH_EDITOR
	int32 EnableThreadedImport = MAX_int32;
	int32 EnableCADCache = MAX_int32;
#endif

	static TSharedPtr< FDatasmithMasterMaterialSelector > ExistingRevitSelector;
	static TSharedPtr< FDatasmithMasterMaterialSelector > RuntimeRevitSelector;

	static TUniquePtr<DatasmithRuntime::FTranslationThread> TranslationThread;

	friend class DatasmithRuntime::FTranslationJob;
};
