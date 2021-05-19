// Copyright Epic Games, Inc. All Rights Reserved.
#include "CoreTechTypes.h"

#include "CADData.h"
#include "CADOptions.h"
#include "CADSceneGraph.h"
#include "CoreTechInterfaceImpl.h"

#include "Misc/Paths.h"

namespace CADLibrary
{
	TSharedPtr<ICoreTechInterface> CoreTechInterface;

	TSharedPtr<ICoreTechInterface>& GetCoreTechInterface()
	{
		return CoreTechInterface;
	}

	void SetCoreTechInterface(TSharedPtr<ICoreTechInterface> CoreTechInterfacePtr)
	{
		CoreTechInterface = CoreTechInterfacePtr;
	}

	void InitializeCoreTechInterface()
	{
#ifdef USE_KERNEL_IO_SDK
		CoreTechInterface = MakeShared<CADLibrary::FCoreTechInterfaceImpl>();
#endif
	}

	bool CTKIO_InitializeKernel(const TCHAR* InEnginePluginsPath)
	{
		if (!CoreTechInterface.IsValid())
		{
			return false;
		}

		FString EnginePluginsPath(InEnginePluginsPath);

		if (EnginePluginsPath.IsEmpty())
		{
			EnginePluginsPath = FPaths::EnginePluginsDir();
		}
		
		return CoreTechInterface->InitializeKernel(*EnginePluginsPath);
	}

	bool CTKIO_ShutdownKernel()
	{
		return CoreTechInterface.IsValid() ? CoreTechInterface->ShutdownKernel() : false;
	}

	bool CTKIO_UnloadModel()
	{
		return CoreTechInterface.IsValid() ? CoreTechInterface->UnloadModel() : false;
	}

	bool CTKIO_ChangeUnit(double SceneUnit)
	{
		return CoreTechInterface.IsValid() ? CoreTechInterface->ChangeUnit(SceneUnit) : false;
	}

	bool CTKIO_CreateModel(uint64& OutMainObjectId)
	{
		OutMainObjectId = 0;
		return CoreTechInterface.IsValid() ? CoreTechInterface->CreateModel(OutMainObjectId) : false;
	}

	bool CTKIO_ChangeTesselationParameters(double MaxSag, double MaxLength, double MaxAngle)
	{
		return CoreTechInterface.IsValid() ? CoreTechInterface->ChangeTesselationParameters(MaxSag, MaxLength, MaxAngle) : false;
	}

	bool CTKIO_LoadModel(const TCHAR* FileName, uint64& MainObject, int32 LoadFlags, int32 Lod, const TCHAR* StringOption)
	{
		MainObject = 0;
		return CoreTechInterface.IsValid() ? CoreTechInterface->LoadModel(FileName, MainObject, LoadFlags, Lod, StringOption) : false;
	}

	bool CTKIO_SaveFile(const TArray<uint64>& ObjectsListToSave, const TCHAR* FileName, const TCHAR* Format, const uint64 CoordSystem)
	{
		return CoreTechInterface.IsValid() ? CoreTechInterface->SaveFile(ObjectsListToSave, FileName, Format, CoordSystem) : false;
	}

	bool CTKIO_Repair(uint64 MainObjectID, EStitchingTechnique StitchingTechnique, double SewingToleranceFactor)
	{
		return CoreTechInterface.IsValid() ? CoreTechInterface->Repair(MainObjectID, StitchingTechnique, SewingToleranceFactor) : false;
	}

	bool CTKIO_SetCoreTechTessellationState(const FImportParameters& ImportParams)
	{
		return CoreTechInterface.IsValid() ? CoreTechInterface->SetCoreTechTessellationState(ImportParams) : false;
	}

	void CTKIO_GetTessellation(uint64 ObjectId, FBodyMesh& OutBodyMesh, bool bIsBody)
	{
		if (CoreTechInterface.IsValid())
		{
			CoreTechInterface->GetTessellation(ObjectId, OutBodyMesh, bIsBody);
		}
	}

	ECoreTechParsingResult CTKIO_LoadFile(const FFileDescription& InFileDescription, const FImportParameters& InImportParameters, const FString& InCachePath, FArchiveSceneGraph& OutSceneGraphArchive, TArray<FString>& OutWarningMessages, TArray<FBodyMesh>& OutBodyMeshes)
	{
		if ( CoreTechInterface.IsValid())
		{
			if (!CoreTechInterface->IsExternal())
			{
				return CoreTechInterface->LoadFile(InFileDescription, InImportParameters, InCachePath, OutSceneGraphArchive, OutWarningMessages, OutBodyMeshes);
			}

			// Allocated data is crossing dll boundaries. The data must be copied.
			FLoadingContext Context(InImportParameters, InCachePath);

			const ECoreTechParsingResult Result = CoreTechInterface->LoadFile(InFileDescription, Context);
			if (Result == ECoreTechParsingResult::ProcessOk)
			{
				OutSceneGraphArchive = *Context.SceneGraphArchive;
				OutWarningMessages = *Context.WarningMessages;
				OutBodyMeshes = *Context.BodyMeshes;
			}

			return Result;
		}

		return  ECoreTechParsingResult::ProcessFailed;
	}

	bool CTKIO_CreateNurbsSurface(const FNurbsSurface& Surface, uint64& ObjectID)
	{
		ObjectID = 0;
		return CoreTechInterface.IsValid() ? CoreTechInterface->CreateNurbsSurface(Surface, ObjectID) : false;
	}

	bool CTKIO_CreateNurbsCurve(const FNurbsCurve& Curve, uint64& ObjectID)
	{
		ObjectID = 0;
		return CoreTechInterface.IsValid() ? CoreTechInterface->CreateNurbsCurve(Curve, ObjectID) : false;
	}

	void CTKIO_MatchCoedges(uint64 FirstCoedgeID, uint64 SecondCoedgeID)
	{
		if (CoreTechInterface.IsValid())
		{
			CoreTechInterface->MatchCoedges(FirstCoedgeID, SecondCoedgeID);
		}
	}

	bool CTKIO_CreateCoedge(bool bReversed, uint64& CoedgeID)
	{
		CoedgeID = 0;
		return CoreTechInterface.IsValid() ? CoreTechInterface->CreateCoedge(bReversed, CoedgeID) : false;
	}

	bool CTKIO_SetUVCurve(const FNurbsCurve& SurfacicCurve, uint64 CoedgeID)
	{
		return CTKIO_SetUVCurve(SurfacicCurve, SurfacicCurve.KnotValues[0], SurfacicCurve.KnotValues.Last(), CoedgeID);
	}

	bool CTKIO_CreateCoedge(const FNurbsCurve& CurveOnSurface, double Start, double End, bool bIsReversed, uint64& CoedgeID)
	{
		CoedgeID = 0;
		if (CTKIO_CreateCoedge(bIsReversed, CoedgeID))
		{
			return CTKIO_SetUVCurve(CurveOnSurface, Start, End, CoedgeID);
		}

		return false;
	}

	bool CTKIO_CreateCoedge(const FNurbsCurve& CurveOnSurface, bool bIsReversed, uint64& CoedgeID)
	{
		CoedgeID = 0;
		if (CTKIO_CreateCoedge(bIsReversed, CoedgeID))
		{
			return CTKIO_SetUVCurve(CurveOnSurface, CoedgeID);
		}

		return false;
	}

	bool CTKIO_SetUVCurve(const FNurbsCurve& SurfacicCurve, double Start, double End, uint64 CoedgeID)
	{
		return CoreTechInterface.IsValid() ? CoreTechInterface->SetUVCurve(SurfacicCurve, Start, End, CoedgeID) : false;
	}

	bool CTKIO_CreateLoop(const TArray<uint64>& Coedges, uint64& LoopID)
	{
		LoopID = 0;
		return CoreTechInterface.IsValid() ? CoreTechInterface->CreateLoop(Coedges, LoopID) : false;
	}

	bool CTKIO_CreateFace(uint64 SurfaceID, bool bIsForward, const TArray<uint64>& Loops, uint64& FaceID)
	{
		FaceID = 0;
		return CoreTechInterface.IsValid() ? CoreTechInterface->CreateFace(SurfaceID, bIsForward, Loops, FaceID) : false;
	}

	bool CTKIO_CreateBody(const TArray<uint64>& Faces, uint64& BodyID)
	{
		BodyID = 0;
		return CoreTechInterface.IsValid() ? CoreTechInterface->CreateBody(Faces, BodyID) : false;
	}

	bool CTKIO_AddBodies(const TArray<uint64>& Bodies, uint64 ComponentID)
	{
		return CoreTechInterface.IsValid() ? CoreTechInterface->AddBodies(Bodies, ComponentID) : false;
	}

	const TCHAR* FCoreTechSessionBase::Owner = nullptr;

	FCoreTechSessionBase::FCoreTechSessionBase(const TCHAR* InOwner)
	{
		// Lib init
		ensureAlways(InOwner != nullptr); // arg must be valid
		ensureAlways(Owner == nullptr); // static Owner must be unset

		Owner = CTKIO_InitializeKernel() ? InOwner : nullptr;
		// ignorable. Just in case CT was not previously emptied 
		CTKIO_UnloadModel();

		if (Owner)
		{
			// Create a main object to hold the BRep
			ensure(CTKIO_CreateModel(MainObjectId));
		}
	}

	FCoreTechSessionBase::~FCoreTechSessionBase()
	{
		if (Owner != nullptr)
		{
			CTKIO_UnloadModel();
			Owner = nullptr;
		}
	}
} // ns CADLibrary

