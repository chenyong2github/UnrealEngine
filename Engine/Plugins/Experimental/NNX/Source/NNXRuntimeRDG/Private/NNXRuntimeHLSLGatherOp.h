// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NNXRuntimeRDG.h"
#include "NNXRuntimeHLSLOp.h"

namespace UE::NNI::RuntimeRDG::Hlsl::Private
{
	bool RegisterGatherOperator(NNX::FMLOperatorRegistryHlsl& Registry);
} // UE::NNI::RuntimeRDG::Hlsl::Private
