// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComponentSourceInterfaces.h"
#include "MeshDescription.h"
#include "ProceduralMeshComponent.h"

class PROCEDURALMESHCOMPONENT_API  FProceduralMeshComponentTargetFactory : public FComponentTargetFactory
{
public:
	bool CanBuild( UActorComponent* Candidate ) override;
	TUniquePtr<FPrimitiveComponentTarget> Build( UPrimitiveComponent* PrimitiveComponent ) override;
};

class PROCEDURALMESHCOMPONENT_API FProceduralMeshComponentTarget : public FPrimitiveComponentTarget
{
public:
	FProceduralMeshComponentTarget( UPrimitiveComponent* Component );

	FMeshDescription* GetMesh() override;
	void CommitMesh( const FCommitter& ) override;
	virtual bool HasSameSourceData(const FPrimitiveComponentTarget& OtherTarget) const override
	{
		return OtherTarget.Component == Component;
	}
private:
	TUniquePtr<FMeshDescription> MeshDescription;
};
