// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "EditableMesh.h"
#include "MeshEditorUtils.h"
#include "MeshElement.h"
#include "UObject/StrongObjectPtr.h"

class AActor;
class FEditorViewportClient;
class UStaticMeshEditorAssetContainer;
class UOverlayComponent;
class UStaticMeshComponent;
class UViewportInteractor;
class UWireframeMesh;
class UWireframeMeshComponent;

struct FElementIDRemappings;

class FEditableMeshCache
{
public:
	static FEditableMeshCache& Get();

	~FEditableMeshCache() {}

	/** Returns editable mesh associated with component and sub-mesh address, editable mesh is created if not in cache */
	UEditableMesh* FindOrCreateEditableMesh(const UPrimitiveComponent& Component, const FEditableMeshSubMeshAddress& SubMeshAddress);

	/** Returns editable mesh associated with component and sub-mesh address, null pointer if not in cache */
	const UEditableMesh* FindEditableMesh( const UPrimitiveComponent& Component, const FEditableMeshSubMeshAddress& SubMeshAddress ) const;
	UEditableMesh* FindModifiableEditableMesh( const UPrimitiveComponent& Component, const FEditableMeshSubMeshAddress& SubMeshAddress ) const;

	/** Removes editable meshes associated with static mesh from cache if applicable */
	void RemoveObject(UStaticMesh* StaticMesh);

	/** Resets the editable meshes associated with static mesh from cache if applicable */
	void ResetObject(UStaticMesh* StaticMesh);

private:
	FEditableMeshCache() {}

	/** Callback to update cache when a static mesh referenced by editable meshes has been re-imported */
	void OnObjectReimported(UObject* InObject);

private:
	/** Cached editable meshes */
	TMap<FEditableMeshSubMeshAddress, TStrongObjectPtr<UEditableMesh> > CachedEditableMeshes;

	TMap<UStaticMesh*, const UStaticMeshComponent* > StaticMeshesToComponents;

	/** Single cache for editable meshes which have been edited */
	static FEditableMeshCache* EditableMeshCacheSingleton;
};

class FMeshEditingContext
{
protected:
	/** Structure to use as key to keep set of unique mesh elements */
	struct FMeshElementKey
	{
		UPrimitiveComponent* PrimitiveComponent;
		FEditableMeshElementAddress MeshElementAddress;

		FMeshElementKey()
		{
		}

		FMeshElementKey( const FMeshElement& MeshElement )
			: PrimitiveComponent( MeshElement.Component.Get() )
			, MeshElementAddress( MeshElement.ElementAddress )
		{
		}

		/** Equality check */
		inline bool operator==( const FMeshElementKey& Other ) const
		{
			return MeshElementAddress == Other.MeshElementAddress && PrimitiveComponent == Other.PrimitiveComponent;
		}

		/** Hashing */
		FORCEINLINE friend uint32 GetTypeHash(const FMeshElementKey& Other)
		{
			return HashCombine( HashCombine( GetTypeHash(Other.MeshElementAddress.ElementType), GetTypeHash(Other.MeshElementAddress.ElementID.GetValue()) ),
								HashCombine( GetTypeHash(Other.PrimitiveComponent), GetTypeHash(Other.MeshElementAddress.SubMeshAddress) ));
		}
	};

public:
	FMeshEditingContext();

	FMeshEditingContext(UStaticMeshComponent* InStaticMeshComponent);

	virtual ~FMeshEditingContext() {}

	/** Initialize context based on viewport and LOD index */
	virtual void Activate(FEditorViewportClient& ViewportClient, int32 InLODIndex);

	/** Reset context */
	virtual void Deactivate();

	/** Initialize editable mesh with data from incoming LOD index */
	virtual void SetLODIndex(int32 InLODIndex);

	/** Return index of LOD currently associated with the context */
	int32 GetLODIndex() const { return LODIndex; }

	/** Return true if context has a valid static mesh component */
	bool IsValid() const { return StaticMeshComponent != nullptr; }

	/** Return pointer to static mesh component attached to context */
	UStaticMeshComponent* GetStaticMeshComponent() const { return StaticMeshComponent; }

	/** Return pointer to editable mesh attached to context */
	UEditableMesh* GetEditableMesh() const { return EditableMesh; }

	/** Callback called when elements of the editable mesh are recomputed, i.e. compacting */
	virtual void OnEditableMeshElementIDsRemapped(UEditableMesh* EditableMesh, const FElementIDRemappings& Remappings);

	/** Return true if mesh element is part of selection */
	bool IsSelected(const FMeshElement& MeshElement)
	{
		return SelectedMeshElements.Find(FMeshElementKey(MeshElement)) != nullptr;
	}

	/** Clear the selection list */
	virtual void ClearSelectedElements();

	/** Remove given mesh element from selection */
	virtual void RemoveElementFromSelection(const FMeshElement& MeshElement);

	/** Add given mesh element to selection */
	virtual void AddElementToSelection(const FMeshElement& MeshElement);

	/** Add given mesh element to selection if not already in, removes it if in */
	virtual void ToggleElementSelection(const FMeshElement& MeshElement);

	void RemoveElementsFromSelection(const TArray<FMeshElement>& MeshElements)
	{
		for (const FMeshElement& MeshElement : MeshElements)
		{
			RemoveElementFromSelection(MeshElement);
		}
	}

	void AddElementsToSelection(const TArray<FMeshElement>& MeshElements)
	{
		for (const FMeshElement& MeshElement : MeshElements)
		{
			AddElementToSelection(MeshElement);
		}
	}

	void ToggleElementsSelection(const TArray<FMeshElement>& MeshElements)
	{
		for (const FMeshElement& MeshElement : MeshElements)
		{
			ToggleElementSelection(MeshElement);
		}
	}

	/** Return array of selected mesh elements of the given type */
	TArray<FMeshElement> GetSelectedElements(EEditableMeshElementType ElementType);

	/** Return true if array of selected mesh elements contains element of the given type */
	bool IsMeshElementTypeSelected(EEditableMeshElementType ElementType) const;

	void ExpandPolygonSelection();

	void ShrinkPolygonSelection();

protected:
	virtual void Reset();

protected:
	int32 LODIndex;

	UStaticMeshComponent* StaticMeshComponent;

	UEditableMesh* EditableMesh;

	/** List of selected mesh elements */
	TSet<FMeshElementKey> SelectedMeshElements;
};

class FMeshEditingUIContext : public FMeshEditingContext
{
public:
	FMeshEditingUIContext();

	FMeshEditingUIContext(UStaticMeshComponent* InStaticMeshComponent);

	virtual ~FMeshEditingUIContext() {}

	/** Initialize base context and 3D UI components */
	void Initialize(FEditorViewportClient& ViewportClient);

	/** Begin override of FMeshEditingContext */
	virtual void Activate(FEditorViewportClient& ViewportClient, int32 InLODIndex) override;
	virtual void Deactivate() override;

	virtual void SetLODIndex(int32 InLODIndex) override;

	/** Callback when UStaticMesh::PostEditChangeProperty is called on edited StaticMesh */
	void OnMeshChanged();

	virtual void ClearSelectedElements() override;
	virtual void RemoveElementFromSelection(const FMeshElement& MeshElement) override;
	virtual void AddElementToSelection(const FMeshElement& MeshElement) override;
	virtual void ToggleElementSelection(const FMeshElement& MeshElement) override;
	/** End override of FMeshEditingContext */

	/** Empty list of hovered mesh elements */
	void ClearHoveredElements();

	/** Remove given mesh element from list of hovered mesh elements */
	void RemoveHoveredElement(const FMeshElement& MeshElement);

	/** Add given mesh element from list of hovered mesh elements */
	void AddHoveredElement(const FMeshElement& MeshElement);

protected:

	/** Reset context */
	virtual void Reset() override;

private:
	/**
	 * Add given mesh element to UI 3D Widget
	 * @param: OverlayComponent		3D UI component to add to
	 * @param: MeshElement			mesh element to add to component
	 * @param: Color				color attributed to mesh aelement
	 * @param: Size					Bias size
	 * @param: bAddContour			Only used for polygon element. Specify 
	 */
	void AddMeshElementToOverlay(UOverlayComponent* OverlayComponent, const FMeshElement& MeshElement, const FColor Color, const float Size, bool bAddContour = false);
	
	/** Remove given mesh element to UI 3D Widget */
	void RemoveMeshElementFromOverlay(UOverlayComponent* OverlayComponent, const FMeshElement& MeshElement);

private:
	TWeakObjectPtr<UWireframeMesh> WireframeBaseCage;

	/** Actor which holds UI mesh components */
	TWeakObjectPtr<AActor> WireframeComponentContainer;

	/** Component containing  wireframe of the edited mesh */
	TWeakObjectPtr<UWireframeMeshComponent> WireframeMeshComponent;

	/** Component containing selected elements */
	TWeakObjectPtr<UOverlayComponent> SelectedElementsComponent;

	/** Component containing hovered elements */
	TWeakObjectPtr<UOverlayComponent> HoveredElementsComponent;

	/** Container of the UE assets used by the 3D UI */
	TStrongObjectPtr<UStaticMeshEditorAssetContainer> AssetContainer;

	/** Cached 3D UI components */
	TMap< UOverlayComponent*, TMap< FMeshElementKey, TArray<int32> > > CachedOverlayIDs;
};
