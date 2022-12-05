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
		virtual bool Initialize(TConstArrayView<FTensorDesc> InputTensorDescs, TConstArrayView<FTensorDesc> OutputTensorDescs, const UE::NNECore::FAttributeMap& Attributes) = 0;
		virtual void Dispatch(FRDGBuilder& GraphBuilder, TConstArrayView<FTensorRDGRef> InInputTensors, TConstArrayView<FTensorRDGRef> InOutputTensors) = 0;
		virtual int ComputeOutputShape(TConstArrayView<FTensorShape> InputShapes, TArray<FTensorShape>& OutputShapes) const { return -1; };
	};

	/**
	  * HLSL ML operator registry
	*/
	typedef TOperatorRegistryRDG<FMLOperatorHlsl> FMLOperatorRegistryHlsl;
	typedef TModelValidatorRDG<FMLOperatorHlsl> FModelValidatorHlsl;

} // NNX