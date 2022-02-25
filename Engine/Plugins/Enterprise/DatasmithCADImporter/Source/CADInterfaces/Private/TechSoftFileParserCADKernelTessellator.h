// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "TechSoftFileParser.h"

namespace CADKernel
{
class FBody;
class FSession;
}

namespace CADLibrary
{

class CADINTERFACES_API FTechSoftFileParserCADKernelTessellator : public FTechSoftFileParser
{
public:

	/**
	 * @param InCADData TODO
	 * @param EnginePluginsPath Full Path of EnginePlugins. Mandatory to set KernelIO to import DWG, or DGN files
	 */
	FTechSoftFileParserCADKernelTessellator(FCADFileData& InCADData, const FString& EnginePluginsPath = TEXT(""));

#ifdef USE_TECHSOFT_SDK

private:

	virtual A3DStatus AdaptBRepModel() override;

	virtual void SewModel() override
	{
		// Do in GenerateBodyMeshes
	}

	// Tessellation methods
	virtual void GenerateBodyMesh(A3DRiRepresentationItem* Representation, FArchiveBody& Body) override;
	void MeshAndGetTessellation(CADKernel::FSession& CADKernelSession, FArchiveBody& ArchiveBody, CADKernel::FBody& CADKernelBody);

#endif

private:

	FCadId LastEntityId = 1;
	int32 LastHostIdUsed = 0;

};

} // ns CADLibrary