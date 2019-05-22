// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComponentSourceInterfaces.h"




/**
 * Component Source Factory for use in the Editor (ie inside FEdMode, etc)
 */
class EDITORINTERACTIVETOOLSFRAMEWORK_API FEditorComponentSourceFactory : public IComponentSourceFactory
{
public:
	virtual ~FEditorComponentSourceFactory() {}

	virtual TUniquePtr<IMeshDescriptionSource> MakeMeshDescriptionSource(UActorComponent* Component);
};





class UStaticMeshComponent;

/**
 * This MeshDescriptionSource provides a specific LOD from a StaticMeshComponent
 */
class EDITORINTERACTIVETOOLSFRAMEWORK_API FStaticMeshComponentMeshDescriptionSource : public IMeshDescriptionSource
{
public:
	UStaticMeshComponent* Component;
	int LODIndex;

	FStaticMeshComponentMeshDescriptionSource(
		UStaticMeshComponent* ComponentIn,
		int LODIndex = 0);
	virtual ~FStaticMeshComponentMeshDescriptionSource() {}

	virtual AActor* GetOwnerActor() const override;
	virtual UActorComponent* GetOwnerComponent() const override;
	virtual FMeshDescription* GetMeshDescription() const override;
	virtual UMaterialInterface* GetMaterial(int32 MaterialIndex) const override;
	virtual FTransform GetWorldTransform() const override;
	virtual bool HitTest(const FRay& WorldRay, FHitResult& OutHit) const override;

	virtual bool IsReadOnly() const override;

	virtual void SetOwnerVisibility(bool bVisible) const override;
	virtual void CommitInPlaceModification(const TFunction<void(FMeshDescription*)>& ModifyFunction) override;
};

