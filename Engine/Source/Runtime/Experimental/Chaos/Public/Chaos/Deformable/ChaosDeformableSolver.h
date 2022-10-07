// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Chaos/Core.h"
#include "Chaos/PBDCollisionSpringConstraints.h"
#include "Chaos/PBDEvolution.h"
#include "Chaos/PBDParticles.h"
#include "Chaos/PBDSoftsSolverParticles.h"
#include "Chaos/PBDSoftsEvolutionFwd.h"
#include "Chaos/PBDTriangleMeshCollisions.h"
#include "Chaos/TriangleMesh.h"
#include "Chaos/XPBDCorotatedConstraints.h"
#include "Chaos/XPBDGridBasedCorotatedConstraints.h"
#include "Chaos/Deformable/ChaosDeformableSolverTypes.h"
#include "Chaos/Deformable/ChaosDeformableSolverProxy.h"
#include "CoreMinimal.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "Templates/UniquePtr.h"

namespace Chaos::Softs
{
	class CHAOS_API FDeformableSolver : public FPhysicsSolverEvents
	{
		friend class FGameThreadAccess;
		friend class FPhysicsThreadAccess;

	public:

		FDeformableSolver(FDeformableSolverProperties InProp = FDeformableSolverProperties());
		virtual ~FDeformableSolver();

		/* Physics Thread Access API */
		class CHAOS_API FPhysicsThreadAccess
		{
		public:
			FPhysicsThreadAccess(FDeformableSolver* InSolver, const FPhysicsThreadAccessor&) : Solver(InSolver) {}
			bool operator()() { return Solver != nullptr; }

			/* Simulation Advance */
			void UpdateProxyInputPackages();
			void Simulate(FSolverReal DeltaTime);
			void AdvanceDt(FSolverReal DeltaTime);
			void Reset(const FDeformableSolverProperties&);
			void Update(FSolverReal DeltaTime);
			void UpdateOutputState(FThreadingProxy&);
			TUniquePtr<FDeformablePackage> PullInputPackage();
			void PushOutputPackage(int32 Frame, FDeformableDataMap&& Package);

			/* Iteration Advance */
			void InitializeSimulationObjects();
			void InitializeSimulationObject(FThreadingProxy&);
			void InitializeCollisionBodies();
			void InitializeKinematicConstraint();
			void InitializeSelfCollisionVariables();
			void RemoveSimulationObjects();

			/*IO Utility*/
			void WriteTrisGEO(const Softs::FSolverParticles& Particles, const TArray<TVec3<int32>>& Mesh);
			void WriteFrame(FThreadingProxy& , const FSolverReal DeltaTime);

			const FDeformableSolverProperties& GetProperties() const { return Solver->GetProperties(); }

			FPBDEvolution* GetEvolution() { return Solver->Evolution.Get(); }
			const FPBDEvolution* GetEvolution() const { return Solver->Evolution.Get(); }

			TArrayCollectionArray<const UObject*>& GetObjectsMap() { return Solver->MObjects; }
			const TArrayCollectionArray<const UObject*>& GetObjectsMap() const { return Solver->MObjects; }


		private:
			FDeformableSolver* Solver;
		};


		/* Game Thread Access API */
		class CHAOS_API FGameThreadAccess
		{
		public:
			FGameThreadAccess(FDeformableSolver* InSolver, const FGameThreadAccessor&) : Solver(InSolver) {}
			bool operator()() { return Solver != nullptr; }

			int32 GetFrame() const { return Solver->GetFrame(); }
			bool HasObject(UObject* InObject) const;
			void AddProxy(FThreadingProxy* InObject);
			void RemoveProxy(FThreadingProxy* InObject);
			void PushInputPackage(int32 Frame, FDeformableDataMap&& InPackage);

			void SetEnableSolver(bool InbEnableSolver);
			bool GetEnableSolver();

			TUniquePtr<FDeformablePackage> PullOutputPackage();

		private:
			FDeformableSolver* Solver;
		};

	protected:

		void SetEnableSolver(bool InbEnableSolver) {FScopeLock Lock(&SolverEnabledMutex); bEnableSolver = InbEnableSolver; }
		bool GetEnableSolver() const { return bEnableSolver; }

		/* Simulation Advance */
		int32 GetFrame() const { return Frame; }
		void UpdateProxyInputPackages();
		void Simulate(FSolverReal DeltaTime);
		void AdvanceDt(FSolverReal DeltaTime);
		void Reset(const FDeformableSolverProperties&);
		void Update(FSolverReal DeltaTime);
		void UpdateOutputState(FThreadingProxy&);
		void PushOutputPackage(int32 Frame, FDeformableDataMap&& Package);
		TUniquePtr<FDeformablePackage> PullInputPackage( );

		/* Iteration Advance */
		void InitializeSimulationObjects();
		void InitializeSimulationObject(FThreadingProxy&);
		void InitializeDeformableParticles(FFleshThreadingProxy&);
		void InitializeKinematicParticles(FFleshThreadingProxy&);
		void InitializeTetrahedralConstraint(FFleshThreadingProxy&);
		void InitializeGidBasedConstraints(FFleshThreadingProxy&);
		void InitializeKinematicConstraint();
		void InitializeCollisionBodies();
		void InitializeSelfCollisionVariables();
		void InitializeGridBasedConstraintVariables();
		void RemoveSimulationObjects();


		/*IO Utility*/
		void WriteTrisGEO(const FSolverParticles& Particles, const TArray<TVec3<int32>>& Mesh);
		void WriteFrame(FThreadingProxy&, const FSolverReal DeltaTime);

		/*Game Thread API*/
		bool HasObject(UObject* InObject) const { return InitializedObjects_External.Contains(InObject); }
		void AddProxy(FThreadingProxy* InObject);
		void RemoveProxy(FThreadingProxy* InObject);
		TUniquePtr<FDeformablePackage> PullOutputPackage();
		void PushInputPackage(int32 Frame, FDeformableDataMap&& InPackage);

		const FDeformableSolverProperties& GetProperties() const { return Property; }

	private:

		// connections outside the solver.
		static FCriticalSection	InitializationMutex; // @todo(flesh) : change to threaded commands to prevent the lock. 
		static FCriticalSection	RemovalMutex; // @todo(flesh) : change to threaded commands to prevent the lock. 
		static FCriticalSection	PackageOutputMutex;
		static FCriticalSection	PackageInputMutex;
		static FCriticalSection	SolverEnabledMutex;

		TArray< FThreadingProxy* > RemovedProxys_Internal;
		TArray< FThreadingProxy* > UninitializedProxys_Internal;
		TArray< TUniquePtr<FDeformablePackage>  > BufferedInputPackages;
		TArray< TUniquePtr<FDeformablePackage>  > BufferedOutputPackages;
		TUniquePtr < FDeformablePackage > CurrentInputPackage;
		TUniquePtr < FDeformablePackage > PreviousInputPackage;

		TSet< const UObject* > InitializedObjects_External;
		TMap< FThreadingProxy::FKey, TUniquePtr<FThreadingProxy> > Proxies;


		// User Configuration
		FDeformableSolverProperties Property;


		// Simulation Variables
		TUniquePtr<Softs::FPBDEvolution> Evolution;
		TArray<TUniquePtr<Softs::FXPBDCorotatedConstraints<Softs::FSolverReal, Softs::FSolverParticles>>> CorotatedConstraints;
		TUniquePtr<Softs::FXPBDGridBasedCorotatedConstraints<Softs::FSolverReal, Softs::FSolverParticles>> GridBasedCorotatedConstraint;
		TUniquePtr<Softs::FPBDCollisionSpringConstraints> CollisionSpringConstraint;
		TUniquePtr<Softs::FPBDTriangleMeshCollisions> TriangleMeshCollisions;
		TArrayCollectionArray<const UObject*> MObjects;
		TUniquePtr <TArray<TVec3<int32>>> SurfaceElements;
		TUniquePtr <TArray<Chaos::TVec4<int32>>> AllElements;
		TUniquePtr <FTriangleMesh> SurfaceTriangleMesh;

		bool bEnableSolver = true;
		FSolverReal Time = 0.f;
		int32 Frame = 0;
		int32 Iteration = 0;
		bool bSimulationInitialized = false;
	};


}; // namesapce Chaos::Softs
