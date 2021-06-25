// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "CADData.h"
#include "CADOptions.h"
#include "MeshDescription.h"

struct FCTMesh;
class FBodyMesh;
class FCADMaterial;
class IDatasmithMaterialIDElement;
class IDatasmithUEPbrMaterialElement;
class IDatasmithScene;

#define LAST_CT_MATERIAL_ID 0x00ffffff

namespace CADLibrary
{
	using TColorMap = TMap<uint32, uint32>;

	CADLIBRARY_API TSharedPtr<IDatasmithUEPbrMaterialElement> CreateDefaultUEPbrMaterial();
	CADLIBRARY_API TSharedPtr<IDatasmithUEPbrMaterialElement> CreateUEPbrMaterialFromColor(const FColor& InColor);
	CADLIBRARY_API TSharedPtr<IDatasmithUEPbrMaterialElement> CreateUEPbrMaterialFromMaterial(FCADMaterial& InMaterial, TSharedRef<IDatasmithScene> Scene);

	CADLIBRARY_API bool ConvertBodyMeshToMeshDescription(const FImportParameters& ImportParams, const FMeshParameters& MeshParameters, FBodyMesh& Body, FMeshDescription& MeshDescription);
	CADLIBRARY_API void CopyPatchGroups(FMeshDescription& MeshSource, FMeshDescription& MeshDestination);

	/** 
	 * Enable per-triangle integer attribute named PolyTriGroups 
	 * This integer defines the identifier of the PolyTriGroup containing the triangle.
	 * In case of mesh coming from a CAD file, a PolyTriGroup is associated to a CAD topological face
	 */
	CADLIBRARY_API TPolygonAttributesRef<int32> EnableCADPatchGroups(FMeshDescription& OutMeshDescription);
	CADLIBRARY_API void GetExistingPatches(FMeshDescription& MeshDestination, TSet<int32>& OutPatchIdSet);

} // namespace CADLibrary
