// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Core.h"
#include "Chaos/ConvexFlattenedArrayStructureData.h"
#include "Chaos/ConvexHalfEdgeStructureData.h"
#include "ChaosArchive.h"
#include "ChaosCheck.h"
#include "ChaosLog.h"
#include "UObject/PhysicsObjectVersion.h"
#include "UObject/FortniteMainBranchObjectVersion.h"

namespace Chaos
{

	// Metadata for a convex shape used by the manifold generation system and anything
	// else that can benefit from knowing which vertices are associated with the faces.
	class CHAOS_API FConvexStructureData
	{
	public:
		using FConvexStructureDataLarge = FConvexHalfEdgeStructureDataS32;
		using FConvexStructureDataMedium = FConvexHalfEdgeStructureDataS16;
		using FConvexStructureDataSmall = FConvexHalfEdgeStructureDataU8;

		// Note: this is serialized (do not change order without adding a new ObjectVersion and legacy load path)
		enum class EIndexType : int8
		{
			None,
			Small,
			Medium,
			Large,
		};

		// Cats the inner data container to the correct index size
		// Do not use - only public for unit tests
		const FConvexStructureDataLarge& DataL() const { checkSlow(IndexType == EIndexType::Large); return *Data.DataL; }
		const FConvexStructureDataMedium& DataM() const { checkSlow(IndexType == EIndexType::Medium); return *Data.DataM; }
		const FConvexStructureDataSmall& DataS() const { checkSlow(IndexType == EIndexType::Small); return *Data.DataS; }

	private:
		FConvexStructureDataLarge& DataL() { checkSlow(IndexType == EIndexType::Large); return *Data.DataL; }
		FConvexStructureDataMedium& DataM() { checkSlow(IndexType == EIndexType::Medium); return *Data.DataM; }
		FConvexStructureDataSmall& DataS() { checkSlow(IndexType == EIndexType::Small); return *Data.DataS; }

		// Call a function on the read-only data (cast to Small, Medium or Large indices as appropriate).
		// It will call "Op" on the down-casted data container if we have a valid container. It will
		// call EmptyOp if the container has not been set up (IndexType is None).
		// Note: This expands into a switch statement, calling the lambda with a container of each index size.
		//
		// Must be used with an auto lambda to handle the downcasting, e.g.:
		//		int32 Plane0VertexCount = ConstDataOp(
		//			[](const auto& ConcreteData) { return ConcreteData.NumPlaneVertices(0); },
		//			[]() { return 0; });
		//
		template<typename T_OP, typename T_EMPTYOP>
		auto ConstDataOp(const T_OP& Op, const T_EMPTYOP& EmptyOp) const
		{
			switch (IndexType)
			{
			case EIndexType::Small:
				return Op(DataS());
			case EIndexType::Medium:
				return Op(DataM());
			case EIndexType::Large:
				return Op(DataL());
			}
			return EmptyOp();
		}

		// A write-enabled version of ConstDataOp
		template<typename T_OP, typename T_EMPTYOP>
		auto NonConstDataOp(const T_OP& Op, const T_EMPTYOP& EmptyOp)
		{
			switch (IndexType)
			{
			case EIndexType::Small:
				return Op(DataS());
			case EIndexType::Medium:
				return Op(DataM());
			case EIndexType::Large:
				return Op(DataL());
			}
			return EmptyOp();
		}

	public:

		FConvexStructureData()
			: Data{ nullptr }
			, IndexType(EIndexType::None)
		{
		}

		FConvexStructureData(const FConvexStructureData& Other) = delete;

		FConvexStructureData(FConvexStructureData&& Other)
		{
			*this = MoveTemp(Other);
		}

		~FConvexStructureData()
		{
			DestroyDataContainer();
		}

		FConvexStructureData& operator=(const FConvexStructureData& Other) = delete;

		FConvexStructureData& operator=(FConvexStructureData&& Other)
		{
			Data.Ptr = Other.Data.Ptr;
			IndexType = Other.IndexType;

			Other.Data.Ptr = nullptr;
			Other.IndexType = EIndexType::None;

			return *this;
		}


		bool IsValid() const
		{
			return (Data.Ptr != nullptr);
		}

		EIndexType GetIndexType() const
		{
			return IndexType;
		}

		int32 FindVertexPlanes(int32 VertexIndex, int32* VertexPlanes, int32 MaxVertexPlanes) const
		{
			return ConstDataOp(
				[VertexIndex, VertexPlanes, MaxVertexPlanes](const auto& ConcreteData) { return ConcreteData.FindVertexPlanes(VertexIndex, VertexPlanes, MaxVertexPlanes); },
				[]() { return 0; });
		}

		// The number of vertices that make up the corners of the specified face
		int32 NumPlaneVertices(int32 PlaneIndex) const
		{
			return ConstDataOp(
				[PlaneIndex](const auto& ConcreteData) { return ConcreteData.NumPlaneVertices(PlaneIndex); },
				[]() { return 0; });
		}

		// Get the vertex index (in the outer convex container) of one of the vertices making up the corners of the specified face
		int32 GetPlaneVertex(int32 PlaneIndex, int32 PlaneVertexIndex) const
		{
			checkSlow(IsValid());

			return ConstDataOp(
				[PlaneIndex, PlaneVertexIndex](const auto& ConcreteData) { return ConcreteData.GetPlaneVertex(PlaneIndex, PlaneVertexIndex); },
				[]() { return (int32)INDEX_NONE; });
		}

		int32 NumEdges() const
		{
			return ConstDataOp(
				[](const auto& ConcreteData) { return ConcreteData.NumEdges(); },
				[]() { return 0; });
		}

		int32 GetEdgeVertex(int32 EdgeIndex, int32 EdgeVertexIndex) const
		{
			return ConstDataOp(
				[EdgeIndex, EdgeVertexIndex](const auto& ConcreteData)
				{
					return ConcreteData.GetEdgeVertex(EdgeIndex, EdgeVertexIndex);
				},
				[]() { return (int32)INDEX_NONE; });
		}

		int32 GetEdgePlane(int32 EdgeIndex, int32 EdgePlaneIndex) const
		{
			return ConstDataOp(
				[EdgeIndex, EdgePlaneIndex](const auto& ConcreteData)
				{
					return ConcreteData.GetEdgePlane(EdgeIndex, EdgePlaneIndex);
				},
				[]() { return (int32)INDEX_NONE; });
		}

		// Initialize the structure data from the set of vertices for each face of the convex
		void SetPlaneVertices(const TArray<TArray<int32>>& InPlaneVertices, int32 NumVerts)
		{
			const EIndexType NewIndexType = GetRequiredIndexType(InPlaneVertices, NumVerts);
			CreateDataContainer(NewIndexType);

			NonConstDataOp(
				[InPlaneVertices, NumVerts](auto& ConcreteData) { ConcreteData.SetPlaneVertices(InPlaneVertices, NumVerts); },
				[]() {});
		}

		void Serialize(FArchive& Ar)
		{
			Ar.UsingCustomVersion(FPhysicsObjectVersion::GUID);
			Ar.UsingCustomVersion(FFortniteMainBranchObjectVersion::GUID);

			const bool bUseHalfEdgeStructureData = Ar.CustomVer(FPhysicsObjectVersion::GUID) >= FPhysicsObjectVersion::ChaosConvexUsesHalfEdges;
			
			// Load and convert the legacy structure if necessary
			if (Ar.IsLoading() && !bUseHalfEdgeStructureData)
			{
				LoadLegacyData(Ar);
				return;
			}

			if (Ar.IsLoading())
			{
				// Create the container with the appropriate index size
				EIndexType NewIndexType;
				Ar << NewIndexType;
				CreateDataContainer(NewIndexType);
			}
			else
			{
				Ar << IndexType;
			}

			// Serialize the container with the correct index type
			NonConstDataOp(
				[&Ar](auto& ConcreteData) { Ar << ConcreteData; },
				[]() {});
		}

		friend FArchive& operator<<(FArchive& Ar, FConvexStructureData& Value)
		{
			Value.Serialize(Ar);
			return Ar;
		}

	private:

		// Load data from an asset saved before we had a proper half-edge data structure
		void LoadLegacyData(FArchive& Ar)
		{
			TArray<TArray<int32>> OldPlaneVertices;
			int32 OldNumVertices = 0;

			Legacy::FLegacyConvexStructureDataLoader::Load(Ar, OldPlaneVertices, OldNumVertices);

			SetPlaneVertices(OldPlaneVertices, OldNumVertices);
		}

		// Determine the minimum index size we need for the specified convex size
		EIndexType GetRequiredIndexType(const TArray<TArray<int32>>& InPlaneVertices, int32 NumVerts)
		{
			if (FConvexStructureDataSmall::CanMake(InPlaneVertices, NumVerts))
			{
				return EIndexType::Small;
			}
			else if (FConvexStructureDataMedium::CanMake(InPlaneVertices, NumVerts))
			{
				return EIndexType::Medium;
			}
			else
			{
				return EIndexType::Large;
			}
		}

		// Create the container to match the desired index size
		void CreateDataContainer(EIndexType InIndexType)
		{
			DestroyDataContainer();

			check(Data.Ptr == nullptr);
			check(IndexType == EIndexType::None);

			if (InIndexType == EIndexType::Large)
			{
				Data.DataL = new FConvexStructureDataLarge();
			}
			else if (InIndexType == EIndexType::Medium)
			{
				Data.DataM = new FConvexStructureDataMedium();
			}
			else if (InIndexType == EIndexType::Small)
			{
				Data.DataS = new FConvexStructureDataSmall();
			}

			IndexType = InIndexType;
		}

		// Destroy the container we created in CreateDataContainer
		void DestroyDataContainer()
		{
			if (Data.Ptr != nullptr)
			{
				check(IndexType != EIndexType::None);

				NonConstDataOp(
					[](auto& ConcreteData) 
					{ 
						delete &ConcreteData;
					},
					[]() {});

				IndexType = EIndexType::None;
				Data.Ptr = nullptr;
			}
		}

		union FStructureData
		{
			void* Ptr;
			FConvexStructureDataLarge* DataL;
			FConvexStructureDataMedium* DataM;
			FConvexStructureDataSmall* DataS;
		};
		FStructureData Data;

		// The index type we require for the structure data
		EIndexType IndexType;
	};

}
