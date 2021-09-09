// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#ifdef USE_KERNEL_IO_SDK
#include "CADData.h"
#include "CADOptions.h"
#include "CADSceneGraph.h"
#include "CoreTechTypes.h"

#include "Containers/Array.h"
#include "Math/Vector.h"
#include "Math/Vector2D.h"

#pragma warning(push)
#pragma warning(disable:4996) // unsafe sprintf
#pragma warning(disable:4828) // illegal character
#include "kernel_io/kernel_io.h"
#include "kernel_io/kernel_io_error.h"
#include "kernel_io/kernel_io_type.h"
#pragma warning(pop)

namespace CADLibrary
{
	class CADINTERFACES_API FCoreTechInterfaceImpl : public ICoreTechInterface
	{
	public:
		FCoreTechInterfaceImpl(bool bSetExternal = false)
			: bIsExternal(bSetExternal)
		{}

		virtual bool IsExternal() override { return bIsExternal; }

		virtual void SetExternal(bool bSetExternal) override { bIsExternal = bSetExternal; }

		virtual bool InitializeKernel(const TCHAR* = TEXT("")) override;

		virtual bool ShutdownKernel() override;

		virtual bool UnloadModel() override;

		virtual bool ChangeUnit(double SceneUnit) override;

		virtual bool CreateModel(uint64& OutMainObjectId) override;


		virtual bool ChangeTesselationParameters(double MaxSag, double MaxLength, double MaxAngle) override;

		virtual bool LoadModel
		(
			const TCHAR* file_name,
			uint64&	     main_object,
			int32     load_flags,
			int32       lod,
			const TCHAR* string_option
		) override;

		virtual bool SaveFile
		(
			const TArray<uint64>& objects_list_to_save,
			const TCHAR* file_name,
			const TCHAR* format,
			const uint64 coordsystem = 0
		) override;

		/**
		 * This function calls, according to the chosen EStitchingTechnique, Kernel_io CT_REPAIR_IO::Sew or CT_REPAIR_IO::Heal. In case of sew, the used tolerance is 100x the geometric tolerance (SewingToleranceFactor = 100).
		 * With the case of UE-83379, Alias file, this value is too big (biggest than the geometric features. So Kernel_io hangs during the sew process... In the wait of more test, 100x is still the value used for CAD import except for Alias where the value of the SewingToleranceFactor is set to 1x
		 * @param SewingToleranceFactor Factor apply to the tolerance 3D to define the sewing tolerance.
		 */
		virtual bool Repair(uint64 MainObjectID, EStitchingTechnique StitchingTechnique, double SewingToleranceFactor) override;
		virtual bool SetCoreTechTessellationState(const FImportParameters& ImportParams) override;

		virtual ECoreTechParsingResult LoadFile(
			const FFileDescription& InFileDescription,
			const FImportParameters& InImportParameters,
			const FString& InCachePath,
			FArchiveSceneGraph& OutSceneGraphArchive,
			TArray<FString>& OutWarningMessages,
			TArray<FBodyMesh>& OutBodyMeshes) override;

		virtual ECoreTechParsingResult LoadFile(const FFileDescription& InFileDescription, FLoadingContext& Context) override
		{
			Context.SceneGraphArchive = MakeShared<FArchiveSceneGraph>();
			Context.WarningMessages = MakeShared<TArray<FString>>();
			Context.BodyMeshes = MakeShared<TArray<FBodyMesh>>();

			return LoadFile(InFileDescription, Context.ImportParameters, Context.CachePath, *Context.SceneGraphArchive, *Context.WarningMessages, *Context.BodyMeshes);
		}

		virtual void GetTessellation(uint64 BodyId, FBodyMesh& OutBodyMesh, bool bIsBody) override;

		virtual void GetTessellation(uint64 BodyId, TSharedPtr<FBodyMesh>& OutBodyMesh, bool bIsBody) override
		{
			OutBodyMesh = MakeShared<FBodyMesh>();
			GetTessellation(BodyId, *OutBodyMesh, bIsBody);
		}

		virtual bool CreateNurbsSurface(const FNurbsSurface& Surface, uint64& ObjectID) override;

		virtual bool CreateNurbsCurve(const FNurbsCurve& Curve, uint64& ObjectID) override;

		virtual void MatchCoedges(uint64 FirstCoedgeID, uint64 SecondCoedgeID) override;

		virtual bool CreateCoedge(bool bReversed, uint64& CoedgeID) override;

		virtual bool SetUVCurve(const FNurbsCurve& SurfacicCurve, double Start, double End, uint64 CoedgeID) override;

		virtual bool CreateLoop(const TArray<uint64>& Coedges, uint64& LoopID) override;

		virtual bool CreateFace(uint64 SurfaceID, bool bIsForward, const TArray<uint64>& Loops, uint64& FaceID) override;

		virtual bool CreateBody(const TArray<uint64>& Faces, uint64& BodyID) override;

		virtual bool AddBodies(const TArray<uint64>& Bodies, uint64 ComponentID) override;

	private:
		void GetAllBodies(CT_OBJECT_ID NodeId, TArray<CT_OBJECT_ID>& OutBodies);
		void FindBodiesToConcatenate(CT_OBJECT_ID NodeId, const TMap<CT_OBJECT_ID, CT_STR>& MarkedBodies, TMap<CT_OBJECT_ID, TArray<CT_OBJECT_ID>>& BodiesToConcatenate);
		void MarkBodies(CT_OBJECT_ID NodeId, TMap<CT_OBJECT_ID, CT_STR>& MarkedBodies);
		void RepairInternal(CT_OBJECT_ID MainId, bool bConnectOpenBody, CT_DOUBLE SewingToleranceFactor);

		bool bIsExternal = false;
		bool bIsInitialize = false;

		bool bScaleUVMap = false;
		double ScaleFactor = 1;
	};
}

#endif // USE_KERNEL_IO_SDK

