// Copyright Epic Games, Inc. All Rights Reserved.

#include "WaterBodySceneProxy.h"
#include "WaterSplineMetadata.h"
#include "WaterModule.h"
#include "WaterUtils.h"
#include "WaterBodyComponent.h"
#include "WaterBodyOceanComponent.h"
#include "WaterBodyRiverComponent.h"
#include "WaterBodyLakeComponent.h"
#include "WaterSplineComponent.h"
#include "Algo/Transform.h"
#include "MeshBoundaryLoops.h"
#include "Curve/GeneralPolygon2.h"
#include "Operations/MinimalHoleFiller.h"
#include "DynamicMesh/DynamicMeshChangeTracker.h"
#include "Operations/InsetMeshRegion.h"
#include "ConstrainedDelaunay2.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "SceneInterface.h"


static TAutoConsoleVariable<int32> CVarWaterShowProxies(
	TEXT("r.Water.ShowWaterSceneProxies"),
	0,
	TEXT("Allows editor visualization of water scene proxies. If the mode is set to 1 we will show only the selected water body in wireframe, if it is set to 2 we will show all in wireframe, and if it is 3 we will show all as opaque meshes"),
	ECVF_Default);

// ----------------------------------------------------------------------------------

class FWaterBodyMeshSection
{
public:
	FStaticMeshVertexBuffers VertexBuffers;
	FDynamicMeshIndexBuffer32 IndexBuffer;
	FLocalVertexFactory VertexFactory;

	FWaterBodyMeshSection(ERHIFeatureLevel::Type InFeatureLevel)
		: VertexFactory(InFeatureLevel, "FWaterBodyMeshSection")
	{}
};

// ----------------------------------------------------------------------------------

static FColor PackWaterFlow(float VelocityMagnitude, float DirectionAngle)
{
	check((DirectionAngle >= 0.f) && (DirectionAngle <= TWO_PI));

	const float MaxVelocity = FWaterUtils::GetWaterMaxFlowVelocity(false);

	float NormalizedMagnitude = FMath::Clamp(VelocityMagnitude, 0.f, MaxVelocity) / MaxVelocity;
	float NormalizedAngle = DirectionAngle / TWO_PI;
	float MappedMagnitude = NormalizedMagnitude * TNumericLimits<uint16>::Max();
	float MappedAngle = NormalizedAngle * TNumericLimits<uint16>::Max();

	uint16 ResultMag = (uint16)MappedMagnitude;
	uint16 ResultAngle = (uint16)MappedAngle;
	
	FColor Result;
	Result.R = (ResultMag >> 8) & 0xFF;
	Result.G = (ResultMag >> 0) & 0xFF;
	Result.B = (ResultAngle >> 8) & 0xFF;
	Result.A = (ResultAngle >> 0) & 0xFF;

	return Result;
}

// ----------------------------------------------------------------------------------

using namespace UE::Geometry;

void FWaterBodySceneProxy::AddSectionFromDynamicMesh(const FDynamicMesh3& DynamicMesh, TFunctionRef<FDynamicMeshVertex(const FVector&)> VertTransformFunc)
{
	TArray<FDynamicMeshVertex> Vertices;
	Vertices.Reserve(DynamicMesh.VertexCount());

	Algo::Transform(DynamicMesh.GetVerticesBuffer(), Vertices, VertTransformFunc);

	FWaterBodyMeshSection* NewSection = new FWaterBodyMeshSection(GetScene().GetFeatureLevel());
	NewSection->IndexBuffer.Indices.Reserve(DynamicMesh.TriangleCount() * 3);
	for (const FIndex3i& Index : DynamicMesh.GetTrianglesBuffer())
	{
		NewSection->IndexBuffer.Indices.Add(Index.A);
		NewSection->IndexBuffer.Indices.Add(Index.B);
		NewSection->IndexBuffer.Indices.Add(Index.C);
	}
	
	NewSection->VertexBuffers.InitFromDynamicVertex(&NewSection->VertexFactory, Vertices);
	InitResources(NewSection);

	Sections.Add(NewSection);
}

void FWaterBodySceneProxy::GenerateLakeMesh(UWaterBodyLakeComponent* Component)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(GenerateLakeMesh);

	const UWaterSplineComponent* SplineComp = Component->GetWaterSpline();
	if (SplineComp->GetNumberOfSplineSegments() < 3)
	{
		return;
	}
	FPolygon2d LakePoly;
	{
		TArray<FVector> PolyLineVertices;
		SplineComp->ConvertSplineToPolyLine(ESplineCoordinateSpace::Local, FMath::Square(10.f), PolyLineVertices);
		
		for (int32 i = 0; i < PolyLineVertices.Num() - 1; ++i) // skip the last vertex since it's the same as the first vertex
		{
			LakePoly.AppendVertex(FVector2D(PolyLineVertices[i]));
		}
	}
	
	FConstrainedDelaunay2d Triangulation;
	Triangulation.FillRule = FConstrainedDelaunay2d::EFillRule::Positive;
	Triangulation.Add(LakePoly);
	Triangulation.Triangulate();

	if (Triangulation.Triangles.Num() == 0)
	{
		return;
	}

	FDynamicMesh3 LakeMesh(EMeshComponents::None);
	for (const FVector2d& Vertex : Triangulation.Vertices)
	{
		LakeMesh.AppendVertex(FVector3d(Vertex.X, Vertex.Y, 0));
	}
	for (const FIndex3i& Triangle : Triangulation.Triangles)
	{
		LakeMesh.AppendTriangle(Triangle);
	}

	AddSectionFromDynamicMesh(LakeMesh, [WaterBodyIndex = Component->GetWaterBodyIndex()](const FVector& In) {
			FDynamicMeshVertex Result(FVector3f(In.X, In.Y, 0.f));
			Result.Color = FColor::Black;
			Result.TextureCoordinate[0].X = WaterBodyIndex;
			return Result;
		});
	
	if (Component->ShapeDilation > 0.f)
	{
		// Inset the mesh by -ShapeDilation to effectively expand the mesh
		FInsetMeshRegion Inset(&LakeMesh);
		Inset.InsetDistance = -1 * Component->ShapeDilation / 2.f;

		for (const FIndex3i& Triangle : LakeMesh.GetTrianglesBuffer())
		{
			Inset.Triangles.Add(Triangle.A);
			Inset.Triangles.Add(Triangle.B);
			Inset.Triangles.Add(Triangle.C);
		}
		
		if (!Inset.Apply())
		{
			UE_LOG(LogWater, Warning, TEXT("Failed to apply mesh inset for shape dilation (%s"), *Component->GetOwner()->GetActorNameOrLabel());
		}
	}
	
	// Push down the dilated region vertices by -ZOffset to prevent dilated regions from overwriting data from adjacent water bodies
	AddSectionFromDynamicMesh(LakeMesh, [ZOffset = Component->GetShapeDilationZOffset()](const FVector& In) {
			FDynamicMeshVertex Result(FVector3f(In.X, In.Y, ZOffset));
			Result.Color = FColor::Black;
			Result.TextureCoordinate[0].X = -1;
			return Result;
		});
}

void FWaterBodySceneProxy::GenerateOceanMesh(UWaterBodyOceanComponent* Component)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(GenerateOceanMesh);

	const UWaterSplineComponent* SplineComp = Component->GetWaterSpline();
	
	if (SplineComp->GetNumberOfSplineSegments() < 3)
	{
		return;
	}

	FPolygon2d Island;
	{
		TArray<FVector> PolyLineVertices;
		SplineComp->ConvertSplineToPolyLine(ESplineCoordinateSpace::Local, FMath::Square(10.f), PolyLineVertices);
		
		// Construct a 2D polygon describing the central island
		for (int32 i = PolyLineVertices.Num() - 2; i >= 0; --i) // skip the last vertex since it's the same as the first vertex
		{
			Island.AppendVertex(FVector2D(PolyLineVertices[i]));
		}
	}

	FVector OceanLocation = Component->GetComponentLocation();
	FVector2D OceanExtent = Component->GetVisualExtents();
	FPolygon2d OceanBoundingPolygon = FPolygon2d::MakeRectangle(FVector2d(OceanLocation.X, OceanLocation.Y), OceanExtent.X, OceanExtent.Y);
	FGeneralPolygon2d FinalPoly(OceanBoundingPolygon);
	FinalPoly.AddHole(Island);

	FConstrainedDelaunay2d Triangulation;
	Triangulation.FillRule = FConstrainedDelaunay2d::EFillRule::Positive;
	Triangulation.Add(FinalPoly);
	Triangulation.Triangulate();

	if (Triangulation.Triangles.Num() == 0)
	{
		return;
	}

	FDynamicMesh3 OceanMesh(EMeshComponents::None);
	for (FVector2d Vertex : Triangulation.Vertices)
	{
		OceanMesh.AppendVertex(FVector3d(Vertex.X, Vertex.Y, 0));
	}
	for (FIndex3i Triangle : Triangulation.Triangles)
	{
		OceanMesh.AppendTriangle(Triangle);
	}
	AddSectionFromDynamicMesh(OceanMesh, [WaterBodyIndex = Component->GetWaterBodyIndex()](const FVector& In) {
			FDynamicMeshVertex Result(FVector3f(In.X, In.Y, 0.f));
			Result.Color = FColor::Black;
			Result.TextureCoordinate[0].X = WaterBodyIndex;
			return Result;
		});

	if (Component->ShapeDilation > 0.f)
	{
		// Inset the mesh by -ShapeDilation to effectively expand the mesh
		FInsetMeshRegion Inset(&OceanMesh);
		Inset.InsetDistance = -1 * Component->ShapeDilation / 2.f;

		for (FIndex3i Triangle : OceanMesh.GetTrianglesBuffer())
		{
			Inset.Triangles.Add(Triangle.A);
			Inset.Triangles.Add(Triangle.B);
			Inset.Triangles.Add(Triangle.C);
		}
		
		if (!Inset.Apply())
		{
			UE_LOG(LogWater, Warning, TEXT("Failed to apply mesh inset for shape dilation (%s"), *Component->GetOwner()->GetActorNameOrLabel());
		}
	}
	
	// Push down the dilated region vertices by -ZOffset to prevent dilated regions from overwriting data from adjacent water bodies
	AddSectionFromDynamicMesh(OceanMesh, [ZOffset = Component->GetShapeDilationZOffset()](const FVector& In) {
			FDynamicMeshVertex Result(FVector3f(In.X, In.Y, ZOffset));
			Result.Color = FColor::Black;
			Result.TextureCoordinate[0].X = -1;
			return Result;
		});
}

static void AddVerticesForRiverSplineStep(float DistanceAlongSpline, const UWaterBodyRiverComponent* Component, TArray<FDynamicMeshVertex>& Vertices, TArray<uint32>& Indices)
{
	const UWaterSplineComponent* SplineComp = Component->GetWaterSpline();
	
	const FVector Tangent = SplineComp->GetTangentAtDistanceAlongSpline(DistanceAlongSpline, ESplineCoordinateSpace::Local).GetSafeNormal();
	const FVector Up = SplineComp->GetUpVectorAtDistanceAlongSpline(DistanceAlongSpline, ESplineCoordinateSpace::Local).GetSafeNormal();

	const FVector Normal = FVector::CrossProduct(Tangent, Up).GetSafeNormal();
	const FVector Pos = SplineComp->GetLocationAtDistanceAlongSpline(DistanceAlongSpline, ESplineCoordinateSpace::Local);

	const float Key = SplineComp->SplineCurves.ReparamTable.Eval(DistanceAlongSpline, 0.f);
	const float HalfWidth = Cast<UWaterSplineMetadata>(SplineComp->GetSplinePointsMetadata())->RiverWidth.Eval(Key) / 2;
	float Velocity = Cast<UWaterSplineMetadata>(SplineComp->GetSplinePointsMetadata())->WaterVelocityScalar.Eval(Key);

	// Distance from the center of the spline to place our first vertices
	FVector OutwardDistance = Normal * HalfWidth;
	// Prevent there being a relative height difference between the two vertices even when the spline has a slight roll to it
	OutwardDistance.Z = 0.f;

	const float DilationAmount = Component->ShapeDilation;
	const FVector DilationOffset = Normal * DilationAmount;

	FDynamicMeshVertex Left(FVector3f(Pos - OutwardDistance));
	FDynamicMeshVertex Right(FVector3f(Pos + OutwardDistance));

	FDynamicMeshVertex DilatedFarLeft(FVector3f(Pos - OutwardDistance - DilationOffset));
	DilatedFarLeft.Position.Z += Component->GetShapeDilationZOffsetFar();
	FDynamicMeshVertex DilatedLeft(FVector3f(Pos - OutwardDistance));
	DilatedLeft.Position.Z += Component->GetShapeDilationZOffset();
	FDynamicMeshVertex DilatedRight(FVector3f(Pos + OutwardDistance));
	DilatedRight.Position.Z += Component->GetShapeDilationZOffset();
	FDynamicMeshVertex DilatedFarRight(FVector3f(Pos + OutwardDistance + DilationOffset));
	DilatedFarRight.Position.Z += Component->GetShapeDilationZOffsetFar();

	float FlowDirection = Tangent.HeadingAngle() + FMath::DegreesToRadians(Component->GetRelativeRotation().Yaw);
	// Convert negative angles into positive angles
	if (FlowDirection < 0.f)
	{
		FlowDirection = TWO_PI + FlowDirection;
	}

	// If negative velocity, inverse the direction and change the velocity back to positive.
	if (Velocity < 0.f)
	{
		Velocity *= -1.f;
		FlowDirection = FMath::Fmod(PI + FlowDirection, TWO_PI);
	}

	const FColor EmptyFlowData = FColor(0.f);

	const FColor PackedFlowData = PackWaterFlow(Velocity, FlowDirection);
	Left.Color = PackedFlowData;
	Right.Color = PackedFlowData;

	DilatedFarLeft.Color = EmptyFlowData;
	DilatedLeft.Color = EmptyFlowData;
	DilatedRight.Color = EmptyFlowData;
	DilatedFarRight.Color = EmptyFlowData;

	// Embed the water body index in the vertex data so that we can distinguish between dilated and undilated regions of the texture
	const int32 WaterBodyIndex = Component->GetWaterBodyIndex();
	Left.TextureCoordinate[0].X = WaterBodyIndex;
	Right.TextureCoordinate[0].X = WaterBodyIndex;
	
	DilatedFarLeft.TextureCoordinate[0].X = -1;
	DilatedLeft.TextureCoordinate[0].X = -1;
	DilatedRight.TextureCoordinate[0].X = -1;
	DilatedFarRight.TextureCoordinate[0].X = -1;

	/* River segment geometry:
		6---7,8---9,10--11
		| /  |  /  |  / |
		0---1,2---3,4---5
	*/
	const uint32 BaseIndex = Vertices.Num();
	Vertices.Append({ DilatedFarLeft, DilatedLeft, Left, Right, DilatedRight, DilatedFarRight });
	// Append left dilation quad
	Indices.Append({ BaseIndex, BaseIndex + 7, BaseIndex + 1, BaseIndex, BaseIndex + 6, BaseIndex + 7 });
	// Append main quad
	Indices.Append({ BaseIndex + 2, BaseIndex + 9, BaseIndex + 3, BaseIndex + 2, BaseIndex + 8, BaseIndex + 9});
	// Append right dilation quad
	Indices.Append({ BaseIndex + 4, BaseIndex + 11, BaseIndex + 5, BaseIndex + 4, BaseIndex + 10, BaseIndex + 11 });
}

enum class ERiverBoundaryEdge {
	Start,
	End,
};

static void AddTerminalVerticesForRiverSpline(ERiverBoundaryEdge Edge, const UWaterBodyRiverComponent* Component, TArray<FDynamicMeshVertex>& Vertices, TArray<uint32>& Indices)
{
	const UWaterSplineComponent* SplineComp = Component->GetWaterSpline();

	const float DistanceAlongSpline = (Edge == ERiverBoundaryEdge::Start) ? 0.f : SplineComp->GetSplineLength();
	
	const FVector Tangent = SplineComp->GetTangentAtDistanceAlongSpline(DistanceAlongSpline, ESplineCoordinateSpace::Local).GetSafeNormal();
	const FVector Up = SplineComp->GetUpVectorAtDistanceAlongSpline(DistanceAlongSpline, ESplineCoordinateSpace::Local).GetSafeNormal();
	
	const FVector Normal = FVector::CrossProduct(Tangent, Up).GetSafeNormal();
	const FVector Pos = SplineComp->GetLocationAtDistanceAlongSpline(DistanceAlongSpline, ESplineCoordinateSpace::Local);
	
	const float Key = SplineComp->SplineCurves.ReparamTable.Eval(DistanceAlongSpline, 0.f);
	const float HalfWidth = Cast<UWaterSplineMetadata>(SplineComp->GetSplinePointsMetadata())->RiverWidth.Eval(Key) / 2;

	const float DilationAmount = Component->ShapeDilation;
	const FVector DilationOffset = Normal * DilationAmount;
	FVector OutwardDistance = Normal * HalfWidth;
	OutwardDistance.Z = 0.f;

	FVector TangentialOffset = Tangent * DilationAmount;
	TangentialOffset.Z = 0.f;

	// For the starting edge the tangential offset is negative to push it backwards
	if (Edge == ERiverBoundaryEdge::Start)
	{
		TangentialOffset *= -1;
	}

	FDynamicMeshVertex BackLeft(FVector3f(Pos - OutwardDistance + TangentialOffset - DilationOffset));
	BackLeft.Position.Z += Component->GetShapeDilationZOffsetFar();
	FDynamicMeshVertex Left(FVector3f(Pos - OutwardDistance + TangentialOffset));
	Left.Position.Z += Component->GetShapeDilationZOffsetFar();
	FDynamicMeshVertex Right(FVector3f(Pos + OutwardDistance + TangentialOffset));
	Right.Position.Z += Component->GetShapeDilationZOffsetFar();
	FDynamicMeshVertex BackRight(FVector3f(Pos + OutwardDistance + TangentialOffset + DilationOffset));
	BackRight.Position.Z += Component->GetShapeDilationZOffsetFar();

	// Initialize the vertex data to correct represent what we expect the dilated region to look like
	// (no color and -1 UVs[0].x
	BackLeft.Color = FColor(0.f);
	BackLeft.TextureCoordinate[0].X = -1;
	Left.Color = FColor(0.f);
	Left.TextureCoordinate[0].X = -1;
	Right.Color = FColor(0.f);
	Right.TextureCoordinate[0].X = -1;
	BackRight.Color = FColor(0.f);
	BackRight.TextureCoordinate[0].X = -1;
	

	Vertices.Append({ BackLeft, Left, Right, BackRight });
	if (Edge == ERiverBoundaryEdge::Start)
	{
		/* Dilated front segment geometry: (ignore vertices 6-7 since they are the non-dilated vertices)
			4---5,6---7,8---9
			|    |     |    |
			0----1-----2----3
		*/
		Indices.Append({ 0, 5, 1, 0, 4, 5});
		Indices.Append({ 1, 8, 2, 1, 5, 8});
		Indices.Append({ 2, 9, 3, 2, 8, 9});
	}
	else
	{
		/* Dilated back segment geometry: (ignore vertices 6-7 since they are the non-dilated vertices)
			6----7-----8----9
			|    |     |    |
			0---1,2---3,4---5
		*/
		check(Vertices.Num() >= 10);
		const uint32 BaseIndex = Vertices.Num() - 10;
		// Since we don't know if we're at the end point or not in the AddVerticesForRiverSplineStep function,
		// we end up adding a bunch of indices which link to the next set of vertices but they don't exist so we must trim them here.
		Indices.SetNum(Indices.Num() - 18);

		Indices.Append({ BaseIndex + 0, BaseIndex + 6, BaseIndex + 7, BaseIndex + 0, BaseIndex + 7, BaseIndex + 1 });
		Indices.Append({ BaseIndex + 1, BaseIndex + 7, BaseIndex + 8, BaseIndex + 1, BaseIndex + 8, BaseIndex + 4 });
		Indices.Append({ BaseIndex + 4, BaseIndex + 9, BaseIndex + 5, BaseIndex + 4, BaseIndex + 8, BaseIndex + 9 });
	}
}

void FWaterBodySceneProxy::GenerateRiverMesh(UWaterBodyRiverComponent* Component)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(GenerateRiverMesh);

	const UWaterSplineComponent* SplineComp = Component->GetWaterSpline();

	TArray<FDynamicMeshVertex> Vertices;
	TArray<uint32> Indices;

	// Add an extra point at the start to dilate starting edge
	AddTerminalVerticesForRiverSpline(ERiverBoundaryEdge::Start, Component, Vertices, Indices);

	TArray<double> Distances;
	TArray<FVector> Points;
	SplineComp->DivideSplineIntoPolylineRecursiveWithDistances(0.f, SplineComp->GetSplineLength(), ESplineCoordinateSpace::Local, FMath::Square(10.f), Points, Distances);
	
	for (double DistanceAlongSpline : Distances)
	{
		AddVerticesForRiverSplineStep(DistanceAlongSpline, Component, Vertices, Indices);
	}
	
	// Add an extra point at the end to dilate ending edge
	AddTerminalVerticesForRiverSpline(ERiverBoundaryEdge::End, Component, Vertices, Indices);


	FWaterBodyMeshSection* NewSection = new FWaterBodyMeshSection(GetScene().GetFeatureLevel());
	NewSection->IndexBuffer.Indices = MoveTemp(Indices);
	
	NewSection->VertexBuffers.InitFromDynamicVertex(&NewSection->VertexFactory, Vertices);

	InitResources(NewSection);

	Sections.Add(NewSection);
}

void FWaterBodySceneProxy::InitResources(FWaterBodyMeshSection* Section)
{
	check(Section != nullptr);
	BeginInitResource(&Section->VertexBuffers.PositionVertexBuffer);
	BeginInitResource(&Section->VertexBuffers.StaticMeshVertexBuffer);
	BeginInitResource(&Section->VertexBuffers.ColorVertexBuffer);
	BeginInitResource(&Section->IndexBuffer);
	BeginInitResource(&Section->VertexFactory);
}

FWaterBodySceneProxy::FWaterBodySceneProxy(UWaterBodyComponent* Component)
	: FPrimitiveSceneProxy(Component)
{
	if (Component == nullptr || Component->GetWaterSpline() == nullptr)
	{
		return;
	}

	switch(Component->GetWaterBodyType())
	{
		case EWaterBodyType::Lake:
			GenerateLakeMesh(CastChecked<UWaterBodyLakeComponent>(Component));
			break;
		case EWaterBodyType::Ocean:
			GenerateOceanMesh(CastChecked<UWaterBodyOceanComponent>(Component));
			break;
		case EWaterBodyType::River:
			GenerateRiverMesh(CastChecked<UWaterBodyRiverComponent>(Component));
			break;
		default:
			break;
	}

	if (UMaterialInstance* WaterInfoMaterial = Component->GetWaterInfoMaterialInstance())
	{
		Material = WaterInfoMaterial->GetRenderProxy();
	}
}

FWaterBodySceneProxy::~FWaterBodySceneProxy()
{
	for (FWaterBodyMeshSection* Section : Sections)
	{
		Section->VertexBuffers.PositionVertexBuffer.ReleaseResource();
		Section->VertexBuffers.StaticMeshVertexBuffer.ReleaseResource();
		Section->VertexBuffers.ColorVertexBuffer.ReleaseResource();
		Section->IndexBuffer.ReleaseResource();
		Section->VertexFactory.ReleaseResource();

		delete Section;
	}
}

void FWaterBodySceneProxy::GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const
{
	FMaterialRenderProxy* MaterialToUse = Material;
	if (!MaterialToUse)
	{
		FColoredMaterialRenderProxy* FallbackMaterial = new FColoredMaterialRenderProxy(
			GEngine->DebugMeshMaterial ? GEngine->DebugMeshMaterial->GetRenderProxy() : nullptr,
			FLinearColor(1.f, 1.f, 1.f));

		Collector.RegisterOneFrameMaterialProxy(FallbackMaterial);

		MaterialToUse = FallbackMaterial;
	}

	// If we are not in the waterinfo pass and the cvar is not set to show opaque bodies, we should be in wireframe
	const bool bWireframe = AllowDebugViewmodes() && !(bWithinWaterInfoPass) && (CVarWaterShowProxies.GetValueOnRenderThread() != 3);

	if (bWireframe)
	{
		FColoredMaterialRenderProxy* WireframeMaterialInstance= new FColoredMaterialRenderProxy(
			GEngine->WireframeMaterial ? GEngine->WireframeMaterial->GetRenderProxy() : NULL,
			FLinearColor(0, 0.5f, 1.f)
			);

		Collector.RegisterOneFrameMaterialProxy(WireframeMaterialInstance);
		MaterialToUse = WireframeMaterialInstance;
	}
	
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{
		for (const FWaterBodyMeshSection* Section : Sections)
		{
			FMeshBatch& Mesh = Collector.AllocateMesh();
			FMeshBatchElement& BatchElement = Mesh.Elements[0];
			BatchElement.IndexBuffer = &Section->IndexBuffer;
			Mesh.bWireframe = bWireframe;
			Mesh.VertexFactory = &Section->VertexFactory;
			Mesh.MaterialRenderProxy = MaterialToUse;

			bool bHasPrecomputedVolumetricLightmap;
			FMatrix PreviousLocalToWorld;
			int32 SingleCaptureIndex;
			bool bOutputVelocity;
			GetScene().GetPrimitiveUniformShaderParameters_RenderThread(GetPrimitiveSceneInfo(), bHasPrecomputedVolumetricLightmap, PreviousLocalToWorld, SingleCaptureIndex, bOutputVelocity);

			FDynamicPrimitiveUniformBuffer& DynamicPrimitiveUniformBuffer = Collector.AllocateOneFrameResource<FDynamicPrimitiveUniformBuffer>();
			DynamicPrimitiveUniformBuffer.Set(GetLocalToWorld(), PreviousLocalToWorld, GetBounds(), GetLocalBounds(), true, bHasPrecomputedVolumetricLightmap, DrawsVelocity(), bOutputVelocity); BatchElement.PrimitiveUniformBufferResource = &DynamicPrimitiveUniformBuffer.UniformBuffer;

			BatchElement.FirstIndex = 0;
			BatchElement.NumPrimitives = Section->IndexBuffer.Indices.Num() / 3;
			check(BatchElement.NumPrimitives != 0);
			BatchElement.MinVertexIndex = 0;
			BatchElement.MaxVertexIndex = Section->VertexBuffers.PositionVertexBuffer.GetNumVertices() - 1;
			Mesh.ReverseCulling = IsLocalToWorldDeterminantNegative();
			Mesh.Type = PT_TriangleList;
			Mesh.DepthPriorityGroup = bWireframe ? SDPG_Foreground : SDPG_World;
			Mesh.bCanApplyViewModeOverrides = false;
			Collector.AddMesh(ViewIndex, Mesh);
		}
	}
}

FPrimitiveViewRelevance FWaterBodySceneProxy::GetViewRelevance(const FSceneView* View) const
{
	FPrimitiveViewRelevance Result;
	Result.bDrawRelevance = IsShown(View);
	Result.bShadowRelevance = IsShadowCast(View);
	Result.bDynamicRelevance = true;
	Result.bRenderInMainPass = ShouldRenderInMainPass();
	Result.bUsesLightingChannels = GetLightingChannelMask() != GetDefaultLightingChannelMask();
	Result.bRenderCustomDepth = ShouldRenderCustomDepth();
	Result.bTranslucentSelfShadow = bCastVolumetricTranslucentShadow;
	Result.bVelocityRelevance = IsMovable();
	return Result;
}

SIZE_T FWaterBodySceneProxy::GetTypeHash() const
{
	static size_t UniquePtr;
	return reinterpret_cast<size_t>(&UniquePtr);
}

uint32 FWaterBodySceneProxy::GetMemoryFootprint() const 
{
	return (sizeof(*this) + GetAllocatedSize());
}

uint32 FWaterBodySceneProxy::GetAllocatedSize() const
{
	return FPrimitiveSceneProxy::GetAllocatedSize();
}

bool FWaterBodySceneProxy::IsShown(const FSceneView* View) const
{
	if (!bWithinWaterInfoPass)
	{
		const int32 ShowProxiesCVar = CVarWaterShowProxies.GetValueOnRenderThread();
		if ((ShowProxiesCVar == 1 && IsSelected()) || ShowProxiesCVar >= 2)
		{
			return true;
		}

		return false;
	}

	return FPrimitiveSceneProxy::IsShown(View);
}

void FWaterBodySceneProxy::SetWithinWaterInfoPass(bool bInWithinWaterInfoPass)
{
	bWithinWaterInfoPass = bInWithinWaterInfoPass;
}
