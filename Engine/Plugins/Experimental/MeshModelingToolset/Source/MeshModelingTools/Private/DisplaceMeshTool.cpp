// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplaceMeshTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"
#include "ToolSetupUtil.h"
#include "DynamicMesh3.h"
#include "SimpleDynamicMeshComponent.h"
#include "MeshNormals.h"
#include "ModelingOperators.h"
#include "Async/ParallelFor.h"
#include "ProfilingDebugging/ScopedTimers.h"
#include "MeshDescription.h"
#define LOCTEXT_NAMESPACE "UDisplaceMeshTool"

namespace {

	void SubdivideMesh(FDynamicMesh3& Mesh, FProgressCancel* ProgressCancel)
	{
		TArray<int> EdgesToProcess;
		for (int tid : Mesh.EdgeIndicesItr())
		{
			EdgesToProcess.Add(tid);
		}
		int MaxTriangleID = Mesh.MaxTriangleID();

		if (ProgressCancel && ProgressCancel->Cancelled()) return;

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

			if (ProgressCancel && ProgressCancel->Cancelled()) return;
		}

		for (int eid : TriSplitEdges)
		{
			if (eid != -1)
			{
				FDynamicMesh3::FEdgeFlipInfo FlipInfo;
				Mesh.FlipEdge(eid, FlipInfo);

				if (ProgressCancel && ProgressCancel->Cancelled()) return;
			}
		}
	}

	namespace ComputeDisplacement 
	{
		/// Directional Filter: Scale displacement for a given vertex based on how well 
		/// the vertex normal agrees with the specified direction.
		struct FDirectionalFilter
		{
			bool bEnableFilter = false;
			FVector3d FilterDirection = {1,0,0};
			double FilterWidth = 0.1;
			const double RampSlope = 5.0;

			double FilterValue(const FVector3d& EvalNormal) const
			{
				if (!bEnableFilter)	{ return 1.0;}

				double DotWithFilterDirection = EvalNormal.Dot(FilterDirection);
				double Offset = 1.0 / RampSlope;
				double MinX = 1.0 - (2.0 + Offset) * FilterWidth;			// Start increasing here
				double MaxX = FMathd::Min(1.0, MinX + Offset);				// Stop increasing here
				
				if (FMathd::Abs(MaxX - MinX) < FMathd::ZeroTolerance) { return 0.0; }
				
				double Y = (DotWithFilterDirection - MinX) / (MaxX - MinX); // Clamped linear interpolation for the ramp region
				return FMathd::Clamp(Y, 0.0, 1.0);
			}
		};

		template<typename DisplaceFunc>
		void ParallelDisplace(const FDynamicMesh3& Mesh,
			const TArray<FVector3d>& Positions,
			const FMeshNormals& Normals,
			TArray<FVector3d>& DisplacedPositions,
			DisplaceFunc Displace)
		{
			ensure(Positions.Num() == Normals.GetNormals().Num());
			ensure(Positions.Num() == DisplacedPositions.Num());
			ensure(Mesh.VertexCount() == Positions.Num());

			int32 NumVertices = Mesh.MaxVertexID();
			ParallelFor(NumVertices, [&](int32 vid)
			{
				if (Mesh.IsVertex(vid))
				{
					DisplacedPositions[vid] = Displace(vid, Positions[vid], Normals[vid]);
				}
			});
		}


		void Constant(const FDynamicMesh3& Mesh,
			const TArray<FVector3d>& Positions, 
			const FMeshNormals& Normals, 
			TFunctionRef<float(int32, const FVector3d&, const FVector3d&)> IntensityFunc,
			TArray<FVector3d>& DisplacedPositions)
		{
			ParallelDisplace(Mesh, Positions, Normals, DisplacedPositions,
				[&](int32 vid, const FVector3d& Position, const FVector3d& Normal)
			{
				double Intensity = IntensityFunc(vid, Position, Normal);
				return Position + (Intensity * Normal);
			});
		}


		void RandomNoise(const FDynamicMesh3& Mesh,
			const TArray<FVector3d>& Positions, 
			const FMeshNormals& Normals,
			TFunctionRef<float(int32, const FVector3d&, const FVector3d&)> IntensityFunc,
			int RandomSeed, 
			TArray<FVector3d>& DisplacedPositions)
		{
			FMath::SRandInit(RandomSeed);
			for (int vid : Mesh.VertexIndicesItr())
			{
				double RandVal = 2.0 * (FMath::SRand() - 0.5);
				double Intensity = IntensityFunc(vid, Positions[vid], Normals[vid]);
				DisplacedPositions[vid] = Positions[vid] + (Normals[vid] * RandVal * Intensity);
			}
		}

		void PerlinNoise(const FDynamicMesh3& Mesh,
			const TArray<FVector3d>& Positions,
			const FMeshNormals& Normals,
			TFunctionRef<float(int32, const FVector3d&, const FVector3d&)> IntensityFunc,
			const TArray<FPerlinLayerProperties>& PerlinLayerProperties,
			int RandomSeed,
			TArray<FVector3d>& DisplacedPositions)
		{
			FMath::SRandInit(RandomSeed);
			const float RandomOffset = 10000.0f * FMath::SRand();

			ParallelDisplace(Mesh, Positions, Normals, DisplacedPositions,
				[&](int32 vid, const FVector3d& Position, const FVector3d& Normal)
			{
				// Compute the sum of Perlin noise evaluations for this point
				FVector EvalLocation(Position + RandomOffset);
				double TotalNoiseValue = 0.0;
				for (int32 Layer = 0; Layer < PerlinLayerProperties.Num(); ++Layer)
				{
					TotalNoiseValue += PerlinLayerProperties[Layer].Intensity * FMath::PerlinNoise3D(PerlinLayerProperties[Layer].Frequency * EvalLocation);
				}
				double Intensity = IntensityFunc(vid, Position, Normal);
				return Position + (TotalNoiseValue * Intensity * Normal);
			});
		}

		void Map(const FDynamicMesh3& Mesh,
			const TArray<FVector3d>& Positions, 
			const FMeshNormals& Normals,
			TFunctionRef<float(int32, const FVector3d&, const FVector3d&)> IntensityFunc,
			const FSampledScalarField2f& DisplaceField,
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
					double Intensity = IntensityFunc(vid, Positions[vid], Normals[vid]);
					DisplacedPositions[vid] = Positions[vid] + (Offset * Intensity * Normals[vid]);
				}
			}
		}
		
		void Sine(const FDynamicMesh3& Mesh,
			const TArray<FVector3d>& Positions,
			const FMeshNormals& Normals,
			TFunctionRef<float(int32, const FVector3d&, const FVector3d&)> IntensityFunc,
			double Frequency,
			double PhaseShift,
			const FVector3d& Direction,
			TArray<FVector3d>& DisplacedPositions)
		{
			FQuaterniond RotateToDirection(Direction, { 0.0, 0.0, 1.0 });

			ParallelDisplace(Mesh, Positions, Normals, DisplacedPositions,
				[&](int32 vid, const FVector3d& Position, const FVector3d& Normal)
			{
				FVector3d RotatedPosition = RotateToDirection * Position;
				double DistXY = FMath::Sqrt(RotatedPosition.X * RotatedPosition.X + RotatedPosition.Y * RotatedPosition.Y);
				double Intensity = IntensityFunc(vid, Position, Normal);
				FVector3d Offset = Intensity * FMath::Sin(Frequency * DistXY + PhaseShift) * Direction;
				return Position + Offset;

			});
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
		FSubdivideMeshOp(const FDynamicMesh3& SourceMesh, int SubdivisionsCountIn, TSharedPtr<FIndexedWeightMap> WeightMap);
		void CalculateResult(FProgressCancel* Progress) final;
	private:
		int SubdivisionsCount;
	};

	FSubdivideMeshOp::FSubdivideMeshOp(const FDynamicMesh3& SourceMesh, int SubdivisionsCountIn, TSharedPtr<FIndexedWeightMap> WeightMap)
		: SubdivisionsCount(SubdivisionsCountIn)
	{
		ResultMesh->Copy(SourceMesh);

		// If we have a WeightMap, initialize VertexUV.X with weightmap value. Note that we are going to process .Y anyway,
		// we could (for exmaple) speculatively compute another weightmap, or store previous weightmap values there, to support
		// fast switching between two...
		ResultMesh->EnableVertexUVs(FVector2f::Zero());
		if (WeightMap != nullptr)
		{
			for (int32 vid : ResultMesh->VertexIndicesItr())
			{
				ResultMesh->SetVertexUV(vid, FVector2f(WeightMap->GetValue(vid), 0));
			}
		}
		else
		{
			for (int32 vid : ResultMesh->VertexIndicesItr())
			{
				ResultMesh->SetVertexUV(vid, FVector2f::One());
			}
		}

	}

	void FSubdivideMeshOp::CalculateResult(FProgressCancel* ProgressCancel)
	{
		// calculate subdivisions (todo: move to elsewhere)
		for (int ri = 0; ri < SubdivisionsCount; ri++)
		{
			if (ProgressCancel && ProgressCancel->Cancelled()) return;
			SubdivideMesh(*ResultMesh, ProgressCancel);
		}
	}

	class FSubdivideMeshOpFactory : public IDynamicMeshOperatorFactory
	{
	public:
		FSubdivideMeshOpFactory(FDynamicMesh3& SourceMeshIn,
			int SubdivisionsCountIn,
			TSharedPtr<FIndexedWeightMap> WeightMapIn)
			: SourceMesh(SourceMeshIn), SubdivisionsCount(SubdivisionsCountIn), WeightMap(WeightMapIn)
		{
		}
		void SetSubdivisionsCount(int SubdivisionsCountIn);
		int  GetSubdivisionsCount();

		void SetWeightMap(TSharedPtr<FIndexedWeightMap> WeightMapIn);

		TUniquePtr<FDynamicMeshOperator> MakeNewOperator() final
		{
			return MakeUnique<FSubdivideMeshOp>(SourceMesh, SubdivisionsCount, WeightMap);
		}
	private:
		const FDynamicMesh3& SourceMesh;
		int SubdivisionsCount;
		TSharedPtr<FIndexedWeightMap> WeightMap;
	};

	void FSubdivideMeshOpFactory::SetSubdivisionsCount(int SubdivisionsCountIn)
	{
		SubdivisionsCount = SubdivisionsCountIn;
	}

	int FSubdivideMeshOpFactory::GetSubdivisionsCount()
	{
		return SubdivisionsCount;
	}

	void FSubdivideMeshOpFactory::SetWeightMap(TSharedPtr<FIndexedWeightMap> WeightMapIn)
	{
		WeightMap = WeightMapIn;
	}

	// A collection of parameters to avoid having excess function parameters
	struct DisplaceMeshParameters
	{
		float DisplaceIntensity = 0.0f;
		int RandomSeed = 0;
		UTexture2D* DisplacementMap = nullptr;
		float SineWaveFrequency = 0.0f;
		float SineWavePhaseShift = 0.0f;
		FVector SineWaveDirection = { 0.0f, 0.0f, 0.0f };
		bool bEnableFilter = false;
		FVector FilterDirection = { 0.0f, 0.0f, 0.0f };
		float FilterWidth = 0.0f;
		FSampledScalarField2f DisplaceField;
		TArray<FPerlinLayerProperties> PerlinLayerProperties;

		TSharedPtr<FIndexedWeightMap> WeightMap;
		TFunction<float(const FVector3d&, const FIndexedWeightMap)> WeightMapQueryFunc;
	};

	class FDisplaceMeshOp : public FDynamicMeshOperator
	{
	public:
		FDisplaceMeshOp(TSharedPtr<FDynamicMesh3> SourceMeshIn, 
						const DisplaceMeshParameters& DisplaceParametersIn,
						EDisplaceMeshToolDisplaceType DisplacementTypeIn);
		void CalculateResult(FProgressCancel* Progress) final;

	private:
		TSharedPtr<FDynamicMesh3> SourceMesh;
		DisplaceMeshParameters Parameters;
		EDisplaceMeshToolDisplaceType DisplacementType;
		TArray<FVector3d> SourcePositions;
		FMeshNormals SourceNormals;
		TArray<FVector3d> DisplacedPositions;
	};

	FDisplaceMeshOp::FDisplaceMeshOp(TSharedPtr<FDynamicMesh3> SourceMeshIn, 
									 const DisplaceMeshParameters& DisplaceParametersIn,
									 EDisplaceMeshToolDisplaceType DisplacementTypeIn)
		: SourceMesh(MoveTemp(SourceMeshIn)), 
		  Parameters(DisplaceParametersIn), 
		  DisplacementType(DisplacementTypeIn)
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

		ComputeDisplacement::FDirectionalFilter DirectionalFilter{ Parameters.bEnableFilter,
			FVector3d(Parameters.FilterDirection),
			Parameters.FilterWidth };
		double Intensity = Parameters.DisplaceIntensity;

		TUniqueFunction<float(int32 vid, const FVector3d&)> WeightMapQueryFunc = [&](int32, const FVector3d&) { return 1.0f; };
		if (Parameters.WeightMap.IsValid())
		{
			if (SourceMesh->IsCompactV() && SourceMesh->VertexCount() == Parameters.WeightMap->Values.Num())
			{
				WeightMapQueryFunc = [&](int32 vid, const FVector3d& Pos) { return Parameters.WeightMap->GetValue(vid); };
			}
			else
			{
				// disable input query function as it uses expensive AABBTree lookup
				//WeightMapQueryFunc = [&](int32 vid, const FVector3d& Pos) { return Parameters.WeightMapQueryFunc(Pos, *Parameters.WeightMap); };
				WeightMapQueryFunc = [&](int32 vid, const FVector3d& Pos) { return SourceMesh->GetVertexUV(vid).X; };
			}
		}
		auto IntensityFunc = [&](int32 vid, const FVector3d& Position, const FVector3d& Normal)
		{
			return Intensity * DirectionalFilter.FilterValue(Normal) * WeightMapQueryFunc(vid, Position);
		};



		// compute Displaced positions in PositionBuffer
		switch (DisplacementType)
		{
		default:
		case EDisplaceMeshToolDisplaceType::Constant:
			ComputeDisplacement::Constant(*SourceMesh, 
				SourcePositions, 
				SourceNormals,
				IntensityFunc,
				DisplacedPositions);
			break;

		case EDisplaceMeshToolDisplaceType::RandomNoise:
			ComputeDisplacement::RandomNoise(*SourceMesh, 
				SourcePositions, 
				SourceNormals,
				IntensityFunc,
				Parameters.RandomSeed,
				DisplacedPositions);
			break;
			
		case EDisplaceMeshToolDisplaceType::PerlinNoise:
			ComputeDisplacement::PerlinNoise(*SourceMesh,
				SourcePositions,
				SourceNormals,
				IntensityFunc,
				Parameters.PerlinLayerProperties,	
				Parameters.RandomSeed,
				DisplacedPositions);
			break;

		case EDisplaceMeshToolDisplaceType::DisplacementMap:
			ComputeDisplacement::Map(*SourceMesh, 
				SourcePositions, 
				SourceNormals,
				IntensityFunc,
				Parameters.DisplaceField, 
				DisplacedPositions);
			break;

		case EDisplaceMeshToolDisplaceType::SineWave:
			ComputeDisplacement::Sine(*SourceMesh, 
				SourcePositions, 
				SourceNormals,
				IntensityFunc,
				Parameters.SineWaveFrequency,
				Parameters.SineWavePhaseShift,
				Parameters.SineWaveDirection,
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
			const DisplaceMeshParameters& DisplaceParametersIn,
			EDisplaceMeshToolDisplaceType DisplacementTypeIn )
			: SourceMesh(SourceMeshIn)
		{
			SetIntensity(DisplaceParametersIn.DisplaceIntensity);
			SetRandomSeed(DisplaceParametersIn.RandomSeed);
			SetDisplacementMap(DisplaceParametersIn.DisplacementMap);
			SetFrequency(DisplaceParametersIn.SineWaveFrequency);
			SetPhaseShift(DisplaceParametersIn.SineWavePhaseShift);
			SetSineWaveDirection(DisplaceParametersIn.SineWaveDirection);
			SetEnableDirectionalFilter(DisplaceParametersIn.bEnableFilter);
			SetFilterDirection(DisplaceParametersIn.FilterDirection);
			SetFilterFalloffWidth(DisplaceParametersIn.FilterWidth);
			SetPerlinNoiseLayerProperties(DisplaceParametersIn.PerlinLayerProperties);
			SetDisplacementType(DisplacementTypeIn);

			Parameters.WeightMap = DisplaceParametersIn.WeightMap;
			Parameters.WeightMapQueryFunc = DisplaceParametersIn.WeightMapQueryFunc;
		}
		void SetIntensity(float IntensityIn);
		void SetRandomSeed(int RandomSeedIn);
		void SetDisplacementMap(UTexture2D* DisplacementMapIn);
		void SetFrequency(float FrequencyIn);
		void SetPhaseShift(float PhaseShiftIn);
		void SetSineWaveDirection(const FVector& Direction);
		void SetDisplacementType(EDisplaceMeshToolDisplaceType TypeIn);
		void SetEnableDirectionalFilter(bool EnableDirectionalFilter);
		void SetFilterDirection(const FVector& Direction);
		void SetFilterFalloffWidth(float FalloffWidth);
		void SetPerlinNoiseLayerProperties(const TArray<FPerlinLayerProperties>& PerlinLayerProperties);
		void SetWeightMap(TSharedPtr<FIndexedWeightMap> WeightMap);

		TUniquePtr<FDynamicMeshOperator> MakeNewOperator() final
		{
			return MakeUnique<FDisplaceMeshOp>(SourceMesh, Parameters, DisplacementType);
		}
	private:
		void UpdateMap();

		DisplaceMeshParameters Parameters;
		EDisplaceMeshToolDisplaceType DisplacementType;

		TSharedPtr<FDynamicMesh3>& SourceMesh;
	};

	void FDisplaceMeshOpFactory::SetIntensity(float IntensityIn)
	{
		Parameters.DisplaceIntensity = IntensityIn;
	}

	void FDisplaceMeshOpFactory::SetRandomSeed(int RandomSeedIn)
	{
		Parameters.RandomSeed = RandomSeedIn;
	}

	void FDisplaceMeshOpFactory::SetDisplacementMap(UTexture2D* DisplacementMapIn)
	{
		if (Parameters.DisplacementMap != DisplacementMapIn)
		{
			Parameters.DisplacementMap = DisplacementMapIn;
			UpdateMap();
		}
	}

	void FDisplaceMeshOpFactory::SetFrequency(float FrequencyIn)
	{
		Parameters.SineWaveFrequency = FrequencyIn;
	}

	void FDisplaceMeshOpFactory::SetPhaseShift(float PhaseShiftIn)
	{
		Parameters.SineWavePhaseShift = PhaseShiftIn;
	}

	void FDisplaceMeshOpFactory::SetSineWaveDirection(const FVector& Direction)
	{
		Parameters.SineWaveDirection = Direction;
	}

	void FDisplaceMeshOpFactory::SetDisplacementType(EDisplaceMeshToolDisplaceType TypeIn)
	{
		DisplacementType = TypeIn;
	}

	void FDisplaceMeshOpFactory::UpdateMap()
	{
		if (Parameters.DisplacementMap == nullptr ||
			Parameters.DisplacementMap->PlatformData == nullptr ||
			Parameters.DisplacementMap->PlatformData->Mips.Num() < 1)
		{
			Parameters.DisplaceField = FSampledScalarField2f();
			return;
		}

		FTextureAccess TextureAccess(Parameters.DisplacementMap);
		if (!TextureAccess.HasData())
		{
			Parameters.DisplaceField = FSampledScalarField2f();
		}
		else
		{
			int TextureWidth = Parameters.DisplacementMap->GetSizeX();
			int TextureHeight = Parameters.DisplacementMap->GetSizeY();
			Parameters.DisplaceField.Resize(TextureWidth, TextureHeight, 0.0f);
			Parameters.DisplaceField.SetCellSize(1.0f / (float)TextureWidth);

			const FColor* FormattedData = TextureAccess.GetData();
			for (int y = 0; y < TextureHeight; ++y)
			{
				for (int x = 0; x < TextureWidth; ++x)
				{
					FColor PixelColor = FormattedData[y * TextureWidth + x];
					float Value = PixelColor.R / 255.0;
					Parameters.DisplaceField.GridValues[y * TextureWidth + x] = Value;
				}
			}
		}
	}

	void FDisplaceMeshOpFactory::SetEnableDirectionalFilter(bool EnableDirectionalFilter)
	{
		Parameters.bEnableFilter = EnableDirectionalFilter;
	}

	void FDisplaceMeshOpFactory::SetFilterDirection(const FVector& Direction)
	{
		Parameters.FilterDirection = Direction;
	}

	void FDisplaceMeshOpFactory::SetFilterFalloffWidth(float FalloffWidth)
	{
		Parameters.FilterWidth = FalloffWidth;
	}

	void FDisplaceMeshOpFactory::SetPerlinNoiseLayerProperties(const TArray<FPerlinLayerProperties>& LayerProperties )
	{
		Parameters.PerlinLayerProperties = LayerProperties;
	}

	void FDisplaceMeshOpFactory::SetWeightMap(TSharedPtr<FIndexedWeightMap> WeightMap)
	{
		Parameters.WeightMap = WeightMap;
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
TArray<FString> UDisplaceMeshCommonProperties::GetWeightMapsFunc()
{
	return WeightMapsList;
}

void UDisplaceMeshTool::Setup()
{
	UInteractiveTool::Setup();

	// UInteractiveToolPropertySets
	NoiseProperties = NewObject<UDisplaceMeshPerlinNoiseProperties>();
	NoiseProperties->RestoreProperties(this);
	CommonProperties = NewObject<UDisplaceMeshCommonProperties>();
	CommonProperties->RestoreProperties(this);
	DirectionalFilterProperties = NewObject<UDisplaceMeshDirectionalFilterProperties>();
	DirectionalFilterProperties->RestoreProperties(this);
	TextureMapProperties = NewObject<UDisplaceMeshTextureMapProperties>();
	TextureMapProperties->RestoreProperties(this);
	SineWaveProperties = NewObject<UDisplaceMeshSineWaveProperties>();
	SineWaveProperties->RestoreProperties(this);

	if (TextureMapProperties->DisplacementMap != nullptr && TextureMapProperties->DisplacementMap->IsValidLowLevel() == false)
	{
		TextureMapProperties->DisplacementMap = nullptr;
	}

	// populate weight maps list
	TArray<FName> WeightMaps;
	UE::WeightMaps::FindVertexWeightMaps(ComponentTarget->GetMesh(), WeightMaps);
	CommonProperties->WeightMapsList.Add(TEXT("None"));
	for (FName Name : WeightMaps)
	{
		CommonProperties->WeightMapsList.Add(Name.ToString());
	}
	if (WeightMaps.Contains(CommonProperties->WeightMap) == false)		// discard restored value if it doesn't apply
	{
		CommonProperties->WeightMap = FName(CommonProperties->WeightMapsList[0]);
	}
	UpdateActiveWeightMap();


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

	DynamicMeshComponent->TangentsType = EDynamicMeshTangentCalcType::AutoCalculated;
	DynamicMeshComponent->InitializeMesh(ComponentTarget->GetMesh());
	OriginalMesh.Copy(*DynamicMeshComponent->GetMesh());
	OriginalMeshSpatial.SetMesh(&OriginalMesh, true);

	DisplaceMeshParameters Parameters;
	Parameters.DisplaceIntensity = CommonProperties->DisplaceIntensity;
	Parameters.RandomSeed = CommonProperties->RandomSeed;
	Parameters.DisplacementMap = TextureMapProperties->DisplacementMap;
	Parameters.SineWaveFrequency = SineWaveProperties->SineWaveFrequency;
	Parameters.SineWavePhaseShift = SineWaveProperties->SineWavePhaseShift;
	Parameters.SineWaveDirection = SineWaveProperties->SineWaveDirection;
	Parameters.bEnableFilter = DirectionalFilterProperties->bEnableFilter;
	Parameters.FilterDirection = DirectionalFilterProperties->FilterDirection;
	Parameters.FilterWidth = DirectionalFilterProperties->FilterWidth;
	Parameters.PerlinLayerProperties = NoiseProperties->PerlinLayerProperties;
	Parameters.WeightMap = ActiveWeightMap;
	Parameters.WeightMapQueryFunc = [this](const FVector3d& Position, const FIndexedWeightMap& WeightMap) { return WeightMapQuery(Position, WeightMap);	};

	Displacer = MakeUnique<FDisplaceMeshOpFactory>(SubdividedMesh, Parameters, CommonProperties->DisplacementType);
		
	// hide input StaticMeshComponent
	ComponentTarget->SetOwnerVisibility(false);

	// initialize our properties
	ToolPropertyObjects.Add(this);

	AddToolPropertySource(CommonProperties);
	SetToolPropertySourceEnabled(CommonProperties, true);

	AddToolPropertySource(DirectionalFilterProperties);
	SetToolPropertySourceEnabled(DirectionalFilterProperties, true);

	AddToolPropertySource(TextureMapProperties);
	SetToolPropertySourceEnabled(TextureMapProperties, CommonProperties->DisplacementType == EDisplaceMeshToolDisplaceType::DisplacementMap);

	AddToolPropertySource(SineWaveProperties);
	SetToolPropertySourceEnabled(SineWaveProperties, CommonProperties->DisplacementType == EDisplaceMeshToolDisplaceType::SineWave);

	AddToolPropertySource(NoiseProperties);
	SetToolPropertySourceEnabled(NoiseProperties, CommonProperties->DisplacementType == EDisplaceMeshToolDisplaceType::PerlinNoise);

	// Set up a callback for when the type of displacement changes
	CommonProperties->WatchProperty(CommonProperties->DisplacementType,
									[this](EDisplaceMeshToolDisplaceType NewType)
									{
										SetToolPropertySourceEnabled(NoiseProperties, (NewType == EDisplaceMeshToolDisplaceType::PerlinNoise));
										SetToolPropertySourceEnabled(SineWaveProperties, (NewType == EDisplaceMeshToolDisplaceType::SineWave));
										SetToolPropertySourceEnabled(TextureMapProperties, (NewType == EDisplaceMeshToolDisplaceType::DisplacementMap));
									} );

	ValidateSubdivisions();
	Subdivider = MakeUnique<FSubdivideMeshOpFactory>(OriginalMesh, CommonProperties->Subdivisions, ActiveWeightMap);

	StartComputation();

	GetToolManager()->DisplayMessage(
		LOCTEXT("OnStartDisplaceMesh", "Subdivide and Displace the input mesh using different noise functions and maps"),
		EToolMessageLevel::UserNotification);
}

void UDisplaceMeshTool::Shutdown(EToolShutdownType ShutdownType)
{
	CommonProperties->SaveProperties(this);
	NoiseProperties->SaveProperties(this);
	DirectionalFilterProperties->SaveProperties(this);
	SineWaveProperties->SaveProperties(this);
	TextureMapProperties->SaveProperties(this);

	if (DynamicMeshComponent != nullptr)
	{
		ComponentTarget->SetOwnerVisibility(true);

		if (ShutdownType == EToolShutdownType::Accept)
		{
			// this block bakes the modified DynamicMeshComponent back into the StaticMeshComponent inside an undo transaction
			GetToolManager()->BeginUndoTransaction(LOCTEXT("DisplaceMeshToolTransactionName", "Displace Mesh"));
			ComponentTarget->CommitMesh([=](const FPrimitiveComponentTarget::FCommitParams& CommitParams)
			{
				DynamicMeshComponent->Bake(CommitParams.MeshDescription, CommonProperties->Subdivisions > 0);
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
	if (CommonProperties->bDisableSizeWarning)
	{
		GetToolManager()->DisplayMessage(FText::GetEmpty(), EToolMessageLevel::UserWarning);
		return;
	}

	bool bIsInitialized = (Subdivider != nullptr);

	constexpr int MaxTriangles = 3000000;
	double NumTriangles = OriginalMesh.MaxTriangleID();
	int MaxSubdivisions = (int)floor(log2(MaxTriangles / NumTriangles) / 2.0);
	if (CommonProperties->Subdivisions > MaxSubdivisions)
	{
		if (bIsInitialized)		// only show warning after initial tool startup
		{
			FText WarningText = FText::Format(LOCTEXT("SubdivisionsTooHigh", "Desired number of Subdivisions ({0}) exceeds maximum number ({1}) for a mesh of this number of triangles."),
				FText::AsNumber(CommonProperties->Subdivisions),
				FText::AsNumber(MaxSubdivisions));
			GetToolManager()->DisplayMessage(WarningText, EToolMessageLevel::UserWarning);
		}
		CommonProperties->Subdivisions = MaxSubdivisions;
	}
	else
	{
		FText ClearWarningText;
		GetToolManager()->DisplayMessage(ClearWarningText, EToolMessageLevel::UserWarning);
	}
	if (CommonProperties->Subdivisions < 0)
	{
		CommonProperties->Subdivisions = 0;
	}
}

#if WITH_EDITOR

void UDisplaceMeshTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	if (PropertySet && Property)
	{
		FDisplaceMeshOpFactory* DisplacerDownCast = static_cast<FDisplaceMeshOpFactory*>(Displacer.Get());
		FSubdivideMeshOpFactory* SubdividerDownCast = static_cast<FSubdivideMeshOpFactory*>(Subdivider.Get());

		const FString PropertySetName = PropertySet->GetFName().GetPlainNameString();
		const FName PropName = Property->GetFName();
	
		bNeedsDisplaced = true;

		if (PropName == GET_MEMBER_NAME_CHECKED(UDisplaceMeshCommonProperties, Subdivisions))
		{
			ValidateSubdivisions();
			if (CommonProperties->Subdivisions != SubdividerDownCast->GetSubdivisionsCount())
			{
				SubdividerDownCast->SetSubdivisionsCount(CommonProperties->Subdivisions);
				bNeedsSubdivided = true;
			}
			else
			{
				return;
			}
		}
		else if (PropName == GET_MEMBER_NAME_CHECKED(UDisplaceMeshCommonProperties, RandomSeed))
		{
			DisplacerDownCast->SetRandomSeed(CommonProperties->RandomSeed);
		}
		else if (PropName == GET_MEMBER_NAME_CHECKED(UDisplaceMeshCommonProperties, DisplacementType))
		{
			DisplacerDownCast->SetDisplacementType(CommonProperties->DisplacementType);
		}
		else if (PropName == GET_MEMBER_NAME_CHECKED(UDisplaceMeshCommonProperties, DisplaceIntensity))
		{
			DisplacerDownCast->SetIntensity(CommonProperties->DisplaceIntensity);
		}
		else if (PropName == GET_MEMBER_NAME_CHECKED(UDisplaceMeshSineWaveProperties, SineWaveFrequency))
		{
			DisplacerDownCast->SetFrequency(SineWaveProperties->SineWaveFrequency);
		}
		else if (PropName == GET_MEMBER_NAME_CHECKED(UDisplaceMeshSineWaveProperties, SineWavePhaseShift))
		{
			DisplacerDownCast->SetPhaseShift(SineWaveProperties->SineWavePhaseShift);
		}
		else if (PropName == GET_MEMBER_NAME_CHECKED(UDisplaceMeshTextureMapProperties, DisplacementMap))
		{
			DisplacerDownCast->SetDisplacementMap(TextureMapProperties->DisplacementMap);
		}
		else if (PropName == GET_MEMBER_NAME_CHECKED(UDisplaceMeshCommonProperties, WeightMap) 
				 || PropName == GET_MEMBER_NAME_CHECKED(UDisplaceMeshCommonProperties, bInvertWeightMap))
		{
			UpdateActiveWeightMap();
			SubdividerDownCast->SetWeightMap(ActiveWeightMap);
			DisplacerDownCast->SetWeightMap(ActiveWeightMap);
			bNeedsSubdivided = true;
		}
		else if (PropName == GET_MEMBER_NAME_CHECKED(UDisplaceMeshDirectionalFilterProperties, bEnableFilter))
		{
			DisplacerDownCast->SetEnableDirectionalFilter(DirectionalFilterProperties->bEnableFilter);
		}
		else if (PropName == GET_MEMBER_NAME_CHECKED(UDisplaceMeshDirectionalFilterProperties, FilterWidth))
		{
			DisplacerDownCast->SetFilterFalloffWidth(DirectionalFilterProperties->FilterWidth);
		}
		else if ((PropName == GET_MEMBER_NAME_CHECKED(FPerlinLayerProperties, Frequency)) || (PropName == GET_MEMBER_NAME_CHECKED(FPerlinLayerProperties, Intensity)))
		{
			DisplacerDownCast->SetPerlinNoiseLayerProperties(NoiseProperties->PerlinLayerProperties);
		}
		else if (PropName == "X" || PropName == "Y" || PropName == "Z")
		{
			if (PropertySetName == "DisplaceMeshDirectionalFilterProperties")
			{
				DirectionalFilterProperties->FilterDirection.Normalize();
				DisplacerDownCast->SetFilterDirection(DirectionalFilterProperties->FilterDirection);
			}
			else if (PropertySetName == "DisplaceMeshSineWaveProperties")
			{
				SineWaveProperties->SineWaveDirection.Normalize();
				DisplacerDownCast->SetSineWaveDirection(SineWaveProperties->SineWaveDirection);
			}
		}


		StartComputation();
	}
}
#endif

void UDisplaceMeshTool::OnTick(float DeltaTime)
{
	AdvanceComputation();
}

void UDisplaceMeshTool::StartComputation()
{
	if ( bNeedsSubdivided )
	{
		if (SubdivideTask)
		{
			SubdivideTask->CancelAndDelete();
		}
		SubdividedMesh = nullptr;
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



void UDisplaceMeshTool::UpdateActiveWeightMap()
{
	if (CommonProperties->WeightMap == FName(TEXT("None")))
	{
		ActiveWeightMap = nullptr;
	}
	else
	{
		TSharedPtr<FIndexedWeightMap> NewWeightMap = MakeShared<FIndexedWeightMap>();
		UE::WeightMaps::GetVertexWeightMap(ComponentTarget->GetMesh(), CommonProperties->WeightMap, *NewWeightMap, 1.0f);
		if (CommonProperties->bInvertWeightMap)
		{
			NewWeightMap->InvertWeightMap();
		}
		ActiveWeightMap = NewWeightMap;
	}
}


float UDisplaceMeshTool::WeightMapQuery(const FVector3d& Position, const FIndexedWeightMap& WeightMap) const
{
	double NearDistSqr;
	int32 NearTID = OriginalMeshSpatial.FindNearestTriangle(Position, NearDistSqr);
	if (NearTID < 0)
	{
		return 1.0f;
	}
	FDistPoint3Triangle3d Distance = TMeshQueries<FDynamicMesh3>::TriangleDistance(OriginalMesh, NearTID, Position);
	FIndex3i Tri = OriginalMesh.GetTriangle(NearTID);
	return WeightMap.GetInterpValue(Tri, Distance.TriangleBaryCoords);
}


#include "Tests/DisplaceMeshTool_Tests.inl"


#undef LOCTEXT_NAMESPACE
