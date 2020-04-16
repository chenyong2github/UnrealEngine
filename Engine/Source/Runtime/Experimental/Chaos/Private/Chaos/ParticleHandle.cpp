// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/ParticleHandle.h"

#include "Chaos/ImplicitObjectTransformed.h"
#include "Chaos/ImplicitObjectScaled.h"
#include "Chaos/TriangleMeshImplicitObject.h"
#include "Chaos/Framework/PhysicsProxy.h"
#include "Chaos/Framework/PhysicsSolverBase.h"
#include "Chaos/Framework/PhysicsProxyBase.h"
#include "Chaos/CastingUtilities.h"

namespace Chaos
{
	template <typename T, int d>
	void Chaos::TGeometryParticle<T, d>::MapImplicitShapes()
	{
		ImplicitShapeMap.Reset();

		for (int32 ShapeIndex = 0; ShapeIndex < MShapesArray.Num(); ++ ShapeIndex)
		{
			const FImplicitObject* ImplicitObject = MShapesArray[ShapeIndex]->GetGeometry().Get();
			ImplicitShapeMap.Add(ImplicitObject, ShapeIndex);

			const FImplicitObject* ImplicitChildObject = Utilities::ImplicitChildHelper(ImplicitObject);
			if (ImplicitChildObject != ImplicitObject)
			{
				ImplicitShapeMap.Add(ImplicitChildObject, ShapeIndex);
			}
		}

		auto& Geometry = MNonFrequentData.Read().Geometry();
		if (Geometry)
		{
			int32 CurrentShapeIndex = INDEX_NONE;
			if (const auto* Union = Geometry->template GetObject<FImplicitObjectUnion>())
			{
				for (const TUniquePtr<FImplicitObject>& ImplicitObject : Union->GetObjects())
				{
					if (ImplicitObject.Get())
					{
						if (const FImplicitObject* ImplicitChildObject = Utilities::ImplicitChildHelper(ImplicitObject.Get()))
						{
							if (ImplicitShapeMap.Contains(ImplicitObject.Get()))
							{
								ImplicitShapeMap.Add(ImplicitChildObject, CopyTemp(ImplicitShapeMap[ImplicitObject.Get()]));
							}
							else if (ImplicitShapeMap.Contains(ImplicitChildObject))
							{
								ImplicitShapeMap.Add(ImplicitObject.Get(), CopyTemp(ImplicitShapeMap[ImplicitChildObject]));
							}
						}
					}
				}
			}
			else 
			{
				if (const FImplicitObject* ImplicitChildObject = Utilities::ImplicitChildHelper(Geometry.Get()))
				{
					if (ImplicitShapeMap.Contains(Geometry.Get()))
					{
						ImplicitShapeMap.Add(ImplicitChildObject, CopyTemp(ImplicitShapeMap[Geometry.Get()]));
					}
					else if (ImplicitShapeMap.Contains(ImplicitChildObject))
					{
						ImplicitShapeMap.Add(Geometry.Get(), CopyTemp(ImplicitShapeMap[ImplicitChildObject]));
					}
				}
			}
		}
	}

	inline FImplicitObject* GetInstancedImplicitHelper(FImplicitObject* Implicit0)
	{
		EImplicitObjectType Implicit0OuterType = Implicit0->GetType();

		if (Implicit0OuterType == TImplicitObjectInstanced<FConvex>::StaticType())
		{
			return const_cast<FConvex*>(Implicit0->template GetObject<TImplicitObjectInstanced<FConvex>>()->GetInstancedObject());
		}
		else if (Implicit0OuterType == TImplicitObjectInstanced<TBox<FReal, 3>>::StaticType())
		{
			return const_cast<TBox<FReal, 3>*>(Implicit0->template GetObject<TImplicitObjectInstanced<TBox<FReal, 3>>>()->GetInstancedObject());
		}
		else if (Implicit0OuterType == TImplicitObjectInstanced<TCapsule<FReal>>::StaticType())
		{
			return const_cast<TCapsule<FReal>*>(Implicit0->template GetObject<TImplicitObjectInstanced<TCapsule<FReal>>>()->GetInstancedObject());
		}
		else if (Implicit0OuterType == TImplicitObjectInstanced<TSphere<FReal, 3>>::StaticType())
		{
			return const_cast<TSphere<FReal, 3>*>(Implicit0->template GetObject<TImplicitObjectInstanced<TSphere<FReal, 3>>>()->GetInstancedObject());
		}
		else if (Implicit0OuterType == TImplicitObjectInstanced<FTriangleMeshImplicitObject>::StaticType())
		{
			return const_cast<FTriangleMeshImplicitObject*>(Implicit0->template GetObject<TImplicitObjectInstanced<FTriangleMeshImplicitObject>>()->GetInstancedObject());
		}

		return nullptr;
	}

	template <typename T, int d>
	void Chaos::TGeometryParticle<T, d>::SetIgnoreAnalyticCollisionsImp(FImplicitObject* Implicit, bool bIgnoreAnalyticCollisions)
	{
		check(Implicit);
		if (Implicit->GetType() == FImplicitObjectUnion::StaticType())
		{
			FImplicitObjectUnion* Union = Implicit->template GetObject<FImplicitObjectUnion>();
			for (const auto& Child : Union->GetObjects())
			{
				SetIgnoreAnalyticCollisionsImp(Child.Get(), bIgnoreAnalyticCollisions);
			}
		}
		else if (Implicit->GetType() == TImplicitObjectTransformed<T, d>::StaticType())
		{
			TImplicitObjectTransformed<FReal, 3>* TransformedImplicit = Implicit->template GetObject<TImplicitObjectTransformed<FReal, 3>>();
			SetIgnoreAnalyticCollisionsImp(const_cast<FImplicitObject*>(TransformedImplicit->GetTransformedObject()), bIgnoreAnalyticCollisions);
		}
		else if ((uint32)Implicit->GetType() & ImplicitObjectType::IsInstanced)
		{
			SetIgnoreAnalyticCollisionsImp(GetInstancedImplicitHelper(Implicit), bIgnoreAnalyticCollisions);
		}
		else
		{
			if (const auto* PerShapeData = GetImplicitShape(Implicit))
			{
				if (PerShapeData->bDisable)
				{
					return;
				}
			}
			if (bIgnoreAnalyticCollisions)
			{
				Implicit->SetCollsionType(Chaos::ImplicitObjectType::LevelSet);
				//Implicit->SetConvex(false);
			}
			else
			{
				Implicit->SetCollsionType(Implicit->GetType());
				// @todo (mlentine): Need to in theory set convex properly here
			}
		}
	}

	template class CHAOS_API TGeometryParticleData<float, 3>;
	template class CHAOS_API TGeometryParticle<float, 3>;

	template class CHAOS_API TKinematicGeometryParticleData<float, 3>;
	template class CHAOS_API TKinematicGeometryParticle<float, 3>;

	template class CHAOS_API TPBDRigidParticleData<float, 3>;
	template class CHAOS_API TPBDRigidParticle<float, 3>;

	template <>
	void Chaos::TGeometryParticle<float, 3>::MarkDirty(const EParticleFlags DirtyBits, bool bInvalidate )
	{
		if (bInvalidate)
		{
			this->MDirtyFlags.MarkDirty(DirtyBits);

			if (Proxy)
			{
				if (FPhysicsSolverBase* PhysicsSolverBase = Proxy->GetSolver<FPhysicsSolverBase>())
				{
					PhysicsSolverBase->AddDirtyProxy(Proxy);
				}
			}
		}
	}

	template <>
	template <>
	int32 TGeometryParticleHandleImp<float, 3, true>::GetPayload<int32>(int32 Idx)
	{
		return Idx;
	}

	template <>
	template <>
	int32 TGeometryParticleHandleImp<float, 3, false>::GetPayload<int32>(int32 Idx)
	{
		return Idx;
	}

}
