// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/PrimitiveComponent.h"
#include "PrimitiveSceneProxy.h"
#include "PersonaSelectionComponent.generated.h"

class UPersonaSelectionComponent;

USTRUCT()
struct FPersonaSelectionCapsule
{
	GENERATED_BODY()

	FPersonaSelectionCapsule()
	{
		Name = NAME_None;
		Transform = FTransform::Identity;
		Radius = HalfHeight = 0.f;
	}

	UPROPERTY()
	FName Name;

	UPROPERTY()
	FTransform Transform;

	UPROPERTY()
	float Radius;

	UPROPERTY()
	float HalfHeight;
};

struct PERSONA_API HPersonaSelectionHitProxy : public HHitProxy
{
	DECLARE_HIT_PROXY()

	int32 CapsuleIndex;
	UPersonaSelectionComponent* Component;
	
	HPersonaSelectionHitProxy()
		:CapsuleIndex(INDEX_NONE)
		,Component(nullptr)
	{}

	HPersonaSelectionHitProxy(int32 InCapsuleIndex, UPersonaSelectionComponent* InComponent)
		: HHitProxy(HPP_Foreground)
		, CapsuleIndex(InCapsuleIndex)
		, Component(InComponent) 
	{
	}

	const FPersonaSelectionCapsule& GetCapsule() const;

	void BroadcastClicked() const;
	
	// HHitProxy interface
	virtual EMouseCursor::Type GetMouseCursor() override { return EMouseCursor::Crosshairs; }
	// End of HHitProxy interface
};

// called when a persona selection capsule has been clicked on
DECLARE_DELEGATE_ThreeParams(FOnPersonaSelectionClicked, UPersonaSelectionComponent*, int32 /*Index*/, const FPersonaSelectionCapsule& /*CapsuleInfo*/);

// called when a persona selection component requires an update for a set of capsules
DECLARE_DELEGATE_ThreeParams(FOnPersonaSelectionUpdateCapsules, UPersonaSelectionComponent*, const TArray<int32>& /*Indices*/, TArray<FPersonaSelectionCapsule>& /*AllCapsules*/);

UCLASS(Blueprintable, ClassGroup = "Persona")
class PERSONA_API UPersonaSelectionComponent : public UPrimitiveComponent
{
	GENERATED_UCLASS_BODY()

protected:

	//~ Begin UPrimitiveComponent Interface.
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	virtual void GetUsedMaterials( TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials = false ) const override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction);
	//~ End UPrimitiveComponent Interface.

public:

	/** Removes all capsules from the component */
	void Reset();
	
	/** Returns the number of capsules in this selection component */
	FORCEINLINE int32 Num() const { return Capsules.Num(); }

	FORCEINLINE const FPersonaSelectionCapsule& operator[](int32 InIndex) const { return Capsules[InIndex]; }
	FORCEINLINE FPersonaSelectionCapsule& operator[](int32 InIndex) { return Capsules[InIndex]; }
	FORCEINLINE TArray<FPersonaSelectionCapsule>::RangedForIteratorType      begin()       { return Capsules.begin(); }
	FORCEINLINE TArray<FPersonaSelectionCapsule>::RangedForConstIteratorType begin() const { return Capsules.begin(); }
	FORCEINLINE TArray<FPersonaSelectionCapsule>::RangedForIteratorType      end()         { return Capsules.end();   }
	FORCEINLINE TArray<FPersonaSelectionCapsule>::RangedForConstIteratorType end() const   { return Capsules.end();   }

	/** Sets the number of capsules */
	void SetNum(int32 InCount);

	/** Adds one or more capsules and returns the first added index */
	int32 Add(int32 InCount = 1);

	/** Marks the capsules as changed and dirties rendering */ 
	void MarkCapsulesChanged();

	/** Returns the delegate firing when a capsule has been clicked on */
	FOnPersonaSelectionClicked& OnClicked() { return OnPersonaSelectioneClicked; }

	/** Returns the delegate used to retrieve the transforms for all capsules */
	FOnPersonaSelectionUpdateCapsules& OnUpdateCapsules() { return OnPersonaSelectionUpdateCapsules; }

	/** Computes a capsule given start end end position of a bone */
	static void ComputeCapsuleFromBonePositions(
    	const FVector& InWorldStart,
    	const FVector& InWorldEnd,
    	float InBounds,
    	float InRadiusMultiplier,
    	FPersonaSelectionCapsule& OutCapsule
    	);

private:

	void SetCapsulesIndicesToFullArray();

	/** The capsules stored in this component */
	UPROPERTY()
	TArray<FPersonaSelectionCapsule> Capsules;

	/** The indices of capsules we want to retrieve */
	UPROPERTY()
	TArray<int32> CapsuleIndices;

	/** If set to true the capsule shapes will be visible in the main pass */
	UPROPERTY()
	bool bVisibleInMainPass;

	/** Hit proxies for this capsule component */
	TArray<TRefCountPtr<HPersonaSelectionHitProxy>> HitProxies;

	/** Fired when a capsule is clicked on */
	FOnPersonaSelectionClicked OnPersonaSelectioneClicked;

	/** Fired when the component needs to update a set of capsules */
	FOnPersonaSelectionUpdateCapsules OnPersonaSelectionUpdateCapsules;

	friend class FPersonaSelectionComponentProxy;
};

class FPersonaSelectionComponentProxy : public FPrimitiveSceneProxy
{
public:

	SIZE_T GetTypeHash() const override;
	FPersonaSelectionComponentProxy(const UPersonaSelectionComponent* InComponent);

	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override;

	/**
	*  Returns a struct that describes to the renderer when to draw this proxy.
	*	@param		Scene view to use to determine our relevence.
	*  @return		View relevance struct
	*/
	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override;
	virtual uint32 GetMemoryFootprint(void) const override;
	uint32 GetAllocatedSize(void) const;

private:

	TWeakObjectPtr<const UPersonaSelectionComponent> SelectionComponentPtr;
	bool bVisibleInMainPass;
};