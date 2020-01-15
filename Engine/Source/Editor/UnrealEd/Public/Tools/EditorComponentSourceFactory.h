// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComponentSourceInterfaces.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"

class UNREALED_API FStaticMeshComponentTargetFactory : public FComponentTargetFactory
{
public:
	bool CanBuild( UActorComponent* Candidate ) override;
	TUniquePtr<FPrimitiveComponentTarget> Build( UPrimitiveComponent* PrimitiveComponent ) override;
};

class UNREALED_API FStaticMeshComponentTarget : public FPrimitiveComponentTarget
{
public:

	FStaticMeshComponentTarget( UPrimitiveComponent* Component )
		: FPrimitiveComponentTarget( Cast<UStaticMeshComponent>(Component) ){}

	virtual void GetMaterialSet(FComponentMaterialSet& MaterialSetOut, bool bAssetMaterials) const override;

	FMeshDescription* GetMesh() override;

	void CommitMesh( const FCommitter& ) override;

	virtual void CommitMaterialSetUpdate(const FComponentMaterialSet& MaterialSet, bool bApplyToAsset) override;

	static const int LODIndex{0};
};
