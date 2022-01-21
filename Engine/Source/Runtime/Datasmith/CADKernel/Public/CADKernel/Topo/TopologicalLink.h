// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CADKernel/Core/Entity.h" 
#include "CADKernel/Math/Point.h"
#include "CADKernel/UI/Message.h"

namespace CADKernel
{
	class FTopologicalEdge;
	class FTopologicalVertex;

	template<typename EntityType>
	class CADKERNEL_API TTopologicalLink : public FEntity
	{
		friend FEntity;

	protected:
		friend EntityType;
		EntityType* ActiveEntity;

		TArray<EntityType*> TwinsEntities;

		TTopologicalLink()
			: ActiveEntity(nullptr)
		{
		}

		TTopologicalLink(EntityType& Entity)
			: ActiveEntity(&Entity)
		{
			TwinsEntities.Add(&Entity);
		}

	public:

		virtual void Serialize(FCADKernelArchive& Ar) override
		{
#ifdef CADKERNEL_DEV
			if (Ar.IsSaving())
			{
				ensureCADKernel(ActiveEntity != nullptr);
				ensureCADKernel(!ActiveEntity->IsDeleted());
			}
#endif
			FEntity::Serialize(Ar);
			SerializeIdent(Ar, &ActiveEntity, false);
			SerializeIdents(Ar, TwinsEntities, false);
		}

		virtual void Delete()
		{
			TwinsEntities.Empty();
			ActiveEntity = nullptr;
			SetDeleted();
		}

		const EntityType* GetActiveEntity() const
		{
			ensureCADKernel(ActiveEntity);
			return ActiveEntity;
		}

		EntityType* GetActiveEntity()
		{
			ensureCADKernel(ActiveEntity);
			return ActiveEntity;
		}

		int32 GetTwinsEntitieNum() const
		{
			return (int32)TwinsEntities.Num();
		}

		const TArray<EntityType*>& GetTwinsEntities() const
		{
			return TwinsEntities;
		}

		void ActivateEntity(const EntityType& NewActiveEntity)
		{
			TFunction<bool()> CheckEntityIsATwin = [&]() {
				for (EntityType* Entity : TwinsEntities)
				{
					if (Entity == &NewActiveEntity)
					{
						return true;
					}
				}
				FMessage::Error(TEXT("FTopologicalLink::ActivateEntity, the topological entity is not found in the twins entities"));
				return false;
			};

			ensureCADKernel(CheckEntityIsATwin());
			ActiveEntity = &NewActiveEntity;
		}

		void RemoveEntity(TSharedPtr<EntityType>& Entity)
		{
			EntityType* EntityPtr = *Entity;
			RemoveEntity(*EntityPtr);
		}

		void RemoveEntity(EntityType& Entity)
		{
			TwinsEntities.Remove(&Entity);
			if (&Entity == ActiveEntity && TwinsEntities.Num() > 0)
			{
				ActiveEntity = TwinsEntities.HeapTop();
			}

			if (TwinsEntities.Num() == 0)
			{
				ActiveEntity = nullptr;
				Delete();
			}
		}

		void UnlinkTwinEntities()
		{
			for (EntityType* Entity : TwinsEntities)
			{
				Entity->ResetTopologicalLink();
			}
			TwinsEntities.Empty();
		}

#ifdef CADKERNEL_DEV
		virtual FInfoEntity& GetInfo(FInfoEntity& Info) const override;
#endif

		virtual EEntity GetEntityType() const override
		{
			return EEntity::EdgeLink;
		}

		void AddEntity(EntityType* Entity)
		{
			TwinsEntities.Add(Entity);
		}

		void AddEntity(EntityType& Entity)
		{
			TwinsEntities.Add(&Entity);
		}

		template <typename LinkableType>
		void AddEntity(const LinkableType* Entity)
		{
			TwinsEntities.Add((EntityType*) Entity);
		}

		template <typename ArrayType>
		void AddEntities(const ArrayType& Entities)
		{
			TwinsEntities.Insert(Entities, TwinsEntities.Num());
		}

		/**
		 * @return true if the Twin entity count link is modified
		 */
		virtual bool CleanLink()
		{
			TArray<EntityType*> NewTwinsEntities;
			NewTwinsEntities.Reserve(TwinsEntities.Num());
			for (EntityType* Entity : TwinsEntities)
			{
				if (Entity)
				{
					NewTwinsEntities.Add(Entity);
				}
			}

			if (NewTwinsEntities.Num() != TwinsEntities.Num())
			{
				Swap(NewTwinsEntities, TwinsEntities);
				if(TwinsEntities.Num())
				{
					ActiveEntity = TwinsEntities.HeapTop();
					return true;
				}
			}
			return false;
		}
	};

	/**
	 * TTopologicalLink overload dedicated to FVertex to manage the barycenter of twin vertices
	 */
	class CADKERNEL_API FVertexLink : public TTopologicalLink<FTopologicalVertex>
	{
		friend class FTopologicalVertex;
	protected:
		FPoint Barycenter;

		void SetBarycenter(const FPoint& Point)
		{
			Barycenter = Point;
		}

	public:
		FVertexLink()
			: Barycenter(FPoint::ZeroPoint)
		{
		}

		FVertexLink(FTopologicalVertex& Entity)
			: TTopologicalLink<FTopologicalVertex>(Entity)
			, Barycenter(FPoint::ZeroPoint)
		{
		}

		virtual void Serialize(FCADKernelArchive& Ar) override
		{
			TTopologicalLink<FTopologicalVertex>::Serialize(Ar);
			Ar << Barycenter;
		}

#ifdef CADKERNEL_DEV
		virtual FInfoEntity& GetInfo(FInfoEntity& Info) const override;
#endif

		const FPoint& GetBarycenter() const
		{
			return Barycenter;
		}

		virtual bool CleanLink() override
		{
			if(TTopologicalLink::CleanLink())
			{
				ComputeBarycenter();
				DefineActiveEntity();
				return true;
			}
			return false;
		}

		virtual EEntity GetEntityType() const override
		{
			return EEntity::VertexLink;
		}

		void ComputeBarycenter();
		void DefineActiveEntity();
	};


} // namespace CADKernel
