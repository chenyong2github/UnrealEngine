// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "PolygonSelectionTool.h"

#include "Components/Widget.h"
#include "Delegates/IDelegateInstance.h"
#include "EditableMesh.h"
#include "EditableMeshTypes.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/Commands/UICommandInfo.h"
#include "Framework/Commands/Commands.h"
#include "IMeshEditorModeUIContract.h"
#include "MeshEditorUtils.h"
#include "MeshElement.h"
#include "UObject/StrongObjectPtr.h"

#include "PolygonEditingToolbar.generated.h"

class AActor;
class FEditorViewportClient;
class FExtender;
class FMeshEditingUIContext;
class FChange;
class FToolBarBuilder;
class FUICommandList;
class IStaticMeshEditor;
class SWidget;
class UEditableMesh;
class UMeshEditorAssetContainer;
class UOverlayComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UViewportInteractor;
class UWireframeMeshComponent;

struct FElementIDRemappings;

// Local actions that can be invoked from this toolbar
class FPolygonEditingCommands : public TCommands<FPolygonEditingCommands>
{
public:
	FPolygonEditingCommands();

	// TCommands<> interface
	virtual void RegisterCommands() override;
	// End of TCommands<> interface

public:
	/** CommandInfo associated with the EditMode button in the toolbar */
	TSharedPtr<FUICommandInfo> EditMode;

	/** CommandInfo associated with the IncludeBackfaces button in the toolbar */
	TSharedPtr<FUICommandInfo> IncludeBackfaces;

	/** CommandInfo associated with the ExpandSelection button in the toolbar */
	TSharedPtr<FUICommandInfo> ExpandSelection;

	/** CommandInfo associated with the ShrinkSelection button in the toolbar */
	TSharedPtr<FUICommandInfo> ShrinkSelection;

	/** CommandInfo associated with the EditMode button in the toolbar */
	TSharedPtr<FUICommandInfo> Defeaturing;
};

UCLASS()
class UPolygonToolbarProxyObject : public UObject
{
	GENERATED_BODY()

public:

	/** The polygon toolbar that owns this */
	class FPolygonEditingToolbar* Owner;
};

class FPolygonEditingToolbar : public IMeshEditorModeUIContract, public TSharedFromThis<FPolygonEditingToolbar>
{
public:
	FPolygonEditingToolbar();
	virtual ~FPolygonEditingToolbar();

	/** Add polygon editing items to the StaticMesh Editor's toolbar */
	static void CreateToolbar(FToolBarBuilder& ToolbarBuilder, const TSharedRef<FUICommandList> CommandList, UStaticMesh* StaticMesh);

	/** Create menu containing different selection's modifiers */
	TSharedRef<SWidget> CreateSelectionMenu(const TSharedRef<FUICommandList> CommandList);

	/** Callback to handle enabling/disabling editing mode */
	void OnToggleEditMode();

	/** Callback to handle enabling/disabling selection of backfaces */
	void OnIncludeBackfaces();

	/** Callback to handle de-featuring */
	void OnDefeaturing();

	/** Return true if in editing mode */
	bool IsEditModeChecked();

	/** Return true if not in editing mode */
	bool IsEditModeUnchecked()
	{
		return !IsEditModeChecked();
	}

	/** Return true if backface selection is enabled */
	bool IsIncludeBackfacesChecked();

	/** Callback to handle expanding the polygon selection */
	void OnExpandSelection();

	/** Callback to handle shrinking the polygon selection */
	void OnShrinkSelection();

	/** Return true if there's a least one mesh element selected */
	bool HasSelectedElement() const;

	/** Set selection modifier */
	void SetSelectionMode(FName InSelectionModeName);

	/** Return ECheckBoxState::Checked if given selection mode name is active */
	ECheckBoxState GetSelectionModeCheckState(FName InSelectionModeName);

	/** Return command info associated with active selection modifier */
	const TSharedPtr<FUICommandInfo> GetSelectionModeCommand();

	/** Begin IMeshEditorModeEditingContract interface */
	virtual const UEditableMesh* FindEditableMesh( class UPrimitiveComponent& Component, const FEditableMeshSubMeshAddress& SubMeshAddress ) const override;
	virtual FName GetActiveAction() const override { return NAME_None; }
	virtual void TrackUndo( UObject* Object, TUniquePtr<FChange> RevertChange ) override;
	virtual void CommitSelectedMeshes() override {}
	virtual bool IsMeshElementSelected(const FMeshElement MeshElement) const override;
	virtual void GetSelectedMeshesAndElements( EEditableMeshElementType ElementType, TMap<UEditableMesh*, TArray<FMeshElement>>& OutMeshesAndElements ) override;
	virtual void GetSelectedMeshesAndVertices( TMap<UEditableMesh*, TArray<FMeshElement>>& OutMeshesAndVertices ) override
	{
		GetSelectedMeshesAndElements( EEditableMeshElementType::Vertex, /* Out */ OutMeshesAndVertices );
	}
	virtual void GetSelectedMeshesAndEdges( TMap<UEditableMesh*, TArray<FMeshElement>>& OutMeshesAndEdges ) override
	{
		GetSelectedMeshesAndElements( EEditableMeshElementType::Edge, /* Out */ OutMeshesAndEdges );
	}
	virtual void GetSelectedMeshesAndPolygons( TMap<UEditableMesh*, TArray<FMeshElement>>& OutMeshesAndPolygons ) override
	{
		GetSelectedMeshesAndElements( EEditableMeshElementType::Polygon, /* Out */ OutMeshesAndPolygons );
	}
	virtual void GetSelectedMeshesAndPolygonsPerimeterEdges(TMap<UEditableMesh*, TArray<FMeshElement>>& OutMeshesAndPolygonsEdges) override { OutMeshesAndPolygonsEdges.Reset(); }
	virtual const TArray<UEditableMesh*>& GetSelectedEditableMeshes() const override;
	virtual const TArray<UEditableMesh*>& GetSelectedEditableMeshes() override;
	virtual void SelectMeshElements( const TArray<FMeshElement>& MeshElementsToSelect ) override;
	virtual void DeselectAllMeshElements() override;
	virtual void DeselectMeshElements( const TArray<FMeshElement>& MeshElementsToDeselect ) override;
	virtual void DeselectMeshElements(const TMap<UEditableMesh*, TArray<FMeshElement>>& MeshElementsToDeselect) override
	{
		for( const auto& MeshAndElements : MeshElementsToDeselect )
		{
			DeselectMeshElements(MeshAndElements.Value);
		}
	}

	// UI related methods: not implemented yet
	virtual FMeshElement GetHoveredMeshElement(const class UViewportInteractor* ViewportInteractor) const override { return FMeshElement(); }
	virtual bool FindEdgeSplitUnderInteractor(UViewportInteractor* ViewportInteractor, const UEditableMesh* EditableMesh, const TArray<FMeshElement>& EdgeElements, FEdgeID& OutClosestEdgeID, float& OutSplit) override
	{
		return false;
	}
	virtual class UViewportInteractor* GetActiveActionInteractor() override { return nullptr; }
	virtual class UMeshFractureSettings* GetFractureSettings() override { return nullptr; }
	/** End IMeshEditorModeEditingContract interface */

	/** Begin IMeshEditorModeUIContract interface */
	virtual EEditableMeshElementType GetMeshElementSelectionMode() const override { return EEditableMeshElementType::Polygon; }
	virtual void SetMeshElementSelectionMode(EEditableMeshElementType ElementType) {}
	virtual EEditableMeshElementType GetSelectedMeshElementType() const override;
	virtual bool IsMeshElementTypeSelected(EEditableMeshElementType ElementType) const override;
	virtual bool IsMeshElementTypeSelectedOrIsActiveSelectionMode( EEditableMeshElementType ElementType ) const override { return IsMeshElementTypeSelected(ElementType); }

	virtual const TArray<TTuple<TSharedPtr<FUICommandInfo>, FUIAction>>& GetCommonActions() const override { return ActionArray; }
	virtual const TArray<TTuple<TSharedPtr<FUICommandInfo>, FUIAction>>& GetVertexActions() const override { return ActionArray; }
	virtual const TArray<TTuple<TSharedPtr<FUICommandInfo>, FUIAction>>& GetEdgeActions() const override { return ActionArray; }
	virtual const TArray<TTuple<TSharedPtr<FUICommandInfo>, FUIAction>>& GetPolygonActions() const override { return ActionArray; }
	virtual const TArray<TTuple<TSharedPtr<FUICommandInfo>, FUIAction>>& GetFractureActions() const override { return ActionArray; }

	virtual const TArray<TTuple<TSharedPtr<FUICommandInfo>, FUIAction>>& GetVertexSelectionModifiers() const override { return ActionArray; }
	virtual const TArray<TTuple<TSharedPtr<FUICommandInfo>, FUIAction>>& GetEdgeSelectionModifiers() const override { return ActionArray; }
	virtual const TArray<TTuple<TSharedPtr<FUICommandInfo>, FUIAction>>& GetPolygonSelectionModifiers() const override { return ActionArray; }
	virtual const TArray<TTuple<TSharedPtr<FUICommandInfo>, FUIAction>>& GetFractureSelectionModifiers() const override { return ActionArray; }

	virtual bool IsEditingPerInstance() const override { return false; }
	virtual void SetEditingPerInstance(bool bPerInstance) override {}
	virtual void PropagateInstanceChanges() override {}
	virtual bool CanPropagateInstanceChanges() const override { return false; }
	virtual FName GetEquippedAction( const EEditableMeshElementType ForElementType ) const override { return NAME_None; }
	virtual void SetEquippedAction(const EEditableMeshElementType ForElementType, const FName ActionToEquip) override {}
	/** End IMeshEditorModeUIContract interface */

private:
	/** Initialize the toolbar */
	bool Initialize(UStaticMesh* InStaticMesh, const TSharedRef<FUICommandList> CommandList);

	/** Populate the toolbar */
	void PopulateToolbar(FToolBarBuilder& ToolbarBuilder, const TSharedRef<FUICommandList> CommandList);

	/** Add commands related to polygon editing to the incoming command list */
	void BindCommands(const TSharedPtr<FUICommandList> CommandList);

	/** Callback when LOD index value has changed in static mesh editor */
	void OnLODModelChanged();

	/** Callback when StaticMesh::PostEditChange has been called on editied static mesh */
	void OnMeshChanged();

	/** Update the list of editable LOD from the edited static mesh */
	void UpdateEditableLODs();

	FPolygonSelectionTool* GetPolygonSelectionToolPtr()
	{
		return PolygonSelectionTool.IsValid() ? static_cast<FPolygonSelectionTool*>(PolygonSelectionTool.Get()) : nullptr;
	}

	/** Returns if the mesh processing functionalities are available */
	bool IsMeshProcessingAvailable() const;

	/* Toggles the dynamic toolbar command bindings */
	void ToggleDynamicBindings(bool bOverrideDeleteCommand) const;

	/** Callback to exit edit mode when a static mesh referenced by editable meshes has been re-imported */
	void OnObjectReimported(UObject* InObject);

	/** Clean up everything when exiting edit mode */
	void ExitEditMode();

private:

	/*
	 * Proxy UObject to pass to the undo system when performing interactions that affect state of the selection set.
	 * We need this because the UE4 undo system requires a UObject, but we're not.
	 */
	TStrongObjectPtr<UPolygonToolbarProxyObject> PolygonToolbarProxyObject;

	struct FSelectOrDeselectMeshElementsChangeInput
	{
		/** New mesh elements that should become selected */
		TArray<FMeshElement> MeshElementsToSelect;

		/** Mesh elements that should be deselected */
		TArray<FMeshElement> MeshElementsToDeselect;

		FSelectOrDeselectMeshElementsChangeInput()
			: MeshElementsToSelect()
			, MeshElementsToDeselect()
		{
		}
	};

	class FSelectOrDeselectMeshElementsChange : public FSwapChange
	{
	public:

		FSelectOrDeselectMeshElementsChange(const FSelectOrDeselectMeshElementsChangeInput& InitInput)
			: Input(InitInput)
		{
		}

		FSelectOrDeselectMeshElementsChange(FSelectOrDeselectMeshElementsChangeInput&& InitInput)
			: Input(MoveTemp(InitInput))
		{
		}

		// Parent class overrides
		virtual TUniquePtr<FChange> Execute(UObject* Object) override;
		virtual FString ToString() const override;

	private:

		/** The data we need to make this change */
		FSelectOrDeselectMeshElementsChangeInput Input;
	};

private:
	/** True if the EditMode button has been selected */
	bool bIsEditing;

	/** True if the IncludeBackfaces button has been toggled ON */
	bool bIncludeBackfaces;

	/** Pointer to the context holding onto the EditableMesh associated to the StaticMesh viewed in the StaticMesh Editor */
	TSharedPtr<FMeshEditingUIContext> EditingContext;

	/** Pointer to the StaticMesh Editor hosting the toolbar */
	IStaticMeshEditor* StaticMeshEditor;

	/** Pointer to the edited static mesh */
	UStaticMesh* StaticMesh;

	/** Pointer to the selection tool */
	TSharedPtr<FEdMode> PolygonSelectionTool;

	/** Pointer to the command list to which the commands are bound to */
	TSharedPtr<FUICommandList> BoundCommandList;

	/* Backup of the generic delete action */
	FUIAction GenericDeleteAction;

	/** Array stating if a LOD level can be edited or not */
	TArray<bool> EditableLODs;

	/** Dummy array used to implement method of the IMeshEditorModeUIContract interface */
	TArray<TTuple<TSharedPtr<FUICommandInfo>, FUIAction>> ActionArray;

	/** Flag to toggle the toolbar-specific bindings */
	mutable bool bDeleteCommandOverriden;

	/** Handle on callback when edited static mesh is re-imported */
	FDelegateHandle OnObjectReimportedHandle;

	/**
	 * True if mesh editing operations have been executed on the static mesh
	 * If true when the static mesh editor closes, a warning message will be logged about loss of mesh editing operations
	 */
	bool bTransactionsRecorded;
};
