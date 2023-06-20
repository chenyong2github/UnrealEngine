// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/ClothTransferSkinWeightsTool.h"

#include "SkeletalMeshAttributes.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "Rendering/SkeletalMeshLODImporterData.h"
#include "Rendering/SkeletalMeshModel.h"
#include "DynamicMesh/DynamicVertexSkinWeightsAttribute.h"
#include "DynamicMesh/DynamicBoneAttribute.h"
#include "Operations/TransferBoneWeights.h"
#include "ToolSetupUtil.h"
#include "ModelingToolTargetUtil.h"
#include "Engine/SkeletalMesh.h"
#include "ChaosClothAsset/ClothEditorContextObject.h"
#include "ContextObjectStore.h"
#include "Dataflow/DataflowEdNode.h"
#include "ChaosClothAsset/TransferSkinWeightsNode.h"
#include "BaseGizmos/TransformGizmoUtil.h"
#include "InteractiveToolObjects.h"
#include "MeshOpPreviewHelpers.h"

#define LOCTEXT_NAMESPACE "ClothTransferSkinWeightsTool"


namespace UE::Chaos::ClothAsset::Private
{
	void SkeletalMeshToDynamicMesh(USkeletalMesh* FromSkeletalMeshAsset, int32 SourceLODIdx, FDynamicMesh3& ToDynamicMesh)
	{
		FMeshDescription SourceMesh;
		FromSkeletalMeshAsset->GetMeshDescription(SourceLODIdx, SourceMesh);
		FMeshDescriptionToDynamicMesh Converter;
		Converter.Convert(&SourceMesh, ToDynamicMesh);
	};


	class FClothTransferSkinWeightsOp : public UE::Geometry::FDynamicMeshOperator
	{
	public:

		FClothTransferSkinWeightsOp(FDynamicMesh3&& OriginalTargetMesh, const TSharedPtr<const FDynamicMesh3> SourceMesh, const FTransform& SourceMeshTransform) :
			SourceMesh(SourceMesh),
			SourceMeshTransform(SourceMeshTransform)
		{
			*ResultMesh = MoveTemp(OriginalTargetMesh);
		}

	private:

		// FDynamicMeshOperator interface
		virtual void CalculateResult(FProgressCancel* Progress) override
		{
			using namespace UE::Geometry;
			using namespace UE::AnimationCore;

			// Copy over bone attributes from the source mesh to the target/preview
			ResultMesh->Attributes()->CopyBoneAttributes(*SourceMesh->Attributes());

			// Do the transfer
			FTransferBoneWeights TransferBoneWeights(SourceMesh.Get(), FSkeletalMeshAttributes::DefaultSkinWeightProfileName);
			TransferBoneWeights.TargetToWorld = FTransformSRT3d(SourceMeshTransform.Inverse());
			bool bComputeOK = false;
			if (TransferBoneWeights.Validate() == EOperationValidationResult::Ok)
			{
				bComputeOK = TransferBoneWeights.TransferWeightsToMesh(*ResultMesh, FSkeletalMeshAttributes::DefaultSkinWeightProfileName);
			}

			FGeometryResult OpResult;
			if (!bComputeOK)
			{
				OpResult.Result = EGeometryResultType::Failure;
			}
			else
			{
				OpResult.Result = EGeometryResultType::Success;
			}
			SetResultInfo(OpResult);
		}

		// Inputs
		const TSharedPtr<const FDynamicMesh3> SourceMesh;
		const FTransform SourceMeshTransform;		
	};


}


// ------------------- Builder -------------------

USingleSelectionMeshEditingTool* UClothTransferSkinWeightsToolBuilder::CreateNewTool(const FToolBuilderState& SceneState) const
{
	UClothTransferSkinWeightsTool* NewTool = NewObject<UClothTransferSkinWeightsTool>(SceneState.ToolManager);

	if (UClothEditorContextObject* ContextObject = SceneState.ToolManager->GetContextObjectStore()->FindContext<UClothEditorContextObject>())
	{
		NewTool->SetClothEditorContextObject(ContextObject);
	}

	return NewTool;
}


// ------------------- Tool -------------------


void UClothTransferSkinWeightsTool::Setup()
{
	USingleSelectionMeshEditingTool::Setup();

	TransferSkinWeightsNode = ClothEditorContextObject->GetSingleSelectedNodeOfType<FChaosClothAssetTransferSkinWeightsNode>();
	checkf(TransferSkinWeightsNode, TEXT("No Transfer Skin Weights Node is currently selected, or more than one node is selected"));

	ToolProperties = NewObject<UClothTransferSkinWeightsToolProperties>(this);

	SetSRTPropertiesFromTransform(TransferSkinWeightsNode->Transform);
	ToolProperties->SourceMesh = TransferSkinWeightsNode->SkeletalMesh;
	ToolProperties->SourceMeshLOD = TransferSkinWeightsNode->LodIndex;
	

	AddToolPropertySource(ToolProperties);


	//
	// Set up Preview mesh that will show the results of the computation
	//

	TargetClothPreview = NewObject<UMeshOpPreviewWithBackgroundCompute>(this);
	TargetClothPreview->Setup(GetTargetWorld(), this);
	ToolSetupUtil::ApplyRenderingConfigurationToPreview(TargetClothPreview->PreviewMesh, Target);
	TargetClothPreview->ConfigureMaterials(ToolSetupUtil::GetVertexColorMaterial(GetToolManager()), ToolSetupUtil::GetDefaultWorkingMaterial(GetToolManager()));

	// Mesh topology is not being changed 
	TargetClothPreview->SetIsMeshTopologyConstant(true, EMeshRenderAttributeFlags::VertexColors);

	TargetClothPreview->OnOpCompleted.AddUObject(this, &UClothTransferSkinWeightsTool::OpFinishedCallback);
	TargetClothPreview->OnMeshUpdated.AddUObject(this, &UClothTransferSkinWeightsTool::PreviewMeshUpdatedCallback);

	// Set the initial preview mesh before any computation runs
	UE::Geometry::FDynamicMesh3 InitialPreviewMesh = UE::ToolTarget::GetDynamicMeshCopy(Target, true);
	TargetClothPreview->PreviewMesh->UpdatePreview(MoveTemp(InitialPreviewMesh));

	TargetClothPreview->SetVisibility(true);

	//
	// Source mesh (populated from the SkeletalMesh tool property)
	//

	SourceMeshParentActor = GetTargetWorld()->SpawnActor<AInternalToolFrameworkActor>();
	SourceMeshComponent = NewObject<UDynamicMeshComponent>(SourceMeshParentActor);
	ToolSetupUtil::ApplyRenderingConfigurationToPreview(SourceMeshComponent, nullptr);
	SourceMeshParentActor->SetRootComponent(SourceMeshComponent);
	SourceMeshComponent->RegisterComponent();

	// Watch for property changes

	ToolProperties->WatchProperty(ToolProperties->SourceMesh, [this](TObjectPtr<USkeletalMesh>) { UpdateSourceMesh(); });

	ToolProperties->WatchProperty(ToolProperties->BoneName, [this](const FName&) 
	{
		check(TargetClothPreview);
		TargetClothPreview->PreviewMesh->NotifyDeferredEditCompleted(UPreviewMesh::ERenderUpdateMode::FastUpdate, EMeshRenderAttributeFlags::VertexColors, false);
	});
	
	ToolProperties->WatchProperty(ToolProperties->bHideSourceMesh, [this](bool bNewHideSourceMesh) 
	{ 
		check(SourceMeshComponent);
		SourceMeshComponent->SetVisibility(!bNewHideSourceMesh);
	});

	ToolProperties->WatchProperty(ToolProperties->SourceMeshLOD, [this](int32 NewLOD)
	{
		const bool bLODIsValid = !ToolProperties->SourceMesh || ToolProperties->SourceMesh->IsValidLODIndex(NewLOD);

		if (bLODIsValid)
		{
			if (bHasInvalidLODWarning)
			{
				GetToolManager()->DisplayMessage(FText(), EToolMessageLevel::UserWarning);    // clear old warning
				bHasInvalidLODWarning = false;
			}
		}
		else
		{
			GetToolManager()->DisplayMessage(LOCTEXT("InvalidLODIndex", "Specified source mesh LOD is not valid"), EToolMessageLevel::UserWarning);
			bHasInvalidLODWarning = true;
		}

		UpdateSourceMesh();
	});


	//
	// Transform/Gizmo/Proxy stuff
	//

	ToolProperties->WatchProperty(ToolProperties->SourceMeshTranslation, [this](const FVector3d& NewTranslation)
	{
		if (DataBinder)
		{
			DataBinder->UpdateAfterDataEdit();
		}
	});

	ToolProperties->WatchProperty(ToolProperties->SourceMeshRotation, [this](const FVector3d& NewTranslation)
	{
		if (DataBinder)
		{
			DataBinder->UpdateAfterDataEdit();
		}
	});

	ToolProperties->WatchProperty(ToolProperties->SourceMeshScale, [this](const FVector3d& NewTranslation)
	{
		if (DataBinder)
		{
			DataBinder->UpdateAfterDataEdit();
		}
	});


	UInteractiveGizmoManager* const GizmoManager = GetToolManager()->GetPairedGizmoManager();
	ensure(GizmoManager);
	SourceMeshTransformProxy = NewObject<UTransformProxy>(this);
	ensure(SourceMeshTransformProxy);
	SourceMeshTransformProxy->SetTransform(TransferSkinWeightsNode->Transform);

	SourceMeshTransformProxy->OnTransformChanged.AddWeakLambda(this, [this](UTransformProxy*, FTransform NewTransform)
	{
		if (SourceMeshParentActor)
		{
			SourceMeshParentActor->SetActorTransform(NewTransform);
		}

		// Recompute result after moving the transform gizmo
		if (ToolProperties->SourceMesh && ToolProperties->SourceMesh->IsValidLODIndex(ToolProperties->SourceMeshLOD))
		{
			TargetClothPreview->InvalidateResult();
		}
	});

	SourceMeshTransformGizmo = UE::TransformGizmoUtil::CreateCustomTransformGizmo(GizmoManager, ETransformGizmoSubElements::StandardTranslateRotate, this);
	ensure(SourceMeshTransformGizmo);

	SourceMeshTransformGizmo->SetActiveTarget(SourceMeshTransformProxy, GetToolManager());
	SourceMeshTransformGizmo->SetVisibility(ToolProperties->SourceMesh != nullptr);
	SourceMeshTransformGizmo->bUseContextCoordinateSystem = false;
	SourceMeshTransformGizmo->bUseContextGizmoMode = false;
	SourceMeshTransformGizmo->CurrentCoordinateSystem = EToolContextCoordinateSystem::Local;

	DataBinder = MakeShared<FTransformGizmoDataBinder>();
	DataBinder->InitializeBoundVectors(&ToolProperties->SourceMeshTranslation, &ToolProperties->SourceMeshRotation, &ToolProperties->SourceMeshScale);
	DataBinder->BindToInitializedGizmo(SourceMeshTransformGizmo, SourceMeshTransformProxy);


	UpdateSourceMesh();
	SetPreviewMeshColorFunction();

	UE::ToolTarget::HideSourceObject(Target);
}

void UClothTransferSkinWeightsTool::Shutdown(EToolShutdownType ShutdownType)
{
	USingleSelectionMeshEditingTool::Shutdown(ShutdownType);

	if (ShutdownType == EToolShutdownType::Accept)
	{
		TransferSkinWeightsNode->SkeletalMesh = ToolProperties->SourceMesh;
		TransferSkinWeightsNode->LodIndex = ToolProperties->SourceMeshLOD;
		TransferSkinWeightsNode->Transform = TransformFromProperties();
	}

	if (SourceMeshTransformProxy)
	{
		SourceMeshTransformProxy->OnTransformChanged.RemoveAll(this);
		SourceMeshTransformProxy->OnEndTransformEdit.RemoveAll(this);
	}

	if (TargetClothPreview)
	{
		TargetClothPreview->OnMeshUpdated.RemoveAll(this);
		TargetClothPreview->Shutdown();
		TargetClothPreview = nullptr;
	}

	if (SourceMeshComponent)
	{
		SourceMeshComponent->DestroyComponent();
		SourceMeshComponent = nullptr;
	}

	if (SourceMeshParentActor)
	{
		SourceMeshParentActor->Destroy();
		SourceMeshParentActor = nullptr;
	}

	GetToolManager()->GetPairedGizmoManager()->DestroyAllGizmosByOwner(this);
	SourceMeshTransformGizmo = nullptr;

	UE::ToolTarget::ShowSourceObject(Target);
}


bool UClothTransferSkinWeightsTool::CanAccept() const
{
	const FTransform& TransformOnNode = TransferSkinWeightsNode->Transform;;

	return (ToolProperties->SourceMesh != TransferSkinWeightsNode->SkeletalMesh) ||
		(ToolProperties->SourceMeshLOD != TransferSkinWeightsNode->LodIndex) ||
		(ToolProperties->SourceMeshRotation != TransformOnNode.Rotator().Euler()) ||
		(ToolProperties->SourceMeshTranslation != TransformOnNode.GetTranslation()) ||
		(ToolProperties->SourceMeshScale != TransformOnNode.GetScale3D());

}

void UClothTransferSkinWeightsTool::OnTick(float DeltaTime)
{
	if (TargetClothPreview)
	{
		TargetClothPreview->Tick(DeltaTime);
	}
}

TUniquePtr<UE::Geometry::FDynamicMeshOperator> UClothTransferSkinWeightsTool::MakeNewOperator()
{
	checkf(ToolProperties->SourceMesh, TEXT("Expected non-null SourceMesh before starting Operator"));
	checkf(ToolProperties->SourceMesh->IsValidLODIndex(ToolProperties->SourceMeshLOD), TEXT("Source Mesh LOD level should be validated before starting Operator"));

	UE::Geometry::FDynamicMesh3 TargetDynamicMesh = UE::ToolTarget::GetDynamicMeshCopy(Target, true);
	const TSharedPtr<const UE::Geometry::FDynamicMesh3> SourceDynamicMesh = MakeShared<UE::Geometry::FDynamicMesh3>(*SourceMeshComponent->GetMesh());

	const FTransform SourceMeshTransform = SourceMeshTransformGizmo->GetGizmoTransform();
	TUniquePtr<UE::Chaos::ClothAsset::Private::FClothTransferSkinWeightsOp> TransferOp =
		MakeUnique<UE::Chaos::ClothAsset::Private::FClothTransferSkinWeightsOp>(MoveTemp(TargetDynamicMesh), SourceDynamicMesh, SourceMeshTransform);

	return TransferOp;
}

void UClothTransferSkinWeightsTool::SetClothEditorContextObject(TObjectPtr<UClothEditorContextObject> InClothEditorContextObject)
{
	ClothEditorContextObject = InClothEditorContextObject;
}


void UClothTransferSkinWeightsTool::SetPreviewMeshColorFunction()
{
	using namespace UE::AnimationCore;
	using namespace UE::Geometry;

	TargetClothPreview->PreviewMesh->SetTriangleColorFunction([this](const FDynamicMesh3* Mesh, int TriangleID) -> FColor
	{
		const FName& CurrentBoneName = ToolProperties->BoneName;
		if (!TargetMeshBoneNameToIndex.Contains(CurrentBoneName))
		{
			return FColor::Black;
		}

		const FBoneIndexType CurrentBoneIndex = TargetMeshBoneNameToIndex[CurrentBoneName];
		const FIndex3i Tri = Mesh->GetTriangle(TriangleID);

		const FName ProfileName = FSkeletalMeshAttributes::DefaultSkinWeightProfileName; // always use default profile for now, later this will be set by the user
		FDynamicMeshVertexSkinWeightsAttribute* Attribute = Mesh->Attributes()->GetSkinWeightsAttribute(ProfileName);
		if (Attribute == nullptr)
		{
			FLinearColor Lin(1.0f, 0.3f, 0.3f, 1.0f);
			return Lin.ToFColor(/*bSRGB = */ true);
		}

		float AvgWeight = 0.0f;
		for (int32 VID = 0; VID < 3; ++VID)
		{
			const int32 VertexID = Tri[VID];
			FBoneWeights Data;
			Attribute->GetValue(VertexID, Data);
			for (FBoneWeight Wt : Data)
			{
				if (Wt.GetBoneIndex() == CurrentBoneIndex)
				{
					AvgWeight += Wt.GetWeight();
				}
			}
		}

		AvgWeight /= 3.0f;
		const FLinearColor Lin(AvgWeight, AvgWeight, AvgWeight, 1.0f);
		return Lin.ToFColor(/*bSRGB = */ true);
	},
	UPreviewMesh::ERenderUpdateMode::FullUpdate);
}


FTransform UClothTransferSkinWeightsTool::TransformFromProperties() const
{
	const FRotator Rotation = FRotator::MakeFromEuler(ToolProperties->SourceMeshRotation);
	return FTransform(Rotation, ToolProperties->SourceMeshTranslation, ToolProperties->SourceMeshScale);
}

void UClothTransferSkinWeightsTool::SetSRTPropertiesFromTransform(const FTransform& Transform) const
{
	ToolProperties->SourceMeshRotation = Transform.Rotator().Euler();
	ToolProperties->SourceMeshTranslation = Transform.GetTranslation();
	ToolProperties->SourceMeshScale = Transform.GetScale3D();
}


void UClothTransferSkinWeightsTool::UpdateSourceMesh()
{
	checkf(ToolProperties, TEXT("ToolProperties is expected to be non-null. Be sure to run Setup() on this tool when it is created."));

	if (ToolProperties->SourceMesh && ToolProperties->SourceMesh->IsValidLODIndex(ToolProperties->SourceMeshLOD))
	{
		// Set up source mesh (from the SkeletalMesh)
		FDynamicMesh3 SourceDynamicMesh;
		UE::Chaos::ClothAsset::Private::SkeletalMeshToDynamicMesh(ToolProperties->SourceMesh, ToolProperties->SourceMeshLOD, SourceDynamicMesh);
		SourceMeshComponent->SetMesh(MoveTemp(SourceDynamicMesh));

		checkf(SourceMeshComponent, TEXT("Source mesh specified in the Tool Properties, but no SourceMesh exists"));
		
		SourceMeshParentActor->SetActorTransform(TransformFromProperties());
		SourceMeshComponent->SetMaterial(0, ToolSetupUtil::GetTransparentSculptMaterial(GetToolManager(), FLinearColor::Red, 0.4, true));
		SourceMeshComponent->SetVisibility(!ToolProperties->bHideSourceMesh);

		// Use ReinitializeGizmoTransform rather than SetNewGizmoTransform to avoid having this on the undo stack
		SourceMeshTransformGizmo->ReinitializeGizmoTransform(SourceMeshParentActor->GetActorTransform());
		SourceMeshTransformGizmo->SetVisibility(!ToolProperties->bHideSourceMesh);
		SourceMeshTransformGizmo->ActiveGizmoMode = EToolContextTransformGizmoMode::Combined;

		TargetClothPreview->InvalidateResult();
	}
	else
	{
		SourceMeshComponent->SetVisibility(false);
		SourceMeshTransformGizmo->SetVisibility(false);
	}	
}


void UClothTransferSkinWeightsTool::OpFinishedCallback(const UE::Geometry::FDynamicMeshOperator* Op)
{
	if (Op->GetResultInfo().Result == UE::Geometry::EGeometryResultType::Failure)
	{
		GetToolManager()->DisplayMessage(LOCTEXT("TransferOpFailedWarning", "Weight transfer failed"), EToolMessageLevel::UserWarning);
		bHasOpFailedWarning = true;
	}
	else
	{
		if (bHasOpFailedWarning)
		{
			GetToolManager()->DisplayMessage(FText(), EToolMessageLevel::UserWarning);    // clear old warning
			bHasOpFailedWarning = false;
		}
	}
}


void UClothTransferSkinWeightsTool::PreviewMeshUpdatedCallback(UMeshOpPreviewWithBackgroundCompute* Preview)
{
	constexpr bool bProcessOnlyIfValid = true;

	Preview->ProcessCurrentMesh([&](const FDynamicMesh3& ResultMesh)
	{
		using namespace UE::Geometry;
		using namespace UE::AnimationCore;

		ToolProperties->BoneNameList.Reset();
		TargetMeshBoneNameToIndex.Reset();

		// Rebuild the set of selectable bone names, and the name -> index map
		// TODO: Do we maybe want to do this in the background op and then copy the final results back to the member variables?

		if (ResultMesh.Attributes() && ResultMesh.Attributes()->GetBoneNames() && ResultMesh.Attributes()->GetBoneParentIndices())
		{
			// Get set of bone indices used in the target mesh
			const TArray<FName>& TargetBoneNames = ResultMesh.Attributes()->GetBoneNames()->GetAttribValues();
			const TArray<int32>& TargetBoneIndices = ResultMesh.Attributes()->GetBoneParentIndices()->GetAttribValues();

			const TMap<FName, TUniquePtr<FDynamicMeshVertexSkinWeightsAttribute>>& WeightLayers = ResultMesh.Attributes()->GetSkinWeightsAttributes();
			TMap<FName, FBoneIndexType> UsedBoneNames;
			for (const TPair<FName, TUniquePtr<FDynamicMeshVertexSkinWeightsAttribute>>& LayerPair : WeightLayers)
			{
				const FDynamicMeshVertexSkinWeightsAttribute* Layer = LayerPair.Value.Get();

				for (const int VertexID : ResultMesh.VertexIndicesItr())
				{
					FBoneWeights Data;
					Layer->GetValue(VertexID, Data);
					for (FBoneWeight Wt : Data)
					{
						const FBoneIndexType BoneIndex = Wt.GetBoneIndex();
						const FName& BoneName = TargetBoneNames[BoneIndex];
						UsedBoneNames.Add(BoneName, BoneIndex);
					}
				}
			}

			// Build the bone name -> index map
			for (int32 BoneID = 0; BoneID < TargetBoneNames.Num(); ++BoneID)
			{
				const FName& BoneName = TargetBoneNames[BoneID];
				const int32 BoneIndex = TargetBoneIndices[BoneID];
				TargetMeshBoneNameToIndex.Add(BoneName, BoneID);
			}

			// Update list of bone names in the Properties panel
			UsedBoneNames.ValueSort([](int16 A, int16 B) { return A < B; });
			UsedBoneNames.GetKeys(ToolProperties->BoneNameList);
		}
	}, 
	bProcessOnlyIfValid);
}

#undef LOCTEXT_NAMESPACE
