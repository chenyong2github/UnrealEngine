// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PCGSettings.h"
#include "PCGSubsystem.h"

#include "PCGComponent.generated.h"

class UPCGComponent;
class UPCGGraph;
class UPCGData;
class ALandscape;

#if WITH_EDITOR
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnPCGGraphGenerated, UPCGComponent*);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnPCGGraphCleaned, UPCGComponent*);
#endif

UENUM(Blueprintable)
enum class EPCGComponentInput : uint8
{
	Actor, /** Generates based on owning actor */
	Landscape,
	Other,
	// More?
	EPCGComponentInput_MAX
};

UCLASS(BlueprintType, ClassGroup = (Procedural), meta = (BlueprintSpawnableComponent))
class PCG_API UPCGComponent : public UActorComponent
{
	GENERATED_BODY()

	friend class UPCGSubsystem;

public:
	/** ~Begin UObject interface */
	virtual void PostLoad() override;
	virtual void BeginDestroy() override;
	/** ~End UObject interface */

	//~Begin UActorComponent Interface
	virtual void BeginPlay() override;
	virtual void OnComponentCreated() override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
	//~End UActorComponent Interface

	UPCGData* GetPCGData();
	UPCGData* GetInputPCGData();
	UPCGData* GetActorPCGData();
	UPCGData* GetOriginalActorPCGData();
	TArray<UPCGData*> GetPCGExclusionData();

	bool CanPartition() const;

	UPCGGraph* GetGraph() const { return Graph; }
	void SetGraph(UPCGGraph* InGraph);

	void AddToGeneratedActors(AActor* InActor);

	/** Transactionable methods to be called from details UI */
	void Generate();
	void Cleanup();

	UFUNCTION(BlueprintCallable, Category = PCG)
	void Generate(bool bForce);

	UFUNCTION(BlueprintCallable, Category = PCG)
	void Cleanup(bool bRemoveComponents, bool bSave = false);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	EPCGComponentInput InputType = EPCGComponentInput::Actor;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	int Seed = 42;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	TSet<FName> ExcludedTags;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TArray<FName> ExclusionTags_DEPRECATED;
#endif

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Properties)
	bool bActivated = true;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, AdvancedDisplay, Category = Properties)
	bool bIsPartitioned = true;

	/** Flag to indicate whether this component has run in the editor. Note that for partitionable actors, this will always be false. */
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, AdvancedDisplay, Category = Properties)
	bool bGenerated = false;

	UPROPERTY(NonPIEDuplicateTransient)
	bool bRuntimeGenerated = false;

#if WITH_EDITORONLY_DATA
	UPROPERTY(BlueprintReadWrite, EditAnywhere, AdvancedDisplay, Category = Properties, meta = (DisplayName = "Regenerate PCG volume in editor"))
	bool bRegenerateInEditor = true;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, AdvancedDisplay, Category = Properties)
	bool bDirtyGenerated = false;

	FOnPCGGraphGenerated OnPCGGraphGeneratedDelegate;
	FOnPCGGraphCleaned OnPCGGraphCleanedDelegate;
#endif

#if WITH_EDITOR
	void Refresh();
	void DirtyGenerated(bool bInDirtyCachedInput = false);
#endif

	bool IsPartitioned() const;

	/** Updates internal properties from other component, dirties as required but does not trigger Refresh */
	void SetPropertiesFromOriginal(const UPCGComponent* Original);

	UPCGSubsystem* GetSubsystem() const;

protected:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Properties)
	TObjectPtr<UPCGGraph> Graph;

private:
	UPCGData* CreatePCGData();
	UPCGData* CreateInputPCGData();
	UPCGData* CreateActorPCGData();
	UPCGData* CreateActorPCGData(AActor* Actor);
	void UpdatePCGExclusionData();

	bool ShouldGenerate(bool bForce = false) const;
	FPCGTaskId GenerateInternal(bool bForce, const TArray<FPCGTaskId>& Dependencies);
	void CleanupInternal(bool bRemoveComponents);
	void CleanupInternal(bool bRemoveComponents, TSet<TSoftObjectPtr<AActor>>& OutActorsToDelete);
	void PostProcessGraph(const FBox& InNewBounds, bool bInGenerated);

	bool GetActorsFromTags(const TSet<FName>& InTags, TSet<TWeakObjectPtr<AActor>>& OutActors, bool bCullAgainstLocalBounds);

#if WITH_EDITOR
	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PreEditUndo() override;
	virtual void PostEditUndo() override;

	void OnGraphChanged(UPCGGraph* InGraph, bool bIsStructural, bool bShouldRefresh);
	void OnGraphChanged(UPCGGraph* InGraph, bool bIsStructural);

	void SetupActorCallbacks();
	void TeardownActorCallbacks();
	void SetupTrackingCallbacks();
	void TeardownTrackingCallbacks();
	void RefreshTrackingData();

	void OnActorAdded(AActor* InActor);
	void OnActorDeleted(AActor* InActor);
	void OnActorMoved(AActor* InActor);
	void OnObjectPropertyChanged(UObject* InObject, FPropertyChangedEvent& InEvent);
	bool ActorHasExcludedTag(AActor* InActor) const;
	bool ActorIsTracked(AActor* InActor) const;

	bool UpdateExcludedActor(AActor* InActor);

	void OnActorChanged(AActor* InActor, UObject* InSourceObject, bool bActorTagChange);

	bool PopulateTrackedActorToTagsMap(bool bForce = false);
	bool AddTrackedActor(AActor* InActor, bool bForce = false);
	bool RemoveTrackedActor(AActor* InActor);
	bool UpdateTrackedActor(AActor* InActor);
	bool DirtyTrackedActor(AActor* InActor);
	void DirtyCacheFromTag(const FName& InTag);
	void DirtyCacheForAllTrackedTags();
#endif

	FBox GetGridBounds() const;
	FBox GetGridBounds(AActor* InActor) const;

	UPROPERTY(Transient, NonPIEDuplicateTransient)
	UPCGData* CachedPCGData = nullptr;

	UPROPERTY(Transient, NonPIEDuplicateTransient)
	UPCGData* CachedInputData = nullptr;

	UPROPERTY(Transient, NonPIEDuplicateTransient)
	UPCGData* CachedActorData = nullptr;

	UPROPERTY(Transient, NonPIEDuplicateTransient)
	TMap<AActor*, UPCGData*> CachedExclusionData;

	// Cached excluded actors list is serialized because we can't get it at postload time
	UPROPERTY()
	TSet<TWeakObjectPtr<AActor>> CachedExcludedActors; 

	UPROPERTY()
	TSet<TSoftObjectPtr<AActor>> GeneratedActors;

	UPROPERTY()
	FBox LastGeneratedBounds = FBox(EForceInit::ForceInit);

#if WITH_EDITOR
	bool bIsGenerating = false;
	bool bWasGeneratedPriorToUndo = false;
	FBox LastGeneratedBoundsPriorToUndo = FBox(EForceInit::ForceInit);
	FPCGTagToSettingsMap CachedTrackedTagsToSettings;
#endif

#if WITH_EDITORONLY_DATA
	// Cached tracked actors list is serialized because we can't get it at postload time
	UPROPERTY()
	TSet<TWeakObjectPtr<AActor>> CachedTrackedActors;

	TMap<TWeakObjectPtr<AActor>, TSet<FName>> CachedTrackedActorToTags;
	TMap<TWeakObjectPtr<AActor>, TSet<TObjectPtr<UObject>>> CachedTrackedActorToDependencies;
	bool bActorToTagsMapPopulated = false;
#endif
};