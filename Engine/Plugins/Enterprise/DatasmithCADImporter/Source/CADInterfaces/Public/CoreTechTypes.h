// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once


#include "CADOptions.h"
#include "Containers/Array.h"
#include "Math/Vector.h"
#include "Math/Vector2D.h"

namespace CADLibrary
{
	enum class ECoreTechParsingResult : uint8
	{
		Unknown,
		Running,
		UnTreated,
		ProcessOk,
		ProcessFailed,
		FileNotFound,
	};

	class FBodyMesh;
	struct FFileDescription;
	class FArchiveSceneGraph;
	class ICoreTechInterface;

	// Helper struct used to pass Nurbs surface definition to CoreTech
	struct FNurbsSurface
	{
		uint32 ControlPointDimension = 0;

		uint32 ControlPointSizeU = 0;
		uint32 ControlPointSizeV = 0;

		uint32 OrderU = 0;
		uint32 OrderV = 0;

		uint32 KnotSizeU = 0;
		uint32 KnotSizeV = 0;

		TArray<double> KnotValuesU;
		TArray<double> KnotValuesV;
		TArray<uint32> KnotMultiplicityU;
		TArray<uint32> KnotMultiplicityV;

		TArray<double> ControlPoints;
	};

	// Helper struct used to pass Nurbs curve definition to CoreTech
	struct FNurbsCurve
	{
		uint32 ControlPointDimension = 0;
		uint32 ControlPointSize = 0;
		uint32 Order = 0;
		uint32 KnotSize = 0;
		TArray<double> KnotValues;
		TArray<uint32> KnotMultiplicity;
		TArray<double> ControlPoints;
	};

	// Helper struct used to pass the result of ICoreTechInterface::LoadFile across dll boundaries
	// when the ICoreTechInterface object has been created through the DatasmithCADRuntime dll 
	struct FLoadingContext
	{
		FLoadingContext(const FImportParameters& InImportParameters, const FString& InCachePath)
			: ImportParameters(InImportParameters)
			, CachePath(InCachePath)
		{
		}

		FLoadingContext(const FLoadingContext& Other)
			: ImportParameters(Other.ImportParameters)
			, CachePath(Other.CachePath)
		{
		}

		const FImportParameters& ImportParameters;
		const FString& CachePath;
		TSharedPtr<FArchiveSceneGraph> SceneGraphArchive;
		TSharedPtr<TArray<FString>> WarningMessages;
		TSharedPtr<TArray<FBodyMesh>> BodyMeshes;
	};

	class ICoreTechInterface
	{
	public:
		/* 
		* Returns true if the object has been created outside of the memory pool of the running process
		* This is the case when the object has been created by the DatasmithRuntime plugin
		*/
		virtual bool IsExternal() = 0;
		virtual void SetExternal(bool Value) = 0;

		virtual bool InitializeKernel(const TCHAR* = TEXT("")) = 0;

		/**
		 * Change Kernel_IO unit
		 * As to be call after CTKIO_UnloadModel
		 * This method set also the Tolerance to 0.00001 m whether 0.01 mm
		 * @param MetricUnit: Length unit express in meter i.e. 0.001 = mm
		 */
		virtual bool ChangeUnit(double SceneUnit) = 0;
		
		virtual bool ShutdownKernel() = 0;

		virtual bool UnloadModel() = 0;

		virtual bool CreateModel(uint64& OutMainObjectId) = 0;

		virtual bool ChangeTesselationParameters(double MaxSag, double MaxLength, double MaxAngle) = 0;

		virtual bool LoadModel
		(
			const TCHAR* file_name,
			uint64& main_object,
			int32      load_flags = 0 /*CT_LOAD_FLAGS_USE_DEFAULT*/,
			int32        lod = 0,
			const TCHAR* string_option = TEXT("")
		) = 0;

		virtual bool SaveFile
		(
			const TArray<uint64>& objects_list_to_save,
			const TCHAR* file_name,
			const TCHAR* format,
			const uint64 coordsystem = 0
		) = 0;

		/**
		* This function calls, according to the chosen EStitchingTechnique, Kernel_io CT_REPAIR_IO::Sew or CT_REPAIR_IO::Heal. In case of sew, the used tolerance is 100x the geometric tolerance (SewingToleranceFactor = 100).
		* With the case of UE-83379, Alias file, this value is too big (biggest than the geometric features. So Kernel_io hangs during the sew process... In the wait of more test, 100x is still the value used for CAD import except for Alias where the value of the SewingToleranceFactor is set to 1x
		* @param SewingToleranceFactor Factor apply to the tolerance 3D to define the sewing tolerance.
		*/
		virtual bool Repair(uint64 MainObjectID, EStitchingTechnique StitchingTechnique, double SewingToleranceFactor = 100.) = 0;
		virtual bool SetCoreTechTessellationState(const FImportParameters& ImportParams) = 0;

		virtual void GetTessellation(uint64 BodyId, FBodyMesh& OutBodyMesh, bool bIsBody) = 0;

		virtual void GetTessellation(uint64 BodyId, TSharedPtr<FBodyMesh>& OutBodyMesh, bool bIsBody) = 0;

		virtual ECoreTechParsingResult LoadFile(
			const FFileDescription& InFileDescription,
			const FImportParameters& InImportParameters,
			const FString& InCachePath,
			FArchiveSceneGraph& OutSceneGraphArchive,
			TArray<FString>& OutWarningMessages,
			TArray<FBodyMesh>& OutBodyMeshes) = 0;

		virtual ECoreTechParsingResult LoadFile(const FFileDescription& InFileDescription, FLoadingContext& LoadingContext) = 0;

		virtual bool CreateNurbsSurface(const FNurbsSurface& Surface, uint64& ObjectID) = 0;

		virtual bool CreateNurbsCurve(const FNurbsCurve& Curve, uint64& ObjectID) = 0;

		virtual void MatchCoedges(uint64 FirstCoedgeID, uint64 SecondCoedgeID) = 0;

		virtual bool CreateCoedge(bool bReversed, uint64& CoedgeID) = 0;

		virtual bool SetUVCurve(const FNurbsCurve& SurfacicCurve, double Start, double End, uint64 CoedgeID) = 0;

		virtual bool CreateLoop(const TArray<uint64>& Coedges, uint64& LoopID) = 0;

		virtual bool CreateFace(uint64 SurfaceID, bool bIsForward, const TArray<uint64>& Loops, uint64& FaceID) = 0;

		virtual bool CreateBody(const TArray<uint64>& Faces, uint64& BodyID) = 0;

		virtual bool AddBodies(const TArray<uint64>& Bodies, uint64 ComponentID) = 0;
	};

	CADINTERFACES_API void InitializeCoreTechInterface();
	CADINTERFACES_API void SetCoreTechInterface(TSharedPtr<ICoreTechInterface> CoreTechInterfacePtr);
	CADINTERFACES_API TSharedPtr<ICoreTechInterface>& GetCoreTechInterface();

	struct FCTMesh
	{
		TArray<uint32> Materials; //GPure Material Id
		TArray<uint32> MaterialUUIDs; //Material Hash from color value
		TArray<FVector> Vertices;
		TArray<FVector> Normals;
		TArray<FVector2D> TexCoords;
		TArray<int32_t> Indices;
		TArray<uint32> TriangleMaterials;
	};

	CADINTERFACES_API bool CTKIO_InitializeKernel(const TCHAR* = TEXT(""));

	/**
	 * Change Kernel_IO unit
	 * As to be call after CTKIO_UnloadModel
	 * This method set also the Tolerance to 0.00001 m whether 0.01 mm
	 * @param MetricUnit: Length unit express in meter i.e. 0.001 = mm
	 */
	CADINTERFACES_API bool CTKIO_ChangeUnit(double SceneUnit);

	CADINTERFACES_API bool CTKIO_ShutdownKernel();
	CADINTERFACES_API bool CTKIO_UnloadModel();
	CADINTERFACES_API bool CTKIO_CreateModel(uint64& OutMainObjectId);

	CADINTERFACES_API bool CTKIO_ChangeTesselationParameters(double MaxSag, double MaxLength, double MaxAngle);

	CADINTERFACES_API bool CTKIO_LoadModel
	(
		const TCHAR*  file_name,                             
		uint64&       main_object,
		int32         load_flags = 0 /*CT_LOAD_FLAGS_USE_DEFAULT*/,
		int32         lod = 0,
		const TCHAR*  string_option = TEXT("")
	);

	CADINTERFACES_API bool CTKIO_SaveFile
	(
		const TArray<uint64>&   ObjectsListToSave,
		const TCHAR*            FileName,           
		const TCHAR*            Format,              
		const uint64            CoordSystem = 0      
	);

	CADINTERFACES_API void CTKIO_GetTessellation(uint64 ObjectId, FBodyMesh& OutBodyMesh, bool bIsBody = true);

	CADINTERFACES_API ECoreTechParsingResult CTKIO_LoadFile(const FFileDescription& InFileDescription, const FImportParameters& InImportParameters, const FString& InCachePath, FArchiveSceneGraph& OutSceneGraphArchive, TArray<FString>& OutWarningMessages, TArray<FBodyMesh>& OutBodyMeshes);

	CADINTERFACES_API bool CTKIO_CreateNurbsSurface(const FNurbsSurface& NurbsDefinition, uint64& ObjectID);

	CADINTERFACES_API bool CTKIO_CreateNurbsCurve(const FNurbsCurve& Curve, uint64& ObjectID);

	CADINTERFACES_API void CTKIO_MatchCoedges(uint64 FirstCoedgeID, uint64 SecondCoedgeID);

	CADINTERFACES_API bool CTKIO_CreateLoop(const TArray<uint64>& Coedges, uint64& LoopID);

	CADINTERFACES_API bool CTKIO_CreateFace(uint64 SurfaceID, bool bIsForward, const TArray<uint64>& Loops, uint64& FaceID);

	CADINTERFACES_API bool CTKIO_CreateBody(const TArray<uint64>& Faces, uint64& BodyID);

	CADINTERFACES_API bool CTKIO_AddBodies(const TArray<uint64>& Bodies, uint64 ComponentID);

	CADINTERFACES_API bool CTKIO_CreateCoedge(bool bIsReversed, uint64& CoedgeID);

	CADINTERFACES_API bool CTKIO_SetUVCurve(const FNurbsCurve& SurfacicCurve, double Start, double End, uint64 CoedgeID);

	CADINTERFACES_API bool CTKIO_SetUVCurve(const FNurbsCurve& SurfacicCurve, uint64 CoedgeID);

	CADINTERFACES_API bool CTKIO_CreateCoedge(const FNurbsCurve& CurveOnSurface, double Start, double End, bool bIsReversed, uint64& CoedgeID);

	CADINTERFACES_API bool CTKIO_CreateCoedge(const FNurbsCurve& CurveOnSurface, bool bIsReversed, uint64& CoedgeID);

	/**
	 * This function calls, according to the chosen EStitchingTechnique, Kernel_io CT_REPAIR_IO::Sew or CT_REPAIR_IO::Heal. In case of sew, the used tolerance is 100x the geometric tolerance (SewingToleranceFactor = 100).
	 * With the case of UE-83379, Alias file, this value is too big (biggest than the geometric features. So Kernel_io hangs during the sew process... In the wait of more test, 100x is still the value used for CAD import except for Alias where the value of the SewingToleranceFactor is set to 1x
	 * @param SewingToleranceFactor Factor apply to the tolerance 3D to define the sewing tolerance.
	 */
	CADINTERFACES_API bool CTKIO_Repair(uint64 MainObjectID, EStitchingTechnique StitchingTechnique, double SewingToleranceFactor = 100.);
	CADINTERFACES_API bool CTKIO_SetCoreTechTessellationState(const FImportParameters& ImportParams);

	class CADINTERFACES_API FCoreTechSessionBase
	{
	public:
		/**
		 * Make sure CT is initialized, and a main object is ready.
		 * Handle input file unit and an output unit
		 * @param FileMetricUnit number of meters per file unit.
		 * eg. For a file in inches, arg should be 0.0254
		 */
		FCoreTechSessionBase(const TCHAR* Owner);
		bool IsSessionValid() { return Owner != nullptr && MainObjectId != 0; }
		virtual ~FCoreTechSessionBase();

	protected:
		uint64 MainObjectId;

	private:
		static const TCHAR* Owner;
	};
}

