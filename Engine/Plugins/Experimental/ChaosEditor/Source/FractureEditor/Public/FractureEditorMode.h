// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdMode.h"
#include "EditorUndoClient.h"
#include "GeometryCollection/GeometryCollectionActor.h"

class UGeometryCollectionComponent;

namespace FractureTransactionContexts
{
	static const TCHAR SelectBoneContext[] = TEXT("SelectGeometryCollectionBone");
};

class FFractureEditorMode : public FEdMode, public FEditorUndoClient
{
public:
	const static FEditorModeID EM_FractureEditorModeId;
public:
	FFractureEditorMode();
	virtual ~FFractureEditorMode();

	using FEdMode::PostUndo;
	using FEditorUndoClient::PostUndo;

	// FEdMode interface 
	virtual void Enter() override;
	virtual void Exit() override;
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;

	// FEditorUndoClient interface
	virtual bool MatchesContext(const FTransactionContext& InContext, const TArray<TPair<UObject *, FTransactionObjectEvent>>& TransactionObjectContexts) const override;
	virtual void PostUndo(bool bSuccess) override; 
	virtual void PostRedo(bool bSuccess) override;

	//virtual void Tick(FEditorViewportClient* ViewportClient, float DeltaTime) override;
	virtual void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) override;

	bool UsesToolkits() const override;
	virtual bool InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event) override;
	virtual bool HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click) override;

	virtual bool BoxSelect(FBox& InBox, bool InSelect = true) override;
	virtual bool FrustumSelect(const FConvexVolume& InFrustum, FEditorViewportClient* InViewportClient, bool InSelect) override;
	virtual bool ComputeBoundingBoxForViewportFocus(AActor* Actor, UPrimitiveComponent* PrimitiveComponent, FBox& InOutBox) const;
	virtual bool GetPivotForOrbit(FVector& OutPivot) const override;
	// End of FEdMode interface
private:
	void OnUndoRedo();
	void OnActorSelectionChanged(const TArray<UObject*>& NewSelection, bool bForceRefresh);
	void GetActorGlobalBounds(TArrayView<UGeometryCollectionComponent*> GeometryComponents, TMap<int32, FBox> &BoundsToBone) const;
	void SelectionStateChanged();
	
	/** Handle package reloading (might be our geometry collection) */
	void HandlePackageReloaded(const EPackageReloadPhase InPackageReloadPhase, FPackageReloadedEvent* InPackageReloadedEvent);

	static FConvexVolume TranformFrustum(const FConvexVolume& InFrustum, const FMatrix& InMatrix);
	static FConvexVolume GetVolumeFromBox(const FBox &InBox);
private:
	/** This selection set is updated from actor selection changed event.  We change state on components as they are selected so we have to maintain or own list **/
	TArray<UGeometryCollectionComponent*> SelectedGeometryComponents;
	// Hack: We have to set this to work around the editor defaulting to orbit around selection and breaking our custom per-bone orbiting
	mutable TOptional<FVector> CustomOrbitPivot;
};
