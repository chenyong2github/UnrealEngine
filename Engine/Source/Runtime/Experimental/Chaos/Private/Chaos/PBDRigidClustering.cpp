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
#include "Chaos/PBDRigidClusteringAlgo.h"
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
#include "PhysicsProxy/GeometryCollectionPhysicsProxy.h"
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

	int32 ComputeClusterCollisionStrains = 1;
	FAutoConsoleVariableRef CVarComputeClusterCollisionStrains(TEXT("p.ComputeClusterCollisionStrains"), ComputeClusterCollisionStrains, TEXT("Whether to use collision constraints when processing clustering."));

	int32 DeactivateClusterChildren = 0;
	FAutoConsoleVariableRef CVarDeactivateClusterChildren(TEXT("p.DeactivateClusterChildren"), DeactivateClusterChildren, TEXT("If children should be decativated when broken and put into another cluster."));


	//==========================================================================
	// TPBDRigidClustering
	//==========================================================================

	FRigidClustering::FRigidClustering(FPBDRigidsEvolution& InEvolution, FPBDRigidClusteredParticles& InParticles)
		: MEvolution(InEvolution)
		, MParticles(InParticles)
		, MCollisionImpulseArrayDirty(true)
		, DoGenerateBreakingData(false)
		, MClusterConnectionFactor(1.0)
		, MClusterUnionConnectionType(FClusterCreationParameters::EConnectionMethod::DelaunayTriangulation)
	{}

	FRigidClustering::~FRigidClustering()
	{}

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::CreateClusterParticle"), STAT_CreateClusterParticle, STATGROUP_Chaos);
	Chaos::FPBDRigidClusteredParticleHandle* FRigidClustering::CreateClusterParticle(
		const int32 ClusterGroupIndex, 
		TArray<Chaos::FPBDRigidParticleHandle*>&& Children, 
		const FClusterCreationParameters& Parameters, 
		TSharedPtr<Chaos::FImplicitObject, ESPMode::ThreadSafe> ProxyGeometry, 
		const FRigidTransform3* ForceMassOrientation, 
		const FUniqueIdx* ExistingIndex)
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
			NewParticle->Strains() /= static_cast<FReal>(ChildrenSet.Num());
		}

		ensureMsgf(!ProxyGeometry || ForceMassOrientation, TEXT("If ProxyGeometry is passed, we must override the mass orientation as they are tied"));

		// TODO: This needs to be rotated to diagonal, used to update I()/InvI() from diagonal, and update transform with rotation.
		FMatrix33 ClusterInertia(0);
		UpdateClusterMassProperties(NewParticle, ChildrenSet, ClusterInertia, ForceMassOrientation);
		UpdateKinematicProperties(NewParticle, MChildren, MEvolution);
		UpdateGeometry(NewParticle, ChildrenSet, ProxyGeometry, Parameters);
		GenerateConnectionGraph(NewParticle, Parameters);

		NewParticle->SetSleeping(bClusterIsAsleep);

		auto AddToClusterUnion = [&](int32 ClusterID, FPBDRigidClusteredParticleHandle* Handle)
		{
			if (ClusterID <= 0)
			{
				return;
			}

			if (!ClusterUnionMap.Contains(ClusterID))
			{
				ClusterUnionMap.Add(ClusterID, TArray<FPBDRigidClusteredParticleHandle*>());
			}

			ClusterUnionMap[ClusterID].Add(Handle);
		};

		if(ClusterGroupIndex)
		{
			AddToClusterUnion(ClusterGroupIndex, NewParticle);
		}

		return NewParticle;
	}

	int32 UnionsHaveCollisionParticles = 0;
	FAutoConsoleVariableRef CVarUnionsHaveCollisionParticles(TEXT("p.UnionsHaveCollisionParticles"), UnionsHaveCollisionParticles, TEXT(""));

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::CreateClusterParticleFromClusterChildren"), STAT_CreateClusterParticleFromClusterChildren, STATGROUP_Chaos);
	Chaos::FPBDRigidClusteredParticleHandle* 
	FRigidClustering::CreateClusterParticleFromClusterChildren(
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
		NewParticle->SetClusterId(ClusterId(nullptr, Children.Num()));
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
		
		// TODO: This needs to be rotated to diagonal, used to update I()/InvI() from diagonal, and update transform with rotation.
		FMatrix33 ClusterInertia(0);
		UpdateClusterMassProperties(NewParticle, ChildrenSet, ClusterInertia, nullptr);
		UpdateKinematicProperties(NewParticle, MChildren, MEvolution);

		UpdateGeometry(NewParticle, ChildrenSet, nullptr, NoCleanParams);

		return NewParticle;
	}

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::UnionClusterGroups"), STAT_UnionClusterGroups, STATGROUP_Chaos);
	void 
	FRigidClustering::UnionClusterGroups()
	{
		SCOPE_CYCLE_COUNTER(STAT_UnionClusterGroups);

		if(ClusterUnionMap.Num())
		{
			struct FClusterGroup {
				TArray<FPBDRigidParticleHandle*> Bodies = TArray < FPBDRigidParticleHandle*>();
				bool bIsSleeping = true;
			};


			TMap<FPBDRigidParticleHandle*, FPBDRigidParticleHandle*> ChildToParentMap;
			TMap<int32, FClusterGroup> NewClusterGroups;

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
						NewClusterGroups.Add(ClusterGroupID, FClusterGroup());
					}

					bool bIsSleeping = true;
					TArray<FPBDRigidParticleHandle*> ClusterBodies;
					for(FPBDRigidClusteredParticleHandle* ActiveCluster : Handles)
					{
						if(!ActiveCluster->Disabled())
						{
							// If this is an external cluster (from the rest collection) we release its children and append them to the current group
							TSet<FPBDRigidParticleHandle*> Children;
							
							// let sleeping clusters stay asleep
							bIsSleeping &= ActiveCluster->ObjectState() == EObjectStateType::Sleeping;

							{
								// First disable breaking data generation - this is not a break we're just reclustering under a dynamic parent.
								TGuardValue<bool> BreakFlagGuard(DoGenerateBreakingData, false);
								Children = ReleaseClusterParticles(ActiveCluster, nullptr, true);
							}

							NewClusterGroups[ClusterGroupID].Bodies.Append(Children.Array());
							
							for(FPBDRigidParticleHandle* Child : Children)
							{
								ChildToParentMap.Add(Child, ActiveCluster);
							}
						}
					}
					NewClusterGroups[ClusterGroupID].bIsSleeping = bIsSleeping;
				}
			}

			// For new cluster groups, create an internal cluster parent.
			for(TTuple<int32, FClusterGroup>& Group : NewClusterGroups)
			{
				int32 ClusterGroupID = FMath::Abs(Group.Key);

				TArray<FPBDRigidParticleHandle*> ActiveCluster = Group.Value.Bodies;

				FClusterCreationParameters Parameters(0.3f, 100, false, !!UnionsHaveCollisionParticles);
				Parameters.ConnectionMethod = MClusterUnionConnectionType;
				TPBDRigidClusteredParticleHandleImp<FReal, 3, true>* Handle = 
					CreateClusterParticle(-ClusterGroupID, MoveTemp(Group.Value.Bodies), Parameters, 
						TSharedPtr<FImplicitObject, ESPMode::ThreadSafe>());
				Handle->SetInternalCluster(true);

				if (Group.Value.bIsSleeping)
				{
					MEvolution.SetParticleObjectState(Handle, Chaos::EObjectStateType::Sleeping);
				}

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
	TSet<FPBDRigidParticleHandle*> 
	FRigidClustering::DeactivateClusterParticle(
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
	TSet<FPBDRigidParticleHandle*> 
	FRigidClustering::ReleaseClusterParticles(
		FPBDRigidClusteredParticleHandle* ClusteredParticle,
		const TMap<FGeometryParticleHandle*, Chaos::FReal>* ExternalStrainMap,
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
		const FRigidTransform3 PreSolveTM = FRigidTransform3(ClusteredParticle->P(), ClusteredParticle->Q());

		//@todo(ocohen): iterate with all the potential parents at once?
		//find all children within some distance of contact point

		auto RemoveChildLambda = [&](FPBDRigidParticleHandle* Child/*, const int32 Idx*/)
		{
			FPBDRigidClusteredParticleHandle* ClusteredChild = Child->CastToClustered();

			MEvolution.EnableParticle(Child, ClusteredParticle);
			TopLevelClusterParents.Add(ClusteredChild);

			ClusteredChild->SetClusterId(ClusterId(nullptr, ClusteredChild->ClusterIds().NumChildren)); // clear Id but retain number of children

			const FRigidTransform3 ChildFrame = ClusteredChild->ChildToParent() * PreSolveTM;
			Child->SetX(ChildFrame.GetTranslation());
			Child->SetR(ChildFrame.GetRotation());

			Child->SetP(Child->X());
			Child->SetQ(Child->R());

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
						ClusterBreak.Proxy = Child->PhysicsProxy();
						ClusterBreak.Location = Child->X();
						ClusterBreak.Velocity = Child->V();
						ClusterBreak.AngularVelocity = Child->W();
						ClusterBreak.Mass = Child->M();
						if (Child->Geometry() && Child->Geometry()->HasBoundingBox())
						{
							ClusterBreak.BoundingBox = Child->Geometry()->BoundingBox();
						}
						if (ClusterBreak.Proxy->GetType() == EPhysicsProxyType::GeometryCollectionType)
						{
							FGeometryCollectionPhysicsProxy* ConcreteProxy = static_cast<FGeometryCollectionPhysicsProxy*>(ClusterBreak.Proxy);
							ClusterBreak.TransformGroupIndex = ConcreteProxy->GetTransformGroupIndexFromHandle(Child);
						}
						else
						{
							ClusterBreak.TransformGroupIndex = INDEX_NONE;
						}
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
						if (ConnectedPieces.Num() == 1) //need to break single pieces first
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
				UpdateKinematicProperties(Child, MChildren, MEvolution);
			}

			//disable cluster
			DisableCluster(ClusteredParticle);
		} // bChildrenChanged

		return ActivatedChildren;
	}



	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::ReleaseClusterParticlesNoInternalCluster"), STAT_ReleaseClusterParticlesNoInternalCluster, STATGROUP_Chaos);
	TSet<FPBDRigidParticleHandle*>
	FRigidClustering::ReleaseClusterParticlesNoInternalCluster(
		FPBDRigidClusteredParticleHandle* ClusteredParticle,
		const TMap<FGeometryParticleHandle*, Chaos::FReal>* ExternalStrainMap,
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
		const FRigidTransform3 PreSolveTM = FRigidTransform3(ClusteredParticle->P(), ClusteredParticle->Q());

		//@todo(ocohen): iterate with all the potential parents at once?
		//find all children within some distance of contact point

		auto RemoveChildLambda = [&](FPBDRigidParticleHandle* Child/*, const int32 Idx*/)
		{
			FPBDRigidClusteredParticleHandle* ClusteredChild = Child->CastToClustered();

			MEvolution.EnableParticle(Child, ClusteredParticle);
			TopLevelClusterParents.Add(ClusteredChild);

			ClusteredChild->SetClusterId(ClusterId(nullptr, ClusteredChild->ClusterIds().NumChildren)); // clear Id but retain number of children

			const FRigidTransform3 ChildFrame = ClusteredChild->ChildToParent() * PreSolveTM;
			Child->SetX(ChildFrame.GetTranslation());
			Child->SetR(ChildFrame.GetRotation());

			Child->SetP(Child->X());
			Child->SetQ(Child->R());

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
						ClusterBreak.Proxy = Child->PhysicsProxy();
						ClusterBreak.Location = Child->X();
						ClusterBreak.Velocity = Child->V();
						ClusterBreak.AngularVelocity = Child->W();
						ClusterBreak.Mass = Child->M();
						if (Child->Geometry() && Child->Geometry()->HasBoundingBox())
						{
							ClusterBreak.BoundingBox = Child->Geometry()->BoundingBox();
						}
						if (ClusterBreak.Proxy && ClusterBreak.Proxy->GetType() == EPhysicsProxyType::GeometryCollectionType)
						{
							FGeometryCollectionPhysicsProxy* ConcreteProxy = static_cast<FGeometryCollectionPhysicsProxy*>(ClusterBreak.Proxy);
							ClusterBreak.TransformGroupIndex = ConcreteProxy->GetTransformGroupIndexFromHandle(Child);
						}
						else
						{
							ClusterBreak.TransformGroupIndex = INDEX_NONE;
						}
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
						if (ConnectedPieces.Num() == 1) //need to break single pieces first
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
				UpdateKinematicProperties(Child, MChildren, MEvolution);
			}

			//disable cluster
			DisableCluster(ClusteredParticle);
		} // bChildrenChanged

		return ActivatedChildren;
	}




	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::ReleaseClusterParticles(LIST)"), STAT_ReleaseClusterParticles_LIST, STATGROUP_Chaos);
	TSet<FPBDRigidParticleHandle*> 
	FRigidClustering::ReleaseClusterParticles(
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
	void 
	FRigidClustering::AdvanceClustering(
		const FReal Dt, 
		FPBDCollisionConstraints& CollisionRule)
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
				for (const auto& ActiveCluster : GetTopLevelClusterParents())
				{
					if (!ActiveCluster->Disabled())
					{
						if (ActiveCluster->ClusterIds().NumChildren > 0) //active index is a cluster
						{
							TArray<FRigidHandle>& ParentToChildren = MChildren[ActiveCluster];
							for (FRigidHandle Child : ParentToChildren)
							{
								if (FClusterHandle ClusteredChild = Child->CastToClustered())
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
			}

			if (MCollisionImpulseArrayDirty)
			{
				SCOPE_CYCLE_COUNTER(STAT_UpdateDirtyImpulses);
				TMap<FPBDRigidClusteredParticleHandle*, TSet<FPBDRigidParticleHandle*>> ClusterToActivatedChildren = 
					BreakingModel();
			} // end if MCollisionImpulseArrayDirty

		} // end if MParticles.Size()
		Timer.Stop();
		UE_LOG(LogChaos, Verbose, TEXT("Cluster Break Update Time is %f"), Time);
	}

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::BreakingModel()"), STAT_BreakingModel, STATGROUP_Chaos);
	TMap<FPBDRigidClusteredParticleHandle*, TSet<FPBDRigidParticleHandle*>> 
	FRigidClustering::BreakingModel(
		TMap<FGeometryParticleHandle*, Chaos::FReal>* ExternalStrainMap)
	{
		SCOPE_CYCLE_COUNTER(STAT_BreakingModel);

		//make copy because release cluster modifies active indices. We want to iterate over original active indices
		TArray<FPBDRigidClusteredParticleHandle*> ClusteredParticlesToProcess;
		for (auto& Particle : MEvolution.GetNonDisabledClusteredView())
		{
			ClusteredParticlesToProcess.Add(Particle.Handle()->CastToClustered());
		}

		TMap<FPBDRigidClusteredParticleHandle*, TSet<FPBDRigidParticleHandle*>> AllActivatedChildren;

		for (FPBDRigidClusteredParticleHandle* ClusteredParticle : ClusteredParticlesToProcess)
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
							ClusterBreak.Proxy = ClusteredParticle->PhysicsProxy();
							ClusterBreak.Location = ClusteredParticle->X();
							ClusterBreak.Velocity = ClusteredParticle->V();
							ClusterBreak.AngularVelocity = ClusteredParticle->W();
							ClusterBreak.Mass = ClusteredParticle->M();
							if (ClusteredParticle->Geometry()->HasBoundingBox())
							{
								ClusterBreak.BoundingBox = ClusteredParticle->Geometry()->BoundingBox();
							}
							if (ClusterBreak.Proxy->GetType() == EPhysicsProxyType::GeometryCollectionType)
							{
								FGeometryCollectionPhysicsProxy* ConcreteProxy = static_cast<FGeometryCollectionPhysicsProxy*>(ClusterBreak.Proxy);
								ClusterBreak.TransformGroupIndex = ConcreteProxy->GetTransformGroupIndexFromHandle(ClusteredParticle);
							}
							else
							{
								ClusterBreak.TransformGroupIndex = INDEX_NONE;
							}
						}
					}
				}
			}
		}

		return AllActivatedChildren;
	}


	DECLARE_CYCLE_STAT(TEXT("FRigidClustering::Visitor"), STAT_ClusterVisitor, STATGROUP_Chaos);
	void FRigidClustering::Visitor(FClusterHandle Cluster, FVisitorFunction Function)
	{
		if (Cluster)
		{
			if (MChildren.Contains(Cluster) && MChildren[Cluster].Num())
			{
				SCOPE_CYCLE_COUNTER(STAT_ClusterVisitor);

				// TQueue is a linked list, which has no preallocator.
				TQueue<FRigidHandle> Queue;
				for (Chaos::FPBDRigidParticleHandle* Child : MChildren[Cluster])
				{
					Queue.Enqueue(Child);
				}

				FRigidHandle CurrentHandle = nullptr;
				while (Queue.Dequeue(CurrentHandle))
				{
					if (CurrentHandle)
					{
						if (FClusterHandle CurrentClusterHandle = CurrentHandle->CastToClustered())
						{
							// @question : Maybe we should just store the leaf node bodies in a
							// map, that will require Memory(n*log(n))
							if (MChildren.Contains(CurrentClusterHandle))
							{
								for (Chaos::FPBDRigidParticleHandle* Child : MChildren[CurrentClusterHandle])
								{
									Queue.Enqueue(Child);
								}
							}
						}
						if (CurrentHandle)
						{
							Function(*this, CurrentHandle);
						}
					}
				}
			}
		}
	}

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::GetActiveClusterIndex"), STAT_GetActiveClusterIndex, STATGROUP_Chaos);
	FPBDRigidParticleHandle* 
	FRigidClustering::GetActiveClusterIndex(
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
	void 
	FRigidClustering::GenerateConnectionGraph(
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

	FRealSingle MinImpulseForStrainEval = 980 * 2 * 1.f / 30.f; //ignore impulses caused by just keeping object on ground. This is a total hack, we should not use accumulated impulse directly. Instead we need to look at delta v along constraint normal
	FAutoConsoleVariableRef CVarMinImpulseForStrainEval(TEXT("p.chaos.MinImpulseForStrainEval"), MinImpulseForStrainEval, TEXT("Minimum accumulated impulse before accumulating for strain eval "));

	bool bUseContactSpeedForStrainThreshold = true;
	FAutoConsoleVariableRef CVarUseContactSpeedForStrainEval(TEXT("p.chaos.UseContactSpeedForStrainEval"), bUseContactSpeedForStrainThreshold, TEXT("Whether to use contact speed to discard contacts when updating cluster strain (true: use speed, false: use impulse)"));

	FRealSingle MinContactSpeedForStrainEval = 1.0f; // Ignore contacts where the two bodies are resting together
	FAutoConsoleVariableRef CVarMinContactSpeedForStrainEval(TEXT("p.chaos.MinContactSpeedForStrainEval"), MinContactSpeedForStrainEval, TEXT("Minimum speed at the contact before accumulating for strain eval "));

	DECLARE_CYCLE_STAT(TEXT("ComputeStrainFromCollision"), STAT_ComputeStrainFromCollision, STATGROUP_Chaos);
	void 
	FRigidClustering::ComputeStrainFromCollision(
		const FPBDCollisionConstraints& CollisionRule)
	{
		SCOPE_CYCLE_COUNTER(STAT_ComputeStrainFromCollision);
		FClusterMap& MParentToChildren = GetChildrenMap();

		ResetCollisionImpulseArray();

		for (const Chaos::FPBDCollisionConstraintHandle* ContactHandle : CollisionRule.GetConstConstraintHandles())
		{
			TVector<const FGeometryParticleHandle*, 2> ConstrainedParticles = ContactHandle->GetConstrainedParticles();
			
			// make sure we only compute things if one of the two particle is clustered
			const FPBDRigidClusteredParticleHandle* ClusteredConstrainedParticles0 = ConstrainedParticles[0]->CastToClustered();
			const FPBDRigidClusteredParticleHandle* ClusteredConstrainedParticles1 = ConstrainedParticles[1]->CastToClustered();
			if (!ClusteredConstrainedParticles0 && !ClusteredConstrainedParticles1)
			{
				continue;
			}

			const FPBDRigidParticleHandle* Rigid0 = ConstrainedParticles[0]->CastToRigidParticle();
			const FPBDRigidParticleHandle* Rigid1 = ConstrainedParticles[1]->CastToRigidParticle();

			if (bUseContactSpeedForStrainThreshold)
			{
				// Get dV between the two particles and project onto the normal to get the approach speed (take PreV as V is the new velocity post-solve)
				const FVec3 V0 = Rigid0 ? Rigid0->PreV() : FVec3(0);
				const FVec3 V1 = Rigid1 ? Rigid1->PreV() : FVec3(0);
				const FVec3 DeltaV = V0 - V1;
				const FReal SpeedAlongNormal = FVec3::DotProduct(DeltaV, ContactHandle->GetContact().CalculateWorldContactNormal());

				// If we're not approaching at more than the min speed, reject the contact
				if (SpeedAlongNormal > -MinContactSpeedForStrainEval && ContactHandle->GetAccumulatedImpulse().SizeSquared() > FReal(0))
				{
					continue;
				}
			}
			else if (ContactHandle->GetAccumulatedImpulse().Size() < MinImpulseForStrainEval)
			{
				continue;
			}

			auto ComputeStrainLambda = [&](
				const FPBDRigidClusteredParticleHandle* Cluster,
				const TArray<FPBDRigidParticleHandle*>& ParentToChildren)
			{
				const FRigidTransform3 WorldToClusterTM = FRigidTransform3(Cluster->P(), Cluster->Q());
				const FVec3 ContactLocationClusterLocal = WorldToClusterTM.InverseTransformPosition(ContactHandle->GetContact().CalculateWorldContactLocation());
				FAABB3 ContactBox(ContactLocationClusterLocal, ContactLocationClusterLocal);
				ContactBox.Thicken(ClusterDistanceThreshold);
				if (Cluster->ChildrenSpatial())
				{
					const TArray<FPBDRigidParticleHandle*> Intersections = Cluster->ChildrenSpatial()->FindAllIntersectingChildren(ContactBox);
					for (FPBDRigidParticleHandle* Child : Intersections)
					{
						if (TPBDRigidClusteredParticleHandle<FReal, 3>*ClusteredChild = Child->CastToClustered())
						{
							ClusteredChild->CollisionImpulses() += ContactHandle->GetAccumulatedImpulse().Size();
						}
					}
				}
			};

			if (const TArray<FPBDRigidParticleHandle*>* ChildrenPtr = MParentToChildren.Find(ClusteredConstrainedParticles0))
			{
				ComputeStrainLambda(ClusteredConstrainedParticles0, *ChildrenPtr);
			}

			if (const TArray<FPBDRigidParticleHandle*>* ChildrenPtr = MParentToChildren.Find(ClusteredConstrainedParticles1))
			{
				ComputeStrainLambda(ClusteredConstrainedParticles1, *ChildrenPtr);
			}

			MCollisionImpulseArrayDirty = true;
		}
	}

	DECLARE_CYCLE_STAT(TEXT("ResetCollisionImpulseArray"), STAT_ResetCollisionImpulseArray, STATGROUP_Chaos);
	void 
	FRigidClustering::ResetCollisionImpulseArray()
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

	void 
	FRigidClustering::DisableCluster(
		FPBDRigidClusteredParticleHandle* ClusteredParticle)
	{
		// #note: we don't recursively descend to the children
		MEvolution.DisableParticle(ClusteredParticle);
		TopLevelClusterParents.Remove(ClusteredParticle);
		GetChildrenMap().Remove(ClusteredParticle);
		ClusteredParticle->ClusterIds() = ClusterId();
		ClusteredParticle->ClusterGroupIndex() = 0;
		MActiveRemovalIndices.Remove(ClusteredParticle);
	}

	void 
	FRigidClustering::DisableParticleWithBreakEvent(
		Chaos::FPBDRigidParticleHandle* Particle)
	{
		DisableCluster(Particle->CastToClustered());

		if (DoGenerateBreakingData)
		{
			const int32 NewIdx = MAllClusterBreakings.Add(FBreakingData());
			FBreakingData& ClusterBreak = MAllClusterBreakings[NewIdx];
			ClusterBreak.Proxy = Particle->PhysicsProxy();
			ClusterBreak.Location = Particle->X();
			ClusterBreak.Velocity = Particle->V();
			ClusterBreak.AngularVelocity = Particle->W();
			ClusterBreak.Mass = Particle->M();
			if (Particle->Geometry()->HasBoundingBox())
			{
				ClusterBreak.BoundingBox = Particle->Geometry()->BoundingBox();
			}
			if (ClusterBreak.Proxy->GetType() == EPhysicsProxyType::GeometryCollectionType)
			{
				FGeometryCollectionPhysicsProxy* ConcreteProxy = static_cast<FGeometryCollectionPhysicsProxy*>(ClusterBreak.Proxy);
				ClusterBreak.TransformGroupIndex = ConcreteProxy->GetTransformGroupIndexFromHandle(Particle);
			}
			else
			{
				ClusterBreak.TransformGroupIndex = INDEX_NONE;
			}
			
		}
	}

	FPBDRigidClusteredParticleHandle*
	FRigidClustering::DestroyClusterParticle(
		FPBDRigidClusteredParticleHandle* ClusteredParticle,
		const FClusterDestoryParameters& Parameters)
	{
		FClusterHandle ParentParticle = nullptr;

		// detach connections to thie parent from the children
		if (MChildren.Contains(ClusteredParticle))
		{
			for (FRigidHandle Child : MChildren[ClusteredParticle])
			{
				if (FClusterHandle ClusteredChild = Child->CastToClustered())
				{
					ClusteredChild->ClusterIds() = ClusterId();
					ClusteredChild->ClusterGroupIndex() = 0;
				}
			}

			MChildren.Remove(ClusteredParticle);
		}

		// disable within the solver
		if (!ClusteredParticle->Disabled())
		{
			MEvolution.DisableParticle(ClusteredParticle);
			ensure(ClusteredParticle->ClusterIds().Id == nullptr);
		}

		// reset the structures
		TopLevelClusterParents.Remove(ClusteredParticle);
		MActiveRemovalIndices.Remove(ClusteredParticle);

		// disconnect from the parents
		if (ClusteredParticle->ClusterIds().Id)
		{
			ParentParticle = ClusteredParticle->Parent();

			ClusteredParticle->ClusterIds() = ClusterId();
			ClusteredParticle->ClusterGroupIndex() = 0;

			if (MChildren.Contains(ParentParticle))
			{
				FRigidHandleArray& Children = MChildren[ParentParticle];

				// disconnect from your parents children list
				Children.Remove(ClusteredParticle);

				// disable internal parents that have lost all their children
				if (!MChildren[ParentParticle].Num() && ParentParticle->InternalCluster())
				{
					DisableCluster(ClusteredParticle);
				}
			}
		}

		// remove internal parents that have no children. 
		if (ClusteredParticle->InternalCluster())
		{
			FUniqueIdx UniqueIdx = ClusteredParticle->UniqueIdx();
			MEvolution.DestroyParticle(ClusteredParticle);
			MEvolution.ReleaseUniqueIdx(UniqueIdx);
		}

		if (Parameters.bReturnInternalOnly && ParentParticle && !ParentParticle->InternalCluster())
		{
			ParentParticle = nullptr;
		}
		return ParentParticle;

	}


	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::UpdateConnectivityGraphUsingPointImplicit"), STAT_UpdateConnectivityGraphUsingPointImplicit, STATGROUP_Chaos);
	void 
	FRigidClustering::UpdateConnectivityGraphUsingPointImplicit(
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

		typedef TPair<FPBDRigidParticleHandle*, FPBDRigidParticleHandle*> ParticlePair;
		typedef TSet<ParticlePair> ParticlePairArray;

		TArray<ParticlePairArray> Connections;
		Connections.Init(ParticlePairArray(), Children.Num());

		PhysicsParallelFor(Children.Num(), [&](int32 i)
			{
				FPBDRigidParticleHandle* Child1 = Children[i];
				if (Child1->Geometry() && Child1->Geometry()->HasBoundingBox())
				{
					ParticlePairArray& ConnectionList = Connections[i];

					const FVec3& Child1X = Child1->X();
					FRigidTransform3 TM1 = FRigidTransform3(Child1X, Child1->R());

					const int32 Offset = i + 1;
					const int32 NumRemainingChildren = Children.Num() - Offset;

					for (int32 Idx = 0; Idx < NumRemainingChildren; ++Idx)
					{
						const int32 ChildrenIdx = Offset + Idx;
						FPBDRigidParticleHandle* Child2 = Children[ChildrenIdx];
						if (Child2->CollisionParticles())
						{

							const FVec3& Child2X = Child2->X();
							const FRigidTransform3 TM = TM1.GetRelativeTransform(FRigidTransform3(Child2X, Child2->R()));
							const uint32 NumCollisionParticles = Child2->CollisionParticles()->Size();
							for (uint32 CollisionIdx = 0; CollisionIdx < NumCollisionParticles; ++CollisionIdx)
							{
								const FVec3 LocalPoint =
									TM.TransformPositionNoScale(Child2->CollisionParticles()->X(CollisionIdx));
								const FReal Phi = Child1->Geometry()->SignedDistance(LocalPoint - (LocalPoint * Delta));
								if (Phi < 0.0)
								{
									ConnectionList.Add(ParticlePair(Child1, Child2));
									break;
								}

							}
						}
					}
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

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::FixConnectivityGraphUsingDelaunayTriangulation"), STAT_FixConnectivityGraphUsingDelaunayTriangulation, STATGROUP_Chaos);
	void 
	FRigidClustering::FixConnectivityGraphUsingDelaunayTriangulation(
		Chaos::FPBDRigidClusteredParticleHandle* Parent,
		const FClusterCreationParameters& Parameters)
	{
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
	void 
	FRigidClustering::UpdateConnectivityGraphUsingDelaunayTriangulation(
		Chaos::FPBDRigidClusteredParticleHandle* Parent, 
		const FClusterCreationParameters& Parameters)
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

	void 
	FRigidClustering::ConnectNodes(
		FPBDRigidParticleHandle* Child1,
		FPBDRigidParticleHandle* Child2)
	{
		check(Child1 != Child2);
		FPBDRigidClusteredParticleHandle* ClusteredChild1 = Child1->CastToClustered();
		FPBDRigidClusteredParticleHandle* ClusteredChild2 = Child2->CastToClustered();
		ConnectNodes(ClusteredChild1, ClusteredChild2);
	}

	void 
	FRigidClustering::ConnectNodes(
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

	void 
	FRigidClustering::RemoveNodeConnections(
		FPBDRigidParticleHandle* Child)
	{
		RemoveNodeConnections(Child->CastToClustered());
	}

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::RemoveNodeConnections"), STAT_RemoveNodeConnections, STATGROUP_Chaos);
	void 
	FRigidClustering::RemoveNodeConnections(
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
