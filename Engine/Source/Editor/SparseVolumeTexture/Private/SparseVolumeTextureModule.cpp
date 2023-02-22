// Copyright Epic Games, Inc. All Rights Reserved.

#include "SparseVolumeTextureModule.h"

#include "SparseVolumeTextureOpenVDB.h"

#define LOCTEXT_NAMESPACE "SparseVolumeTextureModule"

IMPLEMENT_MODULE(FSparseVolumeTextureModule, SparseVolumeTexture);

void FSparseVolumeTextureModule::StartupModule()
{
#if PLATFORM_WINDOWS
	// Global registration of  the vdb types.
	openvdb::initialize();
#endif
}

void FSparseVolumeTextureModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE
