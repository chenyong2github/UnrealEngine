// Copyright Epic Games, Inc. All Rights Reserved.

#include "CADKernel/Core/Entity.h"

#include "CADKernel/Core/EntityGeom.h"

#include "CADKernel/Core/Database.h"
#include "CADKernel/Core/Group.h"
#include "CADKernel/Core/Session.h"
#include "CADKernel/Core/System.h"
#include "CADKernel/Geo/Curves/Curve.h"
#include "CADKernel/Geo/Surfaces/Surface.h"
#include "CADKernel/Mesh/Criteria/Criterion.h"
#include "CADKernel/Topo/Body.h"
#include "CADKernel/Topo/Model.h"
#include "CADKernel/Topo/Shell.h"
#include "CADKernel/Topo/TopologicalEdge.h"
#include "CADKernel/Topo/TopologicalFace.h"
#include "CADKernel/Topo/TopologicalLink.h"
#include "CADKernel/Topo/TopologicalLoop.h"
#include "CADKernel/Topo/TopologicalVertex.h"

namespace CADKernel
{
	const TCHAR* FEntity::TypesNames[] = 
	{
		TEXT("NullEntity"),
		TEXT("Curve"),
		TEXT("Surface"),

		TEXT("Edge Link"),
		TEXT("Vertex Link"),
		TEXT("Edge"),
		TEXT("Face"),
 		TEXT("Link"),
		TEXT("Loop"),
		TEXT("Vertex"),
		TEXT("Shell"),
		TEXT("Body"),
		TEXT("Model"),

		TEXT("Mesh Model"),
		TEXT("Mesh"),

		TEXT("Group"),
		TEXT("Criterion"),
		TEXT("Property"),
		nullptr
	};

	const TCHAR* FEntity::GetTypeName(EEntity Type)
	{
		if (Type >= EEntity::EntityTypeEnd)
		{
			return TypesNames[0];
		}
		return TypesNames[(int32) Type];
	}

	FEntity::~FEntity()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FEntity::~FEntity);
	}

	TSharedPtr<FEntity> FEntity::Deserialize(FCADKernelArchive& Archive)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FEntity::Deserialize);

		ensureCADKernel(Archive.IsLoading());

		EEntity Type = EEntity::NullEntity;
		Archive << Type;

		switch (Type)
		{
		case EEntity::Body:
			return FEntity::MakeShared<FBody>(Archive);
		case EEntity::Curve:
			return FCurve::Deserialize(Archive);
		case EEntity::Criterion:
			return FCriterion::Deserialize(Archive);
		case EEntity::EdgeLink:
			return FEntity::MakeShared<FEdgeLink>(Archive);
		case EEntity::Group:
			return FEntity::MakeShared<FGroup>(Archive);
		case EEntity::Model:
			return FEntity::MakeShared<FModel>(Archive);
		case EEntity::Shell:
			return FEntity::MakeShared<FShell>(Archive);
		case EEntity::Surface:
			return FSurface::Deserialize(Archive);
		case EEntity::TopologicalEdge:
			return FEntity::MakeShared<FTopologicalEdge>(Archive);
		case EEntity::TopologicalFace:
			return FEntity::MakeShared<FTopologicalFace>(Archive);
		case EEntity::TopologicalLoop:
			return FEntity::MakeShared<FTopologicalLoop>(Archive);
		case EEntity::TopologicalVertex:
			return FEntity::MakeShared<FTopologicalVertex>(Archive);
		case EEntity::VertexLink:
			return FEntity::MakeShared<FVertexLink>(Archive);
		default:
			return TSharedPtr<FEntity>();
		}
	}

#ifdef CADKERNEL_DEV

	void FEntity::InfoEntity() const
	{
		FInfoEntity Info;
		GetInfo(Info);
		Info.Display();
	}

	FInfoEntity& FEntity::GetInfo(FInfoEntity& Info) const
	{
		Info.Set(*this);
		Info.Add(TEXT("Id"), GetId())
			.Add(TEXT("Type"), TypesNames[(uint32)GetEntityType()]);
		return Info;
	}

	FInfoEntity& FEntityGeom::GetInfo(FInfoEntity& Info) const
	{
		return FEntity::GetInfo(Info)
			.Add(TEXT("Kio"), CtKioId);
	}

	void FEntity::AddEntityInDatabase(TSharedRef<FEntity> Entity)
	{
		if (FSession::Session.IsValid())
		{
			FSession::Session->GetDatabase().AddEntity(Entity);
		}
	}

#endif

	void FEntity::SerializeIdents(FCADKernelArchive& Ar, TArray<TOrientedEntity<FEntity>>& Array)
	{
		if (Ar.IsLoading())
		{
			int32 ArrayNum = 0;
			Ar << ArrayNum;
			Array.Init(TOrientedEntity<FEntity>(), ArrayNum);
			for (TOrientedEntity<FEntity>& OrientedEntity : Array)
			{
				FIdent OldId = 0;
				Ar << OldId;
				Ar.SetReferencedEntityOrAddToWaitingList(OldId, OrientedEntity.Entity);
				Ar.Serialize(&OrientedEntity.Direction, sizeof(EOrientation));
			}
		}
		else
		{
			int32 ArrayNum = Array.Num();
			Ar << ArrayNum;
			for (TOrientedEntity<FEntity>& OrientedEntity : Array)
			{
				FIdent Id = OrientedEntity.Entity->GetId();
				Ar << Id;
				Ar.Serialize(&OrientedEntity.Direction, sizeof(EOrientation));
				Ar.AddEntityToSave(Id);
			}
		}
	}

	void FEntity::SerializeIdents(FCADKernelArchive& Ar, TArray<TWeakPtr<FEntity>>& EntityArray, bool bSaveSelection)
	{
		if (Ar.IsLoading())
		{
			int32 ArrayNum = 0;
			Ar << ArrayNum;
			EntityArray.Init(TSharedPtr<FEntity>(), ArrayNum);
			for (TWeakPtr<FEntity>& Entity : EntityArray)
			{
				FIdent OldId = 0;
				Ar << OldId;
				Ar.SetReferencedEntityOrAddToWaitingList(OldId, Entity);
			}
		}
		else
		{
			int32 ArrayNum = EntityArray.Num();
			Ar << ArrayNum;
			for (TWeakPtr<FEntity>& Entity : EntityArray)
			{
				if (Entity.IsValid())
				{
					FIdent Id = Entity.Pin()->GetId();
					Ar << Id;
				}
			}

			if (bSaveSelection)
			{
				for (TWeakPtr<FEntity>& Entity : EntityArray)
				{
					if (Entity.IsValid())
					{
						Ar.AddEntityToSave(Entity.Pin()->GetId());
					}
				}
			}
		}
	}

	void FEntity::SerializeIdents(FCADKernelArchive& Ar, TArray<TSharedPtr<FEntity>>& EntityArray, bool bSaveSelection)
	{
		if (Ar.IsLoading())
		{
			int32 ArrayNum = 0;
			Ar << ArrayNum;
			EntityArray.Init(TSharedPtr<FEntity>(), ArrayNum);
			for (TSharedPtr<FEntity>& Entity : EntityArray)
			{
				FIdent OldId = 0;
				Ar << OldId;
				Ar.SetReferencedEntityOrAddToWaitingList(OldId, Entity);
			}
		}
		else
		{
			int32 ArrayNum = EntityArray.Num();
			Ar << ArrayNum;
			for (TSharedPtr<FEntity>& Entity : EntityArray)
			{
				if (Entity.IsValid())
				{
					FIdent Id = Entity->GetId();
					Ar << Id;
				}
			}

			if (bSaveSelection)
			{
				for (TSharedPtr<FEntity>& Entity : EntityArray)
				{
					if(Entity.IsValid())
					{ 
						Ar.AddEntityToSave(Entity->GetId());
					}
				}
			}
		}
	}

	void FEntity::SerializeIdent(FCADKernelArchive& Ar, TSharedPtr<FEntity>& Entity, bool bSaveSelection)
	{
		if (Ar.IsLoading())
		{
			FIdent OldId = 0;
			Ar << OldId;
			Ar.SetReferencedEntityOrAddToWaitingList(OldId, Entity);
		}
		else
		{
			FIdent Id = Entity.IsValid() ? Entity->GetId() : 0;
			Ar << Id;
			if (bSaveSelection && Id)
			{
				Ar.AddEntityToSave(Id);
			}
		}
	}

	void FEntity::SerializeIdent(FCADKernelArchive& Ar, TWeakPtr<FEntity>& Entity, bool bSaveSelection)
	{
		if (Ar.Archive.IsLoading())
		{
			FIdent OldId;
			Ar.Archive << OldId;
			Ar.SetReferencedEntityOrAddToWaitingList(OldId, Entity);
		}
		else
		{
			FIdent Id = Entity.IsValid() ? Entity.Pin()->GetId() : 0;
			Ar.Archive << Id;
			if (bSaveSelection && Id)
			{
				Ar.AddEntityToSave(Id);
			}
		}
	}

	bool FEntity::SetId(FDatabase& Database)
	{
		if (Id < 1)
		{
			Database.AddEntity(AsShared());
			++Database.EntityCount;
			return true;
		}
		return Database.bForceSpawning || false;
	}

} // namespace CADKernel
