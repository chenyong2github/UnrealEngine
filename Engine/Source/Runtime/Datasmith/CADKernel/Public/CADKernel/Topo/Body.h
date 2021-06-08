// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CADKernel/Core/MetadataDictionary.h"
#include "CADKernel/Geo/GeoEnum.h"
#include "CADKernel/Topo/Shell.h"
#include "CADKernel/Topo/TopologicalEntity.h"
#include "CADKernel/Topo/TopologicalFace.h"

namespace CADKernel
{
	class FShell;

	class CADKERNEL_API FBody : public FTopologicalEntity, public FMetadataDictionary
	{
		friend FEntity;

	private:
		TArray<TSharedPtr<FShell>> Shells;

		FBody()
			: FTopologicalEntity()
		{
		}

		FBody(const TArray<TSharedPtr<FShell>>& InShells)
			: FTopologicalEntity()
		{
			for (TSharedPtr<FShell> Shell : InShells)
			{
				if (Shell.IsValid())
				{
					AddShell(Shell.ToSharedRef());
				}
			}
		}

		FBody(FCADKernelArchive& Archive)
			: FTopologicalEntity()
		{
			Serialize(Archive);
		}

	public:
		virtual void Serialize(FCADKernelArchive& Ar) override
		{
			FTopologicalEntity::Serialize(Ar);
			SerializeIdents(Ar, Shells);
			SerializeMetadata(Ar);
		}

		virtual void SpawnIdent(FDatabase& Database) override
		{
			if (!FEntity::SetId(Database))
			{
				return;
			}

			SpawnIdentOnEntities(Shells, Database);
		}

		virtual void ResetMarkersRecursively() override
		{
			ResetMarkers();
			ResetMarkersRecursivelyOnEntities(Shells);
		}

#ifdef CADKERNEL_DEV
		virtual FInfoEntity& GetInfo(FInfoEntity&) const override;
#endif

		virtual EEntity GetEntityType() const override
		{
			return EEntity::Body;
		}

		void AddShell(TSharedRef<FShell> Shell);

		void RemoveEmptyShell();

		void Empty()
		{
			Shells.Empty();
		}

		const TArray<TSharedPtr<FShell>>& GetShells() const
		{
			return Shells;
		}

		virtual int32 FaceCount() const override
		{
			int32 FaceCount = 0;
			for (const TSharedPtr<FShell>& Shell : Shells)
			{
				FaceCount += Shell->FaceCount();
			}
			return FaceCount;
		}

		virtual void GetFaces(TArray<TSharedPtr<FTopologicalFace>>& Faces) override
		{
			for (const TSharedPtr<FShell>& Shell : Shells)
			{
				Shell->GetFaces(Faces);
			}
		}

		virtual void SpreadBodyOrientation() override
		{
			for (const TSharedPtr<FShell>& Shell : Shells)
			{
				Shell->SpreadBodyOrientation();
			}
		}

		virtual TSharedPtr<FEntityGeom> ApplyMatrix(const FMatrixH& InMatrix) const override;
	};
}

