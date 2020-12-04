// Copyright Epic Games, Inc. All Rights Reserved.

#include "PropertySets/PolygroupLayersProperties.h"
#include "DynamicMesh3.h"
#include "DynamicMeshAttributeSet.h"


void UPolygroupLayersProperties::InitializeGroupLayers(const FDynamicMesh3* Mesh)
{
	GroupLayersList.Add(TEXT("Default"));		// always have standard group
	if (Mesh->Attributes())
	{
		for (int32 k = 0; k < Mesh->Attributes()->NumPolygroupLayers(); k++)
		{
			FName Name = Mesh->Attributes()->GetPolygroupLayer(k)->GetName();
			GroupLayersList.Add(Name.ToString());
		}
	}

	if (GroupLayersList.Contains(ActiveGroupLayer.ToString()) == false)		// discard restored value if it doesn't apply
	{
		ActiveGroupLayer = FName(GroupLayersList[0]);
	}
}


bool UPolygroupLayersProperties::HasSelectedPolygroup() const
{
	return ActiveGroupLayer != FName(GroupLayersList[0]);
}


void UPolygroupLayersProperties::SetSelectedFromPolygroupIndex(int32 Index)
{
	if (Index < 0)
	{
		ActiveGroupLayer = FName(GroupLayersList[0]);
	}
	else
	{
		ActiveGroupLayer = FName(GroupLayersList[Index+1]);
	}
}