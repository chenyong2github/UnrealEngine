// Copyright Epic Games, Inc. All Rights Reserved.

#include "ClothingSimulationContext.h"

//==============================================================================
// IClotingSimulationContext
//==============================================================================

IClothingSimulationContext::IClothingSimulationContext()
{}

IClothingSimulationContext::~IClothingSimulationContext()
{}

//==============================================================================
// IClotingSimulationContextBase
//==============================================================================

FClothingSimulationContextBase::FClothingSimulationContextBase()
	: DeltaSeconds(0.0f)
	, PredictedLod(0)
	, WindVelocity(FVector::ZeroVector)
	, WindAdaption(0.0f)
	, TeleportMode(EClothingTeleportMode::None)
{}

FClothingSimulationContextBase::~FClothingSimulationContextBase()
{}
