// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/PBDRigidsEvolution2.h"
namespace Chaos
{
class FChaosArchive;

struct FConstraintHack
{};

template<typename T, int d>
class TPBDRigidsEvolutionGBF2 : public TPBDRigidsEvolutionBase2<TPBDRigidsEvolutionGBF2<T, d>, FConstraintHack, T, d>
{
public:
	typedef TPBDRigidsEvolutionBase2<TPBDRigidsEvolutionGBF2<T, d>, FConstraintHack, T, d> Base;

	using TPBDRigidsEvolutionBase2<TPBDRigidsEvolutionGBF2<T, d>, FConstraintHack, T, d>::DynamicAwakeParticles;
	using TPBDRigidsEvolutionBase2<TPBDRigidsEvolutionGBF2<T, d>, FConstraintHack, T, d>::ForceRules;
	using TPBDRigidsEvolutionBase2<TPBDRigidsEvolutionGBF2<T, d>, FConstraintHack, T, d>::ParticleUpdatePosition;
	using TPBDRigidsEvolutionBase2<TPBDRigidsEvolutionGBF2<T, d>, FConstraintHack, T, d>::SetParticleUpdateVelocityFunction;
	using TPBDRigidsEvolutionBase2<TPBDRigidsEvolutionGBF2<T, d>, FConstraintHack, T, d>::SetParticleUpdatePositionFunction;

	static constexpr int32 DefaultNumIterations = 1;
	static constexpr int32 DefaultNumPushOutIterations = 5;
	static constexpr int32 DefaultNumPushOutPairIterations = 2;

	typedef typename TPBDRigidsEvolutionBase2<TPBDRigidsEvolutionGBF2<T, d>, FConstraintHack, T, d>::FForceRule FForceRule;

	CHAOS_API TPBDRigidsEvolutionGBF2(int32 InNumIterations = DefaultNumIterations);
	CHAOS_API ~TPBDRigidsEvolutionGBF2() {}

	CHAOS_API void Integrate(const T dt);
	CHAOS_API void AdvanceOneTimeStep(const T dt);

protected:
	using Base::UpdateConstraintPositionBasedState;
	using Base::CreateConstraintGraph;
	using Base::CreateIslands;
	using Base::ConstraintGraph;
	using Base::ApplyConstraints;
	using Base::UpdateVelocities;
	using Base::ApplyPushOut;
};
}
