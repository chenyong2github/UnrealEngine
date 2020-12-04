// Copyright Epic Games, Inc. All Rights Reserved.

#include "FractureToolClusterMagnet.h"

#include "FractureTool.h"
#include "FractureEditorStyle.h"
#include "FractureEditorCommands.h"

#include "Chaos/TriangleMesh.h"
#include "PhysicsProxy/GeometryCollectionPhysicsProxy.h"
#include "Chaos/MassProperties.h"
#include "Chaos/Particles.h"
#include "Chaos/Vector.h"

#include "GeometryCollection/GeometryCollectionAlgo.h"
#include "GeometryCollection/GeometryCollectionClusteringUtility.h"
#include "GeometryCollection/GeometryCollectionProximityUtility.h"

#include "GeometryCollection/GeometryCollectionComponent.h"


#define LOCTEXT_NAMESPACE "FractureClusterMagnet"



UFractureToolClusterMagnet::UFractureToolClusterMagnet(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	ClusterMagnetSettings = NewObject<UFractureClusterMagnetSettings>(GetTransientPackage(), UFractureClusterMagnetSettings::StaticClass());
	ClusterMagnetSettings->OwnerTool = this;
}


FText UFractureToolClusterMagnet::GetDisplayText() const
{
	return FText(NSLOCTEXT("Fracture", "FractureToolClusterMagnet", "Magnet"));
}


FText UFractureToolClusterMagnet::GetTooltipText() const
{
	return FText(NSLOCTEXT("Fracture", "FractureToolClusterMagnetToolTip", "Builds clusters at local level by collecting bones adjacent to clusters or bones with highest mass."));
}

FText UFractureToolClusterMagnet::GetApplyText() const
{
	return FText(NSLOCTEXT("Fracturet", "ExecuteClusterMagnet", "Cluster Magnet"));
}

FSlateIcon UFractureToolClusterMagnet::GetToolIcon() const
{
	return FSlateIcon("FractureEditorStyle", "FractureEditor.ClusterMagnet");
}


TArray<UObject*> UFractureToolClusterMagnet::GetSettingsObjects() const
{
	TArray<UObject*> Settings;
	Settings.Add(ClusterMagnetSettings);
	return Settings;
}

void UFractureToolClusterMagnet::RegisterUICommand(FFractureEditorCommands* BindingContext)
{
	UI_COMMAND_EXT(BindingContext, UICommandInfo, "ClusterMagnet", "Cluster Magnet", "Builds clusters at local level by collecting bones adjacent to clusters or bones with highest mass.", EUserInterfaceActionType::ToggleButton, FInputChord());
	BindingContext->ClusterMagnet = UICommandInfo;
}

void UFractureToolClusterMagnet::Execute(TWeakPtr<FFractureEditorModeToolkit> InToolkit)
{
	
	if (InToolkit.IsValid())
	{
		TSet<UGeometryCollectionComponent*> GeomCompSelection;
		GetSelectedGeometryCollectionComponents(GeomCompSelection);
		for (UGeometryCollectionComponent* GeometryCollectionComponent : GeomCompSelection)
		{
			const UGeometryCollection* RestCollection = GeometryCollectionComponent->GetRestCollection();
			if (RestCollection)
			{
				FGeometryCollectionPtr GeometryCollection = RestCollection->GetGeometryCollection();

				// We require certain attributes present to proceed.
				if (!CheckPresenceOfNecessaryAttributes(GeometryCollection))
				{
					return;
				}

				// If no bones are selected, assume that we're working on the root's children
				TArray<int32> SelectedBones = GeometryCollectionComponent->GetSelectedBones();
				if (SelectedBones.Num() == 0)
				{
					FGeometryCollectionClusteringUtility::GetRootBones(GeometryCollection.Get(), SelectedBones);
				}

				for (int32 CurrentRoot : SelectedBones)
				{
					const TManagedArray<TSet<int32>>& Children = GeometryCollection->Children;
					if (Children[CurrentRoot].Num() > 0)
					{
						const TManagedArray<int32>& Levels = GeometryCollection->GetAttribute<int32>("Level", FTransformCollection::TransformGroup);
						const int32 OperatingLevel = Levels[CurrentRoot] + 1;
						//const TArray<int32> TopNodes = GatherTopNodes(GeometryCollection, CurrentRoot);
						const TSet<int32> TopNodes = Children[CurrentRoot];

						UpdateMasses(GeometryCollection, TopNodes);

						float CutoffMass = FindCutoffMass(ClusterMagnetSettings->MassPercentile, GeometryCollection, TopNodes);

						// We have the connections for the leaf nodes of our geometry collection. We want to percolate those up to the top nodes.
						TMap<int32, TSet<int32>> TopNodeConnectivity = InitializeConnectivity(TopNodes, GeometryCollection, OperatingLevel);

						// Separate the top nodes into cluster magnets and a pool of available nodes.
						TArray<FClusterMagnet> ClusterMagnets;
						TSet<int32> RemainingPool;
						SeparateClusterMagnets(GeometryCollection, TopNodes, CutoffMass, TopNodeConnectivity, ClusterMagnets, RemainingPool);

						for (uint32 Iteration = 0; Iteration < ClusterMagnetSettings->Iterations; ++Iteration)
						{
							bool bNeighborsAbsorbed = false;

							// each cluster gathers adjacent nodes from the pool
							for (FClusterMagnet& ClusterMagnet : ClusterMagnets)
							{
								bNeighborsAbsorbed |= AbsorbClusterNeighbors(TopNodeConnectivity, ClusterMagnet, RemainingPool);
							}

							// early termination
							if (!bNeighborsAbsorbed)
							{
								break;
							}
						}

						// Create new clusters from the cluster magnets
						for (const FClusterMagnet& ClusterMagnet : ClusterMagnets)
						{
							if (ClusterMagnet.ClusteredNodes.Num() > 1)
							{
								TArray<int32> NewChildren = ClusterMagnet.ClusteredNodes.Array();
								NewChildren.Sort();
								FGeometryCollectionClusteringUtility::ClusterBonesUnderNewNode(GeometryCollection.Get(), NewChildren[0], NewChildren, false, false);
								
							}
						}

						FGeometryCollectionClusteringUtility::UpdateHierarchyLevelOfChildren(GeometryCollection.Get(), CurrentRoot);
					}
				}

				FScopedColorEdit EditBoneColor = GeometryCollectionComponent->EditBoneSelection();
				EditBoneColor.ResetBoneSelection();
				EditBoneColor.ResetHighlightedBones();
				InToolkit.Pin()->SetBoneSelection(GeometryCollectionComponent, EditBoneColor.GetSelectedBones(), true);

				GeometryCollectionComponent->MarkRenderDynamicDataDirty();
				GeometryCollectionComponent->MarkRenderStateDirty();				
			}
		}

		InToolkit.Pin()->SetOutlinerComponents(GeomCompSelection.Array());
	}
}


bool UFractureToolClusterMagnet::CheckPresenceOfNecessaryAttributes(const FGeometryCollectionPtr GeometryCollection) const
{
	if (!GeometryCollection->HasAttribute("Level", FTransformCollection::TransformGroup))
	{
		UE_LOG(LogFractureTool, Error, TEXT("Cannot execute Cluster Magnet tool: missing Level attribute."));
		return false;
	}

	if (!GeometryCollection->HasAttribute("Proximity", FGeometryCollection::GeometryGroup))
	{
		UE_LOG(LogFractureTool, Error, TEXT("Cannot execute Cluster Magnet tool: missing Proximity attribute."));
		return false;
	}

	return true;
}

float UFractureToolClusterMagnet::FindCutoffMass(float Percentile, const FGeometryCollectionPtr GeometryCollection, const TSet<int32>& TopNodes) const
{
	check(Percentile >= 0.0);
	check(Percentile <= 1.0);
	
	const TManagedArray<float>& Mass = GeometryCollection->GetAttribute<float>("Mass", FTransformCollection::TransformGroup);

	// Collect the top node masses, sort by mass, return the threshold mass at the cutoff threshold. 
	TArray<float> Masses;
	Masses.Reserve(TopNodes.Num());
	for (int32 Index : TopNodes)
	{
		Masses.Add(Mass[Index]);
	}
	Masses.Sort();

	int32 ThresholdIndex = FMath::Floor(TopNodes.Num() * Percentile);
	return Masses[ThresholdIndex];
}


void UFractureToolClusterMagnet::UpdateMasses(FGeometryCollectionPtr GeometryCollection, const TSet<int32>& TopNodes) const 
{
	if (!GeometryCollection->HasAttribute("Mass", FTransformCollection::TransformGroup))
	{
		GeometryCollection->AddAttribute<Chaos::FReal>("Mass", FTransformCollection::TransformGroup);
		UE_LOG(LogFractureTool, Warning, TEXT("Added Mass attribute needed to execute ClusterMagnet."));
	}
	
	TArray<FTransform> Transform;
	GeometryCollectionAlgo::GlobalMatrices(GeometryCollection->Transform, GeometryCollection->Parent, Transform);

	const TManagedArray<FVector>& Vertex = GeometryCollection->Vertex;
	const TManagedArray<int32>& BoneMap = GeometryCollection->BoneMap;
	
	Chaos::TParticles<float, 3> MassSpaceParticles;
	MassSpaceParticles.AddParticles(Vertex.Num());
	for (int32 Idx = 0; Idx < Vertex.Num(); ++Idx)
	{
		MassSpaceParticles.X(Idx) = Transform[BoneMap[Idx]].TransformPosition(Vertex[Idx]);
	}

	for (int32 Index : TopNodes)
	{
		UpdateMasses(GeometryCollection, MassSpaceParticles, Index);
	}
}

void UFractureToolClusterMagnet::UpdateMasses(FGeometryCollectionPtr GeometryCollection, const Chaos::TParticles<float, 3>& MassSpaceParticles, int32 TransformIndex) const
{
	const TManagedArray<TSet<int32>>& Children = GeometryCollection->Children;
	const TManagedArray<bool>& Visible = GeometryCollection->Visible;
	const TManagedArray<int32>& FaceCount = GeometryCollection->FaceCount;
	const TManagedArray<int32>& FaceStart = GeometryCollection->FaceStart;
	const TManagedArray<int32>& TransformToGeometryIndex = GeometryCollection->TransformToGeometryIndex;
	const TManagedArray<FIntVector>& Indices = GeometryCollection->Indices;
	TManagedArray<float>& Mass = GeometryCollection->GetAttribute<float>("Mass", FTransformCollection::TransformGroup);
	
	if (Children[TransformIndex].Num() == 0) // leaf node
	{
		int32 GeometryIndex = TransformToGeometryIndex[TransformIndex];

		TUniquePtr<Chaos::TTriangleMesh<float>> TriMesh(
			CreateTriangleMesh(
				FaceStart[GeometryIndex],
				FaceCount[GeometryIndex],
				Visible,
				Indices,
				true));
		
		float Volume = 0.0;
		Chaos::TVector<Chaos::FReal,3> CenterOfMass;
		Chaos::CalculateVolumeAndCenterOfMass(MassSpaceParticles, TriMesh->GetElements(), Volume, CenterOfMass);

		// Since we're only interested in relative mass, we assume density = 1.0
		Mass[TransformIndex] = Volume;
	}
	else
	{
		// Recurse to children and sum the masses for this node
		float LocalMass = 0.0;
		for (int32 ChildIndex : Children[TransformIndex])
		{
			UpdateMasses(GeometryCollection, MassSpaceParticles, ChildIndex);
			LocalMass += Mass[ChildIndex];
		}

		Mass[TransformIndex] = LocalMass;
	}
}

TMap<int32, TSet<int32>> UFractureToolClusterMagnet::InitializeConnectivity(const TSet<int32>& TopNodes, FGeometryCollectionPtr GeometryCollection, int32 OperatingLevel) const
{
	FGeometryCollectionProximityUtility::UpdateProximity(GeometryCollection.Get());

	TMap<int32, TSet<int32>> ConnectivityMap;
	for (int32 Index : TopNodes)
	{
		// Collect the proximity indices of all the leaf nodes under this top node,
		// traced back up to its parent top node, so that all connectivity describes
		// relationships only between top nodes.
		TSet<int32> Connections;
		CollectTopNodeConnections(GeometryCollection, Index, OperatingLevel, Connections);
		Connections.Remove(Index);

		// Remove any connections outside the current operating branch.
		ConnectivityMap.Add(Index, Connections.Intersect(TopNodes));
	}

	return ConnectivityMap;
}

void UFractureToolClusterMagnet::CollectTopNodeConnections(FGeometryCollectionPtr GeometryCollection, int32 Index, int32 OperatingLevel, TSet<int32>& OutConnections) const
{
	const TManagedArray<TSet<int32>>& Children = GeometryCollection->Children;
	if (Children[Index].Num() == 0) // leaf node
	{
		const TManagedArray<TSet<int32>>& Proximity = GeometryCollection->GetAttribute<TSet<int32>>("Proximity", FGeometryCollection::GeometryGroup);
		const TManagedArray<int32>& GeometryToTransformIndex = GeometryCollection->TransformIndex;
		const TManagedArray<int32>& TransformToGeometryIndex = GeometryCollection->TransformToGeometryIndex;


		for (int32 Neighbor : Proximity[TransformToGeometryIndex[Index]])
		{
			int32 NeighborTransformIndex = GeometryToTransformIndex[Neighbor];
			OutConnections.Add(FGeometryCollectionClusteringUtility::GetParentOfBoneAtSpecifiedLevel(GeometryCollection.Get(), NeighborTransformIndex, OperatingLevel));
		}		
	}
	else
	{
		for (int32 ChildIndex : Children[Index])
		{
			CollectTopNodeConnections(GeometryCollection, ChildIndex, OperatingLevel, OutConnections);
		}
	}	
}

void UFractureToolClusterMagnet::SeparateClusterMagnets(
	const FGeometryCollectionPtr GeometryCollection,
	const TSet<int32>& TopNodes,
	float CutoffMass,
	const TMap<int32, TSet<int32>>& TopNodeConnectivity,
	TArray<FClusterMagnet>& OutClusterMagnets,
	TSet<int32>& OutRemainingPool) const
{
	// Push any top nodes over the mass threshold into cluster magnets.
	
	const TManagedArray<float>& Mass = GeometryCollection->GetAttribute<float>("Mass", FTransformCollection::TransformGroup);
	
	OutClusterMagnets.Reserve(TopNodes.Num());
	OutRemainingPool.Reserve(TopNodes.Num());

	for (int32 Index : TopNodes)
	{
		if (Mass[Index] > CutoffMass)
		{
			OutClusterMagnets.AddDefaulted();
			FClusterMagnet& NewMagnet = OutClusterMagnets.Last();
			NewMagnet.ClusteredNodes.Add(Index);
			NewMagnet.Connections = TopNodeConnectivity[Index];
		}
		else
		{
			OutRemainingPool.Add(Index);
		}
	}
}

bool UFractureToolClusterMagnet::AbsorbClusterNeighbors(const TMap<int32, TSet<int32>> TopNodeConnectivity, FClusterMagnet& OutClusterMagnet, TSet<int32>& OutRemainingPool) const
{
	// Return true if neighbors were absorbed.
	bool bNeighborsAbsorbed = false;

	TSet<int32> NewConnections;
	for (int32 NeighborIndex : OutClusterMagnet.Connections)
	{
		// If the neighbor is still in the pool, absorb it and its connections.
		if (OutRemainingPool.Contains(NeighborIndex))
		{
			OutClusterMagnet.ClusteredNodes.Add(NeighborIndex);
			NewConnections.Append(TopNodeConnectivity[NeighborIndex]);
			OutRemainingPool.Remove(NeighborIndex);
			bNeighborsAbsorbed = true;
		}
	}
	OutClusterMagnet.Connections.Append(NewConnections);

	return bNeighborsAbsorbed;
}

#undef LOCTEXT_NAMESPACE