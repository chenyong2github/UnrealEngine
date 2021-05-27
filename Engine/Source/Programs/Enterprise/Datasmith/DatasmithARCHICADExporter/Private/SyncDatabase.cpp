// Copyright Epic Games, Inc. All Rights Reserved.

#include "SyncDatabase.h"

#include "MaterialsDatabase.h"
#include "TexturesCache.h"
#include "ElementID.h"
#include "Utils/ElementTools.h"
#include "GeometryUtil.h"
#include "Utils/3DElement2String.h"
#include "Utils/Element2String.h"

#include "DatasmithUtils.h"
#include "IDirectLinkUI.h"
#include "IDatasmithExporterUIModule.h"

#include "ModelMeshBody.hpp"
#include "Light.hpp"
#include "AttributeIndex.hpp"

BEGIN_NAMESPACE_UE_AC

#if defined(DEBUG) && 0
	#define UE_AC_DO_TRACE 1
#else
	#define UE_AC_DO_TRACE 0
#endif

// Constructor
FSyncDatabase::FSyncDatabase(const TCHAR* InSceneName, const TCHAR* InSceneLabel, const TCHAR* InAssetsPath,
							 const GS::UniString& InAssetsCache)
	: Scene(FDatasmithSceneFactory::CreateScene(*FDatasmithUtils::SanitizeObjectName(InSceneName)))
	, AssetsFolderPath(InAssetsPath)
	, MaterialsDatabase(new FMaterialsDatabase())
	, TexturesCache(new FTexturesCache(InAssetsCache))
{
	Scene->SetLabel(InSceneLabel);
}

// Destructor
FSyncDatabase::~FSyncDatabase()
{
	// Delete all sync data content by simulatin an emptying a 3d model
	ResetBeforeScan();
	CleanAfterScan();
	int32 RemainingCount = ElementsSyncDataMap.Num();
	if (RemainingCount != 0)
	{
		UE_AC_DebugF("FSyncDatabase::~FSyncDatabase - Database not emptied - %u Remaining\n", RemainingCount);
		for (TPair< FGuid, FSyncData* >& Iter : ElementsSyncDataMap)
		{
			delete Iter.Value;
			Iter.Value = nullptr;
		}
	}

	delete MaterialsDatabase;
	MaterialsDatabase = nullptr;
	delete TexturesCache;
	TexturesCache = nullptr;
}

// Return the asset file path
const TCHAR* FSyncDatabase::GetAssetsFolderPath() const
{
	return *AssetsFolderPath;
}

// Scan all elements, to determine if they need to be synchronized
void FSyncDatabase::Synchronize(const FSyncContext& InSyncContext)
{
	ResetBeforeScan();

	UInt32 ModifiedCount = ScanElements(InSyncContext);

	InSyncContext.NewPhase(kCommonSetUpLights, 0);

	// Cameras from all cameras set
	InSyncContext.NewPhase(kCommonSetUpCameras, 0);
	ScanCameras(InSyncContext);

	// Cameras from the current view
	FSyncData*& CameraSyncData = GetSyncData(FSyncData::FCamera::CurrentViewGUID);
	if (CameraSyncData == nullptr)
	{
		CameraSyncData = new FSyncData::FCamera(FSyncData::FCamera::CurrentViewGUID, 0);
		CameraSyncData->SetParent(&GetSceneSyncData());
		CameraSyncData->MarkAsModified();
	}
	CameraSyncData->MarkAsExisting();

	CleanAfterScan();

	InSyncContext.NewPhase(kCommonConvertElements, ModifiedCount);
	FSyncData::FProcessInfo ProcessInfo(InSyncContext);
	GetSceneSyncData().ProcessTree(&ProcessInfo);
}

// Before a scan we reset our sync data, so we can detect when an element has been modified or destroyed
void FSyncDatabase::ResetBeforeScan()
{
	for (const TPair< FGuid, FSyncData* >& Iter : ElementsSyncDataMap)
	{
		Iter.Value->ResetBeforeScan();
	}
}

inline const FGuid& GSGuid2FGuid(const GS::Guid& InGuid)
{
	return reinterpret_cast< const FGuid& >(InGuid);
}
inline const GS::Guid& FGuid2GSGuid(const FGuid& InGuid)
{
	return reinterpret_cast< const GS::Guid& >(InGuid);
}

// After a scan, but before syncing, we delete obsolete syncdata (and it's Datasmith Element)
void FSyncDatabase::CleanAfterScan()
{
	FSyncData** SyncData = ElementsSyncDataMap.Find(GSGuid2FGuid(FSyncData::FScene::SceneGUID));
	if (SyncData != nullptr)
	{
		(**SyncData).CleanAfterScan(this);
	}
}

// Get existing sync data for the specified guid
FSyncData*& FSyncDatabase::GetSyncData(const GS::Guid& InGuid)
{
	return ElementsSyncDataMap.FindOrAdd(GSGuid2FGuid(InGuid), nullptr);
}

FSyncData& FSyncDatabase::GetSceneSyncData()
{
	FSyncData*& SceneSyncData = GetSyncData(FSyncData::FScene::SceneGUID);
	if (SceneSyncData == nullptr)
	{
		SceneSyncData = new FSyncData::FScene();
	}
	return *SceneSyncData;
}

FSyncData& FSyncDatabase::GetLayerSyncData(short InLayer)
{
	FSyncData*& Layer = GetSyncData(FSyncData::FLayer::GetLayerGUID(InLayer));
	if (Layer == nullptr)
	{
		Layer = new FSyncData::FLayer(FSyncData::FLayer::GetLayerGUID(InLayer));
		Layer->SetParent(&GetSceneSyncData());
	}
	return *Layer;
}

// Delete obsolete syncdata (and it's Datasmith Element)
void FSyncDatabase::DeleteSyncData(const GS::Guid& InGuid)
{
	FSyncData** SyncData = ElementsSyncDataMap.Find(GSGuid2FGuid(InGuid));
	if (SyncData != nullptr)
	{
		ElementsSyncDataMap.Remove(GSGuid2FGuid(InGuid));
	}
	else
	{
		UE_AC_DebugF("FSyncDatabase::Delete {%s}\n", InGuid.ToUniString().ToUtf8());
	}
}

// Return the name of the specified layer
const FString& FSyncDatabase::GetLayerName(short InLayerIndex)
{
	FString* Found = LayerIndex2Name.Find(InLayerIndex);
	if (Found == nullptr)
	{
		LayerIndex2Name.Add(InLayerIndex, GSStringToUE(UE_AC::GetLayerName(InLayerIndex)));
		Found = LayerIndex2Name.Find(InLayerIndex);
		UE_AC_TestPtr(Found);
	}
	return *Found;
}

// Set the mesh in the handle and take care of mesh life cycle.
bool FSyncDatabase::SetMesh(TSharedPtr< IDatasmithMeshElement >*	   Handle,
							const TSharedPtr< IDatasmithMeshElement >& InMesh)
{
	if (Handle->IsValid())
	{
		if (InMesh.IsValid() && FCString::Strcmp(Handle->Get()->GetName(), InMesh->GetName()) == 0)
		{
			return false; // No change : Same name (hash) --> Same mesh
		}

		{
			GS::Guard< GS::Lock > lck(HashToMeshInfoAccesControl);

			typedef TMap< FString, FMeshInfo > FMapHashToMeshInfo;

			FMeshInfo* Older = HashToMeshInfo.Find(Handle->Get()->GetName());
			UE_AC_TestPtr(Older);
			if (--Older->Count == 0)
			{
				Scene->RemoveMesh(Older->Mesh);
				HashToMeshInfo.Remove(Handle->Get()->GetName());
			}
		}
		Handle->Reset();
	}
	else
	{
		if (!InMesh.IsValid())
		{
			return false; // No change : No mesh before and no mesh after
		}
	}

	if (InMesh.IsValid())
	{
		{
			GS::Guard< GS::Lock > lck(HashToMeshInfoAccesControl);
			FMeshInfo&			  MeshInfo = HashToMeshInfo.FindOrAdd(InMesh->GetName());
			if (!MeshInfo.Mesh.IsValid())
			{
				MeshInfo.Mesh = InMesh;
				Scene->AddMesh(InMesh);
			}
			++MeshInfo.Count;
		}
		*Handle = InMesh;
	}

	return true;
}

// Return the libpart from it's index
FLibPartInfo* FSyncDatabase::GetLibPartInfo(GS::Int32 InIndex)
{
	TUniquePtr< FLibPartInfo >* LibPartInfo = IndexToLibPart.Find(InIndex);
	if (LibPartInfo == nullptr)
	{
		LibPartInfo = &IndexToLibPart.Add(InIndex, MakeUnique< FLibPartInfo >());
		LibPartInfo->Get()->Initialize(InIndex);
	}
	return LibPartInfo->Get();
}

// Return the libpart from it's unique id
FLibPartInfo* FSyncDatabase::GetLibPartInfo(const char* InUnID)
{
	FGSUnID	  UnID;
	GSErrCode GSErr = UnID.InitWithString(InUnID);
	if (GSErr != NoError)
	{
		UE_AC_DebugF("FSyncDatabase::GetLibPartInfo - InitWithString(\"%s\") return error\n", InUnID);
		return nullptr;
	}
	if (UnID.Main == GS::NULLGuid && UnID.Rev == GS::NULLGuid)
	{
		return nullptr;
	}

	FLibPartInfo** LibPartInfoPtr = UnIdToLibPart.Find(UnID);
	if (LibPartInfoPtr == nullptr)
	{
		API_LibPart LibPart;
		Zap(&LibPart);
		strncpy(LibPart.ownUnID, InUnID, sizeof(LibPart.ownUnID));
		GSErrCode err = ACAPI_LibPart_Search(&LibPart, false);
		if (err != NoError)
		{
			UE_AC_DebugF("FSyncDatabase::GetLibPartInfo - Can't find libpart \"%s\"\n", InUnID);
			return nullptr;
		}
		FLibPartInfo* LibPartInfo = GetLibPartInfo(LibPart.index);
		UnIdToLibPart.Add(UnID, LibPartInfo);
		return LibPartInfo;
	}
	return *LibPartInfoPtr;
}

// Return the cache path
GS::UniString FSyncDatabase::GetCachePath()
{
	const TCHAR*				CacheDirectory = nullptr;
	IDatasmithExporterUIModule* DsExporterUIModule = IDatasmithExporterUIModule::Get();
	if (DsExporterUIModule != nullptr)
	{
		IDirectLinkUI* DLUI = DsExporterUIModule->GetDirectLinkExporterUI();
		if (DLUI != nullptr)
		{
			CacheDirectory = DLUI->GetDirectLinkCacheDirectory();
		}
	}
	if (CacheDirectory != nullptr && *CacheDirectory != 0)
	{
		return UEToGSString(CacheDirectory);
	}
	else
	{
		return GetAddonDataDirectory();
	}
}

// SetSceneInfo
void FSyncDatabase::SetSceneInfo()
{
	IDatasmithScene& TheScene = *Scene;

	// Set up basics scene informations
	TheScene.SetHost(TEXT("ARCHICAD"));
	TheScene.SetVendor(TEXT("Graphisoft"));
	TheScene.SetProductName(TEXT("ARCHICAD"));
	TheScene.SetProductVersion(UTF8_TO_TCHAR(UE_AC_STRINGIZE(AC_VERSION)));
}

// Scan all elements, to determine if they need to be synchronized
UInt32 FSyncDatabase::ScanElements(const FSyncContext& InSyncContext)
{
	// We create this objects here to not construct/destroy at each iteration
	FElementID ElementID(InSyncContext);

	// Loop on all 3D elements
	UInt32	  ModifiedCount = 0;
	GS::Int32 NbElements = InSyncContext.GetModel().GetElementCount();
	UE_AC_STAT(InSyncContext.Stats.TotalElements = NbElements);
	InSyncContext.NewPhase(kCommonCollectElements, NbElements);
	for (GS::Int32 IndexElement = 1; IndexElement <= NbElements; IndexElement++)
	{
		InSyncContext.NewCurrentValue(IndexElement);

		// Get next valid 3d element
		ElementID.InitElement(IndexElement);
		if (ElementID.IsInvalid())
		{
#if UE_AC_DO_TRACE && 1
			UE_AC_TraceF("FSynchronizer::ScanElements - Element Index=%d Is invalid\n", IndexElement);
#endif
			continue;
		}

		API_Guid ElementGuid = GSGuid2APIGuid(ElementID.GetElement3D().GetElemGuid());
		if (ElementGuid == APINULLGuid)
		{
#if UE_AC_DO_TRACE && 1
			UE_AC_TraceF("FSynchronizer::ScanElements - Element Index=%d hasn't id\n", IndexElement);
#endif
			continue;
		}

#if UE_AC_DO_TRACE && 1
		// Get the name of the element (To help debugging)
		GS::UniString ElementInfo;
		FElementTools::GetInfoString(ElementGuid, &ElementInfo);

	#if 0
		// Print element info in debugger view
		FElement2String::DumpInfo(ElementGuid);
	#endif
#endif

		// Check 3D geometry bounding box
		Box3D box = ElementID.GetElement3D().GetBounds(ModelerAPI::CoordinateSystem::ElemLocal);

		// Bonding box is empty, must not happen, but it happen
		if (box.xMin > box.xMax || box.yMin > box.yMax || box.zMin > box.zMax)
		{
#if UE_AC_DO_TRACE && 1
			UE_AC_TraceF("FSynchronizer::ScanElements - EmptyBox for %s \"%s\" %d %s", ElementID.GetTypeName(),
						 ElementInfo.ToUtf8(), IndexElement, APIGuidToString(ElementGuid).ToUtf8());
#endif
			continue; // Object is invisible (hidden layer or cutting plane)
		}

		// Get the header (modification time, layer, floor, element type...)
		if (!ElementID.InitHeader())
		{
#if UE_AC_DO_TRACE && 1
			UE_AC_DebugF("FSynchronizer::ScanElements - Can't get header for %d %s", IndexElement,
						 APIGuidToString(ElementGuid).ToUtf8());
#endif
			continue;
		}

		UE_AC_STAT(InSyncContext.Stats.TotalElementsWithGeometry++);

		// Get sync data for this element (Create or reuse already existing)
		FSyncData*& SyncData = GetSyncData(APIGuid2GSGuid(ElementID.GetHeader().guid));
		if (SyncData == nullptr)
		{
			SyncData = new FSyncData::FElement(APIGuid2GSGuid(ElementID.GetHeader().guid), InSyncContext);
		}
		ElementID.SetSyncData(SyncData);
		SyncData->Update(ElementID);
		if (SyncData->IsModified())
		{
			++ModifiedCount;
		}

		// Add lights
		if (ElementID.GetElement3D().GetLightCount() > 0)
		{
			ScanLights(ElementID);
		}
	}

	UE_AC_STAT(InSyncContext.Stats.TotalElementsModified = ModifiedCount);

	InSyncContext.NewCurrentValue(NbElements);

	return ModifiedCount;
}

#if defined(DEBUG) && 0
	#define UE_AC_DO_TRACE_LIGHTS 1
#else
	#define UE_AC_DO_TRACE_LIGHTS 0
#endif

// Scan all lights of this element
void FSyncDatabase::ScanLights(const FElementID& InElementID)
{
	GS::Int32 LightsCount = InElementID.GetElement3D().GetLightCount();
	if (LightsCount > 0)
	{
		const API_Elem_Head& Header = InElementID.GetHeader();
#if UE_AC_DO_TRACE_LIGHTS
		UE_AC_TraceF("%s", FElement2String::GetElementAsShortString(Header.guid).c_str());
		UE_AC_TraceF("%s", FElement2String::GetParametersAsString(Header.guid).c_str());
		UE_AC_TraceF("%s", F3DElement2String::ElementLight2String(InElementID.Element3D).c_str());
#endif
		double		  Intensity = 1.0;
		bool		  bUseIES = false;
		GS::UniString IESFileName;
		FAutoMemo	  AutoMemo(Header.guid, APIMemoMask_AddPars);
		if (AutoMemo.GSErr == NoError)
		{
			if (AutoMemo.Memo.params) // Can be null
			{
				double value;
				if (GetParameter(AutoMemo.Memo.params, "gs_light_intensity", &value))
				{
					Intensity = value / 100.0;
				}
				if (GetParameter(AutoMemo.Memo.params, "c4dPhoPhotometric", &value))
				{
					bUseIES = value != 0;
				}
				GetParameter(AutoMemo.Memo.params, "c4dPhoIESFile", &IESFileName);
			}
		}
		else
		{
			UE_AC_DebugF("FSyncDatabase::ScanLights - Error=%d when getting element memo\n", AutoMemo.GSErr);
		}

		ModelerAPI::Light Light;

		for (GS::Int32 LightIndex = 1; LightIndex <= LightsCount; ++LightIndex)
		{
			InElementID.GetElement3D().GetLight(LightIndex, &Light);
			ModelerAPI::Light::Type LightType = Light.GetType();
			if (LightType == ModelerAPI::Light::DirectionLight || LightType == ModelerAPI::Light::SpotLight ||
				LightType == ModelerAPI::Light::PointLight)
			{
				API_Guid	LightId = CombineGuid(Header.guid, GuidFromMD5(LightIndex));
				FSyncData*& SyncData = FSyncDatabase::GetSyncData(APIGuid2GSGuid(LightId));
				if (SyncData == nullptr)
				{
					SyncData = new FSyncData::FLight(APIGuid2GSGuid(LightId), LightIndex);
					SyncData->SetParent(InElementID.GetSyncData());
					SyncData->MarkAsModified();
				}
				FSyncData::FLight& LightSyncData = static_cast< FSyncData::FLight& >(*SyncData);
				LightSyncData.MarkAsExisting();

				const float	 InnerConeAngle = float(Light.GetFalloffAngle1() * 180.0f / PI);
				const float	 OuterConeAngle = float(Light.GetFalloffAngle2() * 180.0f / PI);
				FLinearColor LightColor = ACRGBColorToUELinearColor(Light.GetColor());

				LightSyncData.SetValues(LightType, InnerConeAngle, OuterConeAngle, LightColor);
				LightSyncData.SetValuesFromParameters(Intensity, bUseIES, IESFileName);
				LightSyncData.Placement(FGeometryUtil::GetTranslationVector(Light.GetPosition()),
										FGeometryUtil::GetRotationQuat(Light.GetDirection()));
			}
		}
	}
}

// Scan all cameras
void FSyncDatabase::ScanCameras(const FSyncContext& /* InSyncContext */)
{
	GS::Array< API_Guid > ElemList;
	GSErrCode			  GSErr = ACAPI_Element_GetElemList(API_CamSetID, &ElemList);
	if (GSErr != NoError)
	{
		UE_AC_DebugF("FSyncDatabase::ScanCameras - ACAPI_Element_GetElemList return %d", GSErr);
		return;
	}
	Int32	 IndexCamera = 0;
	API_Guid NextCamera = APINULLGuid;

	for (API_Guid ElemGuid : ElemList)
	{
		// Get info on this element
		API_Element cameraSet;
		Zap(&cameraSet);
		cameraSet.header.guid = ElemGuid;
		GSErr = ACAPI_Element_Get(&cameraSet);
		if (GSErr != NoError)
		{
			if (GSErr != APIERR_DELETED)
			{
				UE_AC_DebugF("FSyncDatabase::ScanCameras - ACAPI_Element_Get return %d", GSErr);
			}
			continue;
		}
		if (cameraSet.camset.firstCam == APINULLGuid)
		{
			continue;
		}

		FSyncData*& CameraSetSyncData = FSyncDatabase::GetSyncData(APIGuid2GSGuid(cameraSet.header.guid));
		if (CameraSetSyncData == nullptr)
		{
			GS::UniString CamSetName(cameraSet.camset.name);
			CameraSetSyncData = new FSyncData::FCameraSet(APIGuid2GSGuid(cameraSet.header.guid), CamSetName,
														  cameraSet.camset.perspPars.openedPath);
			CameraSetSyncData->SetParent(&GetSceneSyncData());
		}
		CameraSetSyncData->MarkAsExisting();

		IndexCamera = 0;
		NextCamera = cameraSet.camset.firstCam;
		while (GSErr == NoError && NextCamera != APINULLGuid)
		{
			API_Element camera;
			Zap(&camera);
			camera.header.guid = NextCamera;
			GSErr = ACAPI_Element_Get(&camera);
			if (GSErr == NoError)
			{
				FSyncData*& CameraSyncData = FSyncDatabase::GetSyncData(APIGuid2GSGuid(camera.header.guid));
				if (CameraSyncData == nullptr)
				{
					CameraSyncData = new FSyncData::FCamera(APIGuid2GSGuid(camera.header.guid), ++IndexCamera);
					CameraSyncData->SetParent(CameraSetSyncData);
				}
				CameraSyncData->MarkAsExisting();
				CameraSyncData->CheckModificationStamp(camera.header.modiStamp);
			}
			NextCamera = camera.camera.perspCam.nextCam;
		}

		if (GSErr != NoError && GSErr != APIERR_DELETED)
		{
			UE_AC_DebugF("FSyncDatabase::ScanCameras - ACAPI_Element_Get return %d", GSErr);
		}
	}
}

END_NAMESPACE_UE_AC
