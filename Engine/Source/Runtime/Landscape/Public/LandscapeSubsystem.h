// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "LandscapeSubsystem.generated.h"

class ALandscapeProxy;
class ULandscapeInfo;

UCLASS(MinimalAPI)
class ULandscapeSubsystem : public UWorldSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	ULandscapeSubsystem();
	virtual ~ULandscapeSubsystem();

	void RegisterActor(ALandscapeProxy* Proxy);
	void UnregisterActor(ALandscapeProxy* Proxy);

	// Begin FTickableGameObject overrides
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickableInEditor() const override { return true; }
	virtual ETickableTickType GetTickableTickType() const override;
	virtual TStatId GetStatId() const override;
	// End FTickableGameObject overrides

#if WITH_EDITOR
	LANDSCAPE_API void BuildAll();
	LANDSCAPE_API void BuildGrassMaps();
	LANDSCAPE_API int32 GetOutdatedGrassMapCount();
	LANDSCAPE_API void BuildGIBakedTextures();
	LANDSCAPE_API int32 GetOutdatedGIBakedTextureComponentsCount();
	LANDSCAPE_API void BuildPhysicalMaterial();
	LANDSCAPE_API int32 GetOudatedPhysicalMaterialComponentsCount();
	LANDSCAPE_API bool IsGridBased() const;
	LANDSCAPE_API void ChangeGridSize(ULandscapeInfo* LandscapeInfo, uint32 NewGridSizeInComponents);
	LANDSCAPE_API ALandscapeProxy* FindOrAddLandscapeProxy(ULandscapeInfo* LandscapeInfo, const FIntPoint& SectionBase);
	LANDSCAPE_API void DisplayBuildMessages(class FCanvas* Canvas, float& XPos, float& YPos);
#endif

private:
	// Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	// End USubsystem

	TArray<ALandscapeProxy*> Proxies;

#if WITH_EDITOR
	class FLandscapeGrassMapsBuilder* GrassMapsBuilder;
	class FLandscapeGIBakedTextureBuilder* GIBakedTextureBuilder;
	class FLandscapePhysicalMaterialBuilder* PhysicalMaterialBuilder;
#endif
};
