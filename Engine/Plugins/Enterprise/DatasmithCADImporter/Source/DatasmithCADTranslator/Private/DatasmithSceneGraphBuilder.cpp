// Copyright Epic Games, Inc. All Rights Reserved.
#include "DatasmithSceneGraphBuilder.h"

#include "CADData.h"
#include "CADSceneGraph.h"
#include "DatasmithSceneFactory.h"
#include "DatasmithSceneSource.h"
#include "DatasmithUtils.h"
#include "IDatasmithSceneElements.h"
#include "MeshDescriptionHelper.h"
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

	void AddTransformToActor(const CADLibrary::FCADArchiveObject& Object, TSharedPtr< IDatasmithActorElement > Actor, const CADLibrary::FImportParameters& ImportParameters)
	{
		if (!Actor.IsValid())
		{
			return;
		}

		FTransform LocalTransform(Object.TransformMatrix);
		FTransform LocalUETransform = FDatasmithUtils::ConvertTransform(ImportParameters.GetModelCoordSys(), LocalTransform);

		Actor->SetTranslation(LocalUETransform.GetTranslation() * ImportParameters.GetScaleFactor());
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


FDatasmithSceneGraphBuilder::FDatasmithSceneGraphBuilder(
	TMap<uint32, FString>& InCADFileToUnrealFileMap, 
	const FString& InCachePath, 
	TSharedRef<IDatasmithScene> InScene, 
	const FDatasmithSceneSource& InSource, 
	const CADLibrary::FImportParameters& InImportParameters)
		: FDatasmithSceneBaseGraphBuilder(nullptr, InCachePath, InScene, InSource, InImportParameters)
		, CADFileToSceneGraphDescriptionFile(InCADFileToUnrealFileMap)
{
}

bool FDatasmithSceneGraphBuilder::Build()
{
	LoadSceneGraphDescriptionFiles();

	uint32 RootHash = RootFileDescription.GetDescriptorHash();
	SceneGraph = CADFileToSceneGraphArchive.FindRef(RootHash);

	if (!SceneGraph)
	{
		return false;
	}
	AncestorSceneGraphHash.Add(RootHash);

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
	CADLibrary::FFileDescriptor AnchorDescription(*CleanFilenameOfCADFile);

	uint32 AnchorHash = AnchorDescription.GetDescriptorHash();
	SceneGraph = CADFileToSceneGraphArchive.FindRef(AnchorHash);

	if (!SceneGraph)
	{
		return;
	}

	FCadId RootId = 1;
	const int32* Index = SceneGraph->CADIdToComponentIndex.Find(RootId);
	if (!Index)
	{
		return;
	}

	ActorData Data(TEXT(""));

	// TODO: check ParentData and Index validity?
	ActorData ParentData(ActorElement->GetName());
	CADLibrary::FArchiveComponent& Component = SceneGraph->Components[*Index];

	TMap<FString, FString> InstanceNodeMetaDataMap;
	FString ActorUUID;
	FString ActorLabel;
	GetNodeUUIDAndName(InstanceNodeMetaDataMap, Component.MetaData, Component.ObjectId, ParentData.Uuid, ActorUUID, ActorLabel);

	AddMetaData(ActorElement, InstanceNodeMetaDataMap, Component.MetaData);

	ActorData ComponentData(*ActorUUID, ParentData);
	DatasmithSceneGraphBuilderImpl::GetMainMaterial(Component.MetaData, ComponentData, bMaterialPropagationIsTopDown);

	AddChildren(ActorElement, Component, ComponentData);

	ActorElement->SetLabel(*ActorLabel);
}

FDatasmithSceneBaseGraphBuilder::FDatasmithSceneBaseGraphBuilder(CADLibrary::FArchiveSceneGraph* InSceneGraph, const FString& InCachePath,  TSharedRef<IDatasmithScene> InScene, const FDatasmithSceneSource& InSource, const CADLibrary::FImportParameters& InImportParameters)
	: SceneGraph(InSceneGraph)
	, CachePath(InCachePath)
	, DatasmithScene(InScene)
	, ImportParameters(InImportParameters)
	, ImportParametersHash(ImportParameters.GetHash())
	, RootFileDescription(*InSource.GetSourceFile())
	, bPreferMaterial(false)
	, bMaterialPropagationIsTopDown(ImportParameters.GetPropagation() == CADLibrary::EDisplayDataPropagationMode::TopDown)
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
	FCadId RootId = 1;
	const int32* Index = SceneGraph->CADIdToComponentIndex.Find(RootId);
	if (!Index)
	{
		return false;
	}

	ActorData Data(TEXT(""));
	CADLibrary::FArchiveComponent& Component = SceneGraph->Components[*Index];
	TSharedPtr< IDatasmithActorElement > RootActor = BuildComponent(Component, Data);
	DatasmithScene->AddActor(RootActor);

	// Set ProductName, ProductVersion in DatasmithScene for Analytics purpose
	// application_name is something like "Catia V5"
	DatasmithScene->SetVendor(TEXT("Techsoft"));

	if (const FString* ProductVersion = Component.MetaData.Find(TEXT("TechsoftVersion")))
	{
		DatasmithScene->SetProductVersion(**ProductVersion);
	}

	FString ProductName;
	const FString* ProductNamePtr = Component.MetaData.Find(TEXT("Input_Format_and_Emitter"));
	if(ProductNamePtr)
	{
		ProductName = *ProductNamePtr;
		ProductName.TrimStartAndEndInline();
		if (!ProductName.IsEmpty())
		{
			DatasmithScene->SetProductName(*ProductName);
		}
	}

	if(ProductName.IsEmpty())
	{
		switch (RootFileDescription.GetFileFormat())
		{
		case CADLibrary::ECADFormat::JT:
			DatasmithScene->SetProductName(TEXT("Jt"));
			break;
		case CADLibrary::ECADFormat::SOLIDWORKS:
			DatasmithScene->SetProductName(TEXT("SolidWorks"));
			break;
		case CADLibrary::ECADFormat::ACIS:
			DatasmithScene->SetProductName(TEXT("3D ACIS"));
			break;
		case CADLibrary::ECADFormat::CATIA:
			DatasmithScene->SetProductName(TEXT("CATIA V5"));
			break;
		case CADLibrary::ECADFormat::CATIA_CGR:
			DatasmithScene->SetProductName(TEXT("CATIA V5"));
			break;
		case CADLibrary::ECADFormat::CATIAV4:
			DatasmithScene->SetProductName(TEXT("CATIA V4"));
			break;
		case CADLibrary::ECADFormat::CATIA_3DXML:
			DatasmithScene->SetProductName(TEXT("3D XML"));
			break;
		case CADLibrary::ECADFormat::CREO:
			DatasmithScene->SetProductName(TEXT("Creo"));
			break;
		case CADLibrary::ECADFormat::IGES:
			DatasmithScene->SetProductName(TEXT("IGES"));
			break;
		case CADLibrary::ECADFormat::INVENTOR:
			DatasmithScene->SetProductName(TEXT("Inventor"));
			break;
		case CADLibrary::ECADFormat::NX:
			DatasmithScene->SetProductName(TEXT("NX"));
			break;
		case CADLibrary::ECADFormat::PARASOLID:
			DatasmithScene->SetProductName(TEXT("Parasolid"));
			break;
		case CADLibrary::ECADFormat::STEP:
			DatasmithScene->SetProductName(TEXT("STEP"));
			break;
		case CADLibrary::ECADFormat::DWG:
			DatasmithScene->SetProductName(TEXT("AutoCAD"));
			break;
		case CADLibrary::ECADFormat::DGN:
			DatasmithScene->SetProductName(TEXT("Micro Station"));
			break;
		default:
			DatasmithScene->SetProductName(TEXT("Unknown"));
			break;
		}
	}

	return true;
}

TSharedPtr< IDatasmithActorElement >  FDatasmithSceneBaseGraphBuilder::BuildInstance(int32 InstanceIndex, const ActorData& ParentData)
{
	CADLibrary::FArchiveComponent* Reference = nullptr;
	CADLibrary::FArchiveComponent EmptyReference;

	CADLibrary::FArchiveInstance& Instance = SceneGraph->Instances[InstanceIndex];

	CADLibrary::FArchiveSceneGraph* InstanceSceneGraph = SceneGraph;
	if (Instance.bIsExternalReference)
	{
		if (!Instance.ExternalReference.GetSourcePath().IsEmpty())
		{
			uint32 InstanceSceneGraphHash = Instance.ExternalReference.GetDescriptorHash();
			SceneGraph = CADFileToSceneGraphArchive.FindRef(InstanceSceneGraphHash);

			if (SceneGraph)
			{
				if (AncestorSceneGraphHash.Find(InstanceSceneGraphHash) == INDEX_NONE)
				{
					AncestorSceneGraphHash.Add(InstanceSceneGraphHash);

					FCadId RootId = 1;
					const int32* Index = SceneGraph->CADIdToComponentIndex.Find(RootId);
					if (Index)
					{
						Reference = &(SceneGraph->Components[*Index]);
					}
				}
			}
		}

		if(!Reference)
		{
			SceneGraph = InstanceSceneGraph;
			const int32* Index = SceneGraph->CADIdToUnloadedComponentIndex.Find(Instance.ReferenceNodeId);
			if (Index)
			{
				Reference = &(SceneGraph->UnloadedComponents[*Index]);
			}
		}
	}
	else 
	{
		const int32* Index = SceneGraph->CADIdToComponentIndex.Find(Instance.ReferenceNodeId);
		if (Index)
		{
			Reference = &(SceneGraph->Components[*Index]);
		}
	}

	if (!Reference) // Should never append
	{
		Reference = &EmptyReference;
	}

	FString ActorUUID;
	FString ActorLabel;
	GetNodeUUIDAndName(Instance.MetaData, Reference->MetaData, Instance.ObjectId, ParentData.Uuid, ActorUUID, ActorLabel);

	TSharedPtr<IDatasmithActorElement> Actor = CreateActor(*ActorUUID, *ActorLabel);
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
	return TSharedPtr<IDatasmithActorElement>();
}

void FDatasmithSceneBaseGraphBuilder::GetNodeUUIDAndName(
	const TMap<FString, FString>& InInstanceNodeMetaDataMap,
	const TMap<FString, FString>& InReferenceNodeMetaDataMap,
	int32 InComponentIndex,
	const TCHAR* InParentUEUUID,
	FString& OutUEUUID,
	FString& OutName
)
{
	const FString* InstanceSDKName = InInstanceNodeMetaDataMap.Find(TEXT("SDKName"));
	const FString* InstanceCADName = InInstanceNodeMetaDataMap.Find(TEXT("Name"));
	const FString* InstanceUUID = InInstanceNodeMetaDataMap.Find(TEXT("UUID"));

	const FString* ReferenceSDKName = InReferenceNodeMetaDataMap.Find(TEXT("SDKName"));
	const FString* ReferenceCADName = InReferenceNodeMetaDataMap.Find(TEXT("Name"));
	const FString* ReferenceUUID = InReferenceNodeMetaDataMap.Find(TEXT("UUID"));

	// Outname Name
	// Instance SDK Name and Reference SDName are build Name. Original names (CAD system name / "Name") are preferred
	if (InstanceCADName && !InstanceCADName->IsEmpty())
	{
		OutName = *InstanceCADName;
	}
	else if (ReferenceCADName && !ReferenceCADName->IsEmpty())
	{
		OutName = *ReferenceCADName;
	}
	else if (InstanceSDKName && !InstanceSDKName->IsEmpty())
	{
		OutName = *InstanceSDKName;
	}
	else if (ReferenceSDKName && !ReferenceSDKName->IsEmpty())
	{
		OutName = *ReferenceSDKName;
	}
	else
	{
		OutName = "NoName";
	}

	FCADUUID UEUUID = HashCombine(GetTypeHash(InParentUEUUID), GetTypeHash(InComponentIndex));

	if (InstanceUUID)
	{
		UEUUID = HashCombine(UEUUID, GetTypeHash(*InstanceUUID));
	}
	if (InstanceCADName)
	{
		UEUUID = HashCombine(UEUUID, GetTypeHash(*InstanceCADName));
	}
	if (InstanceSDKName)
	{
		UEUUID = HashCombine(UEUUID, GetTypeHash(*InstanceSDKName));
	}

	if (ReferenceUUID)
	{
		UEUUID = HashCombine(UEUUID, GetTypeHash(*ReferenceUUID));
	}
	if (ReferenceCADName)
	{
		UEUUID = HashCombine(UEUUID, GetTypeHash(*ReferenceCADName));
	}
	if (ReferenceSDKName)
	{
		UEUUID = HashCombine(UEUUID, GetTypeHash(*ReferenceSDKName));
	}

	OutUEUUID = FString::Printf(TEXT("0x%08x"), UEUUID);
}

TSharedPtr<IDatasmithActorElement> FDatasmithSceneBaseGraphBuilder::BuildComponent(CADLibrary::FArchiveComponent& Component, const ActorData& ParentData)
{
	TMap<FString, FString> InstanceNodeMetaDataMap;

	FString ActorUUID;
	FString ActorLabel;
	GetNodeUUIDAndName(InstanceNodeMetaDataMap, Component.MetaData, Component.ObjectId, ParentData.Uuid, ActorUUID, ActorLabel);

	TSharedPtr< IDatasmithActorElement > Actor = CreateActor(*ActorUUID, *ActorLabel);
	if (!Actor.IsValid())
	{
		return TSharedPtr< IDatasmithActorElement >();
	}

	AddMetaData(Actor, InstanceNodeMetaDataMap, Component.MetaData);

	ActorData ComponentData(*ActorUUID, ParentData);
	DatasmithSceneGraphBuilderImpl::GetMainMaterial(Component.MetaData, ComponentData, bMaterialPropagationIsTopDown);

	AddChildren(Actor, Component, ComponentData);

	DatasmithSceneGraphBuilderImpl::AddTransformToActor(Component, Actor, ImportParameters);

	return Actor;
}

TSharedPtr<IDatasmithActorElement> FDatasmithSceneBaseGraphBuilder::BuildBody(int32 BodyIndex, const ActorData& ParentData)
{
	TMap<FString, FString> InstanceNodeMetaDataMap;

	CADLibrary::FArchiveBody& Body = SceneGraph->Bodies[BodyIndex];

	if (Body.ParentId == 0 || Body.MeshActorName == 0)
	{
		return TSharedPtr<IDatasmithActorElement>();
	}

	FString BodyUUID;
	FString BodyLabel;
	GetNodeUUIDAndName(InstanceNodeMetaDataMap, Body.MetaData, Body.ObjectId, ParentData.Uuid, BodyUUID, BodyLabel);

	// Apply materials on the current part
	uint32 MaterialUuid = 0;
	MaterialUuid = ParentData.MaterialUuid ? ParentData.MaterialUuid : ParentData.ColorUuid;

	if (!(Body.ColorFaceSet.Num() + Body.MaterialFaceSet.Num()))
	{
		Body.ColorFaceSet.Add(MaterialUuid);
	}

	TSharedPtr<IDatasmithMeshElement> MeshElement = FindOrAddMeshElement(Body, BodyLabel);
	if (!MeshElement.IsValid())
	{
		return TSharedPtr< IDatasmithActorElement >();
	}


	TSharedPtr<IDatasmithMeshActorElement> ActorElement = FDatasmithSceneFactory::CreateMeshActor(*BodyUUID);
	if (!ActorElement.IsValid())
	{
		return TSharedPtr< IDatasmithActorElement >();
	}

	ActorElement->SetLabel(*BodyLabel);
	ActorElement->SetStaticMeshPathName(MeshElement->GetName());

	DatasmithSceneGraphBuilderImpl::AddTransformToActor(Body, ActorElement, ImportParameters);

	if (MaterialUuid && ImportParameters.GetPropagation() != CADLibrary::EDisplayDataPropagationMode::BodyOnly)
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

	TFunction<void(TSet<uint32>&)> SetMaterialToDatasmithMeshElement = [&](TSet<uint32>& MaterialSet)
	{
		for (uint32 MaterialSlotId : MaterialSet)
		{
			TSharedPtr< IDatasmithMaterialIDElement > PartMaterialIDElement;
			PartMaterialIDElement = FindOrAddMaterial(MaterialSlotId);
			MeshElement->SetMaterial(PartMaterialIDElement->GetName(), MaterialSlotId);
		}
	};

	SetMaterialToDatasmithMeshElement(Body.ColorFaceSet);
	SetMaterialToDatasmithMeshElement(Body.MaterialFaceSet);

	DatasmithScene->AddMesh(MeshElement);

	BodyUuidToMeshElement.Add(Body.MeshActorName, MeshElement);

	FString BodyCachePath = CADLibrary::BuildCacheFilePath(*CachePath, TEXT("body"), Body.MeshActorName);
	MeshElement->SetFile(*BodyCachePath);

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

		UnwantedAttributes.Add(TEXT("SDKName"));

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
				FString FileDir = RootFileDescription.GetRootFolder();
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


void FDatasmithSceneBaseGraphBuilder::AddChildren(TSharedPtr< IDatasmithActorElement > Actor, const CADLibrary::FArchiveComponent& Component, const ActorData& ParentData)
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
