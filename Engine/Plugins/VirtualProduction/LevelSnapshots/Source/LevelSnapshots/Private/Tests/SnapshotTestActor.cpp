// Copyright Epic Games, Inc. All Rights Reserved.

#include "SnapshotTestActor.h"

#include "UObject/ConstructorHelpers.h"

USubobject::USubobject()
{
	NestedChild = CreateDefaultSubobject<USubSubobject>(TEXT("NestedChild"));
}

USnapshotTestComponent::USnapshotTestComponent()
{
	Subobject = CreateDefaultSubobject<USubobject>(TEXT("Subobject"));
}

ASnapshotTestActor::ASnapshotTestActor()
{
	ConstructorHelpers::FObjectFinder<UMaterialInterface> GradientLinearMaterialFinder(TEXT("Material'/Engine/MaterialTemplates/Gradients/Gradient_Linear.Gradient_Linear'"), LOAD_Quiet | LOAD_NoWarn);
	if (GradientLinearMaterialFinder.Succeeded())
	{
		GradientLinearMaterial = GradientLinearMaterialFinder.Object;
	}

	ConstructorHelpers::FObjectFinder<UMaterialInterface> GradientRadialMaterialFinder(TEXT("Material'/Engine/MaterialTemplates/Gradients/Gradient_Radial.Gradient_Radial'"), LOAD_Quiet | LOAD_NoWarn);
	if (GradientRadialMaterialFinder.Succeeded())
	{
		GradientRadialMaterial = GradientRadialMaterialFinder.Object;
	}
	
	ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("StaticMesh'/Engine/BasicShapes/Cube.Cube'"), LOAD_Quiet | LOAD_NoWarn);
	if (CubeFinder.Succeeded())
	{
		CubeMesh = CubeFinder.Object;
	}
	ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("StaticMesh'/Engine/BasicShapes/Cylinder.Cylinder'"), LOAD_Quiet | LOAD_NoWarn);
	if (CylinderFinder.Succeeded())
	{
		CylinderMesh = CylinderFinder.Object;
	}
	
	InstancedMeshComponent = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("InstancedMeshComponent"));
	InstancedMeshComponent->SetupAttachment(RootComponent);

	PointLightComponent = CreateDefaultSubobject<UPointLightComponent>(TEXT("PointLightComponent"));
	PointLightComponent->SetupAttachment(InstancedMeshComponent);

	TestComponent = CreateDefaultSubobject<USnapshotTestComponent>(TEXT("TestComponent"));
}

bool ASnapshotTestActor::HasObjectReference(UObject* Object, bool bOnlyCheckCollections, FName MapKey) const
{
	const bool bReferencesEqual = (bOnlyCheckCollections
			|| (ObjectReference == Object && SoftPath == Object && SoftObjectPtr == Object && WeakObjectPtr == Object));

	const bool bCollectionsEqual = ObjectArray.Contains(Object)
		&& ObjectSet.Contains(Object)
		&& SoftPathArray.Contains(Object)
		&& SoftPathSet.Contains(Object)
		&& SoftObjectPtrArray.Contains(Object)
		&& SoftObjectPtrSet.Contains(Object)
		&& WeakObjectPtrArray.Contains(Object)
		&& WeakObjectPtrSet.Contains(Object)
		&& ObjectMap.Find(MapKey) && *ObjectMap.Find(MapKey) == Object
		&& SoftPathMap.Find(MapKey) && *SoftPathMap.Find(MapKey) == Object
		&& SoftObjectPtrMap.Find(MapKey) && *SoftObjectPtrMap.Find(MapKey) == Object
		&& WeakObjectPtrMap.Find(MapKey) && *WeakObjectPtrMap.Find(MapKey) == Object;

	return bReferencesEqual && bCollectionsEqual;
}

void ASnapshotTestActor::SetObjectReference(UObject* Object, FName MapKey)
{
	ClearObjectReferences();
	AddObjectReference(Object, MapKey);
}

void ASnapshotTestActor::AddObjectReference(UObject* Object, FName MapKey)
{
	ObjectReference = Object;
	ObjectArray.Add(Object);
	ObjectSet.Add(Object);
	ObjectMap.Add(MapKey, Object);

	SoftPath = Object;
	SoftPathArray.Add(Object);
	SoftPathSet.Add(Object);
	SoftPathMap.Add(MapKey, Object);

	SoftObjectPtr = Object;
	SoftObjectPtrArray.Add(Object);
	SoftObjectPtrSet.Add(Object);
	SoftObjectPtrMap.Add(MapKey, Object);

	WeakObjectPtr = Object;
	WeakObjectPtrArray.Add(Object);
	WeakObjectPtrSet.Add(Object);
	WeakObjectPtrMap.Add(MapKey, Object);
}

void ASnapshotTestActor::ClearObjectReferences()
{
	ObjectReference = nullptr;
	ObjectArray.Reset();
	ObjectSet.Reset();
	ObjectMap.Reset();

	SoftPath = nullptr;
	SoftPathArray.Reset();
	SoftPathSet.Reset();
	SoftPathMap.Reset();

	SoftObjectPtr = nullptr;
	SoftObjectPtrArray.Reset();
	SoftObjectPtrSet.Reset();
	SoftObjectPtrMap.Reset();

	WeakObjectPtr = nullptr;
	WeakObjectPtrArray.Reset();
	WeakObjectPtrSet.Reset();
	WeakObjectPtrMap.Reset();
}

void ASnapshotTestActor::PostInitProperties()
{
	Super::PostInitProperties();

	EditableInstancedSubobject = NewObject<USubobject>(this, USubobject::StaticClass(), TEXT("EditableInstancedSubobject"));
	InstancedSubobject = NewObject<USubobject>(this, USubobject::StaticClass(), TEXT("InstancedSubobject"));
	NakedSubobject = NewObject<USubobject>(this, USubobject::StaticClass(), TEXT("NakedSubobject"));;
}
