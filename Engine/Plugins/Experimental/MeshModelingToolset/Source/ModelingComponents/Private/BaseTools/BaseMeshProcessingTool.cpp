// Copyright Epic Games, Inc. All Rights Reserved.

#include "BaseTools/BaseMeshProcessingTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"
#include "ToolSetupUtil.h"
#include "SimpleDynamicMeshComponent.h"

#include "MeshNormals.h"
#include "MeshTransforms.h"
#include "DynamicMeshToMeshDescription.h"
#include "MeshDescriptionToDynamicMesh.h"


#define LOCTEXT_NAMESPACE "UBaseMeshProcessingTool"


/*
 * ToolBuilder
 */


bool UBaseMeshProcessingToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	if (SupportsMultipleObjects())
	{
		return ToolBuilderUtil::CountComponents(SceneState, CanMakeComponentTarget) > 0;
	}
	else
	{
		return ToolBuilderUtil::CountComponents(SceneState, CanMakeComponentTarget) == 1;
	}
}

UInteractiveTool* UBaseMeshProcessingToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	check(SupportsMultipleObjects() == false); // not supported yet

	UBaseMeshProcessingTool* NewTool = MakeNewToolInstance(SceneState.ToolManager);

	TArray<UActorComponent*> Components = ToolBuilderUtil::FindAllComponents(SceneState, CanMakeComponentTarget);
	check(Components.Num() > 0);

	TArray<TUniquePtr<FPrimitiveComponentTarget>> ComponentTargets;
	for (UActorComponent* ActorComponent : Components)
	{
		auto* MeshComponent = Cast<UPrimitiveComponent>(ActorComponent);
		if (MeshComponent)
		{
			ComponentTargets.Add(MakeComponentTarget(MeshComponent));
		}
	}

	NewTool->SetSelection( MoveTemp(ComponentTargets[0]) );
	NewTool->SetWorld(SceneState.World);

	return NewTool;
}




/*
 * Tool
 */

void UBaseMeshProcessingTool::SetWorld(UWorld* World)
{
	this->TargetWorld = World;
}


void UBaseMeshProcessingTool::Setup()
{
	UInteractiveTool::Setup();

	// hide input StaticMeshComponent
	ComponentTarget->SetOwnerVisibility(false);

	// initialize our properties
	ToolPropertyObjects.Add(this);

	// populate the BaseMesh with a conversion of the input mesh.
	FMeshDescriptionToDynamicMesh Converter;
	Converter.Convert(ComponentTarget->GetMesh(), InitialMesh);

	if (RequiresScaleNormalization())
	{
		// compute area of the input mesh and compute normalization scaling factor
		FVector2d VolArea = TMeshQueries<FDynamicMesh3>::GetVolumeArea(InitialMesh);
		double UnitScalingMeasure = FMathd::Max(0.01, FMathd::Sqrt(VolArea.Y / 6.0));  // 6.0 is a bit arbitrary here...surface area of unit box

		// translate to origin and then apply inverse of scale
		FAxisAlignedBox3d Bounds = InitialMesh.GetCachedBounds();
		SrcTranslate = Bounds.Center();
		MeshTransforms::Translate(InitialMesh, -SrcTranslate);
		SrcScale = UnitScalingMeasure;
		MeshTransforms::Scale(InitialMesh, (1.0 / SrcScale) * FVector3d::One(), FVector3d::Zero());

		// apply that transform to target transform so that visible mesh stays in the same spot
		OverrideTransform = ComponentTarget->GetWorldTransform();
		FVector TranslateDelta = OverrideTransform.TransformVector((FVector)SrcTranslate);
		FVector CurScale = OverrideTransform.GetScale3D();
		OverrideTransform.AddToTranslation(TranslateDelta);
		CurScale.X *= (float)SrcScale;
		CurScale.Y *= (float)SrcScale;
		CurScale.Z *= (float)SrcScale;
		OverrideTransform.SetScale3D(CurScale);

		bIsScaleNormalizationApplied = true;
	}
	else
	{
		SrcTranslate = FVector3d::Zero();
		SrcScale = 1.0;
		OverrideTransform = ComponentTarget->GetWorldTransform();
		bIsScaleNormalizationApplied = false;
	}

	// Construct the preview object and set the material on it.
	Preview = NewObject<UMeshOpPreviewWithBackgroundCompute>(this, "Preview");
	Preview->Setup(this->TargetWorld, this); // Adds the actual functional tool in the Preview object
	Preview->PreviewMesh->SetTangentsMode(EDynamicMeshTangentCalcType::AutoCalculated);

	FComponentMaterialSet MaterialSet;
	ComponentTarget->GetMaterialSet(MaterialSet);
	Preview->ConfigureMaterials(MaterialSet.Materials,
		ToolSetupUtil::GetDefaultWorkingMaterial(GetToolManager())
	);
	Preview->SetWorkingMaterialDelay(0.75);
	Preview->PreviewMesh->SetTransform(OverrideTransform);
	Preview->PreviewMesh->UpdatePreview(&InitialMesh);

	// calculate base mesh vertex normals if necessary normals
	if (RequiresBaseNormals())
	{
		BaseNormals = MakeShared<FMeshNormals>(&InitialMesh);
		BaseNormals->ComputeVertexNormals();
	}

	// show the preview mesh
	Preview->SetVisibility(true);


	InitializeProperties();
	UpdateOptionalPropertyVisibility();

	// start the compute
	InvalidateResult();

	GetToolManager()->DisplayMessage( GetToolMessageString(), EToolMessageLevel::UserNotification);
}



FText UBaseMeshProcessingTool::GetToolMessageString() const
{
	return FText::GetEmpty();
}

FText UBaseMeshProcessingTool::GetAcceptTransactionName() const
{
	return LOCTEXT("BaseMeshProcessingToolTransactionName", "Update Mesh");
}



void UBaseMeshProcessingTool::SavePropertySets()
{
	for (FOptionalPropertySet& PropStruct : OptionalProperties)
	{
		if (PropStruct.PropertySet.IsValid())
		{
			PropStruct.PropertySet->SaveProperties(this);
		}
	}
}



void UBaseMeshProcessingTool::Shutdown(EToolShutdownType ShutdownType)
{
	OnShutdown(ShutdownType);

	SavePropertySets();

	// Restore (unhide) the source meshes
	ComponentTarget->SetOwnerVisibility(true);

	if (Preview != nullptr)
	{
		FDynamicMeshOpResult Result = Preview->Shutdown();
		if (ShutdownType == EToolShutdownType::Accept)
		{
			
			GetToolManager()->BeginUndoTransaction(GetAcceptTransactionName());

			FDynamicMesh3* DynamicMeshResult = Result.Mesh.Get();
			check(DynamicMeshResult != nullptr);

			// un-apply scale normalization if it was applied
			if (bIsScaleNormalizationApplied)
			{
				MeshTransforms::Scale(*DynamicMeshResult, FVector3d(SrcScale, SrcScale, SrcScale), FVector3d::Zero());
				MeshTransforms::Translate(*DynamicMeshResult, SrcTranslate);
			}

			bool bTopologyChanged = HasMeshTopologyChanged();
			ComponentTarget->CommitMesh([DynamicMeshResult, bTopologyChanged](const FPrimitiveComponentTarget::FCommitParams& CommitParams)
			{
				FDynamicMeshToMeshDescription Converter;
				if (bTopologyChanged)
				{
					Converter.Convert(DynamicMeshResult, *CommitParams.MeshDescription);
				}
				else
				{
					Converter.Update(DynamicMeshResult, *CommitParams.MeshDescription);
				}
			});


			GetToolManager()->EndUndoTransaction();
		}
	}
}

void UBaseMeshProcessingTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	UpdateResult();
}

void UBaseMeshProcessingTool::OnTick(float DeltaTime)
{
	Preview->Tick(DeltaTime);
}

void UBaseMeshProcessingTool::InvalidateResult()
{
	Preview->InvalidateResult();
	bResultValid = false;
}

void UBaseMeshProcessingTool::UpdateResult()
{
	if (bResultValid) 
	{
		return;
	}

	bResultValid = Preview->HaveValidResult();
}

bool UBaseMeshProcessingTool::HasAccept() const
{
	return true;
}

bool UBaseMeshProcessingTool::CanAccept() const
{
	return bResultValid;
}




void UBaseMeshProcessingTool::AddOptionalPropertySet(
	UInteractiveToolPropertySet* PropSet, 
	TUniqueFunction<bool()> VisibilityFunc, 
	TUniqueFunction<void()> OnModifiedFunc,
	bool bChangeInvalidatesResult)
{
	AddToolPropertySource(PropSet);
	PropSet->RestoreProperties(this);
	SetToolPropertySourceEnabled(PropSet, false);

	FOptionalPropertySet PropSetStruct;
	PropSetStruct.PropertySet = PropSet;
	PropSetStruct.IsVisible = MoveTemp(VisibilityFunc);
	PropSetStruct.OnModifiedFunc = MoveTemp(OnModifiedFunc);
	PropSetStruct.bInvalidateOnModify = bChangeInvalidatesResult;
	int32 Index = OptionalProperties.Num();
	OptionalProperties.Add(MoveTemp(PropSetStruct));

	PropSet->GetOnModified().AddLambda([Index, this](UObject*, FProperty*) { OnOptionalPropSetModified(Index); } );
}


void UBaseMeshProcessingTool::OnOptionalPropSetModified(int32 Index)
{
	const FOptionalPropertySet& PropSetStruct = OptionalProperties[Index];
	PropSetStruct.OnModifiedFunc();
	if (PropSetStruct.bInvalidateOnModify)
	{
		InvalidateResult();
	}
}


void UBaseMeshProcessingTool::UpdateOptionalPropertyVisibility()
{
	for (FOptionalPropertySet& PropStruct : OptionalProperties)
	{
		if (PropStruct.PropertySet.IsValid())
		{
			bool bVisible = PropStruct.IsVisible();
			SetToolPropertySourceEnabled(PropStruct.PropertySet.Get(), bVisible);
		}
	}
}




TSharedPtr<FMeshNormals>& UBaseMeshProcessingTool::GetBaseNormals()
{
	checkf(BaseNormals.IsValid(), TEXT("Base Normals have not been computed - must return true from RequiresBaseNormals()") );
	return BaseNormals;
}




#undef LOCTEXT_NAMESPACE
