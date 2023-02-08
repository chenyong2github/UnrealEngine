// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "CoreMinimal.h"
#include "NNECoreOperator.h"

namespace UE::NNECore::Internal { class FTensor; }

namespace UE::NNERuntimeRDG::Internal::ElementWiseBinaryCPUHelper
{
	void NNERUNTIMERDG_API Apply(NNECore::Internal::EElementWiseBinaryOperatorType OpType, const NNECore::Internal::FTensor& LHSTensor, const NNECore::Internal::FTensor& RHSTensor, NNECore::Internal::FTensor& OutputTensor);
	
} // UE::NNERuntimeRDG::Internal::ElementWiseBinaryCPUHelper