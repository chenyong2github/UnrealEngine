// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "Rigs/RigControlHierarchy.h"
#include "ControlRigGizmoActor.generated.h"

struct FActorSpawnParameters;

USTRUCT()
struct FGizmoActorCreationParam
{
	GENERATED_BODY()

	FGizmoActorCreationParam()
		: ManipObj(nullptr)
		, ControlRigIndex(INDEX_NONE)
		, ControlName(NAME_None)
		, SpawnTransform(FTransform::Identity)
		, GizmoTransform(FTransform::Identity)
		, MeshTransform(FTransform::Identity)
		, StaticMesh(nullptr)
		, Material(nullptr)
		, ColorParameterName(NAME_None)
		, Color(FLinearColor::Red)
		, bSelectable(true)
	{
	}

	UObject*	ManipObj;
	int32		ControlRigIndex;
	FName		ControlName;
	FTransform	SpawnTransform;
	FTransform  GizmoTransform;
	FTransform  MeshTransform;
	TAssetPtr<UStaticMesh> StaticMesh;
	TAssetPtr<UMaterial> Material;
	FName ColorParameterName;
	FLinearColor Color;
	bool bSelectable;
};

/** An actor used to represent a rig control */
UCLASS(NotPlaceable, Transient)
class CONTROLRIG_API AControlRigGizmoActor : public AActor
{
	GENERATED_BODY()

public:
	AControlRigGizmoActor(const FObjectInitializer& ObjectInitializer);

	// this is the one holding transform for the controls
	UPROPERTY()
	class USceneComponent* ActorRootComponent;

	// this is visual representation of the transform
	UPROPERTY(Category = StaticMesh, VisibleAnywhere, BlueprintReadOnly, meta = (ExposeFunctionCategories = "Mesh,Rendering,Physics,Components|StaticMesh", AllowPrivateAccess = "true"))
	class UStaticMeshComponent* StaticMeshComponent;

	// the name of the control this actor is referencing
	UPROPERTY()
	uint32 ControlRigIndex;

	// the name of the control this actor is referencing
	UPROPERTY()
	FName ControlName;

	// the name of the color parameter on the material
	UPROPERTY()
	FName ColorParameterName;

	UFUNCTION(BlueprintSetter)
	/** Set the control to be enabled/disabled */
	virtual void SetEnabled(bool bInEnabled);

	UFUNCTION(BlueprintGetter)
	/** Get whether the control is enabled/disabled */
	virtual bool IsEnabled() const;

	UFUNCTION(BlueprintSetter)
	/** Set the control to be selected/unselected */
	virtual void SetSelected(bool bInSelected);

	/** Get whether the control is selected/unselected */
	UFUNCTION(BlueprintGetter)
	virtual bool IsSelectedInEditor() const;

	/** Get wether the control is selectable/unselectable */
	virtual bool IsSelectable() const { return bSelectable; }

	UFUNCTION(BlueprintSetter)
	/** Set the control to be selected/unselected */
	virtual void SetSelectable(bool bInSelectable);

	UFUNCTION(BlueprintSetter)
	/** Set the control to be hovered */
	virtual void SetHovered(bool bInHovered);

	UFUNCTION(BlueprintGetter)
	/** Get whether the control is hovered */
	virtual bool IsHovered() const;

	/** Called from the edit mode each tick */
	virtual void TickControl() {};

	/** changes the gizmo color */
	virtual void SetGizmoColor(const FLinearColor& InColor);

	/** Event called when the transform of this control has changed */
	UFUNCTION(BlueprintImplementableEvent)
	void OnTransformChanged(const FTransform& NewTransform);

	/** Event called when the enabled state of this control has changed */
	UFUNCTION(BlueprintImplementableEvent)
	void OnEnabledChanged(bool bIsEnabled);

	/** Event called when the selection state of this control has changed */
	UFUNCTION(BlueprintImplementableEvent)
	void OnSelectionChanged(bool bIsSelected);

	/** Event called when the hovered state of this control has changed */
	UFUNCTION(BlueprintImplementableEvent)
	void OnHoveredChanged(bool bIsSelected);

	/** Event called when the manipulating state of this control has changed */
	UFUNCTION(BlueprintImplementableEvent)
	void OnManipulatingChanged(bool bIsManipulating);

	// this returns root component transform based on attach
	// when there is no attach, it is based on 0
	UFUNCTION(BlueprintCallable, Category = "ControlRig|Gizmo")
	void SetGlobalTransform(const FTransform& InTransform);

	UFUNCTION(BlueprintPure, Category = "ControlRig|Gizmo")
	FTransform GetGlobalTransform() const;
private:
	/** Whether this control is enabled */
	UPROPERTY(BlueprintGetter = IsEnabled, BlueprintSetter= SetEnabled, Category = "ControlRig|Gizmo")
	uint8 bEnabled : 1;

	/** Whether this control is selected */
	UPROPERTY(BlueprintGetter = IsSelectedInEditor, BlueprintSetter = SetSelected, Category = "ControlRig|Gizmo")
	uint8 bSelected : 1;

	/** Whether this control can be selected */
	UPROPERTY(BlueprintGetter = IsSelectable, BlueprintSetter = SetSelectable, Category = "ControlRig|Gizmo")
	uint8 bSelectable : 1;

	/** Whether this control is hovered */
	UPROPERTY(BlueprintGetter = IsHovered, BlueprintSetter = SetHovered, Category = "ControlRig|Gizmo")
	uint8 bHovered : 1;

};

/**
 * Creating Gizmo Param helper functions
 */
namespace FControlRigGizmoHelper
{
	extern CONTROLRIG_API AControlRigGizmoActor* CreateGizmoActor(UWorld* InWorld, UStaticMesh* InStaticMesh, const FGizmoActorCreationParam& CreationParam);
	AControlRigGizmoActor* CreateGizmoActor(UWorld* InWorld, TSubclassOf<AControlRigGizmoActor> InClass, const FGizmoActorCreationParam& CreationParam);
	extern CONTROLRIG_API AControlRigGizmoActor* CreateDefaultGizmoActor(UWorld* InWorld, const FGizmoActorCreationParam& CreationParam);

	FActorSpawnParameters GetDefaultSpawnParameter();
}