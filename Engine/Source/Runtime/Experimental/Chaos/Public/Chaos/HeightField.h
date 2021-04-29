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
	class FHeightfieldRaycastVisitor;
	class FConvex;
	class FTriangle;
	struct FMTDInfo;
}

namespace Chaos
{
	class CHAOS_API FHeightField final : public FImplicitObject
	{
	public:
		using FImplicitObject::GetTypeName;

		FHeightField(TArray<FReal>&& Height, TArray<uint8>&& InMaterialIndices, int32 InNumRows, int32 InNumCols, const FVec3& InScale);
		FHeightField(TArrayView<const uint16> InHeights, TArrayView<uint8> InMaterialIndices, int32 InNumRows, int32 InNumCols, const FVec3& InScale);
		FHeightField(const FHeightField& Other) = delete;
		
		// Not required as long as FImplicitObject also has deleted move constructor (adding this causes an error on Linux build)
		//FHeightField(FHeightField&& Other) = default;

		virtual ~FHeightField() {}

		/** Support for editing a subsection of the heightfield */
		void EditHeights(TArrayView<FReal> InHeights, int32 InBeginRow, int32 InBeginCol, int32 InNumRows, int32 InNumCols);
		void EditHeights(TArrayView<const uint16> InHeights, int32 InBeginRow, int32 InBeginCol, int32 InNumRows, int32 InNumCols);
		FReal GetHeight(int32 InIndex) const;
		FReal GetHeight(int32 InX, int32 InY) const;
		uint8 GetMaterialIndex(int32 InIndex) const;
		uint8 GetMaterialIndex(int32 InX, int32 InY) const;
		bool IsHole(int32 InIndex) const;
		bool IsHole(int32 InCellX, int32 InCellY) const;
		FVec3 GetNormalAt(const FVec2& InGridLocationLocal) const;
		FReal GetHeightAt(const FVec2& InGridLocationLocal) const;

		int32 GetNumRows() const { return GeomData.NumRows; }
		int32 GetNumCols() const { return GeomData.NumCols; }

		virtual FReal PhiWithNormal(const FVec3& x, FVec3& Normal) const;

		virtual bool Raycast(const FVec3& StartPoint, const FVec3& Dir, const FReal Length, const FReal Thickness, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex) const override;
		virtual bool Overlap(const FVec3& Point, const FReal Thickness) const override;
		
		bool OverlapGeom(const TSphere<FReal, 3>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FMTDInfo* OutMTD = nullptr) const;
		bool OverlapGeom(const TBox<FReal, 3>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FMTDInfo* OutMTD = nullptr) const;
		bool OverlapGeom(const FCapsule& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FMTDInfo* OutMTD = nullptr) const;
		bool OverlapGeom(const FConvex& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FMTDInfo* OutMTD = nullptr) const;
		bool OverlapGeom(const TImplicitObjectScaled<TSphere<FReal, 3>>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FMTDInfo* OutMTD = nullptr) const;
		bool OverlapGeom(const TImplicitObjectScaled<TBox<FReal, 3>>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FMTDInfo* OutMTD = nullptr) const;
		bool OverlapGeom(const TImplicitObjectScaled<FCapsule>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FMTDInfo* OutMTD = nullptr) const;
		bool OverlapGeom(const TImplicitObjectScaled<FConvex>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FMTDInfo* OutMTD = nullptr) const;

		bool SweepGeom(const TSphere<FReal, 3>& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, const FReal Thickness = 0, bool bComputeMTD = false) const;
		bool SweepGeom(const TBox<FReal, 3>& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, const FReal Thickness = 0, bool bComputeMTD = false) const;
		bool SweepGeom(const FCapsule& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, const FReal Thickness = 0, bool bComputeMTD = false) const;
		bool SweepGeom(const FConvex& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, const FReal Thickness = 0, bool bComputeMTD = false) const;
		bool SweepGeom(const TImplicitObjectScaled<TSphere<FReal, 3>>& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, const FReal Thickness = 0, bool bComputeMTD = false) const;
		bool SweepGeom(const TImplicitObjectScaled<TBox<FReal, 3>>& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, const FReal Thickness = 0, bool bComputeMTD = false) const;
		bool SweepGeom(const TImplicitObjectScaled<FCapsule>& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, const FReal Thickness = 0, bool bComputeMTD = false) const;
		bool SweepGeom(const TImplicitObjectScaled<FConvex>& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, const FReal Thickness = 0, bool bComputeMTD = false) const;

		bool GJKContactPoint(const TBox<FReal, 3>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FVec3& ContactLocation, FVec3& ContactNormal, FReal& ContactPhi) const;
		bool GJKContactPoint(const TSphere<FReal, 3>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FVec3& ContactLocation, FVec3& ContactNormal, FReal& ContactPhi) const;
		bool GJKContactPoint(const FCapsule& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FVec3& ContactLocation, FVec3& ContactNormal, FReal& ContactPhi) const;
		bool GJKContactPoint(const FConvex& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FVec3& ContactLocation, FVec3& ContactNormal, FReal& ContactPhi) const;
		bool GJKContactPoint(const TImplicitObjectScaled<TBox<FReal, 3>>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FVec3& ContactLocation, FVec3& ContactNormal, FReal& ContactPhi) const;
		bool GJKContactPoint(const TImplicitObjectScaled<TSphere<FReal, 3>>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FVec3& ContactLocation, FVec3& ContactNormal, FReal& ContactPhi) const;
		bool GJKContactPoint(const TImplicitObjectScaled<FCapsule>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FVec3& ContactLocation, FVec3& ContactNormal, FReal& ContactPhi) const;
		bool GJKContactPoint(const TImplicitObjectScaled<FConvex>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FVec3& ContactLocation, FVec3& ContactNormal, FReal& ContactPhi) const;


		virtual int32 FindMostOpposingFace(const FVec3& Position, const FVec3& UnitDir, int32 HintFaceIndex, FReal SearchDist) const override;
		virtual FVec3 FindGeometryOpposingNormal(const FVec3& DenormDir, int32 FaceIndex, const FVec3& OriginalNormal) const override;

		void VisitTriangles(const FAABB3& InQueryBounds, const TFunction<void(const FTriangle& Triangle)>& Visitor) const;

		struct FClosestFaceData
		{
			int32 FaceIndex = INDEX_NONE;
			FReal DistanceToFaceSq = TNumericLimits<FReal>::Max();
			bool bWasSampleBehind = false;
		};

		FClosestFaceData FindClosestFace(const FVec3& Position, FReal SearchDist) const;
		
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
				if(GeomData.MaterialIndices.IsValidIndex(CellIndex))
				{
					return GeomData.MaterialIndices[CellIndex];
				}
			}
			
			// INDEX_NONE will be out of bounds but it is an expected value. If we reach this section of the code and the index isn't INDEX_NONE, we have an issue
			ensureMsgf(HintIndex == INDEX_NONE,TEXT("GetMaterialIndex called with an invalid MaterialIndex => %d"),HintIndex);
			
			return 0;
		}

		virtual const FAABB3 BoundingBox() const
		{
			CachedBounds = FAABB3(LocalBounds.Min() * GeomData.Scale, LocalBounds.Max() * GeomData.Scale);
			return CachedBounds;
		}

		virtual uint32 GetTypeHash() const override
		{
			TArray<uint8> Bytes;
			FMemoryWriter Writer(Bytes);
			FChaosArchive ChaosAr(Writer);

			// Saving to an archive is a const operation, but must be non-const
			// to support loading. Cast const away here to get bytes written
			const_cast<FHeightField*>(this)->Serialize(ChaosAr);

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

		void SetScale(const FVec3& InScale)
		{
			GeomData.Scale = InScale;
		}

		template<typename InStorageType>
		struct FData
		{
			// For ease of access through typedefs
			using StorageType = InStorageType;

			// Only supporting unsigned int types for the height range - really no difference using
			// this or signed but this is a little nicer overall
			static_assert(TIsSame<StorageType, uint8>::Value || 
				TIsSame<StorageType, uint16>::Value || 
				TIsSame<StorageType, uint32>::Value || 
				TIsSame<StorageType, uint64>::Value,
				"Expected unsigned integer type for heightfield data storage");

			// Data sizes to validate during serialization
			static constexpr int32 RealSize = sizeof(FReal);
			static constexpr int32 StorageSize = sizeof(StorageType);

			// Range of the chosen type (unsigned so Min is always 0)
			static constexpr int32 StorageRange = TNumericLimits<StorageType>::Max();

			// Heights in the chosen format. final placement of the vertex will be at
			// MinValue + Heights[Index] * HeightPerUnit
			// With HeightPerUnit being the range of the min/max FReal values of
			// the heightfield divided by the range of StorageType
			TArray<StorageType> Heights;
			TArray<uint8> MaterialIndices;
			FVec3 Scale;
			FReal MinValue;
			FReal MaxValue;
			uint16 NumRows;
			uint16 NumCols;
			FReal Range;
			FReal HeightPerUnit;

			constexpr FReal GetCellWidth() const
			{
				return Scale[0];
			}

			constexpr FReal GetCellHeight() const
			{
				return Scale[1];
			}

			FORCEINLINE FVec3 GetPoint(int32 Index) const
			{
				const FReal Height = MinValue + Heights[Index] * HeightPerUnit;

				const int32 X = Index % (NumCols);
				const int32 Y = Index / (NumCols);

				return {(FReal)X, (FReal)Y, Height};
			}

			FORCEINLINE FVec3 GetPointScaled(int32 Index) const
			{
				return GetPoint(Index) * Scale;
			}

			FORCEINLINE void GetPoints(int32 Index, FVec3 OutPts[4]) const
			{
				const FReal H0 = MinValue + Heights[Index] * HeightPerUnit;
				const FReal H1 = MinValue + Heights[Index + 1] * HeightPerUnit;
				const FReal H2 = MinValue + Heights[Index + NumCols] * HeightPerUnit;
				const FReal H3 = MinValue + Heights[Index + NumCols + 1] * HeightPerUnit;

				const int32 X = Index % (NumCols);
				const int32 Y = Index / (NumCols);

				OutPts[0] = {(FReal)X, (FReal)Y, H0};
				OutPts[1] = {(FReal)X + 1, (FReal)Y, H1};
				OutPts[2] = {(FReal)X, (FReal)Y + 1, H2};
				OutPts[3] = {(FReal)X + 1, (FReal)Y + 1, H3};
			}

			FORCEINLINE void GetPointsScaled(int32 Index, FVec3 OutPts[4]) const
			{
				GetPoints(Index, OutPts);

				OutPts[0] *= Scale;
				OutPts[1] *= Scale;
				OutPts[2] *= Scale;
				OutPts[3] *= Scale;
			}

			FORCEINLINE FReal GetMinHeight() const
			{
				return MinValue;
			}

			FORCEINLINE FReal GetMaxHeight() const
			{
				return MaxValue;
			}

			void SafeSerializeReal(FChaosArchive& Ar, FReal& RealValue, int32 RuntimeRealSize, int32 SerializedRealSize)
			{
				if (RuntimeRealSize == SerializedRealSize)
				{
					// same sizes all FReal
					Ar << RealValue;
				}
				else 
				{
					// size don't match need to do some conversion
					if (SerializedRealSize == sizeof(float))
					{
						float Value = (float)RealValue;
						Ar << Value;
						RealValue = (FReal)Value;
					}
					else if (SerializedRealSize == sizeof(double))
					{
						double Value = (double)RealValue;
						Ar << Value;
						RealValue = (FReal)Value;
					}
				}
			}

			void Serialize(FChaosArchive& Ar)
			{
				// we need to account for the fact that FReal size may change
				const int32 RuntimeRealSize = RealSize;
				const int32 RunTimeStorageSize = StorageSize;


				int32 SerializedRealSize = RealSize;
				int32 SerializedStorageSize = StorageSize;

				Ar << SerializedRealSize;
				Ar << SerializedStorageSize;

				if(Ar.IsLoading())
				{
					// we only support float and double as FReal
					checkf(SerializedRealSize == sizeof(float) || SerializedRealSize == sizeof(double), TEXT("Heightfield was serialized with unexpected real type size (expected: 4 or 8, found: %d)"), SerializedRealSize);
					checkf(SerializedStorageSize == RunTimeStorageSize, TEXT("Heightfield was serialized with mismatched storage type size (expected: %d, found: %d)"), RunTimeStorageSize, SerializedStorageSize);
				}
				
				Ar << Heights;
				Ar << Scale;
				SafeSerializeReal(Ar, MinValue, RuntimeRealSize, SerializedRealSize);
				SafeSerializeReal(Ar, MaxValue, RuntimeRealSize, SerializedRealSize);
				Ar << NumRows;
				Ar << NumCols;

				Ar.UsingCustomVersion(FExternalPhysicsCustomObjectVersion::GUID);
				if (Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) >= FExternalPhysicsCustomObjectVersion::HeightfieldData)
				{
					SafeSerializeReal(Ar, Range, RuntimeRealSize, SerializedRealSize);
					SafeSerializeReal(Ar, HeightPerUnit, RuntimeRealSize, SerializedRealSize);

					if (Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) < FExternalPhysicsCustomObjectVersion::HeightfieldImplicitBounds)
					{
						// todo(chaos) this may not matter if the Vector types are handling serialization properly 
						// legacy, need to keep the inner box type as float ( not FReal ) 
						TArray<TBox<float, 3>> CellBounds;
						Ar << CellBounds;
					}
					else if(Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) < FExternalPhysicsCustomObjectVersion::HeightfieldUsesHeightsDirectly)
					{
						// legacy, need to keep the type as float ( not FReal ) 
						TArray<float> OldHeights;
						Ar << OldHeights;
					}
				}

				if(Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) >= FExternalPhysicsCustomObjectVersion::AddedMaterialManager)
				{
					Ar << MaterialIndices;
				}
			}
		};

		using FDataType = FData<uint16>;
		FDataType GeomData;

	private:

		// Struct for 2D bounds and associated operations
		struct FBounds2D
		{
			FVec2 Min;
			FVec2 Max;
			
			FBounds2D()
				: Min(0)
				, Max(0)
			{}

			explicit FBounds2D(const FAABB3& In3DBounds)
			{
				Set(In3DBounds);
			}

			void Set(const FAABB3& In3DBounds)
			{
				Min = {In3DBounds.Min()[0], In3DBounds.Min()[1]};
				Max = {In3DBounds.Max()[0], In3DBounds.Max()[1]};
			}

			FVec2 GetExtent() const
			{
				return Max - Min;
			}

			bool IsInside(const FVec2& InPoint) const
			{
				return InPoint[0] >= Min[0] && InPoint[0] <= Max[0] && InPoint[1] >= Min[1] && InPoint[1] <= Max[1];
			}

			FVec2 Clamp(const FVec2& InToClamp, FReal InNudge = SMALL_NUMBER) const
			{
				const FVec2 NudgeVec(InNudge, InNudge);
				const FVec2 TestMin = Min + NudgeVec;
				const FVec2 TestMax = Max - NudgeVec;

				FVec2 OutVec = InToClamp;

				OutVec[0] = FMath::Max(OutVec[0], TestMin[0]);
				OutVec[1] = FMath::Max(OutVec[1], TestMin[1]);

				OutVec[0] = FMath::Min(OutVec[0], TestMax[0]);
				OutVec[1] = FMath::Min(OutVec[1], TestMax[1]);

				return OutVec;
			}

			bool IntersectLine(const FVec2& InStart, const FVec2& InEnd)
			{
				if(IsInside(InStart) || IsInside(InEnd))
				{
					return true;
				}

				const FVec2 Extent = GetExtent();
				FReal TA, TB;

				if(Utilities::IntersectLineSegments2D(InStart, InEnd, Min, FVec2(Min[0] + Extent[0], Min[1]), TA, TB)
					|| Utilities::IntersectLineSegments2D(InStart, InEnd, Min, FVec2(Min[0], Min[1] + Extent[1]), TA, TB)
					|| Utilities::IntersectLineSegments2D(InStart, InEnd, Max, FVec2(Max[0] - Extent[0], Max[1]), TA, TB)
					|| Utilities::IntersectLineSegments2D(InStart, InEnd, Max, FVec2(Max[0], Max[1] - Extent[1]), TA, TB))
				{
					return true;
				}

				return false;
			}

			bool ClipLine(const FVec3& InStart, const FVec3& InEnd, FVec2& OutClippedStart, FVec2& OutClippedEnd) const
			{
				FVec2 TempStart(InStart[0], InStart[1]);
				FVec2 TempEnd(InEnd[0], InEnd[1]);

				bool bLineIntersects = ClipLine(TempStart, TempEnd);

				OutClippedStart = TempStart;
				OutClippedEnd = TempEnd;

				return bLineIntersects;
			}

			bool ClipLine(FVec2& InOutStart, FVec2& InOutEnd) const
			{
				
				// Test we don't need to clip at all, quite likely with a heightfield so optimize for it.
				const bool bStartInside = IsInside(InOutStart);
				const bool bEndInside = IsInside(InOutEnd);
				if(bStartInside && bEndInside)
				{
					return true;
				}

				const FVec2 Dir = InOutEnd - InOutStart;

				// Tiny ray not inside so must be outside
				if(Dir.SizeSquared() < 1e-4)
				{
					return false;
				}

				bool bPerpendicular[2];
				FVec2 InvDir;
				for(int Axis = 0; Axis < 2; ++Axis)
				{
					bPerpendicular[Axis] = Dir[Axis] == 0;
					InvDir[Axis] = bPerpendicular[Axis] ? 0 : 1 / Dir[Axis];
				}

				

				if(bStartInside)
				{
					const FReal TimeToExit = ComputeTimeToExit(InOutStart,InvDir);
					InOutEnd = InOutStart + Dir * TimeToExit;
					return true;
				}

				if(bEndInside)
				{
					const FReal TimeToExit = ComputeTimeToExit(InOutEnd,-InvDir);
					InOutStart = InOutEnd - Dir * TimeToExit;
					return true;
				}

				//start and end outside, need to see if we even intersect
				FReal TimesToEnter[2] = {TNumericLimits<FReal>::Max(),TNumericLimits<FReal>::Max()};
				FReal TimesToExit[2] = {TNumericLimits<FReal>::Max(),TNumericLimits<FReal>::Max()};
				
				for(int Axis = 0; Axis < 2; ++Axis)
				{
					if(bPerpendicular[Axis])
					{
						if(InOutStart[Axis] >= Min[Axis] && InOutStart[Axis] <= Max[Axis])
						{
							TimesToEnter[Axis] = 0;
						}
					}
					else
					{
						if(Dir[Axis] > 0)
						{
							if(InOutStart[Axis] <= Max[Axis])
							{
								TimesToEnter[Axis] = FMath::Max<FReal>(Min[Axis] - InOutStart[Axis], 0) * InvDir[Axis];
								TimesToExit[Axis] = (Max[Axis] - InOutStart[Axis])  * InvDir[Axis];
							}
						}
						else if(Dir[Axis] < 0)
						{
							if(InOutStart[Axis] >= Min[Axis])
							{
								TimesToEnter[Axis] = FMath::Max<FReal>(InOutStart[Axis] - Max[Axis],0) * InvDir[Axis];
								TimesToExit[Axis] = (InOutStart[Axis] - Min[Axis]) * InvDir[Axis];
							}
						}
					}
				}

				const FReal TimeToEnter = FMath::Max(FMath::Abs(TimesToEnter[0]),FMath::Abs(TimesToEnter[1]));
				const FReal TimeToExit = FMath::Min(FMath::Abs(TimesToExit[0]),FMath::Abs(TimesToExit[1]));

				if(TimeToExit < TimeToEnter)
				{
					//no intersection
					return false;
				}

				InOutEnd = InOutStart + Dir * TimeToExit;
				InOutStart = InOutStart + Dir * TimeToEnter;
				return true;
			}

		private:
			//This helper assumes Start is inside the min/max box and uses InvDir to compute how long it takes to exit
			FReal ComputeTimeToExit(const FVec2& Start,const FVec2& InvDir) const
			{
				FReal Times[2] ={TNumericLimits<FReal>::Max(),TNumericLimits<FReal>::Max()};
				for(int Axis = 0; Axis < 2; ++Axis)
				{
					if(InvDir[Axis] > 0)
					{
						Times[Axis] = (Max[Axis] - Start[Axis]) * InvDir[Axis];
					}
					else if(InvDir[Axis] < 0)
					{
						Times[Axis] = (Start[Axis] - Min[Axis]) * InvDir[Axis];
					}
				}

				const FReal MinTime = FMath::Min(FMath::Abs(Times[0]),FMath::Abs(Times[1]));
				return MinTime;
			}
		};

		// Helpers for accessing bounds
		bool GetCellBounds2D(const TVec2<int32> InCoord, FBounds2D& OutBounds, const FVec2& InInflate = {0}) const;
		bool GetCellBounds3D(const TVec2<int32> InCoord, FVec3& OutMin, FVec3& OutMax, const FVec3& InInflate = FVec3(0)) const;
		bool GetCellBounds2DScaled(const TVec2<int32> InCoord, FBounds2D& OutBounds, const FVec2& InInflate = {0}) const;
		bool GetCellBounds3DScaled(const TVec2<int32> InCoord, FVec3& OutMin, FVec3& OutMax, const FVec3& InInflate = FVec3(0)) const;
		bool CalcCellBounds3D(const TVec2<int32> InCoord, FVec3& OutMin, FVec3& OutMax, const FVec3& InInflate = FVec3(0)) const;

		// Query functions - sweep, ray, overlap
		template<typename SQVisitor>
		bool GridSweep(const FVec3& StartPoint, const FVec3& Dir, const FReal Length, const FVec3 InHalfExtents, SQVisitor& Visitor) const;
		bool GridCast(const FVec3& StartPoint, const FVec3& Dir, const FReal Length, FHeightfieldRaycastVisitor& Visitor) const;
		bool GetGridIntersections(FBounds2D InFlatBounds, TArray<TVec2<int32>>& OutInterssctions) const;
		
		FBounds2D GetFlatBounds() const;

		// Grid for queries, faster than bounding volumes for heightfields
		TUniformGrid<FReal, 2> FlatGrid;
		// Bounds in 2D of the whole heightfield, to clip queries against
		FBounds2D FlattenedBounds;
		// 3D bounds for the heightfield, for insertion to the scene structure
		FAABB3 LocalBounds;
		// Cached when bounds are requested. Mutable to allow GetBounds to be logical const
		mutable FAABB3 CachedBounds;

		void CalcBounds();
		void BuildQueryData();

		// Needed for serialization
		FHeightField() : FImplicitObject(EImplicitObject::HasBoundingBox, ImplicitObjectType::HeightField) {}
		friend FImplicitObject;

		template <typename QueryGeomType>
		bool OverlapGeomImp(const QueryGeomType& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FMTDInfo* OutMTD = nullptr) const;

		template <typename QueryGeomType>
		bool SweepGeomImp(const QueryGeomType& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, const FReal Thickness, bool bComputeMTD) const;

		template <typename GeomType>
		bool GJKContactPointImp(const GeomType& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FVec3& ContactLocation, FVec3& ContactNormal, FReal& ContactPhi) const;

	};
}
