// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MeshOpPreviewHelpers.h"


void UMeshOpPreviewWithBackgroundCompute::Setup(UWorld* InWorld, IDynamicMeshOperatorFactory* OpGenerator)
{
	PreviewMesh = NewObject<UPreviewMesh>(this, TEXT("PreviewMesh"));
	PreviewMesh->CreateInWorld(InWorld, FTransform::Identity);

	BackgroundCompute = MakeUnique<FBackgroundDynamicMeshComputeSource>(OpGenerator);
	bResultValid = false;
}

FDynamicMeshOpResult UMeshOpPreviewWithBackgroundCompute::Shutdown()
{
	BackgroundCompute->CancelActiveCompute();


	FDynamicMeshOpResult Result{};
	Result.Mesh = PreviewMesh->ExtractPreviewMesh();
	Result.Transform = FTransform3d(PreviewMesh->GetTransform());

	PreviewMesh->SetVisible(false);
	PreviewMesh->Disconnect();
	PreviewMesh = nullptr;

	return Result;
}


void UMeshOpPreviewWithBackgroundCompute::Cancel()
{
	BackgroundCompute->CancelActiveCompute();

	PreviewMesh->SetVisible(false);
	PreviewMesh->Disconnect();
	PreviewMesh = nullptr;
}


void UMeshOpPreviewWithBackgroundCompute::Tick(float DeltaTime)
{
	bool bIsLongDelay = false;
	if (BackgroundCompute)
	{
		BackgroundCompute->Tick(DeltaTime);
		bIsLongDelay = BackgroundCompute->GetElapsedComputeTime() > 2.0;
	}

	UpdateResults();

	if (bResultValid || WorkingMaterial == nullptr || bIsLongDelay == false)
	{
		if (OverrideMaterial != nullptr)
		{
			PreviewMesh->SetOverrideRenderMaterial(OverrideMaterial);
		}
		else
		{
			PreviewMesh->ClearOverrideRenderMaterial();
		}
	}
	else
	{
		PreviewMesh->SetOverrideRenderMaterial(WorkingMaterial);
	}
}


void UMeshOpPreviewWithBackgroundCompute::UpdateResults()
{
	EBackgroundComputeTaskStatus Status = BackgroundCompute->CheckStatus();
	if (Status == EBackgroundComputeTaskStatus::NewResultAvailable)
	{
		TUniquePtr<FDynamicMeshOperator> MeshOp  = BackgroundCompute->ExtractResult();
		TUniquePtr<FDynamicMesh3> ResultMesh = MeshOp->ExtractResult();
		PreviewMesh->SetTransform((FTransform)MeshOp->GetResultTransform());
		PreviewMesh->UpdatePreview(ResultMesh.Get());  // copies the mesh @todo we could just give ownership to the Preview!
		PreviewMesh->SetVisible(bVisible);
		bResultValid = true;

		OnMeshUpdated.Broadcast(this);
	}
}


void UMeshOpPreviewWithBackgroundCompute::InvalidateResult()
{
	BackgroundCompute->NotifyActiveComputeInvalidated();
	bResultValid = false;
}


void UMeshOpPreviewWithBackgroundCompute::ConfigureMaterials(UMaterialInterface* StandardMaterialIn, UMaterialInterface* WorkingMaterialIn)
{
	TArray<UMaterialInterface*> Materials;
	Materials.Add(StandardMaterialIn);
	ConfigureMaterials(Materials, WorkingMaterialIn);
}


void UMeshOpPreviewWithBackgroundCompute::ConfigureMaterials(TArray<UMaterialInterface*> StandardMaterialsIn, UMaterialInterface* WorkingMaterialIn)
{
	this->StandardMaterials = StandardMaterialsIn;
	this->WorkingMaterial = WorkingMaterialIn;

	if (PreviewMesh != nullptr)
	{
		PreviewMesh->SetMaterials(this->StandardMaterials);
	}
}


void UMeshOpPreviewWithBackgroundCompute::SetVisibility(bool bVisibleIn)
{
	bVisible = bVisibleIn;
	PreviewMesh->SetVisible(bVisible);
}
