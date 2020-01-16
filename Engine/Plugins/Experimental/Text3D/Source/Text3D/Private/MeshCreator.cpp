// Copyright Epic Games, Inc. All Rights Reserved.


#include "MeshCreator.h"
#include "Contour.h"
#include "Part.h"
#include "Intersection.h"
#include "Data.h"
#include "ContourList.h"


THIRD_PARTY_INCLUDES_START
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#endif //PLATFORM_WINDOWS

extern "C" {
#include <tessellate.h>
}

#if PLATFORM_WINDOWS
#include "Windows/HideWindowsPlatformTypes.h"
#endif //PLATFORM_WINDOWS
THIRD_PARTY_INCLUDES_END


FMeshCreator::FMeshCreator(TSharedRef<TText3DMeshList> MeshesIn, const TSharedPtr<FData> DataIn)
	: Meshes(MeshesIn)
	, Data(DataIn)
{

}

void FMeshCreator::CreateMeshes(const TSharedPtr<FContourList> ContoursIn, const float Extrude, const float Bevel, const EText3DBevelType Type, const int32 HalfCircleSegments)
{
	Contours = ContoursIn;

	CreateFrontMesh();
	CreateBevelMesh(Bevel, Type, HalfCircleSegments);
#if !TEXT3D_WITH_INTERSECTION
	CreateExtrudeMesh(Extrude, Bevel);
#else
	if (Extrude > Bevel)
	{
		Data->SetCurrentMesh(EText3DMeshType::Extrude);
		const FVector2D Normal(1, 0);
		BevelLinear.BevelContours(Extrude - Bevel, 0, Normal, Normal, false, MarkedVertex);
	}

	Data->SetCurrentMesh(EText3DMeshType::Back);
	BevelLinear.CreateBackCap();
#endif
}

void FMeshCreator::SetFrontAndBevelTextureCoordinates(const float Bevel)
{
	for (int32 Type = 0; Type < static_cast<int32>(EText3DMeshType::TypeCount); Type++)
	{
		Data->SetCurrentMesh(static_cast<EText3DMeshType>(Type));
	}

	struct FBoundingBox
	{
		FBox2D Box;
		FVector2D Size;
	};

	TArray<FBoundingBox> BoundingBoxes;
	FVector2D MaxSize(0, 0);
	{
		EText3DMeshType MeshType = FMath::IsNearlyZero(Bevel) ? EText3DMeshType::Front : EText3DMeshType::Bevel;
		const FText3DMesh& Mesh = Meshes.Get()[static_cast<int32>(MeshType)];
		const TArray<int32>& GlyphStartVertices = Mesh.GlyphStartVertices;
		BoundingBoxes.AddUninitialized(GlyphStartVertices.Num() - 1);

		for (int32 GlyphIndex = 0; GlyphIndex < BoundingBoxes.Num(); GlyphIndex++)
		{
			FBoundingBox& BoundingBox = BoundingBoxes[GlyphIndex];
			FBox2D& Box = BoundingBox.Box;

			const int32 FirstIndex = GlyphStartVertices[GlyphIndex];
			const int32 LastIndex = GlyphStartVertices[GlyphIndex + 1];

			if (FirstIndex < LastIndex)
			{
				const FVector& Position = Mesh.Vertices[FirstIndex].Position;
				const FVector2D PositionFlat = {Position.Y, Position.Z};

				Box.Min = PositionFlat;
				Box.Max = PositionFlat;
			}

			for (int32 VertexIndex = FirstIndex + 1; VertexIndex < LastIndex; VertexIndex++)
			{
				const FDynamicMeshVertex& Vertex = Mesh.Vertices[VertexIndex];
				const FVector& Position = Vertex.Position;

				Box.Min.X = FMath::Min(Box.Min.X, Position.Y);
				Box.Min.Y = FMath::Min(Box.Min.Y, Position.Z);
				Box.Max.X = FMath::Max(Box.Max.X, Position.Y);
				Box.Max.Y = FMath::Max(Box.Max.Y, Position.Z);
			}

			FVector2D& Size = BoundingBox.Size;
			Size = Box.GetSize();

			MaxSize.X = FMath::Max(MaxSize.X, Size.X);
			MaxSize.Y = FMath::Max(MaxSize.Y, Size.Y);
		}
	}

	for (int32 GlyphIndex = 0; GlyphIndex < BoundingBoxes.Num(); GlyphIndex++)
	{
		const int32 NextGlyphIndex = GlyphIndex + 1;
		FBoundingBox& BoundingBox = BoundingBoxes[GlyphIndex];

		TSharedPtr<TText3DMeshList> MeshesLocal = Meshes;
		auto SetTextureCoordinates = [MeshesLocal, NextGlyphIndex, GlyphIndex, &BoundingBox, MaxSize](const EText3DMeshType Mesh)
		{
			FText3DMesh& CurrentMesh = (*MeshesLocal.Get())[static_cast<int32>(Mesh)];
			const TArray<int32>& GlyphStartVertices = CurrentMesh.GlyphStartVertices;

			if (NextGlyphIndex >= GlyphStartVertices.Num())
			{
				return;
			}

			for (int32 VertexIndex = GlyphStartVertices[GlyphIndex]; VertexIndex < GlyphStartVertices[NextGlyphIndex]; VertexIndex++)
			{
				FDynamicMeshVertex& Vertex = CurrentMesh.Vertices[VertexIndex];
				const FVector2D TextureCoordinate = (FVector2D(Vertex.Position.Y, Vertex.Position.Z) - BoundingBox.Box.Min) / MaxSize;

				for (int32 Index = 0; Index < MAX_STATIC_TEXCOORDS; Index++)
				{
					Vertex.TextureCoordinate[Index] = {TextureCoordinate.X, 1 - TextureCoordinate.Y};
				}
			}
		};

		SetTextureCoordinates(EText3DMeshType::Front);
		SetTextureCoordinates(EText3DMeshType::Bevel);
	}
}

void FMeshCreator::MirrorMeshes(const float Extrude, const float ScaleX)
{
	// When TEXT3D_WITH_INTERSECTION=1 the following functions will not do anything
	MirrorMesh(EText3DMeshType::Bevel, EText3DMeshType::Bevel, Extrude, ScaleX);
	MirrorMesh(EText3DMeshType::Front, EText3DMeshType::Back, Extrude, ScaleX);
}

// Using GLUtesselator to make triangulation of front mesh
void FMeshCreator::CreateFrontMesh()
{
	int32 VerticesCount = 0;

	for (const FContour& Contour : *Contours)
	{
		VerticesCount += Contour.Num();
	}

	const TUniquePtr<double[]> Vertices = MakeUnique<double[]>(SIZE_T(VerticesCount) * 2);
	const TUniquePtr<const double* []> ContoursIn = MakeUnique<const double* []>(SIZE_T(Contours->Num()) + 1);
	ContoursIn[0] = Vertices.Get();

	int32 Offset = 0;
	int32 ContourIndex = 0;
	for (const FContour& Contour : *Contours)
	{
		int32 PointIndex = 0;
		const FPart* const First = Contour[0];

		for (const FPart* Point = First; ; Point = Point->Next, PointIndex++)
		{
			double* const Vertex = Vertices.Get() + (Offset + PointIndex) * 2;
			const FVector2D Position = Point->Position;

			Vertex[0] = double(Position.X);
			Vertex[1] = double(Position.Y);

			if (Point == First->Prev)
			{
				break;
			}
		}

		Offset += Contour.Num();
		ContoursIn[SIZE_T(ContourIndex++ + 1)] = Vertices.Get() + Offset * 2;
	}

	double* VerticesOut = nullptr;
	int* IndicesOut = nullptr;
	int VerticesCountOut = 0;
	int TrianglesCountOut = 0;

	// @third party code - BEGIN GLUtesselator tesselate
	// Used to tesselate mesh framed with given contours
	tessellate(&VerticesOut, &VerticesCountOut, &IndicesOut, &TrianglesCountOut, static_cast<const double**>(ContoursIn.Get()), static_cast<const double**>(ContoursIn.Get() + ContourIndex + 1));
	//@third party code - END GLUtesselator tesselate


	auto DeleteData = [&VerticesOut, &IndicesOut]()
	{
		if (IndicesOut)
			free(IndicesOut);

		if (VerticesOut)
			free(VerticesOut);

		IndicesOut = nullptr;
		VerticesOut = nullptr;
	};


	if (VerticesCountOut != VerticesCount)
	{
		DeleteData();
		Contours->Empty();
		return;
	}


	Data->SetCurrentMesh(EText3DMeshType::Front);


	Data->ResetDoneExtrude();
	Data->SetMinBevelTarget();
	const int32 FirstAdded = Data->AddVertices(VerticesCount);

	for (const FContour& Contour : *Contours)
	{
		const FPart* const First = Contour[0];

		for (const FPart* Point = First; ; Point = Point->Next)
		{
			Data->AddVertex(Point, {1, 0}, {0, 0, -1});

			if (Point == First->Prev)
			{
				break;
			}
		}
	}


	Data->AddTriangles(TrianglesCountOut);

	for (int Index = 0; Index < TrianglesCountOut; Index++)
	{
		const int* const t = IndicesOut + Index * 3;
		Data->AddTriangle(FirstAdded + t[0], FirstAdded + t[1], FirstAdded + t[2]);
	}


	DeleteData();
}

void FMeshCreator::CreateBevelMesh(const float Bevel, const EText3DBevelType Type, const int32 HalfCircleSegments)
{
	if (FMath::IsNearlyZero(Bevel))
	{
		return;
	}

	Data->SetCurrentMesh(EText3DMeshType::Bevel);

	switch (Type)
	{
	case EText3DBevelType::Linear:
	{
		const FVector2D Normal = FVector2D(1, -1).GetSafeNormal();
		BevelLinear(Bevel, Bevel, Normal, Normal, false);
		break;
	}
	case EText3DBevelType::HalfCircle:
	{
		const float Step = HALF_PI / HalfCircleSegments;

		float CosCurr = 1.0f;
		float SinCurr = 0.0f;

		float CosNext = 0.0f;
		float SinNext = 0.0f;

		float ExtrudeLocalNext = 0.0f;
		float ExpandLocalNext = 0.0f;

		bool bSmoothNext = false;

		FVector2D NormalNext;
		FVector2D NormalEnd;

		auto MakeStep = [&SinNext, &CosNext, Step, &ExtrudeLocalNext, &ExpandLocalNext, Bevel, &CosCurr, &SinCurr, &NormalNext](int32 Index)
		{
			FMath::SinCos(&SinNext, &CosNext, Index * Step);

			ExtrudeLocalNext = Bevel * (CosCurr - CosNext);
			ExpandLocalNext = Bevel * (SinNext - SinCurr);

			NormalNext = FVector2D(ExtrudeLocalNext, -ExpandLocalNext).GetSafeNormal();
		};

		MakeStep(1);
		for (int32 Index = 0; Index < HalfCircleSegments; Index++)
		{
			CosCurr = CosNext;
			SinCurr = SinNext;

			const float ExtrudeLocal = ExtrudeLocalNext;
			const float ExpandLocal = ExpandLocalNext;

			const FVector2D Normal = NormalNext;
			FVector2D NormalStart;

			const bool bFirst = (Index == 0);
			const bool bLast = (Index == HalfCircleSegments - 1);

			const bool bSmooth = bSmoothNext;

			if (!bLast)
			{
				MakeStep(Index + 2);
				bSmoothNext = FVector2D::DotProduct(Normal, NormalNext) >= -FPart::CosMaxAngleSides;
			}

			NormalStart = bFirst ? Normal : (bSmooth ? NormalEnd : Normal);
			NormalEnd = bLast ? Normal : (bSmoothNext ? (Normal + NormalNext).GetSafeNormal() : Normal);

			BevelLinear(ExtrudeLocal, ExpandLocal, NormalStart, NormalEnd, bSmooth);
		}

		break;
	}

	default:
		break;
	}
}

void FMeshCreator::CreateExtrudeMesh(float Extrude, const float Bevel)
{
	if (Bevel >= Extrude / 2)
	{
		return;
	}

	Data->SetCurrentMesh(EText3DMeshType::Extrude);
	Extrude -= Bevel * 2.0f;
	Data->SetExtrude(Extrude);
	Data->SetExpand(0);

	const FVector2D Normal(1, 0);
	Data->SetNormals(Normal, Normal);

	for (FContour& Contour : *Contours)
	{
		for (FPart* const Part : Contour)
		{
			Part->ResetDoneExpand();
		}
	}


	TArray<float> TextureCoordinateVs;

	auto EdgeLength = [](const FPart* const Edge)
	{
		return (Edge->Next->Position - Edge->Position).Size();
	};

	for (FContour& Contour : *Contours)
	{
		// Compute TexCoord.V-s for each point
		TextureCoordinateVs.Reset(Contour.Num() - 1);
		const FPart* const First = Contour[0];
		TextureCoordinateVs.Add(EdgeLength(First));

		int32 Index = 1;
		for (const FPart* Edge = First->Next; Edge != First->Prev; Edge = Edge->Next)
		{
			TextureCoordinateVs.Add(TextureCoordinateVs[Index - 1] + EdgeLength(Edge));
			Index++;
		}


		const float ContourLength = TextureCoordinateVs.Last() + EdgeLength(Contour.Last());

		if (FMath::IsNearlyZero(ContourLength))
		{
			continue;
		}


		for (float& PointY : TextureCoordinateVs)
		{
			PointY /= ContourLength;
		}

		// Duplicate contour
		Data->SetMinBevelTarget();

		// First point in contour is processed separately
		{
			FPart* const Point = Contour[0];
			// It's set to sharp because we need 2 vertices with TexCoord.Y values 0 and 1 (for smooth points only one vertex is added)
			Point->bSmooth = false;
			EmptyPaths(Point);
			ExpandPointWithoutAddingVertices(Point);

			const FVector2D TexCoordPrev(0, 0);
			const FVector2D TexCoordCurr(0, 1);

			if (Point->bSmooth)
			{
				AddVertexSmooth(Point, TexCoordPrev);
				AddVertexSmooth(Point, TexCoordCurr);
			}
			else
			{
				AddVertexSharp(Point, Point->Prev, TexCoordPrev);
				AddVertexSharp(Point, Point, TexCoordCurr);
			}
		}

		Index = 1;
		for (FPart* Point = First->Next; Point != First; Point = Point->Next)
		{
			EmptyPaths(Point);
			ExpandPoint(Point, {0, 1 - TextureCoordinateVs[Index++ - 1]});
		}


		// Add extruded vertices
		Data->SetMaxBevelTarget();

		// Similarly to duplicating vertices, first point is processed separately
		{
			FPart* const Point = Contour[0];
			ExpandPointWithoutAddingVertices(Point);

			const FVector2D TexCoordPrev(1, 0);
			const FVector2D TexCoordCurr(1, 1);

			if (Point->bSmooth)
			{
				AddVertexSmooth(Point, TexCoordPrev);
				AddVertexSmooth(Point, TexCoordCurr);
			}
			else
			{
				AddVertexSharp(Point, Point->Prev, TexCoordPrev);
				AddVertexSharp(Point, Point, TexCoordCurr);
			}
		}

		Index = 1;
		for (FPart* Point = First->Next; Point != First; Point = Point->Next)
		{
			ExpandPoint(Point, {1, 1 - TextureCoordinateVs[Index++ - 1]});
		}


		for (FPart* const Edge : Contour)
		{
			Data->FillEdge(Edge, false);
		}
	}
}

void FMeshCreator::MirrorMesh(const EText3DMeshType TypeIn, const EText3DMeshType TypeOut, const float Extrude, const float ScaleX)
{
#if !TEXT3D_WITH_INTERSECTION
	const FText3DMesh& MeshIn = Meshes.Get()[static_cast<int32>(TypeIn)];
	FText3DMesh& MeshOut = Meshes.Get()[static_cast<int32>(TypeOut)];


	const TArray<FDynamicMeshVertex>& VerticesIn = MeshIn.Vertices;
	TArray<FDynamicMeshVertex>& VerticesOut = MeshOut.Vertices;

	const int32 VerticesInNum = VerticesIn.Num();
	const int32 VerticesOutNum = VerticesOut.Num();


	const TArray<int32>& IndicesIn = MeshIn.Indices;
	TArray<int32>& IndicesOut = MeshOut.Indices;

	const int32 IndicesInNum = IndicesIn.Num();
	const int32 IndicesOutNum = IndicesOut.Num();



	VerticesOut.AddUninitialized(VerticesInNum);

	for (int32 Index = 0; Index < VerticesInNum; Index++)
	{
		const FDynamicMeshVertex& Vertex = VerticesIn[Index];

		const FVector& Position = Vertex.Position;
		const FVector TangentX = Vertex.TangentX.ToFVector();
		const FVector TangentZ = Vertex.TangentZ.ToFVector();

		VerticesOut[VerticesOutNum + Index] = FDynamicMeshVertex({Extrude * ScaleX - Position.X, Position.Y, Position.Z}, {-TangentX.X, TangentX.Y, TangentX.Z}, {-TangentZ.X, TangentZ.Y, TangentZ.Z}, Vertex.TextureCoordinate[0], Vertex.Color);
	}


	IndicesOut.AddUninitialized(IndicesInNum);

	for (int32 Index = 0; Index < IndicesInNum / 3; Index++)
	{
		const int32 OldTriangle = Index * 3;
		const int32 NewTriangle = IndicesOutNum + OldTriangle;

		IndicesOut[NewTriangle + 0] = VerticesOutNum + IndicesIn[OldTriangle + 0];
		IndicesOut[NewTriangle + 1] = VerticesOutNum + IndicesIn[OldTriangle + 2];
		IndicesOut[NewTriangle + 2] = VerticesOutNum + IndicesIn[OldTriangle + 1];
	}
#endif
}

void FMeshCreator::BevelLinear(const float Extrude, const float Expand, FVector2D NormalStart, FVector2D NormalEnd, const bool bSmooth)
{
	Reset(Extrude, Expand, NormalStart, NormalEnd);

	if (!bSmooth)
	{
		DuplicateContourVertices();
	}

	if (Expand > 0)
	{
		BevelPartsWithIntersectingNormals();
	}

	BevelPartsWithoutIntersectingNormals();

	Data->IncreaseDoneExtrude();
}

void FMeshCreator::DuplicateContourVertices()
{
	Data->SetMinBevelTarget();

	for (FContour& Contour : *Contours)
	{
		for (FPart* const Point : Contour)
		{
			EmptyPaths(Point);
			// Duplicate points of contour (expansion with value 0)
			ExpandPoint(Point);
		}
	}
}

void FMeshCreator::Reset(const float Extrude, const float Expand, FVector2D NormalStart, FVector2D NormalEnd)
{
	Data->SetExtrude(Extrude);
	Data->SetExpand(Expand);

	Data->SetNormals(NormalStart, NormalEnd);
	Contours->Reset();
}

template<class T>
FIntersection& Upcast(T& Intersection)
{
	return static_cast<FIntersection&>(Intersection);
}

void FMeshCreator::BevelPartsWithIntersectingNormals()
{
#if TEXT3D_WITH_INTERSECTION
	for (Iteration = 0; /*Iteration < Iterations*/true; Iteration++)
	{
		// Copy list of contours (but not contours themselves) and iterate this list
		// because contours can be added or removed while beveling till intersections
		TArray<FContour*> ContoursCopy;

		for (FContour& Contour : *Contours)
		{
			ContoursCopy.Add(&Contour);
		}

		bool bIntersectionsExisted = false;

		for (FContour* const Contour : ContoursCopy)
		{
			if (!Contour->HasIntersections())
			{
				continue;
			}

			FIntersectionNear Near(Data, Contours, Contour);
			FIntersectionFar Far(Data, Contours, Contour);

			FIntersection& Closest = Near.GetValue() <= Far.GetValue() ? Upcast(Near) : Upcast(Far);
			const float Value = Closest.GetValue();

			// If intersection will happen further from front cap then is needed to bevel to, skip
			if (Value > Data->GetExpand())
			{
				Contour->DisableIntersections();
				continue;
			}

			Data->SetExpandTarget(Value);
			Closest.BevelTillThis();
			bIntersectionsExisted = true;
		}

		if (!bIntersectionsExisted)
		{
			break;
		}
	}
#endif
}

void FMeshCreator::BevelPartsWithoutIntersectingNormals()
{
	Data->SetMaxBevelTarget();
	const float MaxExpand = Data->GetExpand();

	for (FContour& Contour : *Contours)
	{
		for (FPart* const Point : Contour)
		{
			if (!FMath::IsNearlyEqual(Point->DoneExpand, MaxExpand) || FMath::IsNearlyZero(MaxExpand))
			{
				ExpandPoint(Point);
			}

			const float Delta = MaxExpand - Point->DoneExpand;

			Point->AvailableExpandNear -= Delta;
			Point->DecreaseExpandsFar(Delta);
		}

		for (FPart* const Edge : Contour)
		{
			Data->FillEdge(Edge, false);
		}
	}
}

void FMeshCreator::EmptyPaths(FPart* const Point) const
{
	Point->PathPrev.Empty();
	Point->PathNext.Empty();
}

void FMeshCreator::ExpandPoint(FPart* const Point, const FVector2D TextureCoordinates)
{
	ExpandPointWithoutAddingVertices(Point);

	if (Point->bSmooth)
	{
		AddVertexSmooth(Point, TextureCoordinates);
	}
	else
	{
		AddVertexSharp(Point, Point->Prev, TextureCoordinates);
		AddVertexSharp(Point, Point, TextureCoordinates);
	}
}

void FMeshCreator::ExpandPointWithoutAddingVertices(FPart* const Point) const
{
	Point->Position = Data->Expanded(Point);
	const int32 FirstAdded = Data->AddVertices(Point->bSmooth ? 1 : 2);

	Point->PathPrev.Add(FirstAdded);
	Point->PathNext.Add(Point->bSmooth ? FirstAdded : FirstAdded + 1);
}

void FMeshCreator::AddVertexSmooth(const FPart* const Point, const FVector2D TextureCoordinates)
{
	const FPart* const Curr = Point;
	const FPart* const Prev = Point->Prev;

	Data->AddVertex(Point, (Prev->TangentX + Curr->TangentX).GetSafeNormal(), (Data->ComputeTangentZ(Prev, Point->DoneExpand) + Data->ComputeTangentZ(Curr, Point->DoneExpand)).GetSafeNormal(), TextureCoordinates);
}

void FMeshCreator::AddVertexSharp(const FPart* const Point, const FPart* const Edge, const FVector2D TextureCoordinates)
{
	Data->AddVertex(Point, Edge->TangentX, Data->ComputeTangentZ(Edge, Point->DoneExpand).GetSafeNormal(), TextureCoordinates);
}
