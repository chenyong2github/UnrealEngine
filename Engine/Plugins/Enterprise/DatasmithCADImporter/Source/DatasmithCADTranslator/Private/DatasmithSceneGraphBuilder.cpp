// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "DatasmithSceneGraphBuilder.h"

#ifdef CAD_INTERFACE

#include "CADData.h"
#include "CADSceneGraph.h"
#include "CoreTechFileParser.h"
#include "CoreTechHelper.h"
#include "DatasmithSceneFactory.h"
#include "DatasmithSceneSource.h"
#include "IDatasmithSceneElements.h"
#include "Utility/DatasmithMathUtils.h"

#include "Misc/FileHelper.h"


namespace
{
	void GetMainMaterial(const TMap<FString, FString>& InNodeMetaDataMap, ActorData& OutNodeData, bool bMaterialPropagationIsTopDown)
	{
		if (const FString* MaterialNameStr = InNodeMetaDataMap.Find(TEXT("MaterialName")))
		{
			if (!bMaterialPropagationIsTopDown || !OutNodeData.MaterialUuid)
			{
				OutNodeData.MaterialUuid = FCString::Atoi64(**MaterialNameStr);
			}
		}

		if (const FString* ColorHashStr = InNodeMetaDataMap.Find(TEXT("ColorName")))
		{
			if (bMaterialPropagationIsTopDown || !OutNodeData.ColorUuid)
			{
				OutNodeData.ColorUuid = FCString::Atoi64(**ColorHashStr);
			}
		}
	}

	void AddTransformToActor(CADLibrary::FArchiveInstance& Instance, TSharedPtr< IDatasmithActorElement > Actor, const CADLibrary::FImportParameters& ImportParameters)
	{
		if (!Actor.IsValid())
		{
			return;
		}

		FTransform LocalTransform(Instance.TransformMatrix);
		FTransform LocalUETransform = FDatasmithUtils::ConvertTransform((FDatasmithUtils::EModelCoordSystem) ImportParameters.ModelCoordSys, LocalTransform);

		Actor->SetTranslation(LocalUETransform.GetTranslation() * ImportParameters.ScaleFactor);
		Actor->SetScale(LocalUETransform.GetScale3D());
		Actor->SetRotation(LocalUETransform.GetRotation());
	}

	// Method to reduce the size of huge label. The length of the package path, based on label, cannot be bigger than ~256
	void CleanName(FString& Label)
	{
		const int32 MaxLabelSize = 50; // If the label is smaller than this value, the label is not modified. This size of package name is "acceptable"
		const int32 ReasonableLabelSize = 20; // If the label has to be cut, a label that is not too long is preferred. 
		const int32 MinLabelSize = 5; // If the label is smaller than this value, the label is too much reduce. Therefore a ReasonableLabelSize is preferred 

		if (Label.Len() < MaxLabelSize)
		{
			return;
		}

		FString NewLabel;
		NewLabel = FPaths::GetCleanFilename(Label);
		if ((NewLabel.Len() < MaxLabelSize) && (NewLabel.Len() > MinLabelSize))
		{
			Label = NewLabel;
			return;
		}

		Label = Label.Right(ReasonableLabelSize);
	}
}

FDatasmithSceneGraphBuilder::FDatasmithSceneGraphBuilder(TMap<FString, FString>& InCADFileToUE4FileMap, const FString& InCachePath, TSharedRef<IDatasmithScene> InScene, const FDatasmithSceneSource& InSource, const CADLibrary::FImportParameters& InImportParameters)
	: CADFileToSceneGraphDescriptionFile(InCADFileToUE4FileMap)
	, CachePath(InCachePath)
	, DatasmithScene(InScene)
	, Source(InSource)
	, ImportParameters(InImportParameters)
	, ImportParametersHash(ImportParameters.GetHash())
{
}

bool FDatasmithSceneGraphBuilder::Build()
{
	LoadSceneGraphDescriptionFiles();

	const FString& RootFile = FPaths::GetCleanFilename(Source.GetSourceFile());

	CurrentMockUp = CADFileToArchiveMockUp.FindRef(RootFile);
	if (!CurrentMockUp)
	{
		return false;
	}

	CadId RootId = 1;
	int32* Index = CurrentMockUp->CADIdToComponentIndex.Find(RootId);
	if (!Index)
	{
		return false;
	}

	ActorData Data(TEXT(""));
	TSharedPtr< IDatasmithActorElement > RootActor = BuildComponent(CurrentMockUp->ComponentSet[*Index], Data);
	DatasmithScene->AddActor(RootActor);

	return true;
}

void FDatasmithSceneGraphBuilder::LoadSceneGraphDescriptionFiles()
{
	ArchiveMockUps.Reserve(CADFileToSceneGraphDescriptionFile.Num());
	CADFileToArchiveMockUp.Reserve(CADFileToSceneGraphDescriptionFile.Num());

	for (const auto& FilePair : CADFileToSceneGraphDescriptionFile)
	{
		FString MockUpDescriptionFile = FPaths::Combine(CachePath, TEXT("scene"), FilePair.Value + TEXT(".sg"));

		CADLibrary::FArchiveMockUp& MockUpDescription = ArchiveMockUps.Emplace_GetRef();

		CADFileToArchiveMockUp.Add(FilePair.Key, &MockUpDescription);
	
		CADLibrary::DeserializeMockUpFile(*MockUpDescriptionFile, MockUpDescription);

		for(const auto& ColorPair : MockUpDescription.ColorHIdToColor)
		{
			ColorNameToColorArchive.Emplace(ColorPair.Value.UEMaterialName, ColorPair.Value);
		}

		for (const auto& MaterialPair : MockUpDescription.MaterialHIdToMaterial)
		{
			MaterialNameToMaterialArchive.Emplace(MaterialPair.Value.UEMaterialName, MaterialPair.Value);
		}

	}
}

TSharedPtr< IDatasmithActorElement >  FDatasmithSceneGraphBuilder::BuildInstance(CADLibrary::FArchiveInstance& Instance, ActorData& ParentData)
{
	CADLibrary::FArchiveComponent* Reference = nullptr;
	CADLibrary::FArchiveComponent EmptyReference;

	CADLibrary::FArchiveMockUp* InstanceMockUp = CurrentMockUp;
	if (Instance.bIsExternalRef)
	{
		if (!Instance.ExternalRef.IsEmpty())
		{
			CurrentMockUp = CADFileToArchiveMockUp.FindRef(Instance.ExternalRef);
			if (CurrentMockUp)
			{
				CadId RootId = 1;
				int32* Index = CurrentMockUp->CADIdToComponentIndex.Find(RootId);
				if (Index)
				{
					Reference = &(CurrentMockUp->ComponentSet[*Index]);
				}
			}
		}

		if(!Reference)
		{
			CurrentMockUp = InstanceMockUp;
			int32* Index = CurrentMockUp->CADIdToUnloadedComponentIndex.Find(Instance.ReferenceNodeId);
			if (Index)
			{
				Reference = &(CurrentMockUp->UnloadedComponentSet[*Index]);
			}
		}
	}
	else {
		int32* Index = CurrentMockUp->CADIdToComponentIndex.Find(Instance.ReferenceNodeId);
		if (Index)
		{
			Reference = &(CurrentMockUp->ComponentSet[*Index]);
		}
	}

	if (!Reference) // Should never append
	{
		Reference = &EmptyReference;
	}

	FString ActorUUID;
	FString ActorLabel;
	GetNodeUUIDAndName(Instance.MetaData, Reference->MetaData, ParentData.Uuid, ActorUUID, ActorLabel);

	TSharedPtr< IDatasmithActorElement > Actor = CreateActor(*ActorUUID, *ActorLabel);
	if (!Actor.IsValid())
	{
		return TSharedPtr< IDatasmithActorElement >();
	}
	AddMetaData(Actor, Instance.MetaData, Reference->MetaData);

	ActorData InstanceData(*ActorUUID, ParentData);

	GetMainMaterial(Instance.MetaData, InstanceData, bMaterialPropagationIsTopDown);
	GetMainMaterial(Reference->MetaData, InstanceData, bMaterialPropagationIsTopDown);

	AddChildren(Actor, *Reference, InstanceData);

	AddTransformToActor(Instance, Actor, ImportParameters);

	CurrentMockUp = InstanceMockUp;
	return Actor;
}

TSharedPtr< IDatasmithActorElement >  FDatasmithSceneGraphBuilder::CreateActor(const TCHAR* InEUUID, const TCHAR* InLabel)
{
	TSharedPtr< IDatasmithActorElement > Actor = FDatasmithSceneFactory::CreateActor(InEUUID);
	if (Actor.IsValid())
	{
		Actor->SetLabel(InLabel);
		return Actor;
	}
	return TSharedPtr< IDatasmithActorElement >();
}

void FDatasmithSceneGraphBuilder::GetNodeUUIDAndName(
	TMap<FString, FString>& InInstanceNodeMetaDataMap,
	TMap<FString, FString>& InReferenceNodeMetaDataMap,
	const TCHAR* InParentUEUUID,
	FString& OutUEUUID,
	FString& OutName
)
{
	FString* IName = InInstanceNodeMetaDataMap.Find(TEXT("CTName"));
	FString* IOriginalName = InInstanceNodeMetaDataMap.Find(TEXT("Name"));
	FString* IUUID = InInstanceNodeMetaDataMap.Find(TEXT("UUID"));

	FString* RName = InReferenceNodeMetaDataMap.Find(TEXT("CTName"));
	FString* ROriginalName = InReferenceNodeMetaDataMap.Find(TEXT("Name"));
	FString* RUUID = InReferenceNodeMetaDataMap.Find(TEXT("UUID"));

	FString ReferenceName;

	// Reference Name
	if (ROriginalName)
	{
		ReferenceName = *ROriginalName;
	}
	else if (RName)
	{
		ReferenceName = *RName;
	}
	else
	{
		ReferenceName = "NoName";
	}

	//ReferenceInstanceName = ReferenceName + TEXT("(") + (IOriginalName ? *IOriginalName : ReferenceName) + TEXT(")");
	OutName = IOriginalName ? *IOriginalName : IName ? *IName : ReferenceName;
	CleanName(OutName);

	uint32 UEUUID = 0;
	UEUUID = GetTypeHash(InParentUEUUID);

	if (IUUID)
	{
		UEUUID = HashCombine(UEUUID, GetTypeHash(*IUUID));
	}
	if (IOriginalName)
	{
		UEUUID = HashCombine(UEUUID, GetTypeHash(*IOriginalName));
	}
	if (IName)
	{
		UEUUID = HashCombine(UEUUID, GetTypeHash(*IName));
	}

	if (RUUID)
	{
		UEUUID = HashCombine(UEUUID, GetTypeHash(*RUUID));
	}
	if (ROriginalName)
	{
		UEUUID = HashCombine(UEUUID, GetTypeHash(*ROriginalName));
	}
	if (RName)
	{
		UEUUID = HashCombine(UEUUID, GetTypeHash(*RName));
	}

	OutUEUUID = FString::Printf(TEXT("0x%08x"), UEUUID);
}

TSharedPtr< IDatasmithActorElement > FDatasmithSceneGraphBuilder::BuildComponent(CADLibrary::FArchiveComponent& Component, ActorData& ParentData)
{
	TMap<FString, FString> InstanceNodeMetaDataMap;

	FString ActorUUID = TEXT("");
	FString ActorLabel = TEXT("");
	
	GetNodeUUIDAndName(InstanceNodeMetaDataMap, Component.MetaData, ParentData.Uuid, ActorUUID, ActorLabel);

	TSharedPtr< IDatasmithActorElement > Actor = CreateActor(*ActorUUID, *ActorLabel);
	if (!Actor.IsValid())
	{
		return TSharedPtr< IDatasmithActorElement >();
	}

	AddMetaData(Actor, InstanceNodeMetaDataMap, Component.MetaData);

	ActorData ComponentData(*ActorUUID, ParentData);
	GetMainMaterial(Component.MetaData, ComponentData, bMaterialPropagationIsTopDown);

	AddChildren(Actor, Component, ComponentData);

	return Actor;
}

TSharedPtr< IDatasmithActorElement > FDatasmithSceneGraphBuilder::BuildBody(CADLibrary::FArchiveBody& Body, ActorData& ParentData)
{
	TMap<FString, FString> InstanceNodeMetaDataMap;
	
	FString BodyUUID;
	FString BodyLabel;
	GetNodeUUIDAndName(InstanceNodeMetaDataMap, Body.MetaData, ParentData.Uuid, BodyUUID, BodyLabel);

	// Apply materials on the current part
	uint32 MaterialUuid = 0;
	MaterialUuid = ParentData.MaterialUuid ? ParentData.MaterialUuid : ParentData.ColorUuid;

	if (!Body.ColorFaceSet.Num())
	{
		Body.ColorFaceSet.Add(MaterialUuid);
	}

	TSharedPtr< IDatasmithMeshElement > MeshElement = FindOrAddMeshElement(Body, BodyLabel);
	if (!MeshElement.IsValid())
	{
		return TSharedPtr< IDatasmithActorElement >();
	}


	TSharedPtr< IDatasmithMeshActorElement > ActorElement = FDatasmithSceneFactory::CreateMeshActor(*BodyUUID);
	if (!ActorElement.IsValid())
	{
		return TSharedPtr< IDatasmithActorElement >();
	}

	ActorElement->SetLabel(*BodyLabel);
	ActorElement->SetStaticMeshPathName(MeshElement->GetName());

	if (MaterialUuid && ImportParameters.Propagation != CADLibrary::EDisplayDataPropagationMode::BodyOnly)
	{
		TSharedPtr< IDatasmithMaterialIDElement > PartMaterialIDElement = FindOrAddMaterial(MaterialUuid);
		const TCHAR* MaterialIDElementName = PartMaterialIDElement->GetName();

		for (int32 Index = 0; Index < MeshElement->GetMaterialSlotCount(); ++Index)
		{
			TSharedPtr< IDatasmithMaterialIDElement > MaterialIDElement = FDatasmithSceneFactory::CreateMaterialId(MaterialIDElementName);
			MaterialIDElement->SetId(MeshElement->GetMaterialSlotAt(Index)->GetId());
			ActorElement->AddMaterialOverride(MaterialIDElement);
		}
	}
	return ActorElement;
}

TSharedPtr< IDatasmithMeshElement > FDatasmithSceneGraphBuilder::FindOrAddMeshElement(CADLibrary::FArchiveBody& Body, FString& BodyName)
{
	FString ShellUuidName = FString::Printf(TEXT("0x%012u"), Body.MeshActorName);

	// Look if geometry has not been already processed, return it if found
	TSharedPtr< IDatasmithMeshElement >* MeshElementPtr = BodyUuidToMeshElement.Find(Body.MeshActorName);
	if (MeshElementPtr != nullptr)
	{
		return *MeshElementPtr;
	}

	TSharedPtr< IDatasmithMeshElement > MeshElement = FDatasmithSceneFactory::CreateMesh(*ShellUuidName);
	MeshElement->SetLabel(*BodyName);
	MeshElement->SetLightmapSourceUV(-1);

	// Set MeshElement FileHash used for re-import task 
	FMD5 MD5; // unique Value that define the mesh
	MD5.Update(reinterpret_cast<const uint8*>(&ImportParametersHash), sizeof ImportParametersHash);
	// the scene graph archive name that is define by the name and the stat of the file (creation date, size)
	MD5.Update(reinterpret_cast<const uint8*>(CurrentMockUp->SceneGraphArchive.GetCharArray().GetData()), CurrentMockUp->SceneGraphArchive.GetCharArray().Num());
	// MeshActorName
	MD5.Update(reinterpret_cast<const uint8*>(&Body.MeshActorName), sizeof Body.MeshActorName);

	FMD5Hash Hash;
	Hash.Set(MD5);
	MeshElement->SetFileHash(Hash);

	FString BodyFile = FString::Printf(TEXT("UEx%08x"), Body.MeshActorName);
	MeshElement->SetFile(*FPaths::Combine(CachePath, TEXT("body"), BodyFile + TEXT(".ct")));

	// TODO: Set bounding box 
	//float BoundingBox[6];
	//FString Buffer = GetStringAttribute(GeomID, TEXT("UE_MESH_BBOX"));
	//if (FString::ToHexBlob(Buffer, (uint8*)BoundingBox, sizeof(BoundingBox)))
	//{
	//	MeshElement->SetDimensions(BoundingBox[3] - BoundingBox[0], BoundingBox[4] - BoundingBox[1], BoundingBox[5] - BoundingBox[2], 0.0f);
	//}

	// Currently we assume that face has only colors
	TSet<uint32>& MaterialSet = Body.ColorFaceSet;
	for (uint32 MaterialName : MaterialSet)
	{
		TSharedPtr< IDatasmithMaterialIDElement > PartMaterialIDElement;
		PartMaterialIDElement = FindOrAddMaterial(MaterialName);

		const TCHAR* MaterialIDElementName = PartMaterialIDElement->GetName();

		TSharedPtr< IDatasmithMaterialIDElement > MaterialIDElement = FDatasmithSceneFactory::CreateMaterialId(MaterialIDElementName);

		MeshElement->SetMaterial(MaterialIDElementName, MaterialName);
	}

	DatasmithScene->AddMesh(MeshElement);

	BodyUuidToMeshElement.Add(Body.MeshActorName, MeshElement);

	return MeshElement;
}

TSharedPtr< IDatasmithUEPbrMaterialElement > FDatasmithSceneGraphBuilder::GetDefaultMaterial()
{
	if (!DefaultMaterial.IsValid())
	{
		DefaultMaterial = CADLibrary::CreateDefaultUEPbrMaterial();
		DatasmithScene->AddMaterial(DefaultMaterial);
	}

	return DefaultMaterial;
}

TSharedPtr<IDatasmithMaterialIDElement> FDatasmithSceneGraphBuilder::FindOrAddMaterial(uint32 MaterialUuid)
{
	TSharedPtr< IDatasmithUEPbrMaterialElement > MaterialElement;

	TSharedPtr< IDatasmithUEPbrMaterialElement >* MaterialPtr = MaterialUuidMap.Find(MaterialUuid);
	if (MaterialPtr != nullptr)
	{
		MaterialElement = *MaterialPtr;
	}
	else if (MaterialUuid > 0)
	{
		if (CADLibrary::FArchiveColor* Color = ColorNameToColorArchive.Find(MaterialUuid))
		{
			MaterialElement = CADLibrary::CreateUEPbrMaterialFromColor(Color->Color);
		}
		else if (CADLibrary::FArchiveMaterial* Material = MaterialNameToMaterialArchive.Find(MaterialUuid))
		{
			MaterialElement = CADLibrary::CreateUEPbrMaterialFromMaterial(Material->Material, DatasmithScene);
		}

		if (MaterialElement.IsValid())
		{
			DatasmithScene->AddMaterial(MaterialElement);
			MaterialUuidMap.Add(MaterialUuid, MaterialElement);
		}
	}

	if (!MaterialElement.IsValid())
	{
		MaterialElement = GetDefaultMaterial();
		MaterialUuidMap.Add(MaterialUuid, MaterialElement);
	}

	TSharedPtr< IDatasmithMaterialIDElement > MaterialIDElement = FDatasmithSceneFactory::CreateMaterialId(MaterialElement->GetName());

	return MaterialIDElement;
}

void FDatasmithSceneGraphBuilder::AddMetaData(TSharedPtr< IDatasmithActorElement > ActorElement, TMap<FString, FString>& InstanceNodeAttributeSetMap, TMap<FString, FString>& ReferenceNodeAttributeSetMap)
{
	// Initialize list of attributes not to pass as meta-data
	auto GetUnwantedAttributes = []() -> TSet<FString>
	{
		TSet<FString> UnwantedAttributes;

		// CoreTech
		UnwantedAttributes.Add(TEXT("CTName"));
		UnwantedAttributes.Add(TEXT("LayerId"));
		UnwantedAttributes.Add(TEXT("LayerName"));
		UnwantedAttributes.Add(TEXT("LayerFlag"));
		UnwantedAttributes.Add(TEXT("OriginalUnitsMass"));
		UnwantedAttributes.Add(TEXT("OriginalUnitsLength"));
		UnwantedAttributes.Add(TEXT("OriginalUnitsDuration"));
		UnwantedAttributes.Add(TEXT("OriginalId"));
		UnwantedAttributes.Add(TEXT("OriginalIdStr"));
		UnwantedAttributes.Add(TEXT("ShowAttribute"));
		UnwantedAttributes.Add(TEXT("Identification"));
		UnwantedAttributes.Add(TEXT("MaterialId"));
		UnwantedAttributes.Add(TEXT("ColorUEId"));
		UnwantedAttributes.Add(TEXT("ColorId"));
		return UnwantedAttributes;
	};

	static const TSet<FString> UnwantedAttributes = GetUnwantedAttributes();

	TSharedRef< IDatasmithMetaDataElement > MetaDataRefElement = FDatasmithSceneFactory::CreateMetaData(ActorElement->GetName());
	MetaDataRefElement->SetAssociatedElement(ActorElement);

	for (auto& Attribute : ReferenceNodeAttributeSetMap)
	{
		if (UnwantedAttributes.Contains(Attribute.Key))
		{
			continue;
		}

		// If file information are attached to object, make sure to set a workable and full path
		if (Attribute.Key == TEXT("FileName"))
		{
			FString OFilePath = Attribute.Value;
			if (FPaths::FileExists(OFilePath))
			{
				OFilePath = *FPaths::ConvertRelativePathToFull(OFilePath);
			}
			else
			{
				FString FileDir = FPaths::GetPath(Source.GetSourceFile());
				FString FilePath = FPaths::Combine(FileDir, OFilePath);

				if (FPaths::FileExists(FilePath))
				{
					OFilePath = *FPaths::ConvertRelativePathToFull(OFilePath);
				}
				else // No workable file path to store. Skip
				{
					continue;
				}
			}

			// Beautifying the attributes name
			Attribute.Key = TEXT("FilePath");
			Attribute.Value = OFilePath;
		}

		FString MetaName = TEXT("Reference_");
		MetaName += Attribute.Key;
		TSharedRef< IDatasmithKeyValueProperty > KeyValueProperty = FDatasmithSceneFactory::CreateKeyValueProperty(*MetaName);

		KeyValueProperty->SetValue(*Attribute.Value);
		KeyValueProperty->SetPropertyType(EDatasmithKeyValuePropertyType::String);

		MetaDataRefElement->AddProperty(KeyValueProperty);
	}

	for (const auto& Attribute : InstanceNodeAttributeSetMap)
	{
		if (UnwantedAttributes.Contains(*Attribute.Key))
		{
			continue;
		}

		FString MetaName = TEXT("Instance_");
		MetaName += Attribute.Key;
		TSharedRef< IDatasmithKeyValueProperty > KeyValueProperty = FDatasmithSceneFactory::CreateKeyValueProperty(*MetaName);

		KeyValueProperty->SetValue(*Attribute.Value);
		KeyValueProperty->SetPropertyType(EDatasmithKeyValuePropertyType::String);

		MetaDataRefElement->AddProperty(KeyValueProperty);
	}

	DatasmithScene->AddMetaData(MetaDataRefElement);

}

bool FDatasmithSceneGraphBuilder::DoesActorHaveChildrenOrIsAStaticMesh(const TSharedPtr< IDatasmithActorElement >& ActorElement)
{
	if (ActorElement != nullptr)
	{
		if (ActorElement->GetChildrenCount() > 0)
		{
			return true;
		}
		else if (ActorElement->IsA(EDatasmithElementType::StaticMeshActor))
		{
			const TSharedPtr< IDatasmithMeshActorElement >& MeshActorElement = StaticCastSharedPtr< IDatasmithMeshActorElement >(ActorElement);
			return FCString::Strlen(MeshActorElement->GetStaticMeshPathName()) > 0;
		}
	}
	return false;
}


void FDatasmithSceneGraphBuilder::AddChildren(TSharedPtr< IDatasmithActorElement > Actor, CADLibrary::FArchiveComponent& Component, ActorData& ParentData)
{
	for (const int32 ChildId : Component.Children)
	{
		if (int32* ChildNodeIndex = CurrentMockUp->CADIdToInstanceIndex.Find(ChildId))
		{
			TSharedPtr< IDatasmithActorElement > ChildActor = BuildInstance(CurrentMockUp->Instances[*ChildNodeIndex], ParentData);
			if (ChildActor.IsValid() && DoesActorHaveChildrenOrIsAStaticMesh(ChildActor))
			{
				Actor->AddChild(ChildActor);
			}
		}
		if (int32* ChildNodeIndex = CurrentMockUp->CADIdToBodyIndex.Find(ChildId))
		{
			TSharedPtr< IDatasmithActorElement > ChildActor = BuildBody(CurrentMockUp->BodySet[*ChildNodeIndex], ParentData);
			if (ChildActor.IsValid() && DoesActorHaveChildrenOrIsAStaticMesh(ChildActor))
			{
				Actor->AddChild(ChildActor);
			}
		}
	}
}

#endif // CAD_INTERFACE
