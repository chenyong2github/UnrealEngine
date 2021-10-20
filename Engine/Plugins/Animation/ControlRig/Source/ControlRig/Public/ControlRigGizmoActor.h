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
struct FControlShapeActorCreationParam
{
	GENERATED_BODY()

	FControlShapeActorCreationParam()
		: ManipObj(nullptr)
		, ControlRigIndex(INDEX_NONE)
		, ControlName(NAME_None)
		, SpawnTransform(FTransform::Identity)
		, ShapeTransform(FTransform::Identity)
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
	FTransform  ShapeTransform;
	FTransform  MeshTransform;
	TSoftObjectPtr<UStaticMesh> StaticMesh;
	TSoftObjectPtr<UMaterial> Material;
	FName ColorParameterName;
	FLinearColor Color;
	bool bSelectable;
};

/** An actor used to represent a rig control */
UCLASS(NotPlaceable, Transient)
class CONTROLRIG_API AControlRigShapeActor : public AActor
{
	GENERATED_BODY()

public:
	AControlRigShapeActor(const FObjectInitializer& ObjectInitializer);

	// this is the one holding transform for the controls
	UPROPERTY()
	TObjectPtr<class USceneComponent> ActorRootComponent;

	// this is visual representation of the transform
	UPROPERTY(Category = StaticMesh, VisibleAnywhere, BlueprintReadOnly, meta = (ExposeFunctionCategories = "Mesh,Rendering,Physics,Components|StaticMesh", AllowPrivateAccess = "true"))
	TObjectPtr<class UStaticMeshComponent> StaticMeshComponent;

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

	/** changes the shape color */
	virtual void SetShapeColor(const FLinearColor& InColor);

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
	UFUNCTION(BlueprintCallable, Category = "ControlRig|Shape")
	void SetGlobalTransform(const FTransform& InTransform);

	UFUNCTION(BlueprintPure, Category = "ControlRig|Shape")
	FTransform GetGlobalTransform() const;
private:
	/** Whether this control is enabled */
	UPROPERTY(BlueprintGetter = IsEnabled, BlueprintSetter= SetEnabled, Category = "ControlRig|Shape")
	uint8 bEnabled : 1;

	/** Whether this control is selected */
	UPROPERTY(BlueprintGetter = IsSelectedInEditor, BlueprintSetter = SetSelected, Category = "ControlRig|Shape")
	uint8 bSelected : 1;

	/** Whether this control can be selected */
	UPROPERTY(BlueprintGetter = IsSelectable, BlueprintSetter = SetSelectable, Category = "ControlRig|Shape")
	uint8 bSelectable : 1;

	/** Whether this control is hovered */
	UPROPERTY(BlueprintGetter = IsHovered, BlueprintSetter = SetHovered, Category = "ControlRig|Shape")
	uint8 bHovered : 1;

};

/**
 * Creating Shape Param helper functions
 */
namespace FControlRigShapeHelper
{
	extern CONTROLRIG_API AControlRigShapeActor* CreateShapeActor(UWorld* InWorld, UStaticMesh* InStaticMesh, const FControlShapeActorCreationParam& CreationParam);
	AControlRigShapeActor* CreateShapeActor(UWorld* InWorld, TSubclassOf<AControlRigShapeActor> InClass, const FControlShapeActorCreationParam& CreationParam);
	extern CONTROLRIG_API AControlRigShapeActor* CreateDefaultShapeActor(UWorld* InWorld, const FControlShapeActorCreationParam& CreationParam);

	FActorSpawnParameters GetDefaultSpawnParameter();
}