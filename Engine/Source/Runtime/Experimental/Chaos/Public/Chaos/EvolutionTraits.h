// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

namespace Chaos
{

struct FRewindableEvolutionTraits
{
	static constexpr bool IsRewindable(){ return true; }
};

struct FNonRewindableEvolutionTraits
{
	static constexpr bool IsRewindable(){ return false; }
};

#define EVOLUTION_TRAIT(Trait) Trait,
enum class ETraits
{
#include "Chaos/EvolutionTraits.inl"
	NumTraits
};
#undef EVOLUTION_TRAIT

template <typename T>
inline constexpr ETraits TraitToIdx(){ return ETraits::NumTraits; }
#define EVOLUTION_TRAIT(Trait) template <> inline constexpr ETraits TraitToIdx<Trait>(){ return ETraits::Trait; }
#include "Chaos/EvolutionTraits.inl"
#undef EVOLUTION_TRAIT
}
