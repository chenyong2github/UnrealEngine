// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "TargetInterfaces/DynamicMeshCommitter.h"

#include "GeometryBase.h"

PREDECLARE_GEOMETRY(class FDynamicMesh3);
class IMeshDescriptionCommitter;
class IMeshDescriptionProvider;

namespace UE {
namespace Geometry {

	MODELINGCOMPONENTS_API TSharedPtr<FDynamicMesh3, ESPMode::ThreadSafe> GetDynamicMeshViaMeshDescription(
		IMeshDescriptionProvider& MeshDescriptionProvider);

	MODELINGCOMPONENTS_API void CommitDynamicMeshViaMeshDescription(IMeshDescriptionCommitter& MeshDescriptionCommitter,
		const UE::Geometry::FDynamicMesh3& Mesh, const IDynamicMeshCommitter::FDynamicMeshCommitInfo& CommitInfo);

}}