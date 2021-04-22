// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CADKernel/Core/EntityGeom.h"
#include "CADKernel/Core/Types.h"

namespace CADKernel
{
	class FNode;
	class FModelMesh;
	class FTopologicalEntity;

	class CADKERNEL_API FMesh : public FEntityGeom
	{
	protected:
		TWeakPtr<FModelMesh> ModelMesh;
		TWeakPtr<FTopologicalEntity> TopologicalEntity;

		int32 StartNodeId;
		int32 LastNodeIndex;

		TArray<FPoint> NodeCoordinates;
		int32 MeshModelIndex;

	public:

		FMesh(TSharedRef<FModelMesh> InMeshModel, TSharedRef<FTopologicalEntity> InTopologicalEntity)
			: FEntityGeom()
			, ModelMesh(InMeshModel)
			, TopologicalEntity(InTopologicalEntity)
		{
		}

		virtual ~FMesh() = default;

#ifdef CADKERNEL_DEV
		virtual FInfoEntity& GetInfo(FInfoEntity&) const override;
#endif

		virtual EEntity GetEntityType() const override
		{
			return EEntity::Mesh;
		}

		TArray<FPoint>& GetNodeCoordinates()
		{
			return NodeCoordinates;
		}

		const TArray<FPoint>& GetNodeCoordinates() const
		{
			return NodeCoordinates;
		}

		int32 RegisterCoordinates();

		const int32 GetStartVertexId() const
		{
			return StartNodeId;
		}

		const int32 GetLastVertexIndex() const
		{
			return LastNodeIndex;
		}

		const int32 GetIndexInMeshModel() const
		{
			return MeshModelIndex;
		}

		TSharedRef<FModelMesh> GetMeshModel()
		{
			ensureCADKernel(ModelMesh.Pin().IsValid());
			return ModelMesh.Pin().ToSharedRef();
		}

		const TSharedRef<FModelMesh> GetMeshModel() const
		{
			return ModelMesh.Pin().ToSharedRef();
		}

		const TSharedRef<FTopologicalEntity> GetGeometricEntity() const
		{
			return TopologicalEntity.Pin().ToSharedRef();
		}
	};
}

