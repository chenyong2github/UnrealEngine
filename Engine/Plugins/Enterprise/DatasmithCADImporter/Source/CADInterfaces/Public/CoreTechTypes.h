// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once


#ifdef CAD_INTERFACE

#include "CADOptions.h"
#include "Containers/Array.h"
#include "Math/Vector.h"
#include "Math/Vector2D.h"

#pragma warning(push)
#pragma warning(disable:4996) // unsafe sprintf
#pragma warning(disable:4828) // illegal character
#include "kernel_io/kernel_io.h"
#include "kernel_io/kernel_io_error.h"
#include "kernel_io/kernel_io_type.h"
#include "kernel_io/list_io/list_io.h"
#include "kernel_io/material_io/material_io.h"
#include "kernel_io/object_io/asm_io/component_io/component_io.h"
#include "kernel_io/object_io/asm_io/instance_io/instance_io.h"
#include "kernel_io/object_io/geom_io/curve_io/cnurbs_io/cnurbs_io.h"
#include "kernel_io/object_io/geom_io/surface_io/snurbs_io/snurbs_io.h"
#include "kernel_io/object_io/geom_io/surface_io/surface_io.h"
#include "kernel_io/object_io/history_io/hnode_io/hbranch_io/hbranch_io.h"
#include "kernel_io/object_io/history_io/hnode_io/hleaf_io/hleaf_io.h"
#include "kernel_io/object_io/history_io/solid_io/solid_io.h"
#include "kernel_io/object_io/topo_io/body_io/body_io.h"
#include "kernel_io/object_io/topo_io/coedge_io/coedge_io.h"
#include "kernel_io/object_io/topo_io/face_io/face_io.h"
#include "kernel_io/object_io/topo_io/loop_io/loop_io.h"
#include "kernel_io/repair_io/repair_io.h"
#pragma warning(pop)


namespace CADLibrary
{

	struct CTMesh
	{
		TArray<uint32> Materials; //GPure Material Id
		TArray<uint32> MaterialUUIDs; //Material Hash from color value
		TArray<FVector> Vertices;
		TArray<FVector> Normals;
		TArray<FVector2D> TexCoords;
		TArray<int32_t> Indices;
		TArray<uint32> TriangleMaterials;
	};

	/**
>>>> ORIGINAL //UE4/Release-4.24/Engine/Plugins/Enterprise/DatasmithCADImporter/Source/CADInterfaces/Public/CoreTechTypes.h#2
	 * Mandatory: Kernel_IO has to be initialize with the final MetricUnit, set function doesn't work
==== THEIRS //UE4/Release-4.24/Engine/Plugins/Enterprise/DatasmithCADImporter/Source/CADInterfaces/Public/CoreTechTypes.h#4
	 * Mandatory: Kernel_IO has to be initialize with the final MetricUnit, set function doesn't work
	 * This methode set Tolerance to 0.00001 m whether 0.01 mm
==== YOURS //David.Lesage_YUL-Release-4.24/Engine/Plugins/Enterprise/DatasmithCADImporter/Source/CADInterfaces/Public/CoreTechTypes.h
	 * Mandatory: Kernel_IO has to be initialize with the final MetricUnit, set function doesn`t work
<<<<
	 * @param MetricUnit: Length unit express in meter
	 */
	CADINTERFACES_API CT_IO_ERROR CTKIO_InitializeKernel(double MetricUnit, const TCHAR* = TEXT(""));
	CADINTERFACES_API CT_IO_ERROR CTKIO_ShutdownKernel();

	CADINTERFACES_API CT_IO_ERROR CTKIO_UnloadModel();
	CADINTERFACES_API CT_IO_ERROR CTKIO_AskMainObject(CT_OBJECT_ID& OutMainObjectId);
	CADINTERFACES_API CT_IO_ERROR CTKIO_ChangeTolerance(double Precision);
	CADINTERFACES_API CT_IO_ERROR CTKIO_AskUnit(double Unit);
	CADINTERFACES_API CT_IO_ERROR CTKIO_AskTolerance(double tolerance);
	CADINTERFACES_API CT_IO_ERROR CTKIO_AskModelSize(double model_size);
	CADINTERFACES_API CT_IO_ERROR CTKIO_AskTesselationParameters(
		double      max_face_sag,      
		double      max_face_length,   
		double      max_face_angle,    
		double      max_curve_sag,     
		double      max_curve_length,  
		double      max_curve_angle,   
		CT_LOGICAL&        high_quality,
		CT_TESS_DATA_TYPE& vertex_type, 
		CT_TESS_DATA_TYPE& normal_type, 
		CT_TESS_DATA_TYPE& texture_type 
	);

	CADINTERFACES_API CT_IO_ERROR CTKIO_AskNbObjectsType
	(
		CT_UINT32&     object_count,            /*!< [out] Number of objects found. Type \c CT_UINT. */
		CT_OBJECT_TYPE type = CT_OBJECT_ALL_TYPE /*!< [in] [optional] Searched type. Type \c CT_OBJECT_TYPE. */
	);

	CADINTERFACES_API CT_IO_ERROR CTKIO_ChangeTesselationParameters(
		double     max_sag,                           
		double     max_length,                        
		double     max_angle,                         
		CT_LOGICAL        high_quality = CT_FALSE,     
		CT_TESS_DATA_TYPE vertex_type = CT_TESS_USE_DEFAULT, 
		CT_TESS_DATA_TYPE normal_type = CT_TESS_USE_DEFAULT, 
		CT_TESS_DATA_TYPE texture_type = CT_TESS_USE_DEFAULT 
	);

	CADINTERFACES_API CT_IO_ERROR CTKIO_LoadFile
	(
		const TCHAR*  file_name,                             
		CT_OBJECT_ID& main_object,                           
		CT_FLAGS      load_flags = CT_LOAD_FLAGS_USE_DEFAULT,
		uint32        lod = 0,                               
		const TCHAR*  string_option = TEXT("")               
	);

	CADINTERFACES_API CT_IO_ERROR CTKIO_SaveFile
	(
		CT_LIST_IO&        objects_list_to_save,
		const TCHAR*       file_name,           
		const TCHAR*       format,              
		const CT_OBJECT_ID coordsystem = 0      
	);

	/**
	 * This function calls, according to the chosen EStitchingTechnique, Kernel_io CT_REPAIR_IO::Sew or CT_REPAIR_IO::Heal. In case of sew, the used tolerance is 100x the geometric tolerance (SewingToleranceFactor = 100).
	 * With the case of UE-83379, Alias file, this value is too big (biggest than the geometric features. So Kernel_io hangs during the sew process... In the wait of more test, 100x is still the value used for CAD import except for Alias where the value of the SewingToleranceFactor is set to 1x
	 * @param SewingToleranceFactor Factor apply to the tolerance 3D to define the sewing tolerance.
	 */
	CADINTERFACES_API CT_IO_ERROR Repair(CT_OBJECT_ID MainObjectID, EStitchingTechnique StitchingTechnique, CT_DOUBLE SewingToleranceFactor = 100.);
	CADINTERFACES_API CT_IO_ERROR SetCoreTechTessellationState(const FImportParameters& ImportParams);


	struct CADINTERFACES_API CheckedCTError
	{
		CheckedCTError(CT_IO_ERROR in_e = CT_IO_ERROR::IO_OK, bool in_otherError = false) : e(in_e), OtherError(in_otherError) { Validate(); }
		CheckedCTError& operator=(CT_IO_ERROR in_e) { e = in_e; Validate(); return *this; }
		CheckedCTError& operator=(const CheckedCTError& in_e) { new (this) CheckedCTError(in_e.e, in_e.OtherError); return *this; }
		operator bool() const { return e == CT_IO_ERROR::IO_OK && OtherError == false; }
		operator CT_IO_ERROR() const { return e; }
		void RaiseOtherError(const char* msg) { OtherErrorMsg = msg; OtherError = true; Validate(); }
		CT_IO_ERROR e;

	private:
		void Validate();
		bool OtherError;
		const char* OtherErrorMsg = nullptr;
	};

	class CADINTERFACES_API CoreTechSessionBase
	{
	public:
		/**
		 * Make sure CT is initialized, and a main object is ready.
		 * Handle input file unit and an output unit
		 * @param FileMetricUnit number of meters per file unit.
		 * eg. For a file in inches, arg should be 0.0254
		 */
		CoreTechSessionBase(const TCHAR* Owner, double Unit);
		bool IsSessionValid() { return Owner != nullptr && MainObjectId != 0; }
		virtual ~CoreTechSessionBase();

	protected:
		CT_OBJECT_ID MainObjectId;

	private:
		static const TCHAR* Owner;
	};
}

#endif // CAD_INTERFACE

