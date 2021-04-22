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
		TWeakPtr<EntityType> ActiveEntity;

		TArray<TWeakPtr<EntityType>> TwinsEntities;

		TTopologicalLink()
		{
			ActiveEntity = TWeakPtr<EntityType>();
		}

		TTopologicalLink(TSharedRef<EntityType> Entity)
			: FEntity()
		{
			TwinsEntities.Add(Entity);
			ActiveEntity = Entity;
		}

	public:

		TTopologicalLink(FCADKernelArchive& Ar)
			: FEntity()
		{
			Serialize(Ar);
		}

		virtual void Serialize(FCADKernelArchive& Ar) override
		{
			FEntity::Serialize(Ar);
			SerializeIdent(Ar, ActiveEntity, false);
			SerializeIdents(Ar, TwinsEntities, false);
		}

		TWeakPtr<const EntityType>& GetActiveEntity() const
		{
			ensureCADKernel(ActiveEntity.IsValid());
			return ActiveEntity;
		}

		TWeakPtr<EntityType>& GetActiveEntity()
		{
			ensureCADKernel(ActiveEntity.IsValid());
			return ActiveEntity;
		}

		int32 GetTwinsEntitieNum() const
		{
			return (int32)TwinsEntities.Num();
		}

		const TArray<TWeakPtr<EntityType>>& GetTwinsEntities() const
		{
			return TwinsEntities;
		}

		void ActivateEntity(TSharedRef<EntityType> NewActiveEntity)
		{
			TFunction<bool()> CheckEntityIsATwin = [&]() {
				for (TWeakPtr<EntityType>& Entity : TwinsEntities)
				{
					if (Entity.Pin() == NewActiveEntity)
					{
						return true;
					}
				}
				FMessage::Error(TEXT("FTopologicalLink::ActivateEntity, the topological entity is not found in the twins entities"));
				return false;
			};

			ensureCADKernel(CheckEntityIsATwin());
			ActiveEntity = NewActiveEntity;
		}

		void RemoveEntity(const TSharedPtr<EntityType>& Entity)
		{
			TwinsEntities.Remove(Entity);
			if (Entity == ActiveEntity && TwinsEntities.Num() > 0)
			{
				ActiveEntity = TwinsEntities.HeapTop();
			}
		}

#ifdef CADKERNEL_DEV
		virtual FInfoEntity& GetInfo(FInfoEntity& Info) const override;
#endif

		virtual EEntity GetEntityType() const override
		{
			return EEntity::EdgeLink;
		}

		void AddEntity(TSharedRef<EntityType> Entity)
		{
			TwinsEntities.Add(Entity);
		}

		template <typename ArrayType>
		void AddEntities(const ArrayType& Entities)
		{
			TwinsEntities.Insert(Entities, TwinsEntities.Num());
		}

		bool CleanLink()
		{
			TArray<TWeakPtr<EntityType>> NewTwinsEntities;
			NewTwinsEntities.Reserve(TwinsEntities.Num());
			for (TWeakPtr<EntityType>& Entity : TwinsEntities)
			{
				if (Entity.IsValid())
				{
					NewTwinsEntities.Add(Entity);
				}
			}

			if (NewTwinsEntities.Num() != TwinsEntities.Num())
			{
				Swap(NewTwinsEntities, TwinsEntities);
				ActiveEntity = TwinsEntities.HeapTop();
				return true;
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
			: TTopologicalLink<FTopologicalVertex>()
			, Barycenter(FPoint::ZeroPoint)
		{
		}

		FVertexLink(TSharedRef<FTopologicalVertex> Entity)
			: TTopologicalLink<FTopologicalVertex>(Entity)
			, Barycenter(FPoint::ZeroPoint)

		{
		}

		FVertexLink(FCADKernelArchive& Ar)
			: TTopologicalLink<FTopologicalVertex>()
		{
			Serialize(Ar);
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

		void CleanLink()
		{
			if(TTopologicalLink::CleanLink())
			{
				ComputeBarycenter();
				DefineActiveEntity();
			}
		}

		virtual EEntity GetEntityType() const override
		{
			return EEntity::VertexLink;
		}

		void ComputeBarycenter();
		void DefineActiveEntity();
	};


} // namespace CADKernel
