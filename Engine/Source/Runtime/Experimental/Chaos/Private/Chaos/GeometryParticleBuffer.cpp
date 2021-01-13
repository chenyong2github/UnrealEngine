// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/GeometryParticleBuffer.h"
#include "Chaos/PBDRigidParticleBuffer.h"
#include "Chaos/ImplicitObject.h"
#include "Chaos/Framework/PhysicsSolverBase.h"

namespace Chaos
{

void FGeometryParticleBuffer::SetX(const FVec3& InX, bool bInvalidate /* = true */)
{
	if (bInvalidate)
	{
		FPBDRigidParticleBuffer* Dyn = FPBDRigidParticleBuffer::Cast(this);
		if (Dyn && Dyn->ObjectState() == EObjectStateType::Sleeping)
		{
			Dyn->SetObjectState(EObjectStateType::Dynamic, true);
		}
	}
	MXR.Modify(bInvalidate, MDirtyFlags, Proxy, [&InX](auto& Data) { Data.SetX(InX); });
}


void FGeometryParticleBuffer::SetR(const FRotation3& InR, bool bInvalidate)
{
	if (bInvalidate)
	{
		FPBDRigidParticleBuffer* Dyn = FPBDRigidParticleBuffer::Cast(this);
		if (Dyn && Dyn->ObjectState() == EObjectStateType::Sleeping)
		{
			Dyn->SetObjectState(EObjectStateType::Dynamic, true);
		}
	}
	MXR.Modify(bInvalidate, MDirtyFlags, Proxy, [&InR](auto& Data) { Data.SetR(InR); });
}

EObjectStateType FGeometryParticleBuffer::ObjectState() const
{
	const FKinematicGeometryParticleBuffer* Kin = FKinematicGeometryParticleBuffer::Cast(this);
	return Kin ? Kin->ObjectState() : EObjectStateType::Static;
}

void FGeometryParticleBuffer::MapImplicitShapes()
{
	ImplicitShapeMap.Reset();

	for (int32 ShapeIndex = 0; ShapeIndex < MShapesArray.Num(); ++ShapeIndex)
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

inline FImplicitObject* GetInstancedImplicitHelper2(FImplicitObject* Implicit0)
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

void FGeometryParticleBuffer::SetIgnoreAnalyticCollisionsImp(FImplicitObject* Implicit, bool bIgnoreAnalyticCollisions)
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
	else if (Implicit->GetType() == TImplicitObjectTransformed<FReal, 3>::StaticType())
	{
		TImplicitObjectTransformed<FReal, 3>* TransformedImplicit = Implicit->template GetObject<TImplicitObjectTransformed<FReal, 3>>();
		SetIgnoreAnalyticCollisionsImp(const_cast<FImplicitObject*>(TransformedImplicit->GetTransformedObject()), bIgnoreAnalyticCollisions);
	}
	else if ((uint32)Implicit->GetType() & ImplicitObjectType::IsInstanced)
	{
		SetIgnoreAnalyticCollisionsImp(GetInstancedImplicitHelper2(Implicit), bIgnoreAnalyticCollisions);
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

void FGeometryParticleBuffer::MarkDirty(const EParticleFlags DirtyBits, bool bInvalidate)
{
	if (bInvalidate)
	{
		MDirtyFlags.MarkDirty(DirtyBits);

		if (Proxy)
		{
			if (FPhysicsSolverBase* PhysicsSolverBase = Proxy->GetSolver<FPhysicsSolverBase>())
			{
				PhysicsSolverBase->AddDirtyProxy(Proxy);
			}
		}
	}
}

void FGeometryParticleBuffer::SetProxy(IPhysicsProxyBase* InProxy)
{
	Proxy = InProxy;
	if (Proxy)
	{
		if (MDirtyFlags.IsDirty())
		{
			if (FPhysicsSolverBase* PhysicsSolverBase = Proxy->GetSolver<FPhysicsSolverBase>())
			{
				PhysicsSolverBase->AddDirtyProxy(Proxy);
			}
		}
	}

	for (auto& Shape : MShapesArray)
	{
		Shape->SetProxy(Proxy);
	}
}

}
