// Copyright Epic Games, Inc. All Rights Reserved.

#ifdef NEW_DIRECTLINK_PLUGIN

#include "DatasmithMaxDirectLink.h"

#include "DatasmithSceneFactory.h"


#include "Windows/AllowWindowsPlatformTypes.h"
MAX_INCLUDES_START
	#include "impexp.h"
	#include "max.h"
MAX_INCLUDES_END

#if WITH_ITOO_INTERFACE
#pragma warning( push )
#pragma warning( disable: 4238 )
//#include "itoo/forestitreesinterface.h"
#include "itreesinterface.H"
#pragma warning( pop )

#include "ircinterface.H"
#endif // WITH_ITOO_INTERFACE

namespace DatasmithMaxDirectLink
{

bool ConvertRailClone(ISceneTracker& SceneTracker, FNodeTracker& NodeTracker, Object* Obj)
{
	INode* RailCloneNode = NodeTracker.Node;

	if (RailCloneNode == nullptr)
	{
		return false;
	}

	TimeValue CurrentTime = GetCOREInterface()->GetTime();

	IRCStaticInterface* RCStaticInterface = GetRCStaticInterface();
	if (!RCStaticInterface)
	{
		return false;
	}

	RCStaticInterface->IRCRegisterEngine();
	IRCInterface* RCInterface = GetRCInterface(RailCloneNode->GetObjectRef());
	if (!RCInterface)
	{
		return false;
	}

	RCInterface->IRCRenderBegin(CurrentTime);

	int NumInstances;
	TRCInstance* RCInstance = (TRCInstance *)RCInterface->IRCGetInstances(NumInstances);

	if (RCInstance && NumInstances > 0)
	{
		// todo: materials
		// MaterialEnum(RailCloneNode->GetMtl(), true);
		int32 NextMeshIndex = 0;


		struct FHismInstances
		{
			// todo: for forest - custom mesh node
			TUniquePtr<Mesh> MaxMesh;
			TArray<Matrix3> Transforms;
		};


		TArray<FHismInstances> InstancesForMesh;
		InstancesForMesh.Reserve(NumInstances);
		TMap<Mesh*, int32> RenderableNodeIndicesMap;
		for (int j = 0; j < NumInstances; j++, RCInstance++)
		{
			if (RCInstance && RCInstance->mesh)
			{
				if (int32* RenderableNodeIndexPtr = RenderableNodeIndicesMap.Find(RCInstance->mesh))
				{
					InstancesForMesh[*RenderableNodeIndexPtr].Transforms.Emplace(RCInstance->tm);
				}
				else
				{
					RenderableNodeIndicesMap.Add(RCInstance->mesh, InstancesForMesh.Num());
					FHismInstances& RenderableNode = InstancesForMesh.Emplace_GetRef();
					RenderableNode.MaxMesh = MakeUnique<Mesh>(*RCInstance->mesh);
					RenderableNode.Transforms.Emplace(RCInstance->tm);
				}
			}
		}

		if(!NodeTracker.DatasmithActorElement)
		{
			// note: this is how baseline exporter derives names
			FString UniqueName = FString::FromInt(NodeTracker.Node->GetHandle());
			NodeTracker.DatasmithActorElement = FDatasmithSceneFactory::CreateActor((const TCHAR*)*UniqueName);
		}

		SceneTracker.SetupActor(NodeTracker);

		int32 MeshIndex = 0;
		for (FHismInstances& Instances: InstancesForMesh)
		{
			SceneTracker.SetupDatasmithHISMForNode(NodeTracker, NodeTracker.Node, FRenderMeshForConversion(NodeTracker.Node, Instances.MaxMesh.Get(), false), NodeTracker.Node->GetMtl(), MeshIndex, Instances.Transforms);
			MeshIndex++;
		}
	}

	RCInterface->IRCClearInstances();
	RCInterface->IRCClearMeshes();
	RCInterface->IRCRenderEnd(CurrentTime);

	return true;

}

bool ConvertForest(ISceneTracker& Scene, FNodeTracker& NodeTracker, Object* Obj)
{
	INode* ForestNode = NodeTracker.Node;

	TimeValue CurrentTime = GetCOREInterface()->GetTime();

	ITreesInterface* ITrees = GetTreesInterface(ForestNode->GetObjectRef());
	ITrees->IForestRenderBegin(CurrentTime);

	int NumInstances;
	TForestInstance* ForestInstance = (TForestInstance*)ITrees->IForestGetRenderNodes(NumInstances);

	ulong ForestHandle = ForestNode->GetHandle();
	FString ForestName = ForestNode->GetName();
	Matrix3 ForestMatrix = ForestNode->GetNodeTM(CurrentTime);
	if (ForestInstance && NumInstances)
	{
		struct FHismInstances
		{
			INode* GeometryNode;
			TArray<Matrix3> Transforms;
		};

		TArray<FHismInstances> InstancesForMesh;
		InstancesForMesh.Reserve(NumInstances); // Reserve to avoid reallocations
		TMap<int, int32> RenderableNodeIndicesMap;

		for (int i = 0; i < NumInstances; i++, ForestInstance++)
		{
			if (ForestInstance->node != NULL)
			{
				int VirtualMaster = ITrees->IForestGetSpecID(i);

				if (int32* RenderableNodeIndexPtr = RenderableNodeIndicesMap.Find(VirtualMaster))
				{
					InstancesForMesh[*RenderableNodeIndexPtr].Transforms.Emplace(ForestInstance->tm);
				}
				else
				{
					RenderableNodeIndicesMap.Add(VirtualMaster, InstancesForMesh.Num());
					FHismInstances& RenderableNode = InstancesForMesh.Emplace_GetRef();

					RenderableNode.GeometryNode = ForestInstance->node;
					RenderableNode.Transforms.Emplace(ForestInstance->tm);
				}
			}
		}

		if(!NodeTracker.DatasmithActorElement)
		{
			// note: this is how baseline exporter derives names
			FString UniqueName = FString::FromInt(NodeTracker.Node->GetHandle());
			NodeTracker.DatasmithActorElement = FDatasmithSceneFactory::CreateActor((const TCHAR*)*UniqueName);
		}

		Scene.SetupActor(NodeTracker);

		int32 MeshIndex = 0;
		for (FHismInstances& Instances: InstancesForMesh)
		{
			INode* GeometryNode = Instances.GeometryNode;

			Object* GeomObj = GeometryNode->EvalWorldState(GetCOREInterface()->GetTime()).obj;
			
			FRenderMeshForConversion RenderMesh = GetMeshForGeomObject(GeometryNode, GeomObj);

			if (RenderMesh.IsValid())
			{
				Scene.SetupDatasmithHISMForNode(NodeTracker, GeometryNode, RenderMesh, NodeTracker.Node->GetMtl(), MeshIndex, Instances.Transforms);
			}
		}
	}

	ITrees->IForestClearRenderNodes();
	ITrees->IForestRenderEnd(CurrentTime);

	return true;

}

}

#include "Windows/HideWindowsPlatformTypes.h"

#endif // NEW_DIRECTLINK_PLUGIN
