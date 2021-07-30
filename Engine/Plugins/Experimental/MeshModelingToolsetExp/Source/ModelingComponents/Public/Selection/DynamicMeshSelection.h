// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace UE {
namespace Geometry {

class FDynamicMesh3;

/**
 * Class that represents a selection in a dynamic mesh.
 */
class FDynamicMeshSelection
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

	bool IsEmpty() const
	{
		return SelectedIDs.IsEmpty();
	}

	bool operator==(const FDynamicMeshSelection& Other)
	{
		return Mesh == Other.Mesh
			&& Type == Other.Type
			&& (Type != EType::Group || GroupLayer == Other.GroupLayer)
			&& SelectedIDs.Num() == Other.SelectedIDs.Num()
			&& SelectedIDs.Includes(Other.SelectedIDs);
	}

};

}}// end namespace UE::Geometry