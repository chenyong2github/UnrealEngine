// Copyright Epic Games, Inc. All Rights Reserved.
#include "CoreTechInterfaceImpl.h"

#include "CADInterfacesModule.h"
#include "CoretechFileReader.h"

#include "Misc/Paths.h"

#ifdef USE_KERNEL_IO_SDK

#pragma warning(push)
#pragma warning(disable:4996) // unsafe sprintf
#pragma warning(disable:4828) // illegal character
#include "kernel_io/filter_io/filter_io.h"
#include "kernel_io/list_io/list_io.h"
#include "kernel_io/object_io/asm_io/component_io/component_io.h"
#include "kernel_io/object_io/asm_io/instance_io/instance_io.h"
#include "kernel_io/object_io/geom_io/curve_io/cnurbs_io/cnurbs_io.h"
#include "kernel_io/object_io/geom_io/surface_io/snurbs_io/snurbs_io.h"
#include "kernel_io/object_io/topo_io/body_io/body_io.h"
#include "kernel_io/object_io/topo_io/coedge_io/coedge_io.h"
#include "kernel_io/object_io/topo_io/face_io/face_io.h"
#include "kernel_io/object_io/topo_io/loop_io/loop_io.h"
#include "kernel_io/repair_io/repair_io.h"
#pragma warning(pop)

const char* CoreTechLicenseKey =
#include "CoreTech.lic"
;

namespace CADLibrary
{
	bool FCoreTechInterfaceImpl::InitializeKernel(const TCHAR* EnginePluginsPath)
	{
		if (bIsInitialize)
		{
			return true;
		}

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

		CT_STR AppName = CoreTechLicenseKey;
		CT_IO_ERROR Status = CT_KERNEL_IO::InitializeKernel(AppName, 0.001, 0.01, *KernelIOPath);

		if (Status == IO_OK || Status == IO_ERROR_ALREADY_INITIALIZED)
		{
			if (Status == IO_OK)
			{
				bIsInitialize = true;
			}
			return true;
		}

		switch (Status)
		{
			case IO_ERROR_LICENSE:
				UE_LOG(CADInterfaces, Error, TEXT("CoreTech dll license is missing. Plug-in will not be functional."));
				break;

			case IO_ERROR_NOT_INITIALIZED:
			default:
				UE_LOG(CADInterfaces, Error, TEXT("CoreTech dll is not initialize. Plug - in will not be functional."));
				break;
		}

		return false;
	}

	bool FCoreTechInterfaceImpl::ShutdownKernel()
	{
		bIsInitialize = false;
		return CT_KERNEL_IO::ShutdownKernel() == IO_OK; // ignorable. Just in case CT was not previously stopped
	}

	bool FCoreTechInterfaceImpl::UnloadModel()
	{
		return CT_KERNEL_IO::UnloadModel() == IO_OK;
	}

	bool FCoreTechInterfaceImpl::ChangeUnit(double SceneUnit)
	{
		if (CT_KERNEL_IO::ChangeUnit(SceneUnit) != IO_OK)
		{
			return false;
		}
		return CT_KERNEL_IO::ChangeTolerance(0.00001 / SceneUnit) == IO_OK;
	}

	bool FCoreTechInterfaceImpl::CreateModel(uint64& OutMainObjectId)
	{
		OutMainObjectId = 0;

		CT_OBJECT_ID NullParent = 0;
		return CT_COMPONENT_IO::Create(OutMainObjectId, NullParent) == IO_OK;
	}

	bool FCoreTechInterfaceImpl::ChangeTesselationParameters(double MaxSag, double MaxLength, double MaxAngle)
	{
		constexpr CT_TESS_DATA_TYPE VertexType = CT_TESS_DOUBLE;
		constexpr CT_TESS_DATA_TYPE NormalType = CT_TESS_FLOAT;
		constexpr CT_TESS_DATA_TYPE UVType = CT_TESS_DOUBLE;
		constexpr CT_LOGICAL HighQuality = CT_TRUE;

		return CT_KERNEL_IO::ChangeTesselationParameters(MaxSag, MaxLength, MaxAngle, HighQuality, VertexType, NormalType, UVType) == IO_OK;
	}

	bool FCoreTechInterfaceImpl::LoadModel(const TCHAR* FileName, uint64& MainObject, int32 LoadFlags, int32 Lod, const TCHAR* StringOption)
	{
		MainObject = 0;
		return CT_KERNEL_IO::LoadFile(FileName, MainObject, /*(CT_FLAGS)*/LoadFlags, Lod, StringOption) == IO_OK;
	}

	bool FCoreTechInterfaceImpl::SaveFile(const TArray<uint64>& ObjectsListToSave, const TCHAR* FileName, const TCHAR* Format, const uint64 CoordSystem)
	{
#if WITH_EDITOR
		CT_LIST_IO ObjectList;
		for(uint64 ObjectID : ObjectsListToSave)
		{
			if (ObjectID)
			{
				ObjectList.PushBack(ObjectID);
			}
		}

		return CT_KERNEL_IO::SaveFile(ObjectList, FileName, Format, CoordSystem) == IO_OK;
#else
		return true;
#endif
	}

	void FCoreTechInterfaceImpl::GetAllBodies(CT_OBJECT_ID NodeId, TArray<CT_OBJECT_ID>& OutBodies)
	{
		CT_OBJECT_TYPE Type;
		CT_IO_ERROR Result = CT_OBJECT_IO::AskType(NodeId, Type);
		if (Result != IO_OK)
		{
			return;
		}

		switch (Type)
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
	void FCoreTechInterfaceImpl::FindBodiesToConcatenate(CT_OBJECT_ID NodeId, const TMap<CT_OBJECT_ID, CT_STR>& MarkedBodies, TMap<CT_OBJECT_ID, TArray<CT_OBJECT_ID>>& BodiesToConcatenate)
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
						CT_OBJECT_IO::AskNbAttributes(FaceID, CT_ATTRIB_INTEGER_PARAMETER, NbAttribs);
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
	void FCoreTechInterfaceImpl::MarkBodies(CT_OBJECT_ID NodeId, TMap<CT_OBJECT_ID, CT_STR>& MarkedBodies)
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

	void FCoreTechInterfaceImpl::RepairInternal(CT_OBJECT_ID MainId, bool bConnectOpenBody, CT_DOUBLE SewingToleranceFactor)
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
			CT_FILTER_IO::DeleteAllFilters();
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

	bool FCoreTechInterfaceImpl::Repair(uint64 MainObjectID, EStitchingTechnique StitchingTechnique, double SewingToleranceFactor)
	{
		if (StitchingTechnique == EStitchingTechnique::StitchingSew)
		{
			RepairInternal(MainObjectID, true, SewingToleranceFactor);
		}
		else if (StitchingTechnique == EStitchingTechnique::StitchingHeal)
		{
			RepairInternal(MainObjectID, false, SewingToleranceFactor);
		}

		return true;
	}

	bool FCoreTechInterfaceImpl::SetCoreTechTessellationState(const FImportParameters& ImportParams)
	{
		bScaleUVMap = ImportParams.bScaleUVMap;
		ScaleFactor = ImportParams.ScaleFactor;

		CT_DOUBLE CurrentUnit = 0.001;

		// convert max edge length to model units
		double ModelUnit = FLT_MAX; // default value is huge, as 0.0 causes bugs in KernelIO...
		if (ImportParams.MaxEdgeLength > SMALL_NUMBER)
		{
			ModelUnit = ImportParams.MaxEdgeLength / ImportParams.ScaleFactor;
		}

		// Apply retrieved tessellation parameters to CoreTech tessellation engine
		return ChangeTesselationParameters(ImportParams.ChordTolerance / ImportParams.ScaleFactor, ModelUnit, ImportParams.MaxNormalAngle);
	}

	ECoreTechParsingResult FCoreTechInterfaceImpl::LoadFile(const FFileDescription& InFileDescription, const FImportParameters& InImportParameters, const FString& InCachePath, FArchiveSceneGraph& OutSceneGraphArchive, TArray<FString>& OutWarningMessages, TArray<FBodyMesh>& OutBodyMeshes)
	{
		FCoreTechFileReader::FContext Context(InImportParameters, InCachePath, OutSceneGraphArchive, OutWarningMessages, OutBodyMeshes);

		FCoreTechFileReader FileReader(Context);

		ECoreTechParsingResult Result = FileReader.ProcessFile(InFileDescription);

		return Result;
	}

	int32 GetColorName(CT_OBJECT_ID ObjectID)
	{
		FColor Color;
		if (CT_OBJECT_IO::SearchAttribute(ObjectID, CT_ATTRIB_COLORID) == IO_OK)
		{
			CT_UINT32 ColorId = 0;
			if (CT_CURRENT_ATTRIB_IO::AskIntField(ITH_COLORID_VALUE, ColorId) == IO_OK && ColorId > 0)
			{
				CT_COLOR CtColor = { 200, 200, 200 };
				if (ColorId > 0)
				{
					if (CT_MATERIAL_IO::AskIndexedColor((CT_OBJECT_ID)ColorId, CtColor) != IO_OK)
					{
						return false;
					}
					Color.R = CtColor[0];
					Color.G = CtColor[1];
					Color.B = CtColor[2];
				}

				uint8 Alpha = 255;
				if (CT_OBJECT_IO::SearchAttribute(ObjectID, CT_ATTRIB_TRANSPARENCY) == IO_OK)
				{
					CT_DOUBLE dbl_value = 0.;
					if (CT_CURRENT_ATTRIB_IO::AskDblField(0, dbl_value) == IO_OK && dbl_value >= 0.0 && dbl_value <= 1.0)
					{
						Alpha = (uint8)int((1. - dbl_value) * 255.);
					}
				}
				Color.A = Alpha;
				return BuildColorName(Color);
			}
		}
		return 0;
	}

	void FCoreTechInterfaceImpl::GetTessellation(uint64 ObjectId, FBodyMesh& OutBodyMesh, bool bIsBody)
	{
		TFunction<void(CT_OBJECT_ID, int32, FTessellationData&)> ProcessFace;
		ProcessFace = [&](CT_OBJECT_ID FaceID, int32 Index, FTessellationData& Tessellation)
		{
			int32 ColorName = GetColorName(FaceID);
			Tessellation.ColorName = ColorName;

			if (bScaleUVMap && Tessellation.TexCoordArray.Num() > 0)
			{
				CoreTechFileReaderUtils::ScaleUV(FaceID, Tessellation.TexCoordArray, (float) ScaleFactor);
			}
		};

		if (bIsBody)
		{
			CoreTechFileReaderUtils::GetBodyTessellation(ObjectId, OutBodyMesh, ProcessFace);
		}
		else
		{
			CT_LIST_IO ObjectList;
			if (CT_COMPONENT_IO::AskChildren(ObjectId, ObjectList) != IO_OK || ObjectList.Count() == 0)
			{
				return;
			}

			OutBodyMesh.BodyID = 1;

			while (CT_OBJECT_ID BodyId = ObjectList.IteratorIter())
			{
				CoreTechFileReaderUtils::GetBodyTessellation(BodyId, OutBodyMesh, ProcessFace);
			}
		}
	}

	bool FCoreTechInterfaceImpl::CreateNurbsSurface(const FNurbsSurface& Surface, uint64& ObjectID)
	{
		ObjectID = 0;

		CT_IO_ERROR Result = CT_SNURBS_IO::Create(ObjectID,
			Surface.OrderU, Surface.OrderV,
			Surface.KnotSizeU, Surface.KnotSizeV,
			Surface.ControlPointSizeU, Surface.ControlPointSizeV,
			Surface.ControlPointDimension, const_cast<double*>(Surface.ControlPoints.GetData()),
			const_cast<double*>(Surface.KnotValuesU.GetData()), const_cast<double*>(Surface.KnotValuesV.GetData()),
			const_cast<uint32*>(Surface.KnotMultiplicityU.GetData()), const_cast<uint32*>(Surface.KnotMultiplicityV.GetData())
		);

		return Result == IO_OK;
	}

	bool FCoreTechInterfaceImpl::CreateNurbsCurve(const FNurbsCurve& Curve, uint64& ObjectID)
	{
		ObjectID = 0;

		CT_IO_ERROR Result = CT_CNURBS_IO::Create(ObjectID,
			Curve.Order, Curve.KnotSize, Curve.ControlPointSize, Curve.ControlPointDimension,
			const_cast<double*>(Curve.ControlPoints.GetData()), const_cast<double*>(Curve.KnotValues.GetData()),
			const_cast<uint32*>(Curve.KnotMultiplicity.GetData()), Curve.KnotValues[0], Curve.KnotValues[Curve.KnotSize - 1]
		);

		return Result == IO_OK;
	}

	void FCoreTechInterfaceImpl::MatchCoedges(uint64 FirstCoedgeID, uint64 SecondCoedgeID)
	{
		CT_COEDGE_IO::MatchCoedges(FirstCoedgeID, SecondCoedgeID);
	}

	bool FCoreTechInterfaceImpl::CreateCoedge(bool bIsReversed, uint64& CoedgeID)
	{
		return CT_COEDGE_IO::Create(CoedgeID, bIsReversed ? CT_ORIENTATION::CT_REVERSE : CT_ORIENTATION::CT_FORWARD) == IO_OK;
	}

	bool FCoreTechInterfaceImpl::SetUVCurve(const FNurbsCurve& CurveOnSurface, double Start, double End, uint64 CoedgeID)
	{
		CT_IO_ERROR Result = CT_COEDGE_IO::SetUVCurve(
			CoedgeID,
			CurveOnSurface.Order, CurveOnSurface.KnotSize,
			CurveOnSurface.ControlPointSize, CurveOnSurface.ControlPointDimension,
			const_cast<double*>(CurveOnSurface.ControlPoints.GetData()),
			const_cast<double*>(CurveOnSurface.KnotValues.GetData()), const_cast<uint32*>(CurveOnSurface.KnotMultiplicity.GetData()),
			Start, End);
		
		return Result == IO_OK;
	}

	bool FCoreTechInterfaceImpl::CreateLoop(const TArray<uint64>& Coedges, uint64& LoopID)
	{
		LoopID = 0;

		CT_LIST_IO CoedgeList;
		for(uint64 CoedgeID : Coedges)
		{
			if (CoedgeID)
			{
				CoedgeList.PushBack(CoedgeID);
			}
		}

		return CT_LOOP_IO::Create(LoopID, CoedgeList) == IO_OK;
	}

	bool FCoreTechInterfaceImpl::CreateFace(uint64 SurfaceID, bool bIsForward, const TArray<uint64>& Loops, uint64& FaceID)
	{
		FaceID = 0;

		CT_LIST_IO Boundaries;

		for(uint64 LoopID : Loops)
		{
			if (LoopID)
			{
				Boundaries.PushBack(LoopID);
			}
		}

		const CT_ORIENTATION FaceOrient = bIsForward ? CT_ORIENTATION::CT_FORWARD : CT_ORIENTATION::CT_REVERSE;

		return CT_FACE_IO::Create(FaceID, SurfaceID, FaceOrient, Boundaries) == IO_OK;
	}

	bool FCoreTechInterfaceImpl::CreateBody(const TArray<uint64>& Faces, uint64& BodyID)
	{
		BodyID = 0;

		CT_LIST_IO FaceList;

		for (uint64 FaceID : Faces)
		{
			if (FaceID)
			{
				FaceList.PushBack(FaceID);
			}
		}

		const CT_FLAGS Flags = CT_BODY_PROP::CT_BODY_PROP_EXACT | CT_BODY_PROP::CT_BODY_PROP_CLOSE;

		return CT_BODY_IO::CreateFromFaces(BodyID, Flags, FaceList) == IO_OK;
	}

	bool FCoreTechInterfaceImpl::AddBodies(const TArray<uint64>& Bodies, uint64 ComponentID)
	{
		CT_LIST_IO BodyList;

		for (uint64 BodyID : Bodies)
		{
			if (BodyID)
			{
				BodyList.PushBack(BodyID);
			}
		}

		return CT_COMPONENT_IO::AddChildren(ComponentID, BodyList) == IO_OK;
	}
}
#endif // USE_KERNEL_IO_SDK

