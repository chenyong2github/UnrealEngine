// Copyright Epic Games, Inc. All Rights Reserved.

#include "ToolTargets/StaticMeshUVMeshToolTarget.h"

#include "DynamicMesh3.h"
#include "DynamicMeshAttributeSet.h"
#include "Engine/StaticMesh.h"
#include "MeshDescriptionUVsToDynamicMesh.h"

using namespace UE::Geometry;

namespace StaticMeshUVMeshToolTargetLocals
{
	// TODO: We'd like to make this configurable somehow.
	const int32 LODIndex = 0;

	// This probably should be a visible static const. It represents the
	// translation from UV coordinates to UV mesh vertex coordinates (i.e.,
	// they will range [0,UVScalingFactor] instead of [0,1]).
	const double UVScalingFactor = 1000;
}

int32 UStaticMeshUVMeshToolTarget::GetNumUVLayers()
{
	using namespace StaticMeshUVMeshToolTargetLocals;
	return OriginalAsset->GetNumUVChannels(LODIndex);
}

void UStaticMeshUVMeshToolTarget::SaveBackToUVs(const FDynamicMesh3* MeshToSave, int32 LayerIndex)
{
	using namespace StaticMeshUVMeshToolTargetLocals;

	FMeshDescriptionUVsToDynamicMesh Converter;
	Converter.UVLayerIndex = LODIndex;
	Converter.ScaleFactor = UVScalingFactor;

	FMeshDescription* MeshDescription = OriginalAsset->GetMeshDescription(LODIndex);
	Converter.BakeBackUVsFromUVMesh(MeshToSave, MeshDescription);

	OriginalAsset->CommitMeshDescription(LODIndex);
	OriginalAsset->PostEditChange();
}

TSharedPtr<FDynamicMesh3> UStaticMeshUVMeshToolTarget::GetMesh(int32 LayerIndex)
{
	using namespace StaticMeshUVMeshToolTargetLocals;

	FMeshDescriptionUVsToDynamicMesh Converter;
	Converter.UVLayerIndex = LODIndex;
	Converter.ScaleFactor = UVScalingFactor;

	FMeshDescription* MeshDescription = OriginalAsset->GetMeshDescription(LODIndex);
	return Converter.GetUVMesh(MeshDescription);
}


// Factory

bool UStaticMeshUVMeshToolTargetFactory::CanBuildTarget(UObject* SourceObject, const FToolTargetTypeRequirements& Requirements) const
{
	return Cast<UStaticMesh>(SourceObject) && Requirements.AreSatisfiedBy(UStaticMeshUVMeshToolTarget::StaticClass());
}

UToolTarget* UStaticMeshUVMeshToolTargetFactory::BuildTarget(UObject* SourceObject, const FToolTargetTypeRequirements& Requirements)
{
	UStaticMesh* StaticMesh = Cast<UStaticMesh>(SourceObject);
	check(StaticMesh);
	FMeshDescription* MeshDescription = StaticMesh->GetMeshDescription(0);
	
	// We need to set the outer here for the UV editor initialization to work properly, because GetPath uses it.
	UStaticMeshUVMeshToolTarget* Target = NewObject<UStaticMeshUVMeshToolTarget>(StaticMesh);
	Target->OriginalAsset = StaticMesh;

	check(Requirements.AreSatisfiedBy(Target));
	return Target;
}