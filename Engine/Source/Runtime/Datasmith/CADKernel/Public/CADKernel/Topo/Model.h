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
			//ensureCADKernel(Archive.ArchiveModel == nullptr);
			if(Archive.ArchiveModel == nullptr)
			{
				Archive.ArchiveModel = this;
			}
		}

	public:

		virtual void Serialize(FCADKernelArchive& Ar) override
		{
			FEntityGeom::Serialize(Ar);
			SerializeIdents(Ar, (TArray<TSharedPtr<FEntity>>&) Bodies);
			SerializeIdents(Ar, (TArray<TSharedPtr<FEntity>>&) Faces);
			SerializeMetadata(Ar);
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

		void PrintBodyAndShellCount();
		void RemoveEmptyBodies();

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

		const TArray<TSharedPtr<FTopologicalFace>>& GetFaces() const
		{
			return Faces;
		}

		virtual void GetFaces(TArray<TSharedPtr<FTopologicalFace>>& OutFaces) override;

		virtual int32 FaceCount() const override;

		const TArray<TSharedPtr<FBody>>& GetBodies() const
		{
			return Bodies;
		}

		virtual void SpreadBodyOrientation() override;

		virtual TSharedPtr<FEntityGeom> ApplyMatrix(const FMatrixH& InMatrix) const override;

		// Topo functions

		/**
		 * Check topology of each body
		 */
		void CheckTopology();

		/**
		 * Fore each body
		 */
		void FixModelTopology(double JoiningTolerance);

		void MergeInto(TSharedPtr<FBody> Body, TArray<TSharedPtr<FTopologicalEntity>>& InEntities);
		void Split(TSharedPtr<FBody> Body, TArray<TSharedPtr<FBody>>& OutNewBody);
		void Join(TArray<TSharedPtr<FBody>> Bodies, double Tolerance);

		/**
		 * Fore each shell of each body, try to stitch topological gap
		 */
		void HealModelTopology(double JoiningTolerance);


	};

} // namespace CADKernel
