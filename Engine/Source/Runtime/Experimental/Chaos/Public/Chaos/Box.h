// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Templates/UnrealTemplate.h"

#include "Chaos/ImplicitObject.h"
#include "Chaos/AABB.h"
#include "Chaos/Transform.h"
#include "ChaosArchive.h"
#include "UObject/ExternalPhysicsCustomObjectVersion.h"
#include "UObject/ReleaseObjectVersion.h"

namespace Chaos
{
	/*
	 * Axis-aligned box collision geometry. Consists of a core AABB with a margin. The margin should be considered physically part of the
	 * box - it pads the faces and rounds the corners.
	 */
	template<class T, int d>
	class TBox final : public FImplicitObject
	{
	public:
		using FImplicitObject::SignedDistance;
		using FImplicitObject::GetTypeName;

		FORCEINLINE static constexpr EImplicitObjectType StaticType() { return ImplicitObjectType::Box; }

		// This should never be used outside of creating a default for arrays
		FORCEINLINE TBox()
		    : FImplicitObject(EImplicitObject::FiniteConvex, ImplicitObjectType::Box){};
		FORCEINLINE TBox(const TVector<T, d>& Min, const TVector<T, d>& Max)
		    : FImplicitObject(EImplicitObject::FiniteConvex, ImplicitObjectType::Box)
			, AABB(Min, Max)
		{
			//todo: turn back on
			/*for (int Axis = 0; Axis < d; ++Axis)
			{
				ensure(MMin[Axis] <= MMax[Axis]);
			}*/
		}

		/**
		 * Create a box with the specified size and margin (Min and Max is the desired size including the margin). 
		 * The core geometry will be shrunk by the margin.
		 * E.g.,
		 * 	TBox<T, d>(FVec3(-0.5f), FVec3(0.5f), 0.1f);
		 * will be a box with a bounds of size 1.0f on each axis, but the geometry is actually an AABB
		 * of size 0.8 with a margin of 0.1 (and rounded corners).
		 * 
		 * NOTE: If the margin is larger than the smallest box dimension, the box size will be increased to support the margin.
		 */ 
		FORCEINLINE TBox(const TVector<T, d>& InMin, const TVector<T, d>& InMax, FReal InMargin)
			: FImplicitObject(EImplicitObject::FiniteConvex, ImplicitObjectType::Box)
		{
			// Remove Margin from Min and Max
			FVec3 Min = InMin + FVec3(InMargin);
			FVec3 Max = InMax - FVec3(InMargin);

			// If we have a negative extent in any direction, fix it
			const FReal MinDim = KINDA_SMALL_NUMBER;
			const FVec3 NegExtents = 0.5f * TVector<T, d>::Max(Min - Max + MinDim, FVec3(0));	// +ve for any extents less than MinDim including negative
			Min -= NegExtents;
			Max += NegExtents;

			AABB = TAABB<T, d>(Min, Max);
			SetMargin(InMargin);
		}

		FORCEINLINE TBox(const TBox<T, d>& Other)
		    : FImplicitObject(EImplicitObject::FiniteConvex, ImplicitObjectType::Box)
			, AABB(Other.AABB)
		{
			SetMargin(Other.GetMargin());
		}

		FORCEINLINE TBox(TBox<T, d>&& Other)
		    : FImplicitObject(EImplicitObject::FiniteConvex, ImplicitObjectType::Box)
		    , AABB(MoveTemp(Other.AABB))
		{
			SetMargin(Other.GetMargin());
		}

		FORCEINLINE TBox<T, d>& operator=(const TBox<T, d>& Other)
		{
			this->Type = Other.Type;
			this->bIsConvex = Other.bIsConvex;
			this->bDoCollide = Other.bDoCollide;
			this->bHasBoundingBox = Other.bHasBoundingBox;

			AABB = Other.AABB;
			SetMargin(Other.GetMargin());
			return *this;
		}

		FORCEINLINE TBox<T, d>& operator=(TBox<T, d>&& Other)
		{
			this->Type = Other.Type;
			this->bIsConvex = Other.bIsConvex;
			this->bDoCollide = Other.bDoCollide;
			this->bHasBoundingBox = Other.bHasBoundingBox;

			AABB = MoveTemp(Other.AABB);
			SetMargin(Other.GetMargin());
			return *this;
		}

		virtual ~TBox() {}

		virtual TUniquePtr<FImplicitObject> Copy() const override
		{
			return TUniquePtr<FImplicitObject>(new TBox<T,d>(*this));
		}

		/**
		 * The core shape use for low-level collision detection. This is the box reduce by the margin size.
		 */
		const TAABB<T, d>& GetCore() const
		{
			return AABB;
		}

		/**
		 * Returns sample points centered about the origin.
		 */
		TArray<TVector<T, d>> ComputeLocalSamplePoints() const
		{
			return BoundingBox().ComputeLocalSamplePoints();
		}

		/**
		 * Returns sample points at the current location of the box.
		 */
		TArray<TVector<T, d>> ComputeSamplePoints() const
		{
			return BoundingBox().ComputeSamplePoints();
		}

		FORCEINLINE bool Contains(const TVector<T, d>& Point) const
		{
			return BoundingBox().Contains(Point);
		}

		FORCEINLINE bool Contains(const TVector<T, d>& Point, const T Tolerance) const
		{
			return BoundingBox().Contains(Point, Tolerance);
		}

		// Minimum extents, including margin
		FORCEINLINE const TVector<T, d> Min() const
		{
			return AABB.Min() - TVector<T, d>(GetMargin());
		}
		
		// Maximum extents, including margin
		FORCEINLINE const TVector<T, d> Max() const
		{ 
			return AABB.Max() + TVector<T, d>(GetMargin());
		}

		// Extents, including margin
		FORCEINLINE const TAABB<T, d> BoundingBox() const
		{
			return TAABB<T, d>(Min(), Max());
		}

		// Return the distance and normal is the closest point on the surface to Pos. Negative for penetration.
		virtual T PhiWithNormal(const TVector<T, d>& Pos, TVector<T, d>& Normal) const override
		{
			return AABB.PhiWithNormal(Pos, Normal) - GetMargin();
		}

		static FORCEINLINE bool RaycastFast(const TVector<T,d>& InMin, const TVector<T,d>& InMax, const TVector<T, d>& StartPoint, const TVector<T, d>& Dir, const TVector<T, d>& InvDir, const bool* bParallel, const T Length, const T InvLength, T& OutTime, TVector<T, d>& OutPosition)
		{
			return TAABB<T, d>(InMin, InMax).RaycastFast(StartPoint, Dir, InvDir, bParallel, Length, InvLength, OutTime, OutPosition);
		}

		virtual bool CHAOS_API Raycast(const TVector<T, d>& StartPoint, const TVector<T, d>& Dir, const T Length, const T Thickness, T& OutTime, TVector<T, d>& OutPosition, TVector<T, d>& OutNormal, int32& OutFaceIndex) const override
		{
			if (AABB.Raycast(StartPoint, Dir, Length, Thickness + GetMargin(), OutTime, OutPosition, OutNormal, OutFaceIndex))
			{
				// The AABB Raycast treats thickness as the ray thickness, so correct the position for the box margin
				OutPosition += GetMargin() * OutNormal;
				return true;
			}
			return false;
		}

		TVector<T, d> FindClosestPoint(const TVector<T, d>& StartPoint, const T Thickness = (T)0) const
		{
			return AABB.FindClosestPoint(StartPoint, Thickness + GetMargin());
		}

		virtual Pair<TVector<T, d>, bool> FindClosestIntersectionImp(const TVector<T, d>& StartPoint, const TVector<T, d>& EndPoint, const T Thickness) const override
		{
			return AABB.FindClosestIntersectionImp(StartPoint, EndPoint, Thickness + GetMargin());
		}

		virtual TVector<T, d> FindGeometryOpposingNormal(const TVector<T, d>& DenormDir, int32 FaceIndex, const TVector<T, d>& OriginalNormal) const override
		{
			return AABB.FindGeometryOpposingNormal(DenormDir, FaceIndex, OriginalNormal);
		}

		// Get the index of the plane that most opposes the normal
		int32 GetMostOpposingPlane(const FVec3& Normal) const
		{
			int32 AxisIndex = FVec3(Normal.GetAbs()).MaxAxis();
			if (Normal[AxisIndex] > 0.0f)
			{
				AxisIndex += 3;
			}
			return AxisIndex;
		}

		// Get the index of the plane that most opposes the normal (VertexIndex is ignored)
		int32 GetMostOpposingPlaneWithVertex(int32 VertexIndex, const FVec3& Normal) const
		{
			return GetMostOpposingPlane(Normal);
		}

		// Get the set of planes that pass through the specified vertex
		TArrayView<const int32> GetVertexPlanes(int32 VertexIndex) const
		{
			return MakeArrayView(SVertexPlanes[VertexIndex]);
		}

		// Get the list of vertices that form the boundary of the specified face
		TArrayView<const int32> GetPlaneVertices(int32 FaceIndex) const
		{
			return MakeArrayView(SPlaneVertices[FaceIndex]);
		}

		int32 NumPlanes() const { return 6; }

		int32 NumVertices() const { return 8; }

		// Get the plane at the specified index (e.g., indices from GetVertexPlanes)
		const TPlaneConcrete<FReal, 3> GetPlane(int32 FaceIndex) const
		{
			const FVec3& PlaneN = SNormals[FaceIndex];
			const FVec3 PlaneX = AABB.Center() + 0.5f * (PlaneN * AABB.Extents());
			return TPlaneConcrete<FReal, 3>(PlaneX, PlaneN);
		}

		// Get the vertex at the specified index (e.g., indices from GetPlaneVertices)
		const FVec3 GetVertex(int32 VertexIndex) const
		{
			const FVec3& Vertex = SVertices[VertexIndex];
			return AABB.Center() + 0.5f * (Vertex * AABB.Extents());
		}

		// Returns a position on the outer shape including the margin
		FORCEINLINE TVector<T, d> Support(const TVector<T, d>& Direction, const T Thickness) const
		{
			return AABB.Support(Direction, GetMargin() + Thickness);
		}

		// Returns a position on the core shape excluding the margin
		FORCEINLINE TVector<T, d> SupportCore(const TVector<T, d>& Direction) const
		{
			return AABB.SupportCore(Direction);
		}

		FORCEINLINE TVector<T, d> Center() const { return AABB.Center(); }
		FORCEINLINE TVector<T, d> GetCenter() const { return AABB.GetCenter(); }
		FORCEINLINE TVector<T, d> GetCenterOfMass() const { return AABB.GetCenterOfMass(); }
		FORCEINLINE TVector<T, d> Extents() const { return AABB.Extents() + TVector<T, d>(2.0f * GetMargin()); }

		int LargestAxis() const
		{
			return AABB.LargestAxis();
		}

		// Area of the box including margin, but treating corners as square rather than rounded
		FORCEINLINE T GetArea() const { return BoundingBox().GetArea(); }

		// Volume of the box including margin, but treating corners as square rather than rounded
		FORCEINLINE T GetVolume() const { return BoundingBox().GetVolume(); }

		FORCEINLINE PMatrix<T, d, d> GetInertiaTensor(const T Mass) const { return GetInertiaTensor(Mass, Extents()); }
		FORCEINLINE static PMatrix<T, 3, 3> GetInertiaTensor(const T Mass, const TVector<T, 3>& Dim)
		{
			// https://www.wolframalpha.com/input/?i=cuboid
			const T M = Mass / 12;
			const T WW = Dim[0] * Dim[0];
			const T HH = Dim[1] * Dim[1];
			const T DD = Dim[2] * Dim[2];
			return PMatrix<T, 3, 3>(M * (HH + DD), M * (WW + DD), M * (WW + HH));
		}


		FORCEINLINE static TRotation<T, d> GetRotationOfMass()
		{
			return TAABB<T, d>::GetRotationOfMass();
		}

		virtual FString ToString() const { return FString::Printf(TEXT("TAABB Min:%s, Max:%s, Margin:%f"), *Min().ToString(), *Max().ToString(), GetMargin()); }

		FORCEINLINE void SerializeImp(FArchive& Ar)
		{
			FImplicitObject::SerializeImp(Ar);
			AABB.Serialize(Ar);

			Ar.UsingCustomVersion(FReleaseObjectVersion::GUID);
			if (Ar.CustomVer(FReleaseObjectVersion::GUID) >= FReleaseObjectVersion::MarginAddedToConvexAndBox)
			{
				Ar << FImplicitObject::Margin;
			}
		}

		// Some older classes used to use a TBox as a bounding box, but now use a TAABB. However we still need
		// to be able to read the older files, so those older classes should use TBox::SerializeAsAABB and not TAABB::Serialize
		static void SerializeAsAABB(FArchive& Ar, TAABB<T,d>& AABB)
		{
			Ar.UsingCustomVersion(FExternalPhysicsCustomObjectVersion::GUID);
			if (Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) < FExternalPhysicsCustomObjectVersion::TBoxReplacedWithTAABB)
			{
				TBox<T, d> Tmp;
				Ar << Tmp;
				AABB = Tmp.AABB;
			}
			else
			{
				AABB.Serialize(Ar);
			}
		}

		// See comments on SerializeAsAABB
		static void SerializeAsAABBs(FArchive& Ar, TArray<TAABB<T, d>>& AABBs)
		{
			Ar.UsingCustomVersion(FExternalPhysicsCustomObjectVersion::GUID);
			if (Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) < FExternalPhysicsCustomObjectVersion::TBoxReplacedWithTAABB)
			{
				TArray<TBox<T, d>> Tmp;
				Ar << Tmp;
				AABBs.Reserve(Tmp.Num());
				for (const TBox<T, d>& Box : Tmp)
				{
					AABBs.Add(Box.AABB);
				}
			}
			else
			{
				Ar << AABBs;
			}
		}

		// See comments on SerializeAsAABB
		template <typename Key>
		static void SerializeAsAABBs(FArchive& Ar, TMap<Key, TAABB<T, d>>& AABBs)
		{
			Ar.UsingCustomVersion(FExternalPhysicsCustomObjectVersion::GUID);
			if (Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) < FExternalPhysicsCustomObjectVersion::TBoxReplacedWithTAABB)
			{
				TMap<Key,TBox<T, d>> Tmp;
				Ar << Tmp;

				for (const auto& Itr : Tmp)
				{
					AABBs.Add(Itr.Key, Itr.Value.AABB);
				}
			}
			else
			{
				Ar << AABBs;
			}
		}

		virtual void Serialize(FChaosArchive& Ar) override
		{
			FChaosArchiveScopedMemory ScopedMemory(Ar, GetTypeName());
			SerializeImp(Ar);
		}

		virtual void Serialize(FArchive& Ar) override { SerializeImp(Ar); }

		virtual uint32 GetTypeHash() const override 
		{
			return AABB.GetTypeHash();
		}

		//const TAABB<T, d>& GetAABB() const { return AABB; }

	private:
		TAABB<T, d> AABB;

		// Structure data shared by all boxes and used for manifold creation
		static TArray<TArray<int32>> SVertexPlanes;
		static TArray<TArray<int32>> SPlaneVertices;
		static TArray<FVec3> SNormals;
		static TArray<FVec3> SVertices;
	};
}
