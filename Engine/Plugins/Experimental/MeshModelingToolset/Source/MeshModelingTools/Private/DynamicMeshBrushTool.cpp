// Copyright Epic Games, Inc. All Rights Reserved.

#include "DynamicMeshBrushTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

#include "TargetInterfaces/MaterialProvider.h"
#include "TargetInterfaces/MeshDescriptionProvider.h"
#include "TargetInterfaces/PrimitiveComponentBackedTarget.h"

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;

// localization namespace
#define LOCTEXT_NAMESPACE "UDynamicMeshBrushTool"

/*
 * Tool
 */

UDynamicMeshBrushTool::UDynamicMeshBrushTool()
{
}


void UDynamicMeshBrushTool::Setup()
{
	IPrimitiveComponentBackedTarget* TargetComponent = Cast<IPrimitiveComponentBackedTarget>(Target);
	PreviewMesh = NewObject<UPreviewMesh>(this);
	PreviewMesh->bBuildSpatialDataStructure = true;
	PreviewMesh->CreateInWorld(TargetComponent->GetOwnerActor()->GetWorld(), FTransform::Identity);
	PreviewMesh->SetTransform(TargetComponent->GetWorldTransform());

	FComponentMaterialSet MaterialSet;
	Cast<IMaterialProvider>(Target)->GetMaterialSet(MaterialSet);
	PreviewMesh->SetMaterials(MaterialSet.Materials);

	// initialize from LOD-0 MeshDescription
	PreviewMesh->InitializeMesh(Cast<IMeshDescriptionProvider>(Target)->GetMeshDescription());
	OnBaseMeshComponentChangedHandle = PreviewMesh->GetOnMeshChanged().Add(
		FSimpleMulticastDelegate::FDelegate::CreateUObject(this, &UDynamicMeshBrushTool::OnBaseMeshComponentChanged));

	// call this here so that base tool can estimate target dimension
	InputMeshBoundsLocal = PreviewMesh->GetPreviewDynamicMesh()->GetBounds();
	double ScaledDim = TargetComponent->GetWorldTransform().TransformVector(FVector::OneVector).Size();
	this->WorldToLocalScale = FMathd::Sqrt3 / FMathd::Max(FMathf::ZeroTolerance, ScaledDim);
	UBaseBrushTool::Setup();

	// hide input StaticMeshComponent
	TargetComponent->SetOwnerVisibility(false);
}



double UDynamicMeshBrushTool::EstimateMaximumTargetDimension()
{
	return InputMeshBoundsLocal.MaxDim();
}


void UDynamicMeshBrushTool::Shutdown(EToolShutdownType ShutdownType)
{
	UBaseBrushTool::Shutdown(ShutdownType);

	Cast<IPrimitiveComponentBackedTarget>(Target)->SetOwnerVisibility(true);

	if (PreviewMesh != nullptr)
	{
		PreviewMesh->GetOnMeshChanged().Remove(OnBaseMeshComponentChangedHandle);

		OnShutdown(ShutdownType);

		PreviewMesh->SetVisible(false);
		PreviewMesh->Disconnect();
		PreviewMesh = nullptr;
	}

}




bool UDynamicMeshBrushTool::HitTest(const FRay& Ray, FHitResult& OutHit)
{
	return PreviewMesh->FindRayIntersection(FRay3d(Ray), OutHit);
}






#undef LOCTEXT_NAMESPACE
