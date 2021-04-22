// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CADKernel/Math/Point.h"
#include "CADKernel/Topo/Linkable.h"
#include "CADKernel/Topo/TopologicalLink.h"

namespace CADKernel
{
	class FModelMesh;
	class FTopologicalEdge;
	class FVertexMesh;

	class CADKERNEL_API FTopologicalVertex : public TLinkable<FTopologicalVertex, FVertexLink>
	{
		friend class FEntity;
		friend class FTopologicalEdge;
		friend class FVertexLink;

	protected:

		TArray<TWeakPtr<FTopologicalEdge>> ConnectedEdges;
		FPoint Coordinates;
		TSharedPtr<FVertexMesh> Mesh;

		FTopologicalVertex(const FPoint& InCoordinates)
			: Coordinates(InCoordinates)
			, Mesh(TSharedPtr<FVertexMesh>())
		{
		}

		FTopologicalVertex(FCADKernelArchive& Archive)
			: Mesh(TSharedPtr<FVertexMesh>())
		{
			Serialize(Archive);
		}

	public:

		virtual void Serialize(FCADKernelArchive& Ar) override
		{
			TLinkable<FTopologicalVertex, FVertexLink>::Serialize(Ar);
			Ar << Coordinates;
			SerializeIdents(Ar, (TArray<TSharedPtr<FEntity>>&) ConnectedEdges);
		}

		virtual void SpawnIdent(FDatabase& Database) override;
		//{
		//	if (!FEntity::SetId(Database))
		//	{
		//		return;
		//	}
		//	TopologicalLink->SpawnIdent(Database);
		//	((TSharedPtr<FEntity>) Mesh)->SpawnIdent(Database);
		//}

		virtual void ResetMarkersRecursively() override
		{
			ResetMarkers();
		}


#ifdef CADKERNEL_DEV
		virtual FInfoEntity& GetInfo(FInfoEntity&) const override;
#endif

		virtual EEntity GetEntityType() const override
		{
			return EEntity::TopologicalVertex;
		}

		/**
		 * @return the 3d coordinate of the barycenter of the twin vertices
		 */
		inline const FPoint& GetBarycenter() const
		{
			if (TopologicalLink.IsValid() && TopologicalLink->GetTwinsEntitieNum() > 1)
			{
				return TopologicalLink->GetBarycenter();
			}
			return Coordinates;
		}

		/**
		 * @return the 3d coordinates of the vertex (prefere GetBarycenter())
		 */
		const FPoint& GetCoordinates() const
		{
			return Coordinates;
		}

		void SetCoordinates(const FPoint& NewCoordinates)
		{
			if (GetLink()->GetTwinsEntities().Num() > 1)
			{
				// Update barycenter
				FPoint BaryCenter = GetLink()->GetBarycenter() * (double)GetLink()->GetTwinsEntities().Num();
				BaryCenter -= Coordinates;
				BaryCenter += NewCoordinates;
				BaryCenter /= (double)GetLink()->GetTwinsEntities().Num();
				GetLink()->SetBarycenter(BaryCenter);
			}
			else
			{
				GetLink()->SetBarycenter(NewCoordinates);
			}
			Coordinates = NewCoordinates;
		}

		double Distance(const TSharedRef<FTopologicalVertex>& OtherVertex) const
		{
			return Coordinates.Distance((*OtherVertex).Coordinates);
		}

		double SquareDistance(const TSharedRef<FTopologicalVertex>& OtherVertex) const
		{
			return Coordinates.SquareDistance((*OtherVertex).Coordinates);
		}

		double SquareDistance(const FPoint& Point) const
		{
			return Coordinates.SquareDistance(Point);
		}

		virtual TSharedPtr<FEntityGeom> ApplyMatrix(const FMatrixH& InMatrix) const override;

		TSharedRef<FVertexMesh> GetOrCreateMesh(TSharedRef<FModelMesh>& MeshModel);

		const TSharedRef<FVertexMesh> GetMesh() const
		{
			if (!IsActiveEntity())
			{
				return GetLinkActiveEntity()->GetMesh();
			}
			return Mesh.ToSharedRef();
		}

		void Link(TSharedRef<FTopologicalVertex> InEntity);

		void DeleteIfIsolated()
		{
			if (ConnectedEdges.Num() == 0)
			{
				if (TopologicalLink.IsValid())
				{
					TopologicalLink->RemoveEntity(StaticCastSharedRef<FTopologicalVertex>(AsShared()));
					TopologicalLink->ComputeBarycenter();
					TopologicalLink.Reset();
				}
			}
			SetDeleted();
		}

		bool IsBorderVertex();

		void AddConnectedEdge(TSharedRef<FTopologicalEdge> Edge);
		void RemoveConnectedEdge(TSharedRef<FTopologicalEdge> Edge);

		const TArray<TWeakPtr<FTopologicalEdge>>& GetConnectedEdges() const
		{
			return ConnectedEdges;
		}

		void GetConnectedEdges(TSharedPtr<FTopologicalVertex> OtherVertex, TArray<TSharedPtr<FTopologicalEdge>>& Edges) const;
	};

} // namespace CADKernel
