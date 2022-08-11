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
	// @todo(Deformable) : Explore using the chaos ISolverBase, IProxyBase instead. 
	class CHAOS_API FDeformableSolver
	{
		friend class FGameThreadAccess;
		friend class FPhysicsThreadAccess;
		friend class FDeformableFun;

	public:

		FDeformableSolver(FDeformableSolverProperties InProp = FDeformableSolverProperties());

		/* Physics Thread Access API */
		class CHAOS_API FPhysicsThreadAccess
		{
		public:
			FPhysicsThreadAccess(FDeformableSolver& InSolver, const FPhysicsThreadAccessor&) : Solver(InSolver) {}

			/* Simulation Advance */
			bool Advance(FSolverReal DeltaTime);
			void Reset(const FDeformableSolverProperties&);
			void TickSimulation(FSolverReal DeltaTime);
			void UpdateOutputState(FThreadingProxy&);
			void PushPackage(int32 Frame, FOutputDataMap&& Package);

			/* Iteration Advance */
			void InitializeSimulationObjects();
			void InitializeSimulationObject(FThreadingProxy&);
			void InitializeCollisionBodies();
			void InitializeKinematicState(FThreadingProxy&);
			void InitializeSelfCollisionVariables();

			/*IO Utility*/
			void WriteTrisGEO(const Softs::FSolverParticles& Particles, const TArray<TVec3<int32>>& Mesh);
			void WriteFrame(FThreadingProxy& , const FSolverReal DeltaTime);

			const FDeformableSolverProperties& GetProperties() const { return Solver.GetProperties(); }

			FPBDEvolution* GetEvolution() { return Solver.Evolution.Get(); }
			const FPBDEvolution* GetEvolution() const { return Solver.Evolution.Get(); }

			TArrayCollectionArray<const UObject*>& GetObjectsMap() { return Solver.MObjects; }
			const TArrayCollectionArray<const UObject*>& GetObjectsMap() const { return Solver.MObjects; }


		private:
			FDeformableSolver& Solver;
		};


		/* Game Thread Access API */
		class CHAOS_API FGameThreadAccess
		{
		public:
			FGameThreadAccess(FDeformableSolver& InSolver, const FGameThreadAccessor&) : Solver(InSolver) {}

			void AddProxy(TUniquePtr<FThreadingProxy> InObject);

			TUniquePtr<FOutputPackage> PullPackage();

		private:
			FDeformableSolver& Solver;
		};

	protected:

		/* Simulation Advance */
		bool Advance(FSolverReal DeltaTime);
		void Reset(const FDeformableSolverProperties&);
		void TickSimulation(FSolverReal DeltaTime);
		void UpdateOutputState(FThreadingProxy&);
		void PushPackage(int32 Frame, FOutputDataMap&& Package);

		/* Iteration Advance */
		void InitializeSimulationObjects();
		void InitializeSimulationObject(FThreadingProxy&);
		void InitializeCollisionBodies();
		void InitializeKinematicState(FThreadingProxy&);
		void InitializeSelfCollisionVariables();
		void InitializeGridBasedConstraintVariables();

		/*IO Utility*/
		void WriteTrisGEO(const FSolverParticles& Particles, const TArray<TVec3<int32>>& Mesh);
		void WriteFrame(FThreadingProxy&, const FSolverReal DeltaTime);

		/*Game Thread API*/
		void AddProxy(TUniquePtr<FThreadingProxy> InObject);
		TUniquePtr<FOutputPackage> PullPackage();

		const FDeformableSolverProperties& GetProperties() const { return Property; }

	private:

		// connections outside the solver.
		static FCriticalSection	PackageMutex;
		TArray< TUniquePtr<FThreadingProxy> > UninitializedProxys;
		TMap< FThreadingProxy::FKey, TUniquePtr<FThreadingProxy> > Proxies;
		TArray< TUniquePtr<FOutputPackage>  > OutputPackages;

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

		FSolverReal Time = 0.f;
		int32 Frame = 0;
		bool bSimulationInitialized = false;
	};


}; // namesapce Chaos::Softs
