// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CADKernel/Core/OrientedEntity.h"
#include "CADKernel/Geo/Curves/CompositeCurve.h"
#include "CADKernel/Geo/GeoEnum.h"
#include "CADKernel/Topo/TopologicalEdge.h"
#include "CADKernel/Topo/TopologicalEntity.h"
#include "CADKernel/Topo/TopologicalVertex.h"

namespace CADKernel
{

	class FTopologicalFace;
	class FTopologicalVertex;

	class CADKERNEL_API FOrientedEdge : public TOrientedEntity<FTopologicalEdge>
	{
	public:
		FOrientedEdge(TSharedPtr<FTopologicalEdge>& InEntity, EOrientation InDirection)
			: TOrientedEntity(InEntity, InDirection)
		{
		}

		FOrientedEdge(const FOrientedEdge& OrientiredEntity)
			: TOrientedEntity(OrientiredEntity)
		{
		}

		FOrientedEdge()
			: TOrientedEntity()
		{
		}

		bool operator==(const FOrientedEdge& Edge) const
		{
			return Entity == Edge.Entity;
		}

	};

	class CADKERNEL_API FTopologicalLoop : public FTopologicalEntity
	{
		friend class FEntity;
		friend class FTopologicalFace;
		friend class FTopologicalFace;
		friend class FTopologicalEdge;

	public:
		FSurfacicBoundary Boundary;

	protected:
		TArray<FOrientedEdge> Edges;

		TWeakPtr<FTopologicalFace> Face;
		bool bExternalLoop;

		FTopologicalLoop(const TArray<TSharedPtr<FTopologicalEdge>>& Edges, const TArray<EOrientation>& EdgeDirections);

		FTopologicalLoop(FCADKernelArchive& Archive)
			: FTopologicalEntity()
		{
			Serialize(Archive);
		}

	private:

		void SetSurface(TSharedRef<FTopologicalFace> NewDomain)
		{
			Face = NewDomain;
		}

		void ResetSurface()
		{
			Face.Reset();
		}

	public:

		~FTopologicalLoop() = default;

		static TSharedPtr<FTopologicalLoop> Make(const TArray<TSharedPtr<FTopologicalEdge>>& EdgeList, const TArray<EOrientation>& EdgeDirections, const double GeometricTolerance);

		virtual void Serialize(FCADKernelArchive& Ar) override
		{
			FTopologicalEntity::Serialize(Ar);
			SerializeIdents(Ar, (TArray<TOrientedEntity<FEntity>>&) Edges);
			SerializeIdent(Ar, Face);
			Ar << bExternalLoop;
		}

		virtual void SpawnIdent(FDatabase& Database) override
		{
			if (!FEntity::SetId(Database))
			{
				return;
			}

			SpawnIdentOnEntities((TArray<TOrientedEntity<FEntity>>&) Edges, Database);
		}

		virtual void ResetMarkersRecursively() override
		{
			ResetMarkers();
			ResetMarkersRecursivelyOnEntities((TArray<TOrientedEntity<FEntity>>&) Edges);
		}

#ifdef CADKERNEL_DEV
		virtual FInfoEntity& GetInfo(FInfoEntity&) const override;
#endif

		virtual EEntity GetEntityType() const override
		{
			return EEntity::TopologicalLoop;
		}

		const int32 EdgeCount() const
		{
			return Edges.Num();
		}

		const TArray<FOrientedEdge>& GetEdges() const
		{
			return Edges;
		}

		TArray<FOrientedEdge>& GetEdges()
		{
			return Edges;
		}

		TSharedRef<FTopologicalFace> GetFace() const
		{
			return Face.Pin().ToSharedRef();
		}

		void Orient();
		void SwapOrientation();

		void SetAsInnerBoundary()
		{
			bExternalLoop = false;
		}

		void ReplaceEdge(TSharedPtr<FTopologicalEdge>& OldEdge, TSharedPtr<FTopologicalEdge>& NewEdge);
		void ReplaceEdge(TSharedPtr<FTopologicalEdge>& Edge, TArray<TSharedPtr<FTopologicalEdge>>& NewEdges);
		void ReplaceEdges(TArray<FOrientedEdge>& Candidates, TSharedPtr<FTopologicalEdge>& NewEdge);

		/**
		 * The Edge is split in two edges : Edge + NewEdge 
		 * @param bNewEdgeIsFirst == true => StartVertex Connected to Edge, EndVertexConnected to NewEdge
		 * According to the direction of Edge, if bNewEdgeIsFirst == true, NewEdge is added in the loop after (EOrientation::Front) or before (EOrientation::Back)
		 */
		void SplitEdge(TSharedPtr<FTopologicalEdge> Edge, TSharedPtr<FTopologicalEdge> NewEdge, bool bNewEdgeIsFirst);

		void RemoveEdge(TSharedPtr<FTopologicalEdge>& Edge);
		//void ReplaceEdgesWithMergedEdge(TArray<TSharedPtr<FTopologicalEdge>>& OldEdges, TSharedPtr<FTopologicalVertex>& MiddleVertex, TSharedPtr<FTopologicalEdge>& NewEdge);

		EOrientation GetDirection(TSharedPtr<FTopologicalEdge>& Edge, bool bAllowLinkedEdge = false) const;

		EOrientation GetDirection(int32 Index) const
		{
			return Edges[Index].Direction;
		}

		const TSharedPtr<FTopologicalEdge>& GetEdge(int32 Index) const
		{
			return Edges[Index].Entity;
		}

		int32 GetEdgeIndex(const TSharedPtr<FTopologicalEdge>& Edge) const
		{
			for (int32 Index = 0; Index < Edges.Num(); ++Index)
			{
				if (Edge == Edges[Index].Entity)
				{
					return Index;
				}
			}
			return -1;
		}

		void Get2DSampling(TArray<FPoint2D>& LoopSampling);

		void FindSurfaceCorners(TArray<TSharedPtr<FTopologicalVertex>>& OutCorners, TArray<int32>& OutStartSideIndex) const;
		void FindBreaks(TArray<TSharedPtr<FTopologicalVertex>>& Ruptures, TArray<int32>& OutStartSideIndex, TArray<double>& RuptureValues) const;

		void ComputeBoundaryProperties(const TArray<int32>& StartSideIndex, TArray<FEdge2DProperties>& OutSideProperties) const;

		void EnsureLogicalClosing(const double GeometricTolerance);
	};
}
