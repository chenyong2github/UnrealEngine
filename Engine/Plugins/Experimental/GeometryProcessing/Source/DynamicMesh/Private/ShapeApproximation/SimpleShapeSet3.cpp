// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShapeApproximation/SimpleShapeSet3.h"

#include "MeshQueries.h"
#include "Intersection/ContainmentQueries3.h"



// FSimpleShapeElementKey identifies an element of a FSimpleShapeSet3d, via the Type
// of that element, and the Index into the per-element-type arrays.
struct FSimpleShapeElementKey
{
	// type of element
	ESimpleShapeType Type;
	// index of this element in per-type element lists inside a FSimpleShapeSet3d
	int32 Index;

	// volume of the element, used for sorting/etc
	double Volume;

	FSimpleShapeElementKey() = default;

	FSimpleShapeElementKey(ESimpleShapeType TypeIn, int32 IndexIn, double VolumeIn)
		: Type(TypeIn), Index(IndexIn), Volume(VolumeIn)
	{}
};



static void FilterContained(const FSimpleShapeSet3d& Geometry, const FSphereShape3d& Sphere, const TArray<FSimpleShapeElementKey>& Elements, int32 k, TArray<bool>& RemovedInOut)
{
	int32 N = Elements.Num();
	for (int32 j = k + 1; j < N; ++j)
	{
		if (RemovedInOut[j] == false)
		{
			bool bContained = false;
			int32 ElemIdx = Elements[j].Index;
			if (Elements[j].Type == ESimpleShapeType::Sphere)
			{
				bContained = UE::Geometry::IsInside<double>(Sphere.Sphere, Geometry.Spheres[ElemIdx].Sphere);
			}
			else if (Elements[j].Type == ESimpleShapeType::Box)
			{
				bContained = UE::Geometry::IsInside<double>(Sphere.Sphere, Geometry.Boxes[ElemIdx].Box);
			}
			else if (Elements[j].Type == ESimpleShapeType::Capsule)
			{
				bContained = UE::Geometry::IsInside<double>(Sphere.Sphere, Geometry.Capsules[ElemIdx].Capsule);
			}
			else if (Elements[j].Type == ESimpleShapeType::Convex)
			{
				bContained = UE::Geometry::IsInside(Sphere.Sphere, Geometry.Convexes[ElemIdx].Mesh.VerticesItr());
			}
			else
			{
				ensure(false);		// not implemented yet?
			}

			if (bContained)
			{
				RemovedInOut[j] = true;
			}
		}
	}
}




static void FilterContained(const FSimpleShapeSet3d& Geometry, const FCapsuleShape3d& Capsule, const TArray<FSimpleShapeElementKey>& Elements, int32 k, TArray<bool>& RemovedInOut)
{
	int32 N = Elements.Num();
	for (int32 j = k + 1; j < N; ++j)
	{
		if (RemovedInOut[j] == false)
		{
			bool bContained = false;
			int32 ElemIdx = Elements[j].Index;
			if (Elements[j].Type == ESimpleShapeType::Sphere)
			{
				bContained = UE::Geometry::IsInside<double>(Capsule.Capsule, Geometry.Spheres[ElemIdx].Sphere);
			}
			else if (Elements[j].Type == ESimpleShapeType::Box)
			{
				bContained = UE::Geometry::IsInside<double>(Capsule.Capsule, Geometry.Boxes[ElemIdx].Box);
			}
			else if (Elements[j].Type == ESimpleShapeType::Capsule)
			{
				bContained = UE::Geometry::IsInside<double>(Capsule.Capsule, Geometry.Capsules[ElemIdx].Capsule);
			}
			else if (Elements[j].Type == ESimpleShapeType::Convex)
			{
				bContained = UE::Geometry::IsInside(Capsule.Capsule, Geometry.Convexes[ElemIdx].Mesh.VerticesItr());
			}
			else
			{
				ensure(false);
			}

			if (bContained)
			{
				RemovedInOut[j] = true;
			}
		}
	}
}




static void FilterContained(const FSimpleShapeSet3d& Geometry, const FBoxShape3d& Box, const TArray<FSimpleShapeElementKey>& Elements, int32 k, TArray<bool>& RemovedInOut)
{
	int32 N = Elements.Num();
	for (int32 j = k + 1; j < N; ++j)
	{
		if (RemovedInOut[j] == false)
		{
			bool bContained = false;
			int32 ElemIdx = Elements[j].Index;
			if (Elements[j].Type == ESimpleShapeType::Sphere)
			{
				bContained = UE::Geometry::IsInside<double>(Box.Box, Geometry.Spheres[ElemIdx].Sphere);
			}
			else if (Elements[j].Type == ESimpleShapeType::Box)
			{
				bContained = UE::Geometry::IsInside<double>(Box.Box, Geometry.Boxes[ElemIdx].Box);
			}
			else if (Elements[j].Type == ESimpleShapeType::Capsule)
			{
				bContained = UE::Geometry::IsInside<double>(Box.Box, Geometry.Capsules[ElemIdx].Capsule);
			}
			else if (Elements[j].Type == ESimpleShapeType::Convex)
			{
				bContained = UE::Geometry::IsInside(Box.Box, Geometry.Convexes[ElemIdx].Mesh.VerticesItr());
			}
			else
			{
				ensure(false);
			}

			if (bContained)
			{
				RemovedInOut[j] = true;
			}
		}
	}
}





static void FilterContained(const FSimpleShapeSet3d& Geometry, const FConvexShape3d& Convex, const TArray<FSimpleShapeElementKey>& Elements, int32 k, TArray<bool>& RemovedInOut)
{
	TArray<FHalfspace3d> Planes;
	for (int32 tid : Convex.Mesh.TriangleIndicesItr())
	{
		FVector3d Normal, Centroid; double Area;
		Convex.Mesh.GetTriInfo(tid, Normal, Area, Centroid);
		Planes.Add(FHalfspace3d(Normal, Centroid));
	}

	int32 N = Elements.Num();
	for (int32 j = k + 1; j < N; ++j)
	{
		if (RemovedInOut[j] == false)
		{
			bool bContained = false;
			int32 ElemIdx = Elements[j].Index;
			if (Elements[j].Type == ESimpleShapeType::Sphere)
			{
				bContained = UE::Geometry::IsInsideHull<double>(Planes, Geometry.Spheres[ElemIdx].Sphere);
			}
			else if (Elements[j].Type == ESimpleShapeType::Box)
			{
				bContained = UE::Geometry::IsInsideHull<double>(Planes, Geometry.Boxes[ElemIdx].Box);
			}
			else if (Elements[j].Type == ESimpleShapeType::Capsule)
			{
				bContained = UE::Geometry::IsInsideHull<double>(Planes, Geometry.Capsules[ElemIdx].Capsule);
			}
			else if (Elements[j].Type == ESimpleShapeType::Convex)
			{
				bContained = UE::Geometry::IsInsideHull<double>(Planes, Geometry.Convexes[ElemIdx].Mesh.VerticesItr());
			}
			else
			{
				ensure(false);
			}

			if (bContained)
			{
				RemovedInOut[j] = true;
			}
		}
	}
}



static void GetElementsList(FSimpleShapeSet3d& GeometrySet, TArray<FSimpleShapeElementKey>& Elements)
{
	for (int32 k = 0; k < GeometrySet.Spheres.Num(); ++k)
	{
		Elements.Add(FSimpleShapeElementKey(ESimpleShapeType::Sphere, k, GeometrySet.Spheres[k].Sphere.Volume()));
	}
	for (int32 k = 0; k < GeometrySet.Boxes.Num(); ++k)
	{
		Elements.Add(FSimpleShapeElementKey(ESimpleShapeType::Box, k, GeometrySet.Boxes[k].Box.Volume()));
	}
	for (int32 k = 0; k < GeometrySet.Capsules.Num(); ++k)
	{
		Elements.Add(FSimpleShapeElementKey(ESimpleShapeType::Capsule, k, GeometrySet.Capsules[k].Capsule.Volume()));
	}
	for (int32 k = 0; k < GeometrySet.Convexes.Num(); ++k)
	{
		FVector2d VolArea = TMeshQueries<FDynamicMesh3>::GetVolumeArea(GeometrySet.Convexes[k].Mesh);
		Elements.Add(FSimpleShapeElementKey(ESimpleShapeType::Convex, k, VolArea.X));
	}
}

static void GetElementsSortedByDecreasing(FSimpleShapeSet3d& GeometrySet, TArray<FSimpleShapeElementKey>& Elements)
{
	GetElementsList(GeometrySet, Elements);
	// sort by decreasing volume
	Elements.Sort([](const FSimpleShapeElementKey& A, const FSimpleShapeElementKey& B) { return A.Volume > B.Volume; });
}


void FSimpleShapeSet3d::RemoveContainedGeometry()
{
	TArray<FSimpleShapeElementKey> Elements;
	GetElementsSortedByDecreasing(*this, Elements);

	int32 N = Elements.Num();
	TArray<bool> Removed;
	Removed.Init(false, N);

	// remove contained elements
	for (int32 k = 0; k < N; ++k)
	{
		if (Removed[k]) continue;

		ESimpleShapeType ElemType = Elements[k].Type;
		int32 ElemIdx = (int32)Elements[k].Index;

		if (ElemType == ESimpleShapeType::Sphere)
		{
			FilterContained(*this, Spheres[ElemIdx], Elements, k, Removed);
		}
		else if (ElemType == ESimpleShapeType::Capsule)
		{
			FilterContained(*this, Capsules[ElemIdx], Elements, k, Removed);
		}
		else if (ElemType == ESimpleShapeType::Box)
		{
			FilterContained(*this, Boxes[ElemIdx], Elements, k, Removed);
		}
		else if (ElemType == ESimpleShapeType::Convex)
		{
			FilterContained(*this, Convexes[ElemIdx], Elements, k, Removed);
		}
		else
		{
			ensure(false);
		}
	}


	// build a new shape set
	FSimpleShapeSet3d NewSet;
	for (int32 k = 0; k < N; ++k)
	{
		if (Removed[k] == false)
		{
			ESimpleShapeType ElemType = Elements[k].Type;
			int32 ElemIdx = Elements[k].Index;

			switch (ElemType)
			{
			case ESimpleShapeType::Sphere:
				NewSet.Spheres.Add(Spheres[ElemIdx]);
				break;
			case ESimpleShapeType::Box:
				NewSet.Boxes.Add(Boxes[ElemIdx]);
				break;
			case ESimpleShapeType::Capsule:
				NewSet.Capsules.Add(Capsules[ElemIdx]);
				break;
			case ESimpleShapeType::Convex:
				NewSet.Convexes.Add(Convexes[ElemIdx]);		// todo movetemp here...
				break;
			}
		}
	}

	// replace our lists with new set
	Spheres = MoveTemp(NewSet.Spheres);
	Boxes = MoveTemp(NewSet.Boxes);
	Capsules = MoveTemp(NewSet.Capsules);
	Convexes = MoveTemp(NewSet.Convexes);
}





void FSimpleShapeSet3d::FilterByVolume(int32 MaximumCount)
{
	TArray<FSimpleShapeElementKey> Elements;
	GetElementsSortedByDecreasing(*this, Elements);
	if (Elements.Num() <= MaximumCount)
	{
		return;
	}

	FSimpleShapeSet3d NewSet;
	for (int32 k = 0; k < MaximumCount; ++k)
	{
		ESimpleShapeType ElemType = Elements[k].Type;
		int32 ElemIdx = Elements[k].Index;

		switch (ElemType)
		{
		case ESimpleShapeType::Sphere:
			NewSet.Spheres.Add(Spheres[ElemIdx]);
			break;
		case ESimpleShapeType::Box:
			NewSet.Boxes.Add(Boxes[ElemIdx]);
			break;
		case ESimpleShapeType::Capsule:
			NewSet.Capsules.Add(Capsules[ElemIdx]);
			break;
		case ESimpleShapeType::Convex:
			NewSet.Convexes.Add(Convexes[ElemIdx]);		// todo movetemp here...
			break;
		}
	}

	Spheres = MoveTemp(NewSet.Spheres);
	Boxes = MoveTemp(NewSet.Boxes);
	Capsules = MoveTemp(NewSet.Capsules);
	Convexes = MoveTemp(NewSet.Convexes);
}