// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "Rigs/RigControlHierarchy.h"
#include "ControlRigGizmoActor.generated.h"

struct FActorSpawnParameters;
class IControlRigManipulatable;

USTRUCT()
struct FGizmoActorCreationParam
{
	GENERATED_BODY()

	FGizmoActorCreationParam()
		: ManipObj(nullptr)
		, ControlName(NAME_None)
		, SpawnTransform(FTransform::Identity)
		, GizmoTransform(FTransform::Identity)
		, MeshTransform(FTransform::Identity)
		, StaticMesh(nullptr)
		, Material(nullptr)
		, ColorParameterName(NAME_None)
		, Color(FLinearColor::Red)
	{
	}

	IControlRigManipulatable*	ManipObj;
	FName		ControlName;
	FTransform	SpawnTransform;
	FTransform  GizmoTransform;
	FTransform  MeshTransform;
	TAssetPtr<UStaticMesh> StaticMesh;
	TAssetPtr<UMaterial> Material;
	FName ColorParameterName;
	FLinearColor Color;
};

/** An actor used to represent a rig control */
UCLASS()
class CONTROLRIGMANIPULATION_API AControlRigGizmoActor : public AActor
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

	UFUNCTION(BlueprintSetter)
	/** Set the control to be enabled/disabled */
	virtual void SetEnabled(bool bInEnabled);

	UFUNCTION(BlueprintGetter)
	/** Get whether the control is enabled/disabled */
	virtual bool IsEnabled() const;

	UFUNCTION(BlueprintSetter)
	/** Set the control to be selected/unselected */
	virtual void SetSelected(bool bInSelected);

	UFUNCTION(BlueprintGetter)
	/** Get whether the control is selected/unselected */
	virtual bool IsSelectedInEditor() const override;

	UFUNCTION(BlueprintSetter)
	/** Set the control to be hovered */
	virtual void SetHovered(bool bInHovered);

	UFUNCTION(BlueprintGetter)
	/** Get whether the control is hovered */
	virtual bool IsHovered() const;

	UFUNCTION(BlueprintSetter)
	/** Set whether the control is being manipulated */
	virtual void SetManipulating(bool bInManipulating);

	UFUNCTION(BlueprintGetter)
	/** Get whether the control is being manipulated */
	virtual bool IsManipulating() const;

	/** Called from the edit mode each tick */
	virtual void TickControl() {};

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

	/** Whether this control is hovered */
	UPROPERTY(BlueprintGetter = IsHovered, BlueprintSetter = SetHovered, Category = "ControlRig|Gizmo")
	uint8 bHovered : 1;

	/** Whether this control is being manipulated */
	UPROPERTY(BlueprintGetter = IsManipulating, BlueprintSetter = SetManipulating, Category = "ControlRig|Gizmo")
	uint8 bManipulating : 1;
};

/**
 * Creating Gizmo Param helper functions
 */
namespace FControlRigGizmoHelper
{
	extern CONTROLRIGMANIPULATION_API AControlRigGizmoActor* CreateGizmoActor(UWorld* InWorld, UStaticMesh* InStaticMesh, const FGizmoActorCreationParam& CreationParam);
	AControlRigGizmoActor* CreateGizmoActor(UWorld* InWorld, TSubclassOf<AControlRigGizmoActor> InClass, const FGizmoActorCreationParam& CreationParam);
	extern CONTROLRIGMANIPULATION_API AControlRigGizmoActor* CreateDefaultGizmoActor(UWorld* InWorld, const FGizmoActorCreationParam& CreationParam);

	FActorSpawnParameters GetDefaultSpawnParameter();
}