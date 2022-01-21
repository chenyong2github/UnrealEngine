// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace UE {
namespace Geometry {

class FDynamicMesh3;

/**
 * Class that represents a selection in a dynamic mesh, for use in the UV editor.
 */
class UVEDITORTOOLS_API FUVEditorDynamicMeshSelection
{
public:
	enum class EType
	{
		Vertex,
		Edge,
		Triangle,
		Group
	};

	const FDynamicMesh3* Mesh = nullptr;
	TSet<int32> SelectedIDs;
	EType Type = EType::Vertex;

	// Not relevant if the selection type is not Group
	int32 GroupLayer = 0;

	// Can be used to discard the selection if topology of the mesh
	// has changed (to avoid the risk of referencing elements that may
	// have been deleted)
	int32 TopologyTimestamp = -1;

	/** Checks whether the selection's timestamp still matches the meshes topology timestamp. */
	bool MatchesTimestamp() const
	{
		return Mesh && Mesh->GetTopologyChangeStamp() == TopologyTimestamp;
	}

	bool IsEmpty() const
	{
		return SelectedIDs.IsEmpty();
	}

	bool operator==(const FUVEditorDynamicMeshSelection& Other)
	{
		return Mesh == Other.Mesh
			&& Type == Other.Type
			&& TopologyTimestamp == Other.TopologyTimestamp
			&& (Type != EType::Group || GroupLayer == Other.GroupLayer)
			&& SelectedIDs.Num() == Other.SelectedIDs.Num()
			&& SelectedIDs.Includes(Other.SelectedIDs);
	}

	bool operator!=(const FUVEditorDynamicMeshSelection& Other)
	{
		return !(*this == Other);
	}
};

}}// end namespace UE::Geometry