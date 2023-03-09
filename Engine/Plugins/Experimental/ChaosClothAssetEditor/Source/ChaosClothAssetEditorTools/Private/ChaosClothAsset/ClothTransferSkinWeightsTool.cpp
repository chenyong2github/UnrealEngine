// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/ClothTransferSkinWeightsTool.h"

#include "ChaosClothAsset/ClothComponent.h"
#include "ChaosClothAsset/ClothComponentToolTarget.h"
#include "ChaosClothAsset/ClothPatternToDynamicMesh.h"
#include "ChaosClothAsset/CollectionClothFacade.h"

#include "BoneWeights.h"
#include "SkeletalMeshAttributes.h"

#include "ToolTargetManager.h"
#include "MeshDescriptionToDynamicMesh.h"

#include "Rendering/SkeletalMeshLODImporterData.h"
#include "Rendering/SkeletalMeshModel.h"

#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/DynamicVertexSkinWeightsAttribute.h"
#include "DynamicMesh/DynamicBoneAttribute.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "DynamicMesh/MeshTransforms.h"

#include "Operations/TransferBoneWeights.h"

#include "TransformTypes.h"

#include "InteractiveTool.h"
#include "InteractiveToolManager.h"

#include "Engine/SkeletalMesh.h"
#include "PreviewMesh.h"
#include "DynamicMeshEditor.h"
#include "Util/ColorConstants.h"
#include "ToolSetupUtil.h"
#include "ModelingToolTargetUtil.h"
#include "TargetInterfaces/SkeletalMeshBackedTarget.h"
#include "Components/SkeletalMeshComponent.h"

#define LOCTEXT_NAMESPACE "ClothSkinWeightRetargetingTool"


namespace ClothTransferSkinWeightsToolHelpers
{
	void SkeletalMeshToDynamicMesh(USkeletalMesh* FromSkeletalMeshAsset, int32 SourceLODIdx, FDynamicMesh3& ToDynamicMesh)
	{
		FMeshDescription SourceMesh;

		// Check first if we have bulk data available and non-empty.
		if (FromSkeletalMeshAsset->IsLODImportedDataBuildAvailable(SourceLODIdx) && !FromSkeletalMeshAsset->IsLODImportedDataEmpty(SourceLODIdx))
		{
			FSkeletalMeshImportData SkeletalMeshImportData;
			FromSkeletalMeshAsset->LoadLODImportedData(SourceLODIdx, SkeletalMeshImportData);
			SkeletalMeshImportData.GetMeshDescription(SourceMesh);
		}
		else
		{
			// Fall back on the LOD model directly if no bulk data exists. When we commit
			// the mesh description, we override using the bulk data. This can happen for older
			// skeletal meshes, from UE 4.24 and earlier.
			const FSkeletalMeshModel* SkeletalMeshModel = FromSkeletalMeshAsset->GetImportedModel();
			if (SkeletalMeshModel && SkeletalMeshModel->LODModels.IsValidIndex(SourceLODIdx))
			{
				SkeletalMeshModel->LODModels[SourceLODIdx].GetMeshDescription(SourceMesh, FromSkeletalMeshAsset);
			}
		}

		FMeshDescriptionToDynamicMesh Converter;
		Converter.Convert(&SourceMesh, ToDynamicMesh);
	};


	void ClothComponentToDynamicMesh(const UChaosClothComponent* ClothComponent, UE::Geometry::FDynamicMesh3& MeshOut)
	{
		using namespace UE::Chaos::ClothAsset;

		const UChaosClothAsset* ChaosClothAsset = ClothComponent->GetClothAsset();
		if (!ChaosClothAsset)
		{
			return;
		}

		const FCollectionClothConstFacade ClothFacade(ChaosClothAsset->GetClothCollection());
		constexpr int32 LodIndex = 0;
		const FCollectionClothLodConstFacade ClothLodFacade = ClothFacade.GetLod(LodIndex);
		constexpr bool bGet2DPattern = false;

		UE::Geometry::FDynamicMeshEditor MeshEditor(&MeshOut);
		FClothPatternToDynamicMesh Converter;

		for (int32 PatternIndex = 0; PatternIndex < ClothLodFacade.GetNumPatterns(); ++PatternIndex)
		{
			UE::Geometry::FDynamicMesh3 PatternMesh;
			Converter.Convert(ChaosClothAsset, LodIndex, PatternIndex, bGet2DPattern, PatternMesh);

			UE::Geometry::FMeshIndexMappings IndexMaps;
			MeshEditor.AppendMesh(&PatternMesh, IndexMaps);
		}
	};
}


// ------------------- Properties -------------------

void UClothTransferSkinWeightsToolActionProperties::PostAction(EClothTransferSkinWeightsToolActions Action)
{
	if (ParentTool.IsValid())
	{
		ParentTool->RequestAction(Action);
	}
}


// ------------------- Builder -------------------

const FToolTargetTypeRequirements& UClothTransferSkinWeightsToolBuilder::GetTargetRequirements() const
{
	static FToolTargetTypeRequirements TypeRequirements({ 
		UPrimitiveComponentBackedTarget::StaticClass(),
		UClothAssetBackedTarget::StaticClass()
		});
	return TypeRequirements;
}

bool UClothTransferSkinWeightsToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	const bool ClothComponentSelected = (SceneState.TargetManager->CountSelectedAndTargetable(SceneState, GetTargetRequirements()) == 1);
	
	const static FToolTargetTypeRequirements SourceMeshRequirements(USkeletalMeshBackedTarget::StaticClass());
	const bool SkeletalMeshComponentSelected = (SceneState.TargetManager->CountSelectedAndTargetable(SceneState, SourceMeshRequirements) == 1);

	return ClothComponentSelected && SkeletalMeshComponentSelected;
}

USingleSelectionMeshEditingTool* UClothTransferSkinWeightsToolBuilder::CreateNewTool(const FToolBuilderState& SceneState) const
{
	UClothTransferSkinWeightsTool* NewTool = NewObject<UClothTransferSkinWeightsTool>(SceneState.ToolManager);

	// Setting Target and World on the new tool is handled in USingleSelectionMeshEditingToolBuilder::InitializeNewTool

	return NewTool;
}

void UClothTransferSkinWeightsToolBuilder::PostSetupTool(UInteractiveTool* Tool, const FToolBuilderState& SceneState) const
{
	if (UClothTransferSkinWeightsTool* NewTool = Cast<UClothTransferSkinWeightsTool>(Tool))
	{
		for (UActorComponent* SelectedComponent : SceneState.SelectedComponents)
		{
			if (USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(SelectedComponent))
			{
				NewTool->ToolProperties->SourceMesh = SkeletalMeshComponent->GetSkeletalMeshAsset();
				NewTool->ToolProperties->SourceMeshTransform = SkeletalMeshComponent->GetComponentTransform();
				NewTool->SourceComponent = SkeletalMeshComponent;
				break;
			}
		}
	}
}

// ------------------- Tool -------------------

void UClothTransferSkinWeightsTool::Setup()
{
	USingleSelectionMeshEditingTool::Setup();

	UClothComponentToolTarget* ClothComponentToolTarget = Cast<UClothComponentToolTarget>(Target);
	ClothComponent = ClothComponentToolTarget->GetClothComponent();
	
	ToolProperties = NewObject<UClothTransferSkinWeightsToolProperties>(this);
	AddToolPropertySource(ToolProperties);

	ActionProperties = NewObject<UClothTransferSkinWeightsToolActionProperties>(this);
	ActionProperties->ParentTool = this;
	AddToolPropertySource(ActionProperties);


	PreviewMesh = NewObject<UPreviewMesh>(this);
	if (PreviewMesh == nullptr)
	{
		return;
	}
	PreviewMesh->CreateInWorld(GetTargetWorld(), FTransform::Identity);
	ToolSetupUtil::ApplyRenderingConfigurationToPreview(PreviewMesh, nullptr);


	PreviewMesh->SetTransform(ClothComponentToolTarget->GetWorldTransform());

	ToolProperties->WatchProperty(ToolProperties->SourceMesh, [this](TObjectPtr<USkeletalMesh>) { UpdatePreviewMesh(); });
	//ToolProperties->WatchProperty(ToolProperties->SourceMeshTransform, [this](const FTransform&) { UpdatePreviewMesh(); });
	ToolProperties->WatchProperty(ToolProperties->BoneName, [this](const FName&) {  UpdatePreviewMeshColor(); });
	ToolProperties->WatchProperty(ToolProperties->bHideSourceMesh, [this](bool) { UpdateSourceMeshRender(); });
}

void UClothTransferSkinWeightsTool::Shutdown(EToolShutdownType ShutdownType)
{
	USingleSelectionMeshEditingTool::Shutdown(ShutdownType);

	if (PreviewMesh)
	{
		PreviewMesh->Disconnect();
	}

	UE::ToolTarget::ShowSourceObject(Target);
	SourceComponent->SetVisibility(true);
}

void UClothTransferSkinWeightsTool::UpdatePreviewMeshColor()
{
	using namespace UE::AnimationCore;
	using namespace UE::Geometry;

	PreviewMesh->SetTriangleColorFunction([this](const FDynamicMesh3* Mesh, int TriangleID) -> FColor
	{
		const FName& CurrentBoneName = ToolProperties->BoneName;
		if (!TargetMeshBoneNameToIndex.Contains(CurrentBoneName))
		{
			return FColor::Black;
		}

		const FBoneIndexType CurrentBoneIndex = TargetMeshBoneNameToIndex[CurrentBoneName];
		const FIndex3i Tri = Mesh->GetTriangle(TriangleID);

		const FName ProfileName = FSkeletalMeshAttributes::DefaultSkinWeightProfileName; // always use default profile for now, later this will be set by the user
		FDynamicMeshVertexSkinWeightsAttribute* Attribute = PreviewMesh->GetPreviewDynamicMesh()->Attributes()->GetSkinWeightsAttribute(ProfileName);
		if (Attribute == nullptr)
		{
			FLinearColor Lin(1.0f, 0.3f, 0.3f, 1.0f);
			return Lin.ToFColor(/*bSRGB = */ true);
		}

		float AvgWeight = 0.0f;
		for (int32 VID = 0; VID < 3; ++VID)
		{
			int32 VertexID = Tri[VID];
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
		FLinearColor Lin(AvgWeight, AvgWeight, AvgWeight, 1.0f);
		return Lin.ToFColor(/*bSRGB = */ true);
	},
	UPreviewMesh::ERenderUpdateMode::FullUpdate);
}

void UClothTransferSkinWeightsTool::UpdatePreviewMesh()
{
	using namespace UE::AnimationCore;
	using namespace UE::Geometry;
	
	//TODO: for now, assume we are always transfering from LOD 0, but make this a parameter in the future...
	constexpr int32 SourceLODIdx = 0; 

	// User hasn't specified the source mesh in the UI
	if (ToolProperties->SourceMesh == nullptr)
	{
		//TODO: Display error message
		return;
	}

	// Convert source Skeletal Mesh to Dynamic Mesh
	FDynamicMesh3 SourceDynamicMesh;
	ClothTransferSkinWeightsToolHelpers::SkeletalMeshToDynamicMesh(ToolProperties->SourceMesh, SourceLODIdx, SourceDynamicMesh);
	MeshTransforms::ApplyTransform(SourceDynamicMesh, ToolProperties->SourceMeshTransform, true);

	// Convert target ClothComponent to Dynamic Mesh
	UE::Geometry::FDynamicMesh3 TargetDynamicMesh;
	TargetDynamicMesh.EnableAttributes();
	TargetDynamicMesh.Attributes()->AttachSkinWeightsAttribute(FSkeletalMeshAttributes::DefaultSkinWeightProfileName, new FDynamicMeshVertexSkinWeightsAttribute(&TargetDynamicMesh));
	ClothTransferSkinWeightsToolHelpers::ClothComponentToDynamicMesh(ClothComponent, TargetDynamicMesh);

	// Enable bone attribute for the target mesh and copy over the bone data from the cloth asset
	// TODO: Once we support skin weights and bones in ToDynamicMesh converter we can remove this logic
	const UChaosClothAsset* TargetClothAsset = ClothComponent->GetClothAsset();
	const FReferenceSkeleton& TargetRefSkeleton = TargetClothAsset->GetRefSkeleton();
	TargetDynamicMesh.Attributes()->EnableBones(TargetRefSkeleton.GetRawBoneNum());
	UE::Geometry::FDynamicMeshBoneNameAttribute* BoneNameAttrib = TargetDynamicMesh.Attributes()->GetBoneNames();
	this->TargetMeshBoneNameToIndex.Reset();
	for (int BoneID = 0; BoneID < TargetRefSkeleton.GetRawBoneNum(); ++BoneID)
	{	
		const FName& BoneName = TargetRefSkeleton.GetRawRefBoneInfo()[BoneID].Name;
		BoneNameAttrib->SetValue(BoneID, BoneName);
		this->TargetMeshBoneNameToIndex.Add(BoneName, BoneID);
	}

	// Do the transfer
	FTransferBoneWeights TransferBoneWeights(&SourceDynamicMesh, FSkeletalMeshAttributes::DefaultSkinWeightProfileName);
	const FTransformSRT3d TargetToWorld = ClothComponent->GetComponentTransform();
	if (TransferBoneWeights.Validate() == EOperationValidationResult::Ok)
	{
		TransferBoneWeights.Compute(TargetDynamicMesh, TargetToWorld, FSkeletalMeshAttributes::DefaultSkinWeightProfileName);
	}

	// Get set of bone indices used in the target mesh
	TMap<FName, FBoneIndexType> UsedBoneNames;

	const TMap<FName, TUniquePtr<FDynamicMeshVertexSkinWeightsAttribute>>& WeightLayers = TargetDynamicMesh.Attributes()->GetSkinWeightsAttributes();
	for (const TPair<FName, TUniquePtr<FDynamicMeshVertexSkinWeightsAttribute>>& LayerPair : WeightLayers)
	{
		const FDynamicMeshVertexSkinWeightsAttribute* Layer = LayerPair.Value.Get();

		for (int VertexID = 0; VertexID < TargetDynamicMesh.MaxVertexID(); ++VertexID)
		{
			if (TargetDynamicMesh.IsVertex(VertexID))
			{
				FBoneWeights Data;
				Layer->GetValue(VertexID, Data);
				for (FBoneWeight Wt : Data)
				{
					const FBoneIndexType BoneIndex = Wt.GetBoneIndex();
					const FName& BoneName = TargetRefSkeleton.GetRawRefBoneInfo()[BoneIndex].Name;
					UsedBoneNames.Add(BoneName, BoneIndex);
				}
			}
		}
	}

	// Update list of bone names in the Properties panel
	UsedBoneNames.ValueSort([](int16 A, int16 B) { return A < B; });
	UsedBoneNames.GetKeys(ToolProperties->BoneNameList);

	// Update the preview mesh
	PreviewMesh->UpdatePreview(&TargetDynamicMesh);
	PreviewMesh->SetMaterial(ToolSetupUtil::GetDefaultSculptMaterial(GetToolManager()));
	PreviewMesh->SetOverrideRenderMaterial(ToolSetupUtil::GetSelectionMaterial(GetToolManager()));

	UpdatePreviewMeshColor();

	PreviewMesh->SetTransform(TargetToWorld);
	PreviewMesh->SetVisible(true);

	UE::ToolTarget::HideSourceObject(Target);
}


void UClothTransferSkinWeightsTool::UpdateSourceMeshRender()
{
	if (ToolProperties && SourceComponent)
	{
		SourceComponent->SetVisibility(!ToolProperties->bHideSourceMesh);
	}
}

void UClothTransferSkinWeightsTool::TransferWeights()
{
	using namespace UE::AnimationCore;
	using namespace UE::Geometry;
	using namespace UE::Chaos::ClothAsset;
	
	//TODO: for now, assume we are always transfering from LOD 0, but make this a parameter in the future...
	constexpr int32 SourceLODIdx = 0; 
	
	// User hasn't specified the source mesh in the UI
	if (ToolProperties->SourceMesh == nullptr) 
	{
		//TODO: Display error message
		return;
	}

	// Convert source Skeletal Mesh to Dynamic Mesh
	FDynamicMesh3 SourceDynamicMesh;
	ClothTransferSkinWeightsToolHelpers::SkeletalMeshToDynamicMesh(ToolProperties->SourceMesh, SourceLODIdx, SourceDynamicMesh);
	MeshTransforms::ApplyTransform(SourceDynamicMesh, ToolProperties->SourceMeshTransform, true);

	UChaosClothAsset* TargetClothAsset = ClothComponent->GetClothAsset(); 

	// Compute bone index mappings
	TMap<FName, FBoneIndexType> TargetBoneToIndex;
	const FReferenceSkeleton& TargetRefSkeleton = TargetClothAsset->GetRefSkeleton();
	for (int32 Index = 0; Index < TargetRefSkeleton.GetRawBoneNum(); ++Index)
	{
		TargetBoneToIndex.Add(TargetRefSkeleton.GetRawRefBoneInfo()[Index].Name, Index);
	}

	// Setup bone weight transfer operator
	FTransferBoneWeights TransferBoneWeights(&SourceDynamicMesh, FSkeletalMeshAttributes::DefaultSkinWeightProfileName); 
	if (TransferBoneWeights.Validate() != EOperationValidationResult::Ok)
	{
		//TODO: Display error message
		return;
	}

	FCollectionClothFacade ClothFacade(TargetClothAsset->GetClothCollection());
    FTransformSRT3d TargetToWorld = ClothComponent->GetComponentTransform();

	// Iterate over the LODs and transfer the bone weights from the source Skeletal mesh to the Cloth asset
	for (int TargetLODIdx = 0; TargetLODIdx < ClothFacade.GetNumLods(); ++TargetLODIdx) 
	{
		FCollectionClothLodFacade ClothLodFacade = ClothFacade.GetLod(TargetLODIdx);

		// Cloth collection data arrays we are writing to
		TArrayView<int32> SimNumBoneInfluences = ClothLodFacade.GetSimNumBoneInfluences();
		TArrayView<TArray<int32>> SimBoneIndices = ClothLodFacade.GetSimBoneIndices();
		TArrayView<TArray<float>> SimBoneWeights = ClothLodFacade.GetSimBoneWeights();

		TArrayView<int32> RenderNumBoneInfluences = ClothLodFacade.GetRenderNumBoneInfluences();
		TArrayView<TArray<int32>> RenderBoneIndices = ClothLodFacade.GetRenderBoneIndices();
		TArrayView<TArray<float>> RenderBoneWeights = ClothLodFacade.GetRenderBoneWeights();

		const TArrayView<FVector3f> SimPositions =  ClothLodFacade.GetSimRestPosition();
		
		checkSlow(SimPositions.Num() == SimBoneIndices.Num());
		
		const int32 NumVert = ClothLodFacade.GetNumSimVertices();
		constexpr bool bUseParallel = true; 

		// Iterate over each vertex and write the data from FBoneWeights into cloth collection managed arrays
		ParallelFor(NumVert, [&](int32 VertexID)
		{
			const FVector3f Pos = SimPositions[VertexID];
			const FVector3d PosD = FVector3d((double)Pos[0], (double)Pos[1], (double)Pos[2]);
			
			UE::AnimationCore::FBoneWeights BoneWeights;
			TransferBoneWeights.Compute(PosD, TargetToWorld, BoneWeights, &TargetBoneToIndex);
			
			const int32 NumBones = BoneWeights.Num();
			
			SimNumBoneInfluences[VertexID] = NumBones;
			SimBoneIndices[VertexID].SetNum(NumBones);
			SimBoneWeights[VertexID].SetNum(NumBones);

			RenderNumBoneInfluences[VertexID] = NumBones;
			RenderBoneIndices[VertexID].SetNum(NumBones);
			RenderBoneWeights[VertexID].SetNum(NumBones);

			for (int BoneIdx = 0; BoneIdx < NumBones; ++BoneIdx) 
			{
				SimBoneIndices[VertexID][BoneIdx] = BoneWeights[BoneIdx].GetBoneIndex();
				SimBoneWeights[VertexID][BoneIdx] = BoneWeights[BoneIdx].GetWeight();

				RenderBoneIndices[VertexID][BoneIdx] = BoneWeights[BoneIdx].GetBoneIndex();
				RenderBoneWeights[VertexID][BoneIdx] = BoneWeights[BoneIdx].GetWeight();
			}

		}, bUseParallel ? EParallelForFlags::None : EParallelForFlags::ForceSingleThread);
	}
}


void UClothTransferSkinWeightsTool::OnTick(float DeltaTime)
{
	if (PendingAction != EClothTransferSkinWeightsToolActions::NoAction)
	{
		if (PendingAction == EClothTransferSkinWeightsToolActions::Transfer)
		{
			TransferWeights();
		}
		PendingAction = EClothTransferSkinWeightsToolActions::NoAction;
	}
}


void UClothTransferSkinWeightsTool::RequestAction(EClothTransferSkinWeightsToolActions ActionType)
{
	if (PendingAction != EClothTransferSkinWeightsToolActions::NoAction)
	{
		return;
	}
	PendingAction = ActionType;
}

#undef LOCTEXT_NAMESPACE
