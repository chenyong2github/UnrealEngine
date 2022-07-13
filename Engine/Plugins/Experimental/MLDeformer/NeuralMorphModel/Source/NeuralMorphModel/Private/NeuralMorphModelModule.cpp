// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralMorphModelModule.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

NEURALMORPHMODEL_API DEFINE_LOG_CATEGORY(LogNeuralMorphModel)

#define LOCTEXT_NAMESPACE "NeuralMorphModelModule"

IMPLEMENT_MODULE(UE::NeuralMorphModel::FNeuralMorphModelModule, NeuralMorphModel)

#undef LOCTEXT_NAMESPACE
