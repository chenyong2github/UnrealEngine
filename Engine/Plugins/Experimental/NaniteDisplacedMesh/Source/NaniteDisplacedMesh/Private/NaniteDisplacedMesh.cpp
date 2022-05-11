// Copyright Epic Games, Inc. All Rights Reserved.

#include "NaniteDisplacedMesh.h"
#include "Modules/ModuleManager.h"
#include "Engine/StaticMesh.h"

#if WITH_EDITOR
#include "DerivedDataBuildVersion.h"
#endif

UNaniteDisplacedMesh::UNaniteDisplacedMesh(const FObjectInitializer& Init)
: Super(Init)
{
}

#if WITH_EDITOR

FGuid FNaniteDisplacedMeshParams::GetAggregatedId() const
{
	UE::DerivedData::FBuildVersionBuilder IdBuilder;

	IdBuilder << TessellationLevel;

	IdBuilder << Magnitude1;
	IdBuilder << Magnitude2;
	IdBuilder << Magnitude3;
	IdBuilder << Magnitude4;

	IdBuilder << Bias1;
	IdBuilder << Bias2;
	IdBuilder << Bias3;
	IdBuilder << Bias4;

	if (IsValid(BaseMesh))
	{
		IdBuilder << BaseMesh->GetPackage()->GetPersistentGuid();
	}

	if (IsValid(DisplacementMap1))
	{
		IdBuilder << DisplacementMap1->GetPackage()->GetPersistentGuid();
	}

	if (IsValid(DisplacementMap2))
	{
		IdBuilder << DisplacementMap2->GetPackage()->GetPersistentGuid();
	}

	if (IsValid(DisplacementMap3))
	{
		IdBuilder << DisplacementMap3->GetPackage()->GetPersistentGuid();
	}

	if (IsValid(DisplacementMap4))
	{
		IdBuilder << DisplacementMap4->GetPackage()->GetPersistentGuid();
	}

	return IdBuilder.Build();
}

FString FNaniteDisplacedMeshParams::GetAggregatedIdString() const
{
	return GetAggregatedId().ToString();
}

FGuid UNaniteDisplacedMesh::GetAggregatedId() const
{
	return Parameters.GetAggregatedId();
}

FString UNaniteDisplacedMesh::GetAggregatedIdString() const
{
	return Parameters.GetAggregatedIdString();
}

#endif
