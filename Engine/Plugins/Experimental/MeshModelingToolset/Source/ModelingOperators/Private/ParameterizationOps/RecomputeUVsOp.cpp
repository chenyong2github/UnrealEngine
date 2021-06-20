// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParameterizationOps/RecomputeUVsOp.h"

#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Selections/MeshConnectedComponents.h"
#include "Parameterization/MeshLocalParam.h"
#include "Parameterization/DynamicMeshUVEditor.h"

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;


void FRecomputeUVsOp::NormalizeUVAreas(const FDynamicMesh3& Mesh, FDynamicMeshUVOverlay* Overlay, float GlobalScale)
{
	FMeshConnectedComponents UVComponents(&Mesh);
	UVComponents.FindConnectedTriangles([&](int32 Triangle0, int32 Triangle1) {
		return Overlay->AreTrianglesConnected(Triangle0, Triangle1);
	});

	// TODO ParallelFor
	for (FMeshConnectedComponents::FComponent& Component : UVComponents)
	{
		const TArray<int>& Triangles = Component.Indices;
		TSet<int> UVElements;
		UVElements.Reserve(Triangles.Num() * 3);
		double AreaUV = 0;
		double Area3D = 0;
		FVector3d Triangle3D[3];
		FVector2d TriangleUV[3];
		FAxisAlignedBox2d BoundsUV = FAxisAlignedBox2d::Empty();

		for (int tid : Triangles)
		{
			FIndex3i TriElements = Overlay->GetTriangle(tid);
			if (!TriElements.Contains(FDynamicMesh3::InvalidID))
			{
				for (int j = 0; j < 3; ++j)
				{
					TriangleUV[j] = FVector2d(Overlay->GetElement(TriElements[j]));
					BoundsUV.Contain(TriangleUV[j]);
					Triangle3D[j] = Mesh.GetVertex(Overlay->GetParentVertex(TriElements[j]));
					UVElements.Add(TriElements[j]);
				}
				AreaUV += VectorUtil::Area(TriangleUV[0], TriangleUV[1], TriangleUV[2]);
				Area3D += VectorUtil::Area(Triangle3D[0], Triangle3D[1], Triangle3D[2]);
			}
		}
		
		double LinearScale = (AreaUV > 0.00001) ? ( FMathd::Sqrt(Area3D) / FMathd::Sqrt(AreaUV)) : 1.0;
		LinearScale = LinearScale * GlobalScale;
		FVector2d ComponentOrigin = BoundsUV.Center();

		for (int elemid : UVElements)
		{
			FVector2d UV = FVector2d(Overlay->GetElement(elemid));
			UV = (UV - ComponentOrigin) * LinearScale + ComponentOrigin;
			Overlay->SetElement(elemid, FVector2f(UV));
		}
	}
}


void FRecomputeUVsOp::CalculateResult(FProgressCancel* Progress)
{
	if (!InputMesh.IsValid())
	{
		return;
	}

	ResultMesh = MakeUnique<FDynamicMesh3>(*InputMesh);

	FDynamicMesh3* BaseMesh = ResultMesh.Get();
	FDynamicMeshUVEditor UVEditor(BaseMesh, UVLayer, true);
	FDynamicMeshUVOverlay* UseOverlay = UVEditor.GetOverlay();
	if (ensure(UseOverlay != nullptr) == false)
	{
		return;
	}

	if (Progress && Progress->Cancelled())
	{
		return;
	}

	// find group-connected-components
	FMeshConnectedComponents ConnectedComponents(ResultMesh.Get());
	if (IslandMode == ERecomputeUVsIslandMode::PolyGroups)
	{
		if (InputGroups != nullptr)
		{
			ConnectedComponents.FindConnectedTriangles([this](int32 CurTri, int32 NbrTri) {
				return InputGroups->GetTriangleGroup(CurTri) == InputGroups->GetTriangleGroup(NbrTri);
			});
		}
		else
		{
			ConnectedComponents.FindConnectedTriangles([this](int32 CurTri, int32 NbrTri) {
				return ResultMesh->GetTriangleGroup(CurTri) == ResultMesh->GetTriangleGroup(NbrTri);
			});
		}
	}
	else
	{
		ConnectedComponents.FindConnectedTriangles([&](int32 Triangle0, int32 Triangle1) {
			return UseOverlay->AreTrianglesConnected(Triangle0, Triangle1);
		});
	}

	if (Progress && Progress->Cancelled())
	{
		return;
	}

	// TODO: the solves here could be done in parallel if we pre-allocated the island element IDs

	int32 NumComponents = ConnectedComponents.Num();
	int32 SuccessCount = 0;
	for (int32 k = 0; k < NumComponents; ++k)
	{
		const TArray<int32>& ComponentTris = ConnectedComponents[k].Indices;

		bool bComputedUVs = false;
		switch (UnwrapType)
		{
		case ERecomputeUVsUnwrapType::ExpMap:
			bComputedUVs = UVEditor.SetTriangleUVsFromExpMap(ComponentTris);
			break;

		case ERecomputeUVsUnwrapType::ConformalFreeBoundary:
			bComputedUVs = UVEditor.SetTriangleUVsFromFreeBoundaryConformal(ComponentTris);
			break;
		}

		if (bComputedUVs)
		{
			SuccessCount++;
		}

		if (Progress && Progress->Cancelled())
		{
			return;
		}
	}


	if (bNormalizeAreas)
	{
		// todo should be a DynamicUVEditor function?
		NormalizeUVAreas(*ResultMesh, UseOverlay, AreaScaling);
	}

	if (bPackUVs)
	{
		UVEditor.QuickPack(PackingTextureResolution, PackingGutterWidth);
	}

}