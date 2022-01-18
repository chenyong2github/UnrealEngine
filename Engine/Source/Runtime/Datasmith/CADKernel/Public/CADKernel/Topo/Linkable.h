// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CADKernel/Core/EntityGeom.h"
#include "CADKernel/Topo/TopologicalEntity.h"
#include "CADKernel/Topo/TopologicalLink.h"

namespace CADKernel
{

class FModelMesh;

template<typename EntityType, typename LinkType>
class CADKERNEL_API TLinkable : public FTopologicalEntity
{
protected:
	mutable TSharedPtr<LinkType> TopologicalLink;

public:
	TLinkable() = default;

	void Finalize()
	{
		ensureCADKernel(!TopologicalLink.IsValid());
		TopologicalLink = FEntity::MakeShared<LinkType>((EntityType&)(*this));
	}

	virtual void Serialize(FCADKernelArchive& Ar) override
	{
		FEntityGeom::Serialize(Ar);
		SerializeIdent(Ar, TopologicalLink);
	}

	const TSharedRef<const EntityType> GetLinkActiveEntity() const
	{
		ensureCADKernel(TopologicalLink.IsValid());
		EntityType* ActiveEntity = TopologicalLink->GetActiveEntity();
		return StaticCastSharedRef<EntityType>(ActiveEntity->AsShared());
	}

	TSharedRef<EntityType> GetLinkActiveEntity()
	{
		ensureCADKernel(TopologicalLink.IsValid());
		EntityType* ActiveEntity = TopologicalLink->GetActiveEntity();
		return StaticCastSharedRef<EntityType>(ActiveEntity->AsShared());
	}

	bool IsActiveEntity() const
	{
		ensureCADKernel(TopologicalLink.IsValid());

		if (TopologicalLink->GetTwinsEntitieNum() == 1)
		{
			return true;
		}

		return (TopologicalLink->GetActiveEntity() == this);
	}

	void Activate()
	{
		ensureCADKernel(TopologicalLink.IsValid());
		TopologicalLink->ActivateEntity(*this);
	}

	virtual TSharedPtr<LinkType> GetLink() const
	{
		ensureCADKernel(TopologicalLink.IsValid());
		return TopologicalLink;
	}

	virtual TSharedPtr<LinkType> GetLink()
	{
		ensureCADKernel(TopologicalLink.IsValid());
		return TopologicalLink;
	}

	void ResetTopologicalLink()
	{
		TopologicalLink.Reset();
		TopologicalLink = FEntity::MakeShared<LinkType>((EntityType&)(*this));
	}

	bool IsLinkedTo(TSharedRef<EntityType> Entity) const
	{
		if (this == &*Entity)
		{
			return true;
		}
		return (Entity->TopologicalLink == TopologicalLink);
	}

	int32 GetTwinsEntityCount() const
	{
		ensureCADKernel(TopologicalLink.IsValid());
		return TopologicalLink->GetTwinsEntitieNum();
	}

	bool HasTwin() const
	{
		return GetTwinsEntityCount() != 1;
	}

	const TArray<EntityType*>& GetTwinsEntities() const
	{
		ensureCADKernel(TopologicalLink.IsValid());
		return TopologicalLink->GetTwinsEntities();
	}

	virtual void RemoveFromLink()
	{
		ensureCADKernel(TopologicalLink.IsValid());
		TopologicalLink->RemoveEntity((EntityType&)*this);
		TopologicalLink.Reset();
	}

	const bool IsThinZone() const
	{
		return ((States & EHaveStates::ThinZone) == EHaveStates::ThinZone);
	}

	virtual void SetThinZone()
	{
		States |= EHaveStates::ThinZone;
	}

	virtual void ResetThinZone()
	{
		States &= ~EHaveStates::ThinZone;
	}

protected:

	void MakeLink(EntityType& Twin)
	{
		TSharedPtr<LinkType> Link1 = TopologicalLink;
		TSharedPtr<LinkType> Link2 = Twin.TopologicalLink;

		ensureCADKernel(Link1.IsValid() && Link2.IsValid());

		if (Link1 == Link2)
		{
			return;
		}

		if (Link2->GetTwinsEntities().Num() > Link1->GetTwinsEntities().Num())
		{
			Swap(Link1, Link2);
		}

		Link1->AddEntities(Link2->GetTwinsEntities());
		for (EntityType* Entity : Link2->GetTwinsEntities())
		{
			Entity->SetTopologicalLink(Link1);
		}
		Link2->Delete();
	}

protected:
	void SetTopologicalLink(TSharedPtr<LinkType> Link)
	{
		TopologicalLink = Link;
	}
};

} // namespace CADKernel

