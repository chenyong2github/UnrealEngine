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
	FRealSingle ClusterDistanceThreshold = 100.f;
	FAutoConsoleVariableRef CVarClusterDistance(TEXT("p.ClusterDistanceThreshold"), ClusterDistanceThreshold, TEXT("How close a cluster child must be to a contact to break off"));

	int32 UseConnectivity = 1;
	FAutoConsoleVariableRef CVarUseConnectivity(TEXT("p.UseConnectivity"), UseConnectivity, TEXT("Whether to use connectivity graph when breaking up clusters"));

	CHAOS_API FRealSingle ChaosClusteringChildrenInheritVelocity = 1.f;
	FAutoConsoleVariableRef CVarChildrenInheritVelocity(TEXT("p.ChildrenInheritVelocity"), ChaosClusteringChildrenInheritVelocity, TEXT("Whether children inherit parent collision velocity when declustering. 0 has no impact velocity like glass, 1 has full impact velocity like brick"));

	int32 ComputeClusterCollisionStrains = 1;
	FAutoConsoleVariableRef CVarComputeClusterCollisionStrains(TEXT("p.ComputeClusterCollisionStrains"), ComputeClusterCollisionStrains, TEXT("Whether to use collision constraints when processing clustering."));

	//
	// Update Geometry PVar
	//
	int32 MinLevelsetDimension = 4;
	FAutoConsoleVariableRef CVarMinLevelsetDimension(TEXT("p.MinLevelsetDimension"), MinLevelsetDimension, TEXT("The minimum number of cells on a single level set axis"));

	int32 MaxLevelsetDimension = 20;
	FAutoConsoleVariableRef CVarMaxLevelsetDimension(TEXT("p.MaxLevelsetDimension"), MaxLevelsetDimension, TEXT("The maximum number of cells on a single level set axis"));

	FRealSingle MinLevelsetSize = 50.f;
	FAutoConsoleVariableRef CVarLevelSetResolution(TEXT("p.MinLevelsetSize"), MinLevelsetSize, TEXT("The minimum size on the smallest axis to use a level set"));

	int32 UseLevelsetCollision = 0;
	FAutoConsoleVariableRef CVarUseLevelsetCollision(TEXT("p.UseLevelsetCollision"), UseLevelsetCollision, TEXT("Whether unioned objects use levelsets"));

	int32 LevelsetGhostCells = 1;
	FAutoConsoleVariableRef CVarLevelsetGhostCells(TEXT("p.LevelsetGhostCells"), LevelsetGhostCells, TEXT("Increase the level set grid by this many ghost cells"));

	FRealSingle ClusterSnapDistance = 1.f;
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
	FVec3 GetContactLocation(const FRigidBodyPointContactConstraint& Contact)
	{
		return Contact.GetLocation();
	}

	template<class T, int d>
	FVec3 GetContactLocation(const FRigidBodyContactConstraintPGS& Contact)
	{
		// @todo(mlentine): Does the exact point matter?
		T MinPhi = FLT_MAX;
		FVec3 MinLoc;
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
		FVec3 LinearPseudoMomentum = (InParticles.X(Index) - InParticles.P(Index)) * InParticles.M(Index);
		FRotation3 Delta = InParticles.R(Index) * InParticles.Q(Index).Inverse();
		FVec3 Axis;
		T Angle;
		Delta.ToAxisAndAngle(Axis, Angle);
		FVec3 AngularPseudoMomentum = InParticles.I(Index) * (Axis * Angle);
		return LinearPseudoMomentum.Size() + AngularPseudoMomentum.Size();
	}

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::RewindAndEvolve<BGF>()"), STAT_RewindAndEvolve_BGF, STATGROUP_Chaos);
	template<typename T, int d>
	void RewindAndEvolve(
		FPBDRigidsEvolutionGBF& Evolution, 
		TPBDRigidClusteredParticles<T, d>& InParticles, 
		const TSet<int32>& IslandsToRecollide, 
		const TSet<FPBDRigidParticleHandle*> AllActivatedChildren,
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

		TSet<FGeometryParticleHandle*> AllIslandParticles;
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

		const bool bRewindOnDeclusterSolve = ChaosClusteringChildrenInheritVelocity < 1.f;
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
	void UpdateClusterMassProperties(
		Chaos::FPBDRigidClusteredParticleHandle* Parent, 
		TSet<FPBDRigidParticleHandle*>& Children, 
		const FRigidTransform3* ForceMassOrientation)
	{
		SCOPE_CYCLE_COUNTER(STAT_UpdateClusterMassProperties);
		check(Children.Num());

		Parent->SetX(FVec3(0));
		Parent->SetR(FRotation3(FQuat::MakeFromEuler(FVec3(0))));
		Parent->SetV(FVec3(0));
		Parent->SetW(FVec3(0));
		Parent->SetM(0);
		Parent->SetI(FMatrix33(0));

		bool bHasChild = false;
		bool bHasProxyChild = false;
		for (FPBDRigidParticleHandle* OriginalChild : Children)
		{
			FMultiChildProxyId MultiChildProxyId; // sizeof(FMultiChildProxyId) = sizeof(void*), so copy
			TMultiChildProxyData<FReal, 3>* ProxyData = nullptr;
			if (FPBDRigidClusteredParticleHandle* ClusteredOriginalChild = OriginalChild->CastToClustered())
			{
				MultiChildProxyId = ClusteredOriginalChild->MultiChildProxyId();
				ProxyData = ClusteredOriginalChild->MultiChildProxyData().Get();
			}

			//int32 Child;
			FPBDRigidParticleHandle* Child;
			FVec3 ChildPosition;
			FRotation3 ChildRotation;
			if (MultiChildProxyId.Id == nullptr)
			{
				Child = OriginalChild;
				ChildPosition = Child->X();
				ChildRotation = Child->R();
			}
			else if (ProxyData->KeyChild == OriginalChild)
			{
				Child = MultiChildProxyId.Id;

				const FRigidTransform3 ProxyWorldTM =
					ProxyData->RelativeToKeyChild *
					FRigidTransform3(
						OriginalChild->X(), OriginalChild->R());
				ChildPosition = ProxyWorldTM.GetLocation();
				ChildRotation = ProxyWorldTM.GetRotation();
			}
			else
			{
				continue; //using a proxy but we are not the key child
			}

			const FReal ChildMass = Child->M();
			const FMatrix33 ChildWorldSpaceI = 
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
			for (FPBDRigidParticleHandle* OriginalChild : Children)
			{
				FPBDRigidParticleHandle* Child = OriginalChild;
				const FVec3& ChildPosition = Child->X();
				const FRotation3& ChildRotation = Child->R();
				const FReal ChildMass = Child->M();

				const FMatrix33 ChildWorldSpaceI =
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
		for (int32 i = 0; i < 3; i++)
		{
			const FMatrix33& InertiaTensor = Parent->I();
			if (InertiaTensor.GetColumn(i)[i] < SMALL_NUMBER)
			{
				Parent->SetI(FMatrix33(1.f, 1.f, 1.f));
				break;
			}
		}

		if (!ensure(bHasChild) || !ensure(Parent->M() > SMALL_NUMBER))
		{
			Parent->M() = 1.0;
			Parent->X() = FVec3(0);
			Parent->V() = FVec3(0);
			Parent->PreV() = Parent->V();
			Parent->InvM() = 1;
			Parent->P() = Parent->X();
			Parent->W() = FVec3(0);
			Parent->PreW() = Parent->W();
			Parent->R() = FRotation3(FMatrix::Identity);
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
		for (FPBDRigidParticleHandle* OriginalChild : Children)
		{
			FMultiChildProxyId MultiChildProxyId; // sizeof(FMultiChildProxyId) = sizeof(void*), so copy
			TMultiChildProxyData<FReal, 3>* ProxyData = nullptr;
			if (FPBDRigidClusteredParticleHandle* ClusteredOriginalChild = OriginalChild->CastToClustered())
			{
				MultiChildProxyId = ClusteredOriginalChild->MultiChildProxyId();
				ProxyData = ClusteredOriginalChild->MultiChildProxyData().Get();
			}

			FPBDRigidParticleHandle* Child;
			FVec3 ChildPosition;
			if (MultiChildProxyId.Id == nullptr)
			{
				Child = OriginalChild;
				ChildPosition = Child->X();
			}
			else if (ProxyData && ProxyData->KeyChild == OriginalChild)
			{
				Child = MultiChildProxyId.Id;
				const FRigidTransform3 ProxyWorldTM = 
					ProxyData->RelativeToKeyChild * FRigidTransform3(OriginalChild->X(), OriginalChild->R());
				ChildPosition = ProxyWorldTM.GetLocation();
			}
			else
			{
				continue; //using a proxy but we are not the key child
			}

			FVec3 ParentToChild = ChildPosition - Parent->X();

			const FReal ChildMass = Child->M();
			// taking v from original child since we are not actually simulating the proxy child
			Parent->W() += 
				FVec3::CrossProduct(ParentToChild, 
					OriginalChild->V() * ChildMass);
			{
				const FReal& p0 = ParentToChild[0];
				const FReal& p1 = ParentToChild[1];
				const FReal& p2 = ParentToChild[2];
				const FReal& m = ChildMass;
				Parent->I() += 
					FMatrix33(
						m * (p1 * p1 + p2 * p2), -m * p1 * p0, -m * p2 * p0, 
						m * (p2 * p2 + p0 * p0), -m * p2 * p1, m * (p1 * p1 + p0 * p0));
			}
		}
		FMatrix33& InertiaTensor = Parent->I();
		if (Parent->I().ContainsNaN())
		{
			InertiaTensor = FMatrix33((FReal)1., (FReal)1., (FReal)1.);
		}
		else
		{
			for (int32 i = 0; i < 3; i++)
			{
				if (InertiaTensor.GetColumn(i)[i] < SMALL_NUMBER)
				{
					InertiaTensor = FMatrix33((FReal)1., (FReal)1., (FReal)1.);
					break;
				}
			}
		}
		Parent->W() /= Parent->M();
		Parent->PreW() = Parent->W();
		Parent->R() = Chaos::TransformToLocalSpace(InertiaTensor);
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

	template<class T_FPBDRigidsEvolution, class T_FPBDCollisionConstraint>
	TPBDRigidClustering<T_FPBDRigidsEvolution, T_FPBDCollisionConstraint>::TPBDRigidClustering(T_FPBDRigidsEvolution& InEvolution, FPBDRigidClusteredParticles& InParticles)
		: MEvolution(InEvolution)
		, MParticles(InParticles)
		, MCollisionImpulseArrayDirty(true)
		, DoGenerateBreakingData(false)
		, MClusterConnectionFactor(1.0)
		, MClusterUnionConnectionType(FClusterCreationParameters::EConnectionMethod::DelaunayTriangulation)
	{}

	template<class T_FPBDRigidsEvolution, class T_FPBDCollisionConstraint>
	TPBDRigidClustering<T_FPBDRigidsEvolution, T_FPBDCollisionConstraint>::~TPBDRigidClustering()
	{}

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::CreateClusterParticle"), STAT_CreateClusterParticle, STATGROUP_Chaos);
	template<class T_FPBDRigidsEvolution, class T_FPBDCollisionConstraint>
	Chaos::FPBDRigidClusteredParticleHandle* TPBDRigidClustering<T_FPBDRigidsEvolution, T_FPBDCollisionConstraint>::CreateClusterParticle(const int32 ClusterGroupIndex, TArray<Chaos::FPBDRigidParticleHandle*>&& Children, const FClusterCreationParameters& Parameters, TSharedPtr<Chaos::FImplicitObject, ESPMode::ThreadSafe> ProxyGeometry, const FRigidTransform3* ForceMassOrientation, const FUniqueIdx* ExistingIndex)
	{
		SCOPE_CYCLE_COUNTER(STAT_CreateClusterParticle);

		Chaos::FPBDRigidClusteredParticleHandle* NewParticle = Parameters.ClusterParticleHandle;
		if (!NewParticle)
		{
			NewParticle = MEvolution.CreateClusteredParticles(1, ExistingIndex)[0]; // calls Evolution.DirtyParticle()
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

		// Update clustering data structures.
		if (MChildren.Contains(NewParticle))
		{
			MChildren[NewParticle] = MoveTemp(Children);
		}
		else
		{
			MChildren.Add(NewParticle, MoveTemp(Children));
		}

		const TArray<FPBDRigidParticleHandle*>& ChildrenArray = MChildren[NewParticle];
		TSet<FPBDRigidParticleHandle*> ChildrenSet(ChildrenArray);

		// Disable the children
		MEvolution.DisableParticles(reinterpret_cast<TSet<FGeometryParticleHandle*>&>(ChildrenSet));

		bool bClusterIsAsleep = true;
		for (FPBDRigidParticleHandle* Child : ChildrenSet)
		{
			bClusterIsAsleep &= Child->Sleeping();

			if (FPBDRigidClusteredParticleHandle* ClusteredChild = Child->CastToClustered())
			{
				TopLevelClusterParents.Remove(ClusteredChild);

				// Cluster group id 0 means "don't union with other things"
				// TODO: Use INDEX_NONE instead of 0?
				ClusteredChild->SetClusterGroupIndex(0);
				ClusteredChild->ClusterIds().Id = NewParticle;
				NewParticle->Strains() += ClusteredChild->Strains();

				NewParticle->SetCollisionImpulses(FMath::Max(NewParticle->CollisionImpulses(), ClusteredChild->CollisionImpulses()));

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

		if(ClusterGroupIndex)
		{
			AddToClusterUnion(ClusterGroupIndex, NewParticle);
		}

		return NewParticle;
	}

	int32 UnionsHaveCollisionParticles = 0;
	FAutoConsoleVariableRef CVarUnionsHaveCollisionParticles(TEXT("p.UnionsHaveCollisionParticles"), UnionsHaveCollisionParticles, TEXT(""));

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::CreateClusterParticleFromClusterChildren"), STAT_CreateClusterParticleFromClusterChildren, STATGROUP_Chaos);
	template<class T_FPBDRigidsEvolution, class T_FPBDCollisionConstraint>
	Chaos::FPBDRigidClusteredParticleHandle* 
	TPBDRigidClustering<T_FPBDRigidsEvolution, T_FPBDCollisionConstraint>::CreateClusterParticleFromClusterChildren(
		TArray<FPBDRigidParticleHandle*>&& Children, 
		FPBDRigidClusteredParticleHandle* Parent, 
		const FRigidTransform3& ClusterWorldTM, 
		const FClusterCreationParameters& Parameters)
	{
		SCOPE_CYCLE_COUNTER(STAT_CreateClusterParticleFromClusterChildren);

		//This cluster is made up of children that are currently in a cluster. This means we don't need to update or disable as much
		Chaos::FPBDRigidClusteredParticleHandle* NewParticle = Parameters.ClusterParticleHandle;
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

		TArray<FPBDRigidParticleHandle*>& ChildrenArray = MChildren[NewParticle];
		//child transforms are out of date, need to update them. @todo(ocohen): if children transforms are relative we would not need to update this, but would simply have to do a final transform on the new cluster index
		// TODO(mlentine): Why is this not needed? (Why is it ok to have DeactivateClusterChildren==false?)
		if (DeactivateClusterChildren)
		{
			//TODO: avoid iteration just pass in a view
			TSet<FGeometryParticleHandle*> ChildrenHandles(static_cast<TArray<FGeometryParticleHandle*>>(ChildrenArray));
			MEvolution.DisableParticles(ChildrenHandles);
		}
		for (FPBDRigidParticleHandle* Child : ChildrenArray)
		{
			if (FPBDRigidClusteredParticleHandle* ClusteredChild = Child->CastToClustered())
			{
				FRigidTransform3 ChildFrame = ClusteredChild->ChildToParent() * ClusterWorldTM;
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

		FClusterCreationParameters NoCleanParams = Parameters;
		NoCleanParams.bCleanCollisionParticles = false;
		NoCleanParams.bCopyCollisionParticles = !!UnionsHaveCollisionParticles;

		TSet<FPBDRigidParticleHandle*> ChildrenSet(ChildrenArray);
		UpdateMassProperties(NewParticle, ChildrenSet, nullptr);
		UpdateGeometry(NewParticle, ChildrenSet, nullptr, NoCleanParams);

		return NewParticle;
	}

	int32 UseMultiChildProxy = 1;
	FAutoConsoleVariableRef CVarUseMultiChildProxy(TEXT("p.UseMultiChildProxy"), UseMultiChildProxy, TEXT("Whether to merge multiple children into a single collision proxy when one is available"));

	int32 MinChildrenForMultiProxy = 1;
	FAutoConsoleVariableRef CVarMinChildrenForMultiProxy(TEXT("p.MinChildrenForMultiProxy"), MinChildrenForMultiProxy, TEXT("Min number of children needed for multi child proxy optimization"));

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::UnionClusterGroups"), STAT_UnionClusterGroups, STATGROUP_Chaos);
	template<class T_FPBDRigidsEvolution, class T_FPBDCollisionConstraint>
	void TPBDRigidClustering<T_FPBDRigidsEvolution, T_FPBDCollisionConstraint>::UnionClusterGroups()
	{
		SCOPE_CYCLE_COUNTER(STAT_UnionClusterGroups);

		if(ClusterUnionMap.Num())
		{
			TMap<FPBDRigidParticleHandle*, FPBDRigidParticleHandle*> ChildToParentMap;
			TMap<int32, TArray<FPBDRigidParticleHandle*>> NewClusterGroups;

			// Walk the list of registered cluster groups
			for(TTuple<int32, TArray<FPBDRigidClusteredParticleHandle* >>& Group : ClusterUnionMap)
			{
				int32 ClusterGroupID = Group.Key;
				TArray<FPBDRigidClusteredParticleHandle*> Handles = Group.Value;

				if(Handles.Num() > 1)
				{
					// First see if this is a new group
					if(!NewClusterGroups.Contains(ClusterGroupID))
					{
						NewClusterGroups.Add(ClusterGroupID, TArray < FPBDRigidParticleHandle*>());
					}

					TArray<FPBDRigidParticleHandle*> ClusterBodies;
					for(FPBDRigidClusteredParticleHandle* ActiveCluster : Handles)
					{
						if(!ActiveCluster->Disabled())
						{
							// If this is an external cluster (from the rest collection) we release its children and append them to the current group
							TSet<FPBDRigidParticleHandle*> Children;
							
							{
								// First disable breaking data generation - this is not a break we're just reclustering under a dynamic parent.
								TGuardValue<bool> BreakFlagGuard(DoGenerateBreakingData, false);
								Children = ReleaseClusterParticles(ActiveCluster, nullptr, true);
							}

							NewClusterGroups[ClusterGroupID].Append(Children.Array());
							
							for(FPBDRigidParticleHandle* Child : Children)
							{
								ChildToParentMap.Add(Child, ActiveCluster);
							}
						}
					}
				}
			}

			// For new cluster groups, create an internal cluster parent.
			for(TTuple<int32, TArray<FPBDRigidParticleHandle* >>& Group : NewClusterGroups)
			{
				int32 ClusterGroupID = FMath::Abs(Group.Key);

				TArray<FPBDRigidParticleHandle*> ActiveCluster = Group.Value;

				FClusterCreationParameters Parameters(0.3, 100, false, !!UnionsHaveCollisionParticles);
				Parameters.ConnectionMethod = MClusterUnionConnectionType;
				TPBDRigidClusteredParticleHandleImp<FReal, 3, true>* Handle = CreateClusterParticle(-ClusterGroupID, MoveTemp(Group.Value), Parameters, TSharedPtr<FImplicitObject, ESPMode::ThreadSafe>());
				Handle->SetInternalCluster(true);

				MEvolution.SetPhysicsMaterial(Handle, MEvolution.GetPhysicsMaterial(ActiveCluster[0]));

				for(FPBDRigidParticleHandle* Constituent : ActiveCluster)
				{
					MEvolution.DoInternalParticleInitilization(ChildToParentMap[Constituent], Handle);
				}
			}

			ClusterUnionMap.Empty();
		}
	}


	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::DeactivateClusterParticle"), STAT_DeactivateClusterParticle, STATGROUP_Chaos);
	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint>
	TSet<FPBDRigidParticleHandle*> TPBDRigidClustering<FPBDRigidsEvolution, FPBDCollisionConstraint>::DeactivateClusterParticle(
		FPBDRigidClusteredParticleHandle* ClusteredParticle)
	{
		SCOPE_CYCLE_COUNTER(STAT_DeactivateClusterParticle);

		TSet<FPBDRigidParticleHandle*> ActivatedChildren;
		check(!ClusteredParticle->Disabled());
		if (MChildren.Contains(ClusteredParticle))
		{
			ActivatedChildren = ReleaseClusterParticles(MChildren[ClusteredParticle]);
		}
		return ActivatedChildren;
	}

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::ReleaseClusterParticles(STRAIN)"), STAT_ReleaseClusterParticles_STRAIN, STATGROUP_Chaos);
	template<class T_FPBDRigidsEvolution, class T_FPBDCollisionConstraint>
	TSet<FPBDRigidParticleHandle*> 
	TPBDRigidClustering<T_FPBDRigidsEvolution, T_FPBDCollisionConstraint>::ReleaseClusterParticles(
		FPBDRigidClusteredParticleHandle* ClusteredParticle,
		const TMap<FGeometryParticleHandle*, FReal>* ExternalStrainMap,
		bool bForceRelease)
	{
		SCOPE_CYCLE_COUNTER(STAT_ReleaseClusterParticles_STRAIN);

		TSet<FPBDRigidParticleHandle*> ActivatedChildren;
		if (!ensureMsgf(MChildren.Contains(ClusteredParticle), TEXT("Removing Cluster that does not exist!")))
		{
			return ActivatedChildren;
		}
		TArray<FPBDRigidParticleHandle*>& Children = MChildren[ClusteredParticle];

		bool bChildrenChanged = false;
		const bool bRewindOnDecluster = ChaosClusteringChildrenInheritVelocity < 1;
		const FRigidTransform3 PreSolveTM = 
			bRewindOnDecluster ? 
			FRigidTransform3(ClusteredParticle->X(), ClusteredParticle->R()) : 
			FRigidTransform3(ClusteredParticle->P(), ClusteredParticle->Q());

		//@todo(ocohen): iterate with all the potential parents at once?
		//find all children within some distance of contact point

		auto RemoveChildLambda = [&](FPBDRigidParticleHandle* Child/*, const int32 Idx*/)
		{
			FPBDRigidClusteredParticleHandle* ClusteredChild = Child->CastToClustered();

			MEvolution.EnableParticle(Child, ClusteredParticle);
			TopLevelClusterParents.Add(ClusteredChild);

			//make sure to remove multi child proxy if it exists
			ClusteredChild->MultiChildProxyData().Reset();
			ClusteredChild->MultiChildProxyId().Id = nullptr;
			ClusteredChild->SetClusterId(ClusterId(nullptr, ClusteredChild->ClusterIds().NumChildren)); // clear Id but retain number of children

			const FRigidTransform3 ChildFrame = ClusteredChild->ChildToParent() * PreSolveTM;
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
			FPBDRigidClusteredParticleHandle* Child = Children[ChildIdx]->CastToClustered();
			
			if (!Child)
			{
				continue;
			}

			Chaos::FReal ChildStrain = 0.0;

			if(ExternalStrainMap)
			{
				const Chaos::FReal* MapStrain = ExternalStrainMap->Find(Child);
				ChildStrain = MapStrain ? *MapStrain : Child->CollisionImpulses();
			}
			else
			{
				ChildStrain = Child->CollisionImpulses();
			}

			if (ChildStrain >= Child->Strain() || bForceRelease)
			{
				//UE_LOG(LogTemp, Warning, TEXT("Releasing child %d from parent %p due to strain %.5f Exceeding internal strain %.5f (Source: %s)"), ChildIdx, ClusteredParticle, ChildStrain, Child->Strain(), bForceRelease ? TEXT("Forced by caller") : ExternalStrainMap ? TEXT("External") : TEXT("Collision"));

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
						const int32 NewIdx = MAllClusterBreakings.Add(FBreakingData());
						FBreakingData& ClusterBreak = MAllClusterBreakings[NewIdx];
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
				for (FPBDRigidParticleHandle* Child : ActivatedChildren)
				{
					RemoveNodeConnections(Child);
				}

				if (Children.Num())
				{
					TArray<TArray<FPBDRigidParticleHandle*>> ConnectedPiecesArray;

					{ // tmp scope

						//traverse connectivity and see how many connected pieces we have
						TSet<FPBDRigidParticleHandle*> ProcessedChildren;
						ProcessedChildren.Reserve(Children.Num());

						for (FPBDRigidParticleHandle* PotentialActivatedChild : Children)
						{
							if (ProcessedChildren.Contains(PotentialActivatedChild))
							{
								continue;
							}
							ConnectedPiecesArray.AddDefaulted();
							TArray<FPBDRigidParticleHandle*>& ConnectedPieces = ConnectedPiecesArray.Last();

							TArray<FPBDRigidParticleHandle*> ProcessingQueue;
							ProcessingQueue.Add(PotentialActivatedChild);
							while (ProcessingQueue.Num())
							{
								FPBDRigidParticleHandle* Child = ProcessingQueue.Pop();
								if (!ProcessedChildren.Contains(Child))
								{
									ProcessedChildren.Add(Child);
									ConnectedPieces.Add(Child);
									for (const TConnectivityEdge<FReal>& Edge : Child->CastToClustered()->ConnectivityEdges())
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
					for (TArray<FPBDRigidParticleHandle*>& ConnectedPieces : ConnectedPiecesArray)
					{
						if (ConnectedPieces.Num() == 1) //need to break single pieces first in case multi child proxy needs to be invalidated
						{
							FPBDRigidParticleHandle* Child = ConnectedPieces[0];
							RemoveChildLambda(Child);
						}
						else if (ConnectedPieces.Num() > 1)
						{
							NumNewClusters++;
						}
					}
					TArray<Chaos::FPBDRigidClusteredParticleHandle*> NewClusterHandles = 
						MEvolution.CreateClusteredParticles(NumNewClusters);
					int32 ClusterHandlesIdx = 0;
					for (TArray<FPBDRigidParticleHandle*>& ConnectedPieces : ConnectedPiecesArray)
					{
						if (ConnectedPieces.Num() > 1) //now build the remaining pieces
						{
							Chaos::FClusterCreationParameters CreationParameters;
							CreationParameters.ClusterParticleHandle = NewClusterHandles[ClusterHandlesIdx++];
							Chaos::FPBDRigidClusteredParticleHandle* NewCluster = 
								CreateClusterParticleFromClusterChildren(
									MoveTemp(ConnectedPieces), 
									ClusteredParticle, 
									PreSolveTM, 
									CreationParameters);

							MEvolution.SetPhysicsMaterial(
								NewCluster, MEvolution.GetPhysicsMaterial(ClusteredParticle));

							NewCluster->SetStrain(ClusteredParticle->Strain());
							NewCluster->SetV(ClusteredParticle->V());
							NewCluster->SetW(ClusteredParticle->W());
							NewCluster->SetPreV(ClusteredParticle->PreV());
							NewCluster->SetPreW(ClusteredParticle->PreW());
							NewCluster->SetP(NewCluster->X());
							NewCluster->SetQ(NewCluster->R());

							// Need to get the material from the previous particle and apply it to the new one
							const FShapesArray& ChildShapes = ClusteredParticle->ShapesArray();
							const FShapesArray& NewShapes = NewCluster->ShapesArray();
							const int32 NumChildShapes = ClusteredParticle->ShapesArray().Num();

							if(NumChildShapes > 0)
							{
								// Can only take materials if the child has any - otherwise we fall back on defaults.
								// Due to GC initialisation however, we should always have a valid material as even
								// when one cannot be found we fall back on the default on GEngine
								const int32 NumChildMaterials = ChildShapes[0]->GetMaterials().Num();
								if(NumChildMaterials > 0)
								{
									Chaos::FMaterialHandle ChildMat = ChildShapes[0]->GetMaterials()[0];

									for(const TUniquePtr<FPerShapeData>& PerShape : NewShapes)
									{
										PerShape->SetMaterial(ChildMat);
									}
								}
							}

							ActivatedChildren.Add(NewCluster);
						}
					}
				}
			}

			for (FPBDRigidParticleHandle* Child : ActivatedChildren)
			{
				UpdateKinematicProperties(Child);
			}

			//disable cluster
			DisableCluster(ClusteredParticle);
		} // bChildrenChanged

		return ActivatedChildren;
	}



	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::ReleaseClusterParticlesNoInternalCluster"), STAT_ReleaseClusterParticlesNoInternalCluster, STATGROUP_Chaos);
	template<class T_FPBDRigidsEvolution, class T_FPBDCollisionConstraint>
	TSet<FPBDRigidParticleHandle*>
		TPBDRigidClustering<T_FPBDRigidsEvolution, T_FPBDCollisionConstraint>::ReleaseClusterParticlesNoInternalCluster(
			FPBDRigidClusteredParticleHandle* ClusteredParticle,
			const TMap<FGeometryParticleHandle*, FReal>* ExternalStrainMap,
			bool bForceRelease)
	{
		/* This is a near duplicate of the ReleaseClusterParticles() method with the internal cluster creation removed.
		*  This method should be used exclusively by the GeometryCollectionComponentCacheAdaptor in order to implement
		*  correct behavior when cluster grouping is used. 
		*/
		
		SCOPE_CYCLE_COUNTER(STAT_ReleaseClusterParticlesNoInternalCluster);

		TSet<FPBDRigidParticleHandle*> ActivatedChildren;
		if (!ensureMsgf(MChildren.Contains(ClusteredParticle), TEXT("Removing Cluster that does not exist!")))
		{
			return ActivatedChildren;
		}
		TArray<FPBDRigidParticleHandle*>& Children = MChildren[ClusteredParticle];

		bool bChildrenChanged = false;
		const bool bRewindOnDecluster = ChaosClusteringChildrenInheritVelocity < 1;
		const FRigidTransform3 PreSolveTM =
			bRewindOnDecluster ?
			FRigidTransform3(ClusteredParticle->X(), ClusteredParticle->R()) :
			FRigidTransform3(ClusteredParticle->P(), ClusteredParticle->Q());

		//@todo(ocohen): iterate with all the potential parents at once?
		//find all children within some distance of contact point

		auto RemoveChildLambda = [&](FPBDRigidParticleHandle* Child/*, const int32 Idx*/)
		{
			FPBDRigidClusteredParticleHandle* ClusteredChild = Child->CastToClustered();

			MEvolution.EnableParticle(Child, ClusteredParticle);
			TopLevelClusterParents.Add(ClusteredChild);

			//make sure to remove multi child proxy if it exists
			ClusteredChild->MultiChildProxyData().Reset();
			ClusteredChild->MultiChildProxyId().Id = nullptr;
			ClusteredChild->SetClusterId(ClusterId(nullptr, ClusteredChild->ClusterIds().NumChildren)); // clear Id but retain number of children

			const FRigidTransform3 ChildFrame = ClusteredChild->ChildToParent() * PreSolveTM;
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
			FPBDRigidClusteredParticleHandle* Child = Children[ChildIdx]->CastToClustered();

			if (!Child)
			{
				continue;
			}

			Chaos::FReal ChildStrain = 0.0;

			if (ExternalStrainMap)
			{
				const Chaos::FReal* MapStrain = ExternalStrainMap->Find(Child);
				ChildStrain = MapStrain ? *MapStrain : Child->CollisionImpulses();
			}
			else
			{
				ChildStrain = Child->CollisionImpulses();
			}


			if (ChildStrain >= Child->Strain() || bForceRelease)
			{
				//UE_LOG(LogTemp, Warning, TEXT("Releasing child %d from parent %p due to strain %.5f Exceeding internal strain %.5f (Source: %s)"), ChildIdx, ClusteredParticle, ChildStrain, Child->Strain(), bForceRelease ? TEXT("Forced by caller") : ExternalStrainMap ? TEXT("External") : TEXT("Collision"));

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
						const int32 NewIdx = MAllClusterBreakings.Add(FBreakingData());
						FBreakingData& ClusterBreak = MAllClusterBreakings[NewIdx];
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
				for (FPBDRigidParticleHandle* Child : ActivatedChildren)
				{
					RemoveNodeConnections(Child);
				}

				if (Children.Num())
				{
					TArray<TArray<FPBDRigidParticleHandle*>> ConnectedPiecesArray;

					{ // tmp scope

						//traverse connectivity and see how many connected pieces we have
						TSet<FPBDRigidParticleHandle*> ProcessedChildren;
						ProcessedChildren.Reserve(Children.Num());

						for (FPBDRigidParticleHandle* PotentialActivatedChild : Children)
						{
							if (ProcessedChildren.Contains(PotentialActivatedChild))
							{
								continue;
							}
							ConnectedPiecesArray.AddDefaulted();
							TArray<FPBDRigidParticleHandle*>& ConnectedPieces = ConnectedPiecesArray.Last();

							TArray<FPBDRigidParticleHandle*> ProcessingQueue;
							ProcessingQueue.Add(PotentialActivatedChild);
							while (ProcessingQueue.Num())
							{
								FPBDRigidParticleHandle* Child = ProcessingQueue.Pop();
								if (!ProcessedChildren.Contains(Child))
								{
									ProcessedChildren.Add(Child);
									ConnectedPieces.Add(Child);
									for (const TConnectivityEdge<FReal>& Edge : Child->CastToClustered()->ConnectivityEdges())
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
					for (TArray<FPBDRigidParticleHandle*>& ConnectedPieces : ConnectedPiecesArray)
					{
						if (ConnectedPieces.Num() == 1) //need to break single pieces first in case multi child proxy needs to be invalidated
						{
							FPBDRigidParticleHandle* Child = ConnectedPieces[0];
							RemoveChildLambda(Child);
						}
						else if (ConnectedPieces.Num() > 1)
						{
							NumNewClusters++;
						}
					}			
				}
			}

			for (FPBDRigidParticleHandle* Child : ActivatedChildren)
			{
				UpdateKinematicProperties(Child);
			}

			//disable cluster
			DisableCluster(ClusteredParticle);
		} // bChildrenChanged

		return ActivatedChildren;
	}




	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::ReleaseClusterParticles(LIST)"), STAT_ReleaseClusterParticles_LIST, STATGROUP_Chaos);
	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint>
	TSet<FPBDRigidParticleHandle*> TPBDRigidClustering<FPBDRigidsEvolution, FPBDCollisionConstraint>::ReleaseClusterParticles(
		TArray<FPBDRigidParticleHandle*> ChildrenParticles)
	{
		SCOPE_CYCLE_COUNTER(STAT_ReleaseClusterParticles_LIST);
		TSet<FPBDRigidParticleHandle*> ActivatedBodies;
		if (ChildrenParticles.Num())
		{
			FPBDRigidParticleHandle* ClusterHandle = nullptr;
			//todo(ocohen): refactor incoming, for now just assume these all belong to same cluster and hack strain array
			
			TMap<FGeometryParticleHandle*, FReal> FakeStrain;

			bool bPreDoGenerateData = DoGenerateBreakingData;
			DoGenerateBreakingData = false;

			for (FPBDRigidParticleHandle* ChildHandle : ChildrenParticles)
			{
				if (FPBDRigidClusteredParticleHandle* ClusteredChildHandle = ChildHandle->CastToClustered())
				{
					if (ClusteredChildHandle->Disabled() && ClusteredChildHandle->ClusterIds().Id != nullptr)
					{
						if (ensure(!ClusterHandle || ClusteredChildHandle->ClusterIds().Id == ClusterHandle))
						{
							FakeStrain.Add(ClusteredChildHandle, TNumericLimits<FReal>::Max());
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
	template<class T_FPBDRigidsEvolution, class T_FPBDCollisionConstraint>
	void 
	TPBDRigidClustering<T_FPBDRigidsEvolution, T_FPBDCollisionConstraint>::AdvanceClustering(
		const FReal Dt, 
		T_FPBDCollisionConstraint& CollisionRule)
	{
		SCOPE_CYCLE_COUNTER(STAT_AdvanceClustering);
		UE_LOG(LogChaos, Verbose, TEXT("START FRAME with Dt %f"), Dt);

		double FrameTime = 0, Time = 0;
		FDurationTimer Timer(Time);
		Timer.Start();

		{
			const FReal Threshold = (FReal)1.f;
			TSet<Chaos::FPBDRigidParticleHandle*> RemovalIndicesCopy = MActiveRemovalIndices;
			for (Chaos::FPBDRigidParticleHandle* Particle : RemovalIndicesCopy)
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
						TArray<FPBDRigidParticleHandle*>& ParentToChildren = MChildren[ActiveCluster];
						for (FPBDRigidParticleHandle* Child : ParentToChildren)
						{
							if (FPBDRigidClusteredParticleHandle* ClusteredChild = Child->CastToClustered())
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
				TMap<FPBDRigidClusteredParticleHandle*, TSet<FPBDRigidParticleHandle*>> ClusterToActivatedChildren = 
					BreakingModel();

				TSet<FPBDRigidParticleHandle*> AllActivatedChildren;
				TSet<int32> IslandsToRecollide;
				for (auto Itr : ClusterToActivatedChildren)
				{
					//question: do we need to iterate all the children? Seems like island is known from cluster, but don't want to break anything at this point
					TSet<FPBDRigidParticleHandle*>& ActivatedChildren = Itr.Value;
					for (FPBDRigidParticleHandle* ActiveChild : ActivatedChildren)
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

				const bool bRewindOnDecluster = ChaosClusteringChildrenInheritVelocity < 1.f;
				if (bRewindOnDecluster && AllActivatedChildren.Num())
				{
					SCOPE_CYCLE_COUNTER(STAT_ClusterRewind);

					if (MEvolution.NumIslands())
					{
						RewindAndEvolve(MEvolution, MParticles, IslandsToRecollide, AllActivatedChildren, Dt, CollisionRule);
					}

					if (ChaosClusteringChildrenInheritVelocity > 0.f)
					{
						for (auto Itr : ClusterToActivatedChildren)
						{
							FPBDRigidClusteredParticleHandle* ClusteredParticle = Itr.Key;
							TSet<FPBDRigidParticleHandle*>& ActivatedChildren = Itr.Value;
							for (FPBDRigidParticleHandle* ActiveChild : ActivatedChildren)
							{
								ActiveChild->SetV(
									ActiveChild->V() * (1.f - ChaosClusteringChildrenInheritVelocity) + ClusteredParticle->V() * ChaosClusteringChildrenInheritVelocity);
								ActiveChild->SetW(
									ActiveChild->W() * (1.f - ChaosClusteringChildrenInheritVelocity) + ClusteredParticle->W() * ChaosClusteringChildrenInheritVelocity);
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
	template<class T_FPBDRigidsEvolution, class T_FPBDCollisionConstraint>
	TMap<FPBDRigidClusteredParticleHandle*, TSet<FPBDRigidParticleHandle*>> 
	TPBDRigidClustering<T_FPBDRigidsEvolution, T_FPBDCollisionConstraint>::BreakingModel(
		TMap<FGeometryParticleHandle*, FReal>* ExternalStrainMap)
	{
		SCOPE_CYCLE_COUNTER(STAT_BreakingModel);

		TMap<FPBDRigidClusteredParticleHandle*, TSet<FPBDRigidParticleHandle*>> AllActivatedChildren;

		auto NonDisabledClusteredParticles = MEvolution.GetNonDisabledClusteredArray(); //make copy because release cluster modifies active indices. We want to iterate over original active indices
		for (Chaos::TPBDRigidClusteredParticleHandleImp<FReal, 3, true>* ClusteredParticle : NonDisabledClusteredParticles)
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
							int32 NewIdx = MAllClusterBreakings.Add(FBreakingData());
							FBreakingData& ClusterBreak = MAllClusterBreakings[NewIdx];
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
	template<class T_FPBDRigidsEvolution, class T_FPBDCollisionConstraint>
	FReal TPBDRigidClustering<T_FPBDRigidsEvolution, T_FPBDCollisionConstraint>::PromoteStrains(
		FPBDRigidParticleHandle* CurrentNode)
	{
		SCOPE_CYCLE_COUNTER(STAT_PromoteStrains);
		if (FPBDRigidClusteredParticleHandle* ClusteredCurrentNode = CurrentNode->CastToClustered())
		{
			FReal ChildrenStrains = (FReal)0.;
			if (MChildren.Contains(CurrentNode))
			{
				for (FPBDRigidParticleHandle* Child : MChildren[CurrentNode])
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
		return (FReal)0.;
	}

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::UpdateKinematicProperties()"), STAT_UpdateKinematicProperties, STATGROUP_Chaos);
	template<class T_FPBDRigidsEvolution, class T_FPBDCollisionConstraint>
	void TPBDRigidClustering<T_FPBDRigidsEvolution, T_FPBDCollisionConstraint>::UpdateKinematicProperties(
		Chaos::FPBDRigidParticleHandle* Parent)
	{
		SCOPE_CYCLE_COUNTER(STAT_UpdateKinematicProperties);

		EObjectStateType ObjectState = EObjectStateType::Dynamic;
		check(Parent != nullptr);
		if (MChildren.Contains(Parent) && MChildren[Parent].Num())
		{
			// TQueue is a linked list, which has no preallocator.
			TQueue<Chaos::FPBDRigidParticleHandle*> Queue;
			for (Chaos::FPBDRigidParticleHandle* Child : MChildren[Parent])
			{
				Queue.Enqueue(Child);
			}

			Chaos::FPBDRigidParticleHandle* CurrentHandle;
			while (Queue.Dequeue(CurrentHandle) && ObjectState == EObjectStateType::Dynamic)
			{
				// @question : Maybe we should just store the leaf node bodies in a
				// map, that will require Memory(n*log(n))
				if (MChildren.Contains(CurrentHandle))
				{
					for (Chaos::FPBDRigidParticleHandle* Child : MChildren[CurrentHandle])
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

			Parent->SetObjectStateLowLevel(ObjectState);
		}
	}

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::SwapBufferedData"), STAT_SwapBufferedData, STATGROUP_Chaos);
	template<class T_FPBDRigidsEvolution, class T_FPBDCollisionConstraint>
	void TPBDRigidClustering<T_FPBDRigidsEvolution, T_FPBDCollisionConstraint>::SwapBufferedData()
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
			TArray<FPBDRigidParticleHandle*>* Children = MChildren.Find(ClusteredParticle);
			//if (Children && ClusteredParticle->ClusterIds().Id == INDEX_NONE) //root cluster so copy children
			if (Children && ClusteredParticle->ClusterIds().Id == nullptr) //root cluster so copy children
			{
				//TODO: record GT particle pointer instead
				//BufferResource.MChildren.Add(ClusteredParticle->TransientParticleIndex(), *Children);
				BufferResource.MChildren.Add(
					ClusteredParticle, *Children);
				//BufferResource.ClusterParentTransforms.Add(ClusteredParticle->TransientParticleIndex(), TRigidTransform<FReal, 3>(ClusteredParticle->X(), ClusteredParticle->R()));
				BufferResource.ClusterParentTransforms.Add(
					ClusteredParticle, TRigidTransform<FReal, 3>(ClusteredParticle->X(), ClusteredParticle->R()));
			}
		}

		BufferResource.GeometryPtrs = AllGeom; //in future this should be sparse. SQ has fallback that relies on potentially all geom so can't do it yet
		ResourceLock.WriteUnlock();
*/
	}

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::GetActiveClusterIndex"), STAT_GetActiveClusterIndex, STATGROUP_Chaos);
	template<class T_FPBDRigidsEvolution, class T_FPBDCollisionConstraint>
	FPBDRigidParticleHandle* TPBDRigidClustering<T_FPBDRigidsEvolution, T_FPBDCollisionConstraint>::GetActiveClusterIndex(
		FPBDRigidParticleHandle* Child)
	{
		SCOPE_CYCLE_COUNTER(STAT_GetActiveClusterIndex);
		while (Child && Child->Disabled())
		{
			Child = Child->CastToClustered()->ClusterIds().Id;
		}
		return Child; 
	}

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::GenerateConnectionGraph"), STAT_GenerateConnectionGraph, STATGROUP_Chaos);
	template<class T_FPBDRigidsEvolution, class T_FPBDCollisionConstraint>
	void TPBDRigidClustering<T_FPBDRigidsEvolution, T_FPBDCollisionConstraint>::GenerateConnectionGraph(
		Chaos::FPBDRigidClusteredParticleHandle* Parent,
		const FClusterCreationParameters& Parameters)
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
			FClusterCreationParameters::EConnectionMethod LocalConnectionMethod = Parameters.ConnectionMethod;

			if (LocalConnectionMethod == FClusterCreationParameters::EConnectionMethod::None ||
				(LocalConnectionMethod == FClusterCreationParameters::EConnectionMethod::PointImplicit && 
				 !Parent->CollisionParticles()))
			{
				LocalConnectionMethod = FClusterCreationParameters::EConnectionMethod::MinimalSpanningSubsetDelaunayTriangulation; // default method
			}

			if (LocalConnectionMethod == FClusterCreationParameters::EConnectionMethod::PointImplicit ||
				LocalConnectionMethod == FClusterCreationParameters::EConnectionMethod::PointImplicitAugmentedWithMinimalDelaunay)
			{
				UpdateConnectivityGraphUsingPointImplicit(Parent, Parameters);
			}

			if (LocalConnectionMethod == FClusterCreationParameters::EConnectionMethod::DelaunayTriangulation)
			{
				UpdateConnectivityGraphUsingDelaunayTriangulation(Parent, Parameters); // not thread safe
			}

			if (LocalConnectionMethod == FClusterCreationParameters::EConnectionMethod::PointImplicitAugmentedWithMinimalDelaunay ||
				LocalConnectionMethod == FClusterCreationParameters::EConnectionMethod::MinimalSpanningSubsetDelaunayTriangulation)
			{
				FixConnectivityGraphUsingDelaunayTriangulation(Parent, Parameters);
			}
		}
	}

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::UpdateMassProperties"), STAT_UpdateMassProperties, STATGROUP_Chaos);
	template<class T_FPBDRigidsEvolution, class T_FPBDCollisionConstraint>
	void TPBDRigidClustering<T_FPBDRigidsEvolution, T_FPBDCollisionConstraint>::UpdateMassProperties(
		Chaos::FPBDRigidClusteredParticleHandle* Parent, 
		TSet<FPBDRigidParticleHandle*>& Children, 
		const FRigidTransform3* ForceMassOrientation)
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

	template<class T_FPBDRigidsEvolution, class T_FPBDCollisionConstraint>
	void TPBDRigidClustering<T_FPBDRigidsEvolution, T_FPBDCollisionConstraint>::UpdateGeometry(
		Chaos::FPBDRigidClusteredParticleHandle* Parent, 
		const TSet<FPBDRigidParticleHandle*>& Children, 
		TSharedPtr<Chaos::FImplicitObject, ESPMode::ThreadSafe> ProxyGeometry,
		const FClusterCreationParameters& Parameters)
	{
		SCOPE_CYCLE_COUNTER(STAT_UpdateGeometry);

		TArray<TUniquePtr<FImplicitObject>> Objects;
		TArray<TUniquePtr<FImplicitObject>> Objects2; //todo: find a better way to reuse this
		Objects.Reserve(Children.Num());
		Objects2.Reserve(Children.Num());

		//we should never update existing geometry since this is used by SQ threads.
		ensure(!Parent->Geometry());
		ensure(!Parent->DynamicGeometry());

		const FRigidTransform3 ClusterWorldTM(Parent->X(), Parent->R());

		TArray<FVec3> OriginalPoints;
		TArray<FPBDRigidParticleHandle*> GeomToOriginalParticlesHack;
		GeomToOriginalParticlesHack.Reserve(Children.Num());

		const bool bUseCollisionPoints = (ProxyGeometry || Parameters.bCopyCollisionParticles) && !Parameters.CollisionParticles;
		bool bUseParticleImplicit = false;
		bool bUsingMultiChildProxy = false;

		// Need to extract a filter off one of the cluster children 
		FCollisionFilterData Filter;
		for(FPBDRigidParticleHandle* Child : Children)
		{
			bool bFilterValid = false;
			for(const TUniquePtr<FPerShapeData>& Shape : Child->ShapesArray())
			{
				if(Shape)
				{
					Filter = Shape->GetSimData();
					bFilterValid = Filter.Word0 != 0 || Filter.Word1 != 0 || Filter.Word2 != 0 || Filter.Word3 != 0;
				}

				if(bFilterValid)
				{
					break;
				}
			}

			// Bail once we've found one
			if(bFilterValid)
			{
				break;
			}
		}

		{ // STAT_UpdateGeometry_GatherObjects
			SCOPE_CYCLE_COUNTER(STAT_UpdateGeometry_GatherObjects);

			if (bUseCollisionPoints)
			{
				uint32 NumPoints = 0;
				for (FPBDRigidParticleHandle* Child : Children)
				{
					NumPoints += Child->CollisionParticlesSize();
				}
				OriginalPoints.Reserve(NumPoints);
			}

			for (FPBDRigidParticleHandle* Child : Children)
			{
				const FRigidTransform3 ChildWorldTM(Child->X(), Child->R());
				FRigidTransform3 Frame = ChildWorldTM.GetRelativeTransform(ClusterWorldTM);
				FPBDRigidParticleHandle* UsedGeomChild = Child;
				if (Child->Geometry())
				{
					FPBDRigidClusteredParticleHandle* ClusteredChild = Child->CastToClustered();

					const FMultiChildProxyId MultiChildProxyId = 
						ClusteredChild ? ClusteredChild->MultiChildProxyId() : FMultiChildProxyId();
					const TUniquePtr<TMultiChildProxyData<FReal,3>>& MultiChildProxyData = ClusteredChild->MultiChildProxyData();

					if (UseLevelsetCollision || MultiChildProxyId.Id == nullptr || !MultiChildProxyData)
					{
						Objects.Add(TUniquePtr<FImplicitObject>(new TImplicitObjectTransformed<FReal, 3>(Child->Geometry(), Frame)));
						Objects2.Add(TUniquePtr<FImplicitObject>(new TImplicitObjectTransformed<FReal, 3>(Child->Geometry(), Frame)));
						GeomToOriginalParticlesHack.Add(Child);
					}
					else if (MultiChildProxyData->KeyChild == Child)
					{
						//using multi child proxy and this child is the key
						const FRigidTransform3 ProxyWorldTM = MultiChildProxyData->RelativeToKeyChild * ChildWorldTM;
						const FRigidTransform3 ProxyRelativeTM = ProxyWorldTM.GetRelativeTransform(ClusterWorldTM);
						Objects.Add(TUniquePtr<FImplicitObject>(new TImplicitObjectTransformed<FReal, 3>(MultiChildProxyId.Id->Geometry(), ProxyRelativeTM)));
						Objects2.Add(TUniquePtr<FImplicitObject>(new TImplicitObjectTransformed<FReal, 3>(MultiChildProxyId.Id->Geometry(), ProxyRelativeTM)));
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
					if (const TUniquePtr<FBVHParticles>& CollisionParticles = Child->CollisionParticles())
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

		TArray<FVec3> CleanedPoints;
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

				FImplicitObjectUnionClustered UnionObject(MoveTemp(Objects));
				FAABB3 Bounds = UnionObject.BoundingBox();
				const FVec3 BoundsExtents = Bounds.Extents();
				if (BoundsExtents.Min() >= MinLevelsetSize) //make sure the object is not too small
				{
					TVec3<int32> NumCells = Bounds.Extents() / MinLevelsetSize;
					for (int i = 0; i < 3; ++i)
					{
						NumCells[i] = FMath::Clamp(NumCells[i], MinLevelsetDimension, MaxLevelsetDimension);
					}

					FErrorReporter ErrorReporter;
					TUniformGrid<FReal, 3> Grid(Bounds.Min(), Bounds.Max(), NumCells, LevelsetGhostCells);
					TUniquePtr<FLevelSet> LevelSet(new FLevelSet(ErrorReporter, Grid, UnionObject));

					if (!Parameters.CollisionParticles)
					{
						const FReal MinDepthToSurface = Grid.Dx().Max();
						for (int32 Idx = CleanedPoints.Num() - 1; Idx >= 0; --Idx)
						{
							if (CleanedPoints.Num() > MinCleanedPointsBeforeRemovingInternals) //todo(ocohen): this whole thing should really be refactored
							{
								const FVec3& CleanedCollision = CleanedPoints[Idx];
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
						MakeUnique<TSphere<FReal, 3>>(FVec3(0), BoundsExtents.Size() * 0.5f));
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
				TUniquePtr<FBVHParticles>& CollisionParticles = Parent->CollisionParticles();
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

		if (TSerializablePtr<FImplicitObject> Implicit = Parent->Geometry())
		{
			// strange hacked initilization that seems misplaced and ill thought
			Parent->SetHasBounds(true);
			Parent->SetLocalBounds(Implicit->BoundingBox());
			const Chaos::FAABB3& LocalBounds = Parent->LocalBounds();
			const Chaos::FRigidTransform3 Xf(Parent->X(), Parent->R());
			const Chaos::FAABB3 TransformedBBox = LocalBounds.TransformedAABB(Xf);
			Parent->SetWorldSpaceInflatedBounds(TransformedBBox);
		}

		// Set the captured filter to our new shapes
		for(const TUniquePtr<FPerShapeData>& Shape : Parent->ShapesArray())
		{
			Shape->SetSimData(Filter);
		}
	}

	FRealSingle MinImpulseForStrainEval = 980 * 2 * 1.f / 30.f; //ignore impulses caused by just keeping object on ground. This is a total hack, we should not use accumulated impulse directly. Instead we need to look at delta v along constraint normal
	FAutoConsoleVariableRef CVarMinImpulseForStrainEval(TEXT("p.chaos.MinImpulseForStrainEval"), MinImpulseForStrainEval, TEXT("Minimum accumulated impulse before accumulating for strain eval "));

	bool bUseContactSpeedForStrainThreshold = true;
	FAutoConsoleVariableRef CVarUseContactSpeedForStrainEval(TEXT("p.chaos.UseContactSpeedForStrainEval"), bUseContactSpeedForStrainThreshold, TEXT("Whether to use contact speed to discard contacts when updating cluster strain (true: use speed, false: use impulse)"));

	FRealSingle MinContactSpeedForStrainEval = 1.0f; // Ignore contacts where the two bodies are resting together
	FAutoConsoleVariableRef CVarMinContactSpeedForStrainEval(TEXT("p.chaos.MinContactSpeedForStrainEval"), MinContactSpeedForStrainEval, TEXT("Minimum speed at the contact before accumulating for strain eval "));

	DECLARE_CYCLE_STAT(TEXT("ComputeStrainFromCollision"), STAT_ComputeStrainFromCollision, STATGROUP_Chaos);
	template<class T_FPBDRigidsEvolution, class T_FPBDCollisionConstraint>
	void TPBDRigidClustering<T_FPBDRigidsEvolution, T_FPBDCollisionConstraint>::ComputeStrainFromCollision(
		const T_FPBDCollisionConstraint& CollisionRule)
	{
		SCOPE_CYCLE_COUNTER(STAT_ComputeStrainFromCollision);
		FClusterMap& MParentToChildren = GetChildrenMap();

		ResetCollisionImpulseArray();

		for (const Chaos::FPBDCollisionConstraintHandle* ContactHandle : CollisionRule.GetConstConstraintHandles())
		{
			TVector<const FGeometryParticleHandle*, 2> ConstrainedParticles = ContactHandle->GetConstrainedParticles();
			const FPBDRigidParticleHandle* Rigid0 = ConstrainedParticles[0]->CastToRigidParticle();
			const FPBDRigidParticleHandle* Rigid1 = ConstrainedParticles[1]->CastToRigidParticle();

			if(bUseContactSpeedForStrainThreshold)
			{
				// Get dV between the two particles and project onto the normal to get the approach speed (take PreV as V is the new velocity post-solve)
				const FVec3 V0 = Rigid0 ? Rigid0->PreV() : FVec3(0);
				const FVec3 V1 = Rigid1 ? Rigid1->PreV() : FVec3(0);
				const FVec3 DeltaV = V0 - V1;
				const FReal SpeedAlongNormal = FVec3::DotProduct(DeltaV, ContactHandle->GetContact().GetNormal());

				// If we're not approaching at more than the min speed, reject the contact
				if(SpeedAlongNormal > -MinContactSpeedForStrainEval && ContactHandle->GetAccumulatedImpulse().SizeSquared() > FReal(0))
				{
					continue;
				}
			}
			else if(ContactHandle->GetAccumulatedImpulse().Size() < MinImpulseForStrainEval)
			{
				continue;
			}

			auto ComputeStrainLambda = [&](
				const FPBDRigidClusteredParticleHandle* Cluster, 
				const TArray<FPBDRigidParticleHandle*>& ParentToChildren)
			{
				const FRigidTransform3 WorldToClusterTM = FRigidTransform3(Cluster->P(), Cluster->Q());
				const FVec3 ContactLocationClusterLocal = WorldToClusterTM.InverseTransformPosition(ContactHandle->GetContactLocation());
				FAABB3 ContactBox(ContactLocationClusterLocal, ContactLocationClusterLocal);
				ContactBox.Thicken(ClusterDistanceThreshold);
				if (Cluster->ChildrenSpatial())
				{
					const TArray<FPBDRigidParticleHandle*> Intersections = Cluster->ChildrenSpatial()->FindAllIntersectingChildren(ContactBox);
					for (FPBDRigidParticleHandle* Child : Intersections)
					{
						if (TPBDRigidClusteredParticleHandle<FReal, 3>* ClusteredChild = Child->CastToClustered())
						{
							const TUniquePtr<TMultiChildProxyData<FReal, 3>>& ProxyData = ClusteredChild->MultiChildProxyData();
							const FPBDRigidParticleHandle* KeyChild = ProxyData ? ProxyData->KeyChild : nullptr;
							const FPBDRigidClusteredParticleHandle* ClusteredKeyChild = KeyChild ? KeyChild->CastToClustered() : nullptr;
							if (ClusteredKeyChild)
							{
								//multi child so get its children
								const FRigidTransform3 ProxyToCluster = ProxyData->RelativeToKeyChild * ClusteredKeyChild->ChildToParent();
								const FVec3 ContactLocationProxyLocal = ProxyToCluster.InverseTransformPosition(ContactLocationClusterLocal);
								FAABB3 ContactBoxProxy(ContactLocationProxyLocal, ContactLocationProxyLocal);
								ContactBoxProxy.Thicken(ClusterDistanceThreshold);
								if (ClusteredChild->ChildrenSpatial())
								{
									const TArray<FPBDRigidParticleHandle*> SubIntersections = 
										ClusteredChild->ChildrenSpatial()->FindAllIntersectingChildren(ContactBoxProxy);
									for (FPBDRigidParticleHandle* SubChild : SubIntersections)
									{
										if (FPBDRigidClusteredParticleHandle* ClusteredSubChild = SubChild->CastToClustered())
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

			if (const TArray<FPBDRigidParticleHandle*>* ChildrenPtr = MParentToChildren.Find(ConstrainedParticles[0]->CastToRigidParticle()))
			{
				ComputeStrainLambda(ConstrainedParticles[0]->CastToClustered(), *ChildrenPtr);
			}

			if (const TArray<FPBDRigidParticleHandle*>* ChildrenPtr = MParentToChildren.Find(ConstrainedParticles[1]->CastToRigidParticle()))
			{
				ComputeStrainLambda(ConstrainedParticles[1]->CastToClustered(), *ChildrenPtr);
			}

			MCollisionImpulseArrayDirty = true;
		}
	}

	DECLARE_CYCLE_STAT(TEXT("ResetCollisionImpulseArray"), STAT_ResetCollisionImpulseArray, STATGROUP_Chaos);
	template<class T_FPBDRigidsEvolution, class T_FPBDCollisionConstraint>
	void TPBDRigidClustering<T_FPBDRigidsEvolution, T_FPBDCollisionConstraint>::ResetCollisionImpulseArray()
	{
		SCOPE_CYCLE_COUNTER(STAT_ResetCollisionImpulseArray);
		if (MCollisionImpulseArrayDirty)
		{
			FPBDRigidsSOAs& ParticleStructures = MEvolution.GetParticles();
			ParticleStructures.GetGeometryCollectionParticles().CollisionImpulsesArray().Fill(0.0f);
			ParticleStructures.GetClusteredParticles().CollisionImpulsesArray().Fill(0.0f);
			MCollisionImpulseArrayDirty = false;
		}
	}

	template<class T_FPBDRigidsEvolution, class T_FPBDCollisionConstraint>
	void TPBDRigidClustering<T_FPBDRigidsEvolution, T_FPBDCollisionConstraint>::DisableCluster(
		FPBDRigidClusteredParticleHandle* ClusteredParticle)
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

	template<class T_FPBDRigidsEvolution, class T_FPBDCollisionConstraint>
	void TPBDRigidClustering<T_FPBDRigidsEvolution, T_FPBDCollisionConstraint>::DisableParticleWithBreakEvent(
		Chaos::FPBDRigidParticleHandle* Particle)
	{
		DisableCluster(Particle->CastToClustered());

		if (DoGenerateBreakingData)
		{
			const int32 NewIdx = MAllClusterBreakings.Add(FBreakingData());
			FBreakingData& ClusterBreak = MAllClusterBreakings[NewIdx];
			ClusterBreak.Particle = Particle;
			ClusterBreak.ParticleProxy = nullptr;
			ClusterBreak.Location = Particle->X();
			ClusterBreak.Velocity = Particle->V();
			ClusterBreak.AngularVelocity = Particle->W();
			ClusterBreak.Mass = Particle->M();
		}
	}

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::UpdateConnectivityGraphUsingPointImplicit"), STAT_UpdateConnectivityGraphUsingPointImplicit, STATGROUP_Chaos);
	template<class T_FPBDRigidsEvolution, class T_FPBDCollisionConstraint>
	void TPBDRigidClustering<T_FPBDRigidsEvolution, T_FPBDCollisionConstraint>::UpdateConnectivityGraphUsingPointImplicit(
		Chaos::FPBDRigidClusteredParticleHandle* Parent,
		const FClusterCreationParameters& Parameters)
	{
		SCOPE_CYCLE_COUNTER(STAT_UpdateConnectivityGraphUsingPointImplicit);

		if (!UseConnectivity)
		{
			return;
		}

		const FReal Delta = FMath::Min(FMath::Max(Parameters.CoillisionThicknessPercent, FReal(0)), FReal(1));
		const TArray<FPBDRigidParticleHandle*>& Children = MChildren[Parent];
		for (int32 i = 0; i < Children.Num(); ++i)
		{
			FPBDRigidParticleHandle* Child1 = Children[i];
			if (!Child1->Geometry() || !Child1->Geometry()->HasBoundingBox())
			{
				continue;
			}
			const FVec3& Child1X = Child1->X();
			if (!(ensure(!FMath::IsNaN(Child1X[0])) && ensure(!FMath::IsNaN(Child1X[1])) && ensure(!FMath::IsNaN(Child1X[2]))))
			{
				continue;
			}
			FRigidTransform3 TM1 = FRigidTransform3(Child1X, Child1->R());

			const int32 Offset = i + 1;
			const int32 NumRemainingChildren = Children.Num() - Offset;
			typedef TPair<FPBDRigidParticleHandle*, FPBDRigidParticleHandle*> ParticlePair;
			typedef TArray<ParticlePair> ParticlePairArray;
			TArray<ParticlePairArray> Connections;
			Connections.Init(ParticlePairArray(), NumRemainingChildren);
			PhysicsParallelFor(NumRemainingChildren, [&](int32 Idx) 
			{
				const int32 ChildrenIdx = Offset + Idx;
				FPBDRigidParticleHandle* Child2 = Children[ChildrenIdx];
				if(!Child2->CollisionParticles())
					return;

				const FVec3& Child2X = Child2->X();
				if (!(ensure(!FMath::IsNaN(Child2X[0])) && ensure(!FMath::IsNaN(Child2X[1])) && ensure(!FMath::IsNaN(Child2X[2]))))
					return;

				const FRigidTransform3 TM = TM1.GetRelativeTransform(FRigidTransform3(Child2X, Child2->R()));

				bool bCollided = false;
				for (uint32 CollisionIdx = 0; !bCollided && CollisionIdx < Child2->CollisionParticles()->Size(); ++CollisionIdx)
				{
					const FVec3 LocalPoint = 
						TM.TransformPositionNoScale(Child2->CollisionParticles()->X(CollisionIdx));
					const FReal Phi = Child1->Geometry()->SignedDistance(LocalPoint - (LocalPoint * Delta));
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
	template<class T_FPBDRigidsEvolution, class T_FPBDCollisionConstraint>
	void TPBDRigidClustering<T_FPBDRigidsEvolution, T_FPBDCollisionConstraint>::FixConnectivityGraphUsingDelaunayTriangulation(
		Chaos::FPBDRigidClusteredParticleHandle* Parent,
		const FClusterCreationParameters& Parameters)
	{
		// @todo(investigate) : This is trying to set multiple connections and throwing a warning in ConnectNodes
		SCOPE_CYCLE_COUNTER(STAT_FixConnectivityGraphUsingDelaunayTriangulation);

		const TArray<FPBDRigidParticleHandle*>& Children = MChildren[Parent];

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
			FPBDRigidParticleHandle* GroupId;
			int32 Size;
		};
		TMap<FPBDRigidParticleHandle*, UnionFindInfo> UnionInfo;
		UnionInfo.Reserve(Children.Num());

		// Initialize UnionInfo:
		//		0: GroupId = Children[0], Size = 1
		//		1: GroupId = Children[1], Size = 1
		//		2: GroupId = Children[2], Size = 1
		//		3: GroupId = Children[3], Size = 1

		for(FPBDRigidParticleHandle* Child : Children)
		{
			UnionInfo.Add(Child, { Child, 1 }); // GroupId, Size
		}

		auto FindGroup = [&](FPBDRigidParticleHandle* Id) 
		{
			FPBDRigidParticleHandle* GroupId = Id;
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

		auto MergeGroup = [&](FPBDRigidParticleHandle* A, FPBDRigidParticleHandle* B) 
		{
			FPBDRigidParticleHandle* GroupA = FindGroup(A);
			FPBDRigidParticleHandle* GroupB = FindGroup(B);
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
			FPBDRigidParticleHandle* Child = Children[i];
			const TArray<TConnectivityEdge<FReal>>& Edges = Child->CastToClustered()->ConnectivityEdges();
			for (const TConnectivityEdge<FReal>& Edge : Edges)
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
			FPBDRigidParticleHandle* A;
			FPBDRigidParticleHandle* B;
			FReal DistSq;
		};
		TArray<LinkCandidate> Candidates;
		Candidates.Reserve(Neighbors.Num());

		const FReal AlwaysAcceptBelowDistSqThreshold = 50.f*50.f*100.f*MClusterConnectionFactor;
		for (int32 i = 0; i < Neighbors.Num(); i++)
		{
			FPBDRigidParticleHandle* Child1 = Children[i];
			const TArray<int32>& Child1Neighbors = Neighbors[i];
			for (const int32 Nbr : Child1Neighbors)
			{
				if (Nbr < i)
				{ // assume we'll get the symmetric connection; don't bother considering this one
					continue;
				}
				FPBDRigidParticleHandle* Child2 = Children[Nbr];

				const FReal DistSq = FVector::DistSquared(Pts[i], Pts[Nbr]);
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
			FPBDRigidParticleHandle* Child1 = Candidate.A;
			FPBDRigidParticleHandle* Child2 = Candidate.B;
			if (FindGroup(Child1) != FindGroup(Child2))
			{
				MergeGroup(Child1, Child2);
				ConnectNodes(Child1, Child2);
			}
		}
	}

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::UpdateConnectivityGraphUsingDelaunayTriangulation"), STAT_UpdateConnectivityGraphUsingDelaunayTriangulation, STATGROUP_Chaos);

	template<class T_FPBDRigidsEvolution, class T_FPBDCollisionConstraint>
	void TPBDRigidClustering<T_FPBDRigidsEvolution, T_FPBDCollisionConstraint>::UpdateConnectivityGraphUsingDelaunayTriangulation(Chaos::FPBDRigidClusteredParticleHandle* Parent, const FClusterCreationParameters& Parameters)
	{
		SCOPE_CYCLE_COUNTER(STAT_UpdateConnectivityGraphUsingDelaunayTriangulation);

		const TArray<FPBDRigidParticleHandle*>& Children = MChildren[Parent];

		TArray<FVector> Pts;
		Pts.AddUninitialized(Children.Num());
		for (int32 i = 0; i < Children.Num(); ++i)
		{
			Pts[i] = Children[i]->X();
		}
		TArray<TArray<int>> Neighbors;
		VoronoiNeighbors(Pts, Neighbors);

		TSet<TPair<FPBDRigidParticleHandle*, FPBDRigidParticleHandle*>> UniqueEdges;
		for (int32 i = 0; i < Neighbors.Num(); i++)
		{
			for (int32 j = 0; j < Neighbors[i].Num(); j++)
			{
				FPBDRigidParticleHandle* Child1 = Children[i];
				FPBDRigidParticleHandle* Child2 = Children[Neighbors[i][j]];
				const bool bFirstSmaller = Child1 < Child2;
				TPair<FPBDRigidParticleHandle*, FPBDRigidParticleHandle*> SortedPair(
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

	//template<class T_FPBDRigidsEvolution, class T_FPBDCollisionConstraint>
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

	template<class T_FPBDRigidsEvolution, class T_FPBDCollisionConstraint>
	void TPBDRigidClustering<T_FPBDRigidsEvolution, T_FPBDCollisionConstraint>::ConnectNodes(
		FPBDRigidParticleHandle* Child1,
		FPBDRigidParticleHandle* Child2)
	{
		check(Child1 != Child2);
		FPBDRigidClusteredParticleHandle* ClusteredChild1 = Child1->CastToClustered();
		FPBDRigidClusteredParticleHandle* ClusteredChild2 = Child2->CastToClustered();
		ConnectNodes(ClusteredChild1, ClusteredChild2);
	}

	template<class T_FPBDRigidsEvolution, class T_FPBDCollisionConstraint>
	void TPBDRigidClustering<T_FPBDRigidsEvolution, T_FPBDCollisionConstraint>::ConnectNodes(
		FPBDRigidClusteredParticleHandle* ClusteredChild1,
		FPBDRigidClusteredParticleHandle* ClusteredChild2)
	{
		check(ClusteredChild1 && ClusteredChild2);
		if (ClusteredChild1 == ClusteredChild2)
			return;
		const FReal AvgStrain = (ClusteredChild1->Strains() + ClusteredChild2->Strains()) * (FReal)0.5;
		TArray<TConnectivityEdge<FReal>>& Edges1 = ClusteredChild1->ConnectivityEdges();
		TArray<TConnectivityEdge<FReal>>& Edges2 = ClusteredChild2->ConnectivityEdges();
		if (//Edges1.Num() < Parameters.MaxNumConnections && 
			!Edges1.FindByKey(ClusteredChild2))
		{
			Edges1.Add(TConnectivityEdge<FReal>(ClusteredChild2, AvgStrain));
		}
		if (//Edges2.Num() < Parameters.MaxNumConnections && 
			!Edges2.FindByKey(ClusteredChild1))
		{
			Edges2.Add(TConnectivityEdge<FReal>(ClusteredChild1, AvgStrain));
		}
	}

	template<class T_FPBDRigidsEvolution, class T_FPBDCollisionConstraint>
	void TPBDRigidClustering<T_FPBDRigidsEvolution, T_FPBDCollisionConstraint>::RemoveNodeConnections(
		FPBDRigidParticleHandle* Child)
	{
		RemoveNodeConnections(Child->CastToClustered());
	}

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::RemoveNodeConnections"), STAT_RemoveNodeConnections, STATGROUP_Chaos);
	template<class T_FPBDRigidsEvolution, class T_FPBDCollisionConstraint>
	void TPBDRigidClustering<T_FPBDRigidsEvolution, T_FPBDCollisionConstraint>::RemoveNodeConnections(
		FPBDRigidClusteredParticleHandle* ClusteredChild)
	{
		SCOPE_CYCLE_COUNTER(STAT_RemoveNodeConnections);
		check(ClusteredChild);
		TArray<TConnectivityEdge<FReal>>& Edges = ClusteredChild->ConnectivityEdges();
		for (TConnectivityEdge<FReal>& Edge : Edges)
		{
			TArray<TConnectivityEdge<FReal>>& OtherEdges = Edge.Sibling->CastToClustered()->ConnectivityEdges();
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

template class CHAOS_API Chaos::TPBDRigidClustering<Chaos::FPBDRigidsEvolutionGBF, FPBDCollisionConstraints>;

