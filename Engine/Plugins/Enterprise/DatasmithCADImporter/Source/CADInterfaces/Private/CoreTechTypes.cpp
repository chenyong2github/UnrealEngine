// Copyright Epic Games, Inc. All Rights Reserved.
#include "CoreTechTypes.h"

#include "Misc/Paths.h"

#ifdef CAD_INTERFACE
// Temporary: This is there because CoreTechTypes.h is a public header which should not be changed for a hotfix
// Must be move to CoreTechTypes.h for 4.27
#pragma warning(push)
#pragma warning(disable:4996) // unsafe sprintf
#pragma warning(disable:4828) // illegal character
#include "kernel_io/filter_io/filter_io.h"
#pragma warning(pop)
// End Temporary

const char* CoreTechLicenseKey =
#include "CoreTech.lic"
;

namespace CADLibrary
{
	// Note: CTKIO_* functions are not functionally useful.
	// This wrapping allows a correct profiling of the CT API.

	double CTKIO_MetricUnit = -1;

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

		if (FMath::IsNearlyEqual(Unit, CTKIO_MetricUnit))
		{
			// Kernel_IO is already initialized with requested Unit value
			return IO_OK;
		}

		// Kernel_IO is already initialized, so it is stopped to be able to restart it with the good unit value
		CTKIO_ShutdownKernel();

		CT_STR appName = CoreTechLicenseKey;
		CT_IO_ERROR Status = CT_KERNEL_IO::InitializeKernel(appName, Unit, 0.00001 / Unit, *KernelIOPath);

		if (Status == IO_OK)
		{
			CTKIO_MetricUnit = Unit;
		}

		return Status;
	}

	CT_IO_ERROR CTKIO_ShutdownKernel()
	{
		CTKIO_MetricUnit = -1;
		return CT_KERNEL_IO::ShutdownKernel(); // ignorable. Just in case CT was not previously stopped
	}

	CT_IO_ERROR CTKIO_UnloadModel()
	{
		return CT_KERNEL_IO::UnloadModel();
	}

	CT_IO_ERROR CTKIO_CreateModel(CT_OBJECT_ID& OutMainObjectId)
	{
		CT_OBJECT_ID NullParent = 0;
		return CT_COMPONENT_IO::Create(OutMainObjectId, NullParent);
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

	void GetAllBodies(CT_OBJECT_ID NodeId, TArray<CT_OBJECT_ID>& OutBodies)
	{
		CT_OBJECT_TYPE type;
		CT_IO_ERROR Result = CT_OBJECT_IO::AskType(NodeId, type);
		if (Result != IO_OK)
		{
			return;
		}

		switch (type)
		{
		case CT_ASSEMBLY_TYPE:
		case CT_PART_TYPE:
		case CT_COMPONENT_TYPE:
		{
			CT_LIST_IO Children;
			Result = CT_COMPONENT_IO::AskChildren(NodeId, Children);
			if (Result != IO_OK)
			{
				return;
			}

			// Iterate over the instances and call some function on each
			Children.IteratorInitialize();
			CT_OBJECT_ID Child;
			while ((Child = Children.IteratorIter()) != 0)
			{
				GetAllBodies(Child, OutBodies);
			}
			break;
		}

		case CT_INSTANCE_TYPE:
		{
			CT_OBJECT_ID ChildId;
			Result = CT_INSTANCE_IO::AskChild(NodeId, ChildId);
			if (Result != IO_OK)
			{
				return;
			}

			GetAllBodies(ChildId, OutBodies);
			break;
		}

		case CT_BODY_TYPE:
		{
			if (CT_OBJECT_IO::IsVisible(NodeId))
			{
				OutBodies.Add(NodeId);
			}
			break;
		}
		}
	}

	// Finds new bodies which all faces come from the same original body before sewing
	// Fills up mapping between original bodies and array of newly created bodies related to each 
	void FindBodiesToConcatenate(CT_OBJECT_ID NodeId, const TMap<CT_OBJECT_ID, CT_STR>& MarkedBodies, TMap<CT_OBJECT_ID, TArray<CT_OBJECT_ID>>& BodiesToConcatenate)
	{
		CT_OBJECT_TYPE Type;
		CT_OBJECT_IO::AskType(NodeId, Type);

		switch (Type)
		{
		case CT_INSTANCE_TYPE:
		{
			CT_OBJECT_ID ReferenceNodeId;
			if (CT_INSTANCE_IO::AskChild(NodeId, ReferenceNodeId) == IO_OK)
			{
				FindBodiesToConcatenate(ReferenceNodeId, MarkedBodies, BodiesToConcatenate);
			}
			break;
		}

		case CT_ASSEMBLY_TYPE:
		case CT_PART_TYPE:
		case CT_COMPONENT_TYPE:
		{
			CT_LIST_IO Children;
			if (CT_COMPONENT_IO::AskChildren(NodeId, Children) == IO_OK)
			{
				Children.IteratorInitialize();
				CT_OBJECT_ID ChildId;
				while ((ChildId = Children.IteratorIter()) != 0)
				{
					FindBodiesToConcatenate(ChildId, MarkedBodies, BodiesToConcatenate);
				}
			}

			break;
		}

		break;

		case CT_BODY_TYPE:
		{
			// Skip bodies which were excluded from the sewing
			if (!MarkedBodies.Contains(NodeId))
			{
				CT_LIST_IO FaceList;
				CT_BODY_IO::AskFaces(NodeId, FaceList);

				CT_OBJECT_ID OriginalBodyID = 0;

				CT_OBJECT_ID FaceID;
				FaceList.IteratorInitialize();
				while ((FaceID = FaceList.IteratorIter()) != 0)
				{
					CT_UINT32 NbAttribs = 0;
					CT_OBJECT_IO::AskNbAttributes( FaceID, CT_ATTRIB_INTEGER_PARAMETER, NbAttribs);
					ensure(NbAttribs > 0);

					// Get the latest integer parameter attached to the face. This is the one previously added
					ensure(CT_OBJECT_IO::SearchAttribute(FaceID, CT_ATTRIB_INTEGER_PARAMETER, NbAttribs - 1) == IO_OK);

					CT_STR FieldStrValue;
					ensure(CT_CURRENT_ATTRIB_IO::AskStrField(ITH_INTEGER_PARAMETER_NAME, FieldStrValue) == IO_OK);
					ensure(FCStringAnsi::Strcmp(FieldStrValue.toASCII(), "DatasmithBodyId") == 0);

					CT_INT32 BodyID = 0;
					ensure(CT_CURRENT_ATTRIB_IO::AskIntField(ITH_INTEGER_PARAMETER_VALUE, BodyID) == IO_OK);

					if (BodyID != 0 && OriginalBodyID == 0)
					{
						OriginalBodyID = BodyID;
					}
					// This body contains a new face or a face from another original body, cannot be concatenated
					else if (BodyID == 0 || OriginalBodyID != BodyID)
					{
						OriginalBodyID = 0;
						break;
					}
				}

				// All faces of the body are from the same original body
				if (OriginalBodyID != 0)
				{
					BodiesToConcatenate.FindOrAdd(OriginalBodyID).Add(NodeId);
				}
			}

			break;
		}

		default:
			break;
		}
	}

	// For each body, adds an integer parameter representing the id of the owning body to each of its faces
	// Also stores the original name of the body for potential restoration
	void MarkBodies(CT_OBJECT_ID NodeId, TMap<CT_OBJECT_ID, CT_STR>& MarkedBodies)
	{
		CT_OBJECT_TYPE Type;
		CT_OBJECT_IO::AskType(NodeId, Type);

		switch (Type)
		{
		case CT_INSTANCE_TYPE:
		{
			CT_OBJECT_ID ReferenceNodeId;
			if (CT_INSTANCE_IO::AskChild(NodeId, ReferenceNodeId) == IO_OK)
			{
				MarkBodies(ReferenceNodeId, MarkedBodies);
			}
			break;
		}

		case CT_ASSEMBLY_TYPE:
		case CT_PART_TYPE:
		case CT_COMPONENT_TYPE:
		{
			CT_LIST_IO Children;
			if (CT_COMPONENT_IO::AskChildren(NodeId, Children) == IO_OK)
			{
				Children.IteratorInitialize();
				CT_OBJECT_ID ChildId;
				while ((ChildId = Children.IteratorIter()) != 0)
				{
					MarkBodies(ChildId, MarkedBodies);
				}
			}

			break;
		}

		break;

		case CT_BODY_TYPE:
		{
			// Skip non visible bodies since they will not be part of the sewing process
			if (CT_OBJECT_IO::IsVisible(NodeId) == CT_TRUE && !MarkedBodies.Contains(NodeId))
			{
				CT_STR BodyName;
				CT_UINT32 IthAttrib = 0;
				if (CT_OBJECT_IO::SearchAttribute(NodeId, CT_ATTRIB_NAME, IthAttrib++) == IO_OK)
				{
					ensure (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_NAME_VALUE, BodyName) == IO_OK);
				}

				MarkedBodies.Add(NodeId, BodyName);

				CT_LIST_IO FaceList;
				CT_BODY_IO::AskFaces(NodeId, FaceList);

				CT_OBJECT_ID FaceID;
				FaceList.IteratorInitialize();
				while ((FaceID = FaceList.IteratorIter()) != 0)
				{
					CT_OBJECT_IO::AddAttribute(FaceID, CT_ATTRIB_INTEGER_PARAMETER);

					ensure(CT_CURRENT_ATTRIB_IO::SetStrField(ITH_INTEGER_PARAMETER_NAME, "DatasmithBodyId") == IO_OK);
					ensure(CT_CURRENT_ATTRIB_IO::SetIntField(ITH_INTEGER_PARAMETER_VALUE, NodeId) == IO_OK);
				}
			}
			break;
		}

		default:
			break;
		}
	}

	void RepairInternal(CT_OBJECT_ID MainId, bool bConnectOpenBody, CT_DOUBLE SewingToleranceFactor)
	{
		// Before proceeding, verify that sewing is actually needed, i.e. there are faces with free edges
		CT_UINT32        BodyCount;       
		CT_UINT32        PerfectBodyCount;
		CT_UINT32        ClosedBodyCount;
		CT_UINT32        FaceCount;       
		CT_UINT32        FreeEdgeCount;  
		CT_UINT32        ImprecisionCount;
		CT_DOUBLE        ImprecisionMax;

		CT_REPAIR_IO::Check(MainId, BodyCount, PerfectBodyCount, ClosedBodyCount, FaceCount, FreeEdgeCount, ImprecisionCount, ImprecisionMax);

		if (bConnectOpenBody && FreeEdgeCount > 0)
		{
			TMap<CT_OBJECT_ID, CT_STR> MarkedBodies;
			MarkedBodies.Reserve(BodyCount);
			MarkBodies(MainId, MarkedBodies);

			// Apply the default 'BAD_BODIES' filter to only process bodies which may need repair 
			CT_FILTER_IO::InitializeDefaultFilters();
			CT_FILTER_IO::SetCurrentFilter(6 /* BAD_BODIES */);

			CT_FILTER_IO::SetVisibilitiesByFilter();

			// Perform sewing only on visible bodies
			CT_REPAIR_IO::Sew(MainId, SewingToleranceFactor, CT_SEW_CREATE_BODIES_BY_TOPOLOGY, CT_TRUE);

			// Restore visibility for unprocessed bodies
			for (TPair<CT_OBJECT_ID, CT_STR>& Entry : MarkedBodies)
			{
				CT_OBJECT_IO::SetVisible(Entry.Key, CT_TRUE);
			}

			TMap<CT_OBJECT_ID, TArray<CT_OBJECT_ID>> BodiesToConcatenate;
			FindBodiesToConcatenate(MainId, MarkedBodies, BodiesToConcatenate);

			for (const TPair<CT_OBJECT_ID, TArray<CT_OBJECT_ID>>& Entry : BodiesToConcatenate)
			{
				// Only restore name for new bodies originating from one body
				// TODO: Concatenate new bodies originating from the same body
				if (Entry.Value.Num() == 1)
				{
					CT_OBJECT_ID BodyID = Entry.Value[0];

					if (CT_OBJECT_IO::SearchAttribute(BodyID, CT_ATTRIB_NAME) != IO_OK)
					{
						CT_OBJECT_IO::AddAttribute(BodyID, CT_ATTRIB_NAME);
					}
					ensure(CT_CURRENT_ATTRIB_IO::SetStrField(ITH_NAME_VALUE, MarkedBodies[Entry.Key]) == IO_OK);
				}
			}
		}
		// sew disconnected faces back together
		// to be sure that there is no topology modification, this function has to be called body by body.
		// CT_REPAIR_IO::Sew is prefered to CT_REPAIR_IO::heal because CT_REPAIR_IO::heal is too restrictive
		else if (ImprecisionCount > 0)
		{
			TArray<CT_OBJECT_ID> OutBodies;
			OutBodies.Reserve(BodyCount);
			GetAllBodies(MainId, OutBodies);
			for (CT_OBJECT_ID BodyId : OutBodies)
			{
				// CT_SEW_KEEP_ORIGINAL_BODIES nor CT_SEW_CREATE_BODIES_BY_LAYER (previously used) preserves the integrity of the bodies
				// This is a bug from CoreTech. Using CT_SEW_KEEP_ORIGINAL_BODIES as this is the actual intent while waiting for CoreTech to fix this bug.
				CT_REPAIR_IO::Sew(BodyId, SewingToleranceFactor, CT_SEW_KEEP_ORIGINAL_BODIES);
			}
		}
	}

	CT_IO_ERROR Repair(CT_OBJECT_ID MainObjectID, EStitchingTechnique StitchingTechnique, CT_DOUBLE SewingToleranceFactor)
	{
		if (StitchingTechnique == EStitchingTechnique::StitchingSew)
		{
			RepairInternal(MainObjectID, true, SewingToleranceFactor);
		}
		else if (StitchingTechnique == EStitchingTechnique::StitchingHeal)
		{
			RepairInternal(MainObjectID, false, SewingToleranceFactor);
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
		static CT_LOGICAL bHighQuality = CT_TRUE;
		Result = CTKIO_ChangeTesselationParameters(ImportParams.ChordTolerance / ImportParams.ScaleFactor, MaxEdgeLength_modelUnit, ImportParams.MaxNormalAngle, bHighQuality, VertexType, NormalType, UVType);

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
		ensure(Result == IO_OK);
		Result = CTKIO_AskMainObject(MainObjectId); // not required, just a validation.
		ensure(Result == IO_OK);
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

