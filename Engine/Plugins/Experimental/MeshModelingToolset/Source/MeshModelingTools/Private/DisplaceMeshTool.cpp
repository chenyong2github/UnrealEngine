// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DisplaceMeshTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

#include "DynamicMesh3.h"

#include "SimpleDynamicMeshComponent.h"

#include "MeshNormals.h"

#include "SceneManagement.h" // for FPrimitiveDrawInterface
#include "Async/ParallelFor.h"


#include "ProfilingDebugging/ScopedTimers.h"

#define LOCTEXT_NAMESPACE "UDisplaceMeshTool"

/*
 * ToolBuilder
 */
bool UDisplaceMeshToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return ToolBuilderUtil::CountComponents(SceneState, CanMakeComponentTarget) == 1;
}

UInteractiveTool* UDisplaceMeshToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UDisplaceMeshTool* NewTool = NewObject<UDisplaceMeshTool>(SceneState.ToolManager);

	UActorComponent* ActorComponent = ToolBuilderUtil::FindFirstComponent(SceneState, CanMakeComponentTarget);
	auto* MeshComponent = Cast<UPrimitiveComponent>(ActorComponent);
	check(MeshComponent != nullptr);

	NewTool->SetSelection(MakeComponentTarget(MeshComponent));

	return NewTool;
}

/*
 * Tool
 */
UDisplaceMeshTool::UDisplaceMeshTool()
{
	DisplacementType = EDisplaceMeshToolDisplaceType::DisplacementMap;
	DisplaceIntensity = 10.0f;
	RandomSeed = 31337;
	Subdivisions = 4;
}

void UDisplaceMeshTool::Setup()
{
	UInteractiveTool::Setup();

	// create dynamic mesh component to use for live preview
	DynamicMeshComponent = NewObject<USimpleDynamicMeshComponent>(ComponentTarget->GetOwnerActor(), "DynamicMesh");
	DynamicMeshComponent->SetupAttachment(ComponentTarget->GetOwnerActor()->GetRootComponent());
	DynamicMeshComponent->RegisterComponent();
	DynamicMeshComponent->SetWorldTransform(ComponentTarget->GetWorldTransform());

	// copy material if there is one
	auto Material = ComponentTarget->GetMaterial(0);
	if (Material != nullptr)
	{
		DynamicMeshComponent->SetMaterial(0, Material);
	}

	DynamicMeshComponent->InitializeMesh(ComponentTarget->GetMesh());
	OriginalMesh.Copy(*DynamicMeshComponent->GetMesh());

	UpdateMap(true);

	// hide input StaticMeshComponent
	ComponentTarget->SetOwnerVisibility(false);

	// initialize our properties
	ToolPropertyObjects.Add(this);

	bResultValid = false;
}

void UDisplaceMeshTool::Shutdown(EToolShutdownType ShutdownType)
{
	if (DynamicMeshComponent != nullptr)
	{
		//DynamicMeshComponent->OnMeshChanged.Remove(OnDynamicMeshComponentChangedHandle);

		ComponentTarget->SetOwnerVisibility(true);

		if (ShutdownType == EToolShutdownType::Accept)
		{
			// this block bakes the modified DynamicMeshComponent back into the StaticMeshComponent inside an undo transaction
			GetToolManager()->BeginUndoTransaction(LOCTEXT("DisplaceMeshToolTransactionName", "Displace Mesh"));
			ComponentTarget->CommitMesh([=](FMeshDescription* MeshDescription)
			{
				DynamicMeshComponent->Bake(MeshDescription, Subdivisions > 0);
			});
			GetToolManager()->EndUndoTransaction();
		}

		DynamicMeshComponent->UnregisterComponent();
		DynamicMeshComponent->DestroyComponent();
		DynamicMeshComponent = nullptr;
	}
}

void UDisplaceMeshTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	UpdateResult();
}

#if WITH_EDITOR
void UDisplaceMeshTool::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	UProperty* PropertyThatChanged = PropertyChangedEvent.Property;

	if (PropertyThatChanged)
	{
		//if (PropertyThatChanged->GetFName() == GET_MEMBER_NAME_CHECKED(UDisplaceMeshTool, DisplaceIterations))
		//{
		//}
	}

	bResultValid = false;
}
#endif

void UDisplaceMeshTool::UpdateResult()
{
	if (bResultValid) 
	{
		return;
	}

	UpdateSubdividedMesh();

	// compute Displaceed positions in PositionBuffer
	switch (DisplacementType)
	{
		default:
		case EDisplaceMeshToolDisplaceType::Constant:
			ComputeDisplacement_Constant();
			break;

		case EDisplaceMeshToolDisplaceType::RandomNoise:
			ComputeDisplacement_RandomNoise();
			break;

		case EDisplaceMeshToolDisplaceType::DisplacementMap:
			ComputeDisplacement_Map();
			break;
	}

	// update preview vertex positions
	FDynamicMesh3* TargetMesh = DynamicMeshComponent->GetMesh();
	for (int vid : SubdividedMesh.VertexIndicesItr())
	{
		TargetMesh->SetVertex(vid, DisplacedBuffer[vid]);
	}

	// recalculate normals
	if (TargetMesh->HasAttributes())
	{
		FMeshNormals Normals(TargetMesh);
		FDynamicMeshNormalOverlay* NormalOverlay = TargetMesh->Attributes()->PrimaryNormals();
		Normals.RecomputeOverlayNormals(NormalOverlay);
		Normals.CopyToOverlay(NormalOverlay);
	}
	else
	{
		FMeshNormals::QuickComputeVertexNormals(*TargetMesh);
	}

	DynamicMeshComponent->NotifyMeshUpdated();
	//DynamicMeshComponent->FastNotifyPositionsUpdated(true);

	GetToolManager()->PostInvalidation();

	bResultValid = true;
}

void UDisplaceMeshTool::ComputeDisplacement_Constant()
{
	double Intensity = DisplaceIntensity;

	for (int vid : SubdividedMesh.VertexIndicesItr())
	{
		FVector3d Position = PositionBuffer[vid];
		FVector3d Normal = NormalsBuffer[vid];
		FVector3d Displacement = Intensity * Normal;
		DisplacedBuffer[vid] = Position + Displacement;
	}
}

void UDisplaceMeshTool::ComputeDisplacement_RandomNoise()
{
	double Intensity = DisplaceIntensity;

	FMath::SRandInit(RandomSeed);

	for (int vid : SubdividedMesh.VertexIndicesItr())
	{
		FVector3d Position = PositionBuffer[vid];
		FVector3d Normal = NormalsBuffer[vid];

		double RandVal = 2.0 * (FMath::SRand() - 0.5);
		
		FVector3d Displacement = RandVal * Intensity * Normal;

		DisplacedBuffer[vid] = Position + Displacement;
	}
}

void UDisplaceMeshTool::ComputeDisplacement_Map()
{
	UpdateMap();

	const FDynamicMeshUVOverlay* UVOverlay = SubdividedMesh.Attributes()->GetUVLayer(0);

	double Intensity = DisplaceIntensity;
	FMath::SRandInit(RandomSeed);

	for (int tid : SubdividedMesh.TriangleIndicesItr())
	{
		FIndex3i Tri = SubdividedMesh.GetTriangle(tid);
		FIndex3i UVTri = UVOverlay->GetTriangle(tid);

		for (int j = 0; j < 3; ++j)
		{
			int vid = Tri[j];

			FVector3d Position = PositionBuffer[vid];
			FVector3d Normal = NormalsBuffer[vid];

			FVector2f UV = UVOverlay->GetElement(UVTri[j]);
			double Offset = DisplaceField.BilinearSampleClamped(UV);

			FVector3d Displacement = Offset * Intensity * Normal;

			DisplacedBuffer[vid] = Position + Displacement;
		}
	}

}

namespace {
	class FTextureAccess
	{
	public:
		FTextureAccess(UTexture2D* DisplacementMap)
			:DisplacementMap(DisplacementMap)
		{
			check(DisplacementMap);
			OldCompressionSettings = DisplacementMap->CompressionSettings;
			bOldSRGB = DisplacementMap->SRGB; 
#if WITH_EDITOR
			OldMipGenSettings = DisplacementMap->MipGenSettings;
#endif


			DisplacementMap->CompressionSettings = TextureCompressionSettings::TC_VectorDisplacementmap;
			DisplacementMap->SRGB = false;
#if WITH_EDITOR
			DisplacementMap->MipGenSettings = TextureMipGenSettings::TMGS_NoMipmaps;
#endif
			DisplacementMap->UpdateResource();

			FormattedImageData = static_cast<const FColor*>(DisplacementMap->PlatformData->Mips[0].BulkData.LockReadOnly());
		}
		FTextureAccess(const FTextureAccess&) = delete;
		FTextureAccess(FTextureAccess&&) = delete;
		void operator=(const FTextureAccess&) = delete;
		void operator=(FTextureAccess&&) = delete;

		~FTextureAccess()
		{
			DisplacementMap->PlatformData->Mips[0].BulkData.Unlock();

			DisplacementMap->CompressionSettings = OldCompressionSettings;
			DisplacementMap->SRGB = bOldSRGB;
#if WITH_EDITOR
			DisplacementMap->MipGenSettings = OldMipGenSettings;
#endif

			DisplacementMap->UpdateResource();
		}

		bool HasData() const
		{
			return FormattedImageData != nullptr;
		}
		const FColor* GetData() const
		{
			return FormattedImageData;
		}

	private:
		UTexture2D* DisplacementMap{nullptr};
		TextureCompressionSettings OldCompressionSettings{};
		TextureMipGenSettings OldMipGenSettings{};
		bool bOldSRGB{false};
		const FColor* FormattedImageData{nullptr};
	};
}

void UDisplaceMeshTool::UpdateMap(bool bForceUpdate)
{
	if (CachedMapSource == DisplacementMap && bForceUpdate == false)
	{
		return;
	}
	CachedMapSource = DisplacementMap;

	if (DisplacementMap == nullptr || DisplacementMap->PlatformData == nullptr || DisplacementMap->PlatformData->Mips.Num() < 1)
	{
		DisplaceField = FSampledScalarField2f();
		return;
	}

	FTextureAccess TextureAccess( DisplacementMap );
	if (!TextureAccess.HasData())
	{
		DisplaceField = FSampledScalarField2f();
	}
	else
	{
		int TextureWidth = DisplacementMap->GetSizeX();
		int TextureHeight = DisplacementMap->GetSizeY();
		DisplaceField.Resize(TextureWidth, TextureHeight, 0.0f);
		DisplaceField.SetCellSize(1.0f / (float)TextureWidth);

		const FColor* FormattedData = TextureAccess.GetData();
		for (int y = 0; y < TextureHeight; ++y)
		{
			for (int x = 0; x < TextureWidth; ++x)
			{
				FColor PixelColor = FormattedData[y*TextureWidth + x];
				float Value = PixelColor.R / 255.0;
				DisplaceField.GridValues[y*TextureWidth + x] = Value;
			}
		}
	}
}

void UDisplaceMeshTool::UpdateSubdividedMesh()
{
	if (Subdivisions == CachedSubdivisionsCount)
	{
		return;
	}

	SubdividedMesh.Copy(OriginalMesh);
	int Rounds = Subdivisions;

	// calculate subdivisions (todo: move to elsewhere)
	for (int ri = 0; ri < Rounds; ri++)
	{
		TArray<int> EdgesToProcess;
		for (int tid : SubdividedMesh.EdgeIndicesItr())
		{
			EdgesToProcess.Add(tid);
		}
		int MaxTriangleID = SubdividedMesh.MaxTriangleID();

		TArray<int> TriSplitEdges;
		TriSplitEdges.Init(-1, SubdividedMesh.MaxTriangleID());

		for (int eid : EdgesToProcess)
		{
			FIndex2i EdgeTris = SubdividedMesh.GetEdgeT(eid);

			FDynamicMesh3::FEdgeSplitInfo SplitInfo;
			EMeshResult result = SubdividedMesh.SplitEdge(eid, SplitInfo);
			if (result == EMeshResult::Ok)
			{
				if (EdgeTris.A < MaxTriangleID && TriSplitEdges[EdgeTris.A] == -1)
				{
					TriSplitEdges[EdgeTris.A] = SplitInfo.NewEdges.B;
				}
				if (EdgeTris.B != FDynamicMesh3::InvalidID)
				{
					if (EdgeTris.B < MaxTriangleID && TriSplitEdges[EdgeTris.B] == -1)
					{
						TriSplitEdges[EdgeTris.B] = SplitInfo.NewEdges.C;
					}
				}
			}
		}

		for (int eid : TriSplitEdges)
		{
			if (eid != -1)
			{
				FDynamicMesh3::FEdgeFlipInfo FlipInfo;
				SubdividedMesh.FlipEdge(eid, FlipInfo);
			}
		}
	}

	CachedSubdivisionsCount = Subdivisions;

	// update dynamic mesh component
	DynamicMeshComponent->GetMesh()->Copy(SubdividedMesh);
	DynamicMeshComponent->NotifyMeshUpdated();

	NormalsBuffer = FMeshNormals(&SubdividedMesh);
	NormalsBuffer.ComputeVertexNormals();

	// cache initial positions
	PositionBuffer.SetNum(SubdividedMesh.MaxVertexID());
	for (int vid : SubdividedMesh.VertexIndicesItr())
	{
		PositionBuffer[vid] = SubdividedMesh.GetVertex(vid);
	}
	DisplacedBuffer.SetNum(SubdividedMesh.MaxVertexID());
}


bool UDisplaceMeshTool::HasAccept() const
{
	return true;
}

bool UDisplaceMeshTool::CanAccept() const
{
	return true;
}




#undef LOCTEXT_NAMESPACE
