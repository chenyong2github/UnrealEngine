// Copyright Epic Games, Inc. All Rights Reserved.

#include "MLDeformerModule.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

// Log category.
MLDEFORMERFRAMEWORK_API DEFINE_LOG_CATEGORY(LogMLDeformer)

#define LOCTEXT_NAMESPACE "MLDeformerModule"

IMPLEMENT_MODULE(UE::MLDeformer::FMLDeformerModule, MLDeformerFramework)

#undef LOCTEXT_NAMESPACE
