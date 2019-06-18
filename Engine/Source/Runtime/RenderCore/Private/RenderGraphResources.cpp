// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RenderGraphResources.h"
#include "RenderGraphPass.h"

FRDGResourceState FRDGResourceState::CreateRead(const FRDGPass* Pass)
{
	check(Pass);

	FRDGResourceState State;
	State.Pass = Pass;
	State.Pipeline = Pass->IsCompute() ? EPipeline::Compute : EPipeline::Graphics;
	State.Access = FRDGResourceState::EAccess::Read;
	return State;
}

FRDGResourceState FRDGResourceState::CreateWrite(const FRDGPass* Pass)
{
	check(Pass);

	FRDGResourceState State;
	State.Pass = Pass;
	State.Pipeline = Pass->IsCompute() ? EPipeline::Compute : EPipeline::Graphics;
	State.Access = FRDGResourceState::EAccess::Write;
	return State;
}