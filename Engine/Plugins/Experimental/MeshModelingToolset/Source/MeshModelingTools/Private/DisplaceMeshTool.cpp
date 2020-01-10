// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DisplaceMeshTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"
#include "ToolSetupUtil.h"
#include "DynamicMesh3.h"
#include "SimpleDynamicMeshComponent.h"
#include "MeshNormals.h"
#include "ModelingOperators.h"
#include "SceneManagement.h" // for FPrimitiveDrawInterface
#include "Async/ParallelFor.h"
#include "ProfilingDebugging/ScopedTimers.h"
#define LOCTEXT_NAMESPACE "UDisplaceMeshTool"

namespace {
	void SubdivideMesh(FDynamicMesh3& Mesh)
	{
		TArray<int> EdgesToProcess;
		for (int tid : Mesh.EdgeIndicesItr())
	{
			EdgesToProcess.Add(tid);
	}
		int MaxTriangleID = Mesh.MaxTriangleID();

		TArray<int> TriSplitEdges;
		TriSplitEdges.Init(-1, Mesh.MaxTriangleID());

		for (int eid : EdgesToProcess)
		{
			FIndex2i EdgeTris = Mesh.GetEdgeT(eid);

			FDynamicMesh3::FEdgeSplitInfo SplitInfo;
			EMeshResult result = Mesh.SplitEdge(eid, SplitInfo);
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
				Mesh.FlipEdge(eid, FlipInfo);
	}
	}
	}

	namespace ComputeDisplacement {
		void Constant(const FDynamicMesh3& Mesh,
				const TArray<FVector3d>& Positions, const FMeshNormals& Normals, double Intensity,
				TArray<FVector3d>& DisplacedPositions)
	{
			DisplacedPositions.SetNumUninitialized(Positions.Num());
			for (int vid : Mesh.VertexIndicesItr())
	{
				DisplacedPositions[vid] = Positions[vid] + (Intensity * Normals[vid]);
	}
	}

		void RandomNoise(const FDynamicMesh3& Mesh,
				const TArray<FVector3d>& Positions, const FMeshNormals& Normals,
				double Intensity, int RandomSeed,
				TArray<FVector3d>& DisplacedPositions)
	{
	FMath::SRandInit(RandomSeed);
			for (int vid : Mesh.VertexIndicesItr())
	{
		double RandVal = 2.0 * (FMath::SRand() - 0.5);
				DisplacedPositions[vid] = Positions[vid] + (Normals[vid] * RandVal * Intensity);
			}
	}

		void Map(const FDynamicMesh3& Mesh,
				const TArray<FVector3d>& Positions, const FMeshNormals& Normals,
				double Intensity, const FSampledScalarField2f& DisplaceField,
				TArray<FVector3d>& DisplacedPositions)
		{
			const FDynamicMeshUVOverlay* UVOverlay = Mesh.Attributes()->GetUVLayer(0);
			for (int tid : Mesh.TriangleIndicesItr())
	{
				FIndex3i Tri = Mesh.GetTriangle(tid);
		FIndex3i UVTri = UVOverlay->GetTriangle(tid);
		for (int j = 0; j < 3; ++j)
		{
			int vid = Tri[j];
			FVector2f UV = UVOverlay->GetElement(UVTri[j]);
			double Offset = DisplaceField.BilinearSampleClamped(UV);
					DisplacedPositions[vid] = Positions[vid] + (Offset * Intensity * Normals[vid]);
				}
			}

		}
	}
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

			FormattedImageData = reinterpret_cast<const FColor*>(DisplacementMap->PlatformData->Mips[0].BulkData.LockReadOnly());
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
		UTexture2D* DisplacementMap{ nullptr };
		TextureCompressionSettings OldCompressionSettings{};
		TextureMipGenSettings OldMipGenSettings{};
		bool bOldSRGB{ false };
		const FColor* FormattedImageData{ nullptr };
	};

	class FSubdivideMeshOp : public FDynamicMeshOperator
	{
	public:
		FSubdivideMeshOp(const FDynamicMesh3& SourceMesh, int SubdivisionsCountIn);
		void CalculateResult(FProgressCancel* Progress) final;
	private:
		int SubdivisionsCount;
	};

	FSubdivideMeshOp::FSubdivideMeshOp(const FDynamicMesh3& SourceMesh, int SubdivisionsCountIn)
		: SubdivisionsCount(SubdivisionsCountIn)
	{
		ResultMesh->Copy(SourceMesh);
	}

	void FSubdivideMeshOp::CalculateResult(FProgressCancel* ProgressCancel)
	{
		// calculate subdivisions (todo: move to elsewhere)
		for (int ri = 0; ri < SubdivisionsCount; ri++)
		{
			if (ProgressCancel->Cancelled()) return;
			SubdivideMesh(*ResultMesh);
		}
	}

	class FSubdivideMeshOpFactory : public IDynamicMeshOperatorFactory
	{
	public:
		FSubdivideMeshOpFactory(FDynamicMesh3& SourceMeshIn,
			int SubdivisionsCountIn)
			: SourceMesh(SourceMeshIn), SubdivisionsCount(SubdivisionsCountIn)
		{
		}
		void SetSubdivisionsCount(int SubdivisionsCountIn);
		int  GetSubdivisionsCount();

		TUniquePtr<FDynamicMeshOperator> MakeNewOperator() final
		{
			return MakeUnique<FSubdivideMeshOp>(SourceMesh, SubdivisionsCount);
		}
	private:
		const FDynamicMesh3& SourceMesh;
		int SubdivisionsCount;
	};

	void FSubdivideMeshOpFactory::SetSubdivisionsCount(int SubdivisionsCountIn)
	{
		SubdivisionsCount = SubdivisionsCountIn;
	}

	int FSubdivideMeshOpFactory::GetSubdivisionsCount()
	{
		return SubdivisionsCount;
	}

	class FDisplaceMeshOp : public FDynamicMeshOperator
	{
	public:
		FDisplaceMeshOp(TSharedPtr<FDynamicMesh3> SourceMeshIn, const FSampledScalarField2f& DisplaceFieldIn,
			float DisplaceIntensityIn, int RandomSeedIn, EDisplaceMeshToolDisplaceType DisplacementTypeIn);
		void CalculateResult(FProgressCancel* Progress) final;

	private:
		TSharedPtr<FDynamicMesh3> SourceMesh;
		float DisplaceIntensity;
		int RandomSeed;
		EDisplaceMeshToolDisplaceType DisplacementType;
		const FSampledScalarField2f& DisplaceField;

		TArray<FVector3d> SourcePositions;
		FMeshNormals SourceNormals;
		TArray<FVector3d> DisplacedPositions;
	};

	FDisplaceMeshOp::FDisplaceMeshOp(TSharedPtr<FDynamicMesh3> SourceMeshIn, const FSampledScalarField2f& DisplaceFieldIn,
		float DisplaceIntensityIn, int RandomSeedIn, EDisplaceMeshToolDisplaceType DisplacementTypeIn)
		: SourceMesh(MoveTemp(SourceMeshIn)), DisplaceIntensity(DisplaceIntensityIn), RandomSeed(RandomSeedIn),
		DisplacementType(DisplacementTypeIn),DisplaceField(DisplaceFieldIn)
	{
	}

	void FDisplaceMeshOp::CalculateResult(FProgressCancel* Progress)
	{
		if (Progress->Cancelled()) return;
		ResultMesh->Copy(*SourceMesh);

		if (Progress->Cancelled()) return;
		SourceNormals = FMeshNormals(SourceMesh.Get());
		SourceNormals.ComputeVertexNormals();

		if (Progress->Cancelled()) return;
		// cache initial positions
		SourcePositions.SetNum(SourceMesh->MaxVertexID());
		for (int vid : SourceMesh->VertexIndicesItr())
		{
			SourcePositions[vid] = SourceMesh->GetVertex(vid);
		}

		if (Progress->Cancelled()) return;
		DisplacedPositions.SetNum(SourceMesh->MaxVertexID());

		if (Progress->Cancelled()) return;
		// compute Displaced positions in PositionBuffer
		switch (DisplacementType)
		{
		default:
		case EDisplaceMeshToolDisplaceType::Constant:
			ComputeDisplacement::Constant(*SourceMesh, SourcePositions, SourceNormals,
				DisplaceIntensity,
				DisplacedPositions);
			break;

		case EDisplaceMeshToolDisplaceType::RandomNoise:
			ComputeDisplacement::RandomNoise(*SourceMesh, SourcePositions, SourceNormals,
				DisplaceIntensity, RandomSeed,
				DisplacedPositions);
			break;

		case EDisplaceMeshToolDisplaceType::DisplacementMap:
			ComputeDisplacement::Map(*SourceMesh, SourcePositions, SourceNormals,
				DisplaceIntensity, DisplaceField,
				DisplacedPositions);
			break;
		}

		// update preview vertex positions
		for (int vid : ResultMesh->VertexIndicesItr())
		{
			ResultMesh->SetVertex(vid, DisplacedPositions[vid]);
		}

		// recalculate normals
		if (ResultMesh->HasAttributes())
		{
			FMeshNormals Normals(ResultMesh.Get());
			FDynamicMeshNormalOverlay* NormalOverlay = ResultMesh->Attributes()->PrimaryNormals();
			Normals.RecomputeOverlayNormals(NormalOverlay);
			Normals.CopyToOverlay(NormalOverlay);
		}
		else
		{
			FMeshNormals::QuickComputeVertexNormals(*ResultMesh);
		}
	}

	class FDisplaceMeshOpFactory : public IDynamicMeshOperatorFactory
	{
	public:
		FDisplaceMeshOpFactory(TSharedPtr<FDynamicMesh3>& SourceMeshIn,
			float DisplaceIntensityIn, int RandomSeedIn, UTexture2D* DisplacementMapIn,
			EDisplaceMeshToolDisplaceType DisplacementTypeIn)
			: SourceMesh(SourceMeshIn)
		{
			SetIntensity(DisplaceIntensityIn);
			SetRandomSeed(RandomSeed);
			SetDisplacementMap(DisplacementMapIn);
			SetDisplacementType(DisplacementTypeIn);

		}
		void SetIntensity(float IntensityIn);
		void SetRandomSeed(int RandomSeedIn);
		void SetDisplacementMap(UTexture2D* DisplacementMapIn);
		void SetDisplacementType(EDisplaceMeshToolDisplaceType TypeIn);

		TUniquePtr<FDynamicMeshOperator> MakeNewOperator() final
		{
			return MakeUnique<FDisplaceMeshOp>(SourceMesh, DisplaceField, DisplaceIntensity, RandomSeed, DisplacementType);
		}
	private:
		void UpdateMap();

		float DisplaceIntensity;
		int RandomSeed;
		UTexture2D* DisplacementMap;
		EDisplaceMeshToolDisplaceType DisplacementType;

		TSharedPtr<FDynamicMesh3>& SourceMesh;
		FSampledScalarField2f DisplaceField;
	};

	void FDisplaceMeshOpFactory::SetIntensity(float IntensityIn)
	{
		DisplaceIntensity = IntensityIn;
	}

	void FDisplaceMeshOpFactory::SetRandomSeed(int RandomSeedIn)
	{
		RandomSeed = RandomSeedIn;
	}

	void FDisplaceMeshOpFactory::SetDisplacementMap(UTexture2D* DisplacementMapIn)
	{
		if (DisplacementMap != DisplacementMapIn)
		{
			DisplacementMap = DisplacementMapIn;
			UpdateMap();
		}
	}

	void FDisplaceMeshOpFactory::SetDisplacementType(EDisplaceMeshToolDisplaceType TypeIn)
	{
		DisplacementType = TypeIn;
	}

	void FDisplaceMeshOpFactory::UpdateMap()
	{
		if (DisplacementMap == nullptr ||
			DisplacementMap->PlatformData == nullptr ||
			DisplacementMap->PlatformData->Mips.Num() < 1)
	{
		DisplaceField = FSampledScalarField2f();
		return;
	}

		FTextureAccess TextureAccess(DisplacementMap);
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
} // namespace

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
	DisplacementType = EDisplaceMeshToolDisplaceType::Constant;
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

	// transfer materials
	FComponentMaterialSet MaterialSet;
	ComponentTarget->GetMaterialSet(MaterialSet);
	for (int k = 0; k < MaterialSet.Materials.Num(); ++k)
	{
		DynamicMeshComponent->SetMaterial(k, MaterialSet.Materials[k]);
	}

	DynamicMeshComponent->InitializeMesh(ComponentTarget->GetMesh());
	OriginalMesh.Copy(*DynamicMeshComponent->GetMesh());

	Subdivider = MakeUnique<FSubdivideMeshOpFactory>(OriginalMesh, Subdivisions);
	Displacer = MakeUnique<FDisplaceMeshOpFactory>(SubdividedMesh,
		DisplaceIntensity, RandomSeed, DisplacementMap, DisplacementType);
		
	// hide input StaticMeshComponent
	ComponentTarget->SetOwnerVisibility(false);

	// initialize our properties
	ToolPropertyObjects.Add(this);
	ValidateSubdivisions();
	StartComputation();
}

void UDisplaceMeshTool::Shutdown(EToolShutdownType ShutdownType)
{
	if (DynamicMeshComponent != nullptr)
	{
		ComponentTarget->SetOwnerVisibility(true);

		if (ShutdownType == EToolShutdownType::Accept)
	{
			// this block bakes the modified DynamicMeshComponent back into the StaticMeshComponent inside an undo transaction
			GetToolManager()->BeginUndoTransaction(LOCTEXT("DisplaceMeshToolTransactionName", "Displace Mesh"));
			ComponentTarget->CommitMesh([=](const FPrimitiveComponentTarget::FCommitParams& CommitParams)
		{
				DynamicMeshComponent->Bake(CommitParams.MeshDescription, Subdivisions > 0);
			});
			GetToolManager()->EndUndoTransaction();
		}

		DynamicMeshComponent->UnregisterComponent();
		DynamicMeshComponent->DestroyComponent();
		DynamicMeshComponent = nullptr;
	}
}

void UDisplaceMeshTool::ValidateSubdivisions()
{
	constexpr int MaxTriangles = 3000000;
	double NumTriangles = OriginalMesh.MaxTriangleID();
	int MaxSubdivisions = (int)floor(log2(MaxTriangles / NumTriangles) / 2.0);
	if (Subdivisions > MaxSubdivisions)
		{
		FText WarningText = FText::Format(LOCTEXT("SubdivisionsTooHigh", "Desired number of Subdivisions ({0}) exceeds maximum number of {1}"), FText::AsNumber(Subdivisions), FText::AsNumber(MaxSubdivisions));
		GetToolManager()->DisplayMessage(WarningText, EToolMessageLevel::UserWarning);
		Subdivisions = MaxSubdivisions;
	}
	else
			{
		FText ClearWarningText;
		GetToolManager()->DisplayMessage(ClearWarningText, EToolMessageLevel::UserWarning);
	}
	if (Subdivisions < 0)
				{
		Subdivisions = 0;
				}
}

#if WITH_EDITOR
void UDisplaceMeshTool::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	FProperty* PropertyThatChanged = PropertyChangedEvent.Property;
	if (PropertyThatChanged)
	{
		FSubdivideMeshOpFactory*  SubdividerDownCast = static_cast<FSubdivideMeshOpFactory*>(Subdivider.Get());
		FDisplaceMeshOpFactory*  DisplacerDownCast = static_cast<FDisplaceMeshOpFactory*>(Displacer.Get());
		const FName PropName = PropertyThatChanged->GetFName();
		bNeedsDisplaced = true;
		if (PropName == GET_MEMBER_NAME_CHECKED(UDisplaceMeshTool, Subdivisions))
				{
			ValidateSubdivisions();
			if (Subdivisions != SubdividerDownCast->GetSubdivisionsCount())
					{
				SubdividerDownCast->SetSubdivisionsCount(Subdivisions);
				bNeedsSubdivided = true;
					}
			else
			{
				return;
				}
			}
		else if (PropName == GET_MEMBER_NAME_CHECKED(UDisplaceMeshTool, RandomSeed))
		{
			DisplacerDownCast->SetRandomSeed(RandomSeed);
		}
		else if (PropName == GET_MEMBER_NAME_CHECKED(UDisplaceMeshTool, DisplacementType))
		{
			DisplacerDownCast->SetDisplacementType(DisplacementType);
		}
		else if (PropName == GET_MEMBER_NAME_CHECKED(UDisplaceMeshTool, DisplaceIntensity))
			{
			DisplacerDownCast->SetIntensity(DisplaceIntensity);
			}
		else if (PropName == GET_MEMBER_NAME_CHECKED(UDisplaceMeshTool, DisplacementMap))
		{
			DisplacerDownCast->SetDisplacementMap(DisplacementMap);
		}
		StartComputation();
	}
}
#endif

void UDisplaceMeshTool::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime); 
	AdvanceComputation();
}

void UDisplaceMeshTool::StartComputation()
{
	if ( bNeedsSubdivided )
	{
		if (SubdivideTask)
		{
			SubdivideTask->CancelAndDelete();
			SubdividedMesh = nullptr;
		}
		auto SubdivideMeshOp = Subdivider->MakeNewOperator();
		SubdivideTask = new FAsyncTaskExecuterWithAbort<TModelingOpTask<FDynamicMeshOperator>>(Subdivider->MakeNewOperator());
		SubdivideTask->StartBackgroundTask();
		bNeedsSubdivided = false;
		DynamicMeshComponent->SetOverrideRenderMaterial(ToolSetupUtil::GetDefaultWorkingMaterial(GetToolManager()));
	}
	if (bNeedsDisplaced && DisplaceTask)
	{
		DisplaceTask->CancelAndDelete();
		DisplaceTask = nullptr;
		DynamicMeshComponent->SetOverrideRenderMaterial(ToolSetupUtil::GetDefaultWorkingMaterial(GetToolManager()));
	}
	AdvanceComputation();
}

void UDisplaceMeshTool::AdvanceComputation()
{
	if (SubdivideTask && SubdivideTask->IsDone())
	{
		SubdividedMesh = TSharedPtr<FDynamicMesh3>(SubdivideTask->GetTask().ExtractOperator()->ExtractResult().Release());
		delete SubdivideTask;
		SubdivideTask = nullptr;
	}
	if (SubdividedMesh && bNeedsDisplaced)
	{
		DisplaceTask = new FAsyncTaskExecuterWithAbort<TModelingOpTask<FDynamicMeshOperator>>(Displacer->MakeNewOperator());
		DisplaceTask->StartBackgroundTask();
		bNeedsDisplaced = false;
	}
	if (DisplaceTask && DisplaceTask->IsDone())
	{
		TUniquePtr<FDynamicMesh3> DisplacedMesh = DisplaceTask->GetTask().ExtractOperator()->ExtractResult();
		delete DisplaceTask;
		DisplaceTask = nullptr;
		DynamicMeshComponent->ClearOverrideRenderMaterial();
		DynamicMeshComponent->GetMesh()->Copy(*DisplacedMesh);
		DynamicMeshComponent->NotifyMeshUpdated();
		GetToolManager()->PostInvalidation();
	}
}

#undef LOCTEXT_NAMESPACE
