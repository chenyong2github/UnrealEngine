// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryCollection/GeometryCollectionProximityUtility.h"

#include "Async/ParallelFor.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionAlgo.h"

DEFINE_LOG_CATEGORY_STATIC(LogChaosProximity, Verbose, All);

FGeometryCollectionProximityUtility::FGeometryCollectionProximityUtility(FGeometryCollection* InCollection)
	: Collection(InCollection)
{
	check(Collection);
	
	// We quantize surface normals into 20 uniform bins on a unit sphere surface, ie an icosahedron
	BinNormals.SetNum(NumBins);
	
	BinNormals[0] = FVector3f(0.171535f, -0.793715f, 0.583717f);
	BinNormals[1] = FVector3f(0.627078f, -0.778267f, 0.034524f);
	BinNormals[2] = FVector3f(-0.491358f, -0.810104f, 0.319894f);
	BinNormals[3] = FVector3f(-0.445554f, -0.804788f, -0.392214f);
	BinNormals[4] = FVector3f(0.245658f, -0.785111f, -0.568669f);
	BinNormals[5] = FVector3f(0.984880f, -0.161432f, 0.062144f);
	BinNormals[6] = FVector3f(0.247864f, -0.186425f, 0.950708f);
	BinNormals[7] = FVector3f(-0.824669f, -0.212942f, 0.523975f);
	BinNormals[8] = FVector3f(-0.750546f, -0.204339f, -0.628411f);
	BinNormals[9] = FVector3f(0.367791f, -0.172505f, -0.913787f);
	BinNormals[10] = -BinNormals[0];
	BinNormals[11] = -BinNormals[1];
	BinNormals[12] = -BinNormals[2];
	BinNormals[13] = -BinNormals[3];
	BinNormals[14] = -BinNormals[4];
	BinNormals[15] = -BinNormals[5];
	BinNormals[16] = -BinNormals[6];
	BinNormals[17] = -BinNormals[7];
	BinNormals[18] = -BinNormals[8];
	BinNormals[19] = -BinNormals[9];
	
	NumFaces = Collection->NumElements(FGeometryCollection::FacesGroup);
	PrepFaceData();
}

void FGeometryCollectionProximityUtility::UpdateProximity()
{
	/*
	*	For each face, we generate the surface normal and put the normal into a set of bins that discretize the surface
	*	of the unit sphere. We know that any face can only contact other faces with opposite surface normal, which 
	*	means that we need only compare a face with the bin of faces on the antipode of the sphere. We determine contact first
	*	by testing if the normal is opposite, then if the faces are coplanar, then if the 2D triangles overlap.
	*	Once we have face pairs, we can extend this to geometry indices and form the proximity structure
	*	which is a map from geometry to geometry, one-to-many.
	*/

	BinFaces();
	FindContactingFaces();
	ExtendFaceProximityToGeometry();
}


void FGeometryCollectionProximityUtility::PrepFaceData()
{	
	GenerateFaceToGeometry();
	TransformVertices();
	GenerateSurfaceNormals();
}

void FGeometryCollectionProximityUtility::BinFaces()
{
	// We estimate that each bin will contain approximately 1/20th of the faces -- we double that to provide a decent buffer
	Bins.SetNum(NumBins);
	for (int32 BinIdx = 0; BinIdx < NumBins; ++BinIdx)
	{
		Bins[BinIdx].Reserve(2 * (NumFaces/NumBins));
	}

	for (int32 FaceIdx = 0; FaceIdx < NumFaces; ++FaceIdx)
	{
		if (SurfaceNormals[FaceIdx].IsUnit())
		{ 
			int32 BestFitBin = FindBestBin(SurfaceNormals[FaceIdx]);
			Bins[BestFitBin].Add(FaceIdx);
		}
	}
}

int32 FGeometryCollectionProximityUtility::FindBestBin(const FVector3f& SurfaceNormal) const
{
	// We select the bin with the highest alignment with the surface normal
	float BestAlignment = -1.0;
	int32 BestBin = INDEX_NONE;

	for (int32 BinIdx = 0; BinIdx < NumBins; ++BinIdx)
	{
		float Alignment = FVector3f::DotProduct(SurfaceNormal, BinNormals[BinIdx]);
		if (Alignment > BestAlignment)
		{
			BestAlignment = Alignment;
			BestBin = BinIdx;
		}
	}

	return BestBin;
}

void FGeometryCollectionProximityUtility::FindContactingFaces()
{
	FaceContacts.SetNum(NumFaces);
	
	// The bin hemispheres are symmetrical so we only need to iterate half of them.
	// We are comparing antipodal bins.
	ParallelFor((NumBins / 2), [this](int32 BinIdx)
		{ 
			FindContactPairs(BinIdx, (NumBins/2) + BinIdx); 
		});
}

void FGeometryCollectionProximityUtility::FindContactPairs(int32 Zenith, int32 Nadir)
{
	for (int32 ZenithIdx : Bins[Zenith])
	{
		for (int32 NadirIdx : Bins[Nadir])
		{
			// Are the faces parallel?
			if (AreNormalsOpposite(SurfaceNormals[ZenithIdx], SurfaceNormals[NadirIdx]))
			{
				// Are faces co-planar?
				if (AreFacesCoPlanar(ZenithIdx, NadirIdx))
				{
					// Do triangles overlap?
					if (DoFacesOverlap(ZenithIdx, NadirIdx))
					{
						// Add contacts to both faces
						FaceContacts[ZenithIdx].Add(NadirIdx);
						FaceContacts[NadirIdx].Add(ZenithIdx);
					}
				}
			}
		}
	}
}

bool FGeometryCollectionProximityUtility::AreNormalsOpposite(const FVector3f& Normal0, const FVector3f& Normal1) const
{
	return FVector3f::DotProduct(Normal0, Normal1) < (-1.0f + KINDA_SMALL_NUMBER);
}

bool FGeometryCollectionProximityUtility::AreFacesCoPlanar(int32 Idx0, int32 Idx1) const
{
	// Assumes that faces have already been determined to be parallel.

	const TManagedArray<FIntVector>& Indices = Collection->Indices;

	FVector3f SamplePoint = TransformedVertices[Indices[Idx0].X];
	FVector3f PlaneOrigin = TransformedVertices[Indices[Idx1].X];
	FVector3f PlaneNormal = SurfaceNormals[Idx1];

	return FMath::Abs(FVector3f::DotProduct((SamplePoint - PlaneOrigin), PlaneNormal)) < KINDA_SMALL_NUMBER;
}

bool FGeometryCollectionProximityUtility::DoFacesOverlap(int32 Idx0, int32 Idx1) const
{
	// Assumes that the faces are coplanar

	const TManagedArray<FIntVector>& Indices = Collection->Indices;
	
	// Project the first triangle into its normal plane
	FVector3f Basis0 = TransformedVertices[Indices[Idx0].Y] - TransformedVertices[Indices[Idx0].X];
	Basis0.Normalize();
	FVector3f Basis1 = FVector3f::CrossProduct(SurfaceNormals[Idx0], Basis0);
	Basis1.Normalize();

	FVector3f Origin = TransformedVertices[Indices[Idx0].X];
	
	TStaticArray<FVector2f,3> T0; 
	// T0[0] is the origin of the system
	T0[0] = FVector2f(0.f, 0.f);

	T0[1] = FVector2f(FVector3f::DotProduct(TransformedVertices[Indices[Idx0].Y] - Origin, Basis0), FVector3f::DotProduct(TransformedVertices[Indices[Idx0].Y] - Origin, Basis1));
	T0[2] = FVector2f(FVector3f::DotProduct(TransformedVertices[Indices[Idx0].Z] - Origin, Basis0), FVector3f::DotProduct(TransformedVertices[Indices[Idx0].Z] - Origin, Basis1));

	// Project the second triangle into these coordinates. We reverse the winding order to flip the normal.
	FVector3f Point0 = TransformedVertices[Indices[Idx1].Z] - Origin;
	FVector3f Point1 = TransformedVertices[Indices[Idx1].Y] - Origin;
	FVector3f Point2 = TransformedVertices[Indices[Idx1].X] - Origin;
	TStaticArray<FVector2f,3> T1;
	T1[0] = FVector2f(FVector3f::DotProduct(Point0, Basis0), FVector3f::DotProduct(Point0, Basis1));
	T1[1] = FVector2f(FVector3f::DotProduct(Point1, Basis0), FVector3f::DotProduct(Point1, Basis1));
	T1[2] = FVector2f(FVector3f::DotProduct(Point2, Basis0), FVector3f::DotProduct(Point2, Basis1));

	return IdenticalTriangles(T0, T1) || TrianglesIntersect(T0, T1);
}

bool FGeometryCollectionProximityUtility::IdenticalTriangles(const TStaticArray<FVector2f, 3>& T0, const TStaticArray<FVector2f, 3>& T1)
{
	int32 FirstMatch = 0;
	for (; FirstMatch < 3; ++FirstMatch)
	{
		if (T0[0] == T1[FirstMatch])
		{
			break;
		}
	}

	if (FirstMatch == 3) // No match
	{
		return false;
	}

	if (T0[1] != T1[(FirstMatch + 1) % 3])
	{
		return false;
	}

	if (T0[2] != T1[(FirstMatch + 2) % 3])
	{
		return false;
	}

	return true;
}

bool FGeometryCollectionProximityUtility::TrianglesIntersect(const TStaticArray<FVector2f,3>& T0, const TStaticArray<FVector2f,3>& T1)
{
	// Test if one of the triangles has a side with all of the other triangle's points on the outside.
	float Normal0 = (T0[1].X - T0[0].X) * (T0[2].Y - T0[0].Y) - (T0[1].Y - T0[0].Y) * (T0[2].X - T0[0].X);
	float Normal1 = (T1[1].X - T1[0].X) * (T1[2].Y - T1[0].Y) - (T1[1].Y - T1[0].Y) * (T1[2].X - T1[0].X);

	return !(Cross(T1, T0[0], T0[1], Normal0) ||
		Cross(T1, T0[1], T0[2], Normal0) ||
		Cross(T1, T0[2], T0[0], Normal0) ||
		Cross(T0, T1[0], T1[1], Normal1) ||
		Cross(T0, T1[1], T1[2], Normal1) ||
		Cross(T0, T1[1], T1[0], Normal1));
}

bool FGeometryCollectionProximityUtility::Cross(const TStaticArray<FVector2f,3>& Points, const FVector2f& B, const FVector2f& C, float Normal)
{
	float CyBy = C.Y - B.Y;
	float CxBx = C.X - B.X;
	const FVector2f& Pa = Points[0];
	const FVector2f& Pb = Points[1];
	const FVector2f& Pc = Points[2];

	return !(
		(((Pa.X - B.X) * CyBy - (Pa.Y - B.Y) * CxBx) * Normal < 0) ||
		(((Pb.X - B.X) * CyBy - (Pb.Y - B.Y) * CxBx) * Normal < 0) ||
		(((Pc.X - B.X) * CyBy - (Pc.Y - B.Y) * CxBx) * Normal < 0));
}

void FGeometryCollectionProximityUtility::GenerateSurfaceNormals()
{
	// Generate surface normal for each face
	SurfaceNormals.SetNum(NumFaces);
	ParallelFor(NumFaces, [this](int32 FaceIdx)
		{
			const TManagedArray<FIntVector>& Indices = Collection->Indices;

			FVector3f Edge0 = (TransformedVertices[Indices[FaceIdx].X] - TransformedVertices[Indices[FaceIdx].Y]);
			FVector3f Edge1 = (TransformedVertices[Indices[FaceIdx].Z] - TransformedVertices[Indices[FaceIdx].Y]);
			SurfaceNormals[FaceIdx] = FVector3f::CrossProduct(Edge0, Edge1);
			SurfaceNormals[FaceIdx].Normalize();
		});
}

void FGeometryCollectionProximityUtility::GenerateFaceToGeometry()
{
	// Create a map from face back to geometry
	const TManagedArray<int32>& FaceStart = Collection->FaceStart;
	const TManagedArray<int32>& FaceCount = Collection->FaceCount;

	FaceToGeometry.Init(INDEX_NONE, NumFaces);
	for (int32 GeometryIdx = 0; GeometryIdx < Collection->NumElements(FGeometryCollection::GeometryGroup); ++GeometryIdx)
	{
		for (int32 FaceOffset = 0; FaceOffset < FaceCount[GeometryIdx]; ++FaceOffset)
		{
			int32 FaceIdx = FaceStart[GeometryIdx] + FaceOffset;
			FaceToGeometry[FaceIdx] = GeometryIdx;
		}
	}
}

void FGeometryCollectionProximityUtility::TransformVertices()
{
	TransformedVertices.SetNum(Collection->NumElements(FGeometryCollection::VerticesGroup));

	TArray<FTransform> GlobalTransformArray;
	GeometryCollectionAlgo::GlobalMatrices(Collection->Transform, Collection->Parent, GlobalTransformArray);
	
	ParallelFor(Collection->NumElements(FGeometryCollection::VerticesGroup), [this, &GlobalTransformArray](int32 VertIdx)
		{
			const TManagedArray<int32>& BoneMap = Collection->BoneMap;
			const TManagedArray<FVector3f>& Vertex = Collection->Vertex;

			const FTransform& GlobalTransform = GlobalTransformArray[BoneMap[VertIdx]];
			TransformedVertices[VertIdx] = (FVector3f)GlobalTransform.TransformPosition(FVector(Vertex[VertIdx]));
		});

}

void FGeometryCollectionProximityUtility::ExtendFaceProximityToGeometry()
{
	if (!Collection->HasAttribute("Proximity", FGeometryCollection::GeometryGroup))
	{
		const FManagedArrayCollection::FConstructionParameters GeometryDependency(FGeometryCollection::GeometryGroup);
		Collection->AddAttribute<TSet<int32>>("Proximity", FGeometryCollection::GeometryGroup, GeometryDependency);
	}

	TManagedArray<TSet<int32>>& Proximity = Collection->GetAttribute<TSet<int32>>("Proximity", FGeometryCollection::GeometryGroup);

	for (int32 FaceIdx = 0; FaceIdx < NumFaces; ++FaceIdx)
	{
		TSet<int32>& CurrentContactList = Proximity[FaceToGeometry[FaceIdx]];
		for (int32 ContactFaceIdx : FaceContacts[FaceIdx])
		{
			CurrentContactList.Add(FaceToGeometry[ContactFaceIdx]);
		}
	}
}

