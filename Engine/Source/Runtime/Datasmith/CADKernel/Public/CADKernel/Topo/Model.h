// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CADKernel/Core/EntityGeom.h"
#include "CADKernel/Topo/TopologicalFace.h"

namespace CADKernel
{

	class FBody;
	class FTopologicalFace;
	class FTopologicalEdge;
	class FGroup;
	class FTopologicalVertex;

	class CADKERNEL_API FModel : public FTopologicalEntity, public FMetadataDictionary
	{
		friend FEntity;

	protected:

		TArray<TSharedPtr<FBody>> Bodies;
		TArray<TSharedPtr<FTopologicalFace>> Faces;

		FModel()
			: FTopologicalEntity()
		{
			Bodies.Reserve(100);
			Faces.Reserve(100);
		}

		FModel(FCADKernelArchive& Archive)
			: FTopologicalEntity()
		{
			Serialize(Archive);
		}

	public:

		virtual void Serialize(FCADKernelArchive& Ar) override
		{
			FEntityGeom::Serialize(Ar);
			SerializeIdents(Ar, (TArray<TSharedPtr<FEntity>>&) Bodies);
			SerializeIdents(Ar, (TArray<TSharedPtr<FEntity>>&) Faces);
		}

		virtual void SpawnIdent(FDatabase& Database) override
		{
			if (!FEntity::SetId(Database))
			{
				return;
			}

			SpawnIdentOnEntities(Bodies, Database);
			SpawnIdentOnEntities(Faces, Database);
		}

		virtual void ResetMarkersRecursively() override
		{
			ResetMarkers();
			ResetMarkersRecursivelyOnEntities(Bodies);
			ResetMarkersRecursivelyOnEntities(Faces);
		}

#ifdef CADKERNEL_DEV
		virtual FInfoEntity& GetInfo(FInfoEntity&) const override;
#endif

		virtual EEntity GetEntityType() const override
		{
			return EEntity::Model;
		}

		void AddEntity(TSharedRef<FTopologicalEntity> Entity);

		void Add(TSharedPtr<FTopologicalFace> Face);
		void Add(TSharedPtr<FBody> Body);

		void RemoveEntity(TSharedPtr<FTopologicalEntity> Entity);
		void RemoveDomain(TSharedPtr<FTopologicalFace> ToplologicalFace);
		void RemoveBody(TSharedPtr<FBody> Body);
		bool Contains(TSharedPtr<FTopologicalEntity> Entity);
		
		const TArray<TSharedPtr<FTopologicalFace>>& GetFaces() const
		{
			return Faces;
		}

		const TArray<TSharedPtr<FTopologicalFace>>& GetSurfaceList() const
		{ 
			return Faces;
		}

		const TArray<TSharedPtr<FBody>>& GetBodyList() const
		{
			return Bodies;
		}

		virtual int32 FaceCount() const override;

		virtual void GetFaces(TArray<TSharedPtr<FTopologicalFace>>& OutFaces) override;

		virtual void SpreadBodyOrientation() override;

		virtual TSharedPtr<FEntityGeom> ApplyMatrix(const FMatrixH& InMatrix) const override;
	};

} // namespace CADKernel
