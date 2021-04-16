// Copyright Epic Games, Inc. All Rights Reserved.

#include "SyncData.h"

#include "ElementID.h"
#include "Synchronizer.h"
#include "Commander.h"
#include "Utils/ElementTools.h"
#include "Element2StaticMesh.h"
#include "MetaData.h"
#include "GeometryUtil.h"
#include "Utils/AutoChangeDatabase.h"

DISABLE_SDK_WARNINGS_START
#include "Transformation.hpp"
#include "Line3D.hpp"
DISABLE_SDK_WARNINGS_END

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
	SetDefaultParent(InElementId);
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

// Delete this sync data
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

// Connect this actor to a default parent if it doesn't already have one
void FSyncData::SetDefaultParent(const FElementID& InElementID)
{
	if (!HasParent())
	{
		if (InElementID.ElementHeader.hotlinkGuid == APINULLGuid)
		{
			// Parent is a layer
			SetParent(&InElementID.SyncContext.GetSyncDatabase().GetLayerSyncData(InElementID.ElementHeader.layer));
		}
		else
		{
			// Parent is a hot link instance
			FSyncData*& ParentFound = InElementID.SyncContext.GetSyncDatabase().GetSyncData(
				APIGuid2GSGuid(InElementID.ElementHeader.hotlinkGuid));
			if (ParentFound == nullptr)
			{
				ParentFound = new FSyncData::FHotLinkInstance(APIGuid2GSGuid(InElementID.ElementHeader.hotlinkGuid),
															  &InElementID.SyncContext.GetSyncDatabase());
			}
			SetParent(ParentFound);
		}
	}
}

void FSyncData::ProcessTree(FProcessInfo* IOProcessInfo)
{
	Process(IOProcessInfo);
	for (size_t IterChild = 0; IterChild < Childs.size(); ++IterChild)
	{
		IOProcessInfo->Index = IterChild;
		Childs[IterChild]->ProcessTree(IOProcessInfo);
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

// Return true if this element and all it's childs have been cut out
bool FSyncData::CheckAllCutOut()
{
	return true;
}

#pragma mark -

// Guid given to the scene element.
const GS::Guid FSyncData::FScene::SceneGUID("CBDEFBEF-0D4E-4162-8C4C-64AC34CEB4E6");

FSyncData::FScene::FScene()
	: FSyncData(SceneGUID)
{
}

// Delete this sync data
void FSyncData::FScene::DeleteMe(FSyncDatabase* IOSyncDatabase)
{
	if (SceneInfoMetaData.IsValid())
	{
		IOSyncDatabase->GetScene()->RemoveMetaData(SceneInfoMetaData);
		SceneInfoMetaData.Reset();
	}
	if (SceneInfoActorElement.IsValid())
	{
		IOSyncDatabase->GetScene()->RemoveActor(SceneInfoActorElement, EDatasmithActorRemovalRule::RemoveChildren);
		SceneInfoActorElement.Reset();
	}
	FSyncData::DeleteMe(IOSyncDatabase);
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
	UpdateInfo(IOProcessInfo);
}

void FSyncData::FScene::UpdateInfo(FProcessInfo* IOProcessInfo)
{
	if (!SceneInfoActorElement.IsValid())
	{
		SceneInfoActorElement = FDatasmithSceneFactory::CreateActor(GSStringToUE(SceneGUID.ToUniString()));
		IOProcessInfo->SyncContext.GetScene().AddActor(SceneInfoActorElement);
	}

	FMetaData InfoMetaData(SceneInfoActorElement);

	GSErrCode	  err = NoError;
	GS::UniString projectName = GS::UniString("Untitled");

	// Project info
	{
		API_ProjectInfo projectInfo;
		err = ACAPI_Environment(APIEnv_ProjectID, &projectInfo);
		if (err == NoError)
		{
			if (!projectInfo.untitled || projectInfo.projectName == nullptr)
				projectName = *projectInfo.projectName;
			InfoMetaData.AddStringProperty(TEXT("ProjectName"), projectName);

			if (projectInfo.projectPath != nullptr)
			{
				InfoMetaData.AddStringProperty(TEXT("ProjectPath"), *projectInfo.projectPath);
			}

			if (projectInfo.location != nullptr)
			{
				InfoMetaData.AddStringProperty(TEXT("ProjectLocation"), projectInfo.location->ToDisplayText());
			}

			if (projectInfo.location_team != nullptr)
			{
				InfoMetaData.AddStringProperty(TEXT("SharedProjectLocation"),
											   projectInfo.location_team->ToDisplayText());
			}
		}
	}

	// Project note info
	{
		API_ProjectNoteInfo projectNoteInfo;
		BNZeroMemory(&projectNoteInfo, sizeof(API_ProjectNoteInfo));
		err = ACAPI_Environment(APIEnv_GetProjectNotesID, &projectNoteInfo);

		if (err == NoError)
		{
			InfoMetaData.AddStringProperty(TEXT("Client"), projectNoteInfo.client);
			InfoMetaData.AddStringProperty(TEXT("Company"), projectNoteInfo.company);
			InfoMetaData.AddStringProperty(TEXT("Country"), projectNoteInfo.country);
			InfoMetaData.AddStringProperty(TEXT("PostalCode"), projectNoteInfo.code);
			InfoMetaData.AddStringProperty(TEXT("City"), projectNoteInfo.city);
			InfoMetaData.AddStringProperty(TEXT("Street"), projectNoteInfo.street);
			InfoMetaData.AddStringProperty(TEXT("MainArchitect"), projectNoteInfo.architect);
			InfoMetaData.AddStringProperty(TEXT("Draftsperson"), projectNoteInfo.draftsmen);
			InfoMetaData.AddStringProperty(TEXT("ProjectStatus"), projectNoteInfo.projectStatus);
			InfoMetaData.AddStringProperty(TEXT("DateOfIssue"), projectNoteInfo.dateOfIssue);
			InfoMetaData.AddStringProperty(TEXT("Keywords"), projectNoteInfo.keywords);
			InfoMetaData.AddStringProperty(TEXT("Notes"), projectNoteInfo.notes);
		}
	}

	// Place info
	{
		API_PlaceInfo placeInfo;
		err = ACAPI_Environment(APIEnv_GetPlaceSetsID, &placeInfo);
		if (err == NoError)
		{
			InfoMetaData.AddStringProperty(TEXT("Longitude"), GS::ValueToUniString(placeInfo.longitude));
			InfoMetaData.AddStringProperty(TEXT("Latitude"), GS::ValueToUniString(placeInfo.latitude));
			InfoMetaData.AddStringProperty(TEXT("Altitude"), GS::ValueToUniString(placeInfo.altitude));
			InfoMetaData.AddStringProperty(TEXT("North"), GS::ValueToUniString(placeInfo.north));
			InfoMetaData.AddStringProperty(TEXT("SunAngleXY"), GS::ValueToUniString(placeInfo.sunAngXY));
			InfoMetaData.AddStringProperty(TEXT("SunAngleZ"), GS::ValueToUniString(placeInfo.sunAngZ));
			InfoMetaData.AddStringProperty(TEXT("TimeZoneInMinutes"),
										   GS::ValueToUniString(placeInfo.timeZoneInMinutes));
			InfoMetaData.AddStringProperty(TEXT("TimeZoneOffset"), GS::ValueToUniString(placeInfo.timeZoneOffset));

			GSTime		 gstime;
			GSTimeRecord timeRecord(placeInfo.year, placeInfo.month, 0, placeInfo.day, placeInfo.hour, placeInfo.minute,
									placeInfo.second, 0);
			TIGetGSTime(&timeRecord, &gstime, TI_LOCAL_TIME);
			InfoMetaData.AddStringProperty(TEXT("LocalDateTime"),
										   TIGetTimeString(gstime, TI_LONG_DATE_FORMAT | TI_SHORT_TIME_FORMAT));
		}
	}

	SceneInfoActorElement->SetLabel(GSStringToUE(GS::UniString(projectName + " Project Informations")));

	InfoMetaData.SetOrUpdate(&SceneInfoMetaData, &IOProcessInfo->SyncContext.GetScene());
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

// Delete this sync data
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

// Add tags data
void FSyncData::FActor::UpdateTags(const std::vector< FString >& InTags)
{
	int32 Count = (int32)InTags.size();
	int32 Index = 0;
	if (ActorElement->GetTagsCount() == Count)
	{
		while (Index < Count && InTags[Index] == ActorElement->GetTag(Index))
		{
			++Index;
		}
		if (Index == Count)
		{
			return; // All Tags unchanged
		}
	}

	ActorElement->ResetTags();
	for (Index = 0; Index < Count; ++Index)
	{
		ActorElement->AddTag(*InTags[Index]);
	}
}

// Add tags data
void FSyncData::FActor::AddTags(const FElementID& InElementID)
{
	UE_AC_Assert(ActorElement.IsValid());

	std::vector< FString > Tags;

	static GS::UniString PrefixTagUniqueID("Archicad.Element.UniqueID.");
	GS::UniString		 TagUniqueID = PrefixTagUniqueID + ElementId.ToUniString();
	Tags.push_back(GSStringToUE(TagUniqueID));

	static GS::UniString PrefixTagType("Archicad.Element.Type.");
	GS::UniString		 TagElementType = PrefixTagType + FElementTools::TypeName(InElementID.ElementHeader.typeID);
	Tags.push_back(GSStringToUE(TagElementType));

	GS::Array< GS::Pair< API_ClassificationSystem, API_ClassificationItem > > ApiClassifications;
	GSErrCode GSErr = FElementTools::GetElementClassifications(ApiClassifications, InElementID.ElementHeader.guid);

	if (GSErr == NoError)
	{
		std::set< GS::UniString > ClassificationIds;
		static GS::UniString	  PrefixTagClassificationID("Archicad.Classification.ID.");
		for (const GS::Pair< API_ClassificationSystem, API_ClassificationItem >& Classification : ApiClassifications)
		{
			GS::UniString TagClassificationID = PrefixTagClassificationID + Classification.second.id;
			if (ClassificationIds.insert(TagClassificationID).second == true)
			{
				Tags.push_back(GSStringToUE(TagClassificationID));
			}
		}
	}
	else
	{
		UE_AC_DebugF("FSyncData::AddTags - FElementTools::GetElementClassifications returned error %d", GSErr);
	}

	UpdateTags(Tags);
}

// Replace the current meta data by this new one
void FSyncData::FActor::ReplaceMetaData(IDatasmithScene&							   IOScene,
										const TSharedPtr< IDatasmithMetaDataElement >& InNewMetaData)
{
	// Disconnect previous meta data
	if (MetaData.IsValid())
	{
		IOScene.RemoveMetaData(MetaData);
		MetaData.Reset();
	}

	MetaData = InNewMetaData;

	// Connect previous meta data
	if (MetaData.IsValid())
	{
		MetaData->SetAssociatedElement(ActorElement);
	}
	IOScene.AddMetaData(MetaData);
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
			UE_AC_DebugF("CElementsHierarchy::CreateLayerNode - Error %s for layer index=%d\n", GetErrorName(error),
						 LayerIndex);
			if (error == APIERR_DELETED)
			{
				LayerName = GetGSName(kName_LayerDeleted);
			}
			else
			{
				LayerName = GS::UniString::Printf(GetGSName(kName_LayerError), GetErrorName(error));
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

#pragma mark -

inline Geometry::Transformation3D Convert(const ModelerAPI::Transformation& InMatrix)
{
	Geometry::Matrix33 M33;

	M33.Set(0, 0, InMatrix.matrix[0][0]);
	M33.Set(0, 1, InMatrix.matrix[0][1]);
	M33.Set(0, 2, InMatrix.matrix[0][2]);
	M33.Set(1, 0, InMatrix.matrix[1][0]);
	M33.Set(1, 1, InMatrix.matrix[1][1]);
	M33.Set(1, 2, InMatrix.matrix[1][2]);
	M33.Set(2, 0, InMatrix.matrix[2][0]);
	M33.Set(2, 1, InMatrix.matrix[2][1]);
	M33.Set(2, 2, InMatrix.matrix[2][2]);

	Geometry::Transformation3D Converted;

	Converted.SetMatrix(M33);
	Converted.SetOffset(Vector3D(InMatrix.matrix[0][3], InMatrix.matrix[1][3], InMatrix.matrix[2][3]));

	return Converted;
}

class FConvertGeometry2MeshElement : public GS::Runnable
{
  public:
	FConvertGeometry2MeshElement(const FSyncContext& InSyncContext, FSyncData::FElement* InElementSyncData);

	void AddElementGeometry(FElementID* IOElementID, const ModelerAPI::Transformation& InLocalToWorld);

	bool HasGeometry() const { return Element2StaticMesh.HasGeometry(); }

	void CreateDatasmithMesh()
	{
		if (HasGeometry())
		{
			Run();
		}
		delete this;
	}

	void Run()
	{
		/*
		 #ifdef WIN32
		 SetThreadName(GS::Thread::GetCurrent().GetName().ToUtf8());
		 #else
		 pthread_setname_np(GS::Thread::GetCurrent().GetName().ToUtf8());
		 #endif
		 */
		try
		{
			TSharedPtr< IDatasmithMeshElement > Mesh = Element2StaticMesh.CreateMesh();
			if (SyncContext.GetSyncDatabase().SetMesh(&ElementSyncData.GetMeshElementRef(), Mesh))
			{
				ElementSyncData.MeshElementChanged();
			}
		}
		catch (std::exception& e)
		{
			UE_AC_DebugF("FConvertGeometry2MeshElement::Run - Catch std exception %s\n", e.what());
		}
		catch (GS::GSException& gs)
		{
			UE_AC_DebugF("FConvertGeometry2MeshElement::Run - Catch gs exception %s\n", gs.GetMessage().ToUtf8());
		}
		catch (...)
		{
			UE_AC_DebugF("FConvertGeometry2MeshElement::Run - Catch unknown exception\n");
		}
	}

  private:
	const FSyncContext&	 SyncContext;
	FElement2StaticMesh	 Element2StaticMesh;
	FSyncData::FElement& ElementSyncData;
};

FConvertGeometry2MeshElement::FConvertGeometry2MeshElement(const FSyncContext&	InSyncContext,
														   FSyncData::FElement* InElementSyncData)
	: SyncContext(InSyncContext)
	, Element2StaticMesh(InSyncContext)
	, ElementSyncData(*InElementSyncData)
{
	UE_AC_TestPtr(InElementSyncData);
}

void FConvertGeometry2MeshElement::AddElementGeometry(FElementID*						IOElementID,
													  const ModelerAPI::Transformation& InLocalToWorld)
{
	UE_AC_TestPtr(IOElementID);

	Geometry::Transformation3D Local2World = Convert(InLocalToWorld);
#if AC_VERSION < 24
	Geometry::Transformation3D World2Local = Local2World.GetInverse();
#else
	Geometry::Transformation3D World2Local = Local2World.GetInverse().Get(Geometry::Transformation3D());
#endif

	Element2StaticMesh.AddElementGeometry(IOElementID->Element3D, World2Local);
}

FSyncData::FElement::FElement(const GS::Guid& InGuid, const FSyncContext& InSyncContext)
	: FSyncData::FActor(InGuid)
{
}

void FSyncData::FElement::MeshElementChanged()
{
	if (MeshElement.IsValid())
	{
		UE_AC_Assert(ActorElement.IsValid() && ActorElement->IsA(EDatasmithElementType::StaticMeshActor));

		IDatasmithMeshActorElement& MeshActor = *StaticCastSharedPtr< IDatasmithMeshActorElement >(ActorElement).Get();
		MeshActor.SetStaticMeshPathName(MeshElement->GetName());
		MeshElement->SetLabel(ActorElement->GetLabel());
	}
}

// Return true if this element and all it's childs have been cut out
bool FSyncData::FElement::CheckAllCutOut()
{
	if (Index3D != 0)
	{
		return false;
	}
	for (size_t IterChild = 0; IterChild < Childs.size(); ++IterChild)
	{
		if (!Childs[IterChild]->CheckAllCutOut())
		{
			return false;
		}
	}
	return true;
}

void FSyncData::FElement::Process(FProcessInfo* IOProcessInfo)
{
	if (Index3D == 0) // No 3D imply an hierarchical parent or recently cut out element
	{
		if (ActorElement.IsValid())
		{
			if (ActorElement->IsA(EDatasmithElementType::StaticMeshActor)) // Previously was a mesh, now presume cut out
			{
				// Element is a child cut out and it's parent hasn't been completely cut-out.
				SetActorElement(TSharedPtr< IDatasmithActorElement >());
				if (!CheckAllCutOut())
				{
					UE_AC_DebugF("FSyncData::FElement::Process - Element cut out with uncut child %s\n",
								 ElementId.ToUniString().ToUtf8());
				}
			}
		}
		else // Hierarchical parent
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
			NewActor->SetIsAComponent(bIsAComponent);

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

			ModelerAPI::Transformation LocalToWorld =
				IOProcessInfo->ElementID.Element3D.GetElemLocalToWorldTransformation();
			if ((LocalToWorld.status & TR_IDENT) != 0)
			{
				Box3D Bounds = IOProcessInfo->ElementID.Element3D.GetBounds();
				LocalToWorld.matrix[0][3] = (Bounds.xMin + Bounds.xMax) * 0.5;
				LocalToWorld.matrix[1][3] = (Bounds.yMin + Bounds.yMax) * 0.5;
				LocalToWorld.matrix[2][3] = Bounds.zMin;
				LocalToWorld.status = (LocalToWorld.matrix[0][3] == 0.0 && LocalToWorld.matrix[1][3] == 0.0 &&
									   LocalToWorld.matrix[2][3] == 0.0)
										  ? TR_IDENT
										  : TR_TRANSL_ONLY;
			}

			TSharedPtr< IDatasmithActorElement > OldActor = ActorElement;
			FConvertGeometry2MeshElement*		 ConvertGeometry2MeshElement =
				new FConvertGeometry2MeshElement(IOProcessInfo->SyncContext, this);
			ConvertGeometry2MeshElement->AddElementGeometry(&IOProcessInfo->ElementID, LocalToWorld);
			bool bHasGeometry = ConvertGeometry2MeshElement->HasGeometry();
			if (ActorElement.IsValid())
			{
				if (ActorElement->IsA(EDatasmithElementType::StaticMeshActor))
				{
					if (!bHasGeometry)
					{
						// Change actor from mesh actor to a non mesh actor
						SetActorElement(TSharedPtr< IDatasmithActorElement >());
					}
				}
				else
				{
					if (bHasGeometry)
					{
						// Change actor from non mesh actor to mesh actor
						SetActorElement(TSharedPtr< IDatasmithActorElement >());
					}
				}
			}
			if (!ActorElement.IsValid())
			{
				if (bHasGeometry)
				{
					UE_AC_STAT(IOProcessInfo->SyncContext.Stats.TotalActorsCreated++);
					SetActorElement(FDatasmithSceneFactory::CreateMeshActor(GSStringToUE(ElementId.ToUniString())));
				}
				else
				{
					UE_AC_STAT(IOProcessInfo->SyncContext.Stats.TotalEmptyActorsCreated++);
					SetActorElement(FDatasmithSceneFactory::CreateActor(GSStringToUE(ElementId.ToUniString())));
				}
			}
			ActorElement->SetIsAComponent(bIsAComponent);

			// Need to copy from old actor to new one ?
			if (OldActor.IsValid() && OldActor != ActorElement)
			{
				// Copy childs from old actor to new one
				int32 ChildrenCount = OldActor->GetChildrenCount();
				for (int32 ChildIndex = 0; ChildIndex < ChildrenCount; ++ChildIndex)
				{
					ActorElement->AddChild(OldActor->GetChild(ChildIndex));
				}
			}

			// Set actor label
			GS::UniString ElemenInfo;
			if (FElementTools::GetInfoString(IOProcessInfo->ElementID.ElementHeader.guid, &ElemenInfo))
			{
				ActorElement->SetLabel(GSStringToUE(ElemenInfo));
			}
			else
			{
				ActorElement->SetLabel(TEXT("Unnamed"));
			}

			ActorElement->SetTranslation(FGeometryUtil::GetTranslationVector(LocalToWorld.matrix));
			ActorElement->SetRotation(FGeometryUtil::GetRotationQuat(LocalToWorld.matrix));

			// Set actor layer
			ActorElement->SetLayer(*IOProcessInfo->SyncContext.GetSyncDatabase().GetLayerName(
				IOProcessInfo->ElementID.ElementHeader.layer));

			AddTags(IOProcessInfo->ElementID);

			UpdateMetaData(IOProcessInfo->SyncContext.GetScene());

			ConvertGeometry2MeshElement->CreateDatasmithMesh();
		}
	}

	// We attach observer only when we will need it
	if (bIsObserved == false && IOProcessInfo->SyncContext.IsSynchronizer() && FCommander::IsLiveLinkEnabled())
	{
		bIsObserved = true;
		GSErrCode GSErr = ACAPI_Element_AttachObserver(GSGuid2APIGuid(ElementId), APINotifyElement_EndEvents);
		if (GSErr != NoError && GSErr != APIERR_LINKEXIST)
		{
			UE_AC_DebugF("FSyncData::FElement::Process - ACAPI_Element_AttachObserver error=%s\n", GetErrorName(GSErr));
		}
	}
}

// Delete this sync data
void FSyncData::FElement::DeleteMe(FSyncDatabase* IOSyncDatabase)
{
	IOSyncDatabase->SetMesh(&MeshElement, TSharedPtr< IDatasmithMeshElement >());
	FSyncData::FActor::DeleteMe(IOSyncDatabase);
}

// Rebuild the meta data of this element
void FSyncData::FElement::UpdateMetaData(IDatasmithScene& IOScene)
{
	FMetaData MetaDataExporter(ActorElement);
	MetaDataExporter.ExportMetaData(ElementId);
	MetaDataExporter.SetOrUpdate(&MetaData, &IOScene);
}

#pragma mark -

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

#pragma mark -

// Guid given to the current view.
const GS::Guid FSyncData::FCamera::CurrentViewGUID("B2BD9C50-60EB-4E64-902B-D1574FADEC45");

void FSyncData::FCamera::Process(FProcessInfo* /* IOProcessInfo */)
{
	if (!ActorElement.IsValid())
	{
		SetActorElement(FDatasmithSceneFactory::CreateCameraActor(GSStringToUE(ElementId.ToUniString())));
		MarkAsModified();
	}

	if (ElementId == CurrentViewGUID)
	{
		InitWithCurrentView();
	}
	else
	{
		if (IsModified())
		{
			InitWithCameraElement();
		}
	}
}

void FSyncData::FCamera::InitWithCurrentView()
{
	FAutoChangeDatabase changeDB(APIWind_3DModelID);

	IDatasmithCameraActorElement& CameraElement = static_cast< IDatasmithCameraActorElement& >(*ActorElement.Get());
	CameraElement.SetLabel(TEXT("Current view"));

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

#pragma mark -

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
					float InnerConeAngleClamped = FGeometryUtil::Clamp(InnerConeAngle, 1.0f, 89.0f - 0.001f);
					SpotLight->SetInnerConeAngle(InnerConeAngleClamped);
					float OuterConeAngleClamped =
						FGeometryUtil::Clamp(OuterConeAngle, InnerConeAngleClamped + 0.001f, 89.0f);
					SpotLight->SetOuterConeAngle(OuterConeAngleClamped);
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

		const TCHAR* ParentLabel = TEXT("Unamed object");
		UE_AC_TestPtr(Parent);
		if (Parent->GetElement().IsValid())
		{
			ParentLabel = Parent->GetElement()->GetLabel();
		}
		LightElement.SetLabel(*FString::Printf(TEXT("%s - Light %d"), ParentLabel, Index));
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

#pragma mark -

const GS::Guid FSyncData::FHotLinksRoot::HotLinksRootGUID("C4BFD876-FDE9-4CCF-8899-12023968DC0D");

void FSyncData::FHotLinksRoot::Process(FProcessInfo* /* IOProcessInfo */)
{
	if (!ActorElement.IsValid())
	{
		SetActorElement(FDatasmithSceneFactory::CreateActor(GSStringToUE(ElementId.ToUniString())));
		ActorElement->SetLabel(TEXT("Hot Links"));
	}
}

void FSyncData::FHotLinkNode::Process(FProcessInfo* IOProcessInfo)
{
	if (!ActorElement.IsValid())
	{
		SetActorElement(FDatasmithSceneFactory::CreateActor(GSStringToUE(ElementId.ToUniString())));

		API_HotlinkNode hotlinkNode;
		Zap(&hotlinkNode);
		hotlinkNode.guid = GSGuid2APIGuid(ElementId);
		GSErrCode err = ACAPI_Database(APIDb_GetHotlinkNodeID, &hotlinkNode);
		if (err == NoError)
		{
			GS::UniString Label = hotlinkNode.name;
			if (hotlinkNode.refFloorName[0] != '\0')
			{
				Label += " Floor ";
				Label += hotlinkNode.refFloorName;
			}
			ActorElement->SetLabel(GSStringToUE(Label));

			FMetaData MyMetaData(ActorElement);

			const TCHAR* HotLinkType = TEXT("Unknown");
			if (hotlinkNode.type == APIHotlink_Module)
			{
				HotLinkType = TEXT("Module");
			}
			else if (hotlinkNode.type == APIHotlink_XRef)
			{
				HotLinkType = TEXT("XRef");
			}
			MyMetaData.AddStringProperty(TEXT("HotLinkType"), HotLinkType);

			if (hotlinkNode.sourceLocation != nullptr)
			{
				MyMetaData.AddStringProperty(TEXT("HotLinkLocation"), hotlinkNode.sourceLocation->ToDisplayText());
			}
			if (hotlinkNode.serverSourceLocation != nullptr)
			{
				MyMetaData.AddStringProperty(TEXT("HotLinkSharedLocation"),
											 hotlinkNode.serverSourceLocation->ToDisplayText());
			}
			MyMetaData.AddStringProperty(TEXT("StoryRangeType"), hotlinkNode.storyRangeType == APIHotlink_SingleStory
																	 ? TEXT("Single")
																	 : TEXT("All"));

			const TCHAR* SourceLinkType = TEXT("Unknown");
			if (hotlinkNode.sourceType == APIHotlink_LocalFile)
			{
				SourceLinkType = TEXT("LocalFile");
			}
			else if (hotlinkNode.sourceType == APIHotlink_TWFS)
			{
				SourceLinkType = TEXT("TWFS");
			}
			else if (hotlinkNode.sourceType == APIHotlink_TWProject)
			{
				SourceLinkType = TEXT("TWProject");
			}
			MyMetaData.AddStringProperty(TEXT("StorySourceType"), SourceLinkType);

			ReplaceMetaData(IOProcessInfo->SyncContext.GetScene(), MyMetaData.GetMetaData());

			delete hotlinkNode.sourceLocation;
			delete hotlinkNode.serverSourceLocation;
			BMKillPtr(&hotlinkNode.userData.data);
		}
		else
		{
			UE_AC_DebugF("FSyncData::FHotLinkInstance::Process - ACAPI_Element_Get - Error=%d\n", err);
		}
	}
}

FSyncData::FHotLinkInstance::FHotLinkInstance(const GS::Guid& InGuid, FSyncDatabase* IOSyncDatabase)
	: FSyncData::FActor(InGuid)
{
	Transformation = {};
	Transformation.tmx[0] = 1.0;
	Transformation.tmx[5] = 1.0;
	Transformation.tmx[10] = 1.0;

	API_Element hotlinkElem;
	Zap(&hotlinkElem);
	hotlinkElem.header.typeID = API_HotlinkID;
	hotlinkElem.header.guid = GSGuid2APIGuid(ElementId);
	GSErrCode err = ACAPI_Element_Get(&hotlinkElem);
	if (err == NoError)
	{
		// Parent is a hot link node
		FSyncData*& HotLinkNode = IOSyncDatabase->GetSyncData(APIGuid2GSGuid(hotlinkElem.hotlink.hotlinkNodeGuid));
		if (HotLinkNode == nullptr)
		{
			HotLinkNode = new FSyncData::FHotLinkNode(APIGuid2GSGuid(hotlinkElem.hotlink.hotlinkNodeGuid));
			if (!HotLinkNode->HasParent())
			{
				FSyncData*& HotLinksRoot = IOSyncDatabase->GetSyncData(FSyncData::FHotLinksRoot::HotLinksRootGUID);
				if (HotLinksRoot == nullptr)
				{
					HotLinksRoot = new FSyncData::FHotLinksRoot();
					HotLinksRoot->SetParent(&IOSyncDatabase->GetSceneSyncData());
				}
				HotLinkNode->SetParent(HotLinksRoot);
			}
		}
		SetParent(HotLinkNode);
	}
	else
	{
		UE_AC_DebugF("FSyncData::FHotLinkInstance::FHotLinkInstance - ACAPI_Element_Get - Error=%d\n", err);
	}
}

void FSyncData::FHotLinkInstance::Process(FProcessInfo* IOProcessInfo)
{
	if (!ActorElement.IsValid())
	{
		SetActorElement(FDatasmithSceneFactory::CreateActor(GSStringToUE(ElementId.ToUniString())));

		API_Element hotlinkElem;
		Zap(&hotlinkElem);
		hotlinkElem.header.typeID = API_HotlinkID;
		hotlinkElem.header.guid = GSGuid2APIGuid(ElementId);
		GSErrCode err = ACAPI_Element_Get(&hotlinkElem);
		if (err == NoError)
		{
			const TCHAR* HotLinkType = TEXT("Unknown");
			if (hotlinkElem.hotlink.type == APIHotlink_Module)
			{
				HotLinkType = TEXT("Module");
			}
			else if (hotlinkElem.hotlink.type == APIHotlink_XRef)
			{
				HotLinkType = TEXT("XRef");
			}
			const TCHAR* ParentLabel = TEXT("Unamed object");
			UE_AC_Assert(Parent != nullptr && Parent->ElementId == hotlinkElem.hotlink.hotlinkNodeGuid);
			if (Parent->GetElement().IsValid())
			{
				ParentLabel = Parent->GetElement()->GetLabel();
			}
			ActorElement->SetLabel(
				*FString::Printf(TEXT("%s - %s Instance %llu"), ParentLabel, HotLinkType, IOProcessInfo->Index));

			Transformation = hotlinkElem.hotlink.transformation;

			FMetaData MyMetaData(ActorElement);

			MyMetaData.AddStringProperty(TEXT("HotLinkType"), HotLinkType);

			ReplaceMetaData(IOProcessInfo->SyncContext.GetScene(), MyMetaData.GetMetaData());

			// What do we do with hotlinkElem.hotlink.hotlinkGroupGuid ?
		}
		else
		{
			UE_AC_DebugF("FSyncData::FHotLinkInstance::Process - ACAPI_Element_Get - Error=%d\n", err);
		}
	}
}

END_NAMESPACE_UE_AC
