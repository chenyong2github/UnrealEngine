// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ProceduralMeshBridge.h"

#include "MeshDescription.h"
#include "ProceduralMeshComponent.h"
#include "ProceduralMeshConversion.h"
#include "Components/PrimitiveComponent.h"


bool
FProceduralMeshComponentTargetFactory::CanBuild(UActorComponent* Component)
{
	return !!Cast<UProceduralMeshComponent>(Component);
}

TUniquePtr<FPrimitiveComponentTarget>
FProceduralMeshComponentTargetFactory::Build(UPrimitiveComponent* Component)
{
	UProceduralMeshComponent* ProceduralMeshComponent = Cast<UProceduralMeshComponent>(Component);
	if (ProceduralMeshComponent != nullptr)
	{
		return TUniquePtr< FPrimitiveComponentTarget > { new FProceduralMeshComponentTarget{ Component } };
	}
	return {};
}

FProceduralMeshComponentTarget::FProceduralMeshComponentTarget( UPrimitiveComponent* Component )
	: FPrimitiveComponentTarget{ Cast<UProceduralMeshComponent>(Component) }, MeshDescription{new FMeshDescription}
{
}

FMeshDescription*
FProceduralMeshComponentTarget::GetMesh()
{
	*MeshDescription = BuildMeshDescription( Cast<UProceduralMeshComponent>(Component) );
	return MeshDescription.Get();
}

void
FProceduralMeshComponentTarget::CommitMesh( const FCommitter& ModifyFunc )
{
	ModifyFunc({MeshDescription.Get()});
	MeshDescriptionToProcMesh( *MeshDescription, Cast<UProceduralMeshComponent>(Component) );
}
