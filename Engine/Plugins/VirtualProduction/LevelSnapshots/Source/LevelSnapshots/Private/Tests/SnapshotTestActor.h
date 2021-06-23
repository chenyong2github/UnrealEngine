// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Character.h"
#include "Materials/MaterialInterface.h"
#include "SnapshotTestActor.generated.h"

UCLASS()
class USubSubobject : public UObject
{
	GENERATED_BODY()
public:

	UPROPERTY(EditAnywhere, Category = "Level Snapshots")
	int32 IntProperty;

	UPROPERTY(EditAnywhere, Category = "Level Snapshots")
	float FloatProperty;
	
};

UCLASS()
class USubobject : public UObject
{
	GENERATED_BODY()
public:

	USubobject();
	
	UPROPERTY(EditAnywhere, Category = "Level Snapshots")
	int32 IntProperty;

	UPROPERTY(EditAnywhere, Category = "Level Snapshots")
	float FloatProperty;

	UPROPERTY(Instanced)
	USubSubobject* NestedChild;
};

UCLASS()
class USnapshotTestComponent : public UActorComponent
{
	GENERATED_BODY()
public:

	USnapshotTestComponent();
	
	UPROPERTY(EditAnywhere, Category = "Level Snapshots")
	int32 IntProperty;

	UPROPERTY(EditAnywhere, Category = "Level Snapshots")
	float FloatProperty;

	UPROPERTY(Instanced)
	USubobject* Subobject;
};

UCLASS()
class ASnapshotTestActor : public ACharacter
{
	GENERATED_BODY()
public:

	ASnapshotTestActor();

	bool HasObjectReference(UObject* Object, bool bOnlyCheckCollections = false, FName MapKey = NAME_Name) const;
	
	void SetObjectReference(UObject* Object, FName MapKey = NAME_Name);
	void AddObjectReference(UObject* Object, FName MapKey = NAME_Name);
	void ClearObjectReferences();

	//~ Begin UObject Interface
	virtual void PostInitProperties() override;
	//~ End UObject Interface
	
	
	/******************** Skipped properties  ********************/
	
	UPROPERTY()
	int32 DeprecatedProperty_DEPRECATED = 100;

	UPROPERTY(EditAnywhere, Transient, Category = "Level Snapshots")
	int32 TransientProperty = 200;

	UPROPERTY(EditAnywhere, Category = "Level Snapshots")
	int32 IntProperty;


	
	/******************** Raw references  ********************/
	
	UPROPERTY(EditAnywhere, Category = "Level Snapshots")
	UObject* ObjectReference;

	UPROPERTY(EditAnywhere, Category = "Level Snapshots")
	TArray<UObject*> ObjectArray;

	UPROPERTY(EditAnywhere, Category = "Level Snapshots")
	TSet<UObject*> ObjectSet;

	UPROPERTY(EditAnywhere, Category = "Level Snapshots")
	TMap<FName, UObject*> ObjectMap;



	/******************** FSoftObjectPath  ********************/

	UPROPERTY(EditAnywhere, Category = "Level Snapshots")
	FSoftObjectPath SoftPath;

	UPROPERTY(EditAnywhere, Category = "Level Snapshots")
	TArray<FSoftObjectPath> SoftPathArray;

	UPROPERTY(EditAnywhere, Category = "Level Snapshots")
	TSet<FSoftObjectPath> SoftPathSet;

	UPROPERTY(EditAnywhere, Category = "Level Snapshots")
	TMap<FName, FSoftObjectPath> SoftPathMap;

	

	
	/******************** TSoftObjectPtr  ********************/

	UPROPERTY(EditAnywhere, Category = "Level Snapshots")
	TSoftObjectPtr<UObject> SoftObjectPtr;

	UPROPERTY(EditAnywhere, Category = "Level Snapshots")
	TArray<TSoftObjectPtr<UObject>> SoftObjectPtrArray;

	UPROPERTY(EditAnywhere, Category = "Level Snapshots")
	TSet<TSoftObjectPtr<UObject>> SoftObjectPtrSet;

	UPROPERTY(EditAnywhere, Category = "Level Snapshots")
	TMap<FName, TSoftObjectPtr<UObject>> SoftObjectPtrMap;



	
	/******************** TWeakObjectPtr  ********************/

	UPROPERTY(EditAnywhere, Category = "Level Snapshots")
	TWeakObjectPtr<UObject> WeakObjectPtr;

	UPROPERTY(EditAnywhere, Category = "Level Snapshots")
	TArray<TWeakObjectPtr<UObject>> WeakObjectPtrArray;

	UPROPERTY(EditAnywhere, Category = "Level Snapshots")
	TSet<TWeakObjectPtr<UObject>> WeakObjectPtrSet;

	UPROPERTY(EditAnywhere, Category = "Level Snapshots")
	TMap<FName, TWeakObjectPtr<UObject>> WeakObjectPtrMap;

	

	/******************** External component references  ********************/
	
	UPROPERTY(EditAnywhere, Category = "Level Snapshots")
	UActorComponent* ExternalComponentReference;

	UPROPERTY(EditAnywhere, Category = "Level Snapshots")
	UObject* ExternalComponentReferenceAsUObject;



	
	/******************** External references  ********************/
	
	UPROPERTY(EditAnywhere, Category = "Level Snapshots")
	UMaterialInterface* GradientLinearMaterial;

	UPROPERTY(EditAnywhere, Category = "Level Snapshots")
	UMaterialInterface* GradientRadialMaterial;
	
	UPROPERTY(EditAnywhere, Category = "Level Snapshots")
	UStaticMesh* CubeMesh;

	UPROPERTY(EditAnywhere, Category = "Level Snapshots")
	UStaticMesh* CylinderMesh;



	
	/******************** Subobject Component references  ********************/

	UPROPERTY(EditAnywhere, Category = "Level Snapshots")
	UInstancedStaticMeshComponent* InstancedMeshComponent;

	UPROPERTY(EditAnywhere, Category = "Level Snapshots")
	UPointLightComponent* PointLightComponent;

	UPROPERTY(EditAnywhere, Category = "Level Snapshots")
	USnapshotTestComponent* TestComponent;

	
	/******************** Subobject references  ********************/

	UPROPERTY(EditAnywhere, Instanced, Category = "Level Snapshots")
	USubobject* EditableInstancedSubobject;

	UPROPERTY(Instanced)
	USubobject* InstancedSubobject;

	UPROPERTY()
	USubobject* NakedSubobject;
};
