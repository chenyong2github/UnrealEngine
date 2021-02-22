// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ImplicitObject.h"
#include "Chaos/AABB.h"
#include "Chaos/MassProperties.h"
#include "CollisionConvexMesh.h"
#include "ChaosArchive.h"
#include "GJK.h"
#include "ChaosCheck.h"
#include "ChaosLog.h"
#include "UObject/PhysicsObjectVersion.h"
#include "UObject/FortniteMainBranchObjectVersion.h"

namespace Chaos
{
	// Base class for TConvexStructureDataImp.
	// NOTE: Deliberately no-virtual destructor so it should never be deleted from a 
	// base class pointer.
	class CHAOS_API FConvexStructureDataImp
	{
	public:
		FConvexStructureDataImp() {}
		~FConvexStructureDataImp() {}
	};

	// A container for the convex structure data arrays.
	// This is templated on the index type required, which needs to
	// be large enough to hold the max of the number of vertices or 
	// planes on the convex.
	// 
	// T_INDEX:			the type used to index into the convex vertices 
	//					and planes array (in the outer convex). Must be 
	//					able to contain max(NumPlanes, NumVerts).
	//
	// T_OFFSETINDEX:	the type used to index the flattened array of
	//					indices. Must be able to contain 
	//					Max(NumPlanes*AverageVertsPerPlane)
	//
	template<typename T_INDEX, typename T_OFFSETINDEX>
	class CHAOS_API TConvexStructureDataImp : public FConvexStructureDataImp
	{
	public:
		using FIndex = T_INDEX;
		using FOffsetIndex = T_OFFSETINDEX;

		FORCEINLINE bool IsValid() const
		{
			return PlaneVertices.Num() > 0;
		}

		FORCEINLINE int32 NumVertices() const
		{
			return VertexPlanesOffsetCount.Num();
		}

		// The number of planes that use the specified vertex
		FORCEINLINE int32 NumVertexPlanes(int32 VertexIndex) const
		{
			return VertexPlanesOffsetCount[VertexIndex].Value;
		}

		// Get the plane index (in the outer convex container) of one of the planes that uses the specified vertex
		FORCEINLINE int32 GetVertexPlane(int32 VertexIndex, int32 VertexPlaneIndex) const
		{
			check(VertexPlaneIndex < NumVertexPlanes(VertexIndex));

			const int32 VertexPlaneFlatArrayIndex = VertexPlanesOffsetCount[VertexIndex].Key + VertexPlaneIndex;
			return (int32)VertexPlanes[VertexPlaneFlatArrayIndex];
		}

		FORCEINLINE int32 NumPlanes() const
		{
			return PlaneVerticesOffsetCount.Num();
		}

		// The number of vertices that make up the corners of the specified face
		FORCEINLINE int32 NumPlaneVertices(int32 PlaneIndex) const
		{
			return PlaneVerticesOffsetCount[PlaneIndex].Value;
		}

		// Get the vertex index (in the outer convex container) of one of the vertices making up the corners of the specified face
		FORCEINLINE int32 GetPlaneVertex(int32 PlaneIndex, int32 PlaneVertexIndex) const
		{
			check(PlaneVertexIndex < PlaneVerticesOffsetCount[PlaneIndex].Value);

			const int32 PlaneVertexFlatArrayIndex = PlaneVerticesOffsetCount[PlaneIndex].Key + PlaneVertexIndex;
			return (int32)PlaneVertices[PlaneVertexFlatArrayIndex];
		}

		void SetPlaneVertices(const TArray<TArray<int32>>& InPlaneVertices, int32 NumVerts)
		{
			// We flatten the ragged arrays into a single array, and store a seperate
			// arrray of [index,count] tuples to reproduce the ragged arrays.
			Reset();

			// Generate the ragged array [offset,count] tuples
			PlaneVerticesOffsetCount.SetNumZeroed(InPlaneVertices.Num());
			VertexPlanesOffsetCount.SetNumZeroed(NumVerts);

			// Count the number of planes for each vertex (store it in the tuple)
			int FlatArrayIndexCount = 0;
			for (int32 PlaneIndex = 0; PlaneIndex < InPlaneVertices.Num(); ++PlaneIndex)
			{
				FlatArrayIndexCount += InPlaneVertices[PlaneIndex].Num();

				for (int32 PlaneVertexIndex = 0; PlaneVertexIndex < InPlaneVertices[PlaneIndex].Num(); ++PlaneVertexIndex)
				{
					const int32 VertexIndex = InPlaneVertices[PlaneIndex][PlaneVertexIndex];
					VertexPlanesOffsetCount[VertexIndex].Value++;
				}
			}

			// Initialize the flattened arrary offsets and reset the count (we re-increment it when copying the data below)
			int VertexPlanesArrayStart = 0;
			for (int32 VertexIndex = 0; VertexIndex < NumVerts; ++VertexIndex)
			{
				VertexPlanesOffsetCount[VertexIndex].Key = VertexPlanesArrayStart;
				VertexPlanesArrayStart += VertexPlanesOffsetCount[VertexIndex].Value;
				VertexPlanesOffsetCount[VertexIndex].Value = 0;
			}

			// Allocate space for the flattened arrays
			PlaneVertices.SetNumZeroed(FlatArrayIndexCount);
			VertexPlanes.SetNumZeroed(FlatArrayIndexCount);

			// Copy the indices into the flattened arrays
			int32 PlaneVerticesArrayStart = 0;
			for (int32 PlaneIndex = 0; PlaneIndex < InPlaneVertices.Num(); ++PlaneIndex)
			{
				PlaneVerticesOffsetCount[PlaneIndex].Key = PlaneVerticesArrayStart;
				PlaneVerticesOffsetCount[PlaneIndex].Value = InPlaneVertices[PlaneIndex].Num();
				PlaneVerticesArrayStart += InPlaneVertices[PlaneIndex].Num();

				for (int32 PlaneVertexIndex = 0; PlaneVertexIndex < InPlaneVertices[PlaneIndex].Num(); ++PlaneVertexIndex)
				{
					const int32 VertexIndex = InPlaneVertices[PlaneIndex][PlaneVertexIndex];

					const int32 PlaneVertexFlatArrayIndex = PlaneVerticesOffsetCount[PlaneIndex].Key + PlaneVertexIndex;
					PlaneVertices[PlaneVertexFlatArrayIndex] = VertexIndex;

					const int32 VertexPlaneFlatArrayIndex = VertexPlanesOffsetCount[VertexIndex].Key + VertexPlanesOffsetCount[VertexIndex].Value;
					VertexPlanesOffsetCount[VertexIndex].Value++;
					VertexPlanes[VertexPlaneFlatArrayIndex] = PlaneIndex;
				}
			}
		}

		void Reset()
		{
			PlaneVerticesOffsetCount.Reset();
			VertexPlanesOffsetCount.Reset();
			PlaneVertices.Reset();
			VertexPlanes.Reset();
		}

		void Serialize(FArchive& Ar)
		{
			Ar << PlaneVerticesOffsetCount;
			Ar << VertexPlanesOffsetCount;
			Ar << PlaneVertices;
			Ar << VertexPlanes;
		}

		friend FArchive& operator<<(FArchive& Ar, TConvexStructureDataImp<T_INDEX, T_OFFSETINDEX>& Value)
		{
			Value.Serialize(Ar);
			return Ar;
		}


		// Array of [offset, count] for each plane that gives the set of indices in the PlaneVertices flattened array
		TArray<TPair<FOffsetIndex, FIndex>> PlaneVerticesOffsetCount;

		// Array of [offset, count] for each vertex that gives the set of indices in the VertexPlanes flattened array
		TArray<TPair<FOffsetIndex, FIndex>> VertexPlanesOffsetCount;

		// A flattened ragged array. For each plane: the set of vertex indices that form the corners of the face in counter-clockwise order
		TArray<FIndex> PlaneVertices;

		// A flattened ragged array. For each vertex: the set of plane indices that use the vertex
		TArray<FIndex> VertexPlanes;
	};

	using FConvexStructureDataS32 = TConvexStructureDataImp<int32, int32>;
	using FConvexStructureDataU8 = TConvexStructureDataImp<uint8, uint16>;

	// Metadata for a convex shape used by the manifold generation system and anything
	// else that can benefit from knowing which vertices are associated with the faces.
	class CHAOS_API FConvexStructureData
	{
	private:
		FORCEINLINE FConvexStructureDataS32& Data32() { return static_cast<FConvexStructureDataS32&>(*Data); }
		FORCEINLINE FConvexStructureDataU8& Data8() { return static_cast<FConvexStructureDataU8&>(*Data); }

	public:
		enum class EIndexType : int8
		{
			None,
			S32,
			U8,
		};

		FConvexStructureData()
			: Data(nullptr)
			, IndexType(EIndexType::None)
		{
		}

		~FConvexStructureData()
		{
			DestroyDataContainer();
		}

		FORCEINLINE bool IsValid() const
		{
			return (Data != nullptr);
		}

		FORCEINLINE EIndexType GetIndexType() const
		{
			return IndexType;
		}

		// Only exposed for unit tests
		FORCEINLINE const FConvexStructureDataS32& Data32() const
		{
			checkSlow(IndexType == EIndexType::S32);
			return static_cast<FConvexStructureDataS32&>(*Data);
		}

		// Only exposed for unit tests
		FORCEINLINE const FConvexStructureDataU8& Data8() const
		{
			checkSlow(IndexType == EIndexType::U8);
			return static_cast<FConvexStructureDataU8&>(*Data);
		}

		// The number of planes that use the specified vertex
		FORCEINLINE int32 NumVertexPlanes(int32 VertexIndex) const
		{
			if (IndexType == EIndexType::S32)
			{
				return Data32().NumVertexPlanes(VertexIndex);
			}
			else if (IndexType == EIndexType::U8)
			{
				return Data8().NumVertexPlanes(VertexIndex);
			}
			else
			{
				return 0;
			}
		}

		// Get the plane index (in the outer convex container) of one of the planes that uses the specified vertex
		FORCEINLINE int32 GetVertexPlane(int32 VertexIndex, int32 VertexPlaneIndex) const
		{
			checkSlow(IsValid());
			if (IndexType == EIndexType::S32)
			{
				return Data32().GetVertexPlane(VertexIndex, VertexPlaneIndex);
			}
			else
			{
				return Data8().GetVertexPlane(VertexIndex, VertexPlaneIndex);
			}
		}

		// The number of vertices that make up the corners of the specified face
		FORCEINLINE int32 NumPlaneVertices(int32 PlaneIndex) const
		{
			if (IndexType == EIndexType::S32)
			{
				return Data32().NumPlaneVertices(PlaneIndex);
			}
			else if (IndexType == EIndexType::U8)
			{
				return Data8().NumPlaneVertices(PlaneIndex);
			}
			else
			{
				return 0;
			}
		}

		// Get the vertex index (in the outer convex container) of one of the vertices making up the corners of the specified face
		FORCEINLINE int32 GetPlaneVertex(int32 PlaneIndex, int32 PlaneVertexIndex) const
		{
			checkSlow(IsValid());
			if (IndexType == EIndexType::S32)
			{
				return Data32().GetPlaneVertex(PlaneIndex, PlaneVertexIndex);
			}
			else
			{
				return Data8().GetPlaneVertex(PlaneIndex, PlaneVertexIndex);
			}
		}

		// Initialize the structure data from the set of vertices for each face of the convex
		void SetPlaneVertices(const TArray<TArray<int32>>& InPlaneVertices, int32 NumVerts)
		{
			const EIndexType NewIndexType = GetRequiredIndexType(InPlaneVertices.Num(), NumVerts);
			CreateDataContainer(NewIndexType);

			if (IndexType == EIndexType::S32)
			{
				Data32().SetPlaneVertices(InPlaneVertices, NumVerts);
			}
			else if (IndexType == EIndexType::U8)
			{
				Data8().SetPlaneVertices(InPlaneVertices, NumVerts);
			}
		}

		void Serialize(FArchive& Ar)
		{
			Ar.UsingCustomVersion(FPhysicsObjectVersion::GUID);
			Ar.UsingCustomVersion(FFortniteMainBranchObjectVersion::GUID);

			bool bUseVariableSizeStructureDataUE4 = Ar.CustomVer(FPhysicsObjectVersion::GUID) >= FPhysicsObjectVersion::VariableConvexStructureData;
			bool bUseVariableSizeStructureDataFN = Ar.CustomVer(FFortniteMainBranchObjectVersion::GUID) >= FFortniteMainBranchObjectVersion::ChaosConvexVariableStructureDataAndVerticesArray;
			bool bUseVariableSizeStructureData = bUseVariableSizeStructureDataUE4 || bUseVariableSizeStructureDataFN;

			if (!bUseVariableSizeStructureData)
			{
				// Orginal structure used 32bit indices regardless of max index, and they were store in ragged TArray<TArray<int32>>
				TArray<TArray<int32>> OldPlaneVertices;
				TArray<TArray<int32>> OldVertexPlanes;
				Ar << OldPlaneVertices;
				Ar << OldVertexPlanes;
				SetPlaneVertices(OldPlaneVertices, OldVertexPlanes.Num());
				return;
			}

			if (Ar.IsLoading())
			{
				EIndexType NewIndexType;
				Ar << NewIndexType;
				CreateDataContainer(NewIndexType);
			}
			else
			{
				Ar << IndexType;
			}

			if (IndexType == EIndexType::S32)
			{
				Ar << Data32();
			}
			else if (IndexType == EIndexType::U8)
			{
				Ar << Data8();
			}
		}

		friend FArchive& operator<<(FArchive& Ar, FConvexStructureData& Value)
		{
			Value.Serialize(Ar);
			return Ar;
		}

	private:
		// Determine the minimum index size we need for the specified convex size
		EIndexType GetRequiredIndexType(int32 NumPlanes, int32 NumVerts)
		{
			if ((NumPlanes > 255) || (NumVerts > 255))
			{
				return EIndexType::S32;
			}
			else
			{
				return EIndexType::U8;
			}
		}

		// Create the container to match the desired index size
		void CreateDataContainer(EIndexType InIndexType)
		{
			DestroyDataContainer();

			check(Data == nullptr);
			check(IndexType == EIndexType::None);

			if (InIndexType == EIndexType::S32)
			{
				Data = new TConvexStructureDataImp<int32, int32>();
			}
			else if (InIndexType == EIndexType::U8)
			{
				Data = new TConvexStructureDataImp<uint8, uint16>();
			}

			IndexType = InIndexType;
		}

		// Destroy the container we created in CreateDataContainer
		void DestroyDataContainer()
		{
			if (Data != nullptr)
			{
				check(IndexType != EIndexType::None);

				if (IndexType == EIndexType::S32)
				{
					delete &Data32();
				}
				else if (IndexType == EIndexType::U8)
				{
					delete &Data8();
				}

				Data = nullptr;
				IndexType = EIndexType::None;
			}
		}

		// A pointer to the data base class which has no API. Must be downcast 
		// using Data32() or Data8() depending on IndexType.
		// Note: not using TUniquePtr here because we don't want a vptr in FConvexStructureDataImp
		FConvexStructureDataImp* Data;

		// The index type we require for the structure data
		EIndexType IndexType;
	};

}
