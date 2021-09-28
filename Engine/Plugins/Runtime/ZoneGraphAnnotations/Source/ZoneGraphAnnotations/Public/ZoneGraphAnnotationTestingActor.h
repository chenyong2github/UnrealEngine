// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"
#include "Components/PrimitiveComponent.h"
#include "DebugRenderSceneProxy.h"
#include "ZoneGraphAnnotationTestingActor.generated.h"

class UZoneGraphAnnotationTestingComponent;
class FDebugRenderSceneProxy;

/** Base class for ZoneGraph Annotation tests. */
UCLASS(Abstract, EditInlineNew)
class ZONEGRAPHANNOTATIONS_API UZoneGraphAnnotationTest : public UObject
{
	GENERATED_BODY()

public:
	virtual void Trigger() {}

#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	virtual FBox CalcBounds(const FTransform& LocalToWorld) const { return FBox(ForceInit); };
	virtual void DebugDraw(FDebugRenderSceneProxy* DebugProxy) {}
	virtual void DebugDrawCanvas(UCanvas* Canvas, APlayerController*) {}
#endif
	
	void SetOwner(UZoneGraphAnnotationTestingComponent* Owner) { OwnerComponent = Owner; OnOwnerSet(); }
	const UZoneGraphAnnotationTestingComponent* GetOwner() const { return OwnerComponent; }

protected:
	virtual void OnOwnerSet() {}

	UPROPERTY()
	UZoneGraphAnnotationTestingComponent* OwnerComponent;
};


/** Debug component to test Mass ZoneGraph Annotations. Handles tests and rendering. */
UCLASS(ClassGroup = Custom, HideCategories = (Physics, Collision, Lighting, Rendering, Mobile))
class ZONEGRAPHANNOTATIONS_API UZoneGraphAnnotationTestingComponent : public UPrimitiveComponent
{
	GENERATED_BODY()
public:
	UZoneGraphAnnotationTestingComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;

	void Trigger();

protected:
	
#if WITH_EDITOR
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
#endif

	//~ Begin UPrimitiveComponent Interface.
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void DestroyRenderState_Concurrent() override;
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	//~ End UPrimitiveComponent Interface.

#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	virtual void DebugDraw(FDebugRenderSceneProxy* DebugProxy);
	virtual void DebugDrawCanvas(UCanvas* Canvas, APlayerController*);
#endif
	
	UPROPERTY(EditAnywhere, Category = "Test", Instanced)
	TArray<UZoneGraphAnnotationTest*> Tests;

#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	FDebugDrawDelegateHelper DebugDrawDelegateHelper;
	FDelegateHandle CanvasDebugDrawDelegateHandle;
#endif
};


/** Debug actor to test Mass ZoneGraph Annotations. */
UCLASS(HideCategories = (Actor, Input, Collision, Rendering, Replication, Partition, HLOD, Cooking))
class ZONEGRAPHANNOTATIONS_API AZoneGraphAnnotationTestingActor : public AActor
{
	GENERATED_BODY()
public:
	AZoneGraphAnnotationTestingActor(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/**
	 * Simple trigger function to trigger something on the tests.
	 * Ideally this would be part of each test, but it does not work there.
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Test")
	void Trigger();

protected:
#if WITH_EDITOR
	virtual void PostEditMove(bool bFinished) override;
#endif

	UPROPERTY(Category = Default, VisibleAnywhere, meta = (AllowPrivateAccess = "true"))
	UZoneGraphAnnotationTestingComponent* TestingComp;
};
