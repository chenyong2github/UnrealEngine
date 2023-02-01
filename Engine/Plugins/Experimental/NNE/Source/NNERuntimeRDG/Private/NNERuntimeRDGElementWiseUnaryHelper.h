// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "NNECoreOperator.h"

namespace UE::NNERuntimeRDG::Private::ElementWiseUnaryCPUHelper
{
	template<NNECore::Internal::EElementWiseUnaryOperatorType OpType> float Apply(float X, float Alpha, float Beta, float Gamma);
	
} // UE::NNERuntimeRDG::Private::ElementWiseUnaryCPUHelper
