// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "Chaos/HeightField.h"
#include "Chaos/Convex.h"
#include "Chaos/Box.h"
#include "Chaos/GJK.h"
#include "Chaos/ImplicitObjectScaled.h"
#include "Chaos/Capsule.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"

namespace Chaos
{

	template<typename T>
	class THeightfieldRaycastVisitor
	{
	public:

		THeightfieldRaycastVisitor(const typename THeightField<T>::FDataType* InData, const TVector<T, 3>& InStart, const TVector<T, 3>& InDir, const T InThickness)
			: OutTime(TNumericLimits<T>::Max())
			, OutFaceIndex(INDEX_NONE)
			, GeomData(InData)
			, Start(InStart)
			, Dir(InDir)
			, Thickness(InThickness)
		{}

		enum class ERaycastType
		{
			Raycast,
			Sweep
		};

		template<ERaycastType SQType>
		bool Visit(int32 Payload, T& CurrentLength)
		{
			const int32 SubX = Payload % (GeomData->NumCols - 1);
			const int32 SubY = Payload / (GeomData->NumCols - 1);

			const int32 FullIndex = Payload + SubY;

			const T Radius = Thickness + SMALL_NUMBER;
			const T Radius2 = Radius * Radius;
			bool bIntersection = false;

			auto TestTriangle = [&](int32 FaceIndex, const TVector<T, 3>& A, const TVector<T, 3>& B, const TVector<T, 3>& C) -> bool
			{
				const TVector<T, 3> AB = B - A;
				const TVector<T, 3> AC = C - A;

				TVector<T, 3> Normal = TVector<T, 3>::CrossProduct(AB, AC);
				const T Len2 = Normal.SafeNormalize();

				if(!ensure(Len2 > SMALL_NUMBER))
				{
					// Bad triangle, co-linear points or very thin
					return true;
				}

				const TPlane<T, 3> TrianglePlane(A, Normal);

				TVector<T, 3> ResultPosition(0);
				TVector<T, 3> ResultNormal(0);
				T Time = TNumericLimits<T>::Max();
				int32 DummyFaceIndex = INDEX_NONE;

				if(TrianglePlane.Raycast(Start, Dir, CurrentLength, Thickness, Time, ResultPosition, ResultNormal, DummyFaceIndex))
				{
					if(Time == 0)
					{
						// Initial overlap
						const TVector<T, 3> ClosestPtOnTri = FindClosestPointOnTriangle(TrianglePlane, A, B, C, Start);
						const T DistToTriangle2 = (Start - ClosestPtOnTri).SizeSquared();
						if(DistToTriangle2 <= Radius2)
						{
							OutTime = 0;
							OutPosition = ClosestPtOnTri;
							OutNormal = Normal;
							OutFaceIndex = FaceIndex;
							return false;
						}
					}
					else
					{
						const TVector<T, 3> ClosestPtOnTri = FindClosestPointOnTriangle(ResultPosition, A, B, C, ResultPosition);
						const T DistToTriangle2 = (ResultPosition - ClosestPtOnTri).SizeSquared();
						bIntersection = DistToTriangle2 <= SMALL_NUMBER;
					}
				}

				if(SQType == ERaycastType::Sweep && !bIntersection)
				{
					//sphere is not immediately touching the triangle, but it could start intersecting the perimeter as it sweeps by
					TVector<T, 3> BorderPositions[3];
					TVector<T, 3> BorderNormals[3];
					T BorderTimes[3];
					bool bBorderIntersections[3];

					const TCapsule<T> ABCapsule(A, B, Thickness);
					bBorderIntersections[0] = ABCapsule.Raycast(Start, Dir, CurrentLength, 0, BorderTimes[0], BorderPositions[0], BorderNormals[0], DummyFaceIndex);

					const TCapsule<T> BCCapsule(B, C, Thickness);
					bBorderIntersections[1] = BCCapsule.Raycast(Start, Dir, CurrentLength, 0, BorderTimes[1], BorderPositions[1], BorderNormals[1], DummyFaceIndex);

					const TCapsule<T> ACCapsule(A, C, Thickness);
					bBorderIntersections[2] = ACCapsule.Raycast(Start, Dir, CurrentLength, 0, BorderTimes[2], BorderPositions[2], BorderNormals[2], DummyFaceIndex);

					int32 MinBorderIdx = INDEX_NONE;
					T MinBorderTime = 0;

					for(int32 BorderIdx = 0; BorderIdx < 3; ++BorderIdx)
					{
						if(bBorderIntersections[BorderIdx])
						{
							if(!bIntersection || BorderTimes[BorderIdx] < MinBorderTime)
							{
								MinBorderTime = BorderTimes[BorderIdx];
								MinBorderIdx = BorderIdx;
								bIntersection = true;
							}
						}
					}

					if(MinBorderIdx != INDEX_NONE)
					{
						ResultNormal = BorderNormals[MinBorderIdx];
						ResultPosition = BorderPositions[MinBorderIdx] - ResultNormal * Thickness;

						if(Time == 0)
						{
							//we were initially overlapping with triangle plane so no normal was given. Compute it now
							TVector<T, 3> TmpNormal;
							const T SignedDistance = TrianglePlane.PhiWithNormal(Start, TmpNormal);
							ResultNormal = SignedDistance >= 0 ? TmpNormal : -TmpNormal;
						}

						Time = MinBorderTime;
					}
				}

				if(bIntersection)
				{
					if(Time < OutTime)
					{
						OutPosition = ResultPosition;
						OutNormal = ResultNormal;
						OutTime = Time;
						OutFaceIndex = FaceIndex;
						CurrentLength = Time;
					}
				}

				return true;
			};

			TVector<T, 3> Points[4];
			GeomData->GetPointsScaled(FullIndex, Points);

			// Test both triangles that are in this cell, as we could hit both in any order
			TestTriangle(FullIndex * 2, Points[0], Points[1], Points[2]);
			TestTriangle(FullIndex * 2 + 1, Points[2], Points[1], Points[3]);

			return OutTime > 0;
		}

		bool VisitRaycast(int32 Payload, T& CurLength)
		{
			return Visit<ERaycastType::Raycast>(Payload, CurLength);
		}

		bool VisitSweep(int32 Payload, T& CurLength)
		{
			return Visit<ERaycastType::Sweep>(Payload, CurLength);
		}

		T OutTime;
		TVector<T, 3> OutPosition;
		TVector<T, 3> OutNormal;
		int32 OutFaceIndex;

	private:

		const typename THeightField<T>::FDataType* GeomData;

		TVector<T, 3> Start;
		TVector<T, 3> Dir;
		T Thickness;
	};

	template<typename T>
	class THeightfieldSweepVisitor
	{
	public:

		THeightfieldSweepVisitor(const typename THeightField<T>::FDataType* InData, const TImplicitObject<T, 3>& InQueryGeom, const TRigidTransform<T, 3>& InStartTM, const TVector<T, 3>& InDir, const T InThickness)
			: OutTime(TNumericLimits<T>::Max())
			, OutFaceIndex(INDEX_NONE)
			, HfData(InData)
			, StartTM(InStartTM)
			, OtherGeom(InQueryGeom)
			, Dir(InDir)
			, Thickness(InThickness)
		{}

		bool VisitSweep(int32 Payload, T& CurrentLength)
		{
			const int32 SubX = Payload % (HfData->NumCols - 1);
			const int32 SubY = Payload / (HfData->NumCols - 1);

			const int32 FullIndex = Payload + SubY;

			auto TestTriangle = [&](int32 FaceIndex, const TVector<T, 3>& A, const TVector<T, 3>& B, const TVector<T, 3>& C) -> bool
			{
				if(OutTime == 0)
				{
					return false;
				}

				// This isn't great as we build a convex on-the-fly but we have no other
				// trimesh support. Update later as better trimesh collisions come online
				TParticles<T, 3> TriParticles;
				TriParticles.AddParticles(3);
				TriParticles.X(0) = A;
				TriParticles.X(1) = B;
				TriParticles.X(2) = C;

				TConvex<T, 3> Convex(TriParticles);

				T Time;
				TVector<T, 3> HitPosition;
				TVector<T, 3> HitNormal;
				if(GJKRaycast<T>(Convex, OtherGeom, StartTM, Dir, CurrentLength, Time, HitPosition, HitNormal, Thickness))
				{
					if(Time < OutTime)
					{
						OutNormal = HitNormal;
						OutPosition = HitPosition;
						OutTime = Time;
						CurrentLength = Time;
						OutFaceIndex = FaceIndex;

						if(Time == 0)
						{
							return false;
						}
					}
				}

				return true;
			};

			TVector<T, 3> Points[4];
			HfData->GetPointsScaled(FullIndex, Points);

			TestTriangle(FullIndex * 2, Points[0], Points[1], Points[2]);
			TestTriangle(FullIndex * 2 + 1, Points[2], Points[1], Points[3]);

			return OutTime != 0;
		}

		T OutTime;
		TVector<T, 3> OutPosition;
		TVector<T, 3> OutNormal;
		int32 OutFaceIndex;

	private:

		const typename THeightField<T>::FDataType* HfData;
		const TRigidTransform<T, 3> StartTM;
		const TImplicitObject<T, 3>& OtherGeom;
		const TVector<T, 3>& Dir;
		const T Thickness;

	};

	template<typename HeightfieldT, typename BufferType>
	void BuildGeomData(TArrayView<BufferType> BufferView, int32 NumRows, int32 NumCols, const TVector<HeightfieldT, 3>& InScale, TUniqueFunction<HeightfieldT(const BufferType)> ToRealFunc, typename THeightField<HeightfieldT>::FDataType& OutData, TBox<HeightfieldT, 3>& OutBounds)
	{
		using FDataType = typename THeightField<HeightfieldT>::FDataType;

		using RealType = typename FDataType::RealType;

		const int32 NumCells = NumRows * NumCols;
		ensure(BufferView.Num() == NumCells);
		ensure(NumRows > 1);
		ensure(NumCols > 1);

		// Populate data.
		const int32 NumHeights = BufferView.Num();
		OutData.Heights.SetNum(NumHeights);

		OutData.NumRows = NumRows;
		OutData.NumCols = NumCols;
		OutData.MinValue = ToRealFunc(BufferView[0]);
		OutData.MaxValue = ToRealFunc(BufferView[0]);
		OutData.Scale = InScale;

		for(int32 HeightIndex = 1; HeightIndex < NumHeights; ++HeightIndex)
		{
			const RealType CurrHeight = ToRealFunc(BufferView[HeightIndex]);

			if(CurrHeight > OutData.MaxValue)
			{
				OutData.MaxValue = CurrHeight;
			}
			else if(CurrHeight < OutData.MinValue)
			{
				OutData.MinValue = CurrHeight;
			}
		}

		OutData.Range = OutData.MaxValue - OutData.MinValue;
		OutData.HeightPerUnit = OutData.Range / FDataType::StorageRange;

		for(int32 HeightIndex = 0; HeightIndex < NumHeights; ++HeightIndex)
		{
			OutData.Heights[HeightIndex] = static_cast<typename FDataType::StorageType>((ToRealFunc(BufferView[HeightIndex]) - OutData.MinValue) / OutData.HeightPerUnit);

			int32 X = HeightIndex % (NumCols);
			int32 Y = HeightIndex / (NumCols);
			TVector<HeightfieldT, 3> Position(RealType(X), RealType(Y), OutData.MinValue + OutData.Heights[HeightIndex] * OutData.HeightPerUnit);
			if(HeightIndex == 0)
			{
				OutBounds = TBox<HeightfieldT, 3>(Position * InScale, Position * InScale);
			}
			else
			{
				OutBounds.GrowToInclude(Position * InScale);
			}
		}
		OutBounds.Thicken(KINDA_SMALL_NUMBER);
	}

	template<typename HeightfieldT, typename BufferType>
	void EditGeomData(TArrayView<BufferType> BufferView, int32 InBeginRow, int32 InBeginCol, int32 NumRows, int32 NumCols, TUniqueFunction<HeightfieldT(const BufferType)> ToRealFunc, typename THeightField<HeightfieldT>::FDataType& OutData, TBox<HeightfieldT, 3>& OutBounds)
	{
		using FDataType = typename THeightField<HeightfieldT>::FDataType;
		using RealType = typename FDataType::RealType;

		RealType MinValue = TNumericLimits<HeightfieldT>::Max();
		RealType MaxValue = TNumericLimits<HeightfieldT>::Min();

		for(BufferType& Value : BufferView)
		{
			MinValue = FMath::Min(MinValue, ToRealFunc(Value));
			MaxValue = FMath::Max(MaxValue, ToRealFunc(Value));
		}

		const int32 EndRow = InBeginRow + NumRows;
		const int32 EndCol = InBeginCol + NumCols;

		// If our range now falls outside of the original ranges we need to resample the whole heightfield to perform the edit.
		// Here we resample everything outside of the edit and update our ranges
		const bool bNeedsResample = MinValue < OutData.MinValue || MaxValue > OutData.MaxValue;
		if(bNeedsResample)
		{
			const RealType NewMin = FMath::Min(MinValue, OutData.MinValue);
			const RealType NewMax = FMath::Max(MaxValue, OutData.MaxValue);
			const RealType NewRange = NewMax - NewMin;
			const RealType NewHeightPerUnit = NewRange / FDataType::StorageRange;

			for(int32 RowIdx = 0; RowIdx < OutData.NumRows; ++RowIdx)
			{
				for(int32 ColIdx = 0; ColIdx < OutData.NumCols; ++ColIdx)
				{
					const int32 HeightIndex = RowIdx * OutData.NumCols + ColIdx;

					if(RowIdx >= InBeginRow && RowIdx < EndRow &&
						ColIdx >= InBeginCol && ColIdx < EndCol)
					{
						// From the new set
						const int32 NewSetIndex = (RowIdx - InBeginRow) * NumCols + (ColIdx - InBeginCol);
						OutData.Heights[HeightIndex] = static_cast<typename FDataType::StorageType>((ToRealFunc(BufferView[NewSetIndex]) - NewMin) / NewHeightPerUnit);
					}
					else
					{
						// Resample existing
						const RealType ExpandedHeight = OutData.MinValue + OutData.Heights[HeightIndex] * OutData.HeightPerUnit;
						OutData.Heights[HeightIndex] = static_cast<typename FDataType::StorageType>((ExpandedHeight - NewMin) / NewHeightPerUnit);
					}

					int32 X = HeightIndex % (OutData.NumCols);
					int32 Y = HeightIndex / (OutData.NumCols);
					TVector<HeightfieldT, 3> Position(RealType(X), RealType(Y), NewMin + OutData.Heights[HeightIndex] * NewHeightPerUnit);
					if(HeightIndex == 0)
					{
						OutBounds = TBox<HeightfieldT, 3>(Position, Position);
					}
					else
					{
						OutBounds.GrowToInclude(Position);
					}
				}
			}

			OutBounds.Thicken(KINDA_SMALL_NUMBER);

			OutData.MinValue = NewMin;
			OutData.MaxValue = NewMax;
			OutData.HeightPerUnit = NewHeightPerUnit;
			OutData.Range = NewRange;
		}
		else
		{
			// No resample, just push new heights into the data
			for(int32 RowIdx = InBeginRow; RowIdx < EndRow; ++RowIdx)
			{
				for(int32 ColIdx = InBeginCol; ColIdx < EndCol; ++ColIdx)
				{
					const int32 HeightIndex = RowIdx * NumCols + ColIdx;
					const int32 NewSetIndex = (RowIdx - InBeginRow) * NumCols + (ColIdx - InBeginCol);
					OutData.Heights[HeightIndex] = static_cast<typename FDataType::StorageType>((ToRealFunc(BufferView[NewSetIndex]) - OutData.MinValue) / OutData.HeightPerUnit);
				}
			}
		}
	}

	template <typename T>
	THeightField<T>::THeightField(TArray<T>&& Height, int32 NumRows, int32 NumCols, const TVector<T, 3>& InScale)
		: TImplicitObject<T, 3>(EImplicitObject::HasBoundingBox, ImplicitObjectType::HeightField)
	{
		BuildGeomData<T, T>(MakeArrayView(Height), NumRows, NumCols, TVector<T, 3>(1), [](const T InVal) {return InVal; }, GeomData, LocalBounds);
		CalcBounds();
		SetScale(InScale);
	}

	template<typename T>
	Chaos::THeightField<T>::THeightField(TArrayView<const uint16> InHeights, int32 InNumRows, int32 InNumCols, const TVector<T, 3>& InScale)
		: TImplicitObject<T, 3>(EImplicitObject::HasBoundingBox, ImplicitObjectType::HeightField)
	{
		TUniqueFunction<T(const uint16)> ConversionFunc = [](const T InVal) -> float
		{
			return (float)((int32)InVal - 32768);
		};

		BuildGeomData<T, const uint16>(InHeights, InNumRows, InNumCols, TVector<T, 3>(1), MoveTemp(ConversionFunc), GeomData, LocalBounds);
		CalcBounds();
		SetScale(InScale);
	}

	template<typename T>
	void Chaos::THeightField<T>::EditHeights(TArrayView<const uint16> InHeights, int32 InBeginRow, int32 InBeginCol, int32 InNumRows, int32 InNumCols)
	{
		const int32 NumExpectedValues = InNumRows * InNumCols;
		const int32 EndRow = InBeginRow + InNumRows - 1;
		const int32 EndCol = InBeginCol + InNumCols - 1;

		if(ensure(InHeights.Num() == NumExpectedValues && InBeginRow >= 0 && InBeginCol >= 0 && EndRow < GeomData.NumRows && EndCol < GeomData.NumCols))
		{
			TUniqueFunction<T(const uint16)> ConversionFunc = [](const T InVal) -> float
			{
				return (float)((int32)InVal - 32768);
			};

			EditGeomData<T, const uint16>(InHeights, InBeginRow, InBeginCol, InNumRows, InNumCols, MoveTemp(ConversionFunc), GeomData, LocalBounds);
		}
	}

	template<typename T>
	void Chaos::THeightField<T>::EditHeights(TArrayView<T> InHeights, int32 InBeginRow, int32 InBeginCol, int32 InNumRows, int32 InNumCols)
	{
		const int32 NumExpectedValues = InNumRows * InNumCols;
		const int32 EndRow = InBeginRow + InNumRows - 1;
		const int32 EndCol = InBeginCol + InNumCols - 1;

		if(ensure(InHeights.Num() == NumExpectedValues && InBeginRow >= 0 && InBeginCol >= 0 && EndRow < GeomData.NumRows && EndCol < GeomData.NumCols))
		{
			TUniqueFunction<T(T)> ConversionFunc = [](const T InVal) -> float
			{
				return InVal;
			};

			EditGeomData<T, T>(InHeights, InBeginRow, InBeginCol, InNumRows, InNumCols, MoveTemp(ConversionFunc), GeomData, LocalBounds);
		}
	}

	template<typename T>
	bool Chaos::THeightField<T>::GetCellBounds2D(const TVector<int32, 2> InCoord, FBounds2D& OutBounds, const TVector<T, 2>& InInflate /*= {0}*/) const
	{
		if(FlatGrid.IsValid(InCoord))
		{
			const TBox<T, 3>& Bound = GeomData.CellBounds[InCoord[1] * (GeomData.NumCols - 1) + InCoord[0]];
			OutBounds.Min = {Bound.Min()[0], Bound.Min()[1]};
			OutBounds.Max = {Bound.Max()[0], Bound.Max()[1]};
			OutBounds.Min -= InInflate;
			OutBounds.Max += InInflate;

			return true;
		}

		return false;
	}

	template<typename T>
	bool Chaos::THeightField<T>::GetCellBounds3D(const TVector<int32, 2> InCoord, TVector<T, 3>& OutMin, TVector<T, 3>& OutMax, const TVector<T, 3>& InInflate /*= {0}*/) const
	{
		if(FlatGrid.IsValid(InCoord))
		{
			const TBox<T, 3>& Bound = GeomData.CellBounds[InCoord[1] * (GeomData.NumCols - 1) + InCoord[0]];
			OutMin = Bound.Min() - InInflate;
			OutMax = Bound.Max() + InInflate;

			return true;
		}

		return false;
	}

	template<typename T>
	bool Chaos::THeightField<T>::GetCellBounds2DScaled(const TVector<int32, 2> InCoord, FBounds2D& OutBounds, const TVector<T, 2>& InInflate /*= {0}*/) const
	{
		if(FlatGrid.IsValid(InCoord))
		{
			const TBox<T, 3>& Bound = GeomData.CellBounds[InCoord[1] * (GeomData.NumCols - 1) + InCoord[0]];
			OutBounds.Min = {Bound.Min()[0], Bound.Min()[1]};
			OutBounds.Max = {Bound.Max()[0], Bound.Max()[1]};
			OutBounds.Min -= InInflate;
			OutBounds.Max += InInflate;
			const TVector<float, 2> Scale2D = TVector<T, 2>(GeomData.Scale[0], GeomData.Scale[1]);
			OutBounds.Min *= Scale2D;
			OutBounds.Max *= Scale2D;
			return true;
		}

		return false;
	}

	template<typename T>
	bool Chaos::THeightField<T>::GetCellBounds3DScaled(const TVector<int32, 2> InCoord, TVector<T, 3>& OutMin, TVector<T, 3>& OutMax, const TVector<T, 3>& InInflate /*= {0}*/) const
	{
		if(FlatGrid.IsValid(InCoord))
		{
			const TBox<T, 3>& Bound = GeomData.CellBounds[InCoord[1] * (GeomData.NumCols - 1) + InCoord[0]];
			OutMin = Bound.Min() - InInflate;
			OutMax = Bound.Max() + InInflate;
			OutMin *= GeomData.Scale;
			OutMax *= GeomData.Scale;
			return true;
		}

		return false;
	}

	template<typename T>
	bool Chaos::THeightField<T>::CalcCellBounds3D(const TVector<int32, 2> InCoord, TVector<T, 3>& OutMin, TVector<T, 3>& OutMax, const TVector<T, 3>& InInflate /*= {0}*/) const
	{
		if(FlatGrid.IsValid(InCoord))
		{
			int32 Index = InCoord[1] * (GeomData.NumCols - 1) + InCoord[0] + InCoord[1];
			static TVector<T, 3> Points[4];
			GeomData.GetPoints(Index, Points);

			OutMin = OutMax = Points[0];

			for(int32 PointIndex = 1; PointIndex < 4; ++PointIndex)
			{
				const TVector<T, 3>& Point = Points[PointIndex];
				OutMin = TVector<T, 3>(FGenericPlatformMath::Min(OutMin[0], Point[0]), FGenericPlatformMath::Min(OutMin[1], Point[1]), FGenericPlatformMath::Min(OutMin[2], Point[2]));
				OutMax = TVector<T, 3>(FGenericPlatformMath::Max(OutMax[0], Point[0]), FGenericPlatformMath::Max(OutMax[1], Point[1]), FGenericPlatformMath::Max(OutMax[2], Point[2]));
			}

			OutMin -= InInflate;
			OutMax += InInflate;

			return true;
		}

		return false;
	}


	template <typename T>
	bool THeightField<T>::GridCast(const TVector<T, 3>& StartPoint, const TVector<T, 3>& Dir, const T Length, THeightfieldRaycastVisitor<T>& Visitor) const
	{
		T CurrentLength = Length;
		TVector<T, 2> ClippedFlatRayStart;
		TVector<T, 2> ClippedFlatRayEnd;

		if(GetFlatBounds().ClipLine(StartPoint, StartPoint + Dir * Length, ClippedFlatRayStart, ClippedFlatRayEnd))
		{
			// The line is now valid and is entirely enclosed by the bounds (and thus, the grid)
			if(FMath::Abs((ClippedFlatRayEnd - ClippedFlatRayStart).SizeSquared()) < SMALL_NUMBER)
			{
				// This is a cast down the Z axis, handle in a simpler way as this is the common case
				const int32 QueryX = (int32)(ClippedFlatRayStart[0] / GeomData.Scale[0]);
				const int32 QueryY = (int32)(ClippedFlatRayStart[1] / GeomData.Scale[1]);
				const TVector<int32, 2> Cell = FlatGrid.ClampIndex({QueryX, QueryY});

				Visitor.VisitRaycast(Cell[1] * (GeomData.NumCols - 1) + Cell[0], CurrentLength);
			}
			else
			{
				// Rasterize the line over the grid
				const TVector<T, 2> Scale2D(GeomData.Scale[0], GeomData.Scale[1]);
				TVector<int32, 2> StartCell = FlatGrid.Cell(ClippedFlatRayStart/Scale2D);
				TVector<int32, 2> EndCell = FlatGrid.Cell(ClippedFlatRayEnd/Scale2D);

				// Boundaries might push us one cell over
				StartCell = FlatGrid.ClampIndex(StartCell);
				EndCell = FlatGrid.ClampIndex(EndCell);

				int32 DeltaX = FMath::Abs(EndCell[0] - StartCell[0]);
				int32 DeltaY = -FMath::Abs(EndCell[1] - StartCell[1]);
				int32 DirX = StartCell[0] < EndCell[0] ? 1 : -1;
				int32 DirY = StartCell[1] < EndCell[1] ? 1 : -1;
				int32 Error = DeltaX + DeltaY;

				if(StartCell == EndCell)
				{
					// Visit the selected cell, in this case no need to rasterize because we never
					// leave the initial cell
					Visitor.VisitRaycast(StartCell[1] * (GeomData.NumCols - 1) + StartCell[0], CurrentLength);
				}
				else
				{
					Visitor.VisitRaycast(StartCell[1] * (GeomData.NumCols - 1) + StartCell[0], CurrentLength);

					do 
					{
						const int32 DoubleError = Error * 2;

						if(DoubleError >= DeltaY)
						{
							Error += DeltaY;
							StartCell[0] += DirX;
						}

						if(DoubleError <= DeltaX)
						{
							Error += DeltaX;
							StartCell[1] += DirY;
						}

						// Visit the selected cell
						bool bContinue = Visitor.VisitRaycast(StartCell[1] * (GeomData.NumCols - 1) + StartCell[0], CurrentLength);
						if(!bContinue)
						{
							return true;
						}
					} while(StartCell != EndCell);
				}
			}
		}

		return false;
	}

	struct F2DGridSet
	{
		F2DGridSet(TVector<int32, 2> Size)
			: NumX(Size[0])
			, NumY(Size[1])
		{
			int32 BitsNeeded = NumX * NumY;
			int32 BytesNeeded = 1 + (BitsNeeded) / 8;
			Data = MakeUnique<uint8[]>(BytesNeeded);
			FMemory::Memzero(Data.Get(), BytesNeeded);
		}

		bool Contains(const TVector<int32, 2>& Coordinate)
		{
			int32 Idx = Coordinate[1] * NumX + Coordinate[0];
			int32 ByteIdx = Idx / 8;
			int32 BitIdx = Idx % 8;
			bool bContains = (Data[ByteIdx] >> BitIdx) & 0x1;
			return bContains;
		}

		void Add(const TVector<int32, 2>& Coordinate)
		{
			int32 Idx = Coordinate[1] * NumX + Coordinate[0];
			int32 ByteIdx = Idx / 8;
			int32 BitIdx = Idx % 8;
			uint8 Mask = 1 << BitIdx;
			Data[ByteIdx] |= Mask;
		}

	private:
		int32 NumX;
		int32 NumY;
		TUniquePtr<uint8[]> Data;
	};

	template<typename T>
	template<typename SQVisitor>
	bool THeightField<T>::GridSweep(const TVector<T, 3>& StartPoint, const TVector<T, 3>& Dir, const T Length, const TVector<T, 2> InHalfExtents, SQVisitor& Visitor) const
	{
		T CurrentLength = Length;

		FBounds2D InflatedBounds = GetFlatBounds();
		InflatedBounds.Min -= InHalfExtents;
		InflatedBounds.Max += InHalfExtents;

		TVector<T, 3> HalfExtents3D(InHalfExtents[0], InHalfExtents[1], InHalfExtents[1]);

		const TVector<T, 3> EndPoint = StartPoint + Dir * Length;
		const TVector<T, 2> Start2D(StartPoint[0], StartPoint[1]);
		const TVector<T, 2> End2D(EndPoint[0], EndPoint[1]);
		const TVector<T, 2> Scale2D(GeomData.Scale[0], GeomData.Scale[1]);

		TVector<T, 2> ClippedStart;
		TVector<T, 2> ClippedEnd;

		if(InflatedBounds.ClipLine(StartPoint, StartPoint + Dir * Length, ClippedStart, ClippedEnd))
		{
			// Rasterize the line over the grid
			TVector<int32, 2> StartCell = FlatGrid.Cell(ClippedStart / Scale2D);
			TVector<int32, 2> EndCell = FlatGrid.Cell(ClippedEnd / Scale2D);

			// Boundaries might push us one cell over
			StartCell = FlatGrid.ClampIndex(StartCell);
			EndCell = FlatGrid.ClampIndex(EndCell);

			const int32 DeltaX = FMath::Abs(EndCell[0] - StartCell[0]);
			const int32 DeltaY = -FMath::Abs(EndCell[1] - StartCell[1]);
			const bool bSameCell = DeltaX == 0 && DeltaY == 0;

			const int32 DirX = StartCell[0] < EndCell[0] ? 1 : -1;
			const int32 DirY = StartCell[1] < EndCell[1] ? 1 : -1;
			int32 Error = DeltaX + DeltaY;
			const TVector<int32, 2> ThickenDir = FMath::Abs(DeltaX) > FMath::Abs(DeltaY) ? TVector<int32, 2>(0, 1) : TVector<int32, 2>(1, 0);

			struct FQueueEntry
			{
				TVector<int32, 2> Index;
				T ToI;
			};

			// Tracking data for cells to query (similar to bounding volume approach)
			F2DGridSet Seen(FlatGrid.Counts());
			TArray<FQueueEntry> Queue;
			Queue.Add({StartCell, -1});
			Seen.Add(StartCell);

			// Data for fast box cast
			TVector<T, 3> Min, Max, HitPoint;
			float ToI;
			bool bParallel[3];
			TVector<T, 3> InvDir;

			T InvCurrentLength = 1 / CurrentLength;
			for(int Axis = 0; Axis < 3; ++Axis)
			{
				bParallel[Axis] = Dir[Axis] == 0;
				InvDir[Axis] = bParallel[Axis] ? 0 : 1 / Dir[Axis];
			}

			int32 QueueIndex = 0;
			while(QueueIndex < Queue.Num())
			{
				// Copy so we don't lost the entry through reallocs
				FQueueEntry CellCoord = Queue[QueueIndex++];

				if(CellCoord.ToI > CurrentLength)
				{
					continue;
				}

				if(bSameCell)
				{
					// Test the current cell
					bool bContinue = Visitor.VisitSweep(CellCoord.Index[1] * (GeomData.NumCols - 1) + CellCoord.Index[0], CurrentLength);
					
					if(!bContinue)
					{
						return true;
					}

					// Flatten out a double loop and skip the centre cell
					// to search cells immediately adjacent to the current cell
					static const TVector<int32, 2> Neighbors[] =
					{
						{-1, -1}, {0, -1}, {1, -1},
						{-1, 0}, {1, 0},
						{-1, 1}, {0, 1}, {1, 1}
					};

					for(const TVector<int32, 2>& Neighbor : Neighbors)
					{
						TVector<int32, 2> NeighCoord = CellCoord.Index + Neighbor;

						FBounds2D CellBounds;
						if(GetCellBounds3DScaled(NeighCoord, Min, Max, HalfExtents3D) && !Seen.Contains(NeighCoord))
						{
							if(TBox<T, 3>::RaycastFast(Min, Max, StartPoint, Dir, InvDir, bParallel, CurrentLength, InvCurrentLength, ToI, HitPoint))
							{
								Seen.Add(NeighCoord);
								Queue.Add({NeighCoord, ToI});
							}
						}
					}
				}
				else
				{
					// Expand each cell along the thicken direction
					// Although the line should minimally thicken around the perpendicular to the line direction
					// it's cheaper to just expand in the cardinal opposite the current major direction. We end up
					// doing a broad test on more cells but avoid having to run many rasterize/walk steps for each
					// perpendicular step.
					auto Expand = [&](const TVector<int32, 2>& Begin, const TVector<int32, 2>& Direction, const int32 NumSteps)
					{
						TVector<int32, 2> CurrentCell = Begin;

						for(int32 CurrStep = 0; CurrStep < NumSteps; ++CurrStep)
						{
							CurrentCell += Direction;

							// Fail if we leave the grid
							if(Begin[0] < 0 || Begin[1] < 0 || Begin[0] > FlatGrid.Counts()[0] - 1 || Begin[1] > FlatGrid.Counts()[1] - 1)
							{
								break;
							}

							// No intersections here. We set the ToI to zero to cause an intersection check to happen
							// without any expansion when we reach this cell in the queue.
							if(!Seen.Contains(CurrentCell))
							{
								Seen.Add(CurrentCell);
								Queue.Add({CurrentCell, 0});
							}
						}
					};

					// Check the current cell, if we hit its 3D bound we can move on to narrow phase
					const TVector<int32, 2> Coord = CellCoord.Index;
					if(GetCellBounds3DScaled(Coord, Min, Max, HalfExtents3D) &&
						TBox<T, 3>::RaycastFast(Min, Max, StartPoint, Dir, InvDir, bParallel, CurrentLength, InvCurrentLength, ToI, HitPoint))
					{
						bool bContinue = Visitor.VisitSweep(CellCoord.Index[1] * (GeomData.NumCols - 1) + CellCoord.Index[0], CurrentLength);

						if(!bContinue)
						{
							return true;
						}
					}

					// This time isn't used to reject things for this method but to flag cells that should be expanded
					if(CellCoord.ToI < 0)
					{
						// Perform expansion for thickness
						int32 ExpandAxis;
						if(ThickenDir[0] == 0)
						{
							ExpandAxis = 1;
						}
						else
						{
							ExpandAxis = 0;
						}
						T ExpandSize = HalfExtents3D[ExpandAxis];
						int32 Steps = FMath::RoundFromZero(ExpandSize / GeomData.Scale[ExpandAxis]);

						Expand(Coord, ThickenDir, Steps);
						Expand(Coord, -ThickenDir, Steps);

						// Walk the line and add to the queue
						if(StartCell != EndCell)
						{
							const int32 DoubleError = Error * 2;

							if(DoubleError >= DeltaY)
							{
								Error += DeltaY;
								StartCell[0] += DirX;
							}

							if(DoubleError <= DeltaX)
							{
								Error += DeltaX;
								StartCell[1] += DirY;
							}

							if(!Seen.Contains(StartCell))
							{
								Seen.Add(StartCell);
								Queue.Add({StartCell, -1});
							}
						}
					}
				}
			}
		}

		return false;
	}

	template <typename T>
	bool THeightField<T>::Raycast(const TVector<T, 3>& StartPoint, const TVector<T, 3>& Dir, const T Length, const T Thickness, T& OutTime, TVector<T, 3>& OutPosition, TVector<T, 3>& OutNormal, int32& OutFaceIndex) const
	{
		OutFaceIndex = INDEX_NONE;

		THeightfieldRaycastVisitor<T> Visitor(&GeomData, StartPoint, Dir, Thickness);

		if(Thickness > 0)
		{
			GridSweep(StartPoint, Dir, Length, TVector<T, 2>(Thickness), Visitor);
		}
		else
		{
			GridCast(StartPoint, Dir, Length, Visitor);
		}

		if(Visitor.OutTime <= Length)
		{
			OutTime = Visitor.OutTime;
			OutPosition = Visitor.OutPosition;
			OutNormal = Visitor.OutNormal;
			OutFaceIndex = Visitor.OutFaceIndex;
			return true;
		}

		return false;
	}

	template<typename T>
	bool Chaos::THeightField<T>::GetGridIntersections(FBounds2D InFlatBounds, TArray<TVector<int32, 2>>& OutInterssctions) const
	{
		OutInterssctions.Reset();

		const FBounds2D FlatBounds = GetFlatBounds();
		const TVector<T, 2> Scale2D(GeomData.Scale[0], GeomData.Scale[1]);

		InFlatBounds.Min = FlatBounds.Clamp(InFlatBounds.Min);
		InFlatBounds.Max = FlatBounds.Clamp(InFlatBounds.Max);
		TVector<int32, 2> MinCell = FlatGrid.Cell(InFlatBounds.Min / Scale2D);
		TVector<int32, 2> MaxCell = FlatGrid.Cell(InFlatBounds.Max / Scale2D);
		MinCell = FlatGrid.ClampIndex(MinCell);
		MaxCell = FlatGrid.ClampIndex(MaxCell);

		const int32 DeltaX = MaxCell[0] - MinCell[0];
		const int32 DeltaY = MaxCell[1] - MinCell[1];

		for(int32 CurrX = 0; CurrX < DeltaX; ++CurrX)
		{
			for(int32 CurrY = 0; CurrY < DeltaY; ++CurrY)
			{
				OutInterssctions.Add(TVector<int32, 2>(MinCell[0] + CurrX, MinCell[1] + CurrY));
			}
		}

		return OutInterssctions.Num() > 0;
	}

	template<typename T>
	typename Chaos::THeightField<T>::FBounds2D Chaos::THeightField<T>::GetFlatBounds() const
	{
		FBounds2D Result;
		Result.Min = TVector<T, 2>(CachedBounds.Min()[0], CachedBounds.Min()[1]);
		Result.Max = TVector<T, 2>(CachedBounds.Max()[0], CachedBounds.Max()[1]);
		return Result;
	}

	template <typename T>
	bool THeightField<T>::Overlap(const TVector<T, 3>& Point, const T Thickness) const
	{
		auto OverlapTriangle = [&](const TVector<T, 3>& A, const TVector<T, 3>& B, const TVector<T, 3>& C) -> bool
		{
			const TVector<T, 3> AB = B - A;
			const TVector<T, 3> AC = C - A;
			TVector<T, 3> Normal = TVector<T, 3>::CrossProduct(AB, AC);
			const T NormalLength = Normal.SafeNormalize();

			if(!ensure(NormalLength > KINDA_SMALL_NUMBER))
			{
				return false;
			}

			const TPlane<T, 3> TriPlane{A, Normal};
			const TVector<T, 3> ClosestPointOnTri = FindClosestPointOnTriangle(TriPlane, A, B, C, Point);
			const T Distance2 = (ClosestPointOnTri - Point).SizeSquared();

			if(Distance2 <= Thickness * Thickness)	//This really only has a hope in working if thickness is > 0
			{
				return true;
			}

			return false;
		};

		TBox<T, 3> QueryBounds(Point, Point);
		QueryBounds.Thicken(Thickness);

		FBounds2D FlatQueryBounds;
		FlatQueryBounds.Min = TVector<T, 2>(QueryBounds.Min()[0], QueryBounds.Min()[1]);
		FlatQueryBounds.Max = TVector<T, 2>(QueryBounds.Max()[0], QueryBounds.Max()[1]);

		TArray<TVector<int32, 2>> Intersections;
		TVector<T, 3> Points[4];

		GetGridIntersections(FlatQueryBounds, Intersections);

		for(const TVector<int32, 2>& Cell : Intersections)
		{
			const int32 SingleIndex = Cell[1] * (GeomData.NumCols - 1) + Cell[0];
			GeomData.GetPoints(SingleIndex, Points);

			if(OverlapTriangle(Points[0], Points[1], Points[2]))
			{
				return true;
			}

			if(OverlapTriangle(Points[2], Points[1], Points[3]))
			{
				return true;
			}
		}
		
		return false;
	}


	template <typename T>
	bool THeightField<T>::OverlapGeom(const TImplicitObject<T, 3>& QueryGeom, const TRigidTransform<T, 3>& QueryTM, const T Thickness) const
	{
		auto OverlapTriangle = [&](const TVector<T, 3>& A, const TVector<T, 3>& B, const TVector<T, 3>& C) -> bool
		{
			const TVector<T, 3> AB = B - A;
			const TVector<T, 3> AC = C - A;

			//It's most likely that the query object is in front of the triangle since queries tend to be on the outside.
			//However, maybe we should check if it's behind the triangle plane. Also, we should enforce this winding in some way
			const TVector<T, 3> Offset = TVector<T, 3>::CrossProduct(AB, AC);

			// Ugly but required for now until we have an easier way to do tri collisons 
			TParticles<T, 3> Particles;
			Particles.AddParticles(3);

			Particles.X(0) = A;
			Particles.X(1) = B;
			Particles.X(2) = C;

			TConvex<T, 3> TriangleConvex(Particles);

			return GJKIntersection(TriangleConvex, QueryGeom, QueryTM, Thickness, Offset);
		};

		bool bResult = false;
		TBox<T, 3> QueryBounds = QueryGeom.BoundingBox();
		QueryBounds.Thicken(Thickness);
		QueryBounds = QueryBounds.TransformedBox(QueryTM);

		FBounds2D FlatQueryBounds;
		FlatQueryBounds.Min = TVector<T, 2>(QueryBounds.Min()[0], QueryBounds.Min()[1]);
		FlatQueryBounds.Max = TVector<T, 2>(QueryBounds.Max()[0], QueryBounds.Max()[1]);

		TArray<TVector<int32, 2>> Intersections;
		TVector<T, 3> Points[4];

		GetGridIntersections(FlatQueryBounds, Intersections);

		for(const TVector<int32, 2>& Cell : Intersections)
		{
			const int32 SingleIndex = Cell[1] * (GeomData.NumCols - 1) + Cell[0];
			GeomData.GetPoints(SingleIndex, Points);

			if(OverlapTriangle(Points[0], Points[1], Points[2]))
			{
				return true;
			}

			if(OverlapTriangle(Points[2], Points[1], Points[3]))
			{
				return true;
			}
		}

		return false;
	}

	template <typename T>
	bool THeightField<T>::SweepGeom(const TImplicitObject<T, 3>& QueryGeom, const TRigidTransform<T, 3>& StartTM, const TVector<T, 3>& Dir, const T Length, T& OutTime, TVector<T, 3>& OutPosition, TVector<T, 3>& OutNormal, int32& OutFaceIndex, const T Thickness) const
	{
		bool bHit = false;
		THeightfieldSweepVisitor<T> SQVisitor(&GeomData, QueryGeom, StartTM, Dir, Thickness);
		const TBox<T, 3> QueryBounds = QueryGeom.BoundingBox();
		const TVector<T, 3> StartPoint = StartTM.TransformPositionNoScale(QueryBounds.Center());

		const TVector<T, 3> Inflation3D = QueryBounds.Extents() * 0.5 + TVector<T, 3>(Thickness);
		GridSweep(StartPoint, Dir, Length, TVector<T, 2>(Inflation3D[0], Inflation3D[1]), SQVisitor);

		if(SQVisitor.OutTime <= Length)
		{
			OutTime = SQVisitor.OutTime;
			OutPosition = SQVisitor.OutPosition;
			OutNormal = SQVisitor.OutNormal;
			OutFaceIndex = SQVisitor.OutFaceIndex;
			bHit = true;
		}

		return bHit;
	}

	template <typename T>
	int32 THeightField<T>::FindMostOpposingFace(const TVector<T, 3>& Position, const TVector<T, 3>& UnitDir, int32 HintFaceIndex, T SearchDist) const
	{
		const T SearchDist2 = SearchDist * SearchDist;

		TBox<T, 3> QueryBounds(Position - TVector<T, 3>(SearchDist), Position + TVector<T, 3>(SearchDist));
		const FBounds2D FlatBounds(QueryBounds);
		TArray<TVector<int32, 2>> PotentialIntersections;
		GetGridIntersections(FlatBounds, PotentialIntersections);

		T MostOpposingDot = TNumericLimits<T>::Max();
		int32 MostOpposingFace = HintFaceIndex;

		auto CheckTriangle = [&](int32 FaceIndex, const TVector<T, 3>& A, const TVector<T, 3>& B, const TVector<T, 3>& C)
		{
			const TVector<T, 3> AB = B - A;
			const TVector<T, 3> AC = C - A;
			TVector<T, 3> Normal = TVector<T, 3>::CrossProduct(AB, AC);
			const T NormalLength = Normal.SafeNormalize();
			if(!ensure(NormalLength > KINDA_SMALL_NUMBER))
			{
				//hitting degenerate triangle - should be fixed before we get to this stage
				return;
			}

			const TPlane<T, 3> TriPlane{A, Normal};
			const TVector<T, 3> ClosestPointOnTri = FindClosestPointOnTriangle(TriPlane, A, B, C, Position);
			const T Distance2 = (ClosestPointOnTri - Position).SizeSquared();
			if(Distance2 < SearchDist2)
			{
				const T Dot = TVector<T, 3>::DotProduct(Normal, UnitDir);
				if(Dot < MostOpposingDot)
				{
					MostOpposingDot = Dot;
					MostOpposingFace = FaceIndex;
				}
			}
		};

		for(const TVector<int32, 2>& CellCoord : PotentialIntersections)
		{
			const int32 CellIndex = CellCoord[1] * (GeomData.NumCols - 1) + CellCoord[0];
			const int32 SubX = CellIndex % (GeomData.NumCols - 1);
			const int32 SubY = CellIndex / (GeomData.NumCols - 1);

			const int32 FullIndex = CellIndex + SubY;

			TVector<T, 3> Points[4];

			GeomData.GetPoints(FullIndex, Points);

			CheckTriangle(FullIndex * 2, Points[0], Points[1], Points[2]);
			CheckTriangle(FullIndex * 2 + 1, Points[2], Points[1], Points[3]);
		}

		return MostOpposingFace;
	}

	template <typename T>
	TVector<T, 3> THeightField<T>::FindGeometryOpposingNormal(const TVector<T, 3>& DenormDir, int32 FaceIndex, const TVector<T, 3>& OriginalNormal) const
	{
		if(ensure(FaceIndex != INDEX_NONE))
		{
			bool bSecondFace = FaceIndex % 2 != 0;

			if(bSecondFace)
			{
				FaceIndex -= 1;
			}
			FaceIndex /= 2;

			TVector<T, 3> Points[4];

			GeomData.GetPointsScaled(FaceIndex, Points);

			TVector<T, 3> A;
			TVector<T, 3> B;
			TVector<T, 3> C;

			if(bSecondFace)
			{
				A = Points[2];
				B = Points[1];
				C = Points[3];
			}
			else
			{
				A = Points[0];
				B = Points[1];
				C = Points[2];
			}

			const TVector<T, 3> AB = B - A;
			const TVector<T, 3> AC = C - A;
			TVector<T, 3> Normal = TVector<T, 3>::CrossProduct(AB, AC);
			const T Length = Normal.SafeNormalize();
			ensure(Length);
			return Normal;
		}

		return TVector<T, 3>(0, 0, 1);
	}

	template<typename T>
	void THeightField<T>::CalcBounds()
	{
		// Flatten out the Z axis
		FlattenedBounds = GetFlatBounds();

		BuildQueryData();

		// Cache per-cell bounds
		const int32 NumX = GeomData.NumCols - 1;
		const int32 NumY = GeomData.NumRows - 1;
		GeomData.CellBounds.SetNum(NumX * NumY);
		for(int32 XIndex = 0; XIndex < NumX; ++XIndex)
		{
			for(int32 YIndex = 0; YIndex < NumY; ++YIndex)
			{
				const TVector<int32, 2> Cell(XIndex, YIndex);
				TVector<T, 3> Min, Max;
				CalcCellBounds3D(Cell, Min, Max);
				GeomData.CellBounds[YIndex * NumX + XIndex] = TBox<T, 3>(Min, Max);
			}
		}
	}

	template<typename T>
	void THeightField<T>::BuildQueryData()
	{
		// NumCols and NumRows are the actual heights, there are n-1 cells between those heights
		TVector<int32, 2> Cells(GeomData.NumCols - 1, GeomData.NumRows - 1);
		
		TVector<T, 2> MinCorner(0, 0);
		TVector<T, 2> MaxCorner(GeomData.NumCols - 1, GeomData.NumRows - 1);
		MaxCorner *= {GeomData.Scale[0], GeomData.Scale[1]};

		FlatGrid = TUniformGrid<T, 2>(MinCorner, MaxCorner, Cells);
	}

}

template class Chaos::THeightField<float>;

