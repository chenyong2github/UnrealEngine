// Copyright Epic Games, Inc. All Rights Reserved.

#include "VertexDeltaModelModule.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

VERTEXDELTAMODEL_API DEFINE_LOG_CATEGORY(LogVertexDeltaModel)

#define LOCTEXT_NAMESPACE "VertexDeltaModelModule"

IMPLEMENT_MODULE(UE::VertexDeltaModel::FVertexDeltaModelModule, VertexDeltaModel)

namespace UE::VertexDeltaModel
{
}	// namespace UE::VertexDeltaModel

#undef LOCTEXT_NAMESPACE
