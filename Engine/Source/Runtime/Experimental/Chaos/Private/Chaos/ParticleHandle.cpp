// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/ParticleHandle.h"

#include "Chaos/ImplicitObjectTransformed.h"
#include "Chaos/ImplicitObjectScaled.h"
#include "Chaos/TriangleMeshImplicitObject.h"
#include "Chaos/Framework/PhysicsProxy.h"
#include "Chaos/Framework/PhysicsSolverBase.h"
#include "Chaos/Framework/PhysicsProxyBase.h"
#include "Chaos/CastingUtilities.h"
#include "PBDRigidsSolver.h"

namespace Chaos
{
	void SetObjectStateHelper(IPhysicsProxyBase& Proxy, FPBDRigidParticleHandle& Rigid, EObjectStateType InState, bool bAllowEvents, bool bInvalidate)
	{
		if (auto PhysicsSolver = Proxy.GetSolver<Chaos::FPBDRigidsSolver>())
		{
			PhysicsSolver->GetEvolution()->SetParticleObjectState(&Rigid, InState);
		}
		else
		{
			//not in solver so just set it directly (can this possibly happen?)
			Rigid.SetObjectStateLowLevel(InState);
		}
	}

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
		else if (Implicit0OuterType == TImplicitObjectInstanced<FCapsule>::StaticType())
		{
			return const_cast<FCapsule*>(Implicit0->template GetObject<TImplicitObjectInstanced<FCapsule>>()->GetInstancedObject());
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
	void Chaos::TGeometryParticle<T, d>::MergeGeometry(TArray<TUniquePtr<FImplicitObject>>&& Objects)
	{
		ensure(MNonFrequentData.Read().Geometry());

		// we only support FImplicitObjectUnion
		ensure(MNonFrequentData.Read().Geometry()->GetType() == FImplicitObjectUnion::StaticType());

		if (MNonFrequentData.Read().Geometry()->GetType() == FImplicitObjectUnion::StaticType())
		{
			// if we are currently a union then add the new geometry to this union
			MNonFrequentData.Modify(true, MDirtyFlags, Proxy, [&Objects](auto& Data)
				{
					if (Data.AccessGeometry())
					{
						if (FImplicitObjectUnion* Union = Data.AccessGeometry()->template GetObject<FImplicitObjectUnion>())
						{
							Union->Combine(Objects);
						}
					}
				});

			UpdateShapesArray();
		}
	}

	template <typename T, int d>
	void Chaos::TGeometryParticle<T, d>::RemoveShape(FPerShapeData* InShape, bool bWakeTouching)
	{
		// NOTE: only intended use is to remove objects from inside a FImplicitObjectUnion
		CHAOS_ENSURE(MNonFrequentData.Read().Geometry()->GetType() == FImplicitObjectUnion::StaticType());

		int32 FoundIndex = INDEX_NONE;
		for (int32 Index = 0; Index < MShapesArray.Num(); Index++)
		{
			if (InShape == MShapesArray[Index].Get())
			{
				MShapesArray.RemoveAt(Index);
				FoundIndex = Index;
				break;
			}
		}

		if (MNonFrequentData.Read().Geometry()->GetType() == FImplicitObjectUnion::StaticType())
		{
			// if we are currently a union then remove geometry from this union
			MNonFrequentData.Modify(true, MDirtyFlags, Proxy, [FoundIndex](auto& Data)
				{
					if (Data.AccessGeometry())
					{
						if (FImplicitObjectUnion* Union = Data.AccessGeometry()->template GetObject<FImplicitObjectUnion>())
						{
							Union->RemoveAt(FoundIndex);
						}
					}
				});
		}

		UpdateShapesArray();
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
				if (!PerShapeData->GetSimEnabled())
				{
					return;
				}
			}
			if (bIgnoreAnalyticCollisions)
			{
				Implicit->SetCollisionType(Chaos::ImplicitObjectType::LevelSet);
				//Implicit->SetConvex(false);
			}
			else
			{
				Implicit->SetCollisionType(Implicit->GetType());
				// @todo (mlentine): Need to in theory set convex properly here
			}
		}
	}

	template class CHAOS_API TGeometryParticle<FReal, 3>;

	template class CHAOS_API TKinematicGeometryParticle<FReal, 3>;

	template class CHAOS_API TPBDRigidParticle<FReal, 3>;

	template <>
	void Chaos::TGeometryParticle<FReal, 3>::MarkDirty(const EParticleFlags DirtyBits, bool bInvalidate )
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

	const FVec3 FGenericParticleHandleHandleImp::ZeroVector = FVec3(0);
	const FRotation3 FGenericParticleHandleHandleImp::IdentityRotation = FRotation3(FQuat::Identity);
	const FMatrix33 FGenericParticleHandleHandleImp::ZeroMatrix = FMatrix33(0);
	const TUniquePtr<FBVHParticles> FGenericParticleHandleHandleImp::NullBVHParticles = TUniquePtr<FBVHParticles>();

	template <>
	template <>
	int32 TGeometryParticleHandleImp<FReal, 3, true>::GetPayload<int32>(int32 Idx)
	{
		return Idx;
	}

	template <>
	template <>
	int32 TGeometryParticleHandleImp<FReal, 3, false>::GetPayload<int32>(int32 Idx)
	{
		return Idx;
	}

}
