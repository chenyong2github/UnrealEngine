// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	ValidationRHICommon.h: Public Valdation RHI definitions.
=============================================================================*/

#pragma once 

#ifndef ENABLE_RHI_VALIDATION
	#define ENABLE_RHI_VALIDATION	(UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT)
#endif

#include "RHI.h"
#include "RHIResources.h"
#include "RHIContext.h"
#include "DynamicRHI.h"
