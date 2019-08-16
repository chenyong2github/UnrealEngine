// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "Chaos/HeightField.h"

namespace Chaos
{
template <typename T>
THeightField<T>::THeightField(TArray<T>&& Height, int32 NumRows, int32 NumCols, const TVector<T, 3>& Scale)
	: TImplicitObject<T, 3>(EImplicitObject::HasBoundingBox, ImplicitObjectType::HeightField)
{
	//For now just generate a triangle mesh
	const int32 NumCells = NumRows * NumCols;
	ensure(Height.Num() == NumCells);
	ensure(NumRows > 1);
	ensure(NumCols > 1);

	TArray<TVector<int32, 3>> Triangles;
	Triangles.Reserve(2 * (NumCols - 1)*(NumRows - 1));

	TParticles<T, 3> Particles;
	Particles.AddParticles(NumCells);
	int32 Idx = 0;
	for (int32 Row = 0; Row < NumRows; ++Row)
	{
		for (int32 Col = 0; Col < NumCols; ++Col)
		{
			if (Row < NumRows - 1 && Col < NumCols - 1)
			{
				//add two triangles question: do we care about tesselation control?
				Triangles.Emplace(Idx, Idx + 1, Idx + 1 + NumCols);
				Triangles.Emplace(Idx, Idx + 1 + NumCols, Idx + NumCols);
				//Triangles.Emplace(Idx, Idx + 1, Idx + NumCols);
				//Triangles.Emplace(Idx + 1, Idx + NumCols + 1, Idx + NumCols);
			}

			Particles.X(Idx) = TVector<T, 3>(Col, Row, Height[Idx]) * Scale;
			++Idx;
		}
	}

	MTriangleMeshImplicitObject = MakeUnique<TTriangleMeshImplicitObject<T>>(MoveTemp(Particles), MoveTemp(Triangles));
}

template <typename T>
bool THeightField<T>::Raycast(const TVector<T, 3>& StartPoint, const TVector<T, 3>& Dir, const T Length, const T Thickness, T& OutTime, TVector<T,3>& OutPosition, TVector<T,3>& OutNormal, int32& OutFaceIndex) const
{
	return MTriangleMeshImplicitObject->Raycast(StartPoint, Dir, Length, Thickness, OutTime, OutPosition, OutNormal, OutFaceIndex);
}

template <typename T>
bool THeightField<T>::Overlap(const TVector<T, 3>& Point, const T Thickness) const
{
	return MTriangleMeshImplicitObject->Overlap(Point, Thickness);
}


template <typename T>
bool THeightField<T>::OverlapGeom(const TImplicitObject<T, 3>& QueryGeom, const TRigidTransform<T, 3>& QueryTM, const T Thickness) const
{
	return MTriangleMeshImplicitObject->OverlapGeom(QueryGeom, QueryTM, Thickness);
}

template <typename T>
bool THeightField<T>::SweepGeom(const TImplicitObject<T, 3>& QueryGeom, const TRigidTransform<T, 3>& StartTM, const TVector<T, 3>& Dir, const T Length, T& OutTime, TVector<T, 3>& OutPosition, TVector<T, 3>& OutNormal, int32& OutFaceIndex, const T Thickness /* = 0 */) const
{
	return MTriangleMeshImplicitObject->SweepGeom(QueryGeom, StartTM, Dir, Length, OutTime, OutPosition, OutNormal, OutFaceIndex, Thickness);
}

template <typename T>
int32 THeightField<T>::FindMostOpposingFace(const TVector<T, 3>& Position, const TVector<T, 3>& UnitDir, int32 HintFaceIndex) const
{
	return MTriangleMeshImplicitObject->FindMostOpposingFace(Position, UnitDir, HintFaceIndex);
}

}

template class Chaos::THeightField<float>;