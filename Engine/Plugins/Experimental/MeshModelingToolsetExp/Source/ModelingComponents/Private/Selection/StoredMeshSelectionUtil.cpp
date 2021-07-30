// Copyright Epic Games, Inc. All Rights Reserved.

#include "Selection/StoredMeshSelectionUtil.h"
#include "Selection/GroupTopologyStorableSelection.h"
#include "TargetInterfaces/PrimitiveComponentBackedTarget.h"
#include "ToolTargetManager.h"


const UInteractiveToolStorableSelection* UE::Geometry::GetCurrentToolInputSelection(const FToolBuilderState& SceneState, UToolTarget* Target)
{
	const UInteractiveToolStorableSelection* Selection = SceneState.StoredToolSelection;
	if (Selection == nullptr)
	{
		return nullptr;
	}

	IPrimitiveComponentBackedTarget* TargetInterface = Cast<IPrimitiveComponentBackedTarget>(Target);
	if (TargetInterface == nullptr)
	{
		return nullptr;
	}

	const UGroupTopologyStorableSelection* GroupTopoSelection = Cast<UGroupTopologyStorableSelection>(Selection);
	if (GroupTopoSelection == nullptr || GroupTopoSelection->IdentifyingInfo.ComponentTarget != TargetInterface->GetOwnerComponent())
	{
		return nullptr;
	}

	return Selection;
}



bool UE::Geometry::GetStoredSelectionAsTriangles(
	const UInteractiveToolStorableSelection* Selection,
	const FDynamicMesh3& Mesh,
	TArray<int32>& TrianglesOut)
{
	const UGroupTopologyStorableSelection* GroupTopoSelection = Cast<UGroupTopologyStorableSelection>(Selection);
	if (GroupTopoSelection == nullptr)
	{
		return false;
	}

	// don't support UV selection currently - unclear how reproducible this is?
	if (GroupTopoSelection->IdentifyingInfo.TopologyType == UGroupTopologyStorableSelection::ETopologyType::FUVGroupTopology)
	{
		return false;
	}

	if (GroupTopoSelection->GetGroupIDs().Num() == 0)
	{
		return false;
	}

	if (GroupTopoSelection->IdentifyingInfo.TopologyType == UGroupTopologyStorableSelection::ETopologyType::FGroupTopology)
	{
		TSet<int32> SelectedGroups;
		for (int32 gid : GroupTopoSelection->GetGroupIDs())
		{
			SelectedGroups.Add(gid);
		}

		for (int32 tid : Mesh.TriangleIndicesItr())
		{
			int32 gid = Mesh.GetTriangleGroup(tid);
			if (SelectedGroups.Contains(gid))
			{
				TrianglesOut.Add(tid);
			}
		}
	}
	else if (GroupTopoSelection->IdentifyingInfo.TopologyType == UGroupTopologyStorableSelection::ETopologyType::FTriangleGroupTopology)
	{
		for (int gid : GroupTopoSelection->GetGroupIDs())
		{
			if (Mesh.IsTriangle(gid))
			{
				TrianglesOut.Add(gid);
			}
		}
	}

	return true;
}