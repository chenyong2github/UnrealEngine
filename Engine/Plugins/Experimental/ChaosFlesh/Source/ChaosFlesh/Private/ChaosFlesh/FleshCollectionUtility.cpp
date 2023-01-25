// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	GeometryCollection->cpp: FGeometryCollection methods.
=============================================================================*/

#include "ChaosFlesh/FleshCollectionUtility.h"

#include "ChaosFlesh/ChaosFlesh.h"
#include "ChaosFlesh/FleshCollection.h"
#include "ChaosFlesh/GEO.h"
#include "ChaosFlesh/PB.h"


#include "GeometryCollection/TransformCollection.h"
#include "Misc/Paths.h"

#include <fstream>

namespace ChaosFlesh
{
	void GetTetFaces(
		const FIntVector4& Tet,
		FIntVector3& Face1,
		FIntVector3& Face2,
		FIntVector3& Face3,
		FIntVector3& Face4,
		const bool invert)
	{
		const int32 i = Tet[0];
		const int32 j = Tet[1];
		const int32 k = Tet[2];
		const int32 l = Tet[3];
		if (invert)
		{
			Face1 = { i, k, j };
			Face2 = { i, j, l };
			Face3 = { i, l, k };
			Face4 = { j, k, l };
		}
		else
		{
			Face1 = { i, j, k };
			Face2 = { i, l, j };
			Face3 = { i, k, l };
			Face4 = { j, l, k };
		}
	}

	int32 
	GetMin(const FIntVector3& V) 
	{
		return FMath::Min3(V[0], V[1], V[2]);
	}
	int32 
	GetMid(const FIntVector3& V)
	{
		const int32 X = V[0]; const int32 Y = V[1]; const int32 Z = V[2];
		const int32 XmY = X - Y;
		const int32 YmZ = Y - Z;
		const int32 XmZ = X - Z;
		return (XmY * YmZ > -1 ? Y : XmY * XmZ < 1 ? X : Z);
	}
	int32 
	GetMax(const FIntVector3& V)
	{
		return FMath::Max3(V[0], V[1], V[2]);
	}
	FIntVector3
	GetOrdered(const FIntVector3& V)
	{
		return FIntVector3(GetMin(V), GetMid(V), GetMax(V));
	}
	FIntVector4
	GetOrdered(const FIntVector4& V)
	{
		TArray<int32> VA = { V[0], V[1], V[2], V[3] };
		VA.Sort();
		return FIntVector4(VA[0], VA[1], VA[2], VA[3]);
	}

	void 
	GetSurfaceElements(
		const TArray<FIntVector4>& Tets,
		TArray<FIntVector3>& SurfaceElements,
		const bool KeepInteriorFaces,
		const bool InvertFaces)
	{
		FIntVector3 Faces[4];

		if (KeepInteriorFaces)
		{
			SurfaceElements.Reserve(Tets.Num() * 4);
			for (const FIntVector4& Tet : Tets)
			{
				GetTetFaces(Tet, Faces[0], Faces[1], Faces[2], Faces[3], InvertFaces);
				for (int i = 0; i < 4; i++)
					SurfaceElements.Add(Faces[i]);
			}
		}
		else
		{
			typedef TMap<int32, TPair<uint8, FIntVector3>> ZToCount;
			typedef TMap<int32, ZToCount> YToZ;
			typedef TMap<int32, YToZ> XToY;
			XToY CoordToCount;

			int32 Idx = -1;
			int32 Count = 0;
			for (const FIntVector4& Tet : Tets)
			{
				++Idx;
				if (!(Tet[0] != Tet[1] &&
					Tet[0] != Tet[2] &&
					Tet[0] != Tet[3] &&
					Tet[1] != Tet[2] &&
					Tet[1] != Tet[3] &&
					Tet[2] != Tet[3]))
				{
					UE_LOG(LogChaosFlesh, Display, TEXT("Skipping degenerate tet %d of %d."), Idx, Tets.Num());
					continue;
				}

				GetTetFaces(Tet, Faces[0], Faces[1], Faces[2], Faces[3], InvertFaces);
				for (int i = 0; i < 4; i++)
				{
					const FIntVector3 OFace = GetOrdered(Faces[i]);
					check(OFace[0] <= OFace[1] && OFace[1] <= OFace[2]);
					const int32 OA = OFace[0];
					const int32 OB = OFace[1];
					const int32 OC = OFace[2];

					auto& zc = CoordToCount.FindOrAdd(OFace[0]).FindOrAdd(OFace[1]);
					auto zcIt = zc.Find(OFace[2]);
					if (zcIt == nullptr)
					{
						zc.Add(OFace[2], TPair<uint8,FIntVector3>((uint8)1,Faces[i]) );
						// Increment our count of lone faces.
						Count++;
					}
					else
					{
						zcIt->Key++;
						// Since we're talking about shared faces, the only way we'd
						// get a face instanced more than twice is if we had a degenerate 
						// tet mesh.
						Count--;
					}
				}
			}

			size_t nonManifold = 0;
			SurfaceElements.Reserve(Count);
			for(auto &xyIt : CoordToCount)
			{
				const YToZ& yz = xyIt.Value;
				for(auto &yzIt : yz)
				{
					const ZToCount& zc = yzIt.Value;
					for(auto &zcIt : zc)
					{
						const uint8 FaceCount = zcIt.Value.Key;
						if (FaceCount == 1)
						{
							SurfaceElements.Add(zcIt.Value.Value);
						}
						else if (FaceCount > 2)
						{
							//UE_LOG(LogChaosFlesh, Display, TEXT("WARNING: Non-manifold tetrahedral mesh detected (face [%d, %d, %d] use count %d)."), zcIt->second.second[0], zcIt->second.second[1], zcIt->second.second[2], FaceCount);
							nonManifold++;
						}
					}
				}
			}
			if(nonManifold)
				UE_LOG(LogChaosFlesh, Display, TEXT("WARNING: Encountered %d non-manifold tetrahedral mesh faces."), nonManifold);
		}
	}

	TUniquePtr<FFleshCollection> ReadTetPBDeformableGeometryCollection(const FString& Filename)
	{
		TUniquePtr<FFleshCollection> Collection;
		UE_LOG(LogChaosFlesh, Display, TEXT("Reading Path %s"), *Filename);

		IO::DeformableGeometryCollectionReader Reader(Filename);
		if (!Reader.ReadPBScene())
			return Collection;

		TArray<FVector> *Vertices = nullptr;
		TArray<FVector> FrameVertices;
		TArray<FIntVector4> *Elements = nullptr;
		TArray<FIntVector3> SurfaceElements;

		TArray<IO::DeformableGeometryCollectionReader::TetMesh*> TetMeshes = Reader.GetTetMeshes();
		for (auto* It : TetMeshes)
		{
			Elements = reinterpret_cast<TArray<FIntVector4>*>(&It->Elements);
			Vertices = &It->Points;
			UE_LOG(LogChaosFlesh, Display, TEXT("Got %d tets."), Elements->Num());
			UE_LOG(LogChaosFlesh, Display, TEXT("Got %d rest points."), Vertices->Num());
			break;
		}
		TArray<FIntVector4> ElemTmp;
		if (!Elements)
		{
			Elements = &ElemTmp;
			UE_LOG(LogChaosFlesh, Display, TEXT("Got %d tets."), Elements->Num());
		}

		if (!Vertices || Vertices->Num() == 0)
		{
			TPair<int32, int32> FrameRange = Reader.ReadFrameRange();
			if (!Reader.ReadPoints(FrameRange.Key, FrameVertices))
			{
				return Collection;
			}
			Vertices = &FrameVertices;
			UE_LOG(LogChaosFlesh, Display, TEXT("Got %d points from frame %d."), Vertices->Num(), FrameRange.Key);
		}

		GetSurfaceElements(*Elements, SurfaceElements, false);
		UE_LOG(LogChaosFlesh, Display, TEXT("Got %d tris."), SurfaceElements.Num());

		Collection.Reset(FFleshCollection::NewFleshCollection(*Vertices, SurfaceElements, *Elements));
		return Collection;
	}


	TUniquePtr<FFleshCollection> ReadGEOFile(const FString& Filename)
	{
		TUniquePtr<FFleshCollection> Collection;
		UE_LOG(LogChaosFlesh, Display, TEXT("Reading Path %s"), *Filename);

		TMap<FString, int32> intVars;
		TMap<FString, TArray<int32>> intVectorVars;
		TMap<FString, TArray<float>> floatVectorVars;
		TMap<FString, TPair<TArray<std::string>, TArray<int32>>> indexedStringVars;
		if (!ReadGEO(std::string(TCHAR_TO_UTF8(*Filename)), intVars, intVectorVars, floatVectorVars, indexedStringVars))
		{
			UE_LOG(LogChaosFlesh, Display, TEXT("Failed to open GEO file: '%s'."), *Filename);
			return Collection;
		}

		TArray<FVector> Vertices;
		TArray<FIntVector4> Elements;
		TArray<FIntVector3> SurfaceElements;

		auto fvIt = floatVectorVars.Find("position");
		if (fvIt == nullptr)
			fvIt = floatVectorVars.Find("P");
		if (fvIt != nullptr)
		{
			const TArray<float>& coords = *fvIt;
			Vertices.Reserve(coords.Num() / 3);
			for (size_t i = 0; i < coords.Num(); i += 3)
			{
				FVector pt;
				for (size_t j = 0; j < 3; j++)
					pt[j] = coords[i + j];
				Vertices.Add(pt);
			}
		}
		UE_LOG(LogChaosFlesh, Display, TEXT("Got %d points."), Vertices.Num());

		auto ivIt = intVectorVars.Find("pointref.indices");
		if (ivIt != nullptr)
		{
			auto iIt = intVars.Find("Tetrahedron_run:startvertex");
			int32 startIndex = iIt == nullptr ? 0 : *iIt;

			iIt = intVars.Find("Tetrahedron_run:nprimitives");
			int32 numTets = iIt == nullptr ? -1 : *iIt;

			UE_LOG(LogChaosFlesh, Display, TEXT("Tet start index: %d num tets: %d"), startIndex, numTets);

			const TArray<int32>& indices = *ivIt;
			Elements.Reserve(indices.Num() / 4);
			size_t stopIndex = numTets != -1 ? startIndex + numTets * 4 : indices.Num();
			for (size_t i = startIndex; i < stopIndex; i += 4)
			{
				FIntVector4 tet;
				for (size_t j = 0; j < 4; j++)
					tet[j] = indices[i + j];
				Elements.Add(tet);
			}
			UE_LOG(LogChaosFlesh, Display, TEXT("Got %d tets."), Elements.Num());
		}

		GetSurfaceElements(Elements, SurfaceElements, false);
		UE_LOG(LogChaosFlesh, Display, TEXT("Got %d tris."), SurfaceElements.Num());

		Collection.Reset(FFleshCollection::NewFleshCollection(Vertices, SurfaceElements, Elements));
		return Collection;
	}

	TUniquePtr<FFleshCollection> ReadTetFile(const FString& Filename)
	{
		UE_LOG(LogChaosFlesh, Display, TEXT("Reading Path %s"), *Filename);
		TUniquePtr<FFleshCollection> Collection;

		TArray<FVector> Vertices;
		TArray<FIntVector4> Elements;  //UE::Math::TIntVector4<int32>
		TArray<FIntVector3> SurfaceElements;

		TArray<UE::Math::TVector<float>> FloatPos;
		TArray<Chaos::TVector<int32, 4>> Tets;
		if (IO::ReadStructure<4>(Filename, FloatPos, Tets))
		{
			Vertices.SetNum(FloatPos.Num());
			for (int32 i = 0; i < FloatPos.Num(); i++)
				for (int32 j = 0; j < 3; j++)
					Vertices[i][j] = FloatPos[i][j];

			Elements.SetNum(Tets.Num());
			for (int32 i = 0; i < Tets.Num(); i++)
				for (int32 j = 0; j < 4; j++)
					Elements[i][j] = Tets[i][j];

			UE_LOG(LogChaosFlesh, Display, TEXT("Got %d tets."), Elements.Num());
		}

		GetSurfaceElements(Elements, SurfaceElements, false);
		UE_LOG(LogChaosFlesh, Display, TEXT("Got %d tris."), SurfaceElements.Num());

		Collection.Reset(FFleshCollection::NewFleshCollection(Vertices, SurfaceElements, Elements));
		return Collection;
	}

	TUniquePtr<FFleshCollection> ImportTetFromFile(const FString& Filename)
	{
		if (FPaths::FileExists(Filename))
		{
			if (Filename.EndsWith(".tet") || Filename.EndsWith(".tet.gz"))
				return ReadTetFile(Filename);
			else if (Filename.EndsWith(".geo"))
				return ReadGEOFile(Filename);
			UE_LOG(LogChaosFlesh, Warning, TEXT("Unsupported file type: '%s'."), *Filename);
		}
		else
		{
			UE_LOG(LogChaosFlesh, Warning, TEXT("Unknown file path: '%s'."), *Filename);
		}
		return nullptr;
	}
}
