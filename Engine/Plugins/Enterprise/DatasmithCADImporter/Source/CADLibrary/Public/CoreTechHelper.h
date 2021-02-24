// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CADData.h"
#include "CADOptions.h"
#include "CoreTechTypes.h"
#include "DatasmithUtils.h"
#include "Math/Matrix.h"
#include "Math/Vector.h"
#include "MeshTypes.h"

struct FCTMesh;
struct FMeshDescription;
class IDatasmithMaterialIDElement;
class IDatasmithUEPbrMaterialElement;
class IDatasmithScene;

#define LAST_CT_MATERIAL_ID 0x00ffffff

namespace CADLibrary
{
	CADLIBRARY_API bool LoadFile(const FString& FileName, FMeshDescription& MeshDescription, const FImportParameters& ImportParameters, FMeshParameters& MeshParameters);

	CADLIBRARY_API bool ConvertCTBodySetToMeshDescription(const FImportParameters& ImportParams, const FMeshParameters& MeshParameters, FBodyMesh& Body, FMeshDescription& MeshDescription);

	CADLIBRARY_API bool Tessellate(uint64 MainObjectId, const FImportParameters& ImportParams, FMeshDescription& Mesh, FMeshParameters& MeshParameters);

	using TColorMap = TMap<uint32, uint32>;

	CADLIBRARY_API TSharedPtr<IDatasmithUEPbrMaterialElement> CreateDefaultUEPbrMaterial();
	CADLIBRARY_API TSharedPtr<IDatasmithUEPbrMaterialElement> CreateUEPbrMaterialFromColor(const FColor& InColor);
	CADLIBRARY_API TSharedPtr<IDatasmithUEPbrMaterialElement> CreateUEPbrMaterialFromMaterial(FCADMaterial& InMaterial, TSharedRef<IDatasmithScene> Scene);

	CADLIBRARY_API void CopyPatchGroups(FMeshDescription& MeshSource, FMeshDescription& MeshDestination);

} // namespace CADLibrary
