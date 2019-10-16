// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "CoreTechParserMT.h"

#if defined(USE_CORETECH_MT_PARSER) && defined(CAD_LIBRARY)

#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Containers/Queue.h"
#include "CoreTechHelper.h"
#include "DatasmithMeshHelper.h"
#include "DatasmithSceneFactory.h"
#include "DatasmithSceneSource.h"
#include "HAL/FileManager.h"
#include "IDatasmithSceneElements.h"
#include "Internationalization/Text.h"
#include "MeshDescription.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

using namespace CADLibrary;

#define SGSIZE 100000
#define EXTREFNUM 5000

FCoreTechParserMT::FCoreTechParserMT(const FString& InCachePath, const FDatasmithSceneSource& InSource, TMap<FString, FString>& SharedCADFileToUnrealFile, TMap<FString, FString>& SharedCADFileToGeomMap)
	: Source(InSource)
	, CachePath(InCachePath)
	, CADFileToUnrealFileMap(SharedCADFileToUnrealFile)
	, CADFileToUnrealGeomMap(SharedCADFileToGeomMap)
{
	CurrentSession = MakeShared<CADLibrary::CTSession>(TEXT("FCoreTechParserMT"), 0.001, 0.1);

    FilePath = FPaths::ConvertRelativePathToFull(FPaths::GetPath(Source.GetSourceFile()));
	CurrentSession->SetUnitFactors(Source.GetSourceFileExtension());
}

namespace
{
	uint32 GetFileHash(const FString& FileName, FFileStatData& FileStatData)
	{
		int64 FileSize = FileStatData.FileSize;
		FDateTime ModificationTime = FileStatData.ModificationTime;

		uint32 FileHash = GetTypeHash(FileName);
		FileHash = HashCombine(FileHash, GetTypeHash(FileSize));
		FileHash = HashCombine(FileHash, GetTypeHash(ModificationTime));
		return FileHash;
	}

	uint32 GetGeomFileHash(const uint32 InSGHash, CADLibrary::FImportParameters ImportParam)
	{
		uint32 FileHash = InSGHash;
		FileHash = HashCombine(FileHash, GetTypeHash(ImportParam.ChordTolerance));
		FileHash = HashCombine(FileHash, GetTypeHash(ImportParam.MaxEdgeLength));
		FileHash = HashCombine(FileHash, GetTypeHash(ImportParam.MaxNormalAngle));
		FileHash = HashCombine(FileHash, GetTypeHash(ImportParam.MetricUnit));
		FileHash = HashCombine(FileHash, GetTypeHash(ImportParam.ScaleFactor));
		FileHash = HashCombine(FileHash, GetTypeHash(ImportParam.StitchingTechnique));
		return FileHash;
	}

}

bool FCoreTechParserMT::Read()
{
	if (!CurrentSession->IsSessionValid())
	{
		return false;
	}

	AddFileToProcess(FPaths::GetCleanFilename(Source.GetSourceFile()));

	return ReadFileStack();
}

bool FCoreTechParserMT::ReadFileStack()
{
	FileToReadSet.Reserve(EXTREFNUM);
	FileLoaded.Reserve(EXTREFNUM);
	FileFailed.Reserve(EXTREFNUM);
	FileNotFound.Reserve(EXTREFNUM);
	FileProceed.Reserve(EXTREFNUM);

	while (!FileToRead.IsEmpty())
	{
		// check if the file needs to be processed
		FString CurrentFile;
		GetNewFileToProcess(CurrentFile);

		if (FileProceed.Find(*CurrentFile))
		{
			continue;
		}

		FString FullPath = FPaths::Combine(FilePath, CurrentFile);
		if (!IFileManager::Get().FileExists(*FullPath))
		{
			// File does not exist...
			FileNotFound.Emplace(CurrentFile);
			continue;
		}

		FFileStatData FileStatData = IFileManager::Get().GetStatData(*FullPath);
		uint32 FileHash = GetFileHash(CurrentFile, FileStatData);
		FString SceneGraphFileName = FString::Printf(TEXT("UEx%08x"), FileHash);
		LinkCTFileToUnrealSceneGraphFile(CurrentFile, SceneGraphFileName);

		FString RawDataFile = FPaths::Combine(CachePath, TEXT("scene"), SceneGraphFileName + TEXT(".sg"));
		FString CTFile = FPaths::Combine(CachePath, TEXT("cad"), SceneGraphFileName + TEXT(".ct"));

		uint32 GeomFileHash = GetGeomFileHash(FileHash, CurrentSession->GetImportParameters());
		FString GeomFileName = FString::Printf(TEXT("UEx%08x"), GeomFileHash);
		LinkCTFileToUnrealGeomFile(CurrentFile, GeomFileName);

		FString RawDataGeom = FPaths::Combine(CachePath, TEXT("mesh"), GeomFileName + TEXT(".gm"));

		bool bNeedToProceed = true;
		if (IFileManager::Get().FileExists(*RawDataFile))
		{
			if (!IFileManager::Get().FileExists(*CTFile)) // the file is scene graph only because no CT file
			{
				bNeedToProceed = false;
			}
			else if (IFileManager::Get().FileExists(*RawDataGeom)) // the file has been proceed with same meshing parameters
			{
				bNeedToProceed = false;
			}
			else // the file has been converted into CT file but meshed with different parameters
			{
				FullPath = CTFile;
			}
		}

		bNeedToProceed = true;

		if (!bNeedToProceed)
		{
			// The file has been yet proceed, get ExternalRef
			GetRawDataFileExternalRef(RawDataFile);
			continue;
		}

		// Process the file
		FileProceed.Emplace(CurrentFile);

		FCoreTechFileParser FileParser(CurrentFile, FullPath, SceneGraphFileName, RawDataGeom, CachePath, CurrentSession->GetImportParameters());
		if (!FileParser.ReadFile())
		{
			FileFailed.Emplace(CurrentFile);
		} 
		else
		{
			TSet<FString>& ExternalRefSet = FileParser.GetExternalRefSet();
			if (ExternalRefSet.Num() > 0)
			{
				for (auto ExternalFile : ExternalRefSet)
				{
					AddFileToProcess(ExternalFile);
				}
			}

			FileLoaded.Emplace(CurrentFile);
		}
	}
	return true;
}

void FCoreTechParserMT::GetRawDataFileExternalRef(const FString& InRawDataFile)
{
	TArray<FString> SGDescription;
	FFileHelper::LoadFileToStringArray(SGDescription, *InRawDataFile);
	
	if (SGDescription.Num() < 10)
		return;

	TArray<FString> HeaderSplit;
	FString& HeaderExternalRef = SGDescription[EXTERNALREFLINE];
	HeaderExternalRef.ParseIntoArray(HeaderSplit, TEXT(" "));

	if (HeaderSplit.Num() != 3)
	{
		return;
	}

	int32 StartRef = FCString::Atoi(*HeaderSplit[1]);
	int32 EndRef = StartRef + FCString::Atoi(*HeaderSplit[2]);
	for (int32 Ref = StartRef; Ref < EndRef; ++Ref)
	{
		AddFileToProcess(SGDescription[Ref]);
	}
}

void FCoreTechFileParser::ExportFileSceneGraph()
{
	FFileHelper::SaveStringArrayToFile(SceneGraphDescription, *FPaths::Combine(CachePath, TEXT("scene"), OutSgFile + TEXT(".sg")));
}

uint32 FCoreTechFileParser::GetMaterialNum()
{
	CT_UINT32 IColor = 1;
	while (true)
	{
		CT_COLOR CtColor;
		if (CT_MATERIAL_IO::AskIndexedColor(IColor, CtColor) != IO_OK)
		{
			break;
		}
		IColor++;
	}

	CT_UINT32 IMaterial = 1;
	while (true)
	{
		CT_COLOR Diffuse, Ambient, Specular;
		CT_FLOAT Shininess, Transparency, Reflexion;
		CT_STR Name("");
		CT_TEXTURE_ID TextureId;

		if (CT_MATERIAL_IO::AskParameters(IMaterial, Name, Diffuse, Ambient, Specular, Shininess, Transparency, Reflexion, TextureId) != IO_OK)
		{
			break;
		}
		IMaterial++;
	}

	return IColor + IMaterial - 2;
}

void FCoreTechFileParser::ReadColor()
{
	CT_COLOR CtColor;
	CT_UINT32 IColor = 1;

	SceneGraphDescription[COLORSETLINE] = FString::Printf(TEXT("Color %d"), SceneGraphDescription.Num());
	while (true)
	{
		if (CT_MATERIAL_IO::AskIndexedColor(IColor, CtColor) != IO_OK)
		{
			break;
		}
		SceneGraphDescription.Add(FString::Printf(TEXT("%d %d %d %d"), IColor, CtColor[0], CtColor[1], CtColor[2]));
		IColor++;
	}
	SceneGraphDescription.Add(TEXT(""));
}

void FCoreTechFileParser::ReadMaterial()
{
	CT_UINT32 MaterialId = 1;

	SceneGraphDescription[MATERIALSETLINE] = FString::Printf(TEXT("Material %d"), SceneGraphDescription.Num());
	while (true)
	{
		CT_COLOR Diffuse, Ambient, Specular;
		CT_FLOAT Shininess, Transparency, Reflexion;
		CT_STR Name("");
		CT_TEXTURE_ID TextureId;

		if (CT_MATERIAL_IO::AskParameters(MaterialId, Name, Diffuse, Ambient, Specular, Shininess, Transparency, Reflexion, TextureId) != IO_OK)
		{ 
			break;
		}

		CT_STR TextureName;
		CT_INT32  width = 0, height = 0;
		FString TexturePath;
		if (TextureId)
		{
			if (CT_TEXTURE_IO::AskParameters(TextureId, TextureName, width, height) == IO_OK && width && height)
			{
				TexturePath = FPaths::Combine(CachePath, TextureName.toUnicode());
				TexturePath += TEXT(".png");
				if (CT_TEXTURE_IO::SaveTexture(TextureId, *TexturePath, "PNG") != IO_OK)
				{
					TextureId = 0;
				}
			}
		}

		int32 MaterialHash = BuildMaterialHash(Name, Diffuse, Ambient, Specular, Shininess, Transparency, Reflexion, TextureName);
		SceneGraphDescription.Add(FString::Printf(TEXT("%d %s"), MaterialId, Name.toUnicode()));        // COLORSETLINE 3
		SceneGraphDescription.Add(FString::Printf(TEXT("%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d"), MaterialId, MaterialHash, Diffuse[0], Diffuse[1], Diffuse[2], Ambient[0], Ambient[1], Ambient[2], Specular[0], Specular[1], Specular[2], (int)(Shininess * 255.0), (int)(Transparency * 255.0), (int)(Reflexion * 255.0), TextureId));
		if (TextureId)
		{
			SceneGraphDescription.Add(FString::Printf(TEXT("%d %s"), MaterialId, *FPaths::GetCleanFilename(TexturePath)));
		}
		Material2Partition.LinkMaterialId2MaterialHash(MaterialId, MaterialHash);

		MaterialId++;
	}
	SceneGraphDescription.Add(TEXT(""));
}

FCoreTechFileParser::FCoreTechFileParser(const FString InCADFile, const FString InCTFullPath, const FString InSgFile, const FString InGmFile, const FString InCachePath, CADLibrary::FImportParameters& ImportParams)
	: CADFile(InCADFile)
	, FullPath(InCTFullPath)
	, CachePath(InCachePath)
	, OutSgFile(InSgFile)
	, OutGmFile(InGmFile)
	, bNeedSaveCTFile(false)
	, ImportParameters(ImportParams)
{
	CTIdToRawLineMap.Reserve(EXTREFNUM);
}

bool FCoreTechFileParser::ReadFile()
{
	CheckedCTError Result = IO_OK;
	CT_OBJECT_ID MainId = 0;
	try
	{
		Result = CTKIO_UnloadModel();

		CT_FLAGS CTImportOption = SetCoreTechImportOption(FPaths::GetExtension(CADFile));
		Result = CTKIO_LoadFile(*FullPath, MainId, CTImportOption);
		if (Result == IO_ERROR_EMPTY_ASSEMBLY)
		{
			Result = CTKIO_UnloadModel();
			if (Result != IO_OK)
			{
				return CT_IO_ERROR::IO_ERROR;;
			}
			Result = CTKIO_LoadFile(*FullPath, MainId, CTImportOption);
		}
	}
	catch (...)
	{
		CTKIO_UnloadModel();
		return false;
	}

	if (Result != IO_OK && Result != IO_OK_MISSING_LICENSES)
	{
		CTKIO_UnloadModel();
		return false;
	}

	CADLibrary::Repair(MainId, ImportParameters.StitchingTechnique);
	CADLibrary::SetCoreTechTessellationState(ImportParameters);

	const CT_OBJECT_TYPE TypeSet[] = { CT_INSTANCE_TYPE, CT_ASSEMBLY_TYPE, CT_PART_TYPE, CT_COMPONENT_TYPE, CT_BODY_TYPE, CT_UNLOADED_COMPONENT_TYPE, CT_UNLOADED_ASSEMBLY_TYPE, CT_UNLOADED_PART_TYPE };
	uint32 NbElements[8], NbTotal = 10;
	for (int32 index = 0; index < 8; index++)
	{
		CTKIO_AskNbObjectsType(NbElements[index], TypeSet[index]);
		NbTotal += NbElements[index];
	}

	TArray<uint8> RawData;
	RawData.Append((uint8*) &NbElements[4], sizeof(uint32));
	FFileHelper::SaveArrayToFile(RawData, *OutGmFile, &IFileManager::Get(), FILEWRITE_Append);

	SceneGraphDescription.Reset(NbTotal * 20);
	ExternalRefSet.Empty(NbTotal);
	CTIdToRawLineMap.Reset();

	// Header
	SceneGraphDescription.Empty(SGSIZE);
	SceneGraphDescription.Add(CADFile);  // line 0: File name
	SceneGraphDescription.Add(FullPath);  // line 1: File path
	SceneGraphDescription.Add(TEXT(""));  // Line 2 is reserved
	SceneGraphDescription.Add(TEXT("Color 0"));        // COLORSETLINE 3
	SceneGraphDescription.Add(TEXT("Material 0"));     // MATERIALSETLINE 4
	SceneGraphDescription.Add(TEXT(""));
	SceneGraphDescription.Add(TEXT(""));
	SceneGraphDescription.Add(TEXT("ExternalRef 0"));  // EXTERNALREFLINE 7
	SceneGraphDescription.Add(TEXT("MapCTId 0"));      // MAPCTIDLINE 8
	SceneGraphDescription.Add(TEXT(""));

	uint32 MaterialNum = GetMaterialNum();
	Material2Partition.Empty(MaterialNum);

	ReadColor();
	ReadMaterial();

	// Parse the file
	bool Ret = ReadNode(MainId);
	// End of parsing

	if (bNeedSaveCTFile)
	{
		CT_LIST_IO ObjectList;
		ObjectList.PushBack(MainId);
		CTKIO_SaveFile(ObjectList, *FPaths::Combine(CachePath, TEXT("cad"), OutSgFile+TEXT(".ct")), L"Ct");
	}

	CTKIO_UnloadModel();

	if (!Ret)
	{
		return false;
	}

	uint32 LastLine = SceneGraphDescription.Num();
	if (ExternalRefSet.Num() > 0)
	{
		SceneGraphDescription[EXTERNALREFLINE] = FString::Printf(TEXT("ExternalRef %d %d"), LastLine, ExternalRefSet.Num());
		for (auto ExternalFile : ExternalRefSet)
		{
			SceneGraphDescription.Add(ExternalFile);
		}
	}
	SceneGraphDescription.Add("");

	LastLine = SceneGraphDescription.Num();
	SceneGraphDescription[MAPCTIDLINE] = FString::Printf(TEXT("MapCTId %d"), LastLine);

	FString MapCTId;
	uint32 SizeId = (uint32) log(CTIdToRawLineMap.Num());
	uint32 SizeLine = (uint32)log(LastLine);
	uint32 NbChar = (SizeId > SizeLine ? SizeId : SizeLine) +1;
	MapCTId.Reserve(CTIdToRawLineMap.Num() * NbChar * 2);

	for (auto ID : CTIdToRawLineMap)
	{
		MapCTId.AppendInt(ID.Key);
		MapCTId.Append(TEXT(" "));
		MapCTId.AppendInt(ID.Value);
		MapCTId.Append(TEXT(" "));
	}
	SceneGraphDescription.Add(MapCTId);
	SceneGraphDescription.Add(TEXT(""));
	SceneGraphDescription.Add(TEXT("F"));
	
	ExportFileSceneGraph();

	return true;
}

void FCoreTechParserMT::AddFileToProcess(const FString& File)
{
	if (!FileProceed.Find(File))
	{
		FileToRead.Enqueue(File);
		FileToReadSet.Add(File);
	}
}

void FCoreTechParserMT::LinkCTFileToUnrealSceneGraphFile(const FString& CTFile, const FString& UnrealFile)
{
	CADFileToUnrealFileMap.Add(CTFile, UnrealFile);
}

void FCoreTechParserMT::LinkCTFileToUnrealGeomFile(const FString& CTFile, const FString& UnrealFile)
{
	CADFileToUnrealGeomMap.Add(CTFile, UnrealFile);
}

void FCoreTechParserMT::GetNewFileToProcess(FString& OutFile)
{
	FileToRead.Dequeue(OutFile);
}

void FCoreTechParserMT::UnloadScene()
{
	CTKIO_UnloadModel();
}

void FCoreTechParserMT::SetTessellationOptions(const FDatasmithTessellationOptions& Options)
{
	TessellationOptionsHash = Options.GetHash();
	CurrentSession->SetImportParameters(Options.ChordTolerance, Options.MaxEdgeLength, Options.NormalTolerance, (EStitchingTechnique)Options.StitchingTechnique);
}

CT_FLAGS FCoreTechFileParser::SetCoreTechImportOption(const FString& MainFileExt)
{
	// Set import option
	CT_FLAGS Flags = CT_LOAD_FLAGS_USE_DEFAULT;

	if (MainFileExt == TEXT("jt"))
	{
		Flags |= CT_LOAD_FLAGS_READ_META_DATA;
	}

	if (MainFileExt == TEXT("catpart") || MainFileExt == TEXT("catproduct") || MainFileExt == TEXT("cgr"))
	{
		Flags |= CT_LOAD_FLAGS_V5_READ_GEOM_SET;
	}

	// All the BRep topology is not available in IGES import
	// Ask Kernel IO to complete or create missing topology
	if (MainFileExt == TEXT(".igs") || MainFileExt == TEXT("iges"))
	{
		Flags |= CT_LOAD_FLAG_SEARCH_NEW_TOPOLOGY | CT_LOAD_FLAG_COMPLETE_TOPOLOGY;
	}

	Flags |= CT_LOAD_FLAGS_V5_READ_GEOM_SET;
	
	Flags &= ~CT_LOAD_FLAGS_LOAD_EXTERNAL_REF;

	return Flags;
}

bool FCoreTechFileParser::ReadNode(CT_OBJECT_ID NodeId)
{
	CT_OBJECT_TYPE Type;
	CT_OBJECT_IO::AskType(NodeId, Type);

	uint32* LineNumber = CTIdToRawLineMap.Find(NodeId);
	if (LineNumber != nullptr) 
	{
		return true;
	}

	switch (Type)
	{
	case CT_INSTANCE_TYPE:
		return ReadInstance(NodeId);

	case CT_ASSEMBLY_TYPE:
	case CT_PART_TYPE:
	case CT_COMPONENT_TYPE:
		return ReadComponent(NodeId);

	case CT_UNLOADED_ASSEMBLY_TYPE:
	case CT_UNLOADED_COMPONENT_TYPE:
	case CT_UNLOADED_PART_TYPE:
		return ReadUnloadedComponent(NodeId);

	case CT_BODY_TYPE:
		return ReadBody(NodeId);

		//Treat all CT_CURVE_TYPE :
	case CT_CURVE_TYPE:
	case CT_C_NURBS_TYPE:
	case CT_CONICAL_TYPE:
	case CT_ELLIPSE_TYPE:
	case CT_CIRCLE_TYPE:
	case CT_PARABOLA_TYPE:
	case CT_HYPERBOLA_TYPE:
	case CT_LINE_TYPE:
	case CT_C_COMPO_TYPE:
	case CT_POLYLINE_TYPE:
	case CT_EQUATION_CURVE_TYPE:
	case CT_CURVE_ON_SURFACE_TYPE:
	case CT_INTERSECTION_CURVE_TYPE:
	default:
		break;
	}
	return true;
}

bool FCoreTechFileParser::ReadUnloadedComponent(CT_OBJECT_ID ComponentId)
{
	CTIdToRawLineMap.Add(ComponentId, SceneGraphDescription.Num());

	FString Data = TEXT("U ") + FString::FromInt(ComponentId);
	SceneGraphDescription.Add(*Data);

	ReadNodeMetaDatas(ComponentId);

	CT_STR Filename, FileType;
	CT_COMPONENT_IO::AskExternalDefinition(ComponentId, Filename, FileType);
	Data = TEXT("ext ");
	Data += Filename.toUnicode();
	Data += TEXT(" ");
	Data += FileType.toUnicode();
	SceneGraphDescription.Add(*Data);

	SceneGraphDescription.Add(TEXT(""));

	return true;
}

bool FCoreTechFileParser::ReadComponent(CT_OBJECT_ID ComponentId)
{
	CTIdToRawLineMap.Add(ComponentId, SceneGraphDescription.Num());

	FString Data = TEXT("C ") + FString::FromInt(ComponentId);
	SceneGraphDescription.Add(*Data);

	ReadNodeMetaDatas(ComponentId);

	CT_LIST_IO Children;
	bool error = CT_COMPONENT_IO::AskChildren(ComponentId, Children);

	// Parse children
	Data = TEXT("children ") + FString::FromInt(Children.Count());
	SceneGraphDescription.Add(*Data);

	Children.IteratorInitialize();
	CT_OBJECT_ID CurrentObjectId;
	while ((CurrentObjectId = Children.IteratorIter()) != 0)
	{
		SceneGraphDescription.Add(*FString::FromInt(CurrentObjectId));
	}
	SceneGraphDescription.Add(TEXT(""));

	Children.IteratorInitialize();
	while ((CurrentObjectId = Children.IteratorIter()) != 0)
	{
		ReadNode(CurrentObjectId);
	}

	return true;
}

bool FCoreTechFileParser::ReadInstance(CT_OBJECT_ID InstanceNodeId)
{
	CTIdToRawLineMap.Add(InstanceNodeId, SceneGraphDescription.Num());

	FString Data = TEXT("I ") + FString::FromInt(InstanceNodeId);
	SceneGraphDescription.Add(*Data);

	ReadNodeMetaDatas(InstanceNodeId);

	// Ask the transformation of the instance
	double CTMatrix[16];
	CT_INSTANCE_IO::AskTransformation(InstanceNodeId, CTMatrix);
	SceneGraphDescription.Add(FString::Printf(TEXT("matrix %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f"), CTMatrix[0], CTMatrix[1], CTMatrix[2], CTMatrix[3], CTMatrix[4], CTMatrix[5], CTMatrix[6], CTMatrix[7], CTMatrix[8], CTMatrix[9], CTMatrix[10], CTMatrix[11], CTMatrix[12], CTMatrix[13], CTMatrix[14], CTMatrix[15]));

	// Ask the reference
	CT_OBJECT_ID ReferenceNodeId;
	CT_IO_ERROR CTReturn = CT_INSTANCE_IO::AskChild(InstanceNodeId, ReferenceNodeId);
	if (CTReturn != CT_IO_ERROR::IO_OK)
		return false;

	CT_OBJECT_TYPE type;
	CT_OBJECT_IO::AskType(ReferenceNodeId, type);
	if (type == CT_UNLOADED_PART_TYPE || type == CT_UNLOADED_COMPONENT_TYPE || type == CT_UNLOADED_ASSEMBLY_TYPE)
	{
		CT_STR componentFile, fileType;
		CT_COMPONENT_IO::AskExternalDefinition(ReferenceNodeId, componentFile, fileType);

		FString ExternalRef = FPaths::GetCleanFilename(componentFile.toUnicode());
		Data = TEXT("ext ") + FString::FromInt(ReferenceNodeId) + TEXT(" ") + ExternalRef;
		SceneGraphDescription.Add(*Data);

		ExternalRefSet.Add(ExternalRef);
	}
	else
	{
		Data = TEXT("ref ") + FString::FromInt(ReferenceNodeId);
		SceneGraphDescription.Add(Data);
	}
	SceneGraphDescription.Add(TEXT(""));

	return ReadNode(ReferenceNodeId);
}

uint32 GetStaticMeshUuid(const TCHAR* OutSgFile, const int32 BodyId)
{
	uint32 BodyUUID = GetTypeHash(OutSgFile);
	FString BodyStr = TEXT("B ") + FString::FromInt(BodyId);
	BodyUUID = HashCombine(BodyUUID, GetTypeHash(*BodyStr));
	return BodyUUID;
}

bool FCoreTechFileParser::ReadBody(CT_OBJECT_ID BodyId)
{
	CTIdToRawLineMap.Add(BodyId, SceneGraphDescription.Num());

	bNeedSaveCTFile = true;

	FString Data = TEXT("B ") + FString::FromInt(BodyId);
	SceneGraphDescription.Add(*Data);
	
	ReadNodeMetaDatas(BodyId);

	// Parse Body to set Get Mesh
	TArray<CT_OBJECT_ID> BodySet;
	BodySet.Add(BodyId);
	TArray<FCTTessellation> FaceTessellationSet;

	uint32 BodyUuId = GetStaticMeshUuid(*OutSgFile, BodyId);
	uint32 NbTriangles = GetBodiesTessellations(BodySet, FaceTessellationSet, Material2Partition);
	for (auto Tessellation : FaceTessellationSet)
	{
		Tessellation.BodyUuId = BodyUuId;
		WriteTessellationInFile(Tessellation, OutGmFile);
	}

	const TMap<uint32, uint32>& MaterialIdToHashSetMap = Material2Partition.GetMaterialIdToHashSet();

	FString MapCTId;
	MapCTId.Reserve((MaterialIdToHashSetMap.Num()+1) * 22);

	MapCTId.Append(TEXT("materialMap "));
	for (auto Material : MaterialIdToHashSetMap)
	{
		MapCTId.Append(FString::Printf(TEXT("%u %u "), Material.Key, Material.Value));
	}
	SceneGraphDescription.Add(MapCTId);
	SceneGraphDescription.Add(TEXT(""));

	return true;
}

void FCoreTechFileParser::GetAttributeValue(CT_ATTRIB_TYPE AttributType, int IthField, FString& Value)
{
	CT_STR               FieldName;
	CT_ATTRIB_FIELD_TYPE FieldType;

	Value = "";

	if (CT_ATTRIB_DEFINITION_IO::AskFieldDefinition(AttributType, IthField, FieldType, FieldName) != IO_OK) return;

	switch (FieldType) {
	case CT_ATTRIB_FIELD_UNKNOWN:
	{
		break;
	}
	case CT_ATTRIB_FIELD_INTEGER:
	{
		int IValue;
		if (CT_CURRENT_ATTRIB_IO::AskIntField(IthField, IValue) != IO_OK) break;
		Value = FString::FromInt(IValue);
		break;
	}
	case CT_ATTRIB_FIELD_DOUBLE:
	{
		double DValue;
		if (CT_CURRENT_ATTRIB_IO::AskDblField(IthField, DValue) != IO_OK) break;
		Value = FString::Printf(TEXT("%lf"), DValue);
		break;
	}
	case CT_ATTRIB_FIELD_STRING:
	{
		CT_STR StrValue;
		if (CT_CURRENT_ATTRIB_IO::AskStrField(IthField, StrValue) != IO_OK) break;
		Value = StrValue.toUnicode();
		break;
	}
	case CT_ATTRIB_FIELD_POINTER:
	{
		break;
	}
	}
}

void FCoreTechFileParser::ReadNodeMetaDatas(CT_OBJECT_ID NodeId)
{
	FString AttributeNameLigne = TEXT("M");
	SceneGraphDescription.Add(AttributeNameLigne);
	int32 AttributIndex = SceneGraphDescription.Num();

	if (CT_COMPONENT_IO::IsA(NodeId, CT_COMPONENT_TYPE))
	{
		CT_STR FileName, FileType;
		CT_COMPONENT_IO::AskExternalDefinition(NodeId, FileName, FileType);
		if (!FileName.IsEmpty())
		{
			SceneGraphDescription.Add(TEXT("ExternalDefinition"));
			SceneGraphDescription.Add(FileName.toUnicode());
		}
	}

	CT_UINT32 NbAttributes;
	CT_OBJECT_IO::AskNbAttributes(NodeId, CT_ATTRIB_ALL, NbAttributes);
	CT_UINT32 ith_attrib = 0;

	CT_SHOW_ATTRIBUTE IsShow = CT_UNKNOWN;
	if (CT_OBJECT_IO::AskShowAttribute(NodeId, IsShow) == IO_OK)
	{
		switch (IsShow)
		{
		case CT_SHOW:
			SceneGraphDescription.Add(TEXT("ShowAttribute"));
			SceneGraphDescription.Add(TEXT("show"));
			break;
		case CT_NOSHOW:
			SceneGraphDescription.Add(TEXT("ShowAttribute"));
			SceneGraphDescription.Add(TEXT("noShow"));
			break;
		case CT_UNKNOWN:
			SceneGraphDescription.Add(TEXT("ShowAttribute"));
			SceneGraphDescription.Add(TEXT("unknown"));
			break;
		}
	}

	while (CT_OBJECT_IO::SearchAttribute(NodeId, CT_ATTRIB_ALL, ith_attrib++) == IO_OK)
	{
		// Get the current attribute type
		CT_ATTRIB_TYPE       AttributeType;
		CT_STR               TypeName;

		CT_STR               FieldName;
		CT_STR               FieldStrValue;
		CT_INT32             FieldIntValue;
		CT_DOUBLE            FieldDoubleValue0, FieldDoubleValue1, FieldDoubleValue2;
		FString              FieldValue;


		if (CT_CURRENT_ATTRIB_IO::AskAttributeType(AttributeType) != IO_OK) continue;;
		switch (AttributeType) {

		case CT_ATTRIB_SPLT:
			//if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_SPLT_VALUE, field_strValue) != IO_OK) break;
			//nodeAttributeSet[TEXT("SPLT")] = field_strValue.toUnicode();
			break;

		case CT_ATTRIB_NAME:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_NAME_VALUE, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) { break; }
			SceneGraphDescription.Add(TEXT("CTName"));
			SceneGraphDescription.Add(FieldStrValue.toUnicode());
			break;

		case CT_ATTRIB_ORIGINAL_NAME:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_NAME_VALUE, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) { break; }
			SceneGraphDescription.Add(TEXT("Name"));
			SceneGraphDescription.Add(FieldStrValue.toUnicode());
			break;

		case CT_ATTRIB_ORIGINAL_FILENAME:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_FILENAME_VALUE, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) { break; }
			SceneGraphDescription.Add(TEXT("FileName"));
			SceneGraphDescription.Add(FieldStrValue.toUnicode());
			break;

		case CT_ATTRIB_UUID:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_UUID_VALUE, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) { break; }
			SceneGraphDescription.Add(TEXT("UUID"));
			SceneGraphDescription.Add(FieldStrValue.toUnicode());
			break;

		case CT_ATTRIB_INPUT_FORMAT_AND_EMETTOR:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_INPUT_FORMAT_AND_EMETTOR, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) { break; }
			SceneGraphDescription.Add(TEXT("Input_Format_and_Emitter"));
			SceneGraphDescription.Add(FieldStrValue.toUnicode());
			break;

		case CT_ATTRIB_CONFIGURATION_NAME:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_NAME_VALUE, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) { break; }
			SceneGraphDescription.Add(TEXT("ConfigurationName"));
			SceneGraphDescription.Add(FieldStrValue.toUnicode());
			break;

		case CT_ATTRIB_LAYERID:
			GetAttributeValue(AttributeType, ITH_LAYERID_VALUE, FieldValue);
			SceneGraphDescription.Add(TEXT("LayerId"));
			SceneGraphDescription.Add(FieldValue);
			GetAttributeValue(AttributeType, ITH_LAYERID_NAME, FieldValue);
			SceneGraphDescription.Add(TEXT("LayerName"));
			SceneGraphDescription.Add(FieldValue);
			GetAttributeValue(AttributeType, ITH_LAYERID_FLAG, FieldValue);
			SceneGraphDescription.Add(TEXT("LayerFlag"));
			SceneGraphDescription.Add(FieldValue);
			break;

		case CT_ATTRIB_COLORID:
			{
				if (CT_CURRENT_ATTRIB_IO::AskIntField(ITH_COLORID_VALUE, FieldIntValue) != IO_OK) break;
				uint32 ColorId = FieldIntValue;
				uint8 Alpha = 255;
				SceneGraphDescription.Add(TEXT("ColorId"));
				SceneGraphDescription.Add(FString::FromInt(FieldIntValue));

				CT_COLOR CtColor;
				if (CT_MATERIAL_IO::AskIndexedColor(FieldIntValue, CtColor) != IO_OK) break;

				if (CT_OBJECT_IO::SearchAttribute(NodeId, CT_ATTRIB_TRANSPARENCY) == IO_OK)
				{
					if (CT_CURRENT_ATTRIB_IO::AskDblField(0, FieldDoubleValue0) == IO_OK)
					{
						Alpha = FMath::Max((1. - FieldDoubleValue0), FieldDoubleValue0) * 255.;
					}
				}
				FString colorHexa = FString::Printf(TEXT("%02x%02x%02x%02x"), CtColor[0], CtColor[1], CtColor[2], Alpha);
				SceneGraphDescription.Add(TEXT("ColorValue"));
				SceneGraphDescription.Add(colorHexa);

				SceneGraphDescription.Add(TEXT("ColorUEId"));
				uint32 ColorUuId = BuildFastColorHash(ColorId, Alpha);
				SceneGraphDescription.Add(FString::Printf(TEXT("%u"), ColorUuId));

				int32 ColorHash = BuildColorHash(CtColor, Alpha);
				Material2Partition.LinkMaterialId2MaterialHash(ColorUuId, ColorHash);
			}
			break;

		case CT_ATTRIB_MATERIALID:
			if (CT_CURRENT_ATTRIB_IO::AskIntField(ITH_MATERIALID_VALUE, FieldIntValue) != IO_OK) break;
			SceneGraphDescription.Add(TEXT("MaterialId"));
			SceneGraphDescription.Add(FString::FromInt(FieldIntValue));
			break;

		case CT_ATTRIB_TRANSPARENCY:
			if (CT_CURRENT_ATTRIB_IO::AskDblField(ITH_TRANSPARENCY_VALUE, FieldDoubleValue0) != IO_OK) break;
			FieldIntValue = FMath::Max((1. - FieldDoubleValue0), FieldDoubleValue0) * 255.;
			SceneGraphDescription.Add(TEXT("Transparency"));
			SceneGraphDescription.Add(FString::FromInt(FieldIntValue));
			break;

		case CT_ATTRIB_COMMENT:
			//ITH_COMMENT_POSX, ITH_COMMENT_POSY, ITH_COMMENT_POSZ, ITH_COMMENT_TEXT
			break;

		case CT_ATTRIB_REFCOUNT:
			if (CT_CURRENT_ATTRIB_IO::AskIntField(ITH_REFCOUNT_VALUE, FieldIntValue) != IO_OK) break;
			//NodeAttributeSet.Add(TEXT("RefCount"));
			SceneGraphDescription.Add(FString::FromInt(FieldIntValue));
			break;

		case CT_ATTRIB_TESS_PARAMS:
			//ITH_TESS_PARAMS_MAXSAG, ITH_TESS_PARAMS_MAXANGLE, ITH_TESS_PARAMS_MAXLENGTH
			break;

		case CT_ATTRIB_COMPARE_RESULT:
			//ITH_COMPARE_TYPE, ITH_COMPARE_RESULT_MAXDIST, ITH_COMPARE_RESULT_MINDIST, ITH_COMPARE_THICKNESS, ITH_COMPARE_BACKLASH, ITH_COMPARE_CLEARANCE, ITH_COMPARE_MAXTHICKNESS, ITH_COMPARE_RESULT_NEAREST, ITH_COMPARE_RESULT_ORIGIN1_X, ITH_COMPARE_RESULT_ORIGIN1_Y, ITH_COMPARE_RESULT_ORIGIN1_Z, ITH_COMPARE_RESULT_DIRECTION1_X, ITH_COMPARE_RESULT_DIRECTION1_Y, ITH_COMPARE_RESULT_DIRECTION1_Z, ITH_COMPARE_RESULT_RADIUS1_1, ITH_COMPARE_RESULT_RADIUS2_1, ITH_COMPARE_RESULT_HALFHANGLE1, ITH_COMPARE_RESULT_ORIGIN2_X, ITH_COMPARE_RESULT_ORIGIN2_Y, ITH_COMPARE_RESULT_ORIGIN2_Z, ITH_COMPARE_RESULT_DIRECTION2_X, ITH_COMPARE_RESULT_DIRECTION2_Y, ITH_COMPARE_RESULT_DIRECTION2_Z, ITH_COMPARE_RESULT_RADIUS1_2, ITH_COMPARE_RESULT_RADIUS2_2, ITH_COMPARE_RESULT_HALFHANGLE2, ITH_COMPARE_RESULT_COLOR1, ITH_COMPARE_RESULT_COLOR2, ITH_COMPARE_DIST_VECTOR1_X, ITH_COMPARE_DIST_VECTOR1_Y, ITH_COMPARE_DIST_VECTOR1_Z, ITH_COMPARE_DIST_VECTOR2_X, ITH_COMPARE_DIST_VECTOR2_Y, ITH_COMPARE_DIST_VECTOR2_Z
			break;
		case CT_ATTRIB_DENSITY:
			//ITH_VOLUME_DENSITY_VALUE
			break;

		case CT_ATTRIB_MASS_PROPERTIES:
			if (CT_CURRENT_ATTRIB_IO::AskDblField(ITH_MASS_PROPERTIES_AREA, FieldDoubleValue0) != IO_OK) break;
			SceneGraphDescription.Add(TEXT("Area"));
			SceneGraphDescription.Add(FString::Printf(TEXT("%lf"), FieldDoubleValue0));
			if (CT_CURRENT_ATTRIB_IO::AskDblField(ITH_MASS_PROPERTIES_VOLUME, FieldDoubleValue0) != IO_OK) break;
			SceneGraphDescription.Add(TEXT("Volume"));
			SceneGraphDescription.Add(FString::Printf(TEXT("%lf"), FieldDoubleValue0));
			if (CT_CURRENT_ATTRIB_IO::AskDblField(ITH_MASS_PROPERTIES_MASS, FieldDoubleValue0) != IO_OK) break;
			SceneGraphDescription.Add(TEXT("Mass"));
			SceneGraphDescription.Add(FString::Printf(TEXT("%lf"), FieldDoubleValue0));
			if (CT_CURRENT_ATTRIB_IO::AskDblField(ITH_MASS_PROPERTIES_LENGTH, FieldDoubleValue0) != IO_OK) break;
			SceneGraphDescription.Add(TEXT("Length"));
			SceneGraphDescription.Add(FString::Printf(TEXT("%lf"), FieldDoubleValue0));
			//ITH_MASS_PROPERTIES_COGX, ITH_MASS_PROPERTIES_COGY, ITH_MASS_PROPERTIES_COGZ
			//ITH_MASS_PROPERTIES_M1, ITH_MASS_PROPERTIES_M2, ITH_MASS_PROPERTIES_M3
			//ITH_MASS_PROPERTIES_IXXG,ITH_MASS_PROPERTIES_IYYG, ITH_MASS_PROPERTIES_IZZG, ITH_MASS_PROPERTIES_IXYG, ITH_MASS_PROPERTIES_IYZG, ITH_MASS_PROPERTIES_IZXG
			//ITH_MASS_PROPERTIES_AXIS1X, ITH_MASS_PROPERTIES_AXIS1Y, ITH_MASS_PROPERTIES_AXIS1Z, ITH_MASS_PROPERTIES_AXIS2X, ITH_MASS_PROPERTIES_AXIS2Y, ITH_MASS_PROPERTIES_AXIS2Z, ITH_MASS_PROPERTIES_AXIS3X, ITH_MASS_PROPERTIES_AXIS3Y, ITH_MASS_PROPERTIES_AXIS3Z
			//ITH_MASS_PROPERTIES_XMIN, ITH_MASS_PROPERTIES_YMIN, ITH_MASS_PROPERTIES_ZMIN, ITH_MASS_PROPERTIES_XMAX, ITH_MASS_PROPERTIES_YMAX, ITH_MASS_PROPERTIES_ZMAX
			break;

		case CT_ATTRIB_THICKNESS:
			//ITH_THICKNESS_VALUE
			break;

		case CT_ATTRIB_INTEGER_METADATA:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_INTEGER_METADATA_NAME, FieldName) != IO_OK) break;
			if (CT_CURRENT_ATTRIB_IO::AskIntField(ITH_INTEGER_METADATA_VALUE, FieldIntValue) != IO_OK) break;
			SceneGraphDescription.Add(FieldName.toUnicode());
			SceneGraphDescription.Add(FString::FromInt(FieldIntValue));
			break;

		case CT_ATTRIB_DOUBLE_METADATA:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_DOUBLE_METADATA_NAME, FieldName) != IO_OK) break;
			if (CT_CURRENT_ATTRIB_IO::AskDblField(ITH_DOUBLE_METADATA_VALUE, FieldDoubleValue0) != IO_OK) break;
			SceneGraphDescription.Add(FieldName.toUnicode());
			SceneGraphDescription.Add(FString::Printf(TEXT("%lf"), FieldDoubleValue0));
			break;

		case CT_ATTRIB_STRING_METADATA:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_STRING_METADATA_NAME, FieldName) != IO_OK) break;
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_STRING_METADATA_VALUE, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) break;
			SceneGraphDescription.Add(FieldName.toUnicode());
			SceneGraphDescription.Add(FieldStrValue.toUnicode());
			break;

		case CT_ATTRIB_ORIGINAL_UNITS:
			if (CT_CURRENT_ATTRIB_IO::AskDblField(ITH_ORIGINAL_UNITS_MASS, FieldDoubleValue0) != IO_OK) break;
			if (CT_CURRENT_ATTRIB_IO::AskDblField(ITH_ORIGINAL_UNITS_LENGTH, FieldDoubleValue1) != IO_OK) break;
			if (CT_CURRENT_ATTRIB_IO::AskDblField(ITH_ORIGINAL_UNITS_DURATION, FieldDoubleValue2) != IO_OK) break;
			SceneGraphDescription.Add(TEXT("OriginalUnitsMass"));
			SceneGraphDescription.Add(FString::Printf(TEXT("%lf"), FieldDoubleValue0));
			SceneGraphDescription.Add(TEXT("OriginalUnitsLength"));
			SceneGraphDescription.Add(FString::Printf(TEXT("%lf"), FieldDoubleValue1));
			SceneGraphDescription.Add(TEXT("OriginalUnitsDuration"));
			SceneGraphDescription.Add(FString::Printf(TEXT("%lf"), FieldDoubleValue2));
			break;

		case CT_ATTRIB_ORIGINAL_TOLERANCE:
			//ITH_ORIGINAL_TOLERANCE_LENGTH, ITH_ORIGINAL_TOLERANCE_MAXCOORD, ITH_ORIGINAL_TOLERANCE_ANGLE
			break;
		case CT_ATTRIB_IGES_PARAMETERS:
			//ITH_IGES_DELIMITOR, ITH_IGES_ENDDELIMITOR, ITH_IGES_EMETTORID, ITH_IGES_CREATION_FILENAME, ITH_IGES_EMETTOR_NAME, ITH_IGES_EMETTOR_VERSION, ITH_IGES_NBOFBITS, ITH_IGES_MAXPOW_SINGLE_PREC, ITH_IGES_NB_DIGITS_SINGLE_PRE, ITH_IGES_MAXPOW_DOUBLE_PREC, ITH_IGES_NB_DIGITS_DOUBLE_PRE, ITH_IGES_RECEPTOR_ID, ITH_IGES_SCALE, ITH_IGES_UNIT, ITH_IGES_UNIT_NAME, ITH_IGES_MAXLINEGRADATIONS, ITH_IGES_MAXLINEWIDTH, ITH_IGES_CREATIONFILEDATE, ITH_IGES_MINIRESOLUTION, ITH_IGES_MAXCOORD, ITH_IGES_AUTHORNAME, ITH_IGES_ORGANIZATION, ITH_IGES_IGESVERSION, ITH_IGES_DRAFTINGSTANDARD, ITH_IGES_MODELDATE, ITH_IGES_APPLICATIONPROTOCOL
			break;
		case CT_ATTRIB_READ_V4_MARKER:
			//ITH_READ_V4_MARKER_VALUE
			break;

		case CT_ATTRIB_PRODUCT:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_PRODUCT_REVISION, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) { break; }
			SceneGraphDescription.Add(TEXT("ProductRevision"));
			SceneGraphDescription.Add(FieldStrValue.toUnicode());
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_PRODUCT_DEFINITION, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) { break; }
			SceneGraphDescription.Add(TEXT("ProductDefinition"));
			SceneGraphDescription.Add(FieldStrValue.toUnicode());
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_PRODUCT_NOMENCLATURE, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) { break; }
			SceneGraphDescription.Add(TEXT("ProductNomenclature"));
			SceneGraphDescription.Add(FieldStrValue.toUnicode());
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_PRODUCT_SOURCE, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) { break; }
			SceneGraphDescription.Add(TEXT("ProductSource"));
			SceneGraphDescription.Add(FieldStrValue.toUnicode());
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_PRODUCT_DESCRIPTION, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) { break; }
			SceneGraphDescription.Add(TEXT("ProductDescription"));
			SceneGraphDescription.Add(FieldStrValue.toUnicode());
			break;

		case CT_ATTRIB_SIMPLIFY:
			//ITH_SIMPLIFY_CURRENT_ID, ITH_SIMPLIFY_CONNECTED_ID
			break;
		case CT_ATTRIB_MIDFACE:
			//ITH_MIDFACE_TYPE, ITH_MIDFACE_THICKNESS1, ITH_MIDFACE_THICKNESS2
			break;
		case CT_ATTRIB_DEBUG_STRING:
			//ITH_DEBUG_STRING_VALUE
			break;
		case CT_ATTRIB_DEFEATURING:
			break;
		case CT_ATTRIB_BREPLINKID:
			//ITH_BREPLINKID_BRANCHID, ITH_BREPLINKID_FACEID
			break;
		case CT_ATTRIB_MARKUPS_REF:
			//ITH_MARKUPS_REF
			break;
		case CT_ATTRIB_COLLISION:
			//ITH_COLLISION_ID, ITH_COLLISION_MATID
			break;
		case CT_ATTRIB_EXTERNAL_ID:
			//ITH_EXTERNAL_ID_VALUE
			break;
		case CT_ATTRIB_MODIFIER:
			//ITH_MODIFIER_TYPE, ITH_MODIFIER_INTVALUE, ITH_MODIFIER_DBLVALUE, ITH_MODIFIER_STRVALUE
			break;
		case CT_ATTRIB_ORIGINAL_SURF_OLD:
			break;
		case CT_ATTRIB_RESULT_BREPLINKID:
			break;
		case CT_ATTRIB_AREA:
			//ITH_AREA_VALUE
			break;
		case CT_ATTRIB_ACIS_SG_PIDNAME:
			//ITH_ACIS_SG_PIDNAME_BASE_NAME, ITH_ACIS_SG_PIDNAME_TIME_VAL, ITH_ACIS_SG_PIDNAME_INDEX, ITH_ACIS_SG_PIDNAME_COPY_NUM
			break;
		case CT_ATTRIB_CURVE_ORIGINAL_BOUNDARY_PARAMS:
			//ITH_CURVE_ORIGINAL_BOUNDARY_PARAMS_START, ITH_CURVE_ORIGINAL_BOUNDARY_PARAMS_END, ITH_CURVE_ORIGINAL_BOUNDARY_PARAMS_SCALE
			break;

		case CT_ATTRIB_INTEGER_PARAMETER:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_INTEGER_PARAMETER_NAME, FieldName) != IO_OK) break;
			if (CT_CURRENT_ATTRIB_IO::AskIntField(ITH_INTEGER_PARAMETER_VALUE, FieldIntValue) != IO_OK) break;
			SceneGraphDescription.Add(FieldName.toUnicode());
			SceneGraphDescription.Add(FString::FromInt(FieldIntValue));
			break;

		case CT_ATTRIB_DOUBLE_PARAMETER:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_DOUBLE_PARAMETER_NAME, FieldName) != IO_OK) break;
			if (CT_CURRENT_ATTRIB_IO::AskDblField(ITH_DOUBLE_PARAMETER_VALUE, FieldDoubleValue0) != IO_OK) break;
			SceneGraphDescription.Add(FieldName.toUnicode());
			SceneGraphDescription.Add(FString::Printf(TEXT("%lf"), FieldDoubleValue0));
			break;

		case CT_ATTRIB_STRING_PARAMETER:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_STRING_PARAMETER_NAME, FieldName) != IO_OK) break;
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_STRING_PARAMETER_VALUE, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) { break; }
			SceneGraphDescription.Add(FieldName.toUnicode());
			SceneGraphDescription.Add(FieldStrValue.toUnicode());
			break;

		case CT_ATTRIB_PARAMETER_ARRAY:
			//ITH_PARAMETER_ARRAY_NAME
			//ITH_PARAMETER_ARRAY_NUMBER
			//ITH_PARAMETER_ARRAY_VALUES
			break;

		case CT_ATTRIB_SAVE_OPTION:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_SAVE_OPTION_AUTHOR, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) { break; }
			//NodeAttributeSet.Add(TEXT("SaveOptionAuthor"));
			SceneGraphDescription.Add(FieldStrValue.toUnicode());
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_SAVE_OPTION_ORGANIZATION, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) { break; }
			//NodeAttributeSet.Add(TEXT("SaveOptionOrganization"));
			SceneGraphDescription.Add(FieldStrValue.toUnicode());
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_SAVE_OPTION_FILE_DESCRIPTION, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) { break; }
			//NodeAttributeSet.Add(TEXT("SaveOptionFileDescription"));
			SceneGraphDescription.Add(FieldStrValue.toUnicode());
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_SAVE_OPTION_AUTHORISATION, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) { break; }
			//NodeAttributeSet.Add(TEXT("SaveOptionAuthorisation"));
			SceneGraphDescription.Add(FieldStrValue.toUnicode());
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_SAVE_OPTION_PREPROCESSOR, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) { break; }
			//NodeAttributeSet.Add(TEXT("SaveOptionPreprocessor"));
			SceneGraphDescription.Add(FieldStrValue.toUnicode());
			break;

		case CT_ATTRIB_ORIGINAL_ID:
			GetAttributeValue(AttributeType, ITH_ORIGINAL_ID_VALUE, FieldValue);
			SceneGraphDescription.Add(TEXT("OriginalId"));
			SceneGraphDescription.Add(FieldValue);
			break;

		case CT_ATTRIB_ORIGINAL_ID_STRING:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_ORIGINAL_ID_VALUE_STRING, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) { break; }
			SceneGraphDescription.Add(TEXT("OriginalIdStr"));
			SceneGraphDescription.Add(FieldStrValue.toUnicode());
			break;

		case CT_ATTRIB_COLOR_RGB_DOUBLE:
			if (CT_CURRENT_ATTRIB_IO::AskDblField(ITH_ATTRIB_COLOR_R_DOUBLE, FieldDoubleValue0) != IO_OK) break;
			if (CT_CURRENT_ATTRIB_IO::AskDblField(ITH_ATTRIB_COLOR_G_DOUBLE, FieldDoubleValue1) != IO_OK) break;
			if (CT_CURRENT_ATTRIB_IO::AskDblField(ITH_ATTRIB_COLOR_B_DOUBLE, FieldDoubleValue2) != IO_OK) break;
			FieldValue = FString::Printf(TEXT("%lf"), FieldDoubleValue0) + TEXT(", ") + FString::Printf(TEXT("%lf"), FieldDoubleValue1) + TEXT(", ") + FString::Printf(TEXT("%lf"), FieldDoubleValue2);
			//OutMetaDataSet.Add(TEXT("ColorRGBDouble"))
			//OutMetaDataSet.Add(FieldValue);
			break;

		case CT_ATTRIB_REVERSE_COLORID:
			break;
		case CT_ATTRIB_INITIAL_FILTER:
			break;
		case CT_ATTRIB_ORIGINAL_SURF:
			break;
		case CT_ATTRIB_LINKMANAGER_BRANCH_FACE:
			//ITH_LINKMANAGER_BRANCH_FACE_PART_ID, ITH_LINKMANAGER_BRANCH_FACE_SOLID_ID, ITH_LINKMANAGER_BRANCH_FACE_BRANCH_ID, ITH_LINKMANAGER_BRANCH_FACE_FACE_ID
			break;
		case CT_ATTRIB_LINKMANAGER_PMI:
			//ITH_LINKMANAGER_PMI_ID, ITH_LINKMANAGER_PMI_SENSE, ITH_LINKMANAGER_PMI_INSTANCE
			break;
		case CT_ATTRIB_NULL:
			//ITH_NULL_TITLE
			break;
		case CT_ATTRIB_MEASURE_VALIDATION_ATTRIBUTE:
			//ITH_MEASURE_VALIDATION_NAME
			//ITH_MEASURE_VALIDATION_UNIT
			//ITH_MEASURE_VALIDATION_VALUE
			break;

		case CT_ATTRIB_INTEGER_VALIDATION_ATTRIBUTE:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_INTEGER_VALIDATION_NAME, FieldName) != IO_OK) break;
			if (CT_CURRENT_ATTRIB_IO::AskIntField(ITH_INTEGER_VALIDATION_VALUE, FieldIntValue) != IO_OK) break;
			SceneGraphDescription.Add(FieldName.toUnicode());
			SceneGraphDescription.Add(FString::FromInt(FieldIntValue));
			break;

		case CT_ATTRIB_DOUBLE_VALIDATION_ATTRIBUTE:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_DOUBLE_VALIDATION_NAME, FieldName) != IO_OK) break;
			if (CT_CURRENT_ATTRIB_IO::AskDblField(ITH_DOUBLE_VALIDATION_VALUE, FieldDoubleValue0) != IO_OK) break;
			SceneGraphDescription.Add(FieldName.toUnicode());
			SceneGraphDescription.Add(FString::Printf(TEXT("%lf"), FieldDoubleValue0));
			break;

		case CT_ATTRIB_STRING_VALIDATION_ATTRIBUTE:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_STRING_VALIDATION_NAME, FieldName) != IO_OK) break;
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_STRING_VALIDATION_VALUE, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) { break; }
			SceneGraphDescription.Add(FieldName.toUnicode());
			SceneGraphDescription.Add(FieldStrValue.toUnicode());
			break;

		case CT_ATTRIB_BOUNDING_BOX:
			//ITH_BOUNDING_BOX_XMIN, ITH_BOUNDING_BOX_YMIN, ITH_BOUNDING_BOX_ZMIN, ITH_BOUNDING_BOX_XMAX, ITH_BOUNDING_BOX_YMAX, ITH_BOUNDING_BOX_ZMAX
			break;
		case CT_ATTRIB_DATABASE:
			//ITH_DATABASE_NAME, ITH_DATABASE_TABLE
			break;
		case CT_ATTRIB_CURVE_FONT:
			//ITH_ATTRIB_CURVE_FONT_VALUE
			break;
		case CT_ATTRIB_CURVE_WEIGHT:
			//ITH_ATTRIB_CURVE_WEIGHT_VALUE
			break;
		case CT_ATTRIB_COMPARE_TOPO:
			break;
		case CT_ATTRIB_MONIKER_GUID_TABLE:
			//ITH_MONIKER_GUID_TABLE_NAME, ITH_MONIKER_GUID_TABLE_GUID, ITH_MONIKER_GUID_TABLE_INDEX, ITH_MONIKER_GUID_TABLE_APPLICATION_NAME
			break;
		case CT_ATTRIB_MONIKER_DATA:
			//ITH_MONIKER_DATA_NAME, ITH_MONIKER_DATA_TABLE_INDEX, ITH_MONIKER_DATA_ENTITY_ID, ITH_MONIKER_DATA_LABEL, ITH_MONIKER_DATA_BODY_GUID
			break;
		case CT_ATTRIB_MONIKER_BODY_ID:
			//ITH_MONIKER_BODY_ID_NAME, ITH_MONIKER_BODY_ID_ENTITY_ID, ITH_MONIKER_BODY_ID_APPLICATION_NAME
			break;
		case CT_ATTRIB_NO_INSTANCE:
			break;

		case CT_ATTRIB_GROUPNAME:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_GROUPNAME_VALUE, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) { break; }
			SceneGraphDescription.Add(TEXT("GroupName"));
			SceneGraphDescription.Add(FieldStrValue.toUnicode());
			break;

		case CT_ATTRIB_ANALYZE_ID:
			//ITH_ANALYZE_OBJECT_ID
			break;
		case CT_ATTRIB_ANALYZER_DISPLAY_MODE:
			//ITH_ANALYZER_DISPLAY_MODE_VALUE
			break;
		case CT_ATTRIB_ANIMATION_ID:
			//ITH_ANIMATION_ID_VALUE
			break;
		case CT_ATTRIB_PROJECTED_SURFACE_ID:
			//ITH_PROJECTED_SURFACE_ID_VALUE
			break;
		case CT_ATTRIB_ANALYZE_LINK:
			//ITH_ANALYZE_LINK_ID
			break;
		case CT_ATTRIB_TOPO_EVENT_ID:
			//ITH_TOPO_EVENT_ID_VALUE
			break;
		case CT_ATTRIB_ADDITIVE_MANUFACTURING:
			//ITH_AM_MODE, ITH_AM_ROUGHNESS_MIN, ITH_AM_SECTION_DIST, ITH_AM_FUNCTIONAL_TYPE, ITH_AM_LATTICE_TYPE
			break;
		case CT_ATTRIB_MOLDING_RESULT:
			//ITH_MOLDING_MODE, ITH_MOLDING_MEASURE_MIN, ITH_MOLDING_MEASURE_MAX
			break;
		case CT_ATTRIB_AMF_ID:
			//ITH_AMF_ID_VALUE, ITH_AMF_TYPE
			break;
		case CT_ATTRIB_PARAMETER_LINK:
			//ITH_PARAMETER_LINK_ID, ITH_PARAMETER_LINK_PARENT_ID, ITH_PARAMETER_LINK_SENSE
			break;
		default:
			break;
		}
	}

	AttributeNameLigne = "M " + FString::FromInt(SceneGraphDescription.Num() - AttributIndex);
	SceneGraphDescription[AttributIndex-1] = *AttributeNameLigne;
}

#endif  // USE_CORETECH_MT_PARSER