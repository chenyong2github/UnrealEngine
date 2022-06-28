// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CADKernel/Core/EntityGeom.h"
#include "CADKernel/Topo/TopologicalShapeEntity.h"

namespace CADKernel
{

	class FBody;
	class FTopologicalFace;
	class FTopologicalEdge;
	class FGroup;
	class FTopologicalVertex;

	class CADKERNEL_API FModel : public FTopologicalShapeEntity
	{
		friend FEntity;

	protected:

		TArray<TSharedPtr<FBody>> Bodies;
		TArray<TSharedPtr<FTopologicalFace>> Faces;

		FModel()
		{
			Bodies.Reserve(100);
			Faces.Reserve(100);
		}

	public:

		virtual void Serialize(FCADKernelArchive& Ar) override
		{
			FTopologicalShapeEntity::Serialize(Ar);
			SerializeIdents(Ar, (TArray<TSharedPtr<FEntity>>&) Bodies);
			SerializeIdents(Ar, (TArray<TSharedPtr<FEntity>>&) Faces);

			if(Ar.IsLoading())
			{
				//ensureCADKernel(Archive.ArchiveModel == nullptr);
				if (Ar.ArchiveModel == nullptr)
				{
					Ar.ArchiveModel = this;
				}
			}
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

		void AddEntity(TSharedRef<FTopologicalEntity> InEntity);

		void Add(const TSharedPtr<FTopologicalFace>& InFace)
		{
			Faces.Add(InFace);
		}

		void Append(TArray<TSharedPtr<FTopologicalFace>>& InNewFaces)
		{
			Faces.Append(InNewFaces);
		}

		void Append(TArray<TSharedPtr<FBody>>& InNewBody)
		{
			Bodies.Append(InNewBody);
		}

		void Add(const TSharedPtr<FBody>& InBody)
		{
			Bodies.Add(InBody);
		}

		void Empty()
		{
			Bodies.Empty();
			Faces.Empty();
		}

		void RemoveFace(TSharedPtr<FTopologicalFace> InToplologicalFace)
		{
			Faces.Remove(InToplologicalFace);
		}

		void RemoveBody(TSharedPtr<FBody> InBody)
		{
			Bodies.Remove(InBody);
		}

		void RemoveBody(const FBody* InBody)
		{
			int32 Index = Bodies.IndexOfByPredicate([&](const TSharedPtr<FBody>& Body){ return (InBody == Body.Get()); });
			if (Index != INDEX_NONE)
			{
				Bodies.RemoveAt(Index);
			}
		}

		void PrintBodyAndShellCount();

		void RemoveEntity(TSharedPtr<FTopologicalEntity> InEntity);

		bool Contains(TSharedPtr<FTopologicalEntity> InEntity);

		/**
		 * Copy the body and face arrays of other model
		 */
		void Copy(const TSharedPtr<FModel>& OtherModel)
		{
			Bodies.Append(OtherModel->Bodies);
			Faces.Append(OtherModel->Faces);
		}
		
		/**
		 * Copy the body and face arrays of other model
		 */
		void Copy(const FModel& OtherModel)
		{
			Bodies.Append(OtherModel.Bodies);
			Faces.Append(OtherModel.Faces);
		}

		/**
		 * Copy the body and face arrays of other model
		 * Empty other model arrays
		 */
		void Merge(FModel& OtherModel)
		{
			Copy(OtherModel);
			OtherModel.Bodies.Empty();
			OtherModel.Faces.Empty();
		}

		int32 EntityCount() const
		{
			return Bodies.Num() + Faces.Num();
		}

		const TArray<TSharedPtr<FTopologicalFace>>& GetFaces() const
		{
			return Faces;
		}

		virtual void GetFaces(TArray<FTopologicalFace*>& OutFaces) override;

		virtual int32 FaceCount() const override;

		const TArray<TSharedPtr<FBody>>& GetBodies() const
		{
			return Bodies;
		}

		virtual void SpreadBodyOrientation() override;

		// Topo functions

		/**
		 * Check topology of each body
		 */
		void CheckTopology();

#ifdef CADKERNEL_DEV
		virtual void FillTopologyReport(FTopologyReport& Report) const override;
#endif

		/**
		 * Fore each body
		 */
		void FixModelTopology(double JoiningTolerance);

		void MergeInto(TSharedPtr<FBody> Body, TArray<TSharedPtr<FTopologicalEntity>>& InEntities);

		/**
		 * Fore each shell of each body, try to stitch topological gap
		 */
		void HealModelTopology(double JoiningTolerance);

		void Orient();
	};

} // namespace CADKernel
