// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "WorldPartition/Landscape/LandscapeActorDescFactory.h"
#include "LandscapeSubsystem.generated.h"

class ALandscapeProxy;
class ULandscapeInfo;
class ULandscapeInfoMap;

UCLASS(MinimalAPI)
class ULandscapeSubsystem : public UWorldSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	ULandscapeSubsystem(const FObjectInitializer& ObjectInitializer);
	virtual ~ULandscapeSubsystem();

	void RegisterActor(ALandscapeProxy* Proxy);
	void UnregisterActor(ALandscapeProxy* Proxy);

	void ForEachLandscapeInfo(TFunctionRef<bool(ULandscapeInfo*)> Predicate);
	LANDSCAPE_API static void ForEachLandscapeInfo(const UWorld* InWorld, TFunctionRef<bool(ULandscapeInfo*)> Predicate);

	ULandscapeInfo* GetLandscapeInfo(FGuid LandscapeGuid, bool bCreate = false);
	LANDSCAPE_API static ULandscapeInfo* GetLandscapeInfo(const UWorld* InWorld, FGuid LandscapeGuid, bool bCreate = false);

	// Begin FTickableGameObject overrides
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickableInEditor() const override { return true; }
	virtual ETickableTickType GetTickableTickType() const override;
	virtual TStatId GetStatId() const override;
	// End FTickableGameObject overrides

#if WITH_EDITOR
	LANDSCAPE_API void BuildGrassMaps();
	LANDSCAPE_API int32 GetOutdatedGrassMapCount();

	LANDSCAPE_API bool IsGridBased() const;
	LANDSCAPE_API void UpdateGrid(ULandscapeInfo* LandscapeInfo, uint32 GridSizeInComponents);
	LANDSCAPE_API ALandscapeProxy* FindOrAddLandscapeProxy(ULandscapeInfo* LandscapeInfo, const FIntPoint& SectionBase);

	void RecreateLandscapeInfos(bool bMapCheck);
	LANDSCAPE_API static void RecreateLandscapeInfos(UWorld* InWorld, bool bMapCheck);
#endif

private:
	// Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	// End USubsystem

	TArray<ALandscapeProxy*> Proxies;

	UPROPERTY()
	ULandscapeInfoMap* LandscapeInfos;

#if WITH_EDITOR
	class FLandscapeGrassMapsBuilder* GrassMapsBuilder;
	FLandscapeActorDescFactory LandscapeActorDescFactory;
#endif
};
