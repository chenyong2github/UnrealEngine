// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

namespace Chaos
{
	template <typename Traits>
	class TPBDRigidsEvolutionBase;

	template <typename Traits>
	class TPBDRigidsEvolutionGBF;

	using FDefaultTraits = struct FRewindableEvolutionTraits;

	//The default evolution used by unreal
	using FPBDRigidsEvolution = TPBDRigidsEvolutionGBF<FDefaultTraits>;

	template <typename Traits>
	class TPBDRigidsSolver;

	using FPBDRigidsSolver = TPBDRigidsSolver<FDefaultTraits>;

	template <typename Traits>
	class TEventManager;

	using FEventManager = TEventManager<FDefaultTraits>;
}

class FGeometryCollectionPhysicsProxy;