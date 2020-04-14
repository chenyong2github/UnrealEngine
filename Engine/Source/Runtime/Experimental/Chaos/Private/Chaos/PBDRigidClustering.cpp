// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/PBDRigidClustering.h"

#include "Chaos/ErrorReporter.h"
#include "Chaos/ImplicitObjectTransformed.h"
#include "Chaos/ImplicitObjectUnion.h"
#include "Chaos/Levelset.h"
#include "Chaos/MassProperties.h"
#include "Chaos/PBDRigidsEvolution.h"
#include "Chaos/PBDCollisionConstraints.h"
#include "Chaos/PBDCollisionConstraintsPGS.h"
#include "Chaos/Sphere.h"
#include "Chaos/UniformGrid.h"
#include "ChaosStats.h"
#include "Containers/Queue.h"
#include "ProfilingDebugging/ScopedTimers.h"
#include "Voronoi/Voronoi.h"
#include "Chaos/PBDRigidsEvolutionGBF.h"
#include "Chaos/PerParticleInitForce.h"
#include "Chaos/PerParticleEulerStepVelocity.h"
#include "Chaos/PerParticleEtherDrag.h"
#include "Chaos/PerParticlePBDEulerStep.h"
#include "CoreMinimal.h"

namespace Chaos
{
	//
	//  Connectivity PVar
	//
	float ClusterDistanceThreshold = 100.f;
	FAutoConsoleVariableRef CVarClusterDistance(TEXT("p.ClusterDistanceThreshold"), ClusterDistanceThreshold, TEXT("How close a cluster child must be to a contact to break off"));

	int32 UseConnectivity = 1;
	FAutoConsoleVariableRef CVarUseConnectivity(TEXT("p.UseConnectivity"), UseConnectivity, TEXT("Whether to use connectivity graph when breaking up clusters"));

	float ChildrenInheritVelocity = 1.f;
	FAutoConsoleVariableRef CVarChildrenInheritVelocity(TEXT("p.ChildrenInheritVelocity"), ChildrenInheritVelocity, TEXT("Whether children inherit parent collision velocity when declustering. 0 has no impact velocity like glass, 1 has full impact velocity like brick"));

	int32 ComputeClusterCollisionStrains = 1;
	FAutoConsoleVariableRef CVarComputeClusterCollisionStrains(TEXT("p.ComputeClusterCollisionStrains"), ComputeClusterCollisionStrains, TEXT("Whether to use collision constraints when processing clustering."));

	//
	// Update Geometry PVar
	//
	int32 MinLevelsetDimension = 4;
	FAutoConsoleVariableRef CVarMinLevelsetDimension(TEXT("p.MinLevelsetDimension"), MinLevelsetDimension, TEXT("The minimum number of cells on a single level set axis"));

	int32 MaxLevelsetDimension = 20;
	FAutoConsoleVariableRef CVarMaxLevelsetDimension(TEXT("p.MaxLevelsetDimension"), MaxLevelsetDimension, TEXT("The maximum number of cells on a single level set axis"));

	float MinLevelsetSize = 50.f;
	FAutoConsoleVariableRef CVarLevelSetResolution(TEXT("p.MinLevelsetSize"), MinLevelsetSize, TEXT("The minimum size on the smallest axis to use a level set"));

	int32 UseLevelsetCollision = 0;
	FAutoConsoleVariableRef CVarUseLevelsetCollision(TEXT("p.UseLevelsetCollision"), UseLevelsetCollision, TEXT("Whether unioned objects use levelsets"));

	int32 LevelsetGhostCells = 1;
	FAutoConsoleVariableRef CVarLevelsetGhostCells(TEXT("p.LevelsetGhostCells"), LevelsetGhostCells, TEXT("Increase the level set grid by this many ghost cells"));

	float ClusterSnapDistance = 1.f;
	FAutoConsoleVariableRef CVarClusterSnapDistance(TEXT("p.ClusterSnapDistance"), ClusterSnapDistance, TEXT(""));

	int32 MinCleanedPointsBeforeRemovingInternals = 10;
	FAutoConsoleVariableRef CVarMinCleanedPointsBeforeRemovingInternals(TEXT("p.MinCleanedPointsBeforeRemovingInternals"), MinCleanedPointsBeforeRemovingInternals, TEXT("If we only have this many clean points, don't bother removing internal points as the object is likely very small"));

	int32 MoveClustersWhenDeactivated = 0;
	FAutoConsoleVariableRef CVarMoveClustersWhenDeactivated(TEXT("p.MoveClustersWhenDeactivated"), MoveClustersWhenDeactivated, TEXT("If clusters should be moved when deactivated."));

	int32 DeactivateClusterChildren = 0;
	FAutoConsoleVariableRef CVarDeactivateClusterChildren(TEXT("p.DeactivateClusterChildren"), DeactivateClusterChildren, TEXT("If children should be decativated when broken and put into another cluster."));

	int32 MassPropertiesFromMultiChildProxy = 1;
	FAutoConsoleVariableRef CVarMassPropertiesFromMultiChildProxy(TEXT("p.MassPropertiesFromMultiChildProxy"), MassPropertiesFromMultiChildProxy, TEXT(""));

	//==========================================================================
	// Free helper functions
	//==========================================================================

	template<class T, int d>
	TVector<T, d> GetContactLocation(const FRigidBodyPointContactConstraint& Contact)
	{
		return Contact.GetLocation();
	}

	template<class T, int d>
	TVector<T, d> GetContactLocation(const FRigidBodyContactConstraintPGS& Contact)
	{
		// @todo(mlentine): Does the exact point matter?
		T MinPhi = FLT_MAX;
		TVector<T, d> MinLoc;
		for (int32 i = 0; i < Contact.Phi.Num(); ++i)
		{
			if (Contact.Phi[i] < MinPhi)
			{
				MinPhi = Contact.Phi[i];
				MinLoc = Contact.Location[i];
			}
		}
		return MinLoc;
	}

	template<class T, int d>
	T CalculatePseudoMomentum(const TPBDRigidClusteredParticles<T, d>& InParticles, const uint32 Index)
	{
		TVector<T, d> LinearPseudoMomentum = (InParticles.X(Index) - InParticles.P(Index)) * InParticles.M(Index);
		TRotation<T, d> Delta = InParticles.R(Index) * InParticles.Q(Index).Inverse();
		TVector<T, d> Axis;
		T Angle;
		Delta.ToAxisAndAngle(Axis, Angle);
		TVector<T, d> AngularPseudoMomentum = InParticles.I(Index) * (Axis * Angle);
		return LinearPseudoMomentum.Size() + AngularPseudoMomentum.Size();
	}

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::RewindAndEvolve<BGF>()"), STAT_RewindAndEvolve_BGF, STATGROUP_Chaos);
	template<class T, int d>
	void RewindAndEvolve(
		FPBDRigidsEvolutionGBF& Evolution, 
		TPBDRigidClusteredParticles<T, d>& InParticles, 
		const TSet<int32>& IslandsToRecollide, 
		const TSet<TPBDRigidParticleHandle<T, d>*> AllActivatedChildren,
		const T Dt, 
		FPBDCollisionConstraints& CollisionRule)
	{
		SCOPE_CYCLE_COUNTER(STAT_RewindAndEvolve_BGF);
		// Rewind active particles
		const TArray<int32> IslandsToRecollideArray = IslandsToRecollide.Array();
		PhysicsParallelFor(IslandsToRecollideArray.Num(), [&](int32 Idx) {
			int32 Island = IslandsToRecollideArray[Idx];
			auto Particles = Evolution.GetIslandParticles(Island); // copy
			for (int32 ArrayIdx = Particles.Num() - 1; ArrayIdx >= 0; --ArrayIdx)
			{
				auto PBDRigid = Particles[ArrayIdx]->CastToRigidParticle();
				if(PBDRigid && PBDRigid->ObjectState() == EObjectStateType::Dynamic)
				{
					if (!PBDRigid->Sleeping() && !PBDRigid->Disabled())
					{
						PBDRigid->P() = PBDRigid->X();
						PBDRigid->Q() = PBDRigid->R();
						PBDRigid->V() = PBDRigid->PreV();
						PBDRigid->W() = PBDRigid->PreW();
						continue;
					}
				}
				Particles.RemoveAtSwap(ArrayIdx);
			}
			Evolution.Integrate(MakeHandleView(Particles), Dt);
		});

		TSet<TGeometryParticleHandle<T, d>*> AllIslandParticles;
		for (int32 Island = 0; Island < Evolution.NumIslands(); ++Island)
		{
			const auto& ParticleIndices = Evolution.GetIslandParticles(Island);
			for (const auto Particle : ParticleIndices)
			{
				auto PBDRigid = Particle->CastToRigidParticle();
				if(PBDRigid && PBDRigid->ObjectState() == EObjectStateType::Dynamic)
				{
					bool bDisabled = PBDRigid->Disabled();

					// #TODO - Have to repeat checking out whether the particle is disabled matching the PFor above.
					// Move these into shared array so we only process it once
					if (!AllIslandParticles.Contains(Particle) && !bDisabled)
					{
						AllIslandParticles.Add(Particle);
					}
				}
			}
		}

		const bool bRewindOnDeclusterSolve = ChildrenInheritVelocity < 1.f;
		if (bRewindOnDeclusterSolve)
		{
			// @todo(mlentine): We can precompute internal constraints which can filter some from the narrow phase tests but may not help much

#if CHAOS_PARTICLEHANDLE_TODO
			CollisionRule.UpdateConstraints(InParticles, Evolution.GetNonDisabledIndices(), Dt, AllActivatedChildren, AllIslandParticles.Array());
#else
			CollisionRule.UpdateConstraints(Dt, AllIslandParticles);	//this seems wrong
#endif

			Evolution.InitializeAccelerationStructures();

			// Resolve collisions
			PhysicsParallelFor(IslandsToRecollide.Num(), [&](int32 Island) {
				// @todo(mlentine): This is heavy handed and probably can be simplified as we know only a little bit changed.
				Evolution.UpdateAccelerationStructures(Island);
				Evolution.ApplyConstraints(Dt, Island);
				// @todo(ccaulfield): should we also update velocities here? Evolution does...
				Evolution.ApplyPushOut(Dt, Island);
				// @todo(ccaulfield): support sleep state update on evolution
				//Evolution.UpdateSleepState(Island);
			});
		}
	}

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::UpdateClusterMassProperties()"), STAT_UpdateClusterMassProperties, STATGROUP_Chaos);
	template<class T, int d>
	void UpdateClusterMassProperties(
		Chaos::TPBDRigidClusteredParticleHandle<float, 3>* Parent, 
		TSet<TPBDRigidParticleHandle<T, d>*>& Children, 
		const TRigidTransform<T, d>* ForceMassOrientation)
	{
		SCOPE_CYCLE_COUNTER(STAT_UpdateClusterMassProperties);
		check(Children.Num());

		Parent->SetX(TVector<T, d>(0));
		Parent->SetR(TRotation<T, d>(FQuat::MakeFromEuler(TVector<T, d>(0))));
		Parent->SetV(TVector<T, d>(0));
		Parent->SetW(TVector<T, d>(0));
		Parent->SetM(0);
		Parent->SetI(PMatrix<T, d, d>(0));

		bool bHasChild = false;
		bool bHasProxyChild = false;
		for (TPBDRigidParticleHandle<T, d>* OriginalChild : Children)
		{
			FMultiChildProxyId MultiChildProxyId; // sizeof(FMultiChildProxyId) = sizeof(void*), so copy
			TMultiChildProxyData<T, d>* ProxyData = nullptr;
			if (TPBDRigidClusteredParticleHandle<T, d>* ClusteredOriginalChild = OriginalChild->CastToClustered())
			{
				MultiChildProxyId = ClusteredOriginalChild->MultiChildProxyId();
				ProxyData = ClusteredOriginalChild->MultiChildProxyData().Get();
			}

			//int32 Child;
			TPBDRigidParticleHandle<T, d>* Child;
			TVector<T, d> ChildPosition;
			TRotation<T, d> ChildRotation;
			if (MultiChildProxyId.Id == nullptr)
			{
				Child = OriginalChild;
				ChildPosition = Child->X();
				ChildRotation = Child->R();
			}
			else if (ProxyData->KeyChild == OriginalChild)
			{
				Child = MultiChildProxyId.Id;

				const TRigidTransform<T, d> ProxyWorldTM =
					ProxyData->RelativeToKeyChild *
					TRigidTransform<T, d>(
						OriginalChild->X(), OriginalChild->R());
				ChildPosition = ProxyWorldTM.GetLocation();
				ChildRotation = ProxyWorldTM.GetRotation();
			}
			else
			{
				continue; //using a proxy but we are not the key child
			}

			const T ChildMass = Child->M();
			const PMatrix<T, d, d> ChildWorldSpaceI = 
				(ChildRotation * FMatrix::Identity) * Child->I() * (ChildRotation * FMatrix::Identity).GetTransposed();
			if (ChildWorldSpaceI.ContainsNaN())
			{
				continue;
			}
			bHasProxyChild = true;
			bHasChild = true;
			bHasProxyChild = true;
			Parent->I() += ChildWorldSpaceI;
			Parent->M() += ChildMass;
			Parent->X() += ChildPosition * ChildMass;
			Parent->V() += OriginalChild->V() * ChildMass; // Use orig child for vel because we don't sim the proxy
			Parent->W() += OriginalChild->W() * ChildMass;
		}
		if (!ensure(bHasProxyChild))
		{
			for (TPBDRigidParticleHandle<T, d>* OriginalChild : Children)
			{
				TPBDRigidParticleHandle<T, d>* Child = OriginalChild;
				const TVector<T, d>& ChildPosition = Child->X();
				const TRotation<T, d>& ChildRotation = Child->R();
				const T ChildMass = Child->M();

				const PMatrix<T, d, d> ChildWorldSpaceI = 
					(ChildRotation * FMatrix::Identity) * Child->I() * (ChildRotation * FMatrix::Identity).GetTransposed();
				if (ChildWorldSpaceI.ContainsNaN())
				{
					continue;
				}
				bHasChild = true;
				Parent->I() += ChildWorldSpaceI;
				Parent->M() += ChildMass;
				Parent->X() += ChildPosition * ChildMass;
				Parent->V() += OriginalChild->V() * ChildMass; // Use orig child for vel because we don't sim the proxy
				Parent->W() += OriginalChild->W() * ChildMass;
			}
		}
		for (int32 i = 0; i < d; i++)
		{
			const PMatrix<T, d, d>& InertiaTensor = Parent->I();
			if (InertiaTensor.GetColumn(i)[i] < SMALL_NUMBER)
			{
				Parent->SetI(PMatrix<T, d, d>(1.f, 1.f, 1.f));
				break;
			}
		}

		if (!ensure(bHasChild) || !ensure(Parent->M() > SMALL_NUMBER))
		{
			Parent->M() = 1.0;
			Parent->X() = TVector<T, d>(0);
			Parent->V() = TVector<T, d>(0);
			Parent->PreV() = Parent->V();
			Parent->InvM() = 1;
			Parent->P() = Parent->X();
			Parent->W() = TVector<T, d>(0);
			Parent->PreW() = Parent->W();
			Parent->R() = TRotation<T, d>(FMatrix::Identity);
			Parent->Q() = Parent->R();
			Parent->I() = FMatrix::Identity;
			Parent->InvI() = FMatrix::Identity;
			return;
		}

		check(Parent->M() > SMALL_NUMBER);

		Parent->X() /= Parent->M();
		Parent->V() /= Parent->M();
		Parent->PreV() = Parent->V();
		Parent->InvM() = 1. / Parent->M();
		if (ForceMassOrientation)
		{
			Parent->X() = ForceMassOrientation->GetLocation();
		}
		Parent->P() = Parent->X();
		for (TPBDRigidParticleHandle<T, d>* OriginalChild : Children)
		{
			FMultiChildProxyId MultiChildProxyId; // sizeof(FMultiChildProxyId) = sizeof(void*), so copy
			TMultiChildProxyData<T, d>* ProxyData = nullptr;
			if (TPBDRigidClusteredParticleHandle<T, d>* ClusteredOriginalChild = OriginalChild->CastToClustered())
			{
				MultiChildProxyId = ClusteredOriginalChild->MultiChildProxyId();
				ProxyData = ClusteredOriginalChild->MultiChildProxyData().Get();
			}

			TPBDRigidParticleHandle<T, d>* Child;
			TVector<T, d> ChildPosition;
			if (MultiChildProxyId.Id == nullptr)
			{
				Child = OriginalChild;
				ChildPosition = Child->X();
			}
			else if (ProxyData && ProxyData->KeyChild == OriginalChild)
			{
				Child = MultiChildProxyId.Id;
				const TRigidTransform<T, d> ProxyWorldTM = 
					ProxyData->RelativeToKeyChild * TRigidTransform<T, d>(OriginalChild->X(), OriginalChild->R());
				ChildPosition = ProxyWorldTM.GetLocation();
			}
			else
			{
				continue; //using a proxy but we are not the key child
			}

			TVector<T, d> ParentToChild = ChildPosition - Parent->X();

			const T ChildMass = Child->M();
			// taking v from original child since we are not actually simulating the proxy child
			Parent->W() += 
				TVector<T, d>::CrossProduct(ParentToChild, 
					OriginalChild->V() * ChildMass);
			{
				const T& p0 = ParentToChild[0];
				const T& p1 = ParentToChild[1];
				const T& p2 = ParentToChild[2];
				const T& m = ChildMass;
				Parent->I() += 
					PMatrix<T, d, d>(
						m * (p1 * p1 + p2 * p2), -m * p1 * p0, -m * p2 * p0, 
						m * (p2 * p2 + p0 * p0), -m * p2 * p1, m * (p1 * p1 + p0 * p0));
			}
		}
		PMatrix<T, d, d>& InertiaTensor = Parent->I();
		if (Parent->I().ContainsNaN())
		{
			InertiaTensor = PMatrix<T, d, d>(1.f, 1.f, 1.f);
		}
		else
		{
			for (int32 i = 0; i < d; i++)
			{
				if (InertiaTensor.GetColumn(i)[i] < SMALL_NUMBER)
				{
					InertiaTensor = PMatrix<T, d, d>(1.f, 1.f, 1.f);
					break;
				}
			}
		}
		Parent->W() /= Parent->M();
		Parent->PreW() = Parent->W();
		Parent->R() = Chaos::TransformToLocalSpace<T, d>(InertiaTensor);
		if (ForceMassOrientation)
		{
			Parent->R() = ForceMassOrientation->GetRotation();
		}
		Parent->Q() = Parent->R();
		Parent->InvI() = Parent->I().Inverse();
	}

	//==========================================================================
	// TPBDRigidClustering
	//==========================================================================

	template<class T_FPBDRigidsEvolution, class T_FPBDCollisionConstraint, class T, int d>
	TPBDRigidClustering<T_FPBDRigidsEvolution, T_FPBDCollisionConstraint, T, d>::TPBDRigidClustering(
		T_FPBDRigidsEvolution& InEvolution, 
		TPBDRigidClusteredParticles<T, d>& InParticles)
		: MEvolution(InEvolution)
		, MParticles(InParticles)
		, MCollisionImpulseArrayDirty(true)
		, DoGenerateBreakingData(false)
		, MClusterConnectionFactor(1.0)
		, MClusterUnionConnectionType(FClusterCreationParameters<T>::EConnectionMethod::DelaunayTriangulation)
	{}

	template<class T_FPBDRigidsEvolution, class T_FPBDCollisionConstraint, class T, int d>
	TPBDRigidClustering<T_FPBDRigidsEvolution, T_FPBDCollisionConstraint, T, d>::~TPBDRigidClustering()
	{}

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::CreateClusterParticle"), STAT_CreateClusterParticle, STATGROUP_Chaos);
	template<class T_FPBDRigidsEvolution, class T_FPBDCollisionConstraint, class T, int d>
	Chaos::TPBDRigidClusteredParticleHandle<float, 3>* 
	TPBDRigidClustering<T_FPBDRigidsEvolution, T_FPBDCollisionConstraint, T, d>::CreateClusterParticle(
		const int32 ClusterGroupIndex,
		TArray<Chaos::TPBDRigidParticleHandle<T,d>*>&& Children,
		const FClusterCreationParameters<T>& Parameters,
		TSharedPtr<Chaos::FImplicitObject, ESPMode::ThreadSafe> ProxyGeometry,
		const TRigidTransform<T, d>* ForceMassOrientation)
	{
		SCOPE_CYCLE_COUNTER(STAT_CreateClusterParticle);

		Chaos::TPBDRigidClusteredParticleHandle<float, 3>* NewParticle = Parameters.ClusterParticleHandle;
		if (!NewParticle)
		{
			NewParticle = MEvolution.CreateClusteredParticles(1)[0]; // calls Evolution.DirtyParticle()
		}

		// Must do this so that the constraint graph knows about this particle 
		// prior to calling CreateIslands().  We could call MEvolution.CreateParticle()
		// which does the same thing, but also calls DirtyParticle(), which is already
		// done by MEvolution.CreateClusteredParticles(), and will be done again by
		// MEvolution.EnableParticle().
		//MEvolution.GetConstraintGraph().AddParticle(NewParticle); // PBDRigidsEvolutionGBF protects GetConstraintGraph().  Bah!
		MEvolution.CreateParticle(NewParticle); // Doesn't create, just adds to constraint graph
		MEvolution.EnableParticle(NewParticle, nullptr); // null for parent skips constraint graph EnableParticle()
		NewParticle->SetCollisionGroup(INT_MAX);
		TopLevelClusterParents.Add(NewParticle);

		NewParticle->SetInternalCluster(false);
		NewParticle->SetClusterId(ClusterId(nullptr, Children.Num()));
		NewParticle->SetClusterGroupIndex(ClusterGroupIndex);
		NewParticle->SetStrains(0.0);

		//
		// Update clustering data structures.
		//

		if (MChildren.Contains(NewParticle))
		{
			MChildren[NewParticle] = MoveTemp(Children);
		}
		else
		{
			MChildren.Add(NewParticle, MoveTemp(Children));
		}
		const TArray<TPBDRigidParticleHandle<T, d>*>& ChildrenArray = MChildren[NewParticle];
		TSet<TPBDRigidParticleHandle<T, d>*> ChildrenSet(ChildrenArray);

		// Disable the children
		MEvolution.DisableParticles(reinterpret_cast<TSet<TGeometryParticleHandle<T, d>*>&>(ChildrenSet));

		bool bClusterIsAsleep = true;
		for (TPBDRigidParticleHandle<T, d>* Child : ChildrenSet)
		{
			bClusterIsAsleep &= Child->Sleeping();

			if (TPBDRigidClusteredParticleHandle<T, d>* ClusteredChild = Child->CastToClustered())
			{
				TopLevelClusterParents.Remove(ClusteredChild);

				// Cluster group id 0 means "don't union with other things"
				// TODO: Use INDEX_NONE instead of 0?
				ClusteredChild->SetClusterGroupIndex(0);
				ClusteredChild->ClusterIds().Id = NewParticle;
				NewParticle->Strains() += ClusteredChild->Strains();

				NewParticle->SetCollisionImpulses(
					FMath::Max(NewParticle->CollisionImpulses(), ClusteredChild->CollisionImpulses()));

				const int32 NewCG = NewParticle->CollisionGroup();
				const int32 ChildCG = ClusteredChild->CollisionGroup();
				NewParticle->SetCollisionGroup(NewCG < ChildCG ? NewCG : ChildCG);
			}
		}
		if (ChildrenSet.Num())
		{
			NewParticle->Strains() /= ChildrenSet.Num();
		}

		ensureMsgf(!ProxyGeometry || ForceMassOrientation, TEXT("If ProxyGeometry is passed, we must override the mass orientation as they are tied"));

		UpdateMassProperties(NewParticle, ChildrenSet, ForceMassOrientation);
		UpdateGeometry(NewParticle, ChildrenSet, ProxyGeometry, Parameters);
		GenerateConnectionGraph(NewParticle, Parameters);
		NewParticle->SetSleeping(bClusterIsAsleep);
		if (ClusterGroupIndex) AddToClusterUnion(ClusterGroupIndex, NewParticle);
		return NewParticle;
	}

	int32 UnionsHaveCollisionParticles = 0;
	FAutoConsoleVariableRef CVarUnionsHaveCollisionParticles(TEXT("p.UnionsHaveCollisionParticles"), UnionsHaveCollisionParticles, TEXT(""));

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::CreateClusterParticleFromClusterChildren"), STAT_CreateClusterParticleFromClusterChildren, STATGROUP_Chaos);
	template<class T_FPBDRigidsEvolution, class T_FPBDCollisionConstraint, class T, int d>
	Chaos::TPBDRigidClusteredParticleHandle<float, 3>* 
	TPBDRigidClustering<T_FPBDRigidsEvolution, T_FPBDCollisionConstraint, T, d>::CreateClusterParticleFromClusterChildren(
		TArray<TPBDRigidParticleHandle<T,d>*>&& Children, 
		TPBDRigidClusteredParticleHandle<T,d>* Parent, 
		const TRigidTransform<T, d>& ClusterWorldTM, 
		const FClusterCreationParameters<T>& Parameters)
	{
		SCOPE_CYCLE_COUNTER(STAT_CreateClusterParticleFromClusterChildren);

		//This cluster is made up of children that are currently in a cluster. This means we don't need to update or disable as much
		Chaos::TPBDRigidClusteredParticleHandle<float, 3>* NewParticle = Parameters.ClusterParticleHandle;
		if (!NewParticle)
		{
			NewParticle = MEvolution.CreateClusteredParticles(1)[0]; // calls Evolution.DirtyParticle()
		}
		MEvolution.CreateParticle(NewParticle);
		MEvolution.EnableParticle(NewParticle, Parent);

		NewParticle->SetCollisionGroup(INT_MAX);
		TopLevelClusterParents.Add(NewParticle);
		NewParticle->SetInternalCluster(true);
		NewParticle->SetClusterId(ClusterId(Parent, Children.Num()));
		for (auto& Constituent : Children) MEvolution.DoInternalParticleInitilization(Constituent, NewParticle);

		//
		// Update clustering data structures.
		//
		if (MChildren.Contains(NewParticle))
		{
			MChildren[NewParticle] = MoveTemp(Children);
		}
		else
		{
			MChildren.Add(NewParticle, MoveTemp(Children));
		}

		TArray<TPBDRigidParticleHandle<T, d>*>& ChildrenArray = MChildren[NewParticle];
		//child transforms are out of date, need to update them. @todo(ocohen): if children transforms are relative we would not need to update this, but would simply have to do a final transform on the new cluster index
		// TODO(mlentine): Why is this not needed? (Why is it ok to have DeactivateClusterChildren==false?)
		if (DeactivateClusterChildren)
		{
			//TODO: avoid iteration just pass in a view
			TSet<TGeometryParticleHandle<T, d>*> ChildrenHandles(static_cast<TArray<TGeometryParticleHandle<T,d>*>>(ChildrenArray));
			MEvolution.DisableParticles(ChildrenHandles);
		}
		for (TPBDRigidParticleHandle<T, d>* Child : ChildrenArray)
		{
			if (TPBDRigidClusteredParticleHandle<T, d>* ClusteredChild = Child->CastToClustered())
			{
				TRigidTransform<T, d> ChildFrame = ClusteredChild->ChildToParent() * ClusterWorldTM;
				ClusteredChild->SetX(ChildFrame.GetTranslation());
				ClusteredChild->SetR(ChildFrame.GetRotation());
				ClusteredChild->ClusterIds().Id = NewParticle;
				ClusteredChild->SetClusterGroupIndex(0);
				if (DeactivateClusterChildren)
				{
					TopLevelClusterParents.Remove(ClusteredChild);
				}

				ClusteredChild->SetCollisionImpulses(FMath::Max(NewParticle->CollisionImpulses(), ClusteredChild->CollisionImpulses()));
				Child->SetCollisionGroup(FMath::Min(NewParticle->CollisionGroup(), Child->CollisionGroup()));
			}
		}

		FClusterCreationParameters<T> NoCleanParams = Parameters;
		NoCleanParams.bCleanCollisionParticles = false;
		NoCleanParams.bCopyCollisionParticles = !!UnionsHaveCollisionParticles;

		TSet<TPBDRigidParticleHandle<T, d>*> ChildrenSet(ChildrenArray);
		UpdateMassProperties(NewParticle, ChildrenSet, nullptr);
		UpdateGeometry(NewParticle, ChildrenSet, nullptr, NoCleanParams);

		return NewParticle;
	}

	int32 UseMultiChildProxy = 1;
	FAutoConsoleVariableRef CVarUseMultiChildProxy(TEXT("p.UseMultiChildProxy"), UseMultiChildProxy, TEXT("Whether to merge multiple children into a single collision proxy when one is available"));

	int32 MinChildrenForMultiProxy = 1;
	FAutoConsoleVariableRef CVarMinChildrenForMultiProxy(TEXT("p.MinChildrenForMultiProxy"), MinChildrenForMultiProxy, TEXT("Min number of children needed for multi child proxy optimization"));

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::UnionClusterGroups"), STAT_UnionClusterGroups, STATGROUP_Chaos);
	template<class T_FPBDRigidsEvolution, class T_FPBDCollisionConstraint, class T, int d>
	void TPBDRigidClustering<T_FPBDRigidsEvolution, T_FPBDCollisionConstraint, T, d>::UnionClusterGroups()
	{
		SCOPE_CYCLE_COUNTER(STAT_UnionClusterGroups);
		if (ClusterUnionMap.Num())
		{
			TMap < TPBDRigidParticleHandle<T, 3>*, TPBDRigidParticleHandle<T, 3>*> ClusterParents;
			TMap < int32, TArray< TPBDRigidParticleHandle<T, 3>*>> NewClusterGroups;
			for (TTuple<int32, TArray<TPBDRigidClusteredParticleHandle<T, 3>* >>& Group : ClusterUnionMap)
			{
				int32 ClusterGroupID = Group.Key;
				TArray<TPBDRigidClusteredParticleHandle<T, 3>* > Handles = Group.Value;

				if (Handles.Num() > 1)
				{
					if (!NewClusterGroups.Contains(ClusterGroupID))
						NewClusterGroups.Add(ClusterGroupID, TArray < TPBDRigidParticleHandle<T, 3>*>());

					TArray<TPBDRigidParticleHandle<T, 3>*> ClusterBodies;
					for (TPBDRigidClusteredParticleHandle<T, 3>* ActiveCluster : Handles)
					{
						if (!ActiveCluster->Disabled())
						{
							TSet<TPBDRigidParticleHandle<T, 3>*> Children = ReleaseClusterParticles(ActiveCluster, nullptr, true);
							NewClusterGroups[ClusterGroupID].Append(Children.Array());
							for (auto& Child : Children) ClusterParents.Add(Child, ActiveCluster);
						}
					}
				}
			}

			for (TTuple<int32, TArray<TPBDRigidParticleHandle<T, 3>* >>& Group : NewClusterGroups)
			{
				int32 ClusterGroupID = Group.Key;
				TArray< TPBDRigidParticleHandle<T, 3> *> ActiveCluster = Group.Value;
				TPBDRigidClusteredParticleHandle<T, 3>* NewCluster = CreateClusterParticle(-FMath::Abs(ClusterGroupID), MoveTemp(Group.Value));
				NewCluster->SetInternalCluster(true);
				for (auto& Constituent : ActiveCluster) MEvolution.DoInternalParticleInitilization( ClusterParents[Constituent], NewCluster);
			}
			ClusterUnionMap.Empty();
		}
	}


	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::DeactivateClusterParticle"), STAT_DeactivateClusterParticle, STATGROUP_Chaos);
	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	TSet<TPBDRigidParticleHandle<T, d>*> TPBDRigidClustering<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::DeactivateClusterParticle(
		TPBDRigidClusteredParticleHandle<T,d>* ClusteredParticle)
	{
		SCOPE_CYCLE_COUNTER(STAT_DeactivateClusterParticle);

		TSet<TPBDRigidParticleHandle<T, d>*> ActivatedChildren;
		check(!ClusteredParticle->Disabled());
		if (MChildren.Contains(ClusteredParticle))
		{
			ActivatedChildren = ReleaseClusterParticles(MChildren[ClusteredParticle]);
		}
		return ActivatedChildren;
	}

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::ReleaseClusterParticles(STRAIN)"), STAT_ReleaseClusterParticles_STRAIN, STATGROUP_Chaos);
	template<class T_FPBDRigidsEvolution, class T_FPBDCollisionConstraint, class T, int d>
	TSet<TPBDRigidParticleHandle<T, d>*> 
	TPBDRigidClustering<T_FPBDRigidsEvolution, T_FPBDCollisionConstraint, T, d>::ReleaseClusterParticles(
		TPBDRigidClusteredParticleHandle<T,d>* ClusteredParticle,
		const TMap<TGeometryParticleHandle<T, d>*, float>* ExternalStrainMap,
		bool bForceRelease)
	{
		SCOPE_CYCLE_COUNTER(STAT_ReleaseClusterParticles_STRAIN);

		TSet<TPBDRigidParticleHandle<T, d>*> ActivatedChildren;
		if (!ensureMsgf(MChildren.Contains(ClusteredParticle), TEXT("Removing Cluster that does not exist!")))
		{
			return ActivatedChildren;
		}
		TArray<TPBDRigidParticleHandle<T, d>*>& Children = MChildren[ClusteredParticle];

		bool bChildrenChanged = false;
		const bool bRewindOnDecluster = ChildrenInheritVelocity < 1;
		const TRigidTransform<T, d> PreSolveTM = 
			bRewindOnDecluster ? 
			TRigidTransform<T, d>(ClusteredParticle->X(), ClusteredParticle->R()) : 
			TRigidTransform<T, d>(ClusteredParticle->P(), ClusteredParticle->Q());

		//@todo(ocohen): iterate with all the potential parents at once?
		//find all children within some distance of contact point

		auto RemoveChildLambda = [&](TPBDRigidParticleHandle<T, d>* Child/*, const int32 Idx*/)
		{
			TPBDRigidClusteredParticleHandle<T,d>* ClusteredChild = Child->CastToClustered();

			MEvolution.EnableParticle(Child, ClusteredParticle);
			TopLevelClusterParents.Add(ClusteredChild);

			//make sure to remove multi child proxy if it exists
			ClusteredChild->MultiChildProxyData().Reset();
			ClusteredChild->MultiChildProxyId().Id = nullptr;
			ClusteredChild->SetClusterId(ClusterId(nullptr, ClusteredChild->ClusterIds().NumChildren)); // clear Id but retain number of children

			const TRigidTransform<T, d> ChildFrame = ClusteredChild->ChildToParent() * PreSolveTM;
			Child->SetX(ChildFrame.GetTranslation());
			Child->SetR(ChildFrame.GetRotation());

			if (!bRewindOnDecluster)
			{
				Child->SetP(Child->X());
				Child->SetQ(Child->R());
			}

			//todo(ocohen): for now just inherit velocity at new COM. This isn't quite right for rotation
			//todo(ocohen): in the presence of collisions, this will leave all children with the post-collision
			// velocity. This should be controlled by material properties so we can allow the broken pieces to
			// maintain the clusters pre-collision velocity.
			Child->SetV(ClusteredParticle->V());
			Child->SetW(ClusteredParticle->W());
			Child->SetPreV(ClusteredParticle->PreV());
			Child->SetPreW(ClusteredParticle->PreW());

			ActivatedChildren.Add(Child);
			//if (ChildIdx != INDEX_NONE)
			//{
			//	Children.RemoveAtSwap(ChildIdx, 1, /*bAllowShrinking=*/false); //@todo(ocohen): maybe avoid this until we know all children are not going away?
			//}

			bChildrenChanged = true;
		};

		for (int32 ChildIdx = Children.Num() - 1; ChildIdx >= 0; --ChildIdx)
		{
			TPBDRigidClusteredParticleHandle<T, d>* Child = Children[ChildIdx]->CastToClustered();
			if (!Child)
				continue;
			if ((ExternalStrainMap && (ExternalStrainMap->Find(Child))) ||
			   (!ExternalStrainMap && Child->CollisionImpulses() >= Child->Strain()) ||
				bForceRelease)
			{
				// The piece that hits just breaks off - we may want more control 
				// by looking at the edges of this piece which would give us cleaner 
				// breaks (this approach produces more rubble)
				RemoveChildLambda(Child);

				// Remove from the children array without freeing memory yet. 
				// We're looping over Children and it'd be silly to free the array
				// 1 entry at a time.
				Children.RemoveAtSwap(ChildIdx, 1, false);

				if (Child->ToBeRemovedOnFracture())
				{
					MActiveRemovalIndices.Add(Child);
				}
				else
				{
					if (DoGenerateBreakingData)
					{
						const int32 NewIdx = MAllClusterBreakings.Add(TBreakingData<float, 3>());
						TBreakingData<float, 3>& ClusterBreak = MAllClusterBreakings[NewIdx];
						ClusterBreak.Particle = Child;
						ClusterBreak.ParticleProxy = nullptr;
						ClusterBreak.Location = Child->X();
						ClusterBreak.Velocity = Child->V();
						ClusterBreak.AngularVelocity = Child->W();
						ClusterBreak.Mass = Child->M();
					}
				}
			}
		}

		if (bChildrenChanged)
		{
			if (Children.Num() == 0)
			{
				// Free the memory if we can do so cheaply (no data copies).
				Children.Empty(); 
			}

			if (UseConnectivity)
			{
				// The cluster may have contained forests, so find the connected pieces and cluster them together.

				//first update the connected graph of the children we already removed
				for (TPBDRigidParticleHandle<T,d>* Child : ActivatedChildren)
				{
					RemoveNodeConnections(Child);
				}

				if (Children.Num())
				{
					TArray<TArray<TPBDRigidParticleHandle<T,d>*>> ConnectedPiecesArray;

					{ // tmp scope

						//traverse connectivity and see how many connected pieces we have
						TSet<TPBDRigidParticleHandle<T, d>*> ProcessedChildren;
						ProcessedChildren.Reserve(Children.Num());

						for (TPBDRigidParticleHandle<T, d>* PotentialActivatedChild : Children)
						{
							if (ProcessedChildren.Contains(PotentialActivatedChild))
							{
								continue;
							}
							ConnectedPiecesArray.AddDefaulted();
							TArray<TPBDRigidParticleHandle<T, d>*>& ConnectedPieces = ConnectedPiecesArray.Last();

							TArray<TPBDRigidParticleHandle<T, d>*> ProcessingQueue;
							ProcessingQueue.Add(PotentialActivatedChild);
							while (ProcessingQueue.Num())
							{
								TPBDRigidParticleHandle<T, d>* Child = ProcessingQueue.Pop();
								if (!ProcessedChildren.Contains(Child))
								{
									ProcessedChildren.Add(Child);
									ConnectedPieces.Add(Child);
									for (const TConnectivityEdge<T>& Edge : Child->CastToClustered()->ConnectivityEdges())
									{
										if (!ProcessedChildren.Contains(Edge.Sibling))
										{
											ProcessingQueue.Add(Edge.Sibling);
										}
									}
								}
							}
						}
					} // tmp scope

					int32 NumNewClusters = 0;
					for (TArray<TPBDRigidParticleHandle<T,d>*>& ConnectedPieces : ConnectedPiecesArray)
					{
						if (ConnectedPieces.Num() == 1) //need to break single pieces first in case multi child proxy needs to be invalidated
						{
							TPBDRigidParticleHandle<T,d>* Child = ConnectedPieces[0];
							RemoveChildLambda(Child);
						}
						else if (ConnectedPieces.Num() > 1)
						{
							NumNewClusters++;
						}
					}
					TArray<Chaos::TPBDRigidClusteredParticleHandle<float,3>*> NewClusterHandles = 
						MEvolution.CreateClusteredParticles(NumNewClusters);
					int32 ClusterHandlesIdx = 0;
					for (TArray<TPBDRigidParticleHandle<T,d>*>& ConnectedPieces : ConnectedPiecesArray)
					{
						if (ConnectedPieces.Num() > 1) //now build the remaining pieces
						{
							Chaos::FClusterCreationParameters<float> CreationParameters;
							CreationParameters.ClusterParticleHandle = NewClusterHandles[ClusterHandlesIdx++];
							Chaos::TPBDRigidClusteredParticleHandle<float, 3>* NewCluster = 
								CreateClusterParticleFromClusterChildren(
									MoveTemp(ConnectedPieces), 
									ClusteredParticle, 
									PreSolveTM, 
									CreationParameters);

							NewCluster->SetStrain(ClusteredParticle->Strain());
							MEvolution.SetPhysicsMaterial(
								NewCluster, MEvolution.GetPhysicsMaterial(ClusteredParticle));

							NewCluster->SetV(ClusteredParticle->V());
							NewCluster->SetW(ClusteredParticle->W());
							NewCluster->SetPreV(ClusteredParticle->PreV());
							NewCluster->SetPreW(ClusteredParticle->PreW());
							NewCluster->SetP(NewCluster->X());
							NewCluster->SetQ(NewCluster->R());

							ActivatedChildren.Add(NewCluster);
						}
					}
				}
			}

			for (TPBDRigidParticleHandle<T,d>* Child : ActivatedChildren)
			{
				UpdateKinematicProperties(Child);
			}

			//disable cluster
			DisableCluster(ClusteredParticle);
		} // bChildrenChanged

		return ActivatedChildren;
	}

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::ReleaseClusterParticles(LIST)"), STAT_ReleaseClusterParticles_LIST, STATGROUP_Chaos);
	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	TSet<TPBDRigidParticleHandle<T, d>*> TPBDRigidClustering<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::ReleaseClusterParticles(
		TArray<TPBDRigidParticleHandle<T, d>*> ChildrenParticles)
	{
		SCOPE_CYCLE_COUNTER(STAT_ReleaseClusterParticles_LIST);
		TSet<TPBDRigidParticleHandle<T, d>*> ActivatedBodies;
		if (ChildrenParticles.Num())
		{
			TPBDRigidParticleHandle<float, 3>* ClusterHandle = nullptr;
			//todo(ocohen): refactor incoming, for now just assume these all belong to same cluster and hack strain array
			
			TMap<TGeometryParticleHandle<T, d>*, float> FakeStrain;

			bool bPreDoGenerateData = DoGenerateBreakingData;
			DoGenerateBreakingData = false;

			for (TPBDRigidParticleHandle<T, d>* ChildHandle : ChildrenParticles)
			{
				if (TPBDRigidClusteredParticleHandle<T, d>* ClusteredChildHandle = ChildHandle->CastToClustered())
				{
					if (ClusteredChildHandle->Disabled() && ClusteredChildHandle->ClusterIds().Id != nullptr)
					{
						if (ensure(!ClusterHandle || ClusteredChildHandle->ClusterIds().Id == ClusterHandle))
						{
							FakeStrain.Add(ClusteredChildHandle, TNumericLimits<float>::Max());
							ClusterHandle = ClusteredChildHandle->ClusterIds().Id;
						}
						else
						{
							break; //shouldn't be here
						}
					}
				}
			}
			if (ClusterHandle)
			{
				ActivatedBodies = ReleaseClusterParticles(ClusterHandle->CastToClustered(), &FakeStrain);
			}
			DoGenerateBreakingData = bPreDoGenerateData;
		}
		return ActivatedBodies;
	}

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::AdvanceClustering"), STAT_AdvanceClustering, STATGROUP_Chaos);
	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::Update Impulse from Strain"), STAT_UpdateImpulseStrain, STATGROUP_Chaos);
	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::Update Dirty Impulses"), STAT_UpdateDirtyImpulses, STATGROUP_Chaos);
	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::Rewind"), STAT_ClusterRewind, STATGROUP_Chaos);
	template<class T_FPBDRigidsEvolution, class T_FPBDCollisionConstraint, class T, int d>
	void 
	TPBDRigidClustering<T_FPBDRigidsEvolution, T_FPBDCollisionConstraint, T, d>::AdvanceClustering(
		const T Dt, 
		T_FPBDCollisionConstraint& CollisionRule)
	{
		SCOPE_CYCLE_COUNTER(STAT_AdvanceClustering);
		UE_LOG(LogChaos, Verbose, TEXT("START FRAME with Dt %f"), Dt);

		double FrameTime = 0, Time = 0;
		FDurationTimer Timer(Time);
		Timer.Start();

		{
			const float Threshold = 1.0f;
			TSet<Chaos::TPBDRigidParticleHandle<float, 3>*> RemovalIndicesCopy = MActiveRemovalIndices;
			for (Chaos::TPBDRigidParticleHandle<float, 3>* Particle : RemovalIndicesCopy)
			{
				//if (MParticles.ToBeRemovedOnFracture(ParticleIdx) && MParticles.V(ParticleIdx).SizeSquared() > Threshold && MParticles.PreV(ParticleIdx).SizeSquared() > Threshold)
				if (Particle->ToBeRemovedOnFracture() && 
					Particle->V().SizeSquared() > Threshold && 
					Particle->PreV().SizeSquared() > Threshold)
				{
					DisableParticleWithBreakEvent(Particle);
				}
			}
		}

		if(MChildren.Num())
		{
			//
			//  Grab collision impulses for processing
			//
			if (ComputeClusterCollisionStrains)
			{
				ComputeStrainFromCollision(CollisionRule);
			}
			else
			{
				ResetCollisionImpulseArray();
			}

			//
			//  Monitor the MStrain array for 0 or less values.
			//  That will trigger a break too.
			//
			{
				SCOPE_CYCLE_COUNTER(STAT_UpdateDirtyImpulses);
				const auto& ActiveClusteredArray = MEvolution.GetActiveClusteredArray();
				for (const auto& ActiveCluster : ActiveClusteredArray)
				{
					if (ActiveCluster->ClusterIds().NumChildren > 0) //active index is a cluster
					{
						TArray<TPBDRigidParticleHandle<T, d>*>& ParentToChildren = MChildren[ActiveCluster];
						for (TPBDRigidParticleHandle<T, d>* Child : ParentToChildren)
						{
							if (TPBDRigidClusteredParticleHandle<T, d>* ClusteredChild = Child->CastToClustered())
							{
								if (ClusteredChild->Strain() <= 0.f)
								{
									ClusteredChild->CollisionImpulse() = FLT_MAX;
									MCollisionImpulseArrayDirty = true;
								}
							}
						}
					}
				}
			}

			if (MCollisionImpulseArrayDirty)
			{

				SCOPE_CYCLE_COUNTER(STAT_UpdateDirtyImpulses);
				TMap<TPBDRigidClusteredParticleHandle<T, d>*, TSet<TPBDRigidParticleHandle<T, d>*>> ClusterToActivatedChildren = 
					BreakingModel();

				TSet<TPBDRigidParticleHandle<T, d>*> AllActivatedChildren;
				TSet<int32> IslandsToRecollide;
				for (auto Itr : ClusterToActivatedChildren)
				{
					//question: do we need to iterate all the children? Seems like island is known from cluster, but don't want to break anything at this point
					TSet<TPBDRigidParticleHandle<T, d>*>& ActivatedChildren = Itr.Value;
					for (TPBDRigidParticleHandle<T, d>* ActiveChild : ActivatedChildren)
					{
						if (ensure(!ActiveChild->Disabled()))
						{
							int32 Island = ActiveChild->Island();
							if (!IslandsToRecollide.Contains(Island) && Island != INDEX_NONE) // todo ask mike
							{
								IslandsToRecollide.Add(Island);
							}
						}
					}
					AllActivatedChildren.Append(ActivatedChildren);
				}

				const bool bRewindOnDecluster = ChildrenInheritVelocity < 1.f;
				if (bRewindOnDecluster && AllActivatedChildren.Num())
				{
					SCOPE_CYCLE_COUNTER(STAT_ClusterRewind);

					if (MEvolution.NumIslands())
					{
						RewindAndEvolve(MEvolution, MParticles, IslandsToRecollide, AllActivatedChildren, Dt, CollisionRule);
					}

					if (ChildrenInheritVelocity > 0.f)
					{
						for (auto Itr : ClusterToActivatedChildren)
						{
							TPBDRigidClusteredParticleHandle<T, d>* ClusteredParticle = Itr.Key;
							TSet<TPBDRigidParticleHandle<T, d>*>& ActivatedChildren = Itr.Value;
							for (TPBDRigidParticleHandle<T, d>* ActiveChild : ActivatedChildren)
							{
								ActiveChild->SetV(
									ActiveChild->V() * (1.f - ChildrenInheritVelocity) + ClusteredParticle->V() * ChildrenInheritVelocity);
								ActiveChild->SetW(
									ActiveChild->W() * (1.f - ChildrenInheritVelocity) + ClusteredParticle->W() * ChildrenInheritVelocity);
							}
						}
					}
				}
			} // end if MCollisionImpulseArrayDirty
		} // end if MParticles.Size()
		Timer.Stop();
		UE_LOG(LogChaos, Verbose, TEXT("Cluster Break Update Time is %f"), Time);
	}

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::BreakingModel()"), STAT_BreakingModel, STATGROUP_Chaos);
	template<class T_FPBDRigidsEvolution, class T_FPBDCollisionConstraint, class T, int d>
	TMap<TPBDRigidClusteredParticleHandle<T, d>*, TSet<TPBDRigidParticleHandle<T, d>*>> 
	TPBDRigidClustering<T_FPBDRigidsEvolution, T_FPBDCollisionConstraint, T, d>::BreakingModel(
		TMap<TGeometryParticleHandle<T, d>*, float>* ExternalStrainMap)
	{
		SCOPE_CYCLE_COUNTER(STAT_BreakingModel);

		TMap<TPBDRigidClusteredParticleHandle<T,d>*, TSet<TPBDRigidParticleHandle<T, d>*>> AllActivatedChildren;

		auto NonDisabledClusteredParticles = MEvolution.GetNonDisabledClusteredArray(); //make copy because release cluster modifies active indices. We want to iterate over original active indices
		for (auto ClusteredParticle : NonDisabledClusteredParticles)
		{
			if (ClusteredParticle->ClusterIds().NumChildren)
			{
				AllActivatedChildren.Add(
					ClusteredParticle, 
					ReleaseClusterParticles(ClusteredParticle, ExternalStrainMap));
			}
			else
			{
				// there's no children to break but we need to process whether this single piece is to be removed when damaged
				if (ClusteredParticle->ToBeRemovedOnFracture())
				{
					if (ClusteredParticle->CollisionImpulses() >= ClusteredParticle->Strains())
					{
						DisableCluster(ClusteredParticle);
						if (DoGenerateBreakingData)
						{
							int32 NewIdx = MAllClusterBreakings.Add(TBreakingData<float, 3>());
							TBreakingData<float, 3>& ClusterBreak = MAllClusterBreakings[NewIdx];
							ClusterBreak.Particle = ClusteredParticle;
							ClusterBreak.ParticleProxy = nullptr;
							ClusterBreak.Location = ClusteredParticle->X();
							ClusterBreak.Velocity = ClusteredParticle->V();
							ClusterBreak.AngularVelocity = ClusteredParticle->W();
							ClusterBreak.Mass = ClusteredParticle->M();
						}
					}
				}
			}
		}

		return AllActivatedChildren;
	}

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::PromoteStrains()"), STAT_PromoteStrains, STATGROUP_Chaos);
	template<class T_FPBDRigidsEvolution, class T_FPBDCollisionConstraint, class T, int d>
	T TPBDRigidClustering<T_FPBDRigidsEvolution, T_FPBDCollisionConstraint, T, d>::PromoteStrains(
		TPBDRigidParticleHandle<T, d>* CurrentNode)
	{
		SCOPE_CYCLE_COUNTER(STAT_PromoteStrains);
		if (TPBDRigidClusteredParticleHandle<T, d>* ClusteredCurrentNode = CurrentNode->CastToClustered())
		{
			T ChildrenStrains = 0;
			if (MChildren.Contains(CurrentNode))
			{
				for (TPBDRigidParticleHandle<T, d>* Child : MChildren[CurrentNode])
				{
					ChildrenStrains += PromoteStrains(Child);
				}
			}
			else
			{
				return ClusteredCurrentNode->Strains();
			}
			ClusteredCurrentNode->SetStrains(ClusteredCurrentNode->Strains() + ChildrenStrains);
			return ClusteredCurrentNode->Strains();
		}
		return (T)0.0;
	}

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::UpdateKinematicProperties()"), STAT_UpdateKinematicProperties, STATGROUP_Chaos);
	template<class T_FPBDRigidsEvolution, class T_FPBDCollisionConstraint, class T, int d>
	void TPBDRigidClustering<T_FPBDRigidsEvolution, T_FPBDCollisionConstraint, T, d>::UpdateKinematicProperties(
		Chaos::TPBDRigidParticleHandle<float, 3>* Parent)
	{
		SCOPE_CYCLE_COUNTER(STAT_UpdateKinematicProperties);

		EObjectStateType ObjectState = EObjectStateType::Dynamic;
		check(Parent != nullptr);
		if (MChildren.Contains(Parent) && MChildren[Parent].Num())
		{
			// TQueue is a linked list, which has no preallocator.
			TQueue<Chaos::TPBDRigidParticleHandle<float, 3>*> Queue;
			for (Chaos::TPBDRigidParticleHandle<float, 3>* Child : MChildren[Parent])
			{
				Queue.Enqueue(Child);
			}

			Chaos::TPBDRigidParticleHandle<float, 3>* CurrentHandle;
			while (Queue.Dequeue(CurrentHandle) && ObjectState == EObjectStateType::Dynamic)
			{
				// @question : Maybe we should just store the leaf node bodies in a
				// map, that will require Memory(n*log(n))
				if (MChildren.Contains(CurrentHandle))
				{
					for (Chaos::TPBDRigidParticleHandle<float, 3>* Child : MChildren[CurrentHandle])
					{
						Queue.Enqueue(Child);
					}
				}

				const EObjectStateType CurrState = CurrentHandle->ObjectState();
				if (CurrState == EObjectStateType::Kinematic)
				{
					ObjectState = EObjectStateType::Kinematic;
				}
				else if (CurrState == EObjectStateType::Static)
				{
					ObjectState = EObjectStateType::Static;
				}
			}

			Parent->SetObjectState(ObjectState);
		}
	}

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::SwapBufferedData"), STAT_SwapBufferedData, STATGROUP_Chaos);
	template<class T_FPBDRigidsEvolution, class T_FPBDCollisionConstraint, class T, int d>
	void TPBDRigidClustering<T_FPBDRigidsEvolution, T_FPBDCollisionConstraint, T, d>::SwapBufferedData()
	{
		check(false);
		// TODO: Ryan - this code currently uses MParticles as the only source of clustered particles.
		// As things stand, clustered particles and geometry collection particles are both of clustered
		// type, but are stored separately.  Geometry collection particles exist on both game and physics
		// threads, cluster particles only exist on the physics thread.
/*
		SCOPE_CYCLE_COUNTER(STAT_SwapBufferedData);
		ResourceLock.WriteLock();
		//BufferResource.MChildren.Reset();
		//BufferResource.ClusterParentTransforms.Reset();	//todo: once everything is atomic this should get reset
		const TArray<TSerializablePtr<FImplicitObject>>& AllGeom = MParticles.GetAllGeometry();
		BufferResource.GeometryPtrs.SetNum(AllGeom.Num());

		const auto& NonDisabledClusteredParticles = MEvolution.GetNonDisabledClusteredArray();
		for (auto& ClusteredParticle : NonDisabledClusteredParticles)
		{
			//const TArray<uint32>* Children = MChildren.Find(ClusteredParticle);
			TArray<TPBDRigidParticleHandle<T, d>*>* Children = MChildren.Find(ClusteredParticle);
			//if (Children && ClusteredParticle->ClusterIds().Id == INDEX_NONE) //root cluster so copy children
			if (Children && ClusteredParticle->ClusterIds().Id == nullptr) //root cluster so copy children
			{
				//TODO: record GT particle pointer instead
				//BufferResource.MChildren.Add(ClusteredParticle->TransientParticleIndex(), *Children);
				BufferResource.MChildren.Add(
					ClusteredParticle, *Children);
				//BufferResource.ClusterParentTransforms.Add(ClusteredParticle->TransientParticleIndex(), TRigidTransform<float, 3>(ClusteredParticle->X(), ClusteredParticle->R()));
				BufferResource.ClusterParentTransforms.Add(
					ClusteredParticle, TRigidTransform<float, 3>(ClusteredParticle->X(), ClusteredParticle->R()));
			}
		}

		BufferResource.GeometryPtrs = AllGeom; //in future this should be sparse. SQ has fallback that relies on potentially all geom so can't do it yet
		ResourceLock.WriteUnlock();
*/
	}

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::GetActiveClusterIndex"), STAT_GetActiveClusterIndex, STATGROUP_Chaos);
	template<class T_FPBDRigidsEvolution, class T_FPBDCollisionConstraint, class T, int d>
	TPBDRigidParticleHandle<T, d>* TPBDRigidClustering<T_FPBDRigidsEvolution, T_FPBDCollisionConstraint, T, d>::GetActiveClusterIndex(
		TPBDRigidParticleHandle<T, d>* Child)
	{
		SCOPE_CYCLE_COUNTER(STAT_GetActiveClusterIndex);
		while (Child && Child->Disabled())
		{
			Child = Child->CastToClustered()->ClusterIds().Id;
		}
		return Child; 
	}

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::GenerateConnectionGraph"), STAT_GenerateConnectionGraph, STATGROUP_Chaos);
	template<class T_FPBDRigidsEvolution, class T_FPBDCollisionConstraint, class T, int d>
	void TPBDRigidClustering<T_FPBDRigidsEvolution, T_FPBDCollisionConstraint, T, d>::GenerateConnectionGraph(
		Chaos::TPBDRigidClusteredParticleHandle<float, 3>* Parent,
		const FClusterCreationParameters<T>& Parameters)
	{
		SCOPE_CYCLE_COUNTER(STAT_GenerateConnectionGraph);
		if (!MChildren.Contains(Parent)) 
			return;

		// Connectivity Graph
		//    Build a connectivity graph for the cluster. If the PointImplicit is specified
		//    and the ClusterIndex has collision particles then use the expensive connection
		//    method. Otherwise try the DelaunayTriangulation if not none.
		//
		if (Parameters.bGenerateConnectionGraph)
		{
			typename FClusterCreationParameters<T>::EConnectionMethod LocalConnectionMethod = Parameters.ConnectionMethod;

			if (LocalConnectionMethod == FClusterCreationParameters<T>::EConnectionMethod::None ||
				(LocalConnectionMethod == FClusterCreationParameters<T>::EConnectionMethod::PointImplicit && 
				 !Parent->CollisionParticles()))
			{
				LocalConnectionMethod = FClusterCreationParameters<T>::EConnectionMethod::MinimalSpanningSubsetDelaunayTriangulation; // default method
			}

			if (LocalConnectionMethod == FClusterCreationParameters<T>::EConnectionMethod::PointImplicit ||
				LocalConnectionMethod == FClusterCreationParameters<T>::EConnectionMethod::PointImplicitAugmentedWithMinimalDelaunay)
			{
				UpdateConnectivityGraphUsingPointImplicit(Parent, Parameters);
			}

			if (LocalConnectionMethod == FClusterCreationParameters<T>::EConnectionMethod::DelaunayTriangulation)
			{
				UpdateConnectivityGraphUsingDelaunayTriangulation(Parent, Parameters); // not thread safe
			}

			if (LocalConnectionMethod == FClusterCreationParameters<T>::EConnectionMethod::PointImplicitAugmentedWithMinimalDelaunay ||
				LocalConnectionMethod == FClusterCreationParameters<T>::EConnectionMethod::MinimalSpanningSubsetDelaunayTriangulation)
			{
				FixConnectivityGraphUsingDelaunayTriangulation(Parent, Parameters);
			}
		}
	}

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::UpdateMassProperties"), STAT_UpdateMassProperties, STATGROUP_Chaos);
	template<class T_FPBDRigidsEvolution, class T_FPBDCollisionConstraint, class T, int d>
	void TPBDRigidClustering<T_FPBDRigidsEvolution, T_FPBDCollisionConstraint, T, d>::UpdateMassProperties(
		Chaos::TPBDRigidClusteredParticleHandle<float, 3>* Parent, 
		TSet<TPBDRigidParticleHandle<T, d>*>& Children, 
		const TRigidTransform<T, d>* ForceMassOrientation)
	{
		SCOPE_CYCLE_COUNTER(STAT_UpdateMassProperties);
		UpdateClusterMassProperties(
			Parent,
			Children,
			ForceMassOrientation);
		UpdateKinematicProperties(Parent);
	}

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::UpdateGeometry"), STAT_UpdateGeometry, STATGROUP_Chaos);
	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::UpdateGeometry_GatherObjects"), STAT_UpdateGeometry_GatherObjects, STATGROUP_Chaos);
	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::UpdateGeometry_GatherPoints"), STAT_UpdateGeometry_GatherPoints, STATGROUP_Chaos);
	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::UpdateGeometry_CopyPoints"), STAT_UpdateGeometry_CopyPoints, STATGROUP_Chaos);
	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::UpdateGeometry_PointsBVH"), STAT_UpdateGeometry_PointsBVH, STATGROUP_Chaos);

	template<class T_FPBDRigidsEvolution, class T_FPBDCollisionConstraint, class T, int d>
	void TPBDRigidClustering<T_FPBDRigidsEvolution, T_FPBDCollisionConstraint, T, d>::UpdateGeometry(
		Chaos::TPBDRigidClusteredParticleHandle<float, 3>* Parent, 
		const TSet<TPBDRigidParticleHandle<T, d>*>& Children, 
		TSharedPtr<Chaos::FImplicitObject, ESPMode::ThreadSafe> ProxyGeometry,
		const FClusterCreationParameters<T>& Parameters)
	{
		SCOPE_CYCLE_COUNTER(STAT_UpdateGeometry);

		TArray<TUniquePtr<FImplicitObject>> Objects;
		TArray<TUniquePtr<FImplicitObject>> Objects2; //todo: find a better way to reuse this
		Objects.Reserve(Children.Num());
		Objects2.Reserve(Children.Num());

		//we should never update existing geometry since this is used by SQ threads.
		ensure(!Parent->Geometry());
		ensure(!Parent->DynamicGeometry());

		const TRigidTransform<T, d> ClusterWorldTM(Parent->X(), Parent->R());

		TArray<TVector<T, d>> OriginalPoints;
		TArray<TPBDRigidParticleHandle<T, d>*> GeomToOriginalParticlesHack;
		GeomToOriginalParticlesHack.Reserve(Children.Num());

		const bool bUseCollisionPoints = (ProxyGeometry || Parameters.bCopyCollisionParticles) && !Parameters.CollisionParticles;
		bool bUseParticleImplicit = false;
		bool bUsingMultiChildProxy = false;

		{ // STAT_UpdateGeometry_GatherObjects
			SCOPE_CYCLE_COUNTER(STAT_UpdateGeometry_GatherObjects);

			if (bUseCollisionPoints)
			{
				uint32 NumPoints = 0;
				for (TPBDRigidParticleHandle<T, d>* Child : Children)
				{
					NumPoints += Child->CollisionParticlesSize();
				}
				OriginalPoints.Reserve(NumPoints);
			}

			for (TPBDRigidParticleHandle<T, d>* Child : Children)
			{
				const TRigidTransform<T, d> ChildWorldTM(Child->X(), Child->R());
				TRigidTransform<T, d> Frame = ChildWorldTM.GetRelativeTransform(ClusterWorldTM);
				TPBDRigidParticleHandle<T, d>* UsedGeomChild = Child;
				if (Child->Geometry())
				{
					TPBDRigidClusteredParticleHandle<T, d>* ClusteredChild = Child->CastToClustered();

					const FMultiChildProxyId MultiChildProxyId = 
						ClusteredChild ? ClusteredChild->MultiChildProxyId() : FMultiChildProxyId();
					const TUniquePtr<TMultiChildProxyData<T, d>>& MultiChildProxyData = ClusteredChild->MultiChildProxyData();

					if (UseLevelsetCollision || MultiChildProxyId.Id == nullptr || !MultiChildProxyData)
					{
						Objects.Add(TUniquePtr<FImplicitObject>(new TImplicitObjectTransformed<T, d>(Child->Geometry(), Frame)));
						Objects2.Add(TUniquePtr<FImplicitObject>(new TImplicitObjectTransformed<T, d>(Child->Geometry(), Frame)));
						GeomToOriginalParticlesHack.Add(Child);
					}
					else if (MultiChildProxyData->KeyChild == Child)
					{
						//using multi child proxy and this child is the key
						const TRigidTransform<T, d> ProxyWorldTM = MultiChildProxyData->RelativeToKeyChild * ChildWorldTM;
						const TRigidTransform<T, d> ProxyRelativeTM = ProxyWorldTM.GetRelativeTransform(ClusterWorldTM);
						Objects.Add(TUniquePtr<FImplicitObject>(new TImplicitObjectTransformed<T, d>(MultiChildProxyId.Id->Geometry(), ProxyRelativeTM)));
						Objects2.Add(TUniquePtr<FImplicitObject>(new TImplicitObjectTransformed<T, d>(MultiChildProxyId.Id->Geometry(), ProxyRelativeTM)));
						UsedGeomChild = MultiChildProxyId.Id;
						GeomToOriginalParticlesHack.Add(UsedGeomChild);
						bUsingMultiChildProxy = true;
					}
				}

				ensure(Child->Disabled() == true);
				check(Child->CastToClustered()->ClusterIds().Id == Parent);

				Child->CastToClustered()->SetChildToParent(Frame);

				if (bUseCollisionPoints)
				{
					SCOPE_CYCLE_COUNTER(STAT_UpdateGeometry_GatherPoints);
					if (const TUniquePtr<TBVHParticles<T,d>>& CollisionParticles = Child->CollisionParticles())
					{
						for (uint32 i = 0; i < CollisionParticles->Size(); ++i)
						{
							OriginalPoints.Add(Frame.TransformPosition(CollisionParticles->X(i)));
						}
					}
				}
				if (Child->Geometry() && Child->Geometry()->GetType() == ImplicitObjectType::Unknown)
				{
					bUseParticleImplicit = true;
				}
			} // end for
		} // STAT_UpdateGeometry_GatherObjects

		{
			QUICK_SCOPE_CYCLE_COUNTER(SpatialBVH);
			TUniquePtr<FImplicitObjectUnionClustered>& ChildrenSpatial = Parent->ChildrenSpatial();
			ChildrenSpatial.Reset(
				Objects2.Num() ? 
				new Chaos::FImplicitObjectUnionClustered(MoveTemp(Objects2), GeomToOriginalParticlesHack) : 
				nullptr);
		}

		TArray<TVector<T, d>> CleanedPoints;
		if (!Parameters.CollisionParticles)
		{
			CleanedPoints = 
				Parameters.bCleanCollisionParticles ? 
				CleanCollisionParticles(OriginalPoints, ClusterSnapDistance) : 
				OriginalPoints;
		}

		if (ProxyGeometry)
		{
			//ensureMsgf(false, TEXT("Checking usage with proxy"));
			//@coverage {production}
			Parent->SetSharedGeometry(TSharedPtr<FImplicitObject, ESPMode::ThreadSafe>(ProxyGeometry->DeepCopy().Release()));
		}
		else if (Objects.Num() == 0)
		{
			//ensureMsgf(false, TEXT("Checking usage with no proxy and no objects"));
			//@coverage : {production}
			Parent->SetGeometry(Chaos::TSerializablePtr<Chaos::FImplicitObject>());
		}
		else
		{
			if (UseLevelsetCollision)
			{
				ensureMsgf(false, TEXT("Checking usage with no proxy and multiple ojects with levelsets"));

				FImplicitObjectUnion UnionObject(MoveTemp(Objects));
				TAABB<T, d> Bounds = UnionObject.BoundingBox();
				const TVector<T, d> BoundsExtents = Bounds.Extents();
				if (BoundsExtents.Min() >= MinLevelsetSize) //make sure the object is not too small
				{
					TVector<int32, d> NumCells = Bounds.Extents() / MinLevelsetSize;
					for (int i = 0; i < d; ++i)
					{
						NumCells[i] = FMath::Clamp(NumCells[i], MinLevelsetDimension, MaxLevelsetDimension);
					}

					FErrorReporter ErrorReporter;
					TUniformGrid<T, 3> Grid(Bounds.Min(), Bounds.Max(), NumCells, LevelsetGhostCells);
					TUniquePtr<TLevelSet<T, 3>> LevelSet(new TLevelSet<T, 3>(ErrorReporter, Grid, UnionObject));

					if (!Parameters.CollisionParticles)
					{
						const T MinDepthToSurface = Grid.Dx().Max();
						for (int32 Idx = CleanedPoints.Num() - 1; Idx >= 0; --Idx)
						{
							if (CleanedPoints.Num() > MinCleanedPointsBeforeRemovingInternals) //todo(ocohen): this whole thing should really be refactored
							{
								const TVector<T, d>& CleanedCollision = CleanedPoints[Idx];
								if (LevelSet->SignedDistance(CleanedCollision) < -MinDepthToSurface)
								{
									CleanedPoints.RemoveAtSwap(Idx);
								}
							}
						}
					}
					Parent->SetDynamicGeometry(MoveTemp(LevelSet));
				}
				else
				{
					Parent->SetDynamicGeometry(
						MakeUnique<TSphere<T, d>>(TVector<T, d>(0), BoundsExtents.Size() * 0.5f));
				}
			}
			else // !UseLevelsetCollision
			{
				if (!bUsingMultiChildProxy && Objects.Num() == 1)
				{
					// @coverage:{confidence tests}
					//ensureMsgf(false, TEXT("Checking no proxy, not level set, a single object"));
					Parent->SetDynamicGeometry(MoveTemp(Objects[0]));
				}
				else
				{
					QUICK_SCOPE_CYCLE_COUNTER(UnionBVH);
					// @coverage : { confidence tests}
					//ensureMsgf(false, TEXT("Checking no proxy, not levelset, and multiple objects"));
					Parent->SetDynamicGeometry(
						MakeUnique<FImplicitObjectUnionClustered>(
							MoveTemp(Objects), GeomToOriginalParticlesHack));
				}
			}
		}

		//if children are ignore analytic and this is a dynamic geom, mark it too. todo(ocohen): clean this up
		if (bUseParticleImplicit && Parent->DynamicGeometry()) 
		{
			Parent->DynamicGeometry()->SetDoCollide(false);
		}

		if (Parameters.CollisionParticles)
		{
			SCOPE_CYCLE_COUNTER(STAT_UpdateGeometry_CopyPoints);
			Parent->CollisionParticles().Reset(Parameters.CollisionParticles);
		}
		else
		{
			{
				SCOPE_CYCLE_COUNTER(STAT_UpdateGeometry_GatherPoints);
				Parent->CollisionParticlesInitIfNeeded();
				TUniquePtr<TBVHParticles<T, d>>& CollisionParticles = Parent->CollisionParticles();
				CollisionParticles->AddParticles(CleanedPoints.Num());
				for (int32 i = 0; i < CleanedPoints.Num(); ++i)
				{
					CollisionParticles->X(i) = CleanedPoints[i];
				}
			}

			if (bUseCollisionPoints)
			{
				SCOPE_CYCLE_COUNTER(STAT_UpdateGeometry_PointsBVH);
				Parent->CollisionParticles()->UpdateAccelerationStructures();
			}
		}
	}

	float MinImpulseForStrainEval = 980 * 2 * 1.f / 30.f; //ignore impulses caused by just keeping object on ground. This is a total hack, we should not use accumulated impulse directly. Instead we need to look at delta v along constraint normal
	FAutoConsoleVariableRef CVarMinImpulseForStrainEval(TEXT("p.chaos.MinImpulseForStrainEval"), MinImpulseForStrainEval, TEXT("Minimum accumulated impulse before accumulating for strain eval "));

	DECLARE_CYCLE_STAT(TEXT("ComputeStrainFromCollision"), STAT_ComputeStrainFromCollision, STATGROUP_Chaos);
	template<class T_FPBDRigidsEvolution, class T_FPBDCollisionConstraint, class T, int d>
	void TPBDRigidClustering<T_FPBDRigidsEvolution, T_FPBDCollisionConstraint, T, d>::ComputeStrainFromCollision(
		const T_FPBDCollisionConstraint& CollisionRule)
	{
		SCOPE_CYCLE_COUNTER(STAT_ComputeStrainFromCollision);
		FClusterMap& MParentToChildren = GetChildrenMap();

		ResetCollisionImpulseArray();

		for (const Chaos::FPBDCollisionConstraintHandle* ContactHandle : CollisionRule.GetConstConstraintHandles())
		{
			if (ContactHandle->GetAccumulatedImpulse().Size() < MinImpulseForStrainEval)
			{
				continue;
			}

			auto ComputeStrainLambda = [&](
				const TPBDRigidClusteredParticleHandle<T, d>* Cluster, 
				const TArray<TPBDRigidParticleHandle<T, d>*>& ParentToChildren)
			{
				const TRigidTransform<T, d> WorldToClusterTM = TRigidTransform<T, d>(Cluster->P(), Cluster->Q());
				const TVector<T, d> ContactLocationClusterLocal = WorldToClusterTM.InverseTransformPosition(ContactHandle->GetContactLocation());
				TAABB<T, d> ContactBox(ContactLocationClusterLocal, ContactLocationClusterLocal);
				ContactBox.Thicken(ClusterDistanceThreshold);
				if (Cluster->ChildrenSpatial())
				{
					const TArray<TPBDRigidParticleHandle<FReal, 3>*> Intersections = Cluster->ChildrenSpatial()->FindAllIntersectingChildren(ContactBox);
					for (TPBDRigidParticleHandle<FReal, 3>* Child : Intersections)
					{
						if (TPBDRigidClusteredParticleHandle<FReal, 3>* ClusteredChild = Child->CastToClustered())
						{
							const TUniquePtr<TMultiChildProxyData<T, d>>& ProxyData = ClusteredChild->MultiChildProxyData();
							const TPBDRigidParticleHandle<T, d>* KeyChild = ProxyData ? ProxyData->KeyChild : nullptr;
							const TPBDRigidClusteredParticleHandle<T, d>* ClusteredKeyChild = KeyChild ? KeyChild->CastToClustered() : nullptr;
							if (ClusteredKeyChild)
							{
								//multi child so get its children
								const TRigidTransform<T, d> ProxyToCluster = ProxyData->RelativeToKeyChild * ClusteredKeyChild->ChildToParent();
								const TVector<T, d> ContactLocationProxyLocal = ProxyToCluster.InverseTransformPosition(ContactLocationClusterLocal);
								TAABB<T, d> ContactBoxProxy(ContactLocationProxyLocal, ContactLocationProxyLocal);
								ContactBoxProxy.Thicken(ClusterDistanceThreshold);
								if (ClusteredChild->ChildrenSpatial())
								{
									const TArray<TPBDRigidParticleHandle<FReal, 3>*> SubIntersections = 
										ClusteredChild->ChildrenSpatial()->FindAllIntersectingChildren(ContactBoxProxy);
									for (TPBDRigidParticleHandle<FReal, 3>* SubChild : SubIntersections)
									{
										if (TPBDRigidClusteredParticleHandle<FReal, 3>* ClusteredSubChild = SubChild->CastToClustered())
										{
											ClusteredSubChild->CollisionImpulses() += ContactHandle->GetAccumulatedImpulse().Size();
										}
									}
								}
							}
							else
							{
								ClusteredChild->CollisionImpulses() += ContactHandle->GetAccumulatedImpulse().Size();
							}
						}
					}
				}
			};

			TVector<const TGeometryParticleHandle<T, d>*, 2> ConstrainedParticles = ContactHandle->GetConstrainedParticles();
			if (const TArray<TPBDRigidParticleHandle<T, d>*>* ChildrenPtr = MParentToChildren.Find(ConstrainedParticles[0]->CastToRigidParticle()))
			{
				ComputeStrainLambda(ConstrainedParticles[0]->CastToClustered(), *ChildrenPtr);
			}

			if (const TArray<TPBDRigidParticleHandle<T, d>*>* ChildrenPtr = MParentToChildren.Find(ConstrainedParticles[1]->CastToRigidParticle()))
			{
				ComputeStrainLambda(ConstrainedParticles[1]->CastToClustered(), *ChildrenPtr);
			}

			MCollisionImpulseArrayDirty = true;
		}
	}

	DECLARE_CYCLE_STAT(TEXT("ResetCollisionImpulseArray"), STAT_ResetCollisionImpulseArray, STATGROUP_Chaos);
	template<class T_FPBDRigidsEvolution, class T_FPBDCollisionConstraint, class T, int d>
	void TPBDRigidClustering<T_FPBDRigidsEvolution, T_FPBDCollisionConstraint, T, d>::ResetCollisionImpulseArray()
	{
		SCOPE_CYCLE_COUNTER(STAT_ResetCollisionImpulseArray);
		if (MCollisionImpulseArrayDirty)
		{
			TPBDRigidsSOAs<T, d>& ParticleStructures = MEvolution.GetParticles();
			ParticleStructures.GetGeometryCollectionParticles().CollisionImpulsesArray().Fill(0.0f);
			ParticleStructures.GetClusteredParticles().CollisionImpulsesArray().Fill(0.0f);
			MCollisionImpulseArrayDirty = false;
		}
	}

	template<class T_FPBDRigidsEvolution, class T_FPBDCollisionConstraint, class T, int d>
	void TPBDRigidClustering<T_FPBDRigidsEvolution, T_FPBDCollisionConstraint, T, d>::DisableCluster(
		TPBDRigidClusteredParticleHandle<T,d>* ClusteredParticle)
	{
		// #note: we don't recursively descend to the children
		MEvolution.DisableParticle(ClusteredParticle);

		if (MoveClustersWhenDeactivated)
		{
			ClusteredParticle->P() -= FVector(0.f, 0.f, -10000.f); // HACK : Move them away to prevent reactivation. 
			ClusteredParticle->X() -= FVector(0.f, 0.f, -10000.f); // HACK : Move them away to prevent reactivation. 
			ClusteredParticle->V() = FVector(0.f);            // HACK : Move them away to prevent reactivation.
		}

		TopLevelClusterParents.Remove(ClusteredParticle);
		GetChildrenMap().Remove(ClusteredParticle);
		ClusteredParticle->ClusterIds() = ClusterId();
		ClusteredParticle->ClusterGroupIndex() = 0;
		MActiveRemovalIndices.Remove(ClusteredParticle);
	}

	template<class T_FPBDRigidsEvolution, class T_FPBDCollisionConstraint, class T, int d>
	void TPBDRigidClustering<T_FPBDRigidsEvolution, T_FPBDCollisionConstraint, T, d>::DisableParticleWithBreakEvent(
		Chaos::TPBDRigidParticleHandle<float, 3>* Particle)
	{
		DisableCluster(Particle->CastToClustered());

		if (DoGenerateBreakingData)
		{
			const int32 NewIdx = MAllClusterBreakings.Add(TBreakingData<float, 3>());
			TBreakingData<float, 3>& ClusterBreak = MAllClusterBreakings[NewIdx];
			ClusterBreak.Particle = Particle;
			ClusterBreak.ParticleProxy = nullptr;
			ClusterBreak.Location = Particle->X();
			ClusterBreak.Velocity = Particle->V();
			ClusterBreak.AngularVelocity = Particle->W();
			ClusterBreak.Mass = Particle->M();
		}
	}

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::UpdateConnectivityGraphUsingPointImplicit"), STAT_UpdateConnectivityGraphUsingPointImplicit, STATGROUP_Chaos);
	template<class T_FPBDRigidsEvolution, class T_FPBDCollisionConstraint, class T, int d>
	void TPBDRigidClustering<T_FPBDRigidsEvolution, T_FPBDCollisionConstraint, T, d>::UpdateConnectivityGraphUsingPointImplicit(
		Chaos::TPBDRigidClusteredParticleHandle<float, 3>* Parent,
		const FClusterCreationParameters<T>& Parameters)
	{
		SCOPE_CYCLE_COUNTER(STAT_UpdateConnectivityGraphUsingPointImplicit);

		if (!UseConnectivity)
		{
			return;
		}

		const T Delta = FMath::Min(FMath::Max(Parameters.CoillisionThicknessPercent, (T)0), T(1));
		const TArray<TPBDRigidParticleHandle<T, d>*>& Children = MChildren[Parent];
		for (int32 i = 0; i < Children.Num(); ++i)
		{
			TPBDRigidParticleHandle<T, d>* Child1 = Children[i];
			if (!Child1->Geometry() || !Child1->Geometry()->HasBoundingBox())
			{
				continue;
			}
			const TVector<T,d>& Child1X = Child1->X();
			if (!(ensure(!FMath::IsNaN(Child1X[0])) && ensure(!FMath::IsNaN(Child1X[1])) && ensure(!FMath::IsNaN(Child1X[2]))))
			{
				continue;
			}
			TRigidTransform<T, d> TM1 = TRigidTransform<T, d>(Child1X, Child1->R());
			TBox<T, d> Box1 = Child1->Geometry()->BoundingBox();

			const int32 Offset = i + 1;
			const int32 NumRemainingChildren = Children.Num() - Offset;
			typedef TPair<TPBDRigidParticleHandle<T, d>*, TPBDRigidParticleHandle<T, d>*> ParticlePair;
			typedef TArray<ParticlePair> ParticlePairArray;
			TArray<ParticlePairArray> Connections;
			Connections.Init(ParticlePairArray(), NumRemainingChildren);
			PhysicsParallelFor(NumRemainingChildren, [&](int32 Idx) 
			{
				const int32 ChildrenIdx = Offset + Idx;
				TPBDRigidParticleHandle<T, d>* Child2 = Children[ChildrenIdx];
				if(!Child2->CollisionParticles())
					return;

				const TVector<T, d>& Child2X = Child2->X();
				if (!(ensure(!FMath::IsNaN(Child2X[0])) && ensure(!FMath::IsNaN(Child2X[1])) && ensure(!FMath::IsNaN(Child2X[2]))))
					return;

				const TRigidTransform<T, d> TM = TM1.GetRelativeTransform(TRigidTransform<T, d>(Child2X, Child2->R()));

				bool bCollided = false;
				for (uint32 CollisionIdx = 0; !bCollided && CollisionIdx < Child2->CollisionParticles()->Size(); ++CollisionIdx)
				{
					const TVector<T, d> LocalPoint = 
						TM.TransformPositionNoScale(Child2->CollisionParticles()->X(CollisionIdx));
					const T Phi = Child1->Geometry()->SignedDistance(LocalPoint - (LocalPoint * Delta));
					if (Phi < 0.0)
						bCollided = true;
				}
				if (bCollided)
				{
					Connections[Idx].Add(ParticlePair(Child1, Child2));
				}
			});

			// join results and make connections
			for (const ParticlePairArray& ConnectionList : Connections)
			{
				for (const ParticlePair& Edge : ConnectionList)
				{
					ConnectNodes(Edge.Key, Edge.Value);
				}
			}
		}
	}

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::FixConnectivityGraphUsingDelaunayTriangulation"), STAT_FixConnectivityGraphUsingDelaunayTriangulation, STATGROUP_Chaos);
	template<class T_FPBDRigidsEvolution, class T_FPBDCollisionConstraint, class T, int d>
	void TPBDRigidClustering<T_FPBDRigidsEvolution, T_FPBDCollisionConstraint, T, d>::FixConnectivityGraphUsingDelaunayTriangulation(
		Chaos::TPBDRigidClusteredParticleHandle<float, 3>* Parent,
		const FClusterCreationParameters<T>& Parameters)
	{
		// @todo(investigate) : This is trying to set multiple connections and throwing a warning in ConnectNodes
		SCOPE_CYCLE_COUNTER(STAT_FixConnectivityGraphUsingDelaunayTriangulation);

		const TArray<TPBDRigidParticleHandle<T, d>*>& Children = MChildren[Parent];

		// Compute Delaunay neighbor graph on children centers
		TArray<FVector> Pts;
		Pts.AddUninitialized(Children.Num());
		for (int32 i = 0; i < Children.Num(); ++i)
		{
			Pts[i] = Children[i]->X();
		}
		TArray<TArray<int32>> Neighbors; // Indexes into Children
		VoronoiNeighbors(Pts, Neighbors);

		// Build a UnionFind graph to find (indirectly) connected children
		struct UnionFindInfo
		{
			TPBDRigidParticleHandle<T, d>* GroupId;
			int32 Size;
		};
		TMap<TPBDRigidParticleHandle<T, d>*, UnionFindInfo> UnionInfo;
		UnionInfo.Reserve(Children.Num());

		// Initialize UnionInfo:
		//		0: GroupId = Children[0], Size = 1
		//		1: GroupId = Children[1], Size = 1
		//		2: GroupId = Children[2], Size = 1
		//		3: GroupId = Children[3], Size = 1

		for(TPBDRigidParticleHandle<T, d>* Child : Children)
		{
			UnionInfo.Add(Child, { Child, 1 }); // GroupId, Size
		}

		auto FindGroup = [&](TPBDRigidParticleHandle<T, d>* Id) 
		{
			TPBDRigidParticleHandle<T, d>* GroupId = Id;
			if (GroupId)
			{
				int findIters = 0;
				while (UnionInfo[GroupId].GroupId != GroupId)
				{
					ensure(findIters++ < 10); // if this while loop iterates more than a few times, there's probably a bug in the unionfind
					auto& CurrInfo = UnionInfo[GroupId];
					auto& NextInfo = UnionInfo[CurrInfo.GroupId];
					CurrInfo.GroupId = NextInfo.GroupId;
					GroupId = NextInfo.GroupId;
					if (!GroupId) break; // error condidtion
				}
			}
			return GroupId;
		};

		// MergeGroup(Children[0], Children[1])
		//		0: GroupId = Children[1], Size = 0
		//		1: GroupId = Children[1], Size = 2
		//		2: GroupId = Children[2], Size = 1
		//		3: GroupId = Children[3], Size = 1

		auto MergeGroup = [&](TPBDRigidParticleHandle<T, d>* A, TPBDRigidParticleHandle<T, d>* B) 
		{
			TPBDRigidParticleHandle<T, d>* GroupA = FindGroup(A);
			TPBDRigidParticleHandle<T, d>* GroupB = FindGroup(B);
			if (GroupA == GroupB)
			{
				return;
			}
			// Make GroupA the smaller of the two
			if (UnionInfo[GroupA].Size > UnionInfo[GroupB].Size)
			{
				Swap(GroupA, GroupB);
			}
			// Overwrite GroupA with GroupB
			UnionInfo[GroupA].GroupId = GroupB;
			UnionInfo[GroupB].Size += UnionInfo[GroupA].Size;
			UnionInfo[GroupA].Size = 0; // not strictly necessary, but more correct
		};

		// Merge all groups with edges connecting them.
		for (int32 i = 0; i < Children.Num(); ++i)
		{
			TPBDRigidParticleHandle<T, d>* Child = Children[i];
			const TArray<TConnectivityEdge<T>>& Edges = Child->CastToClustered()->ConnectivityEdges();
			for (const TConnectivityEdge<T>& Edge : Edges)
			{
				if (UnionInfo.Contains(Edge.Sibling))
				{
					MergeGroup(Child, Edge.Sibling);
				}
			}
		}

		// Find candidate edges from the Delaunay graph to consider adding
		struct LinkCandidate
		{
			//int32 A, B;
			TPBDRigidParticleHandle<T, d>* A;
			TPBDRigidParticleHandle<T, d>* B;
			float DistSq;
		};
		TArray<LinkCandidate> Candidates;
		Candidates.Reserve(Neighbors.Num());

		const float AlwaysAcceptBelowDistSqThreshold = 50.f*50.f*100.f*MClusterConnectionFactor;
		for (int32 i = 0; i < Neighbors.Num(); i++)
		{
			TPBDRigidParticleHandle<T, d>* Child1 = Children[i];
			const TArray<int32>& Child1Neighbors = Neighbors[i];
			for (const int32 Nbr : Child1Neighbors)
			{
				if (Nbr < i)
				{ // assume we'll get the symmetric connection; don't bother considering this one
					continue;
				}
				TPBDRigidParticleHandle<T, d>* Child2 = Children[Nbr];

				const float DistSq = FVector::DistSquared(Pts[i], Pts[Nbr]);
				if (DistSq < AlwaysAcceptBelowDistSqThreshold)
				{ // below always-accept threshold: don't bother adding to candidates array, just merge now
					MergeGroup(Child1, Child2);
					ConnectNodes(Child1, Child2);
					continue;
				}

				if (FindGroup(Child1) == FindGroup(Child2))
				{ // already part of the same group so we don't need Delaunay edge  
					continue;
				}

				// add to array to sort and add as-needed
				Candidates.Add({ Child1, Child2, DistSq });
			}
		}

		// Only add edges that would connect disconnected components, considering shortest edges first
		Candidates.Sort([](const LinkCandidate& A, const LinkCandidate& B) { return A.DistSq < B.DistSq; });
		for (const LinkCandidate& Candidate : Candidates)
		{
			TPBDRigidParticleHandle<T, d>* Child1 = Candidate.A;
			TPBDRigidParticleHandle<T, d>* Child2 = Candidate.B;
			if (FindGroup(Child1) != FindGroup(Child2))
			{
				MergeGroup(Child1, Child2);
				ConnectNodes(Child1, Child2);
			}
		}
	}

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::UpdateConnectivityGraphUsingDelaunayTriangulation"), STAT_UpdateConnectivityGraphUsingDelaunayTriangulation, STATGROUP_Chaos);

	template<class T_FPBDRigidsEvolution, class T_FPBDCollisionConstraint, class T, int d>
	void TPBDRigidClustering<T_FPBDRigidsEvolution, T_FPBDCollisionConstraint, T, d>::UpdateConnectivityGraphUsingDelaunayTriangulation(
		Chaos::TPBDRigidClusteredParticleHandle<float, 3>* Parent,
		const FClusterCreationParameters<T>& Parameters)
	{
		SCOPE_CYCLE_COUNTER(STAT_UpdateConnectivityGraphUsingDelaunayTriangulation);

		const TArray<TPBDRigidParticleHandle<T, d>*>& Children = MChildren[Parent];

		TArray<FVector> Pts;
		Pts.AddUninitialized(Children.Num());
		for (int32 i = 0; i < Children.Num(); ++i)
		{
			Pts[i] = Children[i]->X();
		}
		TArray<TArray<int>> Neighbors;
		VoronoiNeighbors(Pts, Neighbors);

		TSet<TPair<TPBDRigidParticleHandle<T, d>*, TPBDRigidParticleHandle<T, d>*>> UniqueEdges;
		for (int32 i = 0; i < Neighbors.Num(); i++)
		{
			for (int32 j = 0; j < Neighbors[i].Num(); j++)
			{
				TPBDRigidParticleHandle<T, d>* Child1 = Children[i];
				TPBDRigidParticleHandle<T, d>* Child2 = Children[Neighbors[i][j]];
				const bool bFirstSmaller = Child1 < Child2;
				TPair<TPBDRigidParticleHandle<T, d>*, TPBDRigidParticleHandle<T, d>*> SortedPair(
					bFirstSmaller ? Child1 : Child2, 
					bFirstSmaller ? Child2 : Child1);
				if (!UniqueEdges.Find(SortedPair))
				{
					// this does not use ConnectNodes because Neighbors is bi-direction : as in (1,2),(2,1)
					ConnectNodes(Child1, Child2);
					UniqueEdges.Add(SortedPair);
				}
			}
		}
	}

	//template<class T_FPBDRigidsEvolution, class T_FPBDCollisionConstraint, class T, int d>
	//void TPBDRigidClustering<T_FPBDRigidsEvolution, T_FPBDCollisionConstraint, T, d>::AddUniqueConnection(uint32 Index1, uint32 Index2, T Strain)
	//{
	//	if (Index1 != Index2)
	//	{
	//		//todo(pref): This can be removed if we are sure there are no duplicate connections generated.
	//		for (int32 i = 0; i < MParticles.ConnectivityEdges(Index1).Num(); i++)
	//		{
	//			if (MParticles.ConnectivityEdges(Index1)[i].Sibling == Index2)
	//			{
	//				// @todo(duplication connection) : re-enable post GDC.  
	//				// FixConnectivityGraphUsingDelaunayTriangulation attempts to add multiple connections.
	//				// so commenting out this msg to remove the noise from the confidence test. 
	//				// ensureMsgf(false, TEXT("Duplicate graph connection."));
	//				return;
	//			}
	//		}
	//
	//		MParticles.ConnectivityEdges(Index1).Add({ Index2, Strain });
	//	}
	//}

	template<class T_FPBDRigidsEvolution, class T_FPBDCollisionConstraint, class T, int d>
	void TPBDRigidClustering<T_FPBDRigidsEvolution, T_FPBDCollisionConstraint, T, d>::ConnectNodes(
		TPBDRigidParticleHandle<T, d>* Child1,
		TPBDRigidParticleHandle<T, d>* Child2)
	{
		check(Child1 != Child2);
		TPBDRigidClusteredParticleHandle<T, d>* ClusteredChild1 = Child1->CastToClustered();
		TPBDRigidClusteredParticleHandle<T, d>* ClusteredChild2 = Child2->CastToClustered();
		ConnectNodes(ClusteredChild1, ClusteredChild2);
	}

	template<class T_FPBDRigidsEvolution, class T_FPBDCollisionConstraint, class T, int d>
	void TPBDRigidClustering<T_FPBDRigidsEvolution, T_FPBDCollisionConstraint, T, d>::ConnectNodes(
		TPBDRigidClusteredParticleHandle<T, d>* ClusteredChild1,
		TPBDRigidClusteredParticleHandle<T, d>* ClusteredChild2)
	{
		check(ClusteredChild1 && ClusteredChild2);
		if (ClusteredChild1 == ClusteredChild2)
			return;
		const T AvgStrain = (ClusteredChild1->Strains() + ClusteredChild2->Strains()) * (T)0.5;
		TArray<TConnectivityEdge<T>>& Edges1 = ClusteredChild1->ConnectivityEdges();
		TArray<TConnectivityEdge<T>>& Edges2 = ClusteredChild2->ConnectivityEdges();
		if (//Edges1.Num() < Parameters.MaxNumConnections && 
			!Edges1.FindByKey(ClusteredChild2))
		{
			Edges1.Add(TConnectivityEdge<T>(ClusteredChild2, AvgStrain));
		}
		if (//Edges2.Num() < Parameters.MaxNumConnections && 
			!Edges2.FindByKey(ClusteredChild1))
		{
			Edges2.Add(TConnectivityEdge<T>(ClusteredChild1, AvgStrain));
		}
	}

	template<class T_FPBDRigidsEvolution, class T_FPBDCollisionConstraint, class T, int d>
	void TPBDRigidClustering<T_FPBDRigidsEvolution, T_FPBDCollisionConstraint, T, d>::RemoveNodeConnections(
		TPBDRigidParticleHandle<T, d>* Child)
	{
		RemoveNodeConnections(Child->CastToClustered());
	}

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::RemoveNodeConnections"), STAT_RemoveNodeConnections, STATGROUP_Chaos);
	template<class T_FPBDRigidsEvolution, class T_FPBDCollisionConstraint, class T, int d>
	void TPBDRigidClustering<T_FPBDRigidsEvolution, T_FPBDCollisionConstraint, T, d>::RemoveNodeConnections(
		TPBDRigidClusteredParticleHandle<T, d>* ClusteredChild)
	{
		SCOPE_CYCLE_COUNTER(STAT_RemoveNodeConnections);
		check(ClusteredChild);
		TArray<TConnectivityEdge<T>>& Edges = ClusteredChild->ConnectivityEdges();
		for (TConnectivityEdge<T>& Edge : Edges)
		{
			TArray<TConnectivityEdge<T>>& OtherEdges = Edge.Sibling->CastToClustered()->ConnectivityEdges();
			const int32 Idx = OtherEdges.IndexOfByKey(ClusteredChild);
			if (Idx != INDEX_NONE)
				OtherEdges.RemoveAtSwap(Idx);
			// Make sure there are no duplicates!
			check(OtherEdges.IndexOfByKey(ClusteredChild) == INDEX_NONE);
		}
		Edges.SetNum(0);
	}


} // namespace Chaos

using namespace Chaos;
template class CHAOS_API Chaos::TPBDRigidClustering<FPBDRigidsEvolutionGBF, FPBDCollisionConstraints, float, 3>;


template CHAOS_API void Chaos::UpdateClusterMassProperties<float, 3>(
	Chaos::TPBDRigidClusteredParticleHandle<float, 3>* NewParticle,
	TSet<TPBDRigidParticleHandle<float, 3>*>& Children,
	const TRigidTransform<float, 3>* ForceMassOrientation);
