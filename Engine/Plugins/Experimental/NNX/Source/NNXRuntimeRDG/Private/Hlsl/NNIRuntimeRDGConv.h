// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NNXRuntimeRDG.h"
#include "NNXRuntimeHLSLOp.h"

namespace UE::NNIRuntimeRDG::Private::Hlsl
{
	bool RegisterConvOperator(NNX::FMLOperatorRegistryHlsl& Registry);
} // UE::NNIRuntimeRDG::Private::Hlsl
