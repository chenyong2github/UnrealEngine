// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/ParticleHandle.h"

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
