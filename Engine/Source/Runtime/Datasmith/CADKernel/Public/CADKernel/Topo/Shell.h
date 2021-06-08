// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CADKernel/Core/CADKernelArchive.h"
#include "CADKernel/Core/Entity.h"
#include "CADKernel/Core/MetadataDictionary.h"
#include "CADKernel/Core/Types.h"
#include "CADKernel/Geo/GeoEnum.h"
#include "CADKernel/Topo/TopologicalEntity.h"

namespace CADKernel
{
	class FTopologicalFace;
	class FBody;

	struct FFaceSubset;

	class CADKERNEL_API FOrientedFace : public TOrientedEntity<FTopologicalFace>
	{
	public:
		FOrientedFace(TSharedPtr<FTopologicalFace>& InEntity, EOrientation InOrientation)
			: TOrientedEntity(InEntity, InOrientation)
		{
		}

		FOrientedFace(const FOrientedFace& OrientiredEntity)
			: TOrientedEntity(OrientiredEntity)
		{
		}

		FOrientedFace()
			: TOrientedEntity()
		{
		}
	};

	class CADKERNEL_API FShell : public FTopologicalEntity, public FMetadataDictionary
	{
		friend FEntity;
		friend class FBody;

	private:
		TArray<FOrientedFace> TopologicalFaces;
		TWeakPtr<FBody> HostedBy;

		FShell()
			: FTopologicalEntity()
		{
		}

		FShell(const TArray<FOrientedFace> InTopologicalFaces, bool bIsInnerShell = false)
			: FTopologicalEntity()
			, TopologicalFaces(InTopologicalFaces)
		{
			if (bIsInnerShell)
			{
				SetInner();
			}
		}

		FShell(const TArray<TSharedPtr<FTopologicalFace>>& InTopologicalFaces, bool bIsInnerShell = true);

		FShell(const TArray<TSharedPtr<FTopologicalFace>>& InTopologicalFaces, const TArray<EOrientation>& InOrientations, bool bIsInnerShell = true);

		FShell(FCADKernelArchive& Archive)
			: FTopologicalEntity()
		{
			Serialize(Archive);
		}

	public:

		virtual void Serialize(FCADKernelArchive& Ar) override
		{
			FTopologicalEntity::Serialize(Ar);
			SerializeIdents(Ar, (TArray<TOrientedEntity<FEntity>>&) TopologicalFaces);
			SerializeIdent(Ar, HostedBy);
			SerializeMetadata(Ar);
		}

		virtual void SpawnIdent(FDatabase& Database) override
		{
			if (!FEntity::SetId(Database))
			{
				return;
			}

			SpawnIdentOnEntities((TArray<TOrientedEntity<FEntity>>&) TopologicalFaces, Database);
		}

		virtual void ResetMarkersRecursively() override
		{
			ResetMarkers();
			ResetMarkersRecursivelyOnEntities((TArray<TOrientedEntity<FEntity>>&) TopologicalFaces);
		}

		TWeakPtr<FBody>& GetHost()
		{
			return HostedBy;
		}

		void SetHost(TSharedPtr<FBody> Body)
		{
			HostedBy = Body;
		}

		void CompleteMetadata()
		{
			if (HostedBy.IsValid())
			{
				CompleteDictionary((const FMetadataDictionary&) * HostedBy.Pin());
			}
		}

		void Empty(int32 NewSize = 0);

		void Add(TSharedRef<FTopologicalFace> InTopologicalFace, EOrientation InOrientation);
		void Add(TArray<TSharedPtr<FTopologicalFace>> Faces);

#ifdef CADKERNEL_DEV
		virtual FInfoEntity& GetInfo(FInfoEntity&) const override;
#endif

		virtual EEntity GetEntityType() const override
		{
			return EEntity::Shell;
		}

		TSharedPtr<FEntityGeom> ApplyMatrix(const FMatrixH& InMatrix) const;

		virtual int32 FaceCount() const override
		{
			return TopologicalFaces.Num();
		}

		void ReplaceFaces(TArray<FOrientedFace>& NewFaces)
		{
			Swap(TopologicalFaces, NewFaces);
			NewFaces.Empty();
		}

		const TArray<FOrientedFace>& GetFaces() const
		{
			return TopologicalFaces;
		}

		virtual void GetFaces(TArray<TSharedPtr<FTopologicalFace>>& OutFaces) override;

		virtual void Merge(TSharedPtr<FShell>& Shell);

		virtual void SpreadBodyOrientation() override;

		void CheckTopology(TArray<FFaceSubset>& SubShells);

		/**
		 * @return true if the shell has at least one border edge 
		 */
		bool IsOpenShell();

		bool IsInner() const
		{
			return ((States & EHaveStates::IsInner) == EHaveStates::IsInner);
		}

		bool IsOutter() const
		{
			return ((States & EHaveStates::IsInner) != EHaveStates::IsInner);
		}

		void SetInner()
		{
			States |= EHaveStates::IsInner;
		}

		void SetOutter()
		{
			States &= ~EHaveStates::IsInner;
		}
	};
}

