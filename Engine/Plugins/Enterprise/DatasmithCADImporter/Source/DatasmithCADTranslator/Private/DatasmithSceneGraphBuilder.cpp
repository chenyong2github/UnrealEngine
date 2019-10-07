// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "DatasmithSceneGraphBuilder.h"

#ifdef USE_CORETECH_MT_PARSER

#include "CoreTechHelper.h"
#include "CoreTechParserMt.h"
#include "DatasmithSceneFactory.h"
#include "DatasmithSceneSource.h"
#include "IDatasmithSceneElements.h"
#include "Misc/FileHelper.h"
#include "Utility/DatasmithMathUtils.h"

using namespace CADLibrary;

FCTRawDataFile::FCTRawDataFile(const FString& InFileName) 
	: FileName(InFileName)
{
	ReadFile();
}

void FCTRawDataFile::ReadFile()
{
	FFileHelper::LoadFileToStringArray(RawData, *FileName);
	if (RawData.Num() < 10)
		return;

	FileName = FPaths::GetBaseFilename(FileName);

	FString Left, Right;
	FString HeaderCTId = RawData[MAPCTIDLINE];
	HeaderCTId.Split(SPACE, &Left, &Right);
	int32 Line = FCString::Atoi(*Right);

	TArray<FString> HeaderCTIdSplit;
	RawData[Line].ParseIntoArray(HeaderCTIdSplit, TEXT(" "));

	for (int32 index = 0; index < HeaderCTIdSplit.Num(); index += 2)
	{
		CTIdToRawEntryMap.Emplace(FCString::Atoi(*HeaderCTIdSplit[index]), FCTNode(*this, FCString::Atoi(*HeaderCTIdSplit[index + 1])));
	}


	// Parse color
	HeaderCTId = RawData[COLORSETLINE];
	HeaderCTId.Split(SPACE, &Left, &Right);
	if (Right != TEXT("0"))
	{
		Line = FCString::Atoi(*Right);
		FString ColorString = RawData[Line];
		while (!ColorString.IsEmpty())
		{
			ColorString.Split(SPACE, &Left, &Right);
			ColorIdToLineMap.Add(FCString::Atoi(*Left), Line);
			Line++;
			ColorString = RawData[Line];
		}
	}
	
	// Parse material
	HeaderCTId = RawData[MATERIALSETLINE];
	HeaderCTId.Split(SPACE, &Left, &Right);
	if (Right != TEXT("0"))
	{
		Line = FCString::Atoi(*Right);
		FString MaterialString = RawData[Line];
		while (!MaterialString.IsEmpty())
		{
			MaterialString.Split(SPACE, &Left, &Right);
			MaterialIdToLineMap.Add(FCString::Atoi(*Left), Line);
			Line += 2;
			MaterialString = RawData[Line];
		}
	}

}

void FCTNode::GetMetaDatas(TMap<FString, FString>& MetaDataMap)
{
	const FString& RawDataString = RawData.GetString(Line+1);
	if (RawDataString.GetCharArray()[0] != L'M')
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
		MetaDataMap.Add(RawData.GetString(LineIndex), RawData.GetString(LineIndex + 1));
	}
}

void FCTNode::GetMaterialSet(FCTMaterialPartition& MaterialPartition)
{
	const FString& MaterialString = RawData.GetString(EndMeta);
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
		MaterialPartition.LinkMaterialId2MaterialHash(MaterialId, MaterialHash);
	}
}

void FCTNode::GetChildren(TArray<int32>& Children)
{
	if(EndMeta == -1)
	{
		TMap<FString, FString> TempMap;
		GetMetaDatas(TempMap);
	}

	const FString& RawDataString = RawData.GetString(EndMeta);
	if (RawDataString.GetCharArray()[0] != L'c')
	{
		return;
	}

	FString Left, Right;
	RawDataString.Split(SPACE, &Left, &Right);
	int32 nbChildren = FCString::Atoi(*Right);
	Children.Reserve(nbChildren);

	int32 StartChildren = EndMeta + 1;
	int32 EndChildren = StartChildren + nbChildren;
	for (int32 LineIndex = StartChildren; LineIndex < EndChildren; LineIndex += 1)
	{
		Children.Add(FCString::Atoi(*RawData.GetString(LineIndex)));
	}
}

void FCTNode::GetNodeReference(int32& RefId, FString& ExternalFile, NODE_TYPE& NodeType)
{
	RefId = 0;
	ExternalFile = "";

	if (EndMeta == -1)
	{
		TMap<FString, FString> TempMap;
		GetMetaDatas(TempMap);
	}

	const FString& RawDataString = RawData.GetString(EndMeta + 1);

	FString TypeStr, Right;
	RawDataString.Split(SPACE, &TypeStr, &Right);

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

void FCTNode::AddTransformToActor(TSharedPtr< IDatasmithActorElement > Actor, CADLibrary::FImportParameters& ImportParameters)
{
	if (!Actor.IsValid())
	{
		return;
	}

	const FString& RawDataString = RawData.GetString(EndMeta);
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
	FTransform LocalUETransform = FDatasmithUtils::ConvertTransform(FDatasmithUtils::EModelCoordSystem::ZUp_RightHanded, LocalTransform);
	FQuat Quat;
	FDatasmithTransformUtils::GetRotation(LocalUETransform, Quat);
	
	Actor->SetTranslation(LocalUETransform.GetTranslation() * ImportParameters.ScaleFactor);
	Actor->SetScale(LocalUETransform.GetScale3D());
	Actor->SetRotation(Quat);
}

void FCTNode::SetNodeType()
{
	if (Line == 0)
	{
		Type = UNDEFINED;
	} 
	else
	{
		const FString& RawDataString = RawData.GetString(Line);
		switch (RawDataString.GetCharArray()[0])
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

uint32 FCTNode::GetStaticMeshUuid()
{
	if (!BodyUUID)
	{
		BodyUUID = GetTypeHash(RawData.GetFileName());
		BodyUUID = HashCombine(BodyUUID, GetTypeHash(*RawData.GetString(Line)));  // "B "+TEXT(CT_OBJECT_ID)
	}
	return BodyUUID;
}

void FCTNode::GetColorDescription(int32 ColorId, uint8 CtColor[3])
{
	RawData.GetColorDescription(ColorId, CtColor);
}

void FCTNode::GetMaterialDescription(int32 MaterialId, int32& OutMaterialHash, FString& OutMaterialName, uint8 OutDiffuse[3], uint8 OutAmbient[3], uint8 OutSpecular[3], uint8& OutShininess, uint8& OutTransparency, uint8& OutReflexion, FString& OutTextureName)
{
	RawData.GetMaterialDescription(MaterialId, OutMaterialHash, OutMaterialName, OutDiffuse, OutAmbient, OutSpecular, OutShininess, OutTransparency, OutReflexion, OutTextureName);
}

FCTNode* FCTNode::GetCTNode(int32 NodeId)
{
	return RawData.GetCTNode(NodeId);
}

FCTNode::FCTNode(FCTRawDataFile& InRawData, int32 InLine)
	: RawData(InRawData)
	, Line(InLine)
	, StartMeta(-1)
	, EndMeta(-1)
	, Type(UNKNOWN)
	, BodyUUID(0)
{
	SetNodeType();
}

FDatasmithSceneGraphBuilder::FDatasmithSceneGraphBuilder(const FString& InCachePath, TSharedRef<IDatasmithScene> InScene, const FDatasmithSceneSource& InSource, TMap<FString, FString>& InCADFileToUE4FileMap, TMap< TSharedPtr< IDatasmithMeshElement >, uint32 >& InMeshElementToCTBodyUuidMap)
	: DatasmithScene(InScene)
	, Source(InSource)
	, CachePath(InCachePath)
	, CADFileToRawDataFile(InCADFileToUE4FileMap)
	, MeshElementToCTBodyUuidMap(InMeshElementToCTBodyUuidMap)
{

}

bool FDatasmithSceneGraphBuilder::Build()
{
	LoadRawDataFile();

	const FString& RootFile = FPaths::GetCleanFilename(Source.GetSourceFile());

	FCTRawDataFile** RawData = CADFileToRawData.Find(RootFile);
	if (RawData == nullptr || *RawData == nullptr)
	{
		return false;
	}

	FCTNode* RootNode = (*RawData)->GetCTNode(1);
	if (RootNode == nullptr)
	{
		return false;
	}

	TSharedPtr< IDatasmithActorElement > RootActor = BuildNode(*RootNode, TEXT(""));
	DatasmithScene->AddActor(RootActor);

	return true;
}

void FDatasmithSceneGraphBuilder::LoadRawDataFile()
{
	RawDataArray.Reserve(CADFileToRawDataFile.Num());
	CADFileToRawData.Reserve(CADFileToRawDataFile.Num());
	for (auto FilePair : CADFileToRawDataFile)
	{
		//OutSgFile = FilePair.Value;
		FString RawDataFile = FPaths::Combine(CachePath, TEXT("scene"), FilePair.Value + TEXT(".sg"));
		uint32 index = RawDataArray.Emplace(RawDataFile);
		CADFileToRawData.Emplace(FilePair.Key, &RawDataArray[index]);
	}
}

TSharedPtr< IDatasmithActorElement >  FDatasmithSceneGraphBuilder::BuildNode(FCTNode& Node, const TCHAR* ParentUuid)
{
	NODE_TYPE Type = Node.GetNodeType();

	switch (Type)
	{
	case INSTANCE:
		return BuildInstance(Node, ParentUuid);

	case COMPONENT:
		return BuildComponent(Node, ParentUuid);

	//case EXTERNALCOMPONENT:
		//return BuildExternalComponent(Node);

	case BODY:
		return BuildBody(Node, ParentUuid);
	default:
		return TSharedPtr< IDatasmithActorElement >();
	}

	return TSharedPtr< IDatasmithActorElement >();
}

TSharedPtr< IDatasmithActorElement >  FDatasmithSceneGraphBuilder::BuildInstance(FCTNode& Node, const TCHAR* ParentUuid)
{
	TMap<FString, FString> InstanceNodeMetaDataMap;
	TMap<FString, FString> ReferenceNodeMetaDataMap;
	Node.GetMetaDatas(InstanceNodeMetaDataMap);

	FString ActorUUID = TEXT("");
	FString ActorLabel = TEXT("");

	GetNodeUUIDAndName(InstanceNodeMetaDataMap, ReferenceNodeMetaDataMap, ParentUuid, ActorUUID, ActorLabel);

	int32 RefId; 
	NODE_TYPE NodeType;
	FString ReferenceFile;
	Node.GetNodeReference(RefId, ReferenceFile, NodeType);

	FCTNode* ComponentNode = nullptr;
	switch(NodeType)
	{
		case COMPONENT:
		{
			ComponentNode = Node.GetCTNode(RefId);
			break;
		}

		case EXTERNALCOMPONENT:
		{
			FCTRawDataFile** RawData = CADFileToRawData.Find(ReferenceFile);
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
		ComponentNode->GetMetaDatas(ReferenceNodeMetaDataMap);
	}

	TSharedPtr< IDatasmithActorElement > Actor = CreateActor(*ActorUUID, *ActorLabel);
	if (!Actor.IsValid())
	{
		return TSharedPtr< IDatasmithActorElement >();
	}
	AddMetaData(Actor, InstanceNodeMetaDataMap, ReferenceNodeMetaDataMap);

	if (ComponentNode)
	{
		AddChildren(Actor, *ComponentNode, *ActorUUID);
	}

	Node.AddTransformToActor(Actor, ImportParameters);
	return Actor;
}

//void FDatasmithSceneGraphBuilder::SetNodeParameterFromAttribute(bool bIsBody)
//{
//	FString* IName = InstanceNodeAttributeSet.Find(TEXT("CTName"));
//	FString* IOriginalName = InstanceNodeAttributeSet.Find(TEXT("Name"));
//	FString* IUUID = InstanceNodeAttributeSet.Find(TEXT("UUID"));
//
//	FString* RName = ReferenceNodeAttributeSet.Find(TEXT("CTName"));
//	FString* ROriginalName = ReferenceNodeAttributeSet.Find(TEXT("Name"));
//	FString* RUUID = ReferenceNodeAttributeSet.Find(TEXT("UUID"));
//
//	// Reference Name
//	if (ROriginalName)
//	{
//		ReferenceName = *ROriginalName;
//	}
//	else if (RName)
//	{
//		ReferenceName = *RName;
//	}
//	else
//	{
//		ReferenceName = "NoName";
//	}
//
//	//ReferenceInstanceName = ReferenceName + TEXT("(") + (IOriginalName ? *IOriginalName : ReferenceName) + TEXT(")");
//	ReferenceInstanceName = IOriginalName ? *IOriginalName : IName ? *IName : ReferenceName;
//
//	//// UUID
//	if (bIsBody)
//	{
//		BuildMeshActorUUID();
//	}
//	else
//	{
//		UEUUID = 0;
//		if (Parent.IsValid())
//		{
//			UEUUID = Parent->GetUUID();
//		}
//
//		if (IUUID)
//		{
//			UEUUID = HashCombine(UEUUID, GetTypeHash(*IUUID));
//		}
//		else if (IOriginalName)
//		{
//			UEUUID = HashCombine(UEUUID, GetTypeHash(*IOriginalName));
//		}
//
//		if (RUUID)
//		{
//			UEUUID = HashCombine(UEUUID, GetTypeHash(*RUUID));
//		}
//		else if (RName)
//		{
//			UEUUID = HashCombine(UEUUID, GetTypeHash(*RName));
//		}
//
//		UEUUIDStr = FString::Printf(TEXT("0x%08x"), UEUUID);
//	}
//}

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
	else if (IOriginalName)
	{
		UEUUID = HashCombine(UEUUID, GetTypeHash(*IOriginalName));
	}

	if (RUUID)
	{
		UEUUID = HashCombine(UEUUID, GetTypeHash(*RUUID));
	}
	else if (RName)
	{
		UEUUID = HashCombine(UEUUID, GetTypeHash(*RName));
	}

	OutUEUUID = FString::Printf(TEXT("0x%08x"), UEUUID);
}


TSharedPtr< IDatasmithActorElement > FDatasmithSceneGraphBuilder::BuildComponent(FCTNode& Node, const TCHAR* ParentUuid)
{
	TMap<FString, FString> InstanceNodeMetaDataMap;
	TMap<FString, FString> ReferenceNodeMetaDataMap;
	Node.GetMetaDatas(ReferenceNodeMetaDataMap);

	FString ActorUUID = TEXT("");
	FString ActorLabel = TEXT("");
	
	GetNodeUUIDAndName(InstanceNodeMetaDataMap, ReferenceNodeMetaDataMap, ParentUuid, ActorUUID, ActorLabel);

	TSharedPtr< IDatasmithActorElement > Actor = CreateActor(*ActorUUID, *ActorLabel);
	if (!Actor.IsValid())
	{
		return TSharedPtr< IDatasmithActorElement >();
	}

	AddMetaData(Actor, InstanceNodeMetaDataMap, ReferenceNodeMetaDataMap);

	AddChildren(Actor, Node, *ActorUUID);

	return Actor;
}

TSharedPtr< IDatasmithActorElement > FDatasmithSceneGraphBuilder::BuildBody(FCTNode& Node, const TCHAR* ParentUuid)
{
	TMap<FString, FString> InstanceNodeMetaDataMap;
	TMap<FString, FString> BodyNodeMetaDataMap;
	Node.GetMetaDatas(BodyNodeMetaDataMap);
	
	FString BodyUUID = TEXT("");
	FString BodyLabel = TEXT("");
	GetNodeUUIDAndName(InstanceNodeMetaDataMap, BodyNodeMetaDataMap, ParentUuid, BodyUUID, BodyLabel);

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

	return ActorElement;
}

TSharedPtr< IDatasmithMeshElement > FDatasmithSceneGraphBuilder::FindOrAddMeshElement(FCTNode& Node, FString& BodyName)
{
	uint32 ShellUuid = Node.GetStaticMeshUuid();
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

	// TODO: Set bounding box 
	//float BoundingBox[6];
	//FString Buffer = GetStringAttribute(GeomID, TEXT("UE_MESH_BBOX"));
	//if (FString::ToHexBlob(Buffer, (uint8*)BoundingBox, sizeof(BoundingBox)))
	//{
	//	MeshElement->SetDimensions(BoundingBox[3] - BoundingBox[0], BoundingBox[4] - BoundingBox[1], BoundingBox[5] - BoundingBox[2], 0.0f);
	//}

	FCTMaterialPartition MaterialPartition;
	Node.GetMaterialSet(MaterialPartition);

	for (auto MaterialId2Hash : MaterialPartition.GetMaterialIdToHashSet())
	{
		TSharedPtr< IDatasmithMaterialIDElement > PartMaterialIDElement;
		PartMaterialIDElement = FindOrAddMaterial(Node, MaterialId2Hash.Key);

		const TCHAR* MaterialIDElementName = PartMaterialIDElement->GetName();

		TSharedPtr< IDatasmithMaterialIDElement > MaterialIDElement = FDatasmithSceneFactory::CreateMaterialId(MaterialIDElementName);

		MeshElement->SetMaterial(MaterialIDElementName, MaterialId2Hash.Value);
	}

	DatasmithScene->AddMesh(MeshElement);

	BodyUuidToMeshElementMap.Add(ShellUuid, MeshElement);
	MeshElementToCTBodyUuidMap.Add(MeshElement, ShellUuid);

	return MeshElement;
}

TSharedPtr< IDatasmithUEPbrMaterialElement > FDatasmithSceneGraphBuilder::GetDefaultMaterial()
{
	if (!DefaultMaterial.IsValid())
	{
		DefaultMaterial = CreateDefaultUEPbrMaterial();
		DatasmithScene->AddMaterial(DefaultMaterial);
	}

	return DefaultMaterial;
}

TSharedPtr<IDatasmithMaterialIDElement> FDatasmithSceneGraphBuilder::FindOrAddMaterial(FCTNode& Node, uint32 MaterialID)
{
	TSharedPtr< IDatasmithUEPbrMaterialElement > MaterialElement;

	TSharedPtr< IDatasmithUEPbrMaterialElement >* MaterialPtr = MaterialMap.Find(MaterialID);
	if (MaterialPtr != nullptr)
	{
		MaterialElement = *MaterialPtr;
	}
	else if (MaterialID > 0)
	{
		if (MaterialID > LAST_CT_MATERIAL_ID)
		{
			MaterialElement = CreateUEPbrMaterialFromColorId(Node, MaterialID);
		}
		else
		{
			MaterialElement = CreateUEPbrMaterialFromMaterialId(Node, MaterialID, DatasmithScene);
		}

		if (MaterialElement.IsValid())
		{
			DatasmithScene->AddMaterial(MaterialElement);
			MaterialMap.Add(MaterialID, MaterialElement);
		}
		DatasmithScene->AddMaterial(MaterialElement);
	}

	if (!MaterialElement.IsValid())
	{
		MaterialElement = GetDefaultMaterial();
	}

	TSharedPtr< IDatasmithMaterialIDElement > MaterialIDElement = FDatasmithSceneFactory::CreateMaterialId(MaterialElement->GetName());

	return MaterialIDElement;
}

TSharedPtr<IDatasmithUEPbrMaterialElement> FDatasmithSceneGraphBuilder::CreateUEPbrMaterialFromColorId(FCTNode& Node, uint32 InColorHashId)
{
	uint32 ColorId;
	uint8 Alpha;
	UnhashFastColorHash(InColorHashId, ColorId, Alpha);

	uint8 Color[3];
	Node.GetColorDescription(ColorId, Color);

	FString Name = FString::FromInt(BuildColorHash(ColorId, Alpha));
	FString Label = FString::Printf(TEXT("%02x%02x%02x%02x"), Color[0], Color[1], Color[2], Alpha);

	// Take the Material diffuse color and connect it to the BaseColor of a UEPbrMaterial
	TSharedRef<IDatasmithUEPbrMaterialElement> MaterialElement = FDatasmithSceneFactory::CreateUEPbrMaterial(*Name);
	MaterialElement->SetLabel(*Label);

	FLinearColor LinearColor = FLinearColor::FromPow22Color(FColor(Color[0], Color[1], Color[2], Alpha));

	IDatasmithMaterialExpressionColor* ColorExpression = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionColor>();
	ColorExpression->SetName(TEXT("Diffuse Color"));
	ColorExpression->GetColor() = LinearColor;

	MaterialElement->GetBaseColor().SetExpression(ColorExpression);

	if (LinearColor.A < 1.0f)
	{
		MaterialElement->SetBlendMode(/*EBlendMode::BLEND_Translucent*/2);

		IDatasmithMaterialExpressionScalar* Scalar = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionScalar>();
		Scalar->GetScalar() = LinearColor.A;

		MaterialElement->GetOpacity().SetExpression(Scalar);
	}
	return MaterialElement;
}

TSharedPtr<IDatasmithUEPbrMaterialElement> FDatasmithSceneGraphBuilder::CreateUEPbrMaterialFromMaterialId(FCTNode& Node, uint32 InMaterialID, TSharedRef<IDatasmithScene> Scene)
{
	FString MaterialLabel, TextureName;
	uint8   Diffuse[3];
	uint8   Ambient[3];
	uint8   Specular[3];
	uint8   Shininess, Transparency, Reflexion;
	int32  Hash;

	FString MaterialName, MaterialStr;
	Node.GetMaterialDescription(InMaterialID, Hash, MaterialLabel, Diffuse, Ambient, Specular, Shininess, Transparency, Reflexion, TextureName);

	FString Name = FString::FromInt(Hash);
	// Take the Material diffuse color and connect it to the BaseColor of a UEPbrMaterial
	TSharedRef<IDatasmithUEPbrMaterialElement> MaterialElement = FDatasmithSceneFactory::CreateUEPbrMaterial(*Name);
	if (MaterialLabel.IsEmpty())
	{
		MaterialLabel = TEXT("Material");
	}
	MaterialElement->SetLabel(*MaterialLabel);

	// Set a diffuse color if there's nothing in the BaseColor
	if (MaterialElement->GetBaseColor().GetExpression() == nullptr)
	{
		FColor Color((uint8)Diffuse[0], (uint8)Diffuse[1], (uint8)Diffuse[2], Transparency);
		FLinearColor LinearColor = FLinearColor::FromPow22Color(Color);

		IDatasmithMaterialExpressionColor* ColorExpression = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionColor>();
		ColorExpression->SetName(TEXT("Diffuse Color"));
		ColorExpression->GetColor() = LinearColor;

		MaterialElement->GetBaseColor().SetExpression(ColorExpression);
	}

	if (Transparency < 255)
	{
		MaterialElement->SetBlendMode(/*EBlendMode::BLEND_Translucent*/2);
		IDatasmithMaterialExpressionScalar* Scalar = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionScalar>();
		Scalar->GetScalar() = (float) Transparency / 255.f;
		MaterialElement->GetOpacity().SetExpression(Scalar);
	}

	// Set a Emissive color 
	if (MaterialElement->GetEmissiveColor().GetExpression() == nullptr)
	{
		// Doc CT => TODO
		//GLfloat Specular[4] = { specular.rgb[0] / 255., specular.rgb[1] / 255., specular.rgb[2] / 255., 1. - transparency };
		//GLfloat Shininess[1] = { (float)(128 * shininess) };
		//glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, Specular);
		//glMaterialfv(GL_FRONT_AND_BACK, GL_SHININESS, Shininess);

		FColor Color((uint8)Specular[0], (uint8)Specular[1], (uint8)Specular[2], Transparency);
		FLinearColor LinearColor = FLinearColor::FromPow22Color(Color);

		IDatasmithMaterialExpressionColor* ColorExpression = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionColor>();
		ColorExpression->SetName(TEXT("Specular Color"));
		ColorExpression->GetColor() = LinearColor;

		MaterialElement->GetEmissiveColor().SetExpression(ColorExpression);
	}

	// Simple conversion of shininess and reflectivity to PBR roughness and metallic values; model could be improved to properly blend the values
	if (Shininess != 0)
	{
		IDatasmithMaterialExpressionScalar* Scalar = static_cast<IDatasmithMaterialExpressionScalar*>(MaterialElement->AddMaterialExpression(EDatasmithMaterialExpressionType::ConstantScalar));
		Scalar->GetScalar() = 1.f - Shininess/255.;
		MaterialElement->GetRoughness().SetExpression(Scalar);
	}

	if (Reflexion != 0)
	{
		IDatasmithMaterialExpressionScalar* Scalar = static_cast<IDatasmithMaterialExpressionScalar*>(MaterialElement->AddMaterialExpression(EDatasmithMaterialExpressionType::ConstantScalar));
		Scalar->GetScalar() = Reflexion;
		MaterialElement->GetMetallic().SetExpression(Scalar);
	}

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

		return UnwantedAttributes;
	};

	static const TSet<FString> UnwantedAttributes = GetUnwantedAttributes();

	TSharedRef< IDatasmithMetaDataElement > MetaDataRefElement = FDatasmithSceneFactory::CreateMetaData(ActorElement->GetName());
	MetaDataRefElement->SetAssociatedElement(ActorElement);

	for (auto& Attribute : ReferenceNodeAttributeSetMap)
	{
		if (UnwantedAttributes.Contains(*Attribute.Key))
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

void FDatasmithSceneGraphBuilder::AddChildren(TSharedPtr< IDatasmithActorElement > Actor, FCTNode& ComponentNode, const TCHAR* ParentUuid)
{
	TArray<int32> ChildrenIds;
	ComponentNode.GetChildren(ChildrenIds);
	for (const auto& ChildId : ChildrenIds)
	{
		FCTNode* ChildNode = ComponentNode.GetCTNode(ChildId);
		if (ChildNode == nullptr)
		{
			continue;
		}
		TSharedPtr< IDatasmithActorElement > ChildActor = BuildNode(*ChildNode, ParentUuid);
		if (ChildActor.IsValid())
		{
			Actor->AddChild(ChildActor);
		}
	}
}


#endif  // USE_CORETECH_MT_PARSER