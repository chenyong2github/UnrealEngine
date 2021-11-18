// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/StaticArray.h"

class FGeometryCollection;



class CHAOS_API FGeometryCollectionProximityUtility
{
public:
	FGeometryCollectionProximityUtility(FGeometryCollection* InCollection);

	void UpdateProximity();

private:
	void PrepFaceData();
	void BinFaces();
	void FindContactingFaces();
	void ExtendFaceProximityToGeometry();

	void TransformVertices();
	void GenerateSurfaceNormals();
	void GenerateFaceToGeometry();
	int32 FindBestBin(const FVector3f& SurfaceNormal) const;
	void FindContactPairs(int32 Zenith, int32 Nadir);
	bool AreNormalsOpposite(const FVector3f& Normal0, const FVector3f& Normal1) const;
	bool AreFacesCoPlanar(int32 Idx0, int32 Idx1) const;
	bool DoFacesOverlap(int32 Idx0, int32 Idx1) const;
	static bool IdenticalTriangles(const TStaticArray<FVector2f, 3>& T0, const TStaticArray<FVector2f, 3>& T1);
	static bool TrianglesIntersect(const TStaticArray<FVector2f,3>& T0, const TStaticArray<FVector2f,3>& T1);
	static bool Cross(const TStaticArray<FVector2f,3>& Points, const FVector2f& B, const FVector2f& C, float Normal);

private:
	FGeometryCollection* Collection;

	int32 NumFaces;
	TArray<FVector3f> SurfaceNormals;
	TArray<int32> FaceToGeometry;
	TArray<FVector3f> TransformedVertices;

	constexpr static int32 NumBins = 20;
	TArray<TArray<int32>> Bins;
	TArray<FVector3f> BinNormals;

	TArray<TArray<int32>> FaceContacts;
};

