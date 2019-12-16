// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#ifdef CAD_LIBRARY

#include "CADData.h"
#include "CADOptions.h"
#include "CoreTechTypes.h"
#include "DatasmithUtils.h"
#include "Math/Matrix.h"
#include "Math/Vector.h"
#include "MeshTypes.h"

struct CTMesh;
struct FMeshDescription;
class IDatasmithMaterialIDElement;
class IDatasmithUEPbrMaterialElement;
class IDatasmithScene;

#define LAST_CT_MATERIAL_ID 0x00ffffff

namespace CADLibrary
{
	struct DbgState
	{
		CT_OBJECT_ID      main_object;
		CT_DOUBLE         max_face_sag;     // Maximum sag for face(in current unit)
		CT_DOUBLE         max_face_length;  // Maximum length for face(in current unit)
		CT_DOUBLE         max_face_angle;   // Maximum angle for face(in degree)
		CT_DOUBLE         max_curve_sag;    // Maximum sag for curve(in current unit)
		CT_DOUBLE         max_curve_length; // Maximum length for curve(in current unit)
		CT_DOUBLE         max_curve_angle;  // Maximum angle for curve(in degree)S
		CT_LOGICAL        high_quality;     // High quality flag
		CT_TESS_DATA_TYPE vertex_type;      // Vertex type
		CT_TESS_DATA_TYPE normal_type;      // Normal type
		CT_TESS_DATA_TYPE texture_type;     // Texture type
		double            model_size;       // Model size (in current unit)
		double            tolerance;        // Tolerance (in current unit)
		double            unit;             // Unit (in meter)

		DbgState()
		{
			CTKIO_AskMainObject(main_object);
			CTKIO_AskTesselationParameters(max_face_sag, max_face_length, max_face_angle, max_curve_sag, max_curve_length, max_curve_angle, high_quality, vertex_type, normal_type, texture_type);
			CTKIO_AskModelSize(model_size);
			CTKIO_AskTolerance(tolerance);
			CTKIO_AskUnit(unit);
		}
	};

	uint32 GetSize(CT_TESS_DATA_TYPE type);

	CADLIBRARY_API bool ConvertCTBodySetToMeshDescription(const FImportParameters& ImportParams, const FMeshParameters& MeshParameters, FBodyMesh& Body, FMeshDescription& MeshDescription);

	CADLIBRARY_API CT_IO_ERROR Tessellate(CT_OBJECT_ID MainObjectId, const FImportParameters& ImportParams, FMeshDescription& Mesh, FMeshParameters& MeshParameters);

	using TColorMap = TMap<uint32, uint32>;

	CADLIBRARY_API TSharedPtr<IDatasmithUEPbrMaterialElement> CreateDefaultUEPbrMaterial();
	CADLIBRARY_API TSharedPtr<IDatasmithUEPbrMaterialElement> CreateUEPbrMaterialFromColor(const FColor& InColor);
	CADLIBRARY_API TSharedPtr<IDatasmithUEPbrMaterialElement> CreateUEPbrMaterialFromMaterial(FCADMaterial& InMaterial, TSharedRef<IDatasmithScene> Scene);

} // namespace CADLibrary

#endif // CAD_LIBRARY
