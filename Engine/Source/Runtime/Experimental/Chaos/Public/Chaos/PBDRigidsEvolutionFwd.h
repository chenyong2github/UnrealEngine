// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

namespace Chaos
{
	template <typename Traits>
	class TPBDRigidsEvolutionBase;

	template <typename Traits>
	class TPBDRigidsEvolutionGBF;

	//The default evolution used by unreal
	using FPBDRigidsEvolution = TPBDRigidsEvolutionGBF<struct FNonRewindableEvolutionTraits>;
}