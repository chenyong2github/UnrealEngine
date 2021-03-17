// Copyright Epic Games, Inc. All Rights Reserved.

#include "SyncData.h"

#include "ElementID.h"
#include "Synchronizer.h"
#include "ElementTools.h"
#include "Element2StaticMesh.h"
#include "MetaData.h"
#include "GeometryUtil.h"
#include "AutoChangeDatabase.h"

BEGIN_NAMESPACE_UE_AC

// Constructor
FSyncData::FSyncData(const GS::Guid& InGuid)
	: ElementId(InGuid)
{
}

// Destructor
FSyncData::~FSyncData()
{
	if (Parent != nullptr)
	{
		UE_AC_DebugF("FSyncData::~FSyncData - Deleting child while attached to it's parent {%s}\n",
					 ElementId.ToUniString().ToUtf8());
		Parent->RemoveChild(this);
		Parent = nullptr;
	}
	for (size_t i = Childs.size(); i != 0; --i)
	{
		Childs[0]->SetParent(nullptr);
		UE_AC_Assert(i == Childs.size());
	}
}

// Update data from a 3d element
void FSyncData::Update(const FElementID& InElementId)
{
	UE_AC_Assert(ElementId == APIGuid2GSGuid(InElementId.ElementHeader.guid));
	UE_AC_Assert(Index3D == 0 && InElementId.Index3D != 0);

	Index3D = InElementId.Index3D;
	if (GenId != InElementId.Element3D.GetGenId())
	{
		GenId = InElementId.Element3D.GetGenId();
		bIsModified = true;
	}

	// If AC element has been modified, recheck connections
	if (ModificationStamp != InElementId.ElementHeader.modiStamp)
	{
		ModificationStamp = InElementId.ElementHeader.modiStamp;
		InElementId.HandleDepedencies();
		bIsModified = true;
	}
	if (Parent == nullptr)
	{
		SetParent(&InElementId.SyncContext.GetSyncDatabase().GetLayerSyncData(InElementId.ElementHeader.layer));
	}
}

// Recursively clean. Delete element that hasn't 3d geometry related to it
void FSyncData::CleanAfterScan(FSyncDatabase* IOSyncDatabase)
{
	for (size_t IdxChild = Childs.size(); IdxChild != 0;)
	{
		Childs[--IdxChild]->CleanAfterScan(IOSyncDatabase);
	}
	if (Childs.size() == 0 && Index3D == 0)
	{
		DeleteMe(IOSyncDatabase);
	}
}

void FSyncData::DeleteMe(FSyncDatabase* IOSyncDatabase)
{
	SetParent(nullptr);
	IOSyncDatabase->DeleteSyncData(ElementId);
	delete this;
}

void FSyncData::SetParent(FSyncData* InParent)
{
	if (Parent != InParent)
	{
		if (InParent)
		{
			InParent->AddChild(this);
		}
		if (Parent)
		{
			Parent->RemoveChild(this);
		}
		Parent = InParent;
	}
}

void FSyncData::ProcessTree(FProcessInfo* IOProcessInfo)
{
	Process(IOProcessInfo);
	for (std::vector< FSyncData* >::iterator IterChild = Childs.begin(); IterChild != Childs.end(); ++IterChild)
	{
		(**IterChild).ProcessTree(IOProcessInfo);
	}
}

// Add a child to this sync data
void FSyncData::AddChild(FSyncData* InChild)
{
	UE_AC_TestPtr(InChild);

	for (auto& i : Childs)
	{
		if (i == InChild)
		{
			UE_AC_VerboseF("FSyncData::AddChild - Child already present\n");
			return;
		}
	}
	Childs.push_back(InChild);
}

// Remove a child from this sync data
void FSyncData::RemoveChild(FSyncData* InChild)
{
	for (std::vector< FSyncData* >::iterator IterChild = Childs.begin(); IterChild != Childs.end(); ++IterChild)
	{
		if (*IterChild == InChild)
		{
			Childs.erase(IterChild);
			return;
		}
	}
	UE_AC_VerboseF("FSyncData::RemoveChild - Child not present\n");
}

#pragma mark -

// Guid given to the scene element.
const GS::Guid FSyncData::FScene::SceneGUID("CBDEFBEF-0D4E-4162-8C4C-64AC34CEB4E6");

FSyncData::FScene::FScene()
	: FSyncData(SceneGUID)
{
}

void FSyncData::FScene::AddChildActor(const TSharedPtr< IDatasmithActorElement >& InActor)
{
	UE_AC_Assert(SceneElement.IsValid());
	SceneElement->AddActor(InActor);
}

// Set (or replace) datasmith actor element related to this sync data
void FSyncData::FScene::SetActorElement(const TSharedPtr< IDatasmithActorElement >& /* InActor */)
{
	UE_AC_Assert(false); // Scene is not an actor
}

// Set the element to the scene element
void FSyncData::FScene::Process(FProcessInfo* IOProcessInfo)
{
	if (SceneElement.IsValid())
	{
		UE_AC_Assert(SceneElement == IOProcessInfo->SyncContext.GetSyncDatabase().GetScene());
	}
	SceneElement = IOProcessInfo->SyncContext.GetSyncDatabase().GetScene();
}

// Return Element as an actor
const TSharedPtr< IDatasmithActorElement >& FSyncData::FScene::GetActorElement() const
{
	static TSharedPtr< IDatasmithActorElement > NotAnActor;
	return NotAnActor;
}

void FSyncData::FScene::RemoveChildActor(const TSharedPtr< IDatasmithActorElement >& InActor)
{
	UE_AC_Assert(SceneElement.IsValid());
	SceneElement->RemoveActor(InActor, EDatasmithActorRemovalRule::RemoveChildren);
}

#pragma mark -

FSyncData::FActor::FActor(const GS::Guid& InGuid)
	: FSyncData(InGuid)
{
}

void FSyncData::FActor::DeleteMe(FSyncDatabase* IOSyncDatabase)
{
	IOSyncDatabase->GetScene()->RemoveMetaData(MetaData);
	SetActorElement(TSharedPtr< IDatasmithActorElement >());
	FSyncData::DeleteMe(IOSyncDatabase);
}

void FSyncData::FActor::AddChildActor(const TSharedPtr< IDatasmithActorElement >& InActor)
{
	UE_AC_Assert(ActorElement.IsValid());
	ActorElement->AddChild(InActor);
}

void FSyncData::FActor::RemoveChildActor(const TSharedPtr< IDatasmithActorElement >& InActor)
{
	UE_AC_Assert(ActorElement.IsValid());
	ActorElement->RemoveChild(InActor);
}

// Set (or replace) datasmith actor element related to this sync data
void FSyncData::FActor::SetActorElement(const TSharedPtr< IDatasmithActorElement >& InElement)
{
	if (ActorElement != InElement)
	{
		UE_AC_TestPtr(Parent);
		if (ActorElement.IsValid())
		{
			Parent->RemoveChildActor(ActorElement);
			ActorElement.Reset();
		}
		if (InElement.IsValid())
		{
			Parent->AddChildActor(InElement);
			ActorElement = InElement;
		}
	}
}

// Add meta data
void FSyncData::FActor::AddTags(const FElementID& InElementID)
{
	UE_AC_Assert(ActorElement.IsValid());

	GS::UniString TagUniqueID = GS::UniString("Element.UniqueID.") + ElementId.ToUniString();
	ActorElement->AddTag(GSStringToUE(TagUniqueID));
	GS::UniString TagElementType =
		GS::UniString("Element.Type.") + FElementTools::TypeName(InElementID.ElementHeader.typeID);
	ActorElement->AddTag(GSStringToUE(TagElementType));

	GS::Array< GS::Pair< API_ClassificationSystem, API_ClassificationItem > > ApiClassifications;
	GSErrCode GSErr = FElementTools::GetElementClassifications(ApiClassifications, InElementID.ElementHeader.guid);

	if (GSErr == NoError)
	{
		for (const GS::Pair< API_ClassificationSystem, API_ClassificationItem >& Classification : ApiClassifications)
		{
			GS::UniString TagClassificationID = GS::UniString("Classification.ID.") + Classification.second.id;
			ActorElement->AddTag(GSStringToUE(TagClassificationID));
		}
	}
	else
	{
		UE_AC_DebugF("FSyncData::AddTags - FElementTools::GetElementClassifications returned error %d", GSErr);
	}
}

#pragma mark -

// Guid used to synthetize layer guid
const GS::Guid FSyncData::FLayer::LayerGUID("97D32F90-A33E-0000-8305-D1A7D3FCED66");

// Return the synthetized layer guid.
GS::Guid FSyncData::FLayer::GetLayerGUID(short Layer)
{
	GS::Guid TmpGUID = LayerGUID;
	reinterpret_cast< short* >(&TmpGUID)[3] = Layer;
	return TmpGUID;
}

// Return true if this guid is for a layer
short FSyncData::FLayer::IsLayerGUID(GS::Guid LayerID)
{
	reinterpret_cast< short* >(&LayerID)[3] = 0;
	return LayerID == LayerGUID;
}

// Return the layer index
short FSyncData::FLayer::GetLayerIndex(const GS::Guid& InLayerID)
{
	return reinterpret_cast< const short* >(&InLayerID)[3];
}

FSyncData::FLayer::FLayer(const GS::Guid& InGuid)
	: FSyncData::FActor(InGuid)
{
}

void FSyncData::FLayer::Process(FProcessInfo* /* IOProcessInfo */)
{
	if (!ActorElement.IsValid())
	{
		short LayerIndex = GetLayerIndex(ElementId);

		// Get the layer's name
		GS::UniString LayerName;

		API_Attribute attribute;
		Zap(&attribute);
		attribute.header.typeID = API_LayerID;
		attribute.header.index = short(LayerIndex);
		attribute.header.uniStringNamePtr = &LayerName;
		GSErrCode error = ACAPI_Attribute_Get(&attribute);
		if (error != NoError)
		{
			// This case happened for the special ArchiCAD layer
			UE_AC_DebugF("CElementsHierarchy::CreateLayerNode - Error %d for layer index=%d\n", error, LayerIndex);
			if (error == APIERR_DELETED)
			{
				LayerName = GetGSName(kName_LayerDeleted);
			}
			else
			{
				LayerName = GS::UniString::Printf(GetGSName(kName_LayerError), error);
			}
		}
		else if (LayerName == "\x14") // Special ARCHICAD layer
			LayerName = "ARCHICAD";
		UE_AC_Assert(LayerName.GetLength() > 0);
		GS::Guid							 LayerGuid = APIGuid2GSGuid(attribute.layer.head.guid);
		TSharedRef< IDatasmithActorElement > NewActor =
			FDatasmithSceneFactory::CreateActor(GSStringToUE(LayerGuid.ToUniString()));
		NewActor->SetLabel(GSStringToUE(LayerName));
		SetActorElement(NewActor);
	}
}

FSyncData::FElement::FElement(const GS::Guid& InGuid)
	: FSyncData::FActor(InGuid)
{
}

void FSyncData::FElement::Process(FProcessInfo* IOProcessInfo)
{
	if (Index3D == 0)
	{
		if (!ActorElement.IsValid())
		{
			IOProcessInfo->ElementID.InitElement(this);
			IOProcessInfo->ElementID.InitHeader(GSGuid2APIGuid(ElementId));

			UE_AC_STAT(IOProcessInfo->SyncContext.Stats.TotalOwnerCreated++);
			TSharedRef< IDatasmithActorElement > NewActor =
				FDatasmithSceneFactory::CreateActor(GSStringToUE(ElementId.ToUniString()));

			GS::UniString ElemenInfo;
			if (FElementTools::GetInfoString(IOProcessInfo->ElementID.ElementHeader.guid, &ElemenInfo))
			{
				NewActor->SetLabel(GSStringToUE(ElemenInfo));
			}
			else
			{
				NewActor->SetLabel(TEXT("Unnamed"));
			}

			SetActorElement(NewActor);
			AddTags(IOProcessInfo->ElementID);
			UpdateMetaData(IOProcessInfo->SyncContext.GetScene());
		}
	}
	else
	{
		if (IsModified())
		{
			// Advance progression bar to the current value
			IOProcessInfo->SyncContext.NewCurrentValue(++IOProcessInfo->ProgessValue);

			IOProcessInfo->ElementID.InitElement(this);
			IOProcessInfo->ElementID.InitHeader();

			if (!ActorElement.IsValid() || !ActorElement->IsA(EDatasmithElementType::StaticMeshActor))
			{
				UE_AC_STAT(IOProcessInfo->SyncContext.Stats.TotalActorsCreated++);
				TSharedRef< IDatasmithMeshActorElement > NewActor =
					FDatasmithSceneFactory::CreateMeshActor(GSStringToUE(ElementId.ToUniString()));

				GS::UniString ElemenInfo;
				if (FElementTools::GetInfoString(IOProcessInfo->ElementID.ElementHeader.guid, &ElemenInfo))
				{
					NewActor->SetLabel(GSStringToUE(ElemenInfo));
				}
				else
				{
					NewActor->SetLabel(TEXT("Unnamed"));
				}

				NewActor->SetLayer(*IOProcessInfo->SyncContext.GetSyncDatabase().GetLayerName(
					IOProcessInfo->ElementID.ElementHeader.layer));

				SetActorElement(NewActor);
				AddTags(IOProcessInfo->ElementID);
				UpdateMetaData(IOProcessInfo->SyncContext.GetScene());
			}
			CreateMesh(&IOProcessInfo->ElementID);
		}
	}
}

// Create/Update mesh of this element
void FSyncData::FElement::CreateMesh(FElementID* IOElementID)
{
	UE_AC_TestPtr(IOElementID);
	UE_AC_TestPtr(IOElementID->SyncData);
	UE_AC_Assert(ActorElement.IsValid() && ActorElement->IsA(EDatasmithElementType::StaticMeshActor));
	IDatasmithMeshActorElement& MeshActor = *StaticCastSharedPtr< IDatasmithMeshActorElement >(ActorElement).Get();

	Box3D	 Bounds = IOElementID->Element3D.GetBounds();
	Vector3D center(-(Bounds.xMin + Bounds.xMax) * 0.5, -(Bounds.yMin + Bounds.yMax) * 0.5, -Bounds.zMin);

	Geometry::Transformation3D World2Local;
	UE_AC_Assert(World2Local.IsIdentity());
	World2Local.AddTranslation(center);

	Geometry::Transformation3D Local2World(World2Local.GetInverse());

	FVector inverseTranslation(float(center.x), -float(center.y), -float(center.z));
	inverseTranslation *= float(IOElementID->SyncContext.ScaleLength);
	MeshActor.SetTranslation(inverseTranslation);

	TSharedPtr< IDatasmithMeshElement > PreviousMesh;

	// Create the mesh
	TSharedPtr< IDatasmithMeshElement > Mesh;
	{
		FElement2StaticMesh Element2StaticMesh(IOElementID->SyncContext, World2Local);
		Element2StaticMesh.AddElementGeometry(IOElementID->Element3D);
		Mesh = Element2StaticMesh.CreateMesh();
		Mesh->SetLabel(MeshActor.GetLabel());
	}

	// If we already have a mesh before forget it if new one is different
	if (MeshElement.IsValid() && FCString::Strcmp(MeshElement->GetName(), Mesh->GetName()) != 0)
	{
		UE_AC_VerboseF("FSyncData::FElement::CreateMesh - Updating Mesh %s -> %s\n",
					   TCHAR_TO_UTF8(MeshElement->GetName()), TCHAR_TO_UTF8(Mesh->GetName()));
		IOElementID->SyncContext.GetScene().RemoveMesh(MeshElement.ToSharedRef());
		MeshElement = TSharedPtr< IDatasmithMeshElement >();
	}

	// If new mesh differ from previous one
	if (!MeshElement.IsValid())
	{
		IOElementID->SyncContext.GetScene().AddMesh(Mesh);
		MeshElement = Mesh;
	}

	// Do we create an new actor or reuse previous one
	const TCHAR* PreviousMeshPathName = MeshActor.GetStaticMeshPathName();
	if (!IsStringEmpty(PreviousMeshPathName) && FCString::Strcmp(PreviousMeshPathName, Mesh->GetName()) != 0)
	{
		UE_AC_VerboseF("FSyncData::FElement::CreateMesh - Replacing previous mesh \"%s\" by \"%s\"\n",
					   TCHAR_TO_UTF8(PreviousMeshPathName), TCHAR_TO_UTF8(Mesh->GetName()));
	}

	MeshActor.SetStaticMeshPathName(Mesh->GetName());
}

// Rebuild the meta data of this element
void FSyncData::FElement::UpdateMetaData(IDatasmithScene& IOScene)
{
	if (MetaData.IsValid())
	{
		IOScene.RemoveMetaData(MetaData);
		MetaData.Reset();
	}
	FMetaData MetaDataExporter(ElementId);
	MetaDataExporter.ExportMetaData();
	MetaData = MetaDataExporter.GetMetaData();
	MetaData->SetAssociatedElement(ActorElement);
	IOScene.AddMetaData(MetaData);
}

void FSyncData::FCameraSet::Process(FProcessInfo* /* IOProcessInfo */)
{
	if (!ActorElement.IsValid())
	{
		TSharedRef< IDatasmithActorElement > NewActor =
			FDatasmithSceneFactory::CreateActor(GSStringToUE(ElementId.ToUniString()));

		NewActor->SetLabel(GSStringToUE(Name));

		SetActorElement(NewActor);

		if (bOpenedPath)
		{
			NewActor->AddTag(TEXT("Path.opened"));
		}
		else
		{
			NewActor->AddTag(TEXT("Path.closed"));
		}
	}
}

// Guid given to the current view.
const GS::Guid FSyncData::FCamera::CurrentViewGUID("B2BD9C50-60EB-4E64-902B-D1574FADEC45");

void FSyncData::FCamera::Process(FProcessInfo* /* IOProcessInfo */)
{
	if (!ActorElement.IsValid() || IsModified())
	{
		if (!ActorElement.IsValid())
		{
			SetActorElement(FDatasmithSceneFactory::CreateCameraActor(GSStringToUE(ElementId.ToUniString())));
		}

		if (ElementId == CurrentViewGUID)
		{
			InitWithCurrentView();
		}
		else
		{
			InitWithCameraElement();
		}
	}
}

void FSyncData::FCamera::InitWithCurrentView()
{
	FAutoChangeDatabase changeDB(APIWind_3DModelID);

	IDatasmithCameraActorElement& CameraElement = static_cast< IDatasmithCameraActorElement& >(*ActorElement.Get());

	// Set the camera data from AC 3D projection info
	API_3DProjectionInfo projSets;
	GSErrCode			 GSErr = ACAPI_Environment(APIEnv_Get3DProjectionSetsID, &projSets, NULL);
	if (GSErr == GS::NoError)
	{
		if (projSets.isPersp)
		{
			CameraElement.SetTranslation(FGeometryUtil::GetTranslationVector(
				{projSets.u.persp.pos.x, projSets.u.persp.pos.y, projSets.u.persp.cameraZ}));

			CameraElement.SetRotation(FGeometryUtil::GetRotationQuat(
				FGeometryUtil::GetPitchAngle(projSets.u.persp.cameraZ, projSets.u.persp.targetZ,
											 projSets.u.persp.distance),
				projSets.u.persp.azimuth, projSets.u.persp.rollAngle));
			CameraElement.SetFocusDistance(FGeometryUtil::GetDistance3D(
				abs(projSets.u.persp.cameraZ - projSets.u.persp.targetZ), projSets.u.persp.distance));
			CameraElement.SetFocalLength(
				FGeometryUtil::GetCameraFocalLength(CameraElement.GetSensorWidth(), projSets.u.persp.viewCone));
		}
		else
		{
			// Get the matrix.
			CameraElement.SetTranslation(FGeometryUtil::GetTranslationVector(
				reinterpret_cast< const double(*)[4] >(projSets.u.axono.invtranmat.tmx)));
			CameraElement.SetRotation(FGeometryUtil::GetRotationQuat(
				reinterpret_cast< const double(*)[4] >(projSets.u.axono.invtranmat.tmx)));
			CameraElement.SetFocusDistance(10000);
			CameraElement.SetFocalLength(FGeometryUtil::GetCameraFocalLength(CameraElement.GetSensorWidth(), 45));
		}
	}
	else
	{
		UE_AC_DebugF("FSyncData::FCamera::InitWithCurrentView - APIEnv_Get3DProjectionSetsID returned error %d\n",
					 GSErr);
	}
}

void FSyncData::FCamera::InitWithCameraElement()
{
	API_Element camera;
	Zap(&camera);
	camera.header.guid = GSGuid2APIGuid(ElementId);
	UE_AC_TestGSError(ACAPI_Element_Get(&camera));

	IDatasmithCameraActorElement& CameraElement = static_cast< IDatasmithCameraActorElement& >(*ActorElement.Get());
	const TCHAR*				  cameraSetLabel = TEXT("Unamed camera");
	UE_AC_TestPtr(Parent);
	if (Parent->GetElement().IsValid())
	{
		cameraSetLabel = Parent->GetElement()->GetLabel();
	}
	CameraElement.SetLabel(*FString::Printf(TEXT("%s %d"), cameraSetLabel, Index));

	const API_PerspPars& camPars = camera.camera.perspCam.persp;

	CameraElement.SetTranslation(FGeometryUtil::GetTranslationVector({camPars.pos.x, camPars.pos.y, camPars.cameraZ}));

	CameraElement.SetRotation(
		FGeometryUtil::GetRotationQuat(FGeometryUtil::GetPitchAngle(camPars.cameraZ, camPars.targetZ, camPars.distance),
									   camPars.azimuth, camPars.rollAngle));

	CameraElement.SetFocusDistance(
		FGeometryUtil::GetDistance3D(abs(camPars.cameraZ - camPars.targetZ), camPars.distance));
	CameraElement.SetFocalLength(FGeometryUtil::GetCameraFocalLength(CameraElement.GetSensorWidth(), camPars.viewCone));
}

void FSyncData::FLight::Process(FProcessInfo* /* IOProcessInfo */)
{
	if (!ActorElement.IsValid())
	{
		switch (Type)
		{
			case ModelerAPI::Light::Type::DirectionLight:
				SetActorElement(FDatasmithSceneFactory::CreateDirectionalLight(GSStringToUE(ElementId.ToUniString())));
				break;
			case ModelerAPI::Light::Type::SpotLight:
				{
					TSharedRef< IDatasmithSpotLightElement > SpotLight =
						FDatasmithSceneFactory::CreateSpotLight(GSStringToUE(ElementId.ToUniString()));
					SpotLight->SetInnerConeAngle(InnerConeAngle);
					SpotLight->SetOuterConeAngle(OuterConeAngle);
					SetActorElement(SpotLight);
					break;
				}
			case ModelerAPI::Light::Type::PointLight:
				SetActorElement(FDatasmithSceneFactory::CreatePointLight(GSStringToUE(ElementId.ToUniString())));
				break;
			default:
				throw std::runtime_error(
					Utf8StringFormat("FSyncData::FLight::Process - Invalid light type %d\n", Type).c_str());
		}
	}
	if (IsModified())
	{
		IDatasmithLightActorElement& LightElement = static_cast< IDatasmithLightActorElement& >(*ActorElement.Get());

		const TCHAR* parentLabel = TEXT("Unamed object");
		UE_AC_TestPtr(Parent);
		if (Parent->GetElement().IsValid())
		{
			parentLabel = Parent->GetElement()->GetLabel();
		}
		LightElement.SetLabel(*FString::Printf(TEXT("%s - Light %d"), parentLabel, Index));
		if (Parent->GetActorElement().IsValid())
		{
			LightElement.SetLayer(Parent->GetActorElement()->GetLayer());
		}

		LightElement.SetTranslation(Position);
		LightElement.SetRotation(Rotation);
		LightElement.SetIntensity(5000.0);
		LightElement.SetColor(Color);
		if (Color == FLinearColor(0, 0, 0))
		{
			LightElement.SetEnabled(false);
		}
	}
}

END_NAMESPACE_UE_AC
