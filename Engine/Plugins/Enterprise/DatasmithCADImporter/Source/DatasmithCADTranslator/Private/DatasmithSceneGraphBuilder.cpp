// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "DatasmithSceneGraphBuilder.h"

#ifdef CAD_INTERFACE
#include "CADData.h"
#include "CoreTechFileParser.h"
#include "CoreTechHelper.h"
#include "DatasmithSceneFactory.h"
#include "DatasmithSceneSource.h"
#include "IDatasmithSceneElements.h"
#include "Utility/DatasmithMathUtils.h"

#include "Misc/FileHelper.h"


namespace
{
	void GetMainMaterial(const TMap<FString, FString>& InNodeMetaDataMap, FSceneNodeDescription* InNode, ActorData& OutNodeData, bool bMaterialPropagationIsTopDown)
	{
		if (const FString* MaterialIdStr = InNodeMetaDataMap.Find(TEXT("MaterialId")))
		{
			if (!bMaterialPropagationIsTopDown || !OutNodeData.MaterialUuid)
			{
				uint32 MaterialId = FCString::Atoi64(**MaterialIdStr);
				CADLibrary::FCADMaterial Material;
				if (InNode->GetMaterial(MaterialId, Material))
				{
					OutNodeData.Material = Material;
					OutNodeData.MaterialUuid = CADLibrary::BuildMaterialHash(OutNodeData.Material);
				}
			}
		}

		if (const FString* ColorIdStr = InNodeMetaDataMap.Find(TEXT("ColorUEId")))
		{
			if (bMaterialPropagationIsTopDown || !OutNodeData.ColorUuid)
			{
				uint32 ColorHId = FCString::Atoi64(**ColorIdStr);
				FColor Color;
				if (InNode->GetColor(ColorHId, Color))
				{
					OutNodeData.Color = Color;
					OutNodeData.ColorUuid = CADLibrary::BuildColorHash(OutNodeData.Color);
				}
			}
		}
	}
}


FCADSceneGraphDescriptionFile::FCADSceneGraphDescriptionFile(const FString& InFileName) 
	: FileName(InFileName)
{
	ReadFile();
}

void FCADSceneGraphDescriptionFile::GetMaterialDescription(int32 LineNumber, CADLibrary::FCADMaterial& Material) const
{
	FString OutMaterialName;
	uint8 OutDiffuse[3];
	uint8 OutAmbient[3];
	uint8 OutSpecular[3];
	float OutShininess, OutTransparency, OutReflexion;
	FString OutTextureName;

	OutDiffuse[0] = OutDiffuse[1] = OutDiffuse[2] = 255;
	OutAmbient[0] = OutAmbient[1] = OutAmbient[2] = 255;
	OutSpecular[0] = OutSpecular[1] = OutSpecular[2] = 255;
	OutShininess = OutReflexion = 0;
	OutTransparency = 255;

	const FString& MaterialLine1 = GetString(LineNumber);
	FString MaterialIdStr;
	MaterialLine1.Split(SPACE, &MaterialIdStr, &OutMaterialName);

	const FString& MaterialLine2 = GetString(LineNumber+1);
	TArray<FString> MaterialParameters;
	MaterialLine2.ParseIntoArray(MaterialParameters, *SPACE);

	for (int32 index = 0; index < 3; index++)
	{
		OutDiffuse[index] = FCString::Atoi(*MaterialParameters[index + 2]);
		OutAmbient[index] = FCString::Atoi(*MaterialParameters[index + 5]);
		OutSpecular[index] = FCString::Atoi(*MaterialParameters[index + 8]);
	}
	OutShininess = FCString::Atof(*MaterialParameters[11]);
	OutTransparency = FCString::Atof(*MaterialParameters[12]);
	OutReflexion = FCString::Atof(*MaterialParameters[13]);
	if (MaterialParameters.Num() > 14)
	{
		OutTextureName = MaterialParameters[14];
	}

	Material.MaterialId = FCString::Atoi(*MaterialParameters[0]);
	Material.MaterialName = OutMaterialName;
	Material.Diffuse= FColor(OutDiffuse[0], OutDiffuse[1], OutDiffuse[2]);
	Material.Ambient = FColor(OutAmbient[0], OutAmbient[1], OutAmbient[2]);
	Material.Specular = FColor(OutSpecular[0], OutSpecular[1], OutSpecular[2]);
	Material.Shininess = OutShininess;
	Material.Transparency = OutTransparency;
	Material.Reflexion = OutReflexion;
	Material.TextureName = OutTextureName;
}

bool FCADSceneGraphDescriptionFile::GetColor(uint32 ColorHId, FColor& Color) const
{
	uint32 ColorId;
	uint8 Alpha;
	CADLibrary::UnhashFastColorHash(ColorHId, ColorId, Alpha);
	const FColor* ColorOpac = ColorIdToColor.Find(ColorId);
	if (ColorOpac)
	{
		Color = *ColorOpac;
		Color.A = Alpha;
		return true;
	}
	return false;
}

bool FCADSceneGraphDescriptionFile::GetMaterial(int32 MaterialId, CADLibrary::FCADMaterial& Material) const
{
	const CADLibrary::FCADMaterial* PMaterial = MaterialIdToMaterial.Find(MaterialId);
	if (PMaterial)
	{
		Material = *PMaterial;
		return true;
	}
	return false;
}

void FCADSceneGraphDescriptionFile::ReadFile()
{
	FFileHelper::LoadFileToStringArray(SceneGraphDescriptionData, *FileName);
	if (SceneGraphDescriptionData.Num() < 10)
		return;

	FileName = FPaths::GetBaseFilename(FileName);

	FString Left, Right;
	FString HeaderCTId = SceneGraphDescriptionData[MAPCTIDLINE];
	HeaderCTId.Split(SPACE, &Left, &Right);
	int32 Line = FCString::Atoi(*Right);

	TArray<FString> HeaderCTIdSplit;
	SceneGraphDescriptionData[Line].ParseIntoArray(HeaderCTIdSplit, TEXT(" "));

	if (HeaderCTIdSplit.Num() % 2 != 0)
	{
		return;
	}

	for (int32 index = 0; index < HeaderCTIdSplit.Num(); index += 2)
	{
		SceneNodeIdToSceneNodeDescriptionMap.Emplace(FCString::Atoi(*HeaderCTIdSplit[index]), FSceneNodeDescription(*this, FCString::Atoi(*HeaderCTIdSplit[index + 1])));
	}

	// Parse color
	HeaderCTId = SceneGraphDescriptionData[COLORSETLINE];
	HeaderCTId.Split(SPACE, &Left, &Right);
	if (Right != TEXT("0"))
	{
		Line = FCString::Atoi(*Right);
		FString ColorString = SceneGraphDescriptionData[Line];
		while (!ColorString.IsEmpty())
		{
			TArray<FString> ColorData;
			ColorString.ParseIntoArray(ColorData, TEXT(" "));
			ColorIdToColor.Emplace(FCString::Atoi(*ColorData[0]), FColor(FCString::Atoi(*ColorData[1]), FCString::Atoi(*ColorData[2]), FCString::Atoi(*ColorData[3])));
			Line++;
			ColorString = SceneGraphDescriptionData[Line];
		}
	}
	
	// Parse material
	HeaderCTId = SceneGraphDescriptionData[MATERIALSETLINE];
	HeaderCTId.Split(SPACE, &Left, &Right);
	if (Right != TEXT("0"))
	{
		Line = FCString::Atoi(*Right);
		FString MaterialString = SceneGraphDescriptionData[Line];
		while (!MaterialString.IsEmpty())
		{
			CADLibrary::FCADMaterial Material;
			GetMaterialDescription(Line, Material);
			MaterialIdToMaterial.Add(Material.MaterialId, Material);
			Line += 2;
			MaterialString = SceneGraphDescriptionData[Line];
		}
	}
}

void FCADSceneGraphDescriptionFile::SetMaterialMaps(TMap< uint32, FColor>& MaterialUuidToColor, TMap< uint32, CADLibrary::FCADMaterial>& MaterialUuidToMaterial)
{
	for (const auto& Color : ColorIdToColor)
	{
		uint32 ColorHash = CADLibrary::BuildColorHash(Color.Value);
		MaterialUuidToColor.Add(ColorHash, Color.Value);
	}
	for (const auto& Material : MaterialIdToMaterial)
	{
		uint32 MaterialHash = CADLibrary::BuildMaterialHash(Material.Value);
		MaterialUuidToMaterial.Add(MaterialHash, Material.Value);
	}
}

void FSceneNodeDescription::GetMetaData(TMap<FString, FString>& MetaDataMap)
{
	const FString& RawDataString = SceneGraphDescription.GetString(Line+1);
	if (RawDataString[0] != L'M')
	{
		return;
	}

	FString Left, Right;
	RawDataString.Split(SPACE, &Left, &Right);
	StartMeta = Line + 2;
	EndMeta = FCString::Atoi(*Right);
	MetaDataMap.Reserve(EndMeta / 2);
	EndMeta += StartMeta;
	for (int32 LineIndex = StartMeta; LineIndex < EndMeta; LineIndex += 2)
	{
		MetaDataMap.Add(SceneGraphDescription.GetString(LineIndex), SceneGraphDescription.GetString(LineIndex + 1));
	}
}

void FSceneNodeDescription::GetMaterialSet(TMap<uint32, uint32>& MaterialIdToMaterialHashMap)
{
	const FString& MaterialString = SceneGraphDescription.GetString(EndMeta);
	TArray<FString> MaterialIDToHashSet;
	MaterialString.ParseIntoArray(MaterialIDToHashSet, TEXT(" "));
	if (MaterialIDToHashSet[0] != TEXT("materialMap"))
	{
		return;
	}

	for (int32 index = 1; index < MaterialIDToHashSet.Num(); index++)
	{
		uint32 MaterialId = FCString::Atoi64(*MaterialIDToHashSet[index]); 
		index++;
		uint32 MaterialHash = FCString::Atoi64(*MaterialIDToHashSet[index]);
		MaterialIdToMaterialHashMap.Add(MaterialId, MaterialHash);
	}
}

void FSceneNodeDescription::GetChildren(TArray<int32>& Children)
{
	if(EndMeta == -1)
	{
		TMap<FString, FString> TempMap;
		GetMetaData(TempMap);
	}

	const FString& DescriptionString = SceneGraphDescription.GetString(EndMeta);
	if (DescriptionString[0] != L'c')
	{
		return;
	}

	FString Left, Right;
	DescriptionString.Split(SPACE, &Left, &Right);
	int32 nbChildren = FCString::Atoi(*Right);
	Children.Reserve(nbChildren);

	int32 StartChildren = EndMeta + 1;
	int32 EndChildren = StartChildren + nbChildren;
	for (int32 LineIndex = StartChildren; LineIndex < EndChildren; LineIndex += 1)
	{
		Children.Add(FCString::Atoi(*SceneGraphDescription.GetString(LineIndex)));
	}
}

void FSceneNodeDescription::GetNodeReference(int32& RefId, FString& ExternalFile, NODE_TYPE& NodeType)
{
	RefId = 0;
	ExternalFile = "";

	if (EndMeta == -1)
	{
		TMap<FString, FString> TempMap;
		GetMetaData(TempMap);
	}

	const FString& DescriptionString = SceneGraphDescription.GetString(EndMeta + 1);

	FString TypeStr, Right;
	DescriptionString.Split(SPACE, &TypeStr, &Right);

	if (TypeStr == TEXT("ref"))
	{
		RefId = FCString::Atoi(*Right);
		NodeType = COMPONENT;
	}
	else if (TypeStr == TEXT("ext"))
	{
		FString RefIdString;
		Right.Split(SPACE, &RefIdString, &ExternalFile);
		RefId = FCString::Atoi(*RefIdString);
		NodeType = EXTERNALCOMPONENT;
	}
	else
	{
		NodeType = UNDEFINED;
		return;
	}
}

void FSceneNodeDescription::AddTransformToActor(TSharedPtr< IDatasmithActorElement > Actor, const CADLibrary::FImportParameters& ImportParameters)
{
	if (!Actor.IsValid())
	{
		return;
	}

	const FString& RawDataString = SceneGraphDescription.GetString(EndMeta);
	TArray<FString> TransformString;
	RawDataString.ParseIntoArray(TransformString, TEXT(" "));

	if (TransformString[0] != TEXT("matrix"))
	{
		return;
	}

	if (TransformString.Num() != 17)
	{
		return;
	}

	FMatrix Matrix;
	float* MatrixFloats = (float*)Matrix.M;
	for(int32 index = 0; index < 16; index++)
	{
		MatrixFloats[index] = FCString::Atof(*TransformString[index + 1]);
	}

	FTransform LocalTransform(Matrix);
	FTransform LocalUETransform = FDatasmithUtils::ConvertTransform((FDatasmithUtils::EModelCoordSystem) ImportParameters.ModelCoordSys, LocalTransform);

	FQuat Quat;
	FDatasmithTransformUtils::GetRotation(LocalUETransform, Quat);
	
	Actor->SetTranslation(LocalUETransform.GetTranslation() * ImportParameters.ScaleFactor);
	Actor->SetScale(LocalUETransform.GetScale3D());
	Actor->SetRotation(Quat);
}

void FSceneNodeDescription::SetNodeType()
{
	if (Line == 0)
	{
		Type = UNDEFINED;
	} 
	else
	{
		const FString& RawDataString = SceneGraphDescription.GetString(Line);
		switch (RawDataString[0])
		{
		case L'C':
			Type = COMPONENT;
			break;
		case L'I':
			Type = INSTANCE;
			break;
		case L'B':
			Type = BODY;
			break;
		case L'E':
			Type = EXTERNALCOMPONENT;
			break;
		default:
			Type = UNDEFINED;
			break;
		}
	}
}

uint32 FSceneNodeDescription::GetStaticMeshUuid()
{
	if (!BodyUUID)
	{
		BodyUUID = GetTypeHash(SceneGraphDescription.GetFileName());
		BodyUUID = HashCombine(BodyUUID, GetTypeHash(*SceneGraphDescription.GetString(Line)));  // "B "+TEXT(CT_OBJECT_ID)
	}
	return BodyUUID;
}

bool FSceneNodeDescription::GetColor(int32 ColorHId, FColor& OutColor) const
{
	return SceneGraphDescription.GetColor(ColorHId, OutColor);
}

bool FSceneNodeDescription::GetMaterial(int32 MaterialId, CADLibrary::FCADMaterial& OutMaterial) const
{
	return SceneGraphDescription.GetMaterial(MaterialId, OutMaterial);
}

FSceneNodeDescription* FSceneNodeDescription::GetCTNode(int32 NodeId)
{
	return SceneGraphDescription.GetCTNode(NodeId);
}

FSceneNodeDescription::FSceneNodeDescription(FCADSceneGraphDescriptionFile& InSceneGraphDescription, int32 InLine)
	: SceneGraphDescription(InSceneGraphDescription)
	, Line(InLine)
	, StartMeta(-1)
	, EndMeta(-1)
	, Type(UNKNOWN)
	, BodyUUID(0)
{
	SetNodeType();
}

FDatasmithSceneGraphBuilder::FDatasmithSceneGraphBuilder(TMap<FString, FString>& InCADFileToUE4FileMap, TMap< TSharedPtr< IDatasmithMeshElement >, uint32 >& InMeshElementToCTBodyUuidMap, const FString& InCachePath, TSharedRef<IDatasmithScene> InScene, const FDatasmithSceneSource& InSource, const CADLibrary::FImportParameters& InImportParameters)
	: CADFileToSceneGraphDescriptionFile(InCADFileToUE4FileMap)
	, MeshElementToCADBodyUuidMap(InMeshElementToCTBodyUuidMap)
	, CachePath(InCachePath)
	, DatasmithScene(InScene)
	, Source(InSource)
	, ImportParameters(InImportParameters)
{

}

bool FDatasmithSceneGraphBuilder::Build()
{
	LoadSceneGraphDescriptionFiles();

	const FString& RootFile = FPaths::GetCleanFilename(Source.GetSourceFile());

	FCADSceneGraphDescriptionFile** RawData = CADFileToSceneGraphDescription.Find(RootFile);
	if (RawData == nullptr || *RawData == nullptr)
	{
		return false;
	}

	FSceneNodeDescription* RootNode = (*RawData)->GetCTNode(1);
	if (RootNode == nullptr)
	{
		return false;
	}

	ActorData Data(TEXT(""));
	TSharedPtr< IDatasmithActorElement > RootActor = BuildNode(*RootNode, Data);
	DatasmithScene->AddActor(RootActor);


	CADFileToSceneGraphDescription.Empty();
	ArrayOfSceneGraphDescription.Empty();
	BodyUuidToMeshElementMap.Empty();

	return true;
}


void FDatasmithSceneGraphBuilder::LoadSceneGraphDescriptionFiles()
{
	ArrayOfSceneGraphDescription.Reserve(CADFileToSceneGraphDescriptionFile.Num());
	CADFileToSceneGraphDescription.Reserve(CADFileToSceneGraphDescriptionFile.Num());
	for (auto FilePair : CADFileToSceneGraphDescriptionFile)
	{
		//OutSgFile = FilePair.Value;
		FString RawDataFile = FPaths::Combine(CachePath, TEXT("scene"), FilePair.Value + TEXT(".sg"));
		uint32 index = ArrayOfSceneGraphDescription.Emplace(RawDataFile);
		ArrayOfSceneGraphDescription[index].SetMaterialMaps(MaterialUuidToColor, MaterialUuidToMaterial);
		CADFileToSceneGraphDescription.Emplace(FilePair.Key, &ArrayOfSceneGraphDescription[index]);
	}
}

TSharedPtr< IDatasmithActorElement >  FDatasmithSceneGraphBuilder::BuildNode(FSceneNodeDescription& Node, ActorData& ParentData)
{
	NODE_TYPE Type = Node.GetNodeType();

	switch (Type)
	{
	case INSTANCE:
		return BuildInstance(Node, ParentData);

	case COMPONENT:
		return BuildComponent(Node, ParentData);

	case BODY:
		return BuildBody(Node, ParentData);

	default:
		return TSharedPtr< IDatasmithActorElement >();
	}
}

TSharedPtr< IDatasmithActorElement >  FDatasmithSceneGraphBuilder::BuildInstance(FSceneNodeDescription& Node, ActorData& ParentData)
{
	TMap<FString, FString> InstanceNodeMetaDataMap;
	TMap<FString, FString> ReferenceNodeMetaDataMap;
	Node.GetMetaData(InstanceNodeMetaDataMap);

	FString ActorUUID;
	FString ActorLabel;
	GetNodeUUIDAndName(InstanceNodeMetaDataMap, ReferenceNodeMetaDataMap, ParentData.Uuid, ActorUUID, ActorLabel);

	int32 RefId; 
	NODE_TYPE NodeType;
	FString ReferenceFile;
	Node.GetNodeReference(RefId, ReferenceFile, NodeType);

	FSceneNodeDescription* ComponentNode = nullptr;
	switch(NodeType)
	{
		case COMPONENT:
		{
			ComponentNode = Node.GetCTNode(RefId);
			break;
		}

		case EXTERNALCOMPONENT:
		{
			FCADSceneGraphDescriptionFile** RawData = CADFileToSceneGraphDescription.Find(ReferenceFile);
			if (RawData != nullptr && *RawData != nullptr)
			{
				ComponentNode = (*RawData)->GetCTNode(1);
			}
			else
			{
				ComponentNode = Node.GetCTNode(RefId);
			}
			break;
		}

		default:
		{
			return TSharedPtr< IDatasmithActorElement >();
		}
	}

	if (ComponentNode)
	{
		ComponentNode->GetMetaData(ReferenceNodeMetaDataMap);
	}

	TSharedPtr< IDatasmithActorElement > Actor = CreateActor(*ActorUUID, *ActorLabel);
	if (!Actor.IsValid())
	{
		return TSharedPtr< IDatasmithActorElement >();
	}
	AddMetaData(Actor, InstanceNodeMetaDataMap, ReferenceNodeMetaDataMap);

	ActorData InstanceData(*ActorUUID, ParentData);

	GetMainMaterial(InstanceNodeMetaDataMap, ComponentNode, InstanceData, bMaterialPropagationIsTopDown);
	GetMainMaterial(ReferenceNodeMetaDataMap, ComponentNode, InstanceData, bMaterialPropagationIsTopDown);

	if (ComponentNode)
	{
		AddChildren(Actor, *ComponentNode, InstanceData);
	}

	Node.AddTransformToActor(Actor, ImportParameters);
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

TSharedPtr< IDatasmithActorElement > FDatasmithSceneGraphBuilder::BuildComponent(FSceneNodeDescription& Node, ActorData& ParentData)
{
	TMap<FString, FString> InstanceNodeMetaDataMap;
	TMap<FString, FString> ReferenceNodeMetaDataMap;
	Node.GetMetaData(ReferenceNodeMetaDataMap);

	FString ActorUUID = TEXT("");
	FString ActorLabel = TEXT("");
	
	GetNodeUUIDAndName(InstanceNodeMetaDataMap, ReferenceNodeMetaDataMap, ParentData.Uuid, ActorUUID, ActorLabel);

	TSharedPtr< IDatasmithActorElement > Actor = CreateActor(*ActorUUID, *ActorLabel);
	if (!Actor.IsValid())
	{
		return TSharedPtr< IDatasmithActorElement >();
	}

	AddMetaData(Actor, InstanceNodeMetaDataMap, ReferenceNodeMetaDataMap);

	ActorData ComponentData(*ActorUUID, ParentData);
	GetMainMaterial(ReferenceNodeMetaDataMap, &Node, ComponentData, bMaterialPropagationIsTopDown);

	AddChildren(Actor, Node, ComponentData);

	return Actor;
}

TSharedPtr< IDatasmithActorElement > FDatasmithSceneGraphBuilder::BuildBody(FSceneNodeDescription& Node, ActorData& ParentData)
{
	TMap<FString, FString> InstanceNodeMetaDataMap;
	TMap<FString, FString> BodyNodeMetaDataMap;
	Node.GetMetaData(BodyNodeMetaDataMap);
	
	FString BodyUUID = TEXT("");
	FString BodyLabel = TEXT("");
	GetNodeUUIDAndName(InstanceNodeMetaDataMap, BodyNodeMetaDataMap, ParentData.Uuid, BodyUUID, BodyLabel);

	TSharedPtr< IDatasmithMeshElement > MeshElement = FindOrAddMeshElement(Node, BodyLabel);
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

	// Apply materials on the current part
	uint32 MaterialUuid = 0;
	MaterialUuid = ParentData.MaterialUuid ? ParentData.MaterialUuid : ParentData.ColorUuid;

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

TSharedPtr< IDatasmithMeshElement > FDatasmithSceneGraphBuilder::FindOrAddMeshElement(FSceneNodeDescription& Node, FString& BodyName)
{
	const uint32 ShellUuid = Node.GetStaticMeshUuid();
	FString ShellUuidName = FString::Printf(TEXT("0x%08x"), ShellUuid);

	// Look if geometry has not been already processed, return it if found
	TSharedPtr< IDatasmithMeshElement >* MeshElementPtr = BodyUuidToMeshElementMap.Find(ShellUuid);
	if (MeshElementPtr != nullptr)
	{
		return *MeshElementPtr;
	}

	TSharedPtr< IDatasmithMeshElement > MeshElement = FDatasmithSceneFactory::CreateMesh(*ShellUuidName);
	MeshElement->SetLabel(*BodyName);
	MeshElement->SetLightmapSourceUV(-1);

	FString BodyFile = FString::Printf(TEXT("UEx%08x"), ShellUuid);
	MeshElement->SetFile(*FPaths::Combine(CachePath, TEXT("body"), BodyFile + TEXT(".ct")));

	// TODO: Set bounding box 
	//float BoundingBox[6];
	//FString Buffer = GetStringAttribute(GeomID, TEXT("UE_MESH_BBOX"));
	//if (FString::ToHexBlob(Buffer, (uint8*)BoundingBox, sizeof(BoundingBox)))
	//{
	//	MeshElement->SetDimensions(BoundingBox[3] - BoundingBox[0], BoundingBox[4] - BoundingBox[1], BoundingBox[5] - BoundingBox[2], 0.0f);
	//}

	TMap<uint32, uint32> MaterialIdToMaterialHashMap;
	Node.GetMaterialSet(MaterialIdToMaterialHashMap);

	for (auto MaterialId2Hash : MaterialIdToMaterialHashMap)
	{
		TSharedPtr< IDatasmithMaterialIDElement > PartMaterialIDElement;
		PartMaterialIDElement = FindOrAddMaterial(MaterialId2Hash.Value);

		const TCHAR* MaterialIDElementName = PartMaterialIDElement->GetName();

		TSharedPtr< IDatasmithMaterialIDElement > MaterialIDElement = FDatasmithSceneFactory::CreateMaterialId(MaterialIDElementName);

		MeshElement->SetMaterial(MaterialIDElementName, MaterialId2Hash.Value);
	}

	DatasmithScene->AddMesh(MeshElement);

	BodyUuidToMeshElementMap.Add(ShellUuid, MeshElement);
	MeshElementToCADBodyUuidMap.Add(MeshElement, ShellUuid);

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
		if (FColor* Color = MaterialUuidToColor.Find(MaterialUuid))
		{
			MaterialElement = CADLibrary::CreateUEPbrMaterialFromColor(*Color);
		}
		else if (CADLibrary::FCADMaterial* Material = MaterialUuidToMaterial.Find(MaterialUuid))
		{
			MaterialElement = CADLibrary::CreateUEPbrMaterialFromMaterial(*Material, DatasmithScene);
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
	}

	TSharedPtr< IDatasmithMaterialIDElement > MaterialIDElement = FDatasmithSceneFactory::CreateMaterialId(MaterialElement->GetName());

	return MaterialIDElement;
}

TSharedPtr<IDatasmithUEPbrMaterialElement> CreateDefaultUEPbrMaterial()
{
	// Take the Material diffuse color and connect it to the BaseColor of a UEPbrMaterial
	TSharedRef<IDatasmithUEPbrMaterialElement> MaterialElement = FDatasmithSceneFactory::CreateUEPbrMaterial(TEXT("0"));
	MaterialElement->SetLabel(TEXT("DefaultCADImportMaterial"));

	FLinearColor LinearColor = FLinearColor::FromPow22Color(FColor(200, 200, 200, 255));
	IDatasmithMaterialExpressionColor* ColorExpression = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionColor>();
	ColorExpression->SetName(TEXT("Diffuse Color"));
	ColorExpression->GetColor() = LinearColor;
	MaterialElement->GetBaseColor().SetExpression(ColorExpression);

	return MaterialElement;
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

	for (auto& Attribute : InstanceNodeAttributeSetMap)
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

void FDatasmithSceneGraphBuilder::LinkActor(TSharedPtr< IDatasmithActorElement > ParentActor, TSharedPtr< IDatasmithActorElement > Actor)
{
	if (Actor.IsValid())
	{
		if (ParentActor.IsValid())
		{
			ParentActor->AddChild(Actor);
		}
		else
		{
			DatasmithScene->AddActor(Actor);
		}
	}
}

void FDatasmithSceneGraphBuilder::AddChildren(TSharedPtr< IDatasmithActorElement > Actor, FSceneNodeDescription& ComponentNode, ActorData& ParentData)
{
	TArray<int32> ChildrenIds;
	ComponentNode.GetChildren(ChildrenIds);
	for (const auto& ChildId : ChildrenIds)
	{
		FSceneNodeDescription* ChildNode = ComponentNode.GetCTNode(ChildId);
		if (ChildNode == nullptr)
		{
			continue;
		}
		TSharedPtr< IDatasmithActorElement > ChildActor = BuildNode(*ChildNode, ParentData);
		if (ChildActor.IsValid())
		{
			Actor->AddChild(ChildActor);
		}
	}
}

#endif // CAD_INTERFACE
