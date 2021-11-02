// Copyright Epic Games, Inc. All Rights Reserved.
#include "DatasmithSceneGraphBuilder.h"

#include "CADData.h"
#include "CADSceneGraph.h"
#include "CoreTechFileParser.h"
#include "CoreTechHelper.h"
#include "DatasmithSceneFactory.h"
#include "DatasmithSceneSource.h"
#include "IDatasmithSceneElements.h"

#include "Misc/FileHelper.h"

namespace DatasmithSceneGraphBuilderImpl
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
		FTransform LocalUETransform = FDatasmithUtils::ConvertTransform(ImportParameters.ModelCoordSys, LocalTransform);

		Actor->SetTranslation(LocalUETransform.GetTranslation() * ImportParameters.ScaleFactor);
		Actor->SetScale(LocalUETransform.GetScale3D());
		Actor->SetRotation(LocalUETransform.GetRotation());
	}
}

FDatasmithSceneGraphBuilder::FDatasmithSceneGraphBuilder(
	TMap<uint32, FString>& InCADFileToUE4FileMap, 
	const FString& InCachePath, 
	TSharedRef<IDatasmithScene> InScene, 
	const FDatasmithSceneSource& InSource, 
	const CADLibrary::FImportParameters& InImportParameters)
		: FDatasmithSceneBaseGraphBuilder(nullptr, InScene, InSource, InImportParameters)
		, CADFileToSceneGraphDescriptionFile(InCADFileToUE4FileMap)
		, CachePath(InCachePath)
{
}

bool FDatasmithSceneGraphBuilder::Build()
{
	LoadSceneGraphDescriptionFiles();

	uint32 rootHash = GetTypeHash(rootFileDescription);
	SceneGraph = CADFileToSceneGraphArchive.FindRef(rootHash);
	if (!SceneGraph)
	{
		return false;
	}
	AncestorSceneGraphHash.Add(rootHash);

	return FDatasmithSceneBaseGraphBuilder::Build();
}

void FDatasmithSceneGraphBuilder::LoadSceneGraphDescriptionFiles()
{
	ArchiveMockUps.Reserve(CADFileToSceneGraphDescriptionFile.Num());
	CADFileToSceneGraphArchive.Reserve(CADFileToSceneGraphDescriptionFile.Num());

	for (const auto& FilePair : CADFileToSceneGraphDescriptionFile)
	{
		FString MockUpDescriptionFile = FPaths::Combine(CachePath, TEXT("scene"), FilePair.Value + TEXT(".sg"));

		CADLibrary::FArchiveSceneGraph& MockUpDescription = ArchiveMockUps.Emplace_GetRef();

		CADFileToSceneGraphArchive.Add(FilePair.Key, &MockUpDescription);

		MockUpDescription.DeserializeMockUpFile(*MockUpDescriptionFile);

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

void FDatasmithSceneGraphBuilder::FillAnchorActor(const TSharedRef<IDatasmithActorElement>& ActorElement, const FString& CleanFilenameOfCADFile)
{
	CADLibrary::FFileDescription AnchorDescription(*CleanFilenameOfCADFile);
	SceneGraph = CADFileToSceneGraphArchive.FindRef(GetTypeHash(AnchorDescription));
	if (!SceneGraph)
	{
		return;
	}

	CadId RootId = 1;
	int32* Index = SceneGraph->CADIdToComponentIndex.Find(RootId);
	if (!Index)
	{
		return;
	}

	ActorData Data(TEXT(""));

	// TODO: check ParentData and Index validity?
	ActorData ParentData(ActorElement->GetName());
	CADLibrary::FArchiveComponent& Component = SceneGraph->ComponentSet[*Index];

	TMap<FString, FString> InstanceNodeMetaDataMap;
	FString ActorUUID;
	FString ActorLabel;
	GetNodeUUIDAndName(InstanceNodeMetaDataMap, Component.MetaData, 1, ParentData.Uuid, ActorUUID, ActorLabel);

	AddMetaData(ActorElement, InstanceNodeMetaDataMap, Component.MetaData);

	ActorData ComponentData(*ActorUUID, ParentData);
	DatasmithSceneGraphBuilderImpl::GetMainMaterial(Component.MetaData, ComponentData, bMaterialPropagationIsTopDown);

	AddChildren(ActorElement, Component, ComponentData);

	ActorElement->SetLabel(*ActorLabel);
}

TSharedPtr<IDatasmithMeshElement> FDatasmithSceneGraphBuilder::FindOrAddMeshElement(CADLibrary::FArchiveBody & Body, FString & InLabel)
{
	if (TSharedPtr<IDatasmithMeshElement> MeshElement = FDatasmithSceneBaseGraphBuilder::FindOrAddMeshElement(Body, InLabel))
	{
		FString BodyFile = FString::Printf(TEXT("UEx%08x"), Body.MeshActorName);
		MeshElement->SetFile(*FPaths::Combine(CachePath, TEXT("body"), BodyFile + TEXT(".ct")));

		return MeshElement;
	}

	return TSharedPtr<IDatasmithMeshElement>();
}

FDatasmithSceneBaseGraphBuilder::FDatasmithSceneBaseGraphBuilder(CADLibrary::FArchiveSceneGraph* InSceneGraph, TSharedRef<IDatasmithScene> InScene, const FDatasmithSceneSource& InSource, const CADLibrary::FImportParameters& InImportParameters)
	: SceneGraph(InSceneGraph)
	, DatasmithScene(InScene)
	, ImportParameters(InImportParameters)
	, ImportParametersHash(ImportParameters.GetHash())
	, rootFileDescription(*InSource.GetSourceFile())
{
	if (InSceneGraph)
	{
		ColorNameToColorArchive.Reserve(SceneGraph->ColorHIdToColor.Num());
		for(const auto& ColorPair : SceneGraph->ColorHIdToColor)
		{
			ColorNameToColorArchive.Emplace(ColorPair.Value.UEMaterialName, ColorPair.Value);
		}

		MaterialNameToMaterialArchive.Reserve(SceneGraph->MaterialHIdToMaterial.Num());
		for (const auto& MaterialPair : SceneGraph->MaterialHIdToMaterial)
		{
			MaterialNameToMaterialArchive.Emplace(MaterialPair.Value.UEMaterialName, MaterialPair.Value);
		}
	}
}

bool FDatasmithSceneBaseGraphBuilder::Build()
{
	CadId RootId = 1;
	int32* Index = SceneGraph->CADIdToComponentIndex.Find(RootId);
	if (!Index)
	{
		return false;
	}

	ActorData Data(TEXT(""));
	CADLibrary::FArchiveComponent& Component = SceneGraph->ComponentSet[*Index];
	TSharedPtr< IDatasmithActorElement > RootActor = BuildComponent(Component, Data);
	DatasmithScene->AddActor(RootActor);

	// Set ProductName, ProductVersion in DatasmithScene for Analytics purpose
	// application_name is something like "Catia V5"
	DatasmithScene->SetVendor(TEXT("CoreTechnologie"));

	{
		if (rootFileDescription.Extension == TEXT("jt"))
		{
			DatasmithScene->SetProductName(TEXT("Jt"));
		}
		else if (rootFileDescription.Extension == TEXT("sldprt") || rootFileDescription.Extension == TEXT("sldasm"))
		{
			DatasmithScene->SetProductName(TEXT("SolidWorks"));
		}
		else if (rootFileDescription.Extension == TEXT("catpart") || rootFileDescription.Extension == TEXT("catproduct") || rootFileDescription.Extension == TEXT("cgr"))
		{
			DatasmithScene->SetProductName(TEXT("CATIA V5"));
		}
		else if (rootFileDescription.Extension == TEXT("3dxml") || rootFileDescription.Extension == TEXT("3drep"))
		{
			DatasmithScene->SetProductName(TEXT("3D XML"));
		}
		else if (rootFileDescription.Extension == TEXT("iam") || rootFileDescription.Extension == TEXT("ipt"))
		{
			DatasmithScene->SetProductName(TEXT("Inventor"));
		}
		else if (rootFileDescription.Extension == TEXT("prt") || rootFileDescription.Extension == TEXT("asm"))
		{
			DatasmithScene->SetProductName(TEXT("NX"));
		}
		else if (rootFileDescription.Extension == TEXT("stp") || rootFileDescription.Extension == TEXT("step"))
		{
			DatasmithScene->SetProductName(TEXT("STEP"));
		}
		else if (rootFileDescription.Extension == TEXT("igs") || rootFileDescription.Extension == TEXT("iges"))
		{
			DatasmithScene->SetProductName(TEXT("IGES"));
		}
		else if (rootFileDescription.Extension == TEXT("x_t") || rootFileDescription.Extension == TEXT("x_b"))
		{
			DatasmithScene->SetProductName(TEXT("Parasolid"));
		}
		else if (rootFileDescription.Extension == TEXT("dwg"))
		{
			DatasmithScene->SetProductName(TEXT("AutoCAD"));
		}
		else if (rootFileDescription.Extension == TEXT("dgn"))
		{
			DatasmithScene->SetProductName(TEXT("Micro Station"));
		}
		else if (rootFileDescription.Extension == TEXT("sat"))
		{
			DatasmithScene->SetProductName(TEXT("3D ACIS"));
		}
		else if (rootFileDescription.Extension.StartsWith(TEXT("asm")) || rootFileDescription.Extension.StartsWith(TEXT("creo")) || rootFileDescription.Extension.StartsWith(TEXT("prt")) || rootFileDescription.Extension.StartsWith(TEXT("neu")))
		{
			DatasmithScene->SetProductName(TEXT("Creo"));
		}
		else
		{
			DatasmithScene->SetProductName(TEXT("Unknown"));
		}
	}

	if (FString* ProductVersion = Component.MetaData.Find(TEXT("Input_Format_and_Emitter")))
	{
		DatasmithScene->SetProductVersion(**ProductVersion);
	}

	return true;
}

TSharedPtr< IDatasmithActorElement >  FDatasmithSceneBaseGraphBuilder::BuildInstance(int32 InstanceIndex, const ActorData& ParentData)
{
	CADLibrary::FArchiveComponent* Reference = nullptr;
	CADLibrary::FArchiveComponent EmptyReference;

	CADLibrary::FArchiveInstance& Instance = SceneGraph->Instances[InstanceIndex];

	CADLibrary::FArchiveSceneGraph* InstanceSceneGraph = SceneGraph;
	if (Instance.bIsExternalRef)
	{
		if (!Instance.ExternalRef.Path.IsEmpty())
		{
			uint32 InstanceSceneGraphHash = GetTypeHash(Instance.ExternalRef);
			SceneGraph = CADFileToSceneGraphArchive.FindRef(InstanceSceneGraphHash);
			if (SceneGraph)
			{
				if (AncestorSceneGraphHash.Find(InstanceSceneGraphHash) == INDEX_NONE)
				{
					AncestorSceneGraphHash.Add(InstanceSceneGraphHash);

					CadId RootId = 1;
					int32* Index = SceneGraph->CADIdToComponentIndex.Find(RootId);
					if (Index)
					{
						Reference = &(SceneGraph->ComponentSet[*Index]);
					}
				}
			}
		}

		if(!Reference)
		{
			SceneGraph = InstanceSceneGraph;
			int32* Index = SceneGraph->CADIdToUnloadedComponentIndex.Find(Instance.ReferenceNodeId);
			if (Index)
			{
				Reference = &(SceneGraph->UnloadedComponentSet[*Index]);
			}
		}
	}
	else {
		int32* Index = SceneGraph->CADIdToComponentIndex.Find(Instance.ReferenceNodeId);
		if (Index)
		{
			Reference = &(SceneGraph->ComponentSet[*Index]);
		}
	}

	if (!Reference) // Should never append
	{
		Reference = &EmptyReference;
	}

	FString ActorUUID;
	FString ActorLabel;
	GetNodeUUIDAndName(Instance.MetaData, Reference->MetaData, InstanceIndex, ParentData.Uuid, ActorUUID, ActorLabel);

	TSharedPtr< IDatasmithActorElement > Actor = CreateActor(*ActorUUID, *ActorLabel);
	if (!Actor.IsValid())
	{
		return TSharedPtr< IDatasmithActorElement >();
	}
	AddMetaData(Actor, Instance.MetaData, Reference->MetaData);

	ActorData InstanceData(*ActorUUID, ParentData);

	DatasmithSceneGraphBuilderImpl::GetMainMaterial(Instance.MetaData, InstanceData, bMaterialPropagationIsTopDown);
	DatasmithSceneGraphBuilderImpl::GetMainMaterial(Reference->MetaData, InstanceData, bMaterialPropagationIsTopDown);

	AddChildren(Actor, *Reference, InstanceData);

	DatasmithSceneGraphBuilderImpl::AddTransformToActor(Instance, Actor, ImportParameters);

	if (SceneGraph != InstanceSceneGraph)
	{
		SceneGraph = InstanceSceneGraph;
		AncestorSceneGraphHash.Pop();
	}
	return Actor;
}

TSharedPtr< IDatasmithActorElement >  FDatasmithSceneBaseGraphBuilder::CreateActor(const TCHAR* InEUUID, const TCHAR* InLabel)
{
	TSharedPtr< IDatasmithActorElement > Actor = FDatasmithSceneFactory::CreateActor(InEUUID);
	if (Actor.IsValid())
	{
		Actor->SetLabel(InLabel);
		return Actor;
	}
	return TSharedPtr< IDatasmithActorElement >();
}

void FDatasmithSceneBaseGraphBuilder::GetNodeUUIDAndName(
	TMap<FString, FString>& InInstanceNodeMetaDataMap,
	TMap<FString, FString>& InReferenceNodeMetaDataMap,
	int32 InComponentIndex,
	const TCHAR* InParentUEUUID,
	FString& OutUEUUID,
	FString& OutName
)
{
	FString* InstanceKernelIOName = InInstanceNodeMetaDataMap.Find(TEXT("CTName"));
	FString* InstanceCADName = InInstanceNodeMetaDataMap.Find(TEXT("Name"));
	FString* InstanceUUID = InInstanceNodeMetaDataMap.Find(TEXT("UUID"));

	FString* ReferenceKernelIOName = InReferenceNodeMetaDataMap.Find(TEXT("CTName"));
	FString* ReferenceCADName = InReferenceNodeMetaDataMap.Find(TEXT("Name"));
	FString* ReferenceUUID = InReferenceNodeMetaDataMap.Find(TEXT("UUID"));

	// Outname Name
	// IName and RName are KernelIO build Name. Original names (CAD system name) are preferred
	if (InstanceCADName && !InstanceCADName->IsEmpty())
	{
		OutName = *InstanceCADName;
	}
	else if (ReferenceCADName && !ReferenceCADName->IsEmpty())
	{
		OutName = *ReferenceCADName;
	}
	else if (InstanceKernelIOName && !InstanceKernelIOName->IsEmpty())
	{
		OutName = *InstanceKernelIOName;
	}
	else if (ReferenceKernelIOName && !ReferenceKernelIOName->IsEmpty())
	{
		OutName = *ReferenceKernelIOName;
	}
	else
	{
		OutName = "NoName";
	}

	CADUUID UEUUID = HashCombine(GetTypeHash(InParentUEUUID), GetTypeHash(InComponentIndex));

	if (InstanceUUID)
	{
		UEUUID = HashCombine(UEUUID, GetTypeHash(*InstanceUUID));
	}
	if (InstanceCADName)
	{
		UEUUID = HashCombine(UEUUID, GetTypeHash(*InstanceCADName));
	}
	if (InstanceKernelIOName)
	{
		UEUUID = HashCombine(UEUUID, GetTypeHash(*InstanceKernelIOName));
	}

	if (ReferenceUUID)
	{
		UEUUID = HashCombine(UEUUID, GetTypeHash(*ReferenceUUID));
	}
	if (ReferenceCADName)
	{
		UEUUID = HashCombine(UEUUID, GetTypeHash(*ReferenceCADName));
	}
	if (ReferenceKernelIOName)
	{
		UEUUID = HashCombine(UEUUID, GetTypeHash(*ReferenceKernelIOName));
	}

	OutUEUUID = FString::Printf(TEXT("0x%08x"), UEUUID);
}

TSharedPtr< IDatasmithActorElement > FDatasmithSceneBaseGraphBuilder::BuildComponent(CADLibrary::FArchiveComponent& Component, const ActorData& ParentData)
{
	TMap<FString, FString> InstanceNodeMetaDataMap;

	FString ActorUUID = TEXT("");
	FString ActorLabel = TEXT("");
	
	GetNodeUUIDAndName(InstanceNodeMetaDataMap, Component.MetaData, 1, ParentData.Uuid, ActorUUID, ActorLabel);

	TSharedPtr< IDatasmithActorElement > Actor = CreateActor(*ActorUUID, *ActorLabel);
	if (!Actor.IsValid())
	{
		return TSharedPtr< IDatasmithActorElement >();
	}

	AddMetaData(Actor, InstanceNodeMetaDataMap, Component.MetaData);

	ActorData ComponentData(*ActorUUID, ParentData);
	DatasmithSceneGraphBuilderImpl::GetMainMaterial(Component.MetaData, ComponentData, bMaterialPropagationIsTopDown);

	AddChildren(Actor, Component, ComponentData);

	return Actor;
}

TSharedPtr< IDatasmithActorElement > FDatasmithSceneBaseGraphBuilder::BuildBody(int32 BodyIndex, const ActorData& ParentData)
{
	TMap<FString, FString> InstanceNodeMetaDataMap;

	CADLibrary::FArchiveBody& Body = SceneGraph->BodySet[BodyIndex];

	FString BodyUUID;
	FString BodyLabel;
	GetNodeUUIDAndName(InstanceNodeMetaDataMap, Body.MetaData, BodyIndex, ParentData.Uuid, BodyUUID, BodyLabel);

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

TSharedPtr< IDatasmithMeshElement > FDatasmithSceneBaseGraphBuilder::FindOrAddMeshElement(CADLibrary::FArchiveBody& Body, FString& BodyName)
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
	MD5.Update(reinterpret_cast<const uint8*>(SceneGraph->ArchiveFileName.GetCharArray().GetData()), SceneGraph->ArchiveFileName.GetCharArray().Num());
	// MeshActorName
	MD5.Update(reinterpret_cast<const uint8*>(&Body.MeshActorName), sizeof Body.MeshActorName);

	FMD5Hash Hash;
	Hash.Set(MD5);
	MeshElement->SetFileHash(Hash);

	MeshElement->SetFile(*(ShellUuidName + TEXT(".ct")));


	// TODO: Set bounding box 
	//float BoundingBox[6];
	//FString Buffer = GetStringAttribute(GeomID, TEXT("UE_MESH_BBOX"));
	//if (FString::ToHexBlob(Buffer, (uint8*)BoundingBox, sizeof(BoundingBox)))
	//{
	//	MeshElement->SetDimensions(BoundingBox[3] - BoundingBox[0], BoundingBox[4] - BoundingBox[1], BoundingBox[5] - BoundingBox[2], 0.0f);
	//}

	// Currently we assume that face has only colors
	TSet<uint32>& MaterialSet = Body.ColorFaceSet;

	for (uint32 MaterialSlotId : MaterialSet)
	{
		TSharedPtr< IDatasmithMaterialIDElement > PartMaterialIDElement;
		PartMaterialIDElement = FindOrAddMaterial(MaterialSlotId);

		MeshElement->SetMaterial(PartMaterialIDElement->GetName(), MaterialSlotId);
	}

	DatasmithScene->AddMesh(MeshElement);

	BodyUuidToMeshElement.Add(Body.MeshActorName, MeshElement);

	return MeshElement;
}

TSharedPtr< IDatasmithUEPbrMaterialElement > FDatasmithSceneBaseGraphBuilder::GetDefaultMaterial()
{
	if (!DefaultMaterial.IsValid())
	{
		DefaultMaterial = CADLibrary::CreateDefaultUEPbrMaterial();
		DatasmithScene->AddMaterial(DefaultMaterial);
	}

	return DefaultMaterial;
}

TSharedPtr<IDatasmithMaterialIDElement> FDatasmithSceneBaseGraphBuilder::FindOrAddMaterial(uint32 MaterialUuid)
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

void FDatasmithSceneBaseGraphBuilder::AddMetaData(TSharedPtr< IDatasmithActorElement > ActorElement, TMap<FString, FString>& InstanceNodeAttributeSetMap, TMap<FString, FString>& ReferenceNodeAttributeSetMap)
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
		UnwantedAttributes.Add(TEXT("OriginalIdStr"));
		UnwantedAttributes.Add(TEXT("ShowAttribute"));
		UnwantedAttributes.Add(TEXT("Identification"));
		UnwantedAttributes.Add(TEXT("MaterialId"));
		UnwantedAttributes.Add(TEXT("ColorUEId"));
		UnwantedAttributes.Add(TEXT("ColorId"));
		UnwantedAttributes.Add(TEXT("KernelIOVersion"));
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

		if (Attribute.Value.IsEmpty())
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
				FString FileDir = FPaths::GetPath(rootFileDescription.Path);
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

		FString MetaName = TEXT("Reference ");
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

		if (Attribute.Value.IsEmpty())
		{
			continue;
		}

		FString MetaName = TEXT("Instance ");
		MetaName += Attribute.Key;
		TSharedRef< IDatasmithKeyValueProperty > KeyValueProperty = FDatasmithSceneFactory::CreateKeyValueProperty(*MetaName);

		KeyValueProperty->SetValue(*Attribute.Value);
		KeyValueProperty->SetPropertyType(EDatasmithKeyValuePropertyType::String);

		MetaDataRefElement->AddProperty(KeyValueProperty);
	}

	DatasmithScene->AddMetaData(MetaDataRefElement);

}

bool FDatasmithSceneBaseGraphBuilder::DoesActorHaveChildrenOrIsAStaticMesh(const TSharedPtr< IDatasmithActorElement >& ActorElement)
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


void FDatasmithSceneBaseGraphBuilder::AddChildren(TSharedPtr< IDatasmithActorElement > Actor, CADLibrary::FArchiveComponent& Component, const ActorData& ParentData)
{
	for (const int32 ChildId : Component.Children)
	{
		if (int32* ChildNodeIndex = SceneGraph->CADIdToInstanceIndex.Find(ChildId))
		{
			TSharedPtr< IDatasmithActorElement > ChildActor = BuildInstance(*ChildNodeIndex, ParentData);
			if (ChildActor.IsValid() && DoesActorHaveChildrenOrIsAStaticMesh(ChildActor))
			{
				Actor->AddChild(ChildActor);
			}
		}
		if (int32* ChildNodeIndex = SceneGraph->CADIdToBodyIndex.Find(ChildId))
		{
			TSharedPtr< IDatasmithActorElement > ChildActor = BuildBody(*ChildNodeIndex, ParentData);
			if (ChildActor.IsValid() && DoesActorHaveChildrenOrIsAStaticMesh(ChildActor))
			{
				Actor->AddChild(ChildActor);
			}
		}
	}
}
