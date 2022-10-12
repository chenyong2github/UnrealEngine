// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NNXRuntime.h"
#include "NNXRuntimeRDG.h"

namespace NNX
{
	/**
	 * Base class for all Hlsl ML operators
	 */
	struct FMLOperatorHlsl : public FMLOperatorRDG
	{
		virtual ~FMLOperatorHlsl() = default;
		virtual bool Initialize(TArrayView<const FMLTensorDesc> InputTensors, TArrayView<const FMLTensorDesc> OutputTensors, const FMLAttributeMap& Attributes) = 0;
		virtual void Dispatch(FRDGBuilder& GraphBuilder, TArrayView<const FMLTensorBinding> InInputBindings, TArrayView<const FMLTensorBinding> OutOutputBindings) = 0;
	};

	/**
	  * HLSL ML operator registry
	*/
	typedef TMLOperatorRegistryRDG<FMLOperatorHlsl> FMLOperatorRegistryHlsl;

} // NNX