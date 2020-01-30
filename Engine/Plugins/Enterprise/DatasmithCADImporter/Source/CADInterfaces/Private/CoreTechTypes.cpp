// Copyright Epic Games, Inc. All Rights Reserved.
#include "CoreTechTypes.h"

#include "Misc/Paths.h"

#ifdef CAD_INTERFACE

const char* CoreTechLicenseKey =
#include "CoreTech.lic"
;

namespace CADLibrary
{
	// Note: CTKIO_* functions are not functionally useful.
	// This wrapping allows a correct profiling of the CT API.

	CT_IO_ERROR CTKIO_InitializeKernel(double Unit, const TCHAR* EnginePluginsPath)
	{
		FString KernelIOPath;
		if (FCString::Strlen(EnginePluginsPath))
		{
			KernelIOPath = FPaths::Combine(EnginePluginsPath, TEXT(KERNEL_IO_PLUGINSPATH));
			KernelIOPath = FPaths::ConvertRelativePathToFull(KernelIOPath);
			if (!FPaths::DirectoryExists(KernelIOPath))
			{
				KernelIOPath.Empty();
			}
		}

		CT_STR appName = CoreTechLicenseKey;
		return CT_KERNEL_IO::InitializeKernel(appName, Unit, 0.00001 / Unit, *KernelIOPath);
	}

	CT_IO_ERROR CTKIO_ShutdownKernel()
	{
		return CT_KERNEL_IO::ShutdownKernel(); // ignorable. Just in case CT was not previously stopped
	}

	CT_IO_ERROR CTKIO_UnloadModel()
	{
		return CT_KERNEL_IO::UnloadModel();
	}

	CT_IO_ERROR CTKIO_AskNbObjectsType(CT_UINT32& object_count, CT_OBJECT_TYPE type)
	{
		return CT_KERNEL_IO::AskNbObjectsType(object_count, type);
	}

	CT_IO_ERROR CTKIO_AskMainObject(CT_OBJECT_ID& OutMainObjectId)
	{
		return CT_KERNEL_IO::AskMainObject(OutMainObjectId);
	}

	CT_IO_ERROR CTKIO_ChangeTolerance(double Precision)
	{
		return CT_KERNEL_IO::ChangeTolerance(Precision);
	}

	CT_IO_ERROR CTKIO_AskUnit(double Unit)
	{
		return CT_KERNEL_IO::AskUnit(Unit);
	}

	CT_IO_ERROR CTKIO_ChangeTesselationParameters(double max_sag, double max_length, double max_angle, CT_LOGICAL high_quality, CT_TESS_DATA_TYPE vertex_type, CT_TESS_DATA_TYPE normal_type, CT_TESS_DATA_TYPE texture_type)
	{
		return CT_KERNEL_IO::ChangeTesselationParameters(max_sag, max_length, max_angle, high_quality, vertex_type, normal_type, texture_type);
	}

	CT_IO_ERROR CTKIO_LoadFile(const TCHAR* FileName, CT_OBJECT_ID& MainObject, CT_FLAGS LoadFlags, uint32 Lod, const TCHAR* StringOption)
	{
		return CT_KERNEL_IO::LoadFile(FileName, MainObject, LoadFlags, Lod, StringOption);
	}

	CT_IO_ERROR CTKIO_SaveFile(CT_LIST_IO& objects_list_to_save, const TCHAR* file_name, const TCHAR* format, const CT_OBJECT_ID coordsystem)
	{
		return CT_KERNEL_IO::SaveFile(objects_list_to_save, file_name, format, coordsystem);
	}

	CT_IO_ERROR CTKIO_AskTolerance(double tolerance)
	{
		return CT_KERNEL_IO::AskTolerance(tolerance);
	}

	CT_IO_ERROR CTKIO_AskModelSize(double model_size)
	{
		return CT_KERNEL_IO::AskModelSize(model_size);
	}

	CT_IO_ERROR CTKIO_AskTesselationParameters(double max_face_sag, double max_face_length, double max_face_angle, double max_curve_sag, double max_curve_length, double max_curve_angle, CT_LOGICAL& high_quality, CT_TESS_DATA_TYPE& vertex_type, CT_TESS_DATA_TYPE& normal_type, CT_TESS_DATA_TYPE& texture_type)
	{
		return CT_KERNEL_IO::AskTesselationParameters(max_face_sag, max_face_length, max_face_angle, max_curve_sag, max_curve_length, max_curve_angle, high_quality, vertex_type, normal_type, texture_type);
	}

	CT_IO_ERROR Repair(CT_OBJECT_ID MainObjectID, EStitchingTechnique StitchingTechnique, CT_DOUBLE SewingToleranceFactor)
	{
		switch (StitchingTechnique)
		{
		case EStitchingTechnique::StitchingSew:
		{
			// Sew disconnected faces back together
			return CT_REPAIR_IO::Sew(MainObjectID, SewingToleranceFactor, CT_SEW_CREATE_BODIES_BY_TOPOLOGY);
		}

		case EStitchingTechnique::StitchingHeal:
		{
			// Heal disconnected faces back together
			CT_UINT32 StitchingToleranceFactor = 0; // topo only if deformation=0, factor of 'tolerance' value
			CT_LOGICAL   OnlyVisible = CT_TRUE;
			CT_LOGICAL   MergeBeforeHealing = CT_FALSE;
			CT_LOGICAL   RemoveTinyObjects = CT_TRUE;
			return CT_REPAIR_IO::Heal(MainObjectID, StitchingToleranceFactor, OnlyVisible, MergeBeforeHealing, RemoveTinyObjects);
		}
		}
		return CT_IO_ERROR::IO_OK;
	}

	CT_IO_ERROR SetCoreTechTessellationState(const FImportParameters& ImportParams)
	{
		CT_IO_ERROR Result = IO_OK;
		CT_DOUBLE CurrentUnit = 0.001;

		CT_TESS_DATA_TYPE VertexType = CT_TESS_DOUBLE;
		CT_TESS_DATA_TYPE NormalType = CT_TESS_FLOAT;
		CT_TESS_DATA_TYPE UVType = CT_TESS_DOUBLE;

		// convert max edge length to model units
		double MaxEdgeLength_modelUnit = FLT_MAX; // default value is huge, as 0.0 causes bugs in KernelIO...
		if (ImportParams.MaxEdgeLength > SMALL_NUMBER)
		{
			MaxEdgeLength_modelUnit = ImportParams.MaxEdgeLength / ImportParams.ScaleFactor;
		}

		// Apply retrieved tessellation parameters to CoreTech tessellation engine
		static CT_LOGICAL high_quality = CT_FALSE;
		Result = CTKIO_ChangeTesselationParameters(ImportParams.ChordTolerance / ImportParams.ScaleFactor, MaxEdgeLength_modelUnit, ImportParams.MaxNormalAngle, high_quality, VertexType, NormalType, UVType);

		return Result;
	}


	const TCHAR* CoreTechSessionBase::Owner = nullptr;

	CoreTechSessionBase::CoreTechSessionBase(const TCHAR* InOwner, double Unit)
	{
		// Lib init
		ensureAlways(InOwner != nullptr); // arg must be valid
		ensureAlways(Owner == nullptr); // static Owner must be unset

		CT_IO_ERROR e = CTKIO_ShutdownKernel(); // ignorable. Just in case CT was not previously stopped
		CT_IO_ERROR Result = CTKIO_InitializeKernel(Unit);
		Owner = (Result == IO_OK) ? InOwner : nullptr;

		// Create a main object to hold the BRep
		CT_OBJECT_ID nullParent = 0;
		Result = CT_COMPONENT_IO::Create(MainObjectId, nullParent);
		Result = CTKIO_AskMainObject(MainObjectId); // not required, just a validation.
	}

	CoreTechSessionBase::~CoreTechSessionBase()
	{
		if (Owner != nullptr)
		{
			CheckedCTError Result;
			Result = CTKIO_UnloadModel();
			Result = CTKIO_ShutdownKernel();
			Owner = nullptr;
		}
	}
} // ns CADLibrary

#endif // CAD_INTERFACE

