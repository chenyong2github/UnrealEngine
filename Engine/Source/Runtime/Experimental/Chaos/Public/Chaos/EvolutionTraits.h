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
}
