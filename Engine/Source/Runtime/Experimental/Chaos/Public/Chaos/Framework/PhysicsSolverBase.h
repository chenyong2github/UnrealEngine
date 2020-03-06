// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Framework/MultiBufferResource.h"
#include "Chaos/Matrix.h"
#include "Misc/ScopeLock.h"

class UObject;
class IPhysicsProxyBase; // WTF - not in Chaos?

namespace Chaos
{

	class CHAOS_API FPhysicsSolverBase
	{
	public:

		void ChangeBufferMode(EMultiBufferMode InBufferMode);

		void AddDirtyProxy(IPhysicsProxyBase * ProxyBaseIn)
		{
			DirtyProxiesSet.Add(ProxyBaseIn);
		}
		void RemoveDirtyProxy(IPhysicsProxyBase * ProxyBaseIn)
		{
			DirtyProxiesSet.Remove(ProxyBaseIn);
		}

		const UObject* GetOwner() const
		{ 
			return Owner; 
		}

		void SetOwner(const UObject* InOwner)
		{
			Owner = InOwner;
		}


#if CHAOS_CHECKED
		void SetDebugName(const FName& Name)
		{
			DebugName = Name;
		}

		const FName& GetDebugName() const
		{
			return DebugName;
		}
#endif

	protected:

		/** Protected construction so callers still have to go through the module to create new instances */
		FPhysicsSolverBase(const EMultiBufferMode BufferingModeIn, UObject* InOwner)
			: BufferMode(BufferingModeIn)
			, Owner(InOwner)
		{}

		/** Only allow construction with valid parameters as well as restricting to module construction */
		FPhysicsSolverBase() = delete;
		FPhysicsSolverBase(const FPhysicsSolverBase& InCopy) = delete;
		FPhysicsSolverBase(FPhysicsSolverBase&& InSteal) = delete;
		FPhysicsSolverBase& operator =(const FPhysicsSolverBase& InCopy) = delete;
		FPhysicsSolverBase& operator =(FPhysicsSolverBase&& InSteal) = delete;

		/** Mode that the results buffers should be set to (single, double, triple) */
		EMultiBufferMode BufferMode;

		// Input Proxy Map
		TSet< IPhysicsProxyBase *> DirtyProxiesSet;

#if CHAOS_CHECKED
		FName DebugName;
#endif

	private:

		/** 
		 * Ptr to the engine object that is counted as the owner of this solver.
		 * Never used internally beyond how the solver is stored and accessed through the solver module.
		 * Nullptr owner means the solver is global or standalone.
		 * @see FChaosSolversModule::CreateSolver
		 */
		const UObject* Owner = nullptr;
	};
}
