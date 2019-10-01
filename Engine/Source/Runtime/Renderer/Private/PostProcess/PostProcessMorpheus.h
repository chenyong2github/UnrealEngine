// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ScreenPass.h"

#if defined(HAS_MORPHEUS) && HAS_MORPHEUS

struct FHMDDistortionInputs;

FScreenPassTexture AddMorpheusDistortionPass(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FHMDDistortionInputs& Inputs);

#endif
