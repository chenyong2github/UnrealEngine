// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Array.h"
#include "ImplicitObject.h"
#include "Box.h"
#include "TriangleMeshImplicitObject.h"
#include "ChaosArchive.h"
#include "Serialization/MemoryWriter.h"
#include "Math/NumericLimits.h"
#include "Templates/UnrealTypeTraits.h"
#include "UniformGrid.h"
#include "Utilities.h"
#include "UObject/ExternalPhysicsCustomObjectVersion.h"

namespace Chaos
{
	template<typename T>
	class THeightfieldRaycastVisitor;
	class FConvex;
}

namespace Chaos
{
	template<typename T>
	class CHAOS_API THeightField final : public FImplicitObject
	{
	public:
		using FImplicitObject::GetTypeName;

		THeightField(TArray<T>&& Height, TArray<uint8>&& InMaterialIndices, int32 InNumRows, int32 InNumCols, const TVector<T,3>& InScale);
		THeightField(TArrayView<const uint16> InHeights, TArrayView<uint8> InMaterialIndices, int32 InNumRows, int32 InNumCols, const TVector<T, 3>& InScale);
		THeightField(const THeightField& Other) = delete;
		THeightField(THeightField&& Other) = default;

		virtual ~THeightField() {}

		/** Support for editing a subsection of the heightfield */
		void EditHeights(TArrayView<T> InHeights, int32 InBeginRow, int32 InBeginCol, int32 InNumRows, int32 InNumCols);
		void EditHeights(TArrayView<const uint16> InHeights, int32 InBeginRow, int32 InBeginCol, int32 InNumRows, int32 InNumCols);
		T GetHeight(int32 InIndex) const;
		T GetHeight(int32 InX, int32 InY) const;
		T GetHeightAt(const TVector<T, 2>& InGridLocation) const;
		int32 GetNumRows() const { return GeomData.NumRows; }
		int32 GetNumCols() const { return GeomData.NumCols; }

		virtual T PhiWithNormal(const TVector<T, 3>& x, TVector<T, 3>& Normal) const
		{
			check(false);	//not supported yet - might support it in the future or we may change the interface
			return (T)0;
		}

		virtual bool Raycast(const TVector<T, 3>& StartPoint, const TVector<T, 3>& Dir, const T Length, const T Thickness, T& OutTime, TVector<T,3>& OutPosition, TVector<T,3>& OutNormal, int32& OutFaceIndex) const override;
		virtual bool Overlap(const TVector<T, 3>& Point, const T Thickness) const override;
		
		bool OverlapGeom(const TSphere<T, 3>& QueryGeom, const TRigidTransform<T, 3>& QueryTM, const T Thickness) const;
		bool OverlapGeom(const TBox<T, 3>& QueryGeom, const TRigidTransform<T, 3>& QueryTM, const T Thickness) const;
		bool OverlapGeom(const TCapsule<T>& QueryGeom, const TRigidTransform<T, 3>& QueryTM, const T Thickness) const;
		bool OverlapGeom(const FConvex& QueryGeom, const TRigidTransform<T, 3>& QueryTM, const T Thickness) const;
		bool OverlapGeom(const TImplicitObjectScaled<TSphere<T, 3>>& QueryGeom, const TRigidTransform<T, 3>& QueryTM, const T Thickness) const;
		bool OverlapGeom(const TImplicitObjectScaled<TBox<T, 3>>& QueryGeom, const TRigidTransform<T, 3>& QueryTM, const T Thickness) const;
		bool OverlapGeom(const TImplicitObjectScaled<TCapsule<T>>& QueryGeom, const TRigidTransform<T, 3>& QueryTM, const T Thickness) const;
		bool OverlapGeom(const TImplicitObjectScaled<FConvex>& QueryGeom, const TRigidTransform<T, 3>& QueryTM, const T Thickness) const;

		bool SweepGeom(const TSphere<T, 3>& QueryGeom, const TRigidTransform<T, 3>& StartTM, const TVector<T, 3>& Dir, const T Length, T& OutTime, TVector<T, 3>& OutPosition, TVector<T, 3>& OutNormal, int32& OutFaceIndex, const T Thickness = 0, bool bComputeMTD = false) const;
		bool SweepGeom(const TBox<T, 3>& QueryGeom, const TRigidTransform<T, 3>& StartTM, const TVector<T, 3>& Dir, const T Length, T& OutTime, TVector<T, 3>& OutPosition, TVector<T, 3>& OutNormal, int32& OutFaceIndex, const T Thickness = 0, bool bComputeMTD = false) const;
		bool SweepGeom(const TCapsule<T>& QueryGeom, const TRigidTransform<T, 3>& StartTM, const TVector<T, 3>& Dir, const T Length, T& OutTime, TVector<T, 3>& OutPosition, TVector<T, 3>& OutNormal, int32& OutFaceIndex, const T Thickness = 0, bool bComputeMTD = false) const;
		bool SweepGeom(const FConvex& QueryGeom, const TRigidTransform<T, 3>& StartTM, const TVector<T, 3>& Dir, const T Length, T& OutTime, TVector<T, 3>& OutPosition, TVector<T, 3>& OutNormal, int32& OutFaceIndex, const T Thickness = 0, bool bComputeMTD = false) const;
		bool SweepGeom(const TImplicitObjectScaled<TSphere<T, 3>>& QueryGeom, const TRigidTransform<T, 3>& StartTM, const TVector<T, 3>& Dir, const T Length, T& OutTime, TVector<T, 3>& OutPosition, TVector<T, 3>& OutNormal, int32& OutFaceIndex, const T Thickness = 0, bool bComputeMTD = false) const;
		bool SweepGeom(const TImplicitObjectScaled<TBox<T, 3>>& QueryGeom, const TRigidTransform<T, 3>& StartTM, const TVector<T, 3>& Dir, const T Length, T& OutTime, TVector<T, 3>& OutPosition, TVector<T, 3>& OutNormal, int32& OutFaceIndex, const T Thickness = 0, bool bComputeMTD = false) const;
		bool SweepGeom(const TImplicitObjectScaled<TCapsule<T>>& QueryGeom, const TRigidTransform<T, 3>& StartTM, const TVector<T, 3>& Dir, const T Length, T& OutTime, TVector<T, 3>& OutPosition, TVector<T, 3>& OutNormal, int32& OutFaceIndex, const T Thickness = 0, bool bComputeMTD = false) const;
		bool SweepGeom(const TImplicitObjectScaled<FConvex>& QueryGeom, const TRigidTransform<T, 3>& StartTM, const TVector<T, 3>& Dir, const T Length, T& OutTime, TVector<T, 3>& OutPosition, TVector<T, 3>& OutNormal, int32& OutFaceIndex, const T Thickness = 0, bool bComputeMTD = false) const;

		bool GJKContactPoint(const TBox<T, 3>& QueryGeom, const TRigidTransform<T, 3>& QueryTM, const T Thickness, TVector<T, 3>& ContactLocation, TVector<T, 3>& ContactNormal, T& ContactPhi) const;
		bool GJKContactPoint(const TSphere<T, 3>& QueryGeom, const TRigidTransform<T, 3>& QueryTM, const T Thickness, TVector<T, 3>& ContactLocation, TVector<T, 3>& ContactNormal, T& ContactPhi) const;
		bool GJKContactPoint(const TCapsule<T>& QueryGeom, const TRigidTransform<T, 3>& QueryTM, const T Thickness, TVector<T, 3>& ContactLocation, TVector<T, 3>& ContactNormal, T& ContactPhi) const;
		bool GJKContactPoint(const FConvex& QueryGeom, const TRigidTransform<T, 3>& QueryTM, const T Thickness, TVector<T, 3>& ContactLocation, TVector<T, 3>& ContactNormal, T& ContactPhi) const;
		bool GJKContactPoint(const TImplicitObjectScaled<TBox<T, 3>>& QueryGeom, const TRigidTransform<T, 3>& QueryTM, const T Thickness, TVector<T, 3>& ContactLocation, TVector<T, 3>& ContactNormal, T& ContactPhi) const;
		bool GJKContactPoint(const TImplicitObjectScaled<TSphere<T, 3>>& QueryGeom, const TRigidTransform<T, 3>& QueryTM, const T Thickness, TVector<T, 3>& ContactLocation, TVector<T, 3>& ContactNormal, T& ContactPhi) const;
		bool GJKContactPoint(const TImplicitObjectScaled<TCapsule<T>>& QueryGeom, const TRigidTransform<T, 3>& QueryTM, const T Thickness, TVector<T, 3>& ContactLocation, TVector<T, 3>& ContactNormal, T& ContactPhi) const;
		bool GJKContactPoint(const TImplicitObjectScaled<FConvex>& QueryGeom, const TRigidTransform<T, 3>& QueryTM, const T Thickness, TVector<T, 3>& ContactLocation, TVector<T, 3>& ContactNormal, T& ContactPhi) const;


		virtual int32 FindMostOpposingFace(const TVector<T, 3>& Position, const TVector<T, 3>& UnitDir, int32 HintFaceIndex, T SearchDist) const override;
		virtual TVector<T, 3> FindGeometryOpposingNormal(const TVector<T, 3>& DenormDir, int32 FaceIndex, const TVector<T, 3>& OriginalNormal) const override;




		virtual uint16 GetMaterialIndex(uint32 HintIndex) const override
		{
			ensure(GeomData.MaterialIndices.Num() > 0);

			// If we've only got a default
			if(GeomData.MaterialIndices.Num() == 1)
			{
				return GeomData.MaterialIndices[0];
			}
			else
			{
				// We store per cell for materials, so change to cell index
				int32 CellIndex = HintIndex / 2;
				if(ensure(GeomData.MaterialIndices.IsValidIndex(CellIndex)))
				{
					return GeomData.MaterialIndices[CellIndex];
				}
			}

			return 0;
		}

		virtual const TAABB<T, 3> BoundingBox() const
		{
			CachedBounds = TAABB<T, 3>(LocalBounds.Min() * GeomData.Scale, LocalBounds.Max() * GeomData.Scale);
			return CachedBounds;
		}

		virtual uint32 GetTypeHash() const override
		{
			TArray<uint8> Bytes;
			FMemoryWriter Writer(Bytes);
			FChaosArchive ChaosAr(Writer);

			// Saving to an archive is a const operation, but must be non-const
			// to support loading. Cast const away here to get bytes written
			const_cast<THeightField<T>*>(this)->Serialize(ChaosAr);

			return FCrc::MemCrc32(Bytes.GetData(), Bytes.GetAllocatedSize());
		}

		static constexpr EImplicitObjectType StaticType()
		{
			return ImplicitObjectType::HeightField;
		}

		virtual void Serialize(FChaosArchive& Ar) override
		{
			FChaosArchiveScopedMemory ScopedMemory(Ar, GetTypeName());
			FImplicitObject::SerializeImp(Ar);
			
			GeomData.Serialize(Ar);

			Ar.UsingCustomVersion(FExternalPhysicsCustomObjectVersion::GUID);
			if (Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) >= FExternalPhysicsCustomObjectVersion::HeightfieldData)
			{
				Ar << FlatGrid;
				Ar << FlattenedBounds.Min;
				Ar << FlattenedBounds.Max;
				TBox<FReal, 3>::SerializeAsAABB(Ar, LocalBounds);
			}
			else
			{
				CalcBounds();
			}
			

			if(Ar.IsLoading())
			{
				BuildQueryData();
				BoundingBox();	//temp hack to initialize cache
			}
		}

		void SetScale(const TVector<T, 3>& InScale)
		{
			GeomData.Scale = InScale;
		}

		template<typename InStorageType, typename InRealType>
		struct FData
		{
			// For ease of access through typedefs
			using StorageType = InStorageType;
			using RealType = InRealType;

			// Only supporting unsigned int types for the height range - really no difference using
			// this or signed but this is a little nicer overall
			static_assert(TIsSame<StorageType, uint8>::Value || 
				TIsSame<StorageType, uint16>::Value || 
				TIsSame<StorageType, uint32>::Value || 
				TIsSame<StorageType, uint64>::Value,
				"Expected unsigned integer type for heightfield data storage");

			// Data sizes to validate during serialization
			static constexpr int32 RealSize = sizeof(RealType);
			static constexpr int32 StorageSize = sizeof(StorageType);

			// Range of the chosen type (unsigned so Min is always 0)
			static constexpr int32 StorageRange = TNumericLimits<StorageType>::Max();

			// Heights in the chosen format. final placement of the vertex will be at
			// MinValue + Heights[Index] * HeightPerUnit
			// With HeightPerUnit being the range of the min/max realtype values of
			// the heightfield divided by the range of StorageType
			TArray<StorageType> Heights;
			TArray<uint8> MaterialIndices;
			TVector<RealType, 3> Scale;
			RealType MinValue;
			RealType MaxValue;
			uint16 NumRows;
			uint16 NumCols;
			RealType Range;
			RealType HeightPerUnit;

			constexpr float GetCellWidth() const
			{
				return Scale[0];
			}

			constexpr float GetCellHeight() const
			{
				return Scale[1];
			}

			FORCEINLINE TVector<T, 3> GetPoint(int32 Index) const
			{
				const typename FDataType::RealType Height = MinValue + Heights[Index] * HeightPerUnit;

				const int32 X = Index % (NumCols);
				const int32 Y = Index / (NumCols);

				return {(typename FDataType::RealType)X, (typename FDataType::RealType)Y, Height};
			}

			FORCEINLINE TVector<T, 3> GetPointScaled(int32 Index) const
			{
				return GetPoint(Index) * Scale;
			}

			FORCEINLINE void GetPoints(int32 Index, TVector<T, 3> OutPts[4]) const
			{
				const typename FDataType::RealType H0 = MinValue + Heights[Index] * HeightPerUnit;
				const typename FDataType::RealType H1 = MinValue + Heights[Index + 1] * HeightPerUnit;
				const typename FDataType::RealType H2 = MinValue + Heights[Index + NumCols] * HeightPerUnit;
				const typename FDataType::RealType H3 = MinValue + Heights[Index + NumCols + 1] * HeightPerUnit;

				const int32 X = Index % (NumCols);
				const int32 Y = Index / (NumCols);

				OutPts[0] = {(typename FDataType::RealType)X, (typename FDataType::RealType)Y, H0};
				OutPts[1] = {(typename FDataType::RealType)X + 1, (typename FDataType::RealType)Y, H1};
				OutPts[2] = {(typename FDataType::RealType)X, (typename FDataType::RealType)Y + 1, H2};
				OutPts[3] = {(typename FDataType::RealType)X + 1, (typename FDataType::RealType)Y + 1, H3};
			}

			FORCEINLINE void GetPointsScaled(int32 Index, TVector<T, 3> OutPts[4]) const
			{
				GetPoints(Index, OutPts);

				OutPts[0] *= Scale;
				OutPts[1] *= Scale;
				OutPts[2] *= Scale;
				OutPts[3] *= Scale;
			}

			FORCEINLINE T GetMinHeight() const
			{
				return static_cast<typename FDataType::RealType>(MinValue);
			}

			void Serialize(FChaosArchive& Ar)
			{
				int32 TempRealSize = RealSize;
				int32 TempStorageSize = StorageSize;

				Ar << TempRealSize;
				Ar << TempStorageSize;

				if(Ar.IsLoading())
				{
					checkf(TempRealSize == RealSize, TEXT("Heightfield was serialized with mismatched real type size (expected: %d, found: %d)"), RealSize, TempRealSize);
					checkf(TempStorageSize == StorageSize, TEXT("Heightfield was serialized with mismatched storage type size (expected: %d, found: %d)"), StorageSize, TempStorageSize);
				}
				
				Ar << Heights;
				Ar << Scale;
				Ar << MinValue;
				Ar << MaxValue;
				Ar << NumRows;
				Ar << NumCols;

				Ar.UsingCustomVersion(FExternalPhysicsCustomObjectVersion::GUID);
				if (Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) >= FExternalPhysicsCustomObjectVersion::HeightfieldData)
				{
					Ar << Range;
					Ar << HeightPerUnit;

					if (Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) < FExternalPhysicsCustomObjectVersion::HeightfieldImplicitBounds)
					{
						TArray<TBox<RealType, 3>> CellBounds;
						Ar << CellBounds;
					}
					else if(Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) < FExternalPhysicsCustomObjectVersion::HeightfieldUsesHeightsDirectly)
					{
						TArray<RealType> OldHeights;
						Ar << OldHeights;
					}
				}

				if(Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) >= FExternalPhysicsCustomObjectVersion::AddedMaterialManager)
				{
					Ar << MaterialIndices;
				}
			}
		};

		using FDataType = FData<uint16, float>;
		FDataType GeomData;

	private:

		// Struct for 2D bounds and associated operations
		struct FBounds2D
		{
			TVector<T, 2> Min;
			TVector<T, 2> Max;
			
			FBounds2D()
				: Min(0)
				, Max(0)
			{}

			explicit FBounds2D(const TAABB<T, 3>& In3DBounds)
			{
				Set(In3DBounds);
			}

			void Set(const TAABB<T, 3>& In3DBounds)
			{
				Min = {In3DBounds.Min()[0], In3DBounds.Min()[1]};
				Max = {In3DBounds.Max()[0], In3DBounds.Max()[1]};
			}

			TVector<T, 2> GetExtent() const
			{
				return Max - Min;
			}

			bool IsInside(const TVector<T, 2>& InPoint) const
			{
				return InPoint[0] >= Min[0] && InPoint[0] <= Max[0] && InPoint[1] >= Min[1] && InPoint[1] <= Max[1];
			}

			TVector<T, 2> Clamp(const TVector<T, 2>& InToClamp, T InNudge = SMALL_NUMBER) const
			{
				const TVector<T, 2> NudgeVec(InNudge, InNudge);
				const TVector<T, 2> TestMin = Min + NudgeVec;
				const TVector<T, 2> TestMax = Max - NudgeVec;

				TVector<T, 2> OutVec = InToClamp;

				OutVec[0] = FMath::Max(OutVec[0], TestMin[0]);
				OutVec[1] = FMath::Max(OutVec[1], TestMin[1]);

				OutVec[0] = FMath::Min(OutVec[0], TestMax[0]);
				OutVec[1] = FMath::Min(OutVec[1], TestMax[1]);

				return OutVec;
			}

			bool IntersectLine(const TVector<T, 2>& InStart, const TVector<T, 2>& InEnd)
			{
				if(IsInside(InStart) || IsInside(InEnd))
				{
					return true;
				}

				const TVector<T, 2> Extent = GetExtent();
				float TA, TB;

				if(Utilities::IntersectLineSegments2D(InStart, InEnd, Min, TVector<T, 2>(Min[0] + Extent[0], Min[1]), TA, TB)
					|| Utilities::IntersectLineSegments2D(InStart, InEnd, Min, TVector<T, 2>(Min[0], Min[1] + Extent[1]), TA, TB)
					|| Utilities::IntersectLineSegments2D(InStart, InEnd, Max, TVector<T, 2>(Max[0] - Extent[0], Max[1]), TA, TB)
					|| Utilities::IntersectLineSegments2D(InStart, InEnd, Max, TVector<T, 2>(Max[0], Max[1] - Extent[1]), TA, TB))
				{
					return true;
				}

				return false;
			}

			bool ClipLine(const TVector<T, 3>& InStart, const TVector<T, 3>& InEnd, TVector<T, 2>& OutClippedStart, TVector<T, 2>& OutClippedEnd) const
			{
				TVector<T, 2> TempStart(InStart[0], InStart[1]);
				TVector<T, 2> TempEnd(InEnd[0], InEnd[1]);

				bool bLineIntersects = ClipLine(TempStart, TempEnd);

				OutClippedStart = TempStart;
				OutClippedEnd = TempEnd;

				return bLineIntersects;
			}

			bool ClipLine(TVector<T, 2>& InOutStart, TVector<T, 2>& InOutEnd) const
			{
				// Test we don't need to clip at all, quite likely with a heightfield so optimize for it.
				if(IsInside(InOutStart) && IsInside(InOutEnd))
				{
					return true;
				}

				TArray<T> HitTimes;
				const TVector<T, 2> Extent = GetExtent();
				float TA, TB;
				if(Utilities::IntersectLineSegments2D(InOutStart, InOutEnd, Min, TVector<T, 2>(Min[0] + Extent[0], Min[1]), TA, TB))
				{
					HitTimes.Add(TA);
				}

				if(Utilities::IntersectLineSegments2D(InOutStart, InOutEnd, Min, TVector<T, 2>(Min[0], Min[1] + Extent[1]), TA, TB))
				{
					HitTimes.Add(TA);
				}

				if(Utilities::IntersectLineSegments2D(InOutStart, InOutEnd, Max, TVector<T, 2>(Max[0] - Extent[0], Max[1]), TA, TB))
				{
					HitTimes.Add(TA);
				}

				if(Utilities::IntersectLineSegments2D(InOutStart, InOutEnd, Max, TVector<T, 2>(Max[0], Max[1] - Extent[1]), TA, TB))
				{
					HitTimes.Add(TA);
				}

				const int32 NumTimes = HitTimes.Num();
				if(NumTimes > 0)
				{
					// Can only ever be 0, 1 or 2 entries in here. First check if we're starting inside the box
					// so we correctly set the clip extents
					HitTimes.Sort();
					const TVector<T, 2> TempStart = InOutStart;

					if(IsInside(InOutStart) && ensure(NumTimes == 1))
					{
						// we begin somewhere inside the box - just clip the end
						InOutEnd = TempStart + HitTimes[0] * (InOutEnd - TempStart);
					}
					else
					{
						// Clip the start and if necessary, the end (might not need to if it's already inside)
						InOutStart = TempStart + HitTimes[0] * (InOutEnd - TempStart);

						if(HitTimes.IsValidIndex(1))
						{
							InOutEnd = TempStart + HitTimes[1] * (InOutEnd - TempStart);
						}
					}

					// Long rays can leave us barely outside the bounds, clamp the final results to avoid this
					InOutStart = Clamp(InOutStart);
					InOutEnd = Clamp(InOutEnd);

					// We must hit the bound in some way
					return true;
				}

				return false;
			}
		};

		// Helpers for accessing bounds
		bool GetCellBounds2D(const TVector<int32, 2> InCoord, FBounds2D& OutBounds, const TVector<T, 2>& InInflate = {0}) const;
		bool GetCellBounds3D(const TVector<int32, 2> InCoord, TVector<T, 3>& OutMin, TVector<T, 3>& OutMax, const TVector<T, 3>& InInflate = TVector<T, 3>(0)) const;
		bool GetCellBounds2DScaled(const TVector<int32, 2> InCoord, FBounds2D& OutBounds, const TVector<T, 2>& InInflate = {0}) const;
		bool GetCellBounds3DScaled(const TVector<int32, 2> InCoord, TVector<T, 3>& OutMin, TVector<T, 3>& OutMax, const TVector<T, 3>& InInflate = TVector<T, 3>(0)) const;
		bool CalcCellBounds3D(const TVector<int32, 2> InCoord, TVector<T, 3>& OutMin, TVector<T, 3>& OutMax, const TVector<T, 3>& InInflate = TVector<T, 3>(0)) const;

		// Query functions - sweep, ray, overlap
		template<typename SQVisitor>
		bool GridSweep(const TVector<T, 3>& StartPoint, const TVector<T, 3>& Dir, const T Length, const TVector<T, 2> InHalfExtents, SQVisitor& Visitor) const;
		bool GridCast(const TVector<T, 3>& StartPoint, const TVector<T, 3>& Dir, const T Length, THeightfieldRaycastVisitor<T>& Visitor) const;
		bool GetGridIntersections(FBounds2D InFlatBounds, TArray<TVector<int32, 2>>& OutInterssctions) const;
		
		FBounds2D GetFlatBounds() const;

		// Grid for queries, faster than bounding volumes for heightfields
		TUniformGrid<T, 2> FlatGrid;
		// Bounds in 2D of the whole heightfield, to clip queries against
		FBounds2D FlattenedBounds;
		// 3D bounds for the heightfield, for insertion to the scene structure
		TAABB<T, 3> LocalBounds;
		// Cached when bounds are requested. Mutable to allow GetBounds to be logical const
		mutable TAABB<T, 3> CachedBounds;

		void CalcBounds();
		void BuildQueryData();
		
		// Needed for serialization
		THeightField() : FImplicitObject(EImplicitObject::HasBoundingBox, ImplicitObjectType::HeightField) {}
		friend FImplicitObject;

		template <typename QueryGeomType>
		bool OverlapGeomImp(const QueryGeomType& QueryGeom, const TRigidTransform<T, 3>& QueryTM, const T Thickness) const;

		template <typename QueryGeomType>
		bool SweepGeomImp(const QueryGeomType& QueryGeom, const TRigidTransform<T, 3>& StartTM, const TVector<T, 3>& Dir, const T Length, T& OutTime, TVector<T, 3>& OutPosition, TVector<T, 3>& OutNormal, int32& OutFaceIndex, const T Thickness, bool bComputeMTD) const;

		template <typename GeomType>
		bool GJKContactPointImp(const GeomType& QueryGeom, const TRigidTransform<T, 3>& QueryTM, const T Thickness, TVector<T, 3>& ContactLocation, TVector<T, 3>& ContactNormal, T& ContactPhi) const;

	};
}
