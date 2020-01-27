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
		int32 ShapeIndex = 0;
		for (TUniquePtr<TPerShapeData<T, d>>& ShapeData : MShapesArray)
		{
			ImplicitShapeMap.Add(ShapeData->Geometry.Get(), ShapeIndex++);
		}

		if (MGeometry)
		{
			int32 CurrentShapeIndex = INDEX_NONE;
			if (const auto* Union = MGeometry->template GetObject<FImplicitObjectUnion>())
			{
				for (const TUniquePtr<FImplicitObject>& ImplicitObject : Union->GetObjects())
				{
					if (ImplicitObject.Get())
					{
						if (const FImplicitObject* ImplicitChildObject = Utilities::ImplicitChildHelper(ImplicitObject.Get()))
						{
							if (ImplicitShapeMap.Contains(ImplicitObject.Get()))
							{
								ImplicitShapeMap.Add(ImplicitChildObject, ImplicitShapeMap[ImplicitObject.Get()]);
							}
							else if (ImplicitShapeMap.Contains(ImplicitChildObject))
							{
								ImplicitShapeMap.Add(ImplicitObject.Get(), ImplicitShapeMap[ImplicitChildObject]);
							}
						}
					}
				}
			}
			else 
			{
				if (const FImplicitObject* ImplicitChildObject = Utilities::ImplicitChildHelper(MGeometry.Get()))
				{
					if (ImplicitShapeMap.Contains(MGeometry.Get()))
					{
						ImplicitShapeMap.Add(ImplicitChildObject, ImplicitShapeMap[MGeometry.Get()]);
					}
					else if (ImplicitShapeMap.Contains(ImplicitChildObject))
					{
						ImplicitShapeMap.Add(MGeometry.Get(), ImplicitShapeMap[ImplicitChildObject]);
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
