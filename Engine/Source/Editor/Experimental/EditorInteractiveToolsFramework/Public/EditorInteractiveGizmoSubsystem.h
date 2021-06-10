// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Delegates/Delegate.h"
#include "EditorSubsystem.h"
#include "EditorInteractiveGizmoSelectionBuilder.h"

#include "EditorInteractiveGizmoSubsystem.generated.h"

class FSubsystemCollectionBase;

/**
 * The InteractiveGizmoSubsystem provides methods for registering and unregistering 
 * selection-based gizmos builders. Editor gizmo managers which are not marked local-only, 
 * will query this subsystem for qualified builders based on the current selection.
 *
 * This subsystem should also be used to register gizmo selection builders from plugins
 * by binding to the delegates returned from OnEditorGizmoSubsystemRegisterGizmoSelectionTypes() and 
 * OnEditorGizmoSubsystemDeregisterGizmoSelectionTypes().
 */
UCLASS()
class EDITORINTERACTIVETOOLSFRAMEWORK_API UEditorInteractiveGizmoSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:

	UEditorInteractiveGizmoSubsystem();

	//~ Begin USubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	virtual void Deinitialize() override;
	//~ End USubsystem interface

	/**
	 * Event which is broadcast just after default types are registered in the gizmo subsystem
	 */
	DECLARE_EVENT(UEditorInteractiveSelectionGizmoSubsystem, FOnEditorGizmoSubsystemRegisterGizmoSelectionTypes);
	FOnEditorGizmoSubsystemRegisterGizmoSelectionTypes& OnEditorGizmoSubsystemRegisterGizmoSelectionTypes() { return RegisterEditorGizmoSelectionTypesDelegate; }

	/**
	 * Event which is broadcast just before default types are deregistered in the gizmo subsystem
	 */
	DECLARE_EVENT(UEditorInteractiveSelectionGizmoSubsystem, FOnEditorGizmoSubsystemDeregisterGizmoSelectionTypes);
	FOnEditorGizmoSubsystemDeregisterGizmoSelectionTypes& OnEditorGizmoSubsystemDeregisterGizmoSelectionTypes() { return DeregisterEditorGizmoSelectionTypesDelegate; }

	/**
	 * Registers all built-in gizmo selection types and broadcast registration event.
	 */
	void RegisterBuiltinGizmoSelectionTypes();

	/**
	 * Removes all built-in gizmo selection types and broadcast deregistration event.
	 */
	void DeregisterBuiltinGizmoSelectionTypes();

	/**
	 * Register a new selection gizmo type
	 * @param InGizmoSelectionBuilder new selection gizmo builder
	 */
	void RegisterGizmoSelectionType(const TObjectPtr<UEditorInteractiveGizmoSelectionBuilder> InGizmoSelectionBuilder);

	/**
	* Remove a gizmo selection type from the set of known gizmo selection types
	* @param InGizmoSelectionBuilder same object pointer that was passed to RegisterGizmoSelectionType()
	* @return true if gizmo type was found and deregistered
	*/
	bool DeregisterGizmoSelectionType(const TObjectPtr<UEditorInteractiveGizmoSelectionBuilder> InGizmoSelectionBuilder);

	/**
	 * Clear all registered gizmo types
	 */
	void ClearGizmoSelectionTypeRegistry();

	/**
	 * Get all qualified gizmo selection builders based on the current state. Qualification is determined by the gizmo builder
	 * returning true from SatisfiesCondition() and relative priority. All qualified builders at the highest found priority
	 * will be returned.
	 * @return array of qualified Gizmo selection builders based on current state
	 */
	virtual TArray<TObjectPtr<UEditorInteractiveGizmoSelectionBuilder>> GetQualifiedGizmoSelectionBuilders(const FToolBuilderState& InToolBuilderState);


private:

	/** Current set of GizmoSelectionBuilders */
	UPROPERTY()
	TArray<TObjectPtr<UEditorInteractiveGizmoSelectionBuilder> > GizmoSelectionBuilders;

	/** Call to register gizmo types */
	FOnEditorGizmoSubsystemRegisterGizmoSelectionTypes RegisterEditorGizmoSelectionTypesDelegate;

	/** Call to deregister gizmo types */
	FOnEditorGizmoSubsystemDeregisterGizmoSelectionTypes DeregisterEditorGizmoSelectionTypesDelegate;

};