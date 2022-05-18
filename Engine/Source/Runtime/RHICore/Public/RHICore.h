// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Logging/LogMacros.h"

RHICORE_API DECLARE_LOG_CATEGORY_EXTERN(LogRHICore, Log, VeryVerbose);

class IRHICommandContext;

struct FRHIRenderPassInfo;

namespace UE
{
namespace RHICore
{

RHICORE_API void ResolveRenderPassTargets(IRHICommandContext& Context, const FRHIRenderPassInfo& Info);

} //! RHICore
} //! UE