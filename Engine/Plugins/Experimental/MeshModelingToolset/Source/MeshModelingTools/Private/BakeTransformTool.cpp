// Copyright Epic Games, Inc. All Rights Reserved.

#include "BakeTransformTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

#include "ToolSetupUtil.h"

#include "DynamicMesh3.h"
#include "BaseBehaviors/MultiClickSequenceInputBehavior.h"
#include "Selection/SelectClickedAction.h"

#include "MeshAdapterTransforms.h"
#include "MeshDescriptionAdapter.h"

#include "MeshDescriptionToDynamicMesh.h"
#include "DynamicMeshToMeshDescription.h"

#include "AssetGenerationUtil.h"


#define LOCTEXT_NAMESPACE "UBakeTransformTool"


/*
 * ToolBuilder
 */


bool UBakeTransformToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return ToolBuilderUtil::CountComponents(SceneState, CanMakeComponentTarget) > 0;
}

UInteractiveTool* UBakeTransformToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UBakeTransformTool* NewTool = NewObject<UBakeTransformTool>(SceneState.ToolManager);

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

	NewTool->SetSelection(MoveTemp(ComponentTargets));
	NewTool->SetWorld(SceneState.World);
	NewTool->SetAssetAPI(AssetAPI);

	return NewTool;
}




/*
 * Tool
 */

UBakeTransformToolProperties::UBakeTransformToolProperties()
{
}



UBakeTransformTool::UBakeTransformTool()
{
}

void UBakeTransformTool::SetWorld(UWorld* World)
{
	this->TargetWorld = World;
}

void UBakeTransformTool::Setup()
{
	UInteractiveTool::Setup();

	BasicProperties = NewObject<UBakeTransformToolProperties>(this);
	AddToolPropertySource(BasicProperties);

	FText AllTheWarnings = LOCTEXT("BakeTransformWarning", "WARNING: This Tool will Modify the selected StaticMesh Assets! If you do not wish to modify the original Assets, please make copies in the Content Browser first!");

	// detect and warn about any meshes in selection that correspond to same source data
	bool bSharesSources = GetMapToFirstComponentsSharingSourceData(MapToFirstOccurrences);
	if (bSharesSources)
	{
		AllTheWarnings = FText::Format(FTextFormat::FromString("{0}\n\n{1}"), AllTheWarnings, LOCTEXT("BakeTransformSharedAssetsWarning", "WARNING: Multiple meshes in your selection use the same source asset!  This is not supported -- each asset can only have one baked transform."));
	}

	bool bHasZeroScales = false;
	for (int32 ComponentIdx = 0; ComponentIdx < ComponentTargets.Num(); ComponentIdx++)
	{
		if (ComponentTargets[ComponentIdx]->GetWorldTransform().GetScale3D().GetAbsMin() < KINDA_SMALL_NUMBER)
		{
			bHasZeroScales = true;
		}
	}
	if (bHasZeroScales)
	{
		AllTheWarnings = FText::Format(FTextFormat::FromString("{0}\n\n{1}"), AllTheWarnings, LOCTEXT("BakeTransformWithZeroScale", "WARNING: Baking a zero scale in any dimension will permanently flatten the asset."));
	}
	
	GetToolManager()->DisplayMessage(AllTheWarnings, EToolMessageLevel::UserWarning);

	GetToolManager()->DisplayMessage(
		LOCTEXT("OnStartTool", "This Tool applies the current Rotation and/or Scaling of the object's Transform to the underlying mesh Asset."),
		EToolMessageLevel::UserNotification);
}



void UBakeTransformTool::Shutdown(EToolShutdownType ShutdownType)
{
	if (ShutdownType == EToolShutdownType::Accept)
	{
		UpdateAssets();
	}
}

void UBakeTransformTool::SetAssetAPI(IToolsContextAssetAPI* AssetAPIIn)
{
	this->AssetAPI = AssetAPIIn;
}




void UBakeTransformTool::UpdateAssets()
{
	GetToolManager()->BeginUndoTransaction(LOCTEXT("BakeTransformToolTransactionName", "Bake Transforms"));

	TArray<FTransform3d> BakedTransforms;
	for (int32 ComponentIdx = 0; ComponentIdx < ComponentTargets.Num(); ComponentIdx++)
	{
		
		FTransform3d ComponentToWorld(ComponentTargets[ComponentIdx]->GetWorldTransform());
		FTransform3d ToBakePart = FTransform3d::Identity();
		FTransform3d NewWorldPart = ComponentToWorld;

		if (MapToFirstOccurrences[ComponentIdx] < ComponentIdx)
		{
			ToBakePart = BakedTransforms[MapToFirstOccurrences[ComponentIdx]];
			BakedTransforms.Add(ToBakePart);
			// try to invert baked transform
			NewWorldPart = FTransform3d(
				NewWorldPart.GetRotation() * ToBakePart.GetRotation().Inverse(),
				NewWorldPart.GetTranslation(),
				NewWorldPart.GetScale() * FTransform3d::GetSafeScaleReciprocal(ToBakePart.GetScale())
			);
			NewWorldPart.SetTranslation(NewWorldPart.GetTranslation() - NewWorldPart.TransformVector(ToBakePart.GetTranslation()));
		}
		else
		{
			if (BasicProperties->bBakeRotation)
			{
				ToBakePart.SetRotation(ComponentToWorld.GetRotation());
				NewWorldPart.SetRotation(FQuaterniond::Identity());
			}
			FVector3d ScaleVec = ComponentToWorld.GetScale();

			// weird algo to choose what to keep around as uniform scale in the case where we want to bake out the non-uniform scaling
			FVector3d AbsScales(FMathd::Abs(ScaleVec.X), FMathd::Abs(ScaleVec.Y), FMathd::Abs(ScaleVec.Z));
			double RemainingUniformScale = AbsScales.X;
			{
				FVector3d Dists;
				for (int SubIdx = 0; SubIdx < 3; SubIdx++)
				{
					int OtherA = (SubIdx + 1) % 3;
					int OtherB = (SubIdx + 2) % 3;
					Dists[SubIdx] = FMathd::Abs(AbsScales[SubIdx] - AbsScales[OtherA]) + FMathd::Abs(AbsScales[SubIdx] - AbsScales[OtherB]);
				}
				int BestSubIdx = 0;
				for (int CompareSubIdx = 1; CompareSubIdx < 3; CompareSubIdx++)
				{
					if (Dists[CompareSubIdx] < Dists[BestSubIdx])
					{
						BestSubIdx = CompareSubIdx;
					}
				}
				RemainingUniformScale = AbsScales[BestSubIdx];
				if (RemainingUniformScale <= FLT_MIN)
				{
					RemainingUniformScale = AbsScales.MaxAbsElement();
				}
			}
			switch (BasicProperties->BakeScale)
			{
			case EBakeScaleMethod::BakeFullScale:
				ToBakePart.SetScale(ScaleVec);
				NewWorldPart.SetScale(FVector3d::One());
				break;
			case EBakeScaleMethod::BakeNonuniformScale:
				check(RemainingUniformScale > FLT_MIN); // avoid baking a ~zero scale
				ToBakePart.SetScale(ScaleVec / RemainingUniformScale);
				NewWorldPart.SetScale(FVector3d(RemainingUniformScale, RemainingUniformScale, RemainingUniformScale));
				break;
			case EBakeScaleMethod::DoNotBakeScale:
				break;
			default:
				check(false); // must explicitly handle all cases
			}

			ComponentTargets[ComponentIdx]->CommitMesh([this, &ToBakePart, &NewWorldPart](const FPrimitiveComponentTarget::FCommitParams& CommitParams)
			{
				FMeshDescriptionEditableTriangleMeshAdapter EditableMeshDescAdapter(CommitParams.MeshDescription);

				// do this part within the commit because we have the MeshDescription already computed
				if (BasicProperties->bRecenterPivot)
				{
					FBox BBox = CommitParams.MeshDescription->ComputeBoundingBox();
					FVector3d Center(BBox.GetCenter());
					FFrame3d LocalFrame(Center);
					ToBakePart.SetTranslation(ToBakePart.GetTranslation() - Center);
					NewWorldPart.SetTranslation(NewWorldPart.GetTranslation() + NewWorldPart.TransformVector(Center));
				}

				MeshAdapterTransforms::ApplyTransform(EditableMeshDescAdapter, ToBakePart);

				FVector3d ScaleVec = ToBakePart.GetScale();
				if (ScaleVec.X * ScaleVec.Y * ScaleVec.Z < 0)
				{
					CommitParams.MeshDescription->ReverseAllPolygonFacing();
				}
			});

			BakedTransforms.Add(ToBakePart);
		}

		UPrimitiveComponent* Component = ComponentTargets[ComponentIdx]->GetOwnerComponent();
		Component->Modify();
		Component->SetWorldTransform((FTransform)NewWorldPart);
		ComponentTargets[ComponentIdx]->GetOwnerActor()->MarkComponentsRenderStateDirty();
	}

	GetToolManager()->EndUndoTransaction();
}




#undef LOCTEXT_NAMESPACE
