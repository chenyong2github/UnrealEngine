// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Chaos/PBDRigidClustering.h"

#include "Chaos/ErrorReporter.h"
#include "Chaos/ImplicitObjectTransformed.h"
#include "Chaos/ImplicitObjectUnion.h"
#include "Chaos/Levelset.h"
#include "Chaos/MassProperties.h"
#include "Chaos/PBDRigidsEvolution.h"
#include "Chaos/PBDRigidsEvolutionPGS.h"
#include "Chaos/Sphere.h"
#include "Chaos/UniformGrid.h"
#include "ChaosStats.h"
#include "Containers/Queue.h"
#include "ProfilingDebugging/ScopedTimers.h"
#include "Voronoi/Voronoi.h"

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

	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	TPBDRigidClustering<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::TPBDRigidClustering(FPBDRigidsEvolution& InEvolution, TPBDRigidParticles<T, d>& InParticles)
	    : MEvolution(InEvolution)
	    , MParticles(InParticles)
	    , MCollisionImpulseArrayDirty(true)
	    , DoGenerateBreakingData(false)
		, MClusterConnectionFactor(1.0)
		, MClusterUnionConnectionType(FClusterCreationParameters<T>::EConnectionMethod::DelaunayTriangulation)
	{
		MParticles.AddArray(&MClusterIds);
		MParticles.AddArray(&MClusterGroupIndex);
		MParticles.AddArray(&MCollisionImpulses);
		MParticles.AddArray(&MInternalCluster);
		MParticles.AddArray(&MStrains);
		MParticles.AddArray(&MChildToParent);
		MParticles.AddArray(&MConnectivityEdges);
		MParticles.AddArray(&MChildrenSpatial);
		MParticles.AddArray(&MMultiChildProxyId);
		MParticles.AddArray(&MMultiChildProxyData);
	}

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::CreateClusterParticle"), STAT_CreateClusterParticle, STATGROUP_Chaos);
	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	int32 TPBDRigidClustering<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::CreateClusterParticle(
	    int32 ClusterGroupIndex,
	    const TArray<uint32>& Children,
	    /*const*/ TSerializablePtr<TImplicitObject<T, d>> ProxyGeometry,
	    const TRigidTransform<T, d>* ForceMassOrientation,
	    const FClusterCreationParameters<T>& Parameters)
	{
		SCOPE_CYCLE_COUNTER(STAT_CreateClusterParticle);

		FClusterMap& MParentToChildren = GetChildrenMap();

		int32 NewIndex = Parameters.RigidBodyIndex;
		if (NewIndex == INDEX_NONE)
		{
			NewIndex = MParticles.Size();
			MParticles.AddParticles(1);
		}

		MEvolution.EnableParticle(NewIndex, Children[0]);
		MParticles.CollisionGroup(NewIndex) = INT_MAX;
		TopLevelClusterParents.Add(NewIndex);

		MInternalCluster[NewIndex] = false;
		MClusterIds[NewIndex] = ClusterId(INDEX_NONE, Children.Num());
		MClusterGroupIndex[NewIndex] = ClusterGroupIndex;

		//
		// Update clustering data structures.
		//
		if (MParentToChildren.Contains(NewIndex))
		{
			MParentToChildren[NewIndex] = MakeUnique<TArray<uint32>>(Children);
		}
		else
		{
			MParentToChildren.Add(NewIndex, MakeUnique<TArray<uint32>>(Children));
		}

		bool bClusterIsAsleep = true;
		MStrains[NewIndex] = 0.0;

		const TArray<uint32>& ChildrenArray = *MParentToChildren[NewIndex];
		TSet<uint32> ChildrenSet = TSet<uint32>(ChildrenArray);	// @todo(ccaulfield): try to eliminate set creation (required by RemoveConstraints)
		MEvolution.DisableParticles(ChildrenSet);
		for (const uint32 Child : ChildrenArray)
		{
			TopLevelClusterParents.Remove(Child);
			bClusterIsAsleep &= MParticles.Sleeping(Child);
			MClusterGroupIndex[Child] = 0;
			MClusterIds[Child].Id = NewIndex;
			MStrains[NewIndex] += MStrains[Child];

			MCollisionImpulses[NewIndex] = FMath::Max(MCollisionImpulses[NewIndex], MCollisionImpulses[Child]);
			MParticles.CollisionGroup(NewIndex) = (MParticles.CollisionGroup(NewIndex) < MParticles.CollisionGroup(Child)) ? MParticles.CollisionGroup(NewIndex) : MParticles.CollisionGroup(Child);
		}
		if (Children.Num())
		{
			MStrains[NewIndex] /= Children.Num();
		}

		ensureMsgf(!ProxyGeometry || ForceMassOrientation, TEXT("If ProxyGeometry is passed, we must override the mass orientation as they are tied"));

		UpdateMassProperties(Children, NewIndex, ForceMassOrientation);
		UpdateGeometry(Children, NewIndex, ProxyGeometry, Parameters);
		GenerateConnectionGraph(NewIndex, Parameters);
		MParticles.SetSleeping(NewIndex, bClusterIsAsleep);

		return NewIndex;
	}

	int32 UnionsHaveCollisionParticles = 0;
	FAutoConsoleVariableRef CVarUnionsHaveCollisionParticles(TEXT("p.UnionsHaveCollisionParticles"), UnionsHaveCollisionParticles, TEXT(""));

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::CreateClusterParticleFromClusterChildren"), STAT_CreateClusterParticleFromClusterChildren, STATGROUP_Chaos);
	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	int32 TPBDRigidClustering<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::CreateClusterParticleFromClusterChildren(const TArray<uint32>& Children, const int32 ParentIndex, const TRigidTransform<T, d>& ClusterWorldTM, const FClusterCreationParameters<T>& Parameters)
	{
		SCOPE_CYCLE_COUNTER(STAT_CreateClusterParticleFromClusterChildren);
		FClusterMap& MParentToChildren = GetChildrenMap();

		//This cluster is made up of children that are currently in a cluster. This means we don't need to update or disable as much
		int32 NewIndex = Parameters.RigidBodyIndex;
		if (NewIndex == INDEX_NONE)
		{
			NewIndex = MParticles.Size();
			MParticles.AddParticles(1);
		}

		MEvolution.EnableParticle(NewIndex, ParentIndex);

		MParticles.CollisionGroup(NewIndex) = INT_MAX;
		TopLevelClusterParents.Add(NewIndex);
		MInternalCluster[NewIndex] = true;
		MClusterIds[NewIndex] = ClusterId(INDEX_NONE, Children.Num());

		//
		// Update clustering data structures.
		//
		if (MParentToChildren.Contains(NewIndex))
		{
			MParentToChildren[NewIndex] = MakeUnique<TArray<uint32>>(Children);
		}
		else
		{
			MParentToChildren.Add(NewIndex, MakeUnique<TArray<uint32>>(Children));
		}

		//child transforms are out of date, need to update them. @todo(ocohen): if children transforms are relative we would not need to update this, but would simply have to do a final transform on the new cluster index
		// TODO(mlentine): Why is this not needed? (Why is it ok to have DeactivateClusterChildren==false?)
		if (DeactivateClusterChildren)
		{
			TSet<uint32> ChildrenSet = TSet<uint32>(Children);	// @todo(ccaulfield): try to eliminate set creation (required by RemoveConstraints)
			MEvolution.DisableParticles(ChildrenSet);
		}
		for (uint32 Child : Children)
		{
			TRigidTransform<T, d> ChildFrame = MChildToParent[Child] * ClusterWorldTM;
			MParticles.X(Child) = ChildFrame.GetTranslation();
			MParticles.R(Child) = ChildFrame.GetRotation();
			MClusterIds[Child].Id = NewIndex;
			MClusterGroupIndex[Child] = 0;
			if (DeactivateClusterChildren)
			{
				TopLevelClusterParents.Remove(Child);
			}

			MCollisionImpulses[NewIndex] = FMath::Max(MCollisionImpulses[NewIndex], MCollisionImpulses[Child]);
			MParticles.CollisionGroup(NewIndex) = (MParticles.CollisionGroup(NewIndex) < MParticles.CollisionGroup(Child)) ? MParticles.CollisionGroup(NewIndex) : MParticles.CollisionGroup(Child);
		}

		FClusterCreationParameters<T> NoCleanParams = Parameters;
		NoCleanParams.bCleanCollisionParticles = false;
		NoCleanParams.bCopyCollisionParticles = !!UnionsHaveCollisionParticles;

		UpdateMassProperties(Children, NewIndex, nullptr);
		UpdateGeometry(Children, NewIndex, TSerializablePtr<TImplicitObject<T, 3>>(), NoCleanParams);

		return NewIndex;
	}

	int32 UseMultiChildProxy = 1;
	FAutoConsoleVariableRef CVarUseMultiChildProxy(TEXT("p.UseMultiChildProxy"), UseMultiChildProxy, TEXT("Whether to merge multiple children into a single collision proxy when one is available"));

	int32 MinChildrenForMultiProxy = 1;
	FAutoConsoleVariableRef CVarMinChildrenForMultiProxy(TEXT("p.MinChildrenForMultiProxy"), MinChildrenForMultiProxy, TEXT("Min number of children needed for multi child proxy optimization"));

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::UnionClusterGroups"), STAT_UnionClusterGroups, STATGROUP_Chaos);
	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	void TPBDRigidClustering<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::UnionClusterGroups()
	{
		SCOPE_CYCLE_COUNTER(STAT_UnionClusterGroups);

		FClusterMap& MParentToChildren = GetChildrenMap();

		TMap<int32, TArray<uint32>> GroupMapping;
		for (int32 i = 0; i < MClusterGroupIndex.Num(); i++) // just loop active clusters here.
		{
			uint32 ParticleIndex = i;
			int32 GroupIndex = MClusterGroupIndex[ParticleIndex];
			if (GroupIndex > 0)
			{
				if (!GroupMapping.Contains(GroupIndex))
				{
					GroupMapping.Add(GroupIndex, TArray<uint32>());
				}
				GroupMapping[GroupIndex].Add(ParticleIndex);
			}
		}

		for (TTuple<int32, TArray<uint32>>& Group : GroupMapping)
		{
			if (PendingClusterCounter.Contains(Group.Key) && PendingClusterCounter[Group.Key] == 0)
			{
				TArray<uint32> ClusterChildren;
				for (uint32& OriginalRootIdx : Group.Value)
				{
					TUniquePtr<TMultiChildProxyData<T, d>> ProxyData;
					if (MParentToChildren.Contains(OriginalRootIdx))
					{
						if (UseMultiChildProxy && !MParticles.DynamicGeometry(OriginalRootIdx) && MParentToChildren[OriginalRootIdx]->Num() > MinChildrenForMultiProxy) //Don't support dynamic geometry
						{
							if (ensure(MParentToChildren[OriginalRootIdx]->Num()))
							{
								ProxyData = MakeUnique<TMultiChildProxyData<T, d>>();
								ProxyData->KeyChild = (*MParentToChildren[OriginalRootIdx])[0];
								ProxyData->RelativeToKeyChild = TRigidTransform<T, d>(MParticles.X(OriginalRootIdx), MParticles.R(OriginalRootIdx)); //store world space of original root. Need to break it up and then compute relative to world space of key child
							}
						}

						const TArray<uint32> OriginalRootChildren = DeactivateClusterParticle(OriginalRootIdx).Array();
						ClusterChildren.Append(OriginalRootChildren);

						if (ProxyData)
						{
							//now that we have world space updated for key child, compute relative transform for original root
							const TRigidTransform<T, d> OriginalRootWorldTM = ProxyData->RelativeToKeyChild;
							ProxyData->RelativeToKeyChild = OriginalRootWorldTM.GetRelativeTransform(TRigidTransform<T, d>(MParticles.X(ProxyData->KeyChild), MParticles.R(ProxyData->KeyChild)));
							MMultiChildProxyData[OriginalRootIdx] = MoveTemp(ProxyData);

							for (uint32 Child : OriginalRootChildren)
							{
								//remember original proxy of child cluster
								MMultiChildProxyId[Child].Id = OriginalRootIdx;
							}
						}
					}
					else
					{
						ClusterChildren.Add(OriginalRootIdx);
					}
				}

				FClusterCreationParameters<T> Parameters(0.3, 100, false, !!UnionsHaveCollisionParticles);
				Parameters.ConnectionMethod = MClusterUnionConnectionType;
				int32 NewIndex = CreateClusterParticle(-Group.Key, ClusterChildren, TSerializablePtr<TImplicitObject<T, d>>(), nullptr, Parameters);
				MInternalCluster[NewIndex] = true;
				MEvolution.SetPhysicsMaterial(NewIndex, MEvolution.GetPhysicsMaterial(Group.Value[0]));

				PendingClusterCounter.Remove(Group.Key);
			}
		}
	}

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::DeactivateClusterParticle"), STAT_DeactivateClusterParticle, STATGROUP_Chaos);
	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	TSet<uint32> TPBDRigidClustering<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::DeactivateClusterParticle(const uint32 ClusterIndex)
	{
		SCOPE_CYCLE_COUNTER(STAT_DeactivateClusterParticle);
		FClusterMap& MParentToChildren = GetChildrenMap();

		TSet<uint32> ActivatedChildren;
		if (0 <= ClusterIndex && ClusterIndex < (uint32)MClusterIds.Num())
		{
			check(!MParticles.Disabled(ClusterIndex));
			if (MParentToChildren.Contains(ClusterIndex))
			{
				ActivatedChildren = ReleaseClusterParticles(*MParentToChildren[ClusterIndex]);
			}
		}
		return ActivatedChildren;
	}

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::ReleaseClusterParticles(LIST)"), STAT_ReleaseClusterParticles_LIST, STATGROUP_Chaos);
	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	TSet<uint32> TPBDRigidClustering<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::ReleaseClusterParticles(const TArray<uint32>& ChildrenParticles)
	{
		SCOPE_CYCLE_COUNTER(STAT_ReleaseClusterParticles_LIST);
		TSet<uint32> ActivatedBodies;
		bool bFound = false;
		if (ChildrenParticles.Num())
		{
			uint32 ClusterIdx = 0;
			//todo(ocohen): refactor incoming, for now just assume these all belong to same cluster and hack strain array
			TArray<float> FakeStrain;
			FakeStrain.Init(0, MParticles.Size()); //this part especially sucks

			bool bPreDoGenerateData = DoGenerateBreakingData;
			DoGenerateBreakingData = false;

			for (uint32 ChildIdx : ChildrenParticles)
			{
				if (MParticles.Disabled(ChildIdx) && MClusterIds[ChildIdx].Id != INDEX_NONE)
				{
					if (ensure(!bFound || MClusterIds[ChildIdx].Id == ClusterIdx))
					{
						bFound = true;
						FakeStrain[ChildIdx] = FLT_MAX;
						ClusterIdx = MClusterIds[ChildIdx].Id;
					}
					else
					{
						break; //shouldn't be here
					}
				}
			}

			if (bFound)
			{
				ActivatedBodies.Append(ReleaseClusterParticles(ClusterIdx, FakeStrain));
			}

			DoGenerateBreakingData = bPreDoGenerateData;
		}

		return ActivatedBodies;
	}

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::ReleaseClusterParticles(STRAIN)"), STAT_ReleaseClusterParticles_STRAIN, STATGROUP_Chaos);
	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	TSet<uint32> TPBDRigidClustering<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::ReleaseClusterParticles(const uint32 ClusterIndex, const TArrayView<T>& StrainArray)
	{
		SCOPE_CYCLE_COUNTER(STAT_ReleaseClusterParticles_STRAIN);
		FClusterMap& MParentToChildren = GetChildrenMap();

		TSet<uint32> ActivatedChildren;
		const float ClusterDistanceThreshold2 = ClusterDistanceThreshold * ClusterDistanceThreshold;

		if (!ensureMsgf(MParentToChildren.Contains(ClusterIndex), TEXT("Removing Cluster that does not exist!")))
		{
			return ActivatedChildren;
		}
		TArray<uint32>& Children = *MParentToChildren[ClusterIndex];
		bool bChildrenChanged = false;
		const bool bRewindOnDecluster = ChildrenInheritVelocity < 1;
		const TRigidTransform<T, d> PreSolveTM = bRewindOnDecluster ? TRigidTransform<T, d>(MParticles.X(ClusterIndex), MParticles.R(ClusterIndex)) : TRigidTransform<T, d>(MParticles.P(ClusterIndex), MParticles.Q(ClusterIndex));

		//@todo(ocohen): iterate with all the potential parents at once?
		//find all children within some distance of contact point

		auto RemoveChildLambda = [&](uint32 Child, int32 ChildIdx) {

			MEvolution.EnableParticle(Child, ClusterIndex);

			TopLevelClusterParents.Add(Child);

			//make sure to remove multi child proxy if it exists
			if (MMultiChildProxyId[Child].Id != INDEX_NONE)
			{
				MMultiChildProxyData[MMultiChildProxyId[Child].Id].Reset();
			}

			TRigidTransform<T, d> ChildFrame = MChildToParent[Child] * PreSolveTM;
			MParticles.X(Child) = ChildFrame.GetTranslation();
			MParticles.R(Child) = ChildFrame.GetRotation();
			MClusterIds[Child].Id = INDEX_NONE;

			if (!bRewindOnDecluster)
			{
				MParticles.P(Child) = MParticles.X(Child);
				MParticles.Q(Child) = MParticles.R(Child);
			}

			//todo(ocohen): for now just inherit velocity at new COM. This isn't quite right for rotation
			//todo(ocohen): in the presence of collisions, this will leave all children with the post-collision
			// velocity. This should be controlled by material properties so we can allow the broken pieces to
			// maintain the clusters pre-collision velocity.
			MParticles.V(Child) = MParticles.V(ClusterIndex);
			MParticles.W(Child) = MParticles.W(ClusterIndex);
			MParticles.PreV(Child) = MParticles.PreV(ClusterIndex);
			MParticles.PreW(Child) = MParticles.PreW(ClusterIndex);


			ActivatedChildren.Add(Child);
			if (ChildIdx != INDEX_NONE)
			{
				Children.RemoveAtSwap(ChildIdx, 1, /*bAllowShrinking=*/false); //@todo(ocohen): maybe avoid this until we know all children are not going away?
			}

			bChildrenChanged = true;
		};

		for (int32 ChildIdx = Children.Num() - 1; ChildIdx >= 0; --ChildIdx)
		{
			int32 Child = Children[ChildIdx];
			if (StrainArray[Child] >= MStrains[Child])
			{
				RemoveChildLambda(Child, ChildIdx); //the piece that hits just breaks off - we may want more control by looking at the edges of this piece which would give us cleaner breaks (this approach produces more rubble)

				if (MParticles.ToBeRemovedOnFracture(Child))
				{
					MActiveRemovalIndices.Add(Child);
				}
				else
				{
					if (DoGenerateBreakingData)
					{
						int32 NewIdx = MAllClusterBreakings.Add(TBreakingData<float, 3>());
						TBreakingData<float, 3>& ClusterBreakingItem = MAllClusterBreakings[NewIdx];
						ClusterBreakingItem.ParticleIndex = Child;
						ClusterBreakingItem.Location = MParticles.X(Child);
						ClusterBreakingItem.Velocity = MParticles.V(Child);
						ClusterBreakingItem.AngularVelocity = MParticles.W(Child);
						ClusterBreakingItem.Mass = MParticles.M(Child);
					}
				}
			}
		}

		if (bChildrenChanged)
		{
			if (UseConnectivity)
			{
				//cluster may have contained forests so find the connected pieces and cluster them together
				TSet<uint32> PotentialActivatedChildren;
				PotentialActivatedChildren.Append(Children);

				//first update the connected graph of the children we already removed
				for (uint32 Child : ActivatedChildren)
				{
					RemoveNodeConnections(Child);
				}

				if (PotentialActivatedChildren.Num())
				{
					TArray<TArray<uint32>> ConnectedPiecesArray;
					//traverse connectivity and see how many connected pieces we have
					TSet<uint32> ProcessedChildren;
					for (uint32 PotentialActivatedChild : PotentialActivatedChildren)
					{
						if (!ProcessedChildren.Contains(PotentialActivatedChild))
						{
							ConnectedPiecesArray.AddDefaulted();
							TArray<uint32>& ConnectedPieces = ConnectedPiecesArray.Last();

							TArray<uint32> ProcessingQueue;
							ProcessingQueue.Add(PotentialActivatedChild);
							while (ProcessingQueue.Num())
							{
								uint32 Child = ProcessingQueue.Pop();
								if (!ProcessedChildren.Contains(Child))
								{
									ProcessedChildren.Add(Child);
									ConnectedPieces.Add(Child);
									for (const TConnectivityEdge<T>& Edge : MConnectivityEdges[Child])
									{
										if (!ProcessedChildren.Contains(Edge.Sibling))
										{
											ProcessingQueue.Add(Edge.Sibling);
										}
									}
								}
							}
						}
					}

					for (const TArray<uint32>& ConnectedPieces : ConnectedPiecesArray)
					{
						if (ConnectedPieces.Num() == 1) //need to break single pieces first in case multi child proxy needs to be invalidated
						{
							const uint32 Child = ConnectedPieces[0];
							RemoveChildLambda(Child, INDEX_NONE); //no need to remove child, we'll just empty it at the end
						}
					}
					for (const TArray<uint32>& ConnectedPieces : ConnectedPiecesArray)
					{
						if (ConnectedPieces.Num() > 1) //now build the remaining pieces
						{
							int32 NewClusterIndex = CreateClusterParticleFromClusterChildren(ConnectedPieces, ClusterIndex, PreSolveTM);
							MStrains[NewClusterIndex] = MStrains[ClusterIndex];
							MEvolution.SetPhysicsMaterial(NewClusterIndex, MEvolution.GetPhysicsMaterial(ClusterIndex));

							MParticles.V(NewClusterIndex) = MParticles.V(ClusterIndex);
							MParticles.W(NewClusterIndex) = MParticles.W(ClusterIndex);
							MParticles.PreV(NewClusterIndex) = MParticles.PreV(ClusterIndex);
							MParticles.PreW(NewClusterIndex) = MParticles.PreW(ClusterIndex);
							MParticles.P(NewClusterIndex) = MParticles.X(NewClusterIndex);
							MParticles.Q(NewClusterIndex) = MParticles.R(NewClusterIndex);

							ActivatedChildren.Add(NewClusterIndex);
						}
					}
				}
			}

			for (uint32 Active : ActivatedChildren)
			{
				UpdateKinematicProperties(Active);
			}

			//disable cluster
			DisableCluster(ClusterIndex);
		}

		return ActivatedChildren;
	}

	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	void TPBDRigidClustering<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::DisableCluster(uint32 ClusterIndex)
	{
		// #note: we don't recursively descend to the children
		FClusterMap& ParentToChildren = GetChildrenMap();

		MEvolution.DisableParticle(ClusterIndex);

		if (MoveClustersWhenDeactivated)
		{
			MParticles.P(ClusterIndex) -= FVector(0.f, 0.f, -10000.f); // HACK : Move them away to prevent reactivation. 
			MParticles.X(ClusterIndex) -= FVector(0.f, 0.f, -10000.f); // HACK : Move them away to prevent reactivation. 
			MParticles.V(ClusterIndex) = FVector(0.f);            // HACK : Move them away to prevent reactivation.
		}

		TopLevelClusterParents.Remove(ClusterIndex);
		ParentToChildren.Remove(ClusterIndex);
		MClusterIds[ClusterIndex] = ClusterId();
		MClusterGroupIndex[ClusterIndex] = 0;
		MActiveRemovalIndices.Remove(ClusterIndex);
	}

	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	void TPBDRigidClustering<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::DisableParticleWithBreakEvent(uint32 ClusterIndex)
	{
		DisableCluster(ClusterIndex);

		if (DoGenerateBreakingData)
		{
			int32 NewIdx = MAllClusterBreakings.Add(TBreakingData<float, 3>());

			TBreakingData<float, 3>& ClusterBreakingItem = MAllClusterBreakings[NewIdx];
			ClusterBreakingItem.ParticleIndex = ClusterIndex;
			ClusterBreakingItem.Location = MParticles.X(ClusterIndex);
			ClusterBreakingItem.Velocity = MParticles.V(ClusterIndex);
			ClusterBreakingItem.AngularVelocity = MParticles.W(ClusterIndex);
			ClusterBreakingItem.Mass = MParticles.M(ClusterIndex);
		}
	}

	DECLARE_CYCLE_STAT(TEXT("ResetCollisionImpulseArray"), STAT_ResetCollisionImpulseArray, STATGROUP_Chaos);
	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	void TPBDRigidClustering<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::ResetCollisionImpulseArray()
	{
		SCOPE_CYCLE_COUNTER(STAT_ResetCollisionImpulseArray);
		if (MCollisionImpulseArrayDirty)
		{
			for (int i = 0; i < MCollisionImpulses.Num(); i++)
			{
				MCollisionImpulses[i] = (T)0.0;
			}
			MCollisionImpulseArrayDirty = false;
		}
	}

	float MinImpulseForStrainEval = 980 * 2 * 1.f / 30.f; //ignore impulses caused by just keeping object on ground. This is a total hack, we should not use accumulated impulse directly. Instead we need to look at delta v along constraint normal
	FAutoConsoleVariableRef CVarMinImpulseForStrainEval(TEXT("p.chaos.MinImpulseForStrainEval"), MinImpulseForStrainEval, TEXT("Minimum accumulated impulse before accumulating for strain eval "));

	DECLARE_CYCLE_STAT(TEXT("ComputeStrainFromCollision"), STAT_ComputeStrainFromCollision, STATGROUP_Chaos);
	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	void TPBDRigidClustering<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::ComputeStrainFromCollision(const FPBDCollisionConstraint& CollisionRule)
	{
		SCOPE_CYCLE_COUNTER(STAT_ComputeStrainFromCollision);
		FClusterMap& MParentToChildren = GetChildrenMap();

		ResetCollisionImpulseArray();

		for (const typename FPBDCollisionConstraint::FRigidBodyContactConstraint& Contact : CollisionRule.GetAllConstraints())
		{
			if (Contact.AccumulatedImpulse.Size() < MinImpulseForStrainEval)
			{
				continue;
			}

			auto ComputeStrainLambda = [&](uint32 ClusterIndex, const TArray<uint32>& ParentToChildren) {
				const TRigidTransform<T, d> WorldToClusterTM = TRigidTransform<T, d>(MParticles.P(ClusterIndex), MParticles.Q(ClusterIndex));
				const TVector<T, d> ContactLocationClusterLocal = WorldToClusterTM.InverseTransformPosition(GetContactLocation(Contact));
				TBox<T, d> ContactBox(ContactLocationClusterLocal, ContactLocationClusterLocal);
				ContactBox.Thicken(ClusterDistanceThreshold);
				if (MChildrenSpatial[ClusterIndex])
				{
					const TArray<int32> Intersections = MChildrenSpatial[ClusterIndex]->FindAllIntersectingChildren(ContactBox);
					for (int32 Child : Intersections)
					{
						if (const TUniquePtr<TMultiChildProxyData<T, d>>& ProxyData = MMultiChildProxyData[Child])
						{
							//multi child so get its children
							const int32 KeyChild = ProxyData->KeyChild;
							const TRigidTransform<T, d> ProxyToCluster = ProxyData->RelativeToKeyChild * MChildToParent[KeyChild];
							const TVector<T, d> ContactLocationProxyLocal = ProxyToCluster.InverseTransformPosition(ContactLocationClusterLocal);
							TBox<T, d> ContactBoxProxy(ContactLocationProxyLocal, ContactLocationProxyLocal);
							ContactBoxProxy.Thicken(ClusterDistanceThreshold);
							if (MChildrenSpatial[Child])
							{
								const TArray<int32> SubIntersections = MChildrenSpatial[Child]->FindAllIntersectingChildren(ContactBoxProxy);
								for (int32 SubChild : SubIntersections)
								{
									MCollisionImpulses[SubChild] += Contact.AccumulatedImpulse.Size();
								}
							}
						}
						else
						{
							MCollisionImpulses[Child] += Contact.AccumulatedImpulse.Size();
						}
					}
				}
			};

			if (TUniquePtr<TArray<uint32>>* ChildrenPtrPtr = MParentToChildren.Find(Contact.ParticleIndex))
			{
				ComputeStrainLambda(Contact.ParticleIndex, **ChildrenPtrPtr);
			}

			if (TUniquePtr<TArray<uint32>>* ChildrenPtrPtr = MParentToChildren.Find(Contact.LevelsetIndex))
			{
				ComputeStrainLambda(Contact.LevelsetIndex, **ChildrenPtrPtr);
			}

			MCollisionImpulseArrayDirty = true;
		}
	}

	template<class T, int d>
	T CalculatePseudoMomentum(const TPBDRigidParticles<T, d>& InParticles, const uint32 Index)
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
	void RewindAndEvolve(TPBDRigidsEvolutionGBF<T, d>& Evolution, TPBDRigidParticles<T, d>& InParticles, const TSet<int32>& IslandsToRecollide, const TSet<uint32>& AllActivatedChildren, const T Dt, TPBDCollisionConstraint<T, d>& CollisionRule)
	{
		SCOPE_CYCLE_COUNTER(STAT_RewindAndEvolve_BGF);
		// Rewind active particles
		const TArray<int32> IslandsToRecollideArray = IslandsToRecollide.Array();
		PhysicsParallelFor(IslandsToRecollideArray.Num(), [&](int32 Idx) {
			int32 Island = IslandsToRecollideArray[Idx];
			TArray<int32> ParticleIndices = Evolution.GetIslandParticles(Island); // copy
			for (int32 ArrayIdx = ParticleIndices.Num() - 1; ArrayIdx >= 0; --ArrayIdx)
			{
				int32 Index = ParticleIndices[ArrayIdx];
				if (InParticles.Sleeping(Index) || InParticles.Disabled(Index))
				{
					ParticleIndices.RemoveAtSwap(ArrayIdx);
				}
				else
				{
					InParticles.P(Index) = InParticles.X(Index);
					InParticles.Q(Index) = InParticles.R(Index);
					InParticles.V(Index) = InParticles.PreV(Index);
					InParticles.W(Index) = InParticles.PreW(Index);
				}
			}
			Evolution.Integrate(ParticleIndices, Dt);
		});

		TSet<uint32> AllIslandParticles;
		for (int32 Island = 0; Island < Evolution.NumIslands(); ++Island)
		{
			const TArray<int32>& ParticleIndices = Evolution.GetIslandParticles(Island);
			for (const int32 Index : ParticleIndices)
			{
				bool bDisabled = InParticles.Disabled(Index);

				// #TODO - Have to repeat checking out whether the particle is disabled matching the PFor above.
				// Move these into shared array so we only process it once
				if (!AllIslandParticles.Contains(Index) && !bDisabled)
				{
					AllIslandParticles.Add(Index);
				}
			}
		}

		const bool bRewindOnDeclusterSolve = ChildrenInheritVelocity < 1.f;
		if (bRewindOnDeclusterSolve)
		{
			// @todo(mlentine): We can precompute internal constraints which can filter some from the narrow phase tests but may not help much

			CollisionRule.UpdateConstraints(InParticles, Evolution.GetNonDisabledIndices(), Dt, AllActivatedChildren, AllIslandParticles.Array());

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

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::RewindAndEvolve<PSG>()"), STAT_RewindAndEvolve_PSG, STATGROUP_Chaos);
	template<class T, int d>
	void RewindAndEvolve(TPBDRigidsEvolutionPGS<T, d>& Evolution, TPBDRigidParticles<T, d>& InParticles, const TSet<int32>& IslandsToRecollide, const TSet<uint32>& AllActivatedChildren, const T Dt, TPBDCollisionConstraintPGS<T, d>& CollisionRule)
	{
		SCOPE_CYCLE_COUNTER(STAT_RewindAndEvolve_PSG);
		// Rewind active particles
		TArray<int32>& NonDisabledIndices = Evolution.GetNonDisabledIndices();
		PhysicsParallelFor(IslandsToRecollide.Num(), [&](int32 Island) {
			TArray<int32> ParticleIndices = Evolution.GetIslandParticles(Island); // copy
			for (int32 ArrayIdx = ParticleIndices.Num() - 1; ArrayIdx >= 0; --ArrayIdx)
			{
				int32 Index = ParticleIndices[ArrayIdx];
				if (InParticles.Sleeping(Index) || InParticles.Disabled(Index))
				{
					ParticleIndices.RemoveAtSwap(ArrayIdx);
				}
				else
				{
					InParticles.P(Index) = InParticles.X(Index);
					InParticles.Q(Index) = InParticles.R(Index);
					InParticles.V(Index) = InParticles.PreV(Index);
					InParticles.W(Index) = InParticles.PreW(Index);
				}
			}
			Evolution.IntegrateV(ParticleIndices, Dt);
		});

		TSet<uint32> AllIslandParticles;
		for (const auto& Island : IslandsToRecollide)
		{
			const TArray<int32>& ParticleIndices = Evolution.GetIslandParticles(Island);
			for (const auto& Index : ParticleIndices)
			{
				if (InParticles.Disabled(Index) == false) //HACK: cluster code is incorrectly adding disabled children
				{
					if (!AllIslandParticles.Contains(Index))
					{
						AllIslandParticles.Add(Index);
						NonDisabledIndices.Add(Index);
					}
				}
				else
				{
					//FPlatformMisc::DebugBreak();
				}
			}
		}

		// @todo(mlentine): We can precompute internal constraints which can filter some from the narrow phase tests but may not help much
		CollisionRule.UpdateConstraints(InParticles, Dt, AllActivatedChildren, AllIslandParticles.Array());

		Evolution.InitializeAccelerationStructures();

		PhysicsParallelFor(Evolution.NumIslands(), [&](int32 Island) {
			Evolution.UpdateAccelerationStructures(Island);
			Evolution.ApplyConstraints(Dt, Island);
		});
		PhysicsParallelFor(Evolution.NumIslands(), [&](int32 Island) {
			const TArray<int32>& ParticleIndices = Evolution.GetIslandParticles(Island);
			Evolution.IntegrateX(ParticleIndices, Dt);
		});

		// @todo(mlentine): Need to enforce constraints
		PhysicsParallelFor(Evolution.NumIslands(), [&](int32 Island) {
			Evolution.ApplyPushOut(Dt, Island);
		});
	}

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::AdvanceClustering"), STAT_AdvanceClustering, STATGROUP_Chaos);
	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::Update Impulse from Strain"), STAT_UpdateImpulseStrain, STATGROUP_Chaos);
	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::Update Dirty Impulses"), STAT_UpdateDirtyImpulses, STATGROUP_Chaos);
	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::Rewind"), STAT_ClusterRewind, STATGROUP_Chaos);
	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	void TPBDRigidClustering<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::AdvanceClustering(const T Dt, FPBDCollisionConstraint& CollisionRule)
	{
		SCOPE_CYCLE_COUNTER(STAT_AdvanceClustering);
		UE_LOG(LogChaos, Verbose, TEXT("START FRAME with Dt %f"), Dt);

		FClusterMap& MParentToChildren = GetChildrenMap();

		double FrameTime = 0, Time = 0;
		FDurationTimer Timer(Time);
		Timer.Start();

		{
			const float Threshold = 1.0f;
			TSet<int32> RemovalIndicesCopy = MActiveRemovalIndices; // copy since RemoveOnFracture alters MActiveRemovalIndices
			for (int32 Idx : RemovalIndicesCopy)
			{
				if (MParticles.ToBeRemovedOnFracture(Idx) && MParticles.V(Idx).SizeSquared() > Threshold && MParticles.PreV(Idx).SizeSquared() > Threshold)
				{
					DisableParticleWithBreakEvent(Idx);
				}
			}
		}

		if (MCollisionImpulses.Num())
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
				const TSet<int32>& ActiveIndices = MEvolution.GetActiveIndices();
				for (const auto& ActiveIndex : ActiveIndices)
				{
					if (MClusterIds[ActiveIndex].NumChildren > 0) //active index is a cluster
					{
						const TArray<uint32>& ParentToChildren = *MParentToChildren[ActiveIndex];
						for (const uint32 Child : ParentToChildren)
						{
							if (MStrains[Child] <= 0.f)
							{
								MCollisionImpulses[Child] = FLT_MAX;
								MCollisionImpulseArrayDirty = true;
							}
						}
					}
				}
			}

			if (MCollisionImpulseArrayDirty)
			{
				SCOPE_CYCLE_COUNTER(STAT_UpdateDirtyImpulses);
				TArrayView<T> CollisionImpulsesView(&(MCollisionImpulses[0]), MCollisionImpulses.Num());
				TMap<uint32, TSet<uint32>> ClusterToActivatedChildren = BreakingModel(CollisionImpulsesView);

				TSet<uint32> AllActivatedChildren;
				TSet<int32> IslandsToRecollide;
				for (auto Itr : ClusterToActivatedChildren)
				{
					//question: do we need to iterate all the children? Seems like island is known from cluster, but don't want to break anything at this point
					const TSet<uint32>& ActivatedChildren = Itr.Value;
					for (uint32 ActiveChild : ActivatedChildren)
					{
						if (ensure(!MParticles.Disabled(ActiveChild)))
						{
							int32 Island = MParticles.Island(ActiveChild);
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
							const uint32 ClusterId = Itr.Key;

							const TSet<uint32>& ActivatedChildren = Itr.Value;
							for (uint32 ActiveChild : ActivatedChildren)
							{
								MParticles.V(ActiveChild) = MParticles.V(ActiveChild) * (1.f - ChildrenInheritVelocity) + MParticles.V(ClusterId) * ChildrenInheritVelocity;
								MParticles.W(ActiveChild) = MParticles.W(ActiveChild) * (1.f - ChildrenInheritVelocity) + MParticles.W(ClusterId) * ChildrenInheritVelocity;
							}
						}
					}
				}
			}
		}

		Timer.Stop();
		UE_LOG(LogChaos, Verbose, TEXT("Cluster Break Update Time is %f"), Time);
	}

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::PromoteStrains()"), STAT_PromoteStrains, STATGROUP_Chaos);
	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	T TPBDRigidClustering<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::PromoteStrains(uint32 CurrentNode, TArrayView<T>& ExternalStrains)
	{
		SCOPE_CYCLE_COUNTER(STAT_PromoteStrains);
		FClusterMap& MParentToChildren = GetChildrenMap();

		T Result = 0;
		if (MParentToChildren.Contains(CurrentNode) && MParentToChildren[CurrentNode])
		{
			for (uint32 Child : *MParentToChildren[CurrentNode])
			{
				Result += PromoteStrains(Child, ExternalStrains);
			}
		}
		else
		{
			return ExternalStrains[CurrentNode];
		}
		ExternalStrains[CurrentNode] += Result;
		return Result;
	}

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::BreakingModel()"), STAT_BreakingModel, STATGROUP_Chaos);
	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	TMap<uint32, TSet<uint32>> TPBDRigidClustering<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::BreakingModel(TArrayView<T> ExternalStrain)
	{
		SCOPE_CYCLE_COUNTER(STAT_BreakingModel);
		FClusterMap& MParentToChildren = GetChildrenMap();

		TMap<uint32, TSet<uint32>> AllActivatedChildren;

		TArray<int32> NonDisabledIndices = MEvolution.GetNonDisabledIndices(); //make copy because release cluster modifies active indices. We want to iterate over original active indices
		for (int32 ActiveIdx : NonDisabledIndices)
		{
			if (MClusterIds[ActiveIdx].NumChildren)
			{
				AllActivatedChildren.Add(ActiveIdx, ReleaseClusterParticles(ActiveIdx, ExternalStrain));
			}
			else
			{
				// there's no children to break but we need to process whether this single piece is to be removed when damaged
				if (MParticles.ToBeRemovedOnFracture(ActiveIdx))
				{
					if (ExternalStrain[ActiveIdx] >= MStrains[ActiveIdx])
					{
						DisableCluster(ActiveIdx);

						if (DoGenerateBreakingData)
						{
							int32 NewIdx = MAllClusterBreakings.Add(TBreakingData<float, 3>());

							TBreakingData<float, 3>& ClusterBreakingItem = MAllClusterBreakings[NewIdx];
							ClusterBreakingItem.ParticleIndex = ActiveIdx;
							ClusterBreakingItem.Location = MParticles.X(ActiveIdx);
							ClusterBreakingItem.Velocity = MParticles.V(ActiveIdx);
							ClusterBreakingItem.AngularVelocity = MParticles.W(ActiveIdx);
							ClusterBreakingItem.Mass = MParticles.M(ActiveIdx);
						}
					}
				}
			}
		}

		return AllActivatedChildren;
	}

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::UpdateKinematicProperties()"), STAT_UpdateKinematicProperties, STATGROUP_Chaos);
	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	void TPBDRigidClustering<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::UpdateKinematicProperties(uint32 ClusterIndex)
	{
		SCOPE_CYCLE_COUNTER(STAT_UpdateKinematicProperties);
		FClusterMap& MParentToChildren = GetChildrenMap();

		EObjectStateType ObjectState = EObjectStateType::Dynamic;
		check(0 <= ClusterIndex && ClusterIndex < MParticles.Size());
		if (MParentToChildren.Contains(ClusterIndex) && MParentToChildren[ClusterIndex] != nullptr && MParentToChildren[ClusterIndex]->Num())
		{
			TQueue<int32> Queue;
			for (const int32 Child : *MParentToChildren[ClusterIndex])
			{
				Queue.Enqueue(Child);
			}

			int32 CurrentIndex;
			while (Queue.Dequeue(CurrentIndex) && ObjectState == EObjectStateType::Dynamic)
			{
				// @question : Maybe we should just store the leaf node bodies in a
				// map, that will require Memory(n*log(n))
				if (MParentToChildren.Contains(CurrentIndex))
				{
					for (const auto Child : *MParentToChildren[CurrentIndex])
					{
						Queue.Enqueue(Child);
					}
				}

				if (MParticles.ObjectState(CurrentIndex) == EObjectStateType::Kinematic)
				{
					ObjectState = EObjectStateType::Kinematic;
				}
				if (MParticles.ObjectState(CurrentIndex) == EObjectStateType::Static)
				{
					ObjectState = EObjectStateType::Static;
				}
			}

			MParticles.SetObjectState(ClusterIndex, ObjectState);
		}
	}


	int32 MassPropertiesFromMultiChildProxy = 1;
	FAutoConsoleVariableRef CVarMassPropertiesFromMultiChildProxy(TEXT("p.MassPropertiesFromMultiChildProxy"), MassPropertiesFromMultiChildProxy, TEXT(""));

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::UpdateClusterMassProperties()"), STAT_UpdateClusterMassProperties, STATGROUP_Chaos);
	template<class T, int d>
	void UpdateClusterMassProperties(TPBDRigidParticles<T, d>& Particles, const TArray<uint32>& Children, const uint32 NewIndex, const TRigidTransform<T, d>* ForceMassOrientation, const TArrayCollectionArray<TUniquePtr<TMultiChildProxyData<T, d>>>* MMultiChildProxyData, const TArrayCollectionArray<FMultiChildProxyId>* MMultiChildProxyId)
	{
		SCOPE_CYCLE_COUNTER(STAT_UpdateClusterMassProperties);
		check(0 <= NewIndex && NewIndex < Particles.Size());
		check(Children.Num());

		Particles.X(NewIndex) = TVector<T, d>(0);
		Particles.R(NewIndex) = TRotation<T, d>(FQuat::MakeFromEuler(TVector<T, d>(0)));
		Particles.V(NewIndex) = TVector<T, d>(0);
		Particles.W(NewIndex) = TVector<T, d>(0);
		Particles.M(NewIndex) = 0;
		Particles.I(NewIndex) = PMatrix<T, d, d>(0);

		auto GetMultiChildProxy = [MMultiChildProxyId, MMultiChildProxyData](const int32 OriginalChild, const TMultiChildProxyData<T,d>*& ProxyData) -> int32
		{
			const int32 MultiChildProxyId = MassPropertiesFromMultiChildProxy && MMultiChildProxyId ? (*MMultiChildProxyId)[OriginalChild].Id : INDEX_NONE;
			if (MultiChildProxyId != INDEX_NONE)
			{
				ProxyData = (*MMultiChildProxyData)[MultiChildProxyId].Get();
				return ProxyData ? MultiChildProxyId : INDEX_NONE;
			}
			return INDEX_NONE;
		};

		bool bHasChild = false;
		bool bHasProxyChild = false;
		for (const auto OriginalChild : Children)
		{
			const TMultiChildProxyData<T, d>* ProxyData;
			const int32 MultiChildProxyId = GetMultiChildProxy(OriginalChild, ProxyData);
			TVector<T, d> ChildPosition;
			TRotation<T, d> ChildRotation;
			int32 Child;

			if (MultiChildProxyId == INDEX_NONE)
			{
				Child = OriginalChild;
				ChildPosition = Particles.X(Child);
				ChildRotation = Particles.R(Child);
			}
			else if (ProxyData->KeyChild == OriginalChild)
			{
				Child = MultiChildProxyId;

				const TRigidTransform<T, d> ProxyWorldTM = ProxyData->RelativeToKeyChild * TRigidTransform<T, d>(Particles.X(OriginalChild), Particles.R(OriginalChild));
				ChildPosition = ProxyWorldTM.GetLocation();
				ChildRotation = ProxyWorldTM.GetRotation();
			}
			else
			{
				continue; //using a proxy but we are not the key child
			}

			const auto& ChildMass = Particles.M(Child);
			const PMatrix<T, d, d> ChildWorldSpaceI = (ChildRotation * FMatrix::Identity) * Particles.I(Child) * (ChildRotation * FMatrix::Identity).GetTransposed();
			if (ChildWorldSpaceI.ContainsNaN())
			{
				continue;
			}
			bHasProxyChild = true;
			bHasChild = true;
			Particles.I(NewIndex) += ChildWorldSpaceI;
			Particles.M(NewIndex) += ChildMass;
			Particles.X(NewIndex) += ChildPosition * ChildMass;
			Particles.V(NewIndex) += Particles.V(OriginalChild) * ChildMass; //use original child for veocities because we don't simulate proxy
			Particles.W(NewIndex) += Particles.W(OriginalChild) * ChildMass;
		}
		if (!ensure(bHasProxyChild))
		{
			for (const auto OriginalChild : Children)
			{
				TVector<T, d> ChildPosition;
				TRotation<T, d> ChildRotation;
				int32 Child;

				Child = OriginalChild;
				ChildPosition = Particles.X(Child);
				ChildRotation = Particles.R(Child);

				const auto& ChildMass = Particles.M(Child);
				const PMatrix<T, d, d> ChildWorldSpaceI = (ChildRotation * FMatrix::Identity) * Particles.I(Child) * (ChildRotation * FMatrix::Identity).GetTransposed();
				if (ChildWorldSpaceI.ContainsNaN())
				{
					continue;
				}
				bHasChild = true;
				Particles.I(NewIndex) += ChildWorldSpaceI;
				Particles.M(NewIndex) += ChildMass;
				Particles.X(NewIndex) += ChildPosition * ChildMass;
				Particles.V(NewIndex) += Particles.V(OriginalChild) * ChildMass;	//use original child for veocities because we don't simulate proxy
				Particles.W(NewIndex) += Particles.W(OriginalChild) * ChildMass;
			}
		}
		for (int32 i = 0; i < d; i++)
		{
			const PMatrix<T, d, d>& InertiaTensor = Particles.I(NewIndex);
			if (InertiaTensor.GetColumn(i)[i] < SMALL_NUMBER)
			{
				Particles.I(NewIndex) = PMatrix<T, d, d>(1.f, 1.f, 1.f);
				break;
			}
		}

		if (!ensure(bHasChild) || !ensure(Particles.M(NewIndex) > SMALL_NUMBER))
		{
			Particles.M(NewIndex) = 1;
			Particles.X(NewIndex) = TVector<T, d>(0);
			Particles.V(NewIndex) =  TVector<T, d>(0);
			Particles.PreV(NewIndex) = Particles.V(NewIndex);
			Particles.InvM(NewIndex) = 1;
			Particles.P(NewIndex) = Particles.X(NewIndex);
			Particles.W(NewIndex) = TVector<T, d>(0);
			Particles.PreW(NewIndex) = Particles.W(NewIndex);
			Particles.R(NewIndex) = TRotation<T, d>(FMatrix::Identity);
			Particles.Q(NewIndex) = Particles.R(NewIndex);
			Particles.I(NewIndex) = FMatrix::Identity;	
			Particles.InvI(NewIndex) = FMatrix::Identity;	
			return;
		}

		check(Particles.M(NewIndex) > SMALL_NUMBER);

		Particles.X(NewIndex) /= Particles.M(NewIndex);
		Particles.V(NewIndex) /= Particles.M(NewIndex);
		Particles.PreV(NewIndex) = Particles.V(NewIndex);
		Particles.InvM(NewIndex) = 1. / Particles.M(NewIndex);
		if (ForceMassOrientation)
		{
			Particles.X(NewIndex) = ForceMassOrientation->GetLocation();
		}
		Particles.P(NewIndex) = Particles.X(NewIndex);
		for (const auto OriginalChild : Children)
		{
			int32 Child;
			const TMultiChildProxyData<T, d>* ProxyData = nullptr;
			const int32 MultiChildProxyId = bHasProxyChild ? GetMultiChildProxy(OriginalChild, ProxyData) : INDEX_NONE;

			TVector<T, d> ChildPosition;
			if (MultiChildProxyId == INDEX_NONE)
			{
				Child = OriginalChild;
				ChildPosition = Particles.X(Child);
			}
			else if (ProxyData->KeyChild == OriginalChild)
			{
				Child = MultiChildProxyId;
				const TRigidTransform<T, d> ProxyWorldTM = ProxyData->RelativeToKeyChild * TRigidTransform<T, d>(Particles.X(OriginalChild), Particles.R(OriginalChild));
				ChildPosition = ProxyWorldTM.GetLocation();
			}
			else
			{
				continue; //using a proxy but we are not the key child
			}

			TVector<T, d> ParentToChild = ChildPosition - Particles.X(NewIndex);

			const auto& ChildMass = Particles.M(Child);
			Particles.W(NewIndex) += TVector<T, d>::CrossProduct(ParentToChild, Particles.V(OriginalChild) * ChildMass); //taking v from original child since we are not actually simulating the proxy child
			{
				const T& p0 = ParentToChild[0];
				const T& p1 = ParentToChild[1];
				const T& p2 = ParentToChild[2];
				const T& m = Particles.M(Child);
				Particles.I(NewIndex) += PMatrix<T, d, d>(m * (p1 * p1 + p2 * p2), -m * p1 * p0, -m * p2 * p0, m * (p2 * p2 + p0 * p0), -m * p2 * p1, m * (p1 * p1 + p0 * p0));
			}
		}
		if (Particles.I(NewIndex).ContainsNaN())
		{
			Particles.I(NewIndex) = PMatrix<T, d, d>(1.f, 1.f, 1.f);
		}
		for (int32 i = 0; i < d; i++)
		{
			const PMatrix<T, d, d>& InertiaTensor = Particles.I(NewIndex);
			if (InertiaTensor.GetColumn(i)[i] < SMALL_NUMBER)
			{
				Particles.I(NewIndex) = PMatrix<T, d, d>(1.f, 1.f, 1.f);
				break;
			}
		}
		Particles.W(NewIndex) /= Particles.M(NewIndex);
		Particles.PreW(NewIndex) = Particles.W(NewIndex);
		Particles.R(NewIndex) = Chaos::TransformToLocalSpace<T, d>(Particles.I(NewIndex));
		if (ForceMassOrientation)
		{
			Particles.R(NewIndex) = ForceMassOrientation->GetRotation();
		}
		Particles.Q(NewIndex) = Particles.R(NewIndex);
		Particles.InvI(NewIndex) = Particles.I(NewIndex).Inverse();
	}

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::UpdateGeometry"), STAT_UpdateGeometry, STATGROUP_Chaos);
	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::UpdateGeometry_GatherObjects"), STAT_UpdateGeometry_GatherObjects, STATGROUP_Chaos);
	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::UpdateGeometry_GatherPoints"), STAT_UpdateGeometry_GatherPoints, STATGROUP_Chaos);
	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::UpdateGeometry_CopyPoints"), STAT_UpdateGeometry_CopyPoints, STATGROUP_Chaos);
	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::UpdateGeometry_PointsBVH"), STAT_UpdateGeometry_PointsBVH, STATGROUP_Chaos);

	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	void TPBDRigidClustering<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::UpdateGeometry(const TArray<uint32>& Children,
	    const uint32 NewIndex, TSerializablePtr<TImplicitObject<T, d>> ProxyGeometry, const FClusterCreationParameters<T>& Parameters)
	{
		SCOPE_CYCLE_COUNTER(STAT_UpdateGeometry);
		const bool bUseCollisionPoints = (ProxyGeometry || Parameters.bCopyCollisionParticles) && !Parameters.CollisionParticles;
		TArray<TUniquePtr<TImplicitObject<T, d>>> Objects;
		TArray<TUniquePtr<TImplicitObject<T, d>>> Objects2; //todo: find a better way to reuse this

		ensure(!MParticles.Geometry(NewIndex)); //we should never update existing geometry since this is used by SQ threads.
		ensure(!MParticles.DynamicGeometry(NewIndex)); //we should never update existing geometry since this is used by SQ threads.

		TArray<TVector<T, d>> OriginalPoints;
		bool bUseParticleImplicit = false;
		TArray<int32> GeomToOriginalParticlesHack;
		GeomToOriginalParticlesHack.Reserve(Children.Num());
		bool bUsingMultiChildProxy = false;

		const TRigidTransform<T, d> ClusterWorldTM(MParticles.X(NewIndex), MParticles.R(NewIndex));
		{
			SCOPE_CYCLE_COUNTER(STAT_UpdateGeometry_GatherObjects);
			for (const uint32 Child : Children)
			{
				const TRigidTransform<T, d> ChildWorldTM(MParticles.X(Child), MParticles.R(Child));
				TRigidTransform<T, d> Frame = ChildWorldTM.GetRelativeTransform(ClusterWorldTM);
				int32 UsedGeomChild = Child;
				if (MParticles.Geometry(Child))
				{
					const int32 MultiChildProxyId = MMultiChildProxyId[Child].Id;
					if (UseLevelsetCollision || MultiChildProxyId == INDEX_NONE || !MMultiChildProxyData[MultiChildProxyId])
					{
						Objects.Add(TUniquePtr<TImplicitObject<T, d>>(new TImplicitObjectTransformed<T, d>(MParticles.Geometry(Child), Frame)));
						Objects2.Add(TUniquePtr<TImplicitObject<T, d>>(new TImplicitObjectTransformed<T, d>(MParticles.Geometry(Child), Frame)));
						GeomToOriginalParticlesHack.Add(Child);
					}
					else if (MMultiChildProxyData[MultiChildProxyId]->KeyChild == Child)
					{
						//using multi child proxy and this child is the key
						const TRigidTransform<T, d> ProxyWorldTM = MMultiChildProxyData[MultiChildProxyId]->RelativeToKeyChild * ChildWorldTM;
						const TRigidTransform<T, d> ProxyRelativeTM = ProxyWorldTM.GetRelativeTransform(ClusterWorldTM);
						Objects.Add(TUniquePtr<TImplicitObject<T, d>>(new TImplicitObjectTransformed<T, d>(MParticles.Geometry(MultiChildProxyId), ProxyRelativeTM)));
						Objects2.Add(TUniquePtr<TImplicitObject<T, d>>(new TImplicitObjectTransformed<T, d>(MParticles.Geometry(MultiChildProxyId), ProxyRelativeTM)));
						UsedGeomChild = MultiChildProxyId;
						GeomToOriginalParticlesHack.Add(UsedGeomChild);
						bUsingMultiChildProxy = true;
					}
				}

				ensure(MParticles.Disabled(Child) == true);
				ensure(!MEvolution.GetActiveIndices().Contains(Child));

				check(MClusterIds[Child].Id == NewIndex);
				MChildToParent[Child] = Frame;

				if (bUseCollisionPoints)
				{
					SCOPE_CYCLE_COUNTER(STAT_UpdateGeometry_GatherPoints);
					if (MParticles.CollisionParticles(Child))
					{
						for (uint32 i = 0; i < MParticles.CollisionParticles(Child)->Size(); ++i)
						{
							OriginalPoints.Add(Frame.TransformPosition(MParticles.CollisionParticles(Child)->X(i)));
						}
					}
				}

				if (MParticles.Geometry(Child) && MParticles.Geometry(Child)->GetType() == ImplicitObjectType::Unknown)
				{
					bUseParticleImplicit = true;
				}
			}
		}

		{
			QUICK_SCOPE_CYCLE_COUNTER(SpatialBVH);
			MChildrenSpatial[NewIndex] = Objects2.Num() ? MakeUnique<TImplicitObjectUnion<T, d>>(MoveTemp(Objects2), GeomToOriginalParticlesHack) : nullptr;
		}

		TArray<TVector<T, d>> CleanedPoints;
		if (!Parameters.CollisionParticles)
		{
			if (Parameters.bCleanCollisionParticles)
			{
				CleanedPoints = CleanCollisionParticles(OriginalPoints, ClusterSnapDistance);
			}
			else
			{
				CleanedPoints = OriginalPoints;
			}
		}

		if (ProxyGeometry)
		{
			//ensureMsgf(false, TEXT("Checking usage with proxy"));
			//@coverage {production}
			MParticles.SetGeometry(NewIndex, ProxyGeometry);
		}
		else if (Objects.Num() == 0)
		{
			//ensureMsgf(false, TEXT("Checking usage with no proxy and no objects"));
			//@coverage : {production}
			MParticles.SetGeometry(NewIndex, Chaos::TSerializablePtr<Chaos::TImplicitObject<float, 3>>());
		}
		else
		{
			if (UseLevelsetCollision)
			{
				ensureMsgf(false, TEXT("Checking usage with no proxy and multiple ojects with levelsets"));

				TImplicitObjectUnion<T, d> UnionObject(MoveTemp(Objects));
				TBox<T, d> Bounds = UnionObject.BoundingBox();
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
					MParticles.SetDynamicGeometry(NewIndex, MoveTemp(LevelSet));
				}
				else
				{
					MParticles.SetDynamicGeometry(NewIndex, MakeUnique<TSphere<T, d>>(TVector<T, d>(0), BoundsExtents.Size() * 0.5f));
				}
			}
			else
			{
				if (!bUsingMultiChildProxy && Objects.Num() == 1)
				{
					// @coverage:{confidence tests}
					//ensureMsgf(false, TEXT("Checking no proxy, not level set, a single object"));
					MParticles.SetDynamicGeometry(NewIndex, MoveTemp(Objects[0]));
				}
				else
				{
					QUICK_SCOPE_CYCLE_COUNTER(UnionBVH);
					// @coverage : { confidence tests}
					//ensureMsgf(false, TEXT("Checking no proxy, not levelset, and multiple objects"));
					MParticles.SetDynamicGeometry(NewIndex, MakeUnique<TImplicitObjectUnion<T, d>>(MoveTemp(Objects), GeomToOriginalParticlesHack));
				}
			}
		}

		if (bUseParticleImplicit && MParticles.DynamicGeometry(NewIndex)) //if children are ignore analytic and this is a dynamic geom, mark it too. todo(ocohen): clean this up
		{
			MParticles.DynamicGeometry(NewIndex)->IgnoreAnalyticCollisions();
		}

		if (Parameters.CollisionParticles)
		{
			SCOPE_CYCLE_COUNTER(STAT_UpdateGeometry_CopyPoints);
			MParticles.CollisionParticles(NewIndex).Reset(Parameters.CollisionParticles);

		}
		else
		{
			{
				SCOPE_CYCLE_COUNTER(STAT_UpdateGeometry_GatherPoints);
				MParticles.CollisionParticlesInitIfNeeded(NewIndex);
				MParticles.CollisionParticles(NewIndex)->Resize(0);
				MParticles.CollisionParticles(NewIndex)->AddParticles(CleanedPoints.Num());
				for (int32 i = 0; i < CleanedPoints.Num(); ++i)
				{
					MParticles.CollisionParticles(NewIndex)->X(i) = CleanedPoints[i];
				}
			}

			if (bUseCollisionPoints)
			{
				SCOPE_CYCLE_COUNTER(STAT_UpdateGeometry_PointsBVH);
				MParticles.CollisionParticles(NewIndex)->UpdateAccelerationStructures();
			}
		}
	}

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::GenerateConnectionGraph"), STAT_GenerateConnectionGraph, STATGROUP_Chaos);
	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	void TPBDRigidClustering<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::GenerateConnectionGraph(int32 ClusterIndex, const FClusterCreationParameters<T>& Parameters)
	{
		SCOPE_CYCLE_COUNTER(STAT_GenerateConnectionGraph);
		if (!GetChildrenMap().Contains(ClusterIndex)) return;

		// Connectivity Graph
		//    Build a connectivity graph for the cluster. If the PointImplicit is specified
		//    and the ClusterIndex has collision particles then use the expensive connection
		//    method. Otherwise try the DelaunayTriangulation if not none.
		//
		if (Parameters.bGenerateConnectionGraph)
		{
			typename FClusterCreationParameters<T>::EConnectionMethod LocalConnectionMethod = Parameters.ConnectionMethod;

			if (LocalConnectionMethod == FClusterCreationParameters<T>::EConnectionMethod::None || 
				(LocalConnectionMethod == FClusterCreationParameters<T>::EConnectionMethod::PointImplicit && !MParticles.CollisionParticles(ClusterIndex)))
			{
				LocalConnectionMethod = FClusterCreationParameters<T>::EConnectionMethod::MinimalSpanningSubsetDelaunayTriangulation; // default method
			}

			if (LocalConnectionMethod == FClusterCreationParameters<T>::EConnectionMethod::PointImplicit || 
				LocalConnectionMethod == FClusterCreationParameters<T>::EConnectionMethod::PointImplicitAugmentedWithMinimalDelaunay)
			{
				UpdateConnectivityGraphUsingPointImplicit(ClusterIndex, Parameters);
			}

			if (LocalConnectionMethod == FClusterCreationParameters<T>::EConnectionMethod::DelaunayTriangulation)
			{
				UpdateConnectivityGraphUsingDelaunayTriangulation(ClusterIndex, Parameters); // not thread safe
			}

			if (LocalConnectionMethod == FClusterCreationParameters<T>::EConnectionMethod::PointImplicitAugmentedWithMinimalDelaunay ||
				LocalConnectionMethod == FClusterCreationParameters<T>::EConnectionMethod::MinimalSpanningSubsetDelaunayTriangulation)
			{
				FixConnectivityGraphUsingDelaunayTriangulation(ClusterIndex, Parameters);
			}
		}
	}

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::FixConnectivityGraphUsingDelaunayTriangulation"), STAT_FixConnectivityGraphUsingDelaunayTriangulation, STATGROUP_Chaos);
	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	void TPBDRigidClustering<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::FixConnectivityGraphUsingDelaunayTriangulation(uint32 ClusterIndex, const FClusterCreationParameters<T>& Parameters)
	{
		// @todo(investigate) : This is trying to set multiple connections and throwing a warning in ConnectNodes
		SCOPE_CYCLE_COUNTER(STAT_FixConnectivityGraphUsingDelaunayTriangulation);

		const TArray<uint32>& Children = *GetChildrenMap()[ClusterIndex];

		// Compute Delaunay neighbor graph on children centers
		TArray<FVector> Pts;
		Pts.AddUninitialized(Children.Num());
		for (int32 i = 0; i < Children.Num(); ++i)
		{
			Pts[i] = MParticles.X(Children[i]);
		}
		TArray<TArray<int32>> Neighbors;
		VoronoiNeighbors(Pts, Neighbors);

		// Build a UnionFind graph to find (indirectly) connected children
		struct UnionFindInfo
		{
			int32 GroupIdx;
			int32 Size;
		};
		TArray<UnionFindInfo> UnionInfo;
		UnionInfo.SetNumUninitialized(Children.Num());
		TMap<int32, int32> ChildReverseIdx;
		for (int32 i = 0; i < Children.Num(); ++i)
		{
			UnionInfo[i].GroupIdx = i;
			UnionInfo[i].Size = 1;
			ChildReverseIdx.Add(Children[i], i);
		}
		auto FindGroup = [&](int Idx) {
			int GroupIdx = Idx;
			if (GroupIdx >= 0 && GroupIdx < UnionInfo.Num())
			{
				int findIters = 0;
				while (UnionInfo[GroupIdx].GroupIdx != GroupIdx)
				{
					ensure(findIters++ < 10); // if this while loop iterates more than a few times, there's probably a bug in the unionfind
					UnionInfo[GroupIdx].GroupIdx = UnionInfo[UnionInfo[GroupIdx].GroupIdx].GroupIdx;
					GroupIdx = UnionInfo[GroupIdx].GroupIdx;
					if (GroupIdx < 0 || GroupIdx >= UnionInfo.Num()) break; // this is a error exit
				}
			}
			return GroupIdx;
		};
		auto MergeGroup = [&](int A, int B) {
			int GroupA = FindGroup(A);
			int GroupB = FindGroup(B);
			if (GroupA == GroupB)
			{
				return;
			}
			if (UnionInfo[GroupA].Size > UnionInfo[GroupB].Size)
			{
				Swap(GroupA, GroupB);
			}
			UnionInfo[GroupA].GroupIdx = GroupB;
			UnionInfo[GroupB].Size += UnionInfo[GroupA].Size;
		};
		for (int32 i = 0; i < Children.Num(); ++i)
		{
			for (const TConnectivityEdge<T>& Edge : MConnectivityEdges[Children[i]])
			{
				int* nbr = ChildReverseIdx.Find(Edge.Sibling);
				if (nbr)
				{
					MergeGroup(i, *nbr);
				}
			}
		}

		// Find candidate edges from the Delaunay graph to consider adding
		struct LinkCandidate
		{
			int32 A, B;
			float DistSq;
		};
		TArray<LinkCandidate> Candidates;
		float AlwaysAcceptBelowDistSqThreshold = 50.f*50.f*100.f*MClusterConnectionFactor;
		for (int32 i = 0; i < Neighbors.Num(); i++)
		{
			for (int32 Nbr : Neighbors[i])
			{
				if (Nbr < i)
				{ // assume we'll get the symmetric connection; don't bother considering this one
					continue;
				}
				
				float DistSq = FVector::DistSquared(Pts[i], Pts[Nbr]);
				if (DistSq < AlwaysAcceptBelowDistSqThreshold)
				{ // below always-accept threshold: don't bother adding to candidates array, just merge now
					MergeGroup(i, Nbr);
					int32 ChildA = Children[i], ChildB = Children[Nbr];
					const T AvgStrain = (MStrains[ChildA] + MStrains[ChildB]) * (T)0.5f;
					ConnectNodes(ChildA, ChildB, AvgStrain);
					continue;
				}

				if (FindGroup(i) == FindGroup(Nbr)) 
				{ // already part of the same group so we don't need Delaunay edge  
					continue;
				}

				// add to array to sort and add as-needed
				Candidates.Add({i, Nbr, DistSq});
			}
		}

		// Only add edges that would connect disconnected components, considering shortest edges first
		Candidates.Sort([](const LinkCandidate& A, const LinkCandidate& B) { return A.DistSq < B.DistSq; });
		for (const LinkCandidate& Candidate : Candidates)
		{
			int32 A = Candidate.A, B = Candidate.B;
			if (FindGroup(A) != FindGroup(B))
			{
				MergeGroup(A, B);
				int32 ChildA = Children[A], ChildB = Children[B];
				const T AvgStrain = (MStrains[ChildA] + MStrains[ChildB]) * (T)0.5f;
				ConnectNodes(ChildA, ChildB, AvgStrain);
			}
		}
	}

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::UpdateConnectivityGraphUsingDelaunayTriangulation"), STAT_UpdateConnectivityGraphUsingDelaunayTriangulation, STATGROUP_Chaos);
	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	void TPBDRigidClustering<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::UpdateConnectivityGraphUsingDelaunayTriangulation(uint32 ClusterIndex, const FClusterCreationParameters<T>& Parameters)
	{
		SCOPE_CYCLE_COUNTER(STAT_UpdateConnectivityGraphUsingDelaunayTriangulation);

		const TArray<uint32>& Children = *GetChildrenMap()[ClusterIndex];

		TArray<FVector> Pts;
		Pts.AddUninitialized(Children.Num());
		for (int32 i = 0; i < Children.Num(); ++i)
		{
			Pts[i] = MParticles.X(Children[i]);
		}

		TArray<TArray<int>> Neighbors;
		VoronoiNeighbors(Pts, Neighbors);

		TSet<TPair<int32, int32>> UniqueEdges;
		for (int32 i = 0; i < Neighbors.Num(); i++)
		{
			for (int32 j = 0; j < Neighbors[i].Num(); j++)
			{
				const int32 First = Children[i];
				const int32 Sibling = Children[Neighbors[i][j]];
				const bool bFirstSmaller = First < Sibling;
				TPair<int32, int32> SortedPair(bFirstSmaller ? First : Sibling, bFirstSmaller ? Sibling : First);
				if (!UniqueEdges.Contains(SortedPair))
				{
					// this does not use ConnectNodes because Neighbors is bi-direction : as in (1,2),(2,1)
					const T AvgStrain = (MStrains[First] + MStrains[Sibling]) * (T)0.5f;
					//AddUniqueConnection(Children[i], Children[Neighbors[i][j]], AvgStrain);
					ConnectNodes(First, Sibling, AvgStrain);
					UniqueEdges.Add(SortedPair);
				}
			}
		}
	}

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::UpdateConnectivityGraphUsingPointImplicit"), STAT_UpdateConnectivityGraphUsingPointImplicit, STATGROUP_Chaos);
	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	void TPBDRigidClustering<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::UpdateConnectivityGraphUsingPointImplicit(uint32 ClusterIndex, const FClusterCreationParameters<T>& Parameters)
	{
		SCOPE_CYCLE_COUNTER(STAT_UpdateConnectivityGraphUsingPointImplicit);
		FClusterMap& MParentToChildren = GetChildrenMap();

		if (UseConnectivity)
		{
			T Delta = FMath::Min(FMath::Max(Parameters.CoillisionThicknessPercent, (T)0), T(1));
			const TArray<uint32>& Children = *MParentToChildren[ClusterIndex];
			for (int32 i = 0; i < Children.Num(); ++i)
			{
				const uint32 Child1 = Children[i];
				if (!MParticles.Geometry(Child1) || !MParticles.Geometry(Child1)->HasBoundingBox())
				{
					continue;
				}
				if (!(ensure(!FMath::IsNaN(MParticles.X(Child1)[0])) && ensure(!FMath::IsNaN(MParticles.X(Child1)[1])) && ensure(!FMath::IsNaN(MParticles.X(Child1)[2]))))
				{
					continue;
				}
				TRigidTransform<T, d> TM1 = TRigidTransform<T, d>(MParticles.X(Child1), MParticles.R(Child1));
				TBox<T, d> Box1 = MParticles.Geometry(Child1)->BoundingBox();

				TArray<TArray<TPair<uint32, uint32>>> Connections;
				Connections.Init(TArray<TPair<uint32, uint32>>(), Children.Num() - (i + 1));
				PhysicsParallelFor(Children.Num() - (i + 1), [&](int32 Idx) {
					const uint32 Child2 = Children[Idx + i + 1];
					if (!MParticles.CollisionParticles(Child2))
					{
						return;
					}
					if (!(ensure(!FMath::IsNaN(MParticles.X(Child2)[0])) && ensure(!FMath::IsNaN(MParticles.X(Child2)[1])) && ensure(!FMath::IsNaN(MParticles.X(Child2)[2]))))
					{
						return;
					}
					TRigidTransform<T, d> TM = TM1.GetRelativeTransform(TRigidTransform<T, d>(MParticles.X(Child2), MParticles.R(Child2)));

					bool bCollided = false;
					for (uint32 CollisionIdx = 0; !bCollided && CollisionIdx < MParticles.CollisionParticles(Child2)->Size(); ++CollisionIdx)
					{
						TVector<T, d> Normal;
						TVector<T, d> LocalPoint = TM.TransformPositionNoScale(MParticles.CollisionParticles(Child2)->X(CollisionIdx));
						T Phi = MParticles.Geometry(Child1)->PhiWithNormal(LocalPoint - (LocalPoint * Delta), Normal);
						if (Phi < 0.0)
							bCollided = true;
					}
					if (bCollided)
					{
						Connections[Idx].Add(TPair<uint32, uint32>((uint32)Child1, (uint32)Child2));
					}
				});

				// join results and make connections
				for (const auto& ConnectionList : Connections)
				{
					for (const auto& Elem : ConnectionList)
					{
						if (MConnectivityEdges[Elem.Key].Num() < Parameters.MaxNumConnections)
						{
							const T AvgStrain = (MStrains[Elem.Key] + MStrains[Elem.Value]) * (T)0.5f;
							ConnectNodes(Elem.Key, Elem.Value, AvgStrain);
						}
					}
				}
			}
		}
	}

	template<class T, int d>
	TVector<T, d> GetContactLocation(const TRigidBodyContactConstraint<T, d>& Contact)
	{
		return Contact.Location;
	}

	template<class T, int d>
	TVector<T, d> GetContactLocation(const TRigidBodyContactConstraintPGS<T, d>& Contact)
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

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::UpdateMassProperties"), STAT_UpdateMassProperties, STATGROUP_Chaos);
	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	void TPBDRigidClustering<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::UpdateMassProperties(const TArray<uint32>& Children, const uint32 ClusterIndex, const TRigidTransform<T, d>* ForceMassOrientation)
	{
		SCOPE_CYCLE_COUNTER(STAT_UpdateMassProperties);
		UpdateClusterMassProperties(MParticles, Children, ClusterIndex, ForceMassOrientation, &MMultiChildProxyData, &MMultiChildProxyId);
		UpdateKinematicProperties(ClusterIndex);
	}

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::GetActiveClusterIndex"), STAT_GetActiveClusterIndex, STATGROUP_Chaos);
	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	int32 TPBDRigidClustering<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::GetActiveClusterIndex(uint32 ChildIndex)
	{
		SCOPE_CYCLE_COUNTER(STAT_GetActiveClusterIndex);
		int32 NumParticles = MParticles.Size();
		int32 CurrentIndex = (int32)ChildIndex;
		while (0 <= CurrentIndex && CurrentIndex < NumParticles)
		{
			if (!MParticles.Disabled(CurrentIndex))
			{
				return CurrentIndex;
			}
			CurrentIndex = MClusterIds[CurrentIndex].Id;
		}
		return INDEX_NONE;
	}

	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	void TPBDRigidClustering<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::AddUniqueConnection(uint32 Index1, uint32 Index2, T Strain)
	{
		if (Index1 != Index2)
		{
			//todo(pref): This can be removed if we are sure there are no duplicate connections generated.
			for (int32 i = 0; i < MConnectivityEdges[Index1].Num(); i++)
			{
				if (MConnectivityEdges[Index1][i].Sibling == Index2)
				{
					// @todo(duplication connection) : re-enable post GDC.  
					// FixConnectivityGraphUsingDelaunayTriangulation attempts to add multiple connections.
					// so commenting out this msg to remove the noise from the confidence test. 
					// ensureMsgf(false, TEXT("Duplicate graph connection."));
					return;
				}
			}

			MConnectivityEdges[Index1].Add({Index2, Strain});
		}
	}

	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	void TPBDRigidClustering<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::ConnectNodes(uint32 Index1, uint32 Index2, T Strain)
	{
		AddUniqueConnection(Index1, Index2, Strain);
		AddUniqueConnection(Index2, Index1, Strain);
	}

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::RemoveNodeConnections"), STAT_RemoveNodeConnections, STATGROUP_Chaos);
	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	void TPBDRigidClustering<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::RemoveNodeConnections(uint32 ParticleIndex)
	{
		SCOPE_CYCLE_COUNTER(STAT_RemoveNodeConnections);
		checkSlow(0 <= ParticleIndex && ParticleIndex < (uint32)MClusterIds.Num());

		for (int32 i = MConnectivityEdges[ParticleIndex].Num() - 1; 0 <= i; i--)
		{
			const TConnectivityEdge<T>& Edge = MConnectivityEdges[ParticleIndex][i];
			for (int Idx = MConnectivityEdges[Edge.Sibling].Num() - 1; Idx >= 0; --Idx)
			{
				const TConnectivityEdge<T>& OtherEdge = MConnectivityEdges[Edge.Sibling][Idx];
				if (OtherEdge.Sibling == ParticleIndex)
				{
					MConnectivityEdges[Edge.Sibling].RemoveAtSwap(Idx); //Note: we shouldn't have to keep searching, but sometimes these over subscribe, should fix
				}
			}
		}
		MConnectivityEdges[ParticleIndex].SetNum(0);
	}

	DECLARE_CYCLE_STAT(TEXT("TPBDRigidClustering<>::SwapBufferedData"), STAT_SwapBufferedData, STATGROUP_Chaos);
	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	void TPBDRigidClustering<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::SwapBufferedData()
	{
		SCOPE_CYCLE_COUNTER(STAT_SwapBufferedData);
		ResourceLock.WriteLock();
		//BufferResource.MChildren.Reset();
		//BufferResource.ClusterParentTransforms.Reset();	//todo: once everything is atomic this should get reset
		const TArray<TSerializablePtr<TImplicitObject<T, d>>>& AllGeom = MParticles.GetAllGeometry();
		BufferResource.GeometryPtrs.SetNum(AllGeom.Num());

		const TArray<int32>& NonDisabledIndices = MEvolution.GetNonDisabledIndices();
		for (int32 Idx : NonDisabledIndices)
		{
			const TUniquePtr<TArray<uint32>>* Children = MChildren.Find(Idx);
			if (Children && MClusterIds[Idx].Id == INDEX_NONE) //root cluster so copy children
			{
				BufferResource.MChildren.Add(Idx, **Children);
				BufferResource.ClusterParentTransforms.Add(Idx, TRigidTransform<float, 3>(MParticles.X(Idx), MParticles.R(Idx)));
			}
		}

		BufferResource.GeometryPtrs = AllGeom; //in future this should be sparse. SQ has fallback that relies on potentially all geom so can't do it yet

		ResourceLock.WriteUnlock();
	}

	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	void TPBDRigidClustering<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::IncrementPendingClusterCounter(uint32 ClusterGroupID)
	{
		if (!PendingClusterCounter.Contains(ClusterGroupID))
		{
			PendingClusterCounter.Add(ClusterGroupID, 0);
		}
		PendingClusterCounter[ClusterGroupID]++;
	}

	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	void TPBDRigidClustering<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::DecrementPendingClusterCounter(uint32 ClusterGroupID)
	{
		PendingClusterCounter[ClusterGroupID]--;
		ensure(0 <= PendingClusterCounter[ClusterGroupID]);
	}

	/*
	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	void TPBDRigidClustering<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::ClearPendingClusterCounter(uint32 ClusterGroupID)
	{
		PendingClusterCounter[ClusterGroupID]=0;
		//ensure(0 <= PendingClusterCounter[ClusterGroupID]);
	}
*/
}

using namespace Chaos;
template class Chaos::TPBDRigidClustering<TPBDRigidsEvolutionGBF<float, 3>, TPBDCollisionConstraint<float, 3>, float, 3>;
template class Chaos::TPBDRigidClustering<TPBDRigidsEvolutionPGS<float, 3>, TPBDCollisionConstraintPGS<float, 3>, float, 3>;
template CHAOS_API void Chaos::UpdateClusterMassProperties<float, 3>(TPBDRigidParticles<float, 3>& Particles, const TArray<uint32>& Children, const uint32 ClusterIndex, const TRigidTransform<float, 3>* ForceMassOrientation,
    const TArrayCollectionArray<TUniquePtr<TMultiChildProxyData<float, 3>>>* MMultiChildProxyData,
    const TArrayCollectionArray<FMultiChildProxyId>* MMultiChildProxyId);
