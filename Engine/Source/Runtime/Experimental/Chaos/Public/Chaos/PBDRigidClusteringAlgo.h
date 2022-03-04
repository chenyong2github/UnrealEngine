// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/PBDRigidClustering.h"

namespace Chaos
{
	void UpdateClusterMassProperties(
		FPBDRigidClusteredParticleHandle* Parent,
		TSet<FPBDRigidParticleHandle*>& Children,
		FMatrix33& ParentInertia,
		const FRigidTransform3* ForceMassOrientation = nullptr);

	void CHAOS_API UpdateKinematicProperties(
		FPBDRigidParticleHandle* Parent,
		FRigidClustering::FClusterMap&,
		FRigidClustering::FRigidEvolution&);

	void CHAOS_API UpdateGeometry(
		Chaos::FPBDRigidClusteredParticleHandle* Parent,
		const TSet<FPBDRigidParticleHandle*>& Children,
		TSharedPtr<Chaos::FImplicitObject, ESPMode::ThreadSafe> ProxyGeometry,
		const FClusterCreationParameters& Parameters);

} // namespace Chaos
