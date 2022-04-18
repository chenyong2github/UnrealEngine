// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryScript/PolyPathFunctions.h"

#include "Curve/CurveUtil.h"

#define LOCTEXT_NAMESPACE "UGeometryScriptLibrary_PolyPathFunctions"


int UGeometryScriptLibrary_PolyPathFunctions::GetPolyPathNumVertices(FGeometryScriptPolyPath PolyPath)
{
	return (PolyPath.Path.IsValid()) ? PolyPath.Path->Num() : 0;
}

int UGeometryScriptLibrary_PolyPathFunctions::GetPolyPathLastIndex(FGeometryScriptPolyPath PolyPath)
{
	return (PolyPath.Path.IsValid()) ? FMath::Max(PolyPath.Path->Num()-1,0) : 0;
}

FVector UGeometryScriptLibrary_PolyPathFunctions::GetPolyPathVertex(FGeometryScriptPolyPath PolyPath, int Index, bool& bIsValidIndex)
{
	bIsValidIndex = false;
	if (PolyPath.Path.IsValid() && Index >= 0 && Index < PolyPath.Path->Num())
	{
		bIsValidIndex = true;
		return (*PolyPath.Path)[Index];
	}
	return FVector::ZeroVector;
}

FVector UGeometryScriptLibrary_PolyPathFunctions::GetPolyPathTangent(FGeometryScriptPolyPath PolyPath, int Index, bool& bIsValidIndex)
{
	bIsValidIndex = false;
	if (PolyPath.Path.IsValid() && Index >= 0 && Index < PolyPath.Path->Num())
	{
		bIsValidIndex = true;
		FVector Tangent = UE::Geometry::CurveUtil::Tangent<double, FVector>(*PolyPath.Path, Index, PolyPath.bClosedLoop);
		return Tangent;
	}
	return FVector::ZeroVector;
}

double UGeometryScriptLibrary_PolyPathFunctions::GetPolyPathArcLength(FGeometryScriptPolyPath PolyPath)
{
	if (PolyPath.Path.IsValid())
	{
		return UE::Geometry::CurveUtil::ArcLength<double, FVector>(*PolyPath.Path, PolyPath.bClosedLoop);
	}
	return 0;
}

int32 UGeometryScriptLibrary_PolyPathFunctions::GetNearestVertexIndex(FGeometryScriptPolyPath PolyPath, FVector Point)
{
	if (PolyPath.Path.IsValid())
	{
		return UE::Geometry::CurveUtil::FindNearestIndex<double, FVector>(*PolyPath.Path, Point);
	}
	return -1;
}

UFUNCTION(BlueprintCallable, Category = "GeometryScript|PolyPath")
FGeometryScriptPolyPath UGeometryScriptLibrary_PolyPathFunctions::FlattenTo2DOnAxis(FGeometryScriptPolyPath PolyPath, EGeometryScriptAxis DropAxis)
{
	if (PolyPath.Path.IsValid())
	{
		int32 Keep0 = int32(DropAxis == EGeometryScriptAxis::X);
		int32 Keep1 = 1 + int32(DropAxis != EGeometryScriptAxis::Z);
		for (FVector& V : *PolyPath.Path)
		{
			V[0] = V[Keep0];
			V[1] = V[Keep1];
			V.Z = 0;
		}
	}
	return PolyPath;
}

void UGeometryScriptLibrary_PolyPathFunctions::ConvertPolyPathToArray(FGeometryScriptPolyPath PolyPath, TArray<FVector>& PathVertices)
{
	PathVertices.Reset();
	if (PolyPath.Path.IsValid())
	{
		PathVertices.Append(*PolyPath.Path);
	}
}

void UGeometryScriptLibrary_PolyPathFunctions::ConvertArrayToPolyPath(const TArray<FVector>& PathVertices, FGeometryScriptPolyPath& PolyPath)
{
	PolyPath.Reset();
	PolyPath.Path->Append(PathVertices);
}

void UGeometryScriptLibrary_PolyPathFunctions::ConvertPolyPathToArrayOfVector2D(FGeometryScriptPolyPath PolyPath, TArray<FVector2D>& PathVertices)
{
	PathVertices.Reset();
	if (PolyPath.Path.IsValid())
	{
		PathVertices.Reserve(PolyPath.Path->Num());
		for (const FVector& V : *PolyPath.Path)
		{
			PathVertices.Emplace(V.X, V.Y);
		}
	}
}

void UGeometryScriptLibrary_PolyPathFunctions::ConvertArrayOfVector2DToPolyPath(const TArray<FVector2D>& PathVertices, FGeometryScriptPolyPath& PolyPath)
{
	PolyPath.Reset();
	PolyPath.Path->Reserve(PathVertices.Num());
	for (const FVector2D& V : PathVertices)
	{
		PolyPath.Path->Emplace(V.X, V.Y, 0);
	}
}

void UGeometryScriptLibrary_PolyPathFunctions::ConvertSplineToPolyPath(const USplineComponent* Spline, FGeometryScriptPolyPath& PolyPath, FGeometryScriptSplineSamplingOptions SamplingOptions)
{
	PolyPath.Reset();
	if (Spline)
	{
		bool bIsLoop = Spline->IsClosedLoop();
		PolyPath.bClosedLoop = bIsLoop;
		if (SamplingOptions.SampleSpacing == EGeometryScriptSampleSpacing::ErrorTolerance)
		{
			float SquaredErrorTolerance = FMath::Max(KINDA_SMALL_NUMBER, SamplingOptions.ErrorTolerance * SamplingOptions.ErrorTolerance);
			Spline->ConvertSplineToPolyLine(SamplingOptions.CoordinateSpace, SquaredErrorTolerance, *PolyPath.Path);
			if (bIsLoop)
			{
				PolyPath.Path->Pop(); // delete the duplicate end-point for loops
			}
		}
		else
		{
			float Duration = Spline->Duration;
			
			bool bUseConstantVelocity = SamplingOptions.SampleSpacing == EGeometryScriptSampleSpacing::UniformDistance;
			int32 UseSamples = FMath::Max(2, SamplingOptions.NumSamples); // Always use at least 2 samples
			// In non-loops, we adjust DivNum so we exactly sample the end of the spline
			// In loops we don't sample the endpoint, by convention, as it's the same as the start
			float DivNum = float(UseSamples - (int32)!bIsLoop);
			PolyPath.Path->Reserve(UseSamples);
			for (int32 Idx = 0; Idx < UseSamples; Idx++)
			{
				float Time = Duration * ((float)Idx / DivNum);
				PolyPath.Path->Add(Spline->GetLocationAtTime(Time, SamplingOptions.CoordinateSpace, bUseConstantVelocity));
			}
		}
	}
}

TArray<FVector> UGeometryScriptLibrary_PolyPathFunctions::Conv_GeometryScriptPolyPathToArray(FGeometryScriptPolyPath PolyPath)
{
	TArray<FVector> PathVertices;
	ConvertPolyPathToArray(PolyPath, PathVertices);
	return PathVertices;
}

TArray<FVector2D> UGeometryScriptLibrary_PolyPathFunctions::Conv_GeometryScriptPolyPathToArrayOfVector2D(FGeometryScriptPolyPath PolyPath)
{
	TArray<FVector2D> PathVertices;
	ConvertPolyPathToArrayOfVector2D(PolyPath, PathVertices);
	return PathVertices;
}

FGeometryScriptPolyPath UGeometryScriptLibrary_PolyPathFunctions::Conv_ArrayToGeometryScriptPolyPath(const TArray<FVector>& PathVertices)
{
	FGeometryScriptPolyPath PolyPath;
	ConvertArrayToPolyPath(PathVertices, PolyPath);
	return PolyPath;
}

FGeometryScriptPolyPath UGeometryScriptLibrary_PolyPathFunctions::Conv_ArrayOfVector2DToGeometryScriptPolyPath(const TArray<FVector2D>& PathVertices)
{
	FGeometryScriptPolyPath PolyPath;
	ConvertArrayOfVector2DToPolyPath(PathVertices, PolyPath);
	return PolyPath;
}


#undef LOCTEXT_NAMESPACE