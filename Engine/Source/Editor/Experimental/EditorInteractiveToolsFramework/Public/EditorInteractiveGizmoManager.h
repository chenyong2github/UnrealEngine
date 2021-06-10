// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InteractiveGizmoManager.h"
#include "EditorInteractiveGizmoManager.generated.h"

class UEditorInteractiveGizmoSelectionBuilder;
class UEdModeInteractiveToolsContext;
class UinteractiveGizmo;
class IToolsContextRenderAPI;

USTRUCT()
struct FActiveSelectionGizmo
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UInteractiveGizmo> Gizmo = nullptr;
	void* Owner = nullptr;
};

/**
 * UEditorInteractiveGizmoManager allows users of the Tools framework to register and create selection-based Gizmo instances.
 * For each selection-based Gizmo, a builder derived from UInteractiveGizmoSelectionBuilder is registered with the GizmoManager.
 * When the section changes, the highest priority builders for which SatisfiesCondition() return true, will be used to
 * build gizmos.
 */
UCLASS(Transient)
class EDITORINTERACTIVETOOLSFRAMEWORK_API UEditorInteractiveGizmoManager : public UInteractiveGizmoManager
{
	GENERATED_BODY()

protected:
	friend class UEdModeInteractiveToolsContext;		// to call Initialize/Shutdown

	UEditorInteractiveGizmoManager();

	/** Initialize the GizmoManager with the necessary Context-level state. UEdModeInteractiveToolsContext calls this, you should not. */
	virtual void InitializeWithEditorModeManager(IToolsContextQueriesAPI* QueriesAPI, IToolsContextTransactionsAPI* TransactionsAPI, UInputRouter* InputRouter, FEditorModeTools* InEditorModeManager);

	// UInteractiveGizmoManager interface
	virtual void Shutdown() override;

public:

	// UInteractiveGizmoManager interface
	virtual void Tick(float DeltaTime) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;
	virtual void DrawHUD(FCanvas* Canvas, IToolsContextRenderAPI* RenderAPI) override;

	/**
	 * Register a new selection gizmo type
	 * @param InGizmoSelectionBuilder new auto gizmo builder
	 */
	void RegisterGizmoSelectionType(const TObjectPtr<UEditorInteractiveGizmoSelectionBuilder> InGizmoSelectionBuilder);

	/**
	* Remove a gizmo auto type from the set of known gizmo auto types
	* @param InGizmoSelectionBuilder same object pointer that was passed to RegisterGizmoSelectionType()
	* @return true if gizmo type was found and deregistered
	*/
	bool DeregisterGizmoSelectionType(const TObjectPtr<UEditorInteractiveGizmoSelectionBuilder> InGizmoSelectionBuilder);

	/**
	 * Get all qualified gizmo auto builders based on the current state. Qualification is determined by the gizmo builder
	 * returning true from SatisfiesCondition() and relative priority. All qualified builders at the highest found priority
	 * will be returned.
	 * @return array of qualified Gizmo auto builders based on current state
	 */
	virtual TArray<TObjectPtr<UEditorInteractiveGizmoSelectionBuilder>> GetQualifiedGizmoSelectionBuilders(const FToolBuilderState& InToolBuilderState);

	/** 
	 * Set how auto gizmo resolution should occur when CreateSelectionGizmo is invoked. If bSearchLocalOnly is true, only the current
	 * @param bLocalOnly - if true, only the current gizmo manager registry will be searched for candidate gizmos. If false,
	 *   both the gizmo manager registry and any higher gizmo manager or gizmo subsystem (in the case of selection builders) will be searched
	 */
	virtual void SetGizmoSelectionBuilderResolution(bool bLocalOnly)
	{
		bSearchLocalBuildersOnly = bLocalOnly;
	}

	/**
	 * Returns the current auto gizmo resolution setting 
	 */
	virtual bool GetGizmoSelectionBuilderResolution() const
	{
		return bSearchLocalBuildersOnly;
	}

	/**
	 * Try to automatically activate a new Gizmo instance based on the current state
	 * @param Owner void pointer to whatever "owns" this Gizmo. Allows Gizmo to later be deleted using DestroyAllGizmosByOwner()
	 * @return array of new Gizmo instances that have been created and initialized
	 */
	virtual TArray<UInteractiveGizmo*> CreateSelectionGizmos(void* Owner = nullptr);

	/**
	 * Handle Editor selection changes
	 * @param Tools - Mode Manager which invoked this selection changed call
	 * @param NewSelection - Object which is undergoing selection change
	 */
	virtual void OnEditorSelectionChanged();

	/**
	 * Handle case when selection has been cleared.
	 */
	virtual void OnEditorSelectNone();

	/**
	 * Shutdown and remove a selection-based Gizmo
	 * @param Gizmo the Gizmo to shutdown and remove
	 * @return true if the Gizmo was found and removed
	 */
	virtual bool DestroySelectionGizmo(UInteractiveGizmo* Gizmo);

		/**
	 * Shutdown and remove all active auto gizmos
	 */
	virtual void DestroyAllSelectionGizmos();

protected:

	/**
	 * Returns true if selection gizmos should be visible. 
	 * @todo move this to a gizmo context object
	 */
	virtual bool GetShowSelectionGizmos();

	/**
	 * Returns true if gizmos should be visible based on the current view's engine show flag.
	 * @todo move this to a gizmo context object
	 */
	virtual bool GetShowSelectionGizmosForView(IToolsContextRenderAPI* RenderAPI);

	/**
	 * Updates active selection gizmos when show selection state changes
	 */
	void UpdateActiveSelectionGizmos();

protected:

	/** set of Currently-active Gizmos */
	UPROPERTY()
	TArray<FActiveSelectionGizmo> ActiveSelectionGizmos;

	/** Current set of GizmoSelectionBuilders */
	UPROPERTY()
	TArray<TObjectPtr<UEditorInteractiveGizmoSelectionBuilder>> GizmoSelectionBuilders;

	/** If false, only search gizmo builders in current gizmo manager. If true, also search gizmo subsystem */
	bool bSearchLocalBuildersOnly = false;

private:
	/** @todo: remove when GetShowSelectionGizmos() is moved to gizmo context object */
	FEditorModeTools* EditorModeManager = nullptr;

	/** Whether selection gizmos are enabled. UpdateActiveSelectionGizmos() determines this value each tick and updates if it has changed. */
	bool bShowSelectionGizmos = false;
};
