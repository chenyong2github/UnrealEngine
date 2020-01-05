// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreTechFileParser.h"

#ifdef CAD_INTERFACE


#include "CADData.h"
#include "CADOptions.h"

#include "CoreTechTypes.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/FileManager.h"
#include "Internationalization/Text.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#define EXTREFNUM 5000

namespace CADLibrary 
{

namespace {
	double Distance(CT_COORDINATE Point1, CT_COORDINATE Point2)
	{
		return sqrt((Point2.xyz[0] - Point1.xyz[0]) * (Point2.xyz[0] - Point1.xyz[0]) + (Point2.xyz[1] - Point1.xyz[1]) * (Point2.xyz[1] - Point1.xyz[1]) + (Point2.xyz[2] - Point1.xyz[2]) * (Point2.xyz[2] - Point1.xyz[2]));
	}

	template<typename UVType>
	void ScaleUV(CT_OBJECT_ID FaceID, void* TexCoordArray, int32 VertexCount, UVType Scale)
	{
		UVType VMin, VMax, UMin, UMax;
		VMin = UMin = HUGE_VALF;
		VMax = UMax = -HUGE_VALF;
		UVType* UVSet = (UVType*) TexCoordArray;
		for (int32 Index = 0, UVCoord = 0; Index < VertexCount; ++Index, UVCoord += 2)
		{
			UMin = FMath::Min(UVSet[UVCoord + 0], UMin);
			UMax = FMath::Max(UVSet[UVCoord + 0], UMax);
			VMin = FMath::Min(UVSet[UVCoord + 1], VMin);
			VMax = FMath::Max(UVSet[UVCoord + 1], VMax);
		}

		double PuMin, PuMax, PvMin, PvMax;
		PuMin = PvMin = HUGE_VALF;
		PuMax = PvMax = -HUGE_VALF;

		// fast UV min max 
		CT_FACE_IO::AskUVminmax(FaceID, PuMin, PuMax, PvMin, PvMax);

		const uint32 NbIsoCurves = 7;

		// Compute Point grid on the restricted surface defined by [PuMin, PuMax], [PvMin, PvMax]
		CT_OBJECT_ID SurfaceID;
		CT_ORIENTATION Orientation;
		CT_FACE_IO::AskSurface(FaceID, SurfaceID, Orientation);

		CT_OBJECT_TYPE SurfaceType;
		CT_SURFACE_IO::AskType(SurfaceID, SurfaceType);

		UVType DeltaU = (PuMax - PuMin) / (NbIsoCurves - 1);
		UVType DeltaV = (PvMax - PvMin) / (NbIsoCurves - 1);
		UVType U = PuMin, V = PvMin;

		CT_COORDINATE NodeMatrix[121];

		for (int32 IndexI = 0; IndexI < NbIsoCurves; IndexI++)
		{
			for (int32 IndexJ = 0; IndexJ < NbIsoCurves; IndexJ++)
			{
				CT_SURFACE_IO::Evaluate(SurfaceID, U, V, NodeMatrix[IndexI*NbIsoCurves + IndexJ]);
				V += DeltaV;
			}
			U += DeltaU;
			V = PvMin;
		}

		// Compute length of 7 iso V line
		UVType LengthU[NbIsoCurves];
		UVType LengthUMin = HUGE_VAL;
		UVType LengthUMax = 0;
		UVType LengthUMed = 0;

		for (int32 IndexJ = 0; IndexJ < NbIsoCurves; IndexJ++)
		{
			LengthU[IndexJ] = 0;
			for (int32 IndexI = 0; IndexI < (NbIsoCurves - 1); IndexI++)
			{
				LengthU[IndexJ] += Distance(NodeMatrix[IndexI * NbIsoCurves + IndexJ], NodeMatrix[(IndexI + 1) * NbIsoCurves + IndexJ]);
			}
			LengthUMed += LengthU[IndexJ];
			LengthUMin = FMath::Min(LengthU[IndexJ], LengthUMin);
			LengthUMax = FMath::Max(LengthU[IndexJ], LengthUMax);
		}
		LengthUMed /= NbIsoCurves;
		LengthUMed = LengthUMed * 2 / 3 + LengthUMax / 3;

		// Compute length of 7 iso U line
		UVType LengthV[NbIsoCurves];
		UVType LengthVMin = HUGE_VAL;
		UVType LengthVMax = 0;
		UVType LengthVMed = 0;

		for (int32 IndexI = 0; IndexI < NbIsoCurves; IndexI++)
		{
			LengthV[IndexI] = 0;
			for (int32 IndexJ = 0; IndexJ < (NbIsoCurves - 1); IndexJ++)
			{
				LengthV[IndexI] += Distance(NodeMatrix[IndexI * NbIsoCurves + IndexJ], NodeMatrix[IndexI * NbIsoCurves + IndexJ + 1]);
			}
			LengthVMed += LengthV[IndexI];
			LengthVMin = FMath::Min(LengthV[IndexI], LengthVMin);
			LengthVMax = FMath::Max(LengthV[IndexI], LengthVMax);
		}
		LengthVMed /= NbIsoCurves;
		LengthVMed = LengthVMed * 2 / 3 + LengthVMax / 3;

		switch (SurfaceType)
		{
		case CT_CONE_TYPE:
		case CT_CYLINDER_TYPE:
		case CT_SPHERE_TYPE:
			Swap(LengthUMed, LengthVMed);
			break;
		case CT_S_REVOL_TYPE:
		case CT_TORUS_TYPE:
			// Need swap ?
			// Swap(LengthUMed, LengthVMed);
			break;
		case CT_S_NURBS_TYPE:
		case CT_PLANE_TYPE:
		case CT_S_OFFSET_TYPE:
		case CT_S_RULED_TYPE:
		case CT_TABULATED_RULED_TYPE:
		case CT_S_LINEARTRANSFO_TYPE:
		case CT_S_NONLINEARTRANSFO_TYPE:
		case CT_S_BLEND_TYPE:
		default:
			break;
		}

		// scale the UV map
		// 0.1 define UV in cm and not in mm
		UVType VScale = Scale * LengthVMed * 1 / (VMax - VMin) / 100;
		UVType UScale = Scale * LengthUMed * 1 / (UMax - UMin) / 100;

		for (int32 Index = 0, UVCoord = 0; Index < VertexCount; ++Index, UVCoord += 2)
		{
			UVSet[UVCoord + 0] *= UScale;
			UVSet[UVCoord + 1] *= VScale;
		}
	}
}

uint32 GetFileHash(const FString& FileName, const FFileStatData& FileStatData, const FString& Config, const FImportParameters& ImportParam)
{
	int64 FileSize = FileStatData.FileSize;
	FDateTime ModificationTime = FileStatData.ModificationTime;

	uint32 FileHash = GetTypeHash(FileName);
	FileHash = HashCombine(FileHash, GetTypeHash(FileSize));
	FileHash = HashCombine(FileHash, GetTypeHash(ModificationTime));
	FileHash = HashCombine(FileHash, GetTypeHash(ImportParam.StitchingTechnique));
	if (!Config.IsEmpty())
	{
		FileHash = HashCombine(FileHash, GetTypeHash(Config));
	}

	return FileHash;
}

uint32 GetGeomFileHash(const uint32 InSGHash, const FImportParameters& ImportParam)
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

uint32 GetFaceTessellation(CT_OBJECT_ID FaceID, TArray<FTessellationData>& FaceTessellationSet, const FImportParameters& ImportParams)
{
	CT_IO_ERROR Error = IO_OK;

	CT_UINT32         VertexCount;
	CT_UINT32         NormalCount;
	CT_UINT32         IndexCount;
	CT_TESS_DATA_TYPE VertexType;
	CT_TESS_DATA_TYPE TexCoordType;
	CT_TESS_DATA_TYPE NormalType;
	CT_LOGICAL        HasRGBColor;
	CT_UINT16         UserSize;
	CT_TESS_DATA_TYPE IndexType;
	void*             VertexArray;
	void*             TexCoordArray;
	void*             NormalArray;
	void*             ColorArray;
	void*             UserArray;
	void*             IndexArray;

	Error = CT_FACE_IO::AskTesselation(FaceID, VertexCount, NormalCount, IndexCount,
		VertexType, TexCoordType, NormalType, HasRGBColor, UserSize, IndexType,
		VertexArray, TexCoordArray, NormalArray, ColorArray, UserArray, IndexArray);

	// Something wrong happened, either an error or no data to collect
	if (Error != IO_OK || VertexArray == nullptr || IndexArray == nullptr || IndexCount == 0)
	{
		VertexCount = NormalCount = IndexCount = 0;
		return 0;
	}

	FTessellationData& Tessellation = FaceTessellationSet.Emplace_GetRef();
	if (ImportParams.bScaleUVMap && TexCoordArray != nullptr)
	{
		switch (TexCoordType)
		{
		case CT_TESS_FLOAT:
			ScaleUV<float>(FaceID, TexCoordArray, VertexCount, (float) ImportParams.ScaleFactor);
			break;
		case CT_TESS_DOUBLE:
			ScaleUV<double>(FaceID, TexCoordArray, VertexCount, ImportParams.ScaleFactor);
			break;
		}
	}

	Tessellation.VertexCount = VertexCount;
	Tessellation.NormalCount = NormalCount;
	Tessellation.IndexCount = IndexCount;
	Tessellation.TexCoordCount = TexCoordArray ? VertexCount : 0;
	Tessellation.SizeOfVertexType = GetSize(VertexType);
	Tessellation.SizeOfTexCoordType = GetSize(TexCoordType);
	Tessellation.SizeOfNormalType = GetSize(NormalType);
	Tessellation.SizeOfIndexType = GetSize(IndexType);

	Tessellation.VertexArray.Append((uint8*) VertexArray, 3 * Tessellation.VertexCount * Tessellation.SizeOfVertexType);
	Tessellation.NormalArray.Append((uint8*)NormalArray, 3 * Tessellation.NormalCount * Tessellation.SizeOfNormalType);
	Tessellation.IndexArray.Append((uint8*)IndexArray, Tessellation.IndexCount * Tessellation.SizeOfIndexType);

	if (TexCoordArray)
	{
		Tessellation.TexCoordArray.Append((uint8*)TexCoordArray, 2 * Tessellation.TexCoordCount * Tessellation.SizeOfTexCoordType);
	}

	return (uint32)Tessellation.IndexCount / 3;
}


void GetCTObjectDisplayDataIds(CT_OBJECT_ID ObjectID, FObjectDisplayDataId& Material)
{
	if (CT_OBJECT_IO::SearchAttribute(ObjectID, CT_ATTRIB_MATERIALID) == IO_OK)
	{
		CT_UINT32 MaterialId = 0;
		if (CT_CURRENT_ATTRIB_IO::AskIntField(ITH_MATERIALID_VALUE, MaterialId) == IO_OK && MaterialId > 0)
		{
			Material.Material = (uint32)MaterialId;
		}
	}

	if (CT_OBJECT_IO::SearchAttribute(ObjectID, CT_ATTRIB_COLORID) == IO_OK)
	{
		CT_UINT32 ColorId = 0;
		if (CT_CURRENT_ATTRIB_IO::AskIntField(ITH_COLORID_VALUE, ColorId) == IO_OK && ColorId > 0)
		{
			uint8 alpha = 255;
			if (CT_OBJECT_IO::SearchAttribute(ObjectID, CT_ATTRIB_TRANSPARENCY) == IO_OK)
			{
				CT_DOUBLE dbl_value = 0.;
				if (CT_CURRENT_ATTRIB_IO::AskDblField(0, dbl_value) == IO_OK && dbl_value >= 0.0 && dbl_value <= 1.0)
				{
					alpha = (uint8)int((1. - dbl_value) * 255.);
				}
			}
			Material.Color = BuildColorId(ColorId, alpha);
		}
	}
}

FArchiveMaterial& FCoreTechFileParser::FindOrAddMaterial(CT_MATERIAL_ID MaterialId)
{
	if (FArchiveMaterial* NewMaterial = MockUpDescription.MaterialHIdToMaterial.Find(MaterialId))
	{
		return *NewMaterial;
	}

	FArchiveMaterial& NewMaterial = MockUpDescription.MaterialHIdToMaterial.Emplace(MaterialId, MaterialId);
	GetMaterial(MaterialId, NewMaterial.Material);
	NewMaterial.UEMaterialName = BuildMaterialName(NewMaterial.Material);
	return NewMaterial;
}

FArchiveColor& FCoreTechFileParser::FindOrAddColor(uint32 ColorHId)
{
	if (FArchiveColor* Color = MockUpDescription.ColorHIdToColor.Find(ColorHId))
	{
		return *Color;
	}

	FArchiveColor& NewColor = MockUpDescription.ColorHIdToColor.Add(ColorHId, ColorHId);
	GetColor(ColorHId, NewColor.Color);
	NewColor.UEMaterialName = BuildColorName(NewColor.Color);
	return NewColor;
}

uint32 FCoreTechFileParser::GetObjectMaterial(ICADArchiveObject& Object)
{
	if (FString* Material = Object.MetaData.Find(TEXT("MaterialName")))
	{
		return (uint32) FCString::Atoi64(**Material);
	}
	if (FString* Material = Object.MetaData.Find(TEXT("ColorName")))
	{
		return (uint32)FCString::Atoi64(**Material);
	}
	return 0;
}

bool GetColor(uint32 ColorUuid, FColor& OutColor)
{
	uint32 ColorId;
	uint8 Alpha;
	GetCTColorIdAlpha(ColorUuid, ColorId, Alpha);

	CT_COLOR CtColor = { 200, 200, 200 };
	if (ColorId > 0)
	{
		if (CT_MATERIAL_IO::AskIndexedColor((CT_OBJECT_ID)ColorId, CtColor) != IO_OK)
		{
			return false;
		}
	}

	OutColor.R = CtColor[0];
	OutColor.G = CtColor[1];
	OutColor.B = CtColor[2];
	OutColor.A = Alpha;
	return true;
}

bool GetMaterial(uint32 MaterialId, FCADMaterial& OutMaterial)
{
	//	// Ref. BaseHelper.cpp
	CT_STR CtName;
	CT_COLOR CtDiffuse = { 200, 200, 200 }, CtAmbient = { 200, 200, 200 }, CtSpecular = { 200, 200, 200 };
	CT_FLOAT CtShininess = 0.f, CtTransparency = 0.f, CtReflexion = 0.f;
	CT_TEXTURE_ID CtTextureId = 0;
	if (MaterialId)
	{
		CT_IO_ERROR bReturn = CT_MATERIAL_IO::AskParameters(MaterialId, CtName, CtDiffuse, CtAmbient, CtSpecular, CtShininess, CtTransparency, CtReflexion, CtTextureId);
		if (bReturn != IO_OK)
		{
			return false;
		}
	}

	CT_STR CtTextureName = "";
	if (CtTextureId)
	{
		CT_INT32  Width = 0, Height = 0;
		if (!(CT_TEXTURE_IO::AskParameters(CtTextureId, CtTextureName, Width, Height) == IO_OK && Width && Height))
		{
			CtTextureName = "";
		}
	}

	OutMaterial.MaterialName = CtName.toUnicode();
	OutMaterial.Diffuse = FColor(CtDiffuse[0], CtDiffuse[1], CtDiffuse[2], 255);
	OutMaterial.Ambient = FColor(CtAmbient[0], CtAmbient[1], CtAmbient[2], 255);
	OutMaterial.Specular = FColor(CtSpecular[0], CtSpecular[1], CtSpecular[2], 255);
	OutMaterial.Shininess = CtShininess;
	OutMaterial.Transparency = CtTransparency;
	OutMaterial.Reflexion = CtReflexion;
	OutMaterial.TextureName = CtTextureName.toUnicode();
	return true;
}

void FCoreTechFileParser::SetFaceMainMaterial(FObjectDisplayDataId& InFaceMaterial, FObjectDisplayDataId& InBodyMaterial, FBodyMesh& BodyMesh, int32 FaceIndex)
{
	uint32 FaceMaterialHash = 0;
	uint32 BodyMaterialHash = 0;
	uint32 FaceColorHash = 0;
	uint32 BodyColorHash = 0;

	FTessellationData& FaceTessellations = BodyMesh.Faces[FaceIndex];

	if (InFaceMaterial.Material > 0)
	{
		FArchiveMaterial& Material = FindOrAddMaterial(InFaceMaterial.Material);
		FaceTessellations.MaterialName = Material.UEMaterialName;
		BodyMesh.MaterialSet.Add(Material.UEMaterialName);
	}
	else if (InBodyMaterial.Material > 0)
	{
		FArchiveMaterial& Material = FindOrAddMaterial(InBodyMaterial.Material);
		FaceTessellations.MaterialName = Material.UEMaterialName;
		BodyMesh.MaterialSet.Add(Material.UEMaterialName);
	}

	if (InFaceMaterial.Color > 0)
	{
		FArchiveColor& Color = FindOrAddColor(InFaceMaterial.Color);
		FaceTessellations.ColorName = Color.UEMaterialName;
		BodyMesh.ColorSet.Add(Color.UEMaterialName);
	}
	else if (InBodyMaterial.Color > 0)
	{
		FArchiveColor& Color = FindOrAddColor(InBodyMaterial.Color);
		FaceTessellations.ColorName = Color.UEMaterialName;
		BodyMesh.ColorSet.Add(Color.UEMaterialName);
	}
	else if(InBodyMaterial.DefaultMaterialName)
	{
		FaceTessellations.ColorName = InBodyMaterial.DefaultMaterialName;
		BodyMesh.ColorSet.Add(InBodyMaterial.DefaultMaterialName);
	}
}

uint32 GetStaticMeshUuid(const TCHAR* OutSgFile, const int32 BodyId)
{
	uint32 BodyUUID = GetTypeHash(OutSgFile);
	BodyUUID = HashCombine(BodyUUID, GetTypeHash(BodyId));
	return BodyUUID;
}

void FCoreTechFileParser::ExportSceneGraphFile()
{
	SerializeMockUp(MockUpDescription, *FPaths::Combine(CachePath, TEXT("scene"), MockUpDescription.SceneGraphArchive + TEXT(".sg")));
}

void FCoreTechFileParser::ExportMeshArchiveFile()
{
	SerializeBodyMeshSet(*MeshArchiveFilePath, BodyMeshes);
}

void FCoreTechFileParser::LoadSceneGraphArchive(const FString& SGFile)
{
	DeserializeMockUpFile(*SGFile, MockUpDescription);
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

void FCoreTechFileParser::ReadMaterials()
{
	CT_UINT32 MaterialId = 1;
	while (true)
	{
		FCADMaterial Material;
		bool bReturn = GetMaterial(MaterialId, Material);
		if (!bReturn)
		{
			break;
		}

		FArchiveMaterial& MaterialObject = MockUpDescription.MaterialHIdToMaterial.Emplace(MaterialId, MaterialId);
		MaterialObject.UEMaterialName = BuildMaterialName(Material);
		MaterialObject.Material = Material; 

		MaterialId++;
	}
}

FCoreTechFileParser::FCoreTechFileParser(const FString& InCADFullPath, const FString& InCachePath, const FImportParameters& ImportParams, const TCHAR* KernelIOPath)
	: CachePath(InCachePath)
	, FullPath(InCADFullPath)
	, bNeedSaveCTFile(false)
	, ImportParameters(ImportParams)
{
	CTKIO_InitializeKernel(ImportParameters.MetricUnit, KernelIOPath);
}

FCoreTechFileParser::EProcessResult FCoreTechFileParser::ProcessFile()
{
	FileConfiguration.Empty();

	CADFile = FPaths::GetCleanFilename(*FullPath);

	// Check if configuration is passed with file name
	FString NewCADFile;
	const FString CutOffMarker = TEXT("|");
	if (FullPath.Split(CutOffMarker, &NewCADFile, &FileConfiguration))
	{
		FullPath = NewCADFile;
	}

	if (!IFileManager::Get().FileExists(*FullPath))
	{
		return EProcessResult::FileNotFound;
	}

	FFileStatData FileStatData = IFileManager::Get().GetStatData(*FullPath);
	uint32 FileHash = GetFileHash(CADFile, FileStatData, FileConfiguration, ImportParameters);

	MockUpDescription.SceneGraphArchive = FString::Printf(TEXT("UEx%08x"), FileHash);

	FString SceneGraphArchiveFilePath = FPaths::Combine(CachePath, TEXT("scene"), MockUpDescription.SceneGraphArchive + TEXT(".sg"));
	FString CTFilePath = FPaths::Combine(CachePath, TEXT("cad"), MockUpDescription.SceneGraphArchive + TEXT(".ct"));

	uint32 MeshFileHash = GetGeomFileHash(FileHash, ImportParameters);
	MeshArchiveFile = FString::Printf(TEXT("UEx%08x"), MeshFileHash);
	MeshArchiveFilePath = FPaths::Combine(CachePath, TEXT("mesh"), MeshArchiveFile + TEXT(".gm"));

	bool bNeedToProceed = true;
#ifndef IGNORE_CACHE
	if (IFileManager::Get().FileExists(*SceneGraphArchiveFilePath))
	{
		if (!IFileManager::Get().FileExists(*CTFilePath)) // the file is scene graph only because no CT file
		{
			bNeedToProceed = false;
		}
		else if (IFileManager::Get().FileExists(*MeshArchiveFilePath)) // the file has been proceed with same meshing parameters
		{
			bNeedToProceed = false;
		}
		else // the file has been converted into CT file but meshed with different parameters
		{
			FullPath = CTFilePath;
		}
	}

	if (!bNeedToProceed)
	{
		// The file has been yet proceed, get ExternalRef
		LoadSceneGraphArchive(SceneGraphArchiveFilePath);
		return EProcessResult::ProcessOk;
	}
#endif
	// Process the file
	return ReadFileWithKernelIO();
}

FCoreTechFileParser::EProcessResult FCoreTechFileParser::ReadFileWithKernelIO()
{
	CT_IO_ERROR Result = IO_OK;
	CT_OBJECT_ID MainId = 0;

	Result = CT_KERNEL_IO::UnloadModel();

	CT_FLAGS CTImportOption = SetCoreTechImportOption(FPaths::GetExtension(CADFile));

	FString LoadOption;
	CT_UINT32 NumberOfIds = 1;
	if (!FileConfiguration.IsEmpty())
	{
		NumberOfIds = CT_KERNEL_IO::AskFileNbOfIds(*FullPath);
		if (NumberOfIds > 1)
		{
			CT_UINT32 ActiveConfig = CT_KERNEL_IO::AskFileActiveConfig(*FullPath);
			for (CT_UINT32 i = 0; i < NumberOfIds; i++)
			{
				CT_STR ConfValue = CT_KERNEL_IO::AskFileIdIthName(*FullPath, i);
				if (FileConfiguration == ConfValue.toUnicode()) {
					ActiveConfig = i;
					break;
				}
			}

			CTImportOption |= CT_LOAD_FLAGS_READ_SPECIFIC_OBJECT;
			LoadOption = FString::FromInt((int32) ActiveConfig);
		}
	}

	Result = CT_KERNEL_IO::LoadFile(*FullPath, MainId, CTImportOption, 0, *LoadOption);
	if (Result == IO_ERROR_EMPTY_ASSEMBLY)
	{
		Result = CT_KERNEL_IO::UnloadModel();
		if (Result != IO_OK)
		{
			return EProcessResult::ProcessFailed;
		}
		Result = CT_KERNEL_IO::LoadFile(*FullPath, MainId, CTImportOption | CT_LOAD_FLAGS_LOAD_EXTERNAL_REF);
	}

	if (Result != IO_OK && Result != IO_OK_MISSING_LICENSES)
	{
		CT_KERNEL_IO::UnloadModel();
		return EProcessResult::ProcessFailed;
	}

	Repair(MainId, ImportParameters.StitchingTechnique);
	SetCoreTechTessellationState(ImportParameters);

	MockUpDescription.FullPath = FullPath;
	MockUpDescription.CADFile = CADFile;

	const CT_OBJECT_TYPE TypeSet[] = { CT_INSTANCE_TYPE, CT_ASSEMBLY_TYPE, CT_PART_TYPE, CT_COMPONENT_TYPE, CT_BODY_TYPE, CT_UNLOADED_COMPONENT_TYPE, CT_UNLOADED_ASSEMBLY_TYPE, CT_UNLOADED_PART_TYPE};
	enum EObjectTypeIndex : uint8	{ CT_INSTANCE_INDEX = 0, CT_ASSEMBLY_INDEX, CT_PART_INDEX, CT_COMPONENT_INDEX, CT_BODY_INDEX, CT_UNLOADED_COMPONENT_INDEX, CT_UNLOADED_ASSEMBLY_INDEX, CT_UNLOADED_PART_INDEX };

	uint32 NbElements[8], NbTotal = 10;
	for (int32 index = 0; index < 8; index++)
	{
		CT_KERNEL_IO::AskNbObjectsType(NbElements[index], TypeSet[index]);
		NbTotal += NbElements[index];
	}

	BodyMeshes.Reserve(NbElements[CT_BODY_INDEX]);

	MockUpDescription.BodySet.Reserve(NbElements[CT_BODY_INDEX]);
	MockUpDescription.ComponentSet.Reserve(NbElements[CT_ASSEMBLY_INDEX] + NbElements[CT_PART_INDEX] + NbElements[CT_COMPONENT_INDEX]);
	MockUpDescription.UnloadedComponentSet.Reserve(NbElements[CT_UNLOADED_COMPONENT_INDEX] + NbElements[CT_UNLOADED_ASSEMBLY_INDEX] + NbElements[CT_UNLOADED_PART_INDEX]);
	MockUpDescription.Instances.Reserve(NbElements[CT_INSTANCE_INDEX]);

	MockUpDescription.CADIdToBodyIndex.Reserve(NbElements[CT_BODY_INDEX]);
	MockUpDescription.CADIdToComponentIndex.Reserve(NbElements[CT_ASSEMBLY_INDEX] + NbElements[CT_PART_INDEX] + NbElements[CT_COMPONENT_INDEX]);
	MockUpDescription.CADIdToUnloadedComponentIndex.Reserve(NbElements[CT_UNLOADED_COMPONENT_INDEX] + NbElements[CT_UNLOADED_ASSEMBLY_INDEX] + NbElements[CT_UNLOADED_PART_INDEX]);
	MockUpDescription.CADIdToInstanceIndex.Reserve(NbElements[CT_INSTANCE_INDEX]);

	uint32 MaterialNum = GetMaterialNum();
	MockUpDescription.MaterialHIdToMaterial.Reserve(MaterialNum);

	ReadMaterials();

	// Parse the file
	uint32 DefaultMaterialHash = 0;
	bool bReadNodeSucceed = ReadNode(MainId, DefaultMaterialHash);
	// End of parsing

	if (bNeedSaveCTFile)
	{
		CT_LIST_IO ObjectList;
		ObjectList.PushBack(MainId);

		CT_KERNEL_IO::SaveFile(ObjectList, *FPaths::Combine(CachePath, TEXT("cad"), MockUpDescription.SceneGraphArchive + TEXT(".ct")), L"Ct");
	}

	CT_KERNEL_IO::UnloadModel();

	if (!bReadNodeSucceed)
	{
		return EProcessResult::ProcessFailed;
	}

	ExportSceneGraphFile();
	ExportMeshArchiveFile();

	return EProcessResult::ProcessOk;
}

CT_FLAGS FCoreTechFileParser::SetCoreTechImportOption(const FString& MainFileExt)
{
	// Set import option
	CT_FLAGS Flags = CT_LOAD_FLAGS_USE_DEFAULT;

	// Do not read meta-data from JT files with CoreTech. It crashes...
	if (MainFileExt != TEXT("jt"))
	{
		Flags |= CT_LOAD_FLAGS_READ_META_DATA;
	}

	if (MainFileExt == TEXT("catpart") || MainFileExt == TEXT("catproduct") || MainFileExt == TEXT("cgr"))
	{
		Flags |= CT_LOAD_FLAGS_V5_READ_GEOM_SET;
	}

	// All the BRep topology is not available in IGES import
	// Ask Kernel IO to complete or create missing topology
	if (MainFileExt == TEXT("igs") || MainFileExt == TEXT("iges"))
	{
		Flags |= CT_LOAD_FLAG_SEARCH_NEW_TOPOLOGY | CT_LOAD_FLAG_COMPLETE_TOPOLOGY;
	}

	// 3dxml file is zipped files, it's full managed by Kernel_io. We cannot read it in sequential mode
	if (MainFileExt != TEXT("3dxml"))
	{
		Flags &= ~CT_LOAD_FLAGS_LOAD_EXTERNAL_REF;
	}

	return Flags;
}

bool FCoreTechFileParser::ReadNode(CT_OBJECT_ID NodeId, uint32 DefaultMaterialHash)
{
	CT_OBJECT_TYPE Type;
	CT_OBJECT_IO::AskType(NodeId, Type);

	switch (Type)
	{
	case CT_INSTANCE_TYPE:
		if (int32* Index = MockUpDescription.CADIdToInstanceIndex.Find(NodeId))
		{
			return true;
		}
		return ReadInstance(NodeId, DefaultMaterialHash);

	case CT_ASSEMBLY_TYPE:
	case CT_PART_TYPE:
	case CT_COMPONENT_TYPE:
		if (int32* Index = MockUpDescription.CADIdToComponentIndex.Find(NodeId))
		{
			return true;
		}
		return ReadComponent(NodeId, DefaultMaterialHash);

	case CT_UNLOADED_ASSEMBLY_TYPE:
	case CT_UNLOADED_COMPONENT_TYPE:
	case CT_UNLOADED_PART_TYPE:
		if (int32* Index = MockUpDescription.CADIdToUnloadedComponentIndex.Find(NodeId))
		{
			return true;
		}
		return ReadUnloadedComponent(NodeId);

	case CT_BODY_TYPE:
		if (int32* Index = MockUpDescription.CADIdToBodyIndex.Find(NodeId))
		{
			return true;
		}
		return ReadBody(NodeId, DefaultMaterialHash);

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
	CT_STR Filename, FileType;
	CT_IO_ERROR Error = CT_COMPONENT_IO::AskExternalDefinition(ComponentId, Filename, FileType);
	if (Error != IO_OK)
	{
		return false;
	}

	int32 Index = MockUpDescription.UnloadedComponentSet.Emplace(ComponentId);
	MockUpDescription.CADIdToUnloadedComponentIndex.Add(ComponentId, Index);
	ReadNodeMetaData(ComponentId, MockUpDescription.UnloadedComponentSet[Index].MetaData);

	MockUpDescription.UnloadedComponentSet[Index].FileName = Filename.toUnicode();
	MockUpDescription.UnloadedComponentSet[Index].FileType = FileType.toUnicode();

	return true;
}

bool FCoreTechFileParser::ReadComponent(CT_OBJECT_ID ComponentId, uint32 DefaultMaterialHash)
{
	int32 Index = MockUpDescription.ComponentSet.Emplace(ComponentId);
	MockUpDescription.CADIdToComponentIndex.Add(ComponentId, Index);
	ReadNodeMetaData(ComponentId, MockUpDescription.ComponentSet[Index].MetaData);

	if (uint32 MaterialHash = GetObjectMaterial(MockUpDescription.ComponentSet[Index]))
	{
		DefaultMaterialHash = MaterialHash;
	}

	CT_LIST_IO Children;
	CT_COMPONENT_IO::AskChildren(ComponentId, Children);

	// Parse children
	Children.IteratorInitialize();
	CT_OBJECT_ID ChildId;
	while ((ChildId = Children.IteratorIter()) != 0)
	{
		if (ReadNode(ChildId, DefaultMaterialHash))
		{
			MockUpDescription.ComponentSet[Index].Children.Add(ChildId);
		}
	}

	return true;
}

bool FCoreTechFileParser::ReadInstance(CT_OBJECT_ID InstanceNodeId, uint32 DefaultMaterialHash)
{
	NodeConfiguration.Empty();

	int32 Index = MockUpDescription.Instances.Emplace(InstanceNodeId);
	MockUpDescription.CADIdToInstanceIndex.Add(InstanceNodeId, Index);
	ReadNodeMetaData(InstanceNodeId, MockUpDescription.Instances[Index].MetaData);

	if (uint32 MaterialHash = GetObjectMaterial(MockUpDescription.Instances[Index]))
	{
		DefaultMaterialHash = MaterialHash;
	}

	// Ask the transformation of the instance
	double Matrix[16];
	if (CT_INSTANCE_IO::AskTransformation(InstanceNodeId, Matrix) == IO_OK)
	{
		float* MatrixFloats = (float*)MockUpDescription.Instances[Index].TransformMatrix.M;
		for (int32 index = 0; index < 16; index++)
		{
			MatrixFloats[index] = (float) Matrix[index];
		}
	}
	
	// Ask the reference
	CT_OBJECT_ID ReferenceNodeId;
	CT_IO_ERROR CTReturn = CT_INSTANCE_IO::AskChild(InstanceNodeId, ReferenceNodeId);
	if (CTReturn != CT_IO_ERROR::IO_OK)
		return false;
	MockUpDescription.Instances[Index].ReferenceNodeId = ReferenceNodeId;

	CT_OBJECT_TYPE type;
	CT_OBJECT_IO::AskType(ReferenceNodeId, type);
	if (type == CT_UNLOADED_PART_TYPE || type == CT_UNLOADED_COMPONENT_TYPE || type == CT_UNLOADED_ASSEMBLY_TYPE)
	{
		MockUpDescription.Instances[Index].bIsExternalRef = true;

		CT_STR ComponentFile, FileType;
		CT_COMPONENT_IO::AskExternalDefinition(ReferenceNodeId, ComponentFile, FileType);
		FString ExternalRefFullPath = ComponentFile.toUnicode();

		if(!NodeConfiguration.IsEmpty())
		{
			ExternalRefFullPath += TEXT("|") + NodeConfiguration;
		}

		MockUpDescription.Instances[Index].ExternalRef = FPaths::GetCleanFilename(ExternalRefFullPath);
		MockUpDescription.ExternalRefSet.Add(ExternalRefFullPath);
	}
	else
	{
		MockUpDescription.Instances[Index].bIsExternalRef = false;
	}

	return ReadNode(ReferenceNodeId, DefaultMaterialHash);
}


uint32 GetBodiesFaceSetNum(TArray<CT_OBJECT_ID>& BodySet)
{
	uint32 size = 0;
	for (int Index = 0; Index < BodySet.Num(); Index++)
	{
		// Loop through the face of the first body and collect material data
		CT_LIST_IO FaceList;
		CT_BODY_IO::AskFaces(BodySet[Index], FaceList);
		size += FaceList.Count();
	}
	return size;
}

void FCoreTechFileParser::GetBodyTessellation(CT_OBJECT_ID BodyId, FBodyMesh& OutBodyMesh, const FImportParameters& ImportParams, uint32 DefaultMaterialHash)
{
	CT_LIST_IO FaceList;
	CT_BODY_IO::AskFaces(BodyId, FaceList);

	uint32 FaceSize = FaceList.Count();

	// Allocate memory space for tessellation data
	OutBodyMesh.Faces.Reserve(FaceSize);
	OutBodyMesh.ColorSet.Reserve(FaceSize);
	OutBodyMesh.MaterialSet.Reserve(FaceSize);

	CT_MATERIAL_ID BodyCtMaterialId = 0;

	FObjectDisplayDataId BodyMaterial;
	BodyMaterial.DefaultMaterialName = DefaultMaterialHash;
	GetCTObjectDisplayDataIds(BodyId, BodyMaterial);

	// Loop through the face of the first body and collect all tessellation data
	FaceList.IteratorInitialize();

	int32 FaceIndex = 0;

	CT_OBJECT_ID FaceID;
	while ((FaceID = FaceList.IteratorIter()) != 0)
	{
		uint32 TriangleNum = GetFaceTessellation(FaceID, OutBodyMesh.Faces, ImportParams);

		if (TriangleNum == 0)
		{
			continue;
		}

		OutBodyMesh.TriangleCount += TriangleNum;

		FObjectDisplayDataId FaceMaterial;
		GetCTObjectDisplayDataIds(FaceID, FaceMaterial);
		SetFaceMainMaterial(FaceMaterial, BodyMaterial, OutBodyMesh, FaceIndex);
		FaceIndex++;
	}
}

bool FCoreTechFileParser::ReadBody(CT_OBJECT_ID BodyId, uint32 DefaultMaterialHash)
{
	// Is this body a constructive geometry ?
	CT_LIST_IO FaceList;
	CT_BODY_IO::AskFaces(BodyId, FaceList);
	if (1 == FaceList.Count())
	{
		FaceList.IteratorInitialize();
		FString Value;
		GetStringMetaDataValue(FaceList.IteratorIter(), TEXT("Constructive Plane"), Value);
		if (Value == TEXT("true"))
		{
			return false;
		}
	}

	int32 Index = MockUpDescription.BodySet.Emplace(BodyId);
	MockUpDescription.CADIdToBodyIndex.Add(BodyId, Index);
	ReadNodeMetaData(BodyId, MockUpDescription.BodySet[Index].MetaData);

	int32 BodyMeshIndex = BodyMeshes.Emplace(BodyId);

	if (uint32 MaterialHash = GetObjectMaterial(MockUpDescription.BodySet[Index]))
	{
		DefaultMaterialHash = MaterialHash;
	}

	bNeedSaveCTFile = true;

	MockUpDescription.BodySet[Index].MeshActorName = GetStaticMeshUuid(*MockUpDescription.SceneGraphArchive, BodyId);
	BodyMeshes[BodyMeshIndex].MeshActorName = MockUpDescription.BodySet[Index].MeshActorName;

	GetBodyTessellation(BodyId, BodyMeshes[BodyMeshIndex], ImportParameters, DefaultMaterialHash);

	MockUpDescription.BodySet[Index].ColorFaceSet = BodyMeshes[BodyMeshIndex].ColorSet;
	MockUpDescription.BodySet[Index].MaterialFaceSet = BodyMeshes[BodyMeshIndex].MaterialSet;

	// Save Body in CT file for re-tessellation
	CT_LIST_IO ObjectList;
	ObjectList.PushBack(BodyId);
	FString BodyFile = FString::Printf(TEXT("UEx%08x"), MockUpDescription.BodySet[Index].MeshActorName);
	CT_KERNEL_IO::SaveFile(ObjectList, *FPaths::Combine(CachePath, TEXT("body"), BodyFile + TEXT(".ct")), L"Ct");

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

void FCoreTechFileParser::GetStringMetaDataValue(CT_OBJECT_ID NodeId, const TCHAR* InMetaDataName, FString& OutMetaDataValue)
{
	CT_STR FieldName;
	CT_UINT32 IthAttrib = 0;
	while (CT_OBJECT_IO::SearchAttribute(NodeId, CT_ATTRIB_STRING_METADATA, IthAttrib++) == IO_OK)
	{
		if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_STRING_METADATA_NAME, FieldName) != IO_OK) break;
		if (!FCString::Strcmp(InMetaDataName, FieldName.toUnicode()))
		{
			CT_STR FieldStrValue;
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_STRING_METADATA_VALUE, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty())
			{
				return;
			}
			OutMetaDataValue = FieldStrValue.toUnicode();
			return;
		}
	}
}

void FCoreTechFileParser::ReadNodeMetaData(CT_OBJECT_ID NodeId, TMap<FString, FString>& OutMetaData)
{
	const FString ConfigName = TEXT("Configuration Name");

	if (CT_COMPONENT_IO::IsA(NodeId, CT_COMPONENT_TYPE))
	{
		CT_STR FileName, FileType;
		CT_COMPONENT_IO::AskExternalDefinition(NodeId, FileName, FileType);
		if (!FileName.IsEmpty())
		{
			OutMetaData.Add(TEXT("ExternalDefinition"), FileName.toUnicode());
		}
	}

	CT_SHOW_ATTRIBUTE IsShow = CT_UNKNOWN;
	if (CT_OBJECT_IO::AskShowAttribute(NodeId, IsShow) == IO_OK)
	{
		switch (IsShow)
		{
		case CT_SHOW:
			OutMetaData.Add(TEXT("ShowAttribute"), TEXT("show"));
			break;
		case CT_NOSHOW:
			OutMetaData.Add(TEXT("ShowAttribute"), TEXT("noShow"));
			break;
		case CT_UNKNOWN:
			OutMetaData.Add(TEXT("ShowAttribute"), TEXT("unknown"));
			break;
		}
	}

	CT_UINT32 IthAttrib = 0;
	while (CT_OBJECT_IO::SearchAttribute(NodeId, CT_ATTRIB_ALL, IthAttrib++) == IO_OK)
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
			break;

		case CT_ATTRIB_NAME:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_NAME_VALUE, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) { break; }
			OutMetaData.Add(TEXT("CTName"), FieldStrValue.toUnicode());
			break;

		case CT_ATTRIB_ORIGINAL_NAME:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_NAME_VALUE, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) { break; }
			OutMetaData.Add(TEXT("Name"), FieldStrValue.toUnicode());
			break;

		case CT_ATTRIB_ORIGINAL_FILENAME:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_FILENAME_VALUE, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) { break; }
			OutMetaData.Add(TEXT("FileName"), FieldStrValue.toUnicode());
			break;

		case CT_ATTRIB_UUID:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_UUID_VALUE, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) { break; }
			OutMetaData.Add(TEXT("UUID"), FieldStrValue.toUnicode());
			break;

		case CT_ATTRIB_INPUT_FORMAT_AND_EMETTOR:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_INPUT_FORMAT_AND_EMETTOR, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) { break; }
			OutMetaData.Add(TEXT("Input_Format_and_Emitter"), FieldStrValue.toUnicode());
			break;

		case CT_ATTRIB_CONFIGURATION_NAME:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_NAME_VALUE, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) { break; }
			OutMetaData.Add(TEXT("ConfigurationName"), FieldStrValue.toUnicode());
			break;

		case CT_ATTRIB_LAYERID:
			GetAttributeValue(AttributeType, ITH_LAYERID_VALUE, FieldValue);
			OutMetaData.Add(TEXT("LayerId"), FieldValue);
			GetAttributeValue(AttributeType, ITH_LAYERID_NAME, FieldValue);
			OutMetaData.Add(TEXT("LayerName"), FieldValue);
			GetAttributeValue(AttributeType, ITH_LAYERID_FLAG, FieldValue);
			OutMetaData.Add(TEXT("LayerFlag"), FieldValue);
			break;

		case CT_ATTRIB_COLORID:
			{
				if (CT_CURRENT_ATTRIB_IO::AskIntField(ITH_COLORID_VALUE, FieldIntValue) != IO_OK) break;
				uint32 ColorId = FieldIntValue;

				uint8 Alpha = 255;
				if (CT_OBJECT_IO::SearchAttribute(NodeId, CT_ATTRIB_TRANSPARENCY) == IO_OK)
				{
					if (CT_CURRENT_ATTRIB_IO::AskDblField(0, FieldDoubleValue0) == IO_OK)
					{
						Alpha = FMath::Max((1. - FieldDoubleValue0), FieldDoubleValue0) * 255.;
					}
				}

				uint32 ColorHId = BuildColorId(ColorId, Alpha);
				FArchiveColor& ColorArchive = FindOrAddColor(ColorHId);
				OutMetaData.Add(TEXT("ColorName"), FString::FromInt(ColorArchive.UEMaterialName));

				FString colorHexa = FString::Printf(TEXT("%02x%02x%02x%02x"), ColorArchive.Color.R, ColorArchive.Color.G, ColorArchive.Color.B, ColorArchive.Color.A);
				OutMetaData.Add(TEXT("ColorValue"), colorHexa);
			}
			break;

		case CT_ATTRIB_MATERIALID:
		{
			if (CT_CURRENT_ATTRIB_IO::AskIntField(ITH_MATERIALID_VALUE, FieldIntValue) != IO_OK) break;
			if (FArchiveMaterial* Material = MockUpDescription.MaterialHIdToMaterial.Find(FieldIntValue))
			{
				OutMetaData.Add(TEXT("MaterialName"), FString::FromInt(Material->UEMaterialName));
			}
			break;
		}

		case CT_ATTRIB_TRANSPARENCY:
			if (CT_CURRENT_ATTRIB_IO::AskDblField(ITH_TRANSPARENCY_VALUE, FieldDoubleValue0) != IO_OK) break;
			FieldIntValue = FMath::Max((1. - FieldDoubleValue0), FieldDoubleValue0) * 255.;
			OutMetaData.Add(TEXT("Transparency"), FString::FromInt(FieldIntValue));
			break;

		case CT_ATTRIB_COMMENT:
			//ITH_COMMENT_POSX, ITH_COMMENT_POSY, ITH_COMMENT_POSZ, ITH_COMMENT_TEXT
			break;

		case CT_ATTRIB_REFCOUNT:
			if (CT_CURRENT_ATTRIB_IO::AskIntField(ITH_REFCOUNT_VALUE, FieldIntValue) != IO_OK) break;
			//OutMetaData.Add(TEXT("RefCount"), FString::FromInt(FieldIntValue));
			break;

		case CT_ATTRIB_TESS_PARAMS:
		case CT_ATTRIB_COMPARE_RESULT:
			break;

		case CT_ATTRIB_DENSITY:
			//ITH_VOLUME_DENSITY_VALUE
			break;

		case CT_ATTRIB_MASS_PROPERTIES:
			if (CT_CURRENT_ATTRIB_IO::AskDblField(ITH_MASS_PROPERTIES_AREA, FieldDoubleValue0) != IO_OK) break;
			OutMetaData.Add(TEXT("Area"), FString::Printf(TEXT("%lf"), FieldDoubleValue0));
			if (CT_CURRENT_ATTRIB_IO::AskDblField(ITH_MASS_PROPERTIES_VOLUME, FieldDoubleValue0) != IO_OK) break;
			OutMetaData.Add(TEXT("Volume"), FString::Printf(TEXT("%lf"), FieldDoubleValue0));
			if (CT_CURRENT_ATTRIB_IO::AskDblField(ITH_MASS_PROPERTIES_MASS, FieldDoubleValue0) != IO_OK) break;
			OutMetaData.Add(TEXT("Mass"), FString::Printf(TEXT("%lf"), FieldDoubleValue0));
			if (CT_CURRENT_ATTRIB_IO::AskDblField(ITH_MASS_PROPERTIES_LENGTH, FieldDoubleValue0) != IO_OK) break;
			OutMetaData.Add(TEXT("Length"), FString::Printf(TEXT("%lf"), FieldDoubleValue0));
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
			OutMetaData.Add(FieldName.toUnicode(), FString::FromInt(FieldIntValue));
			break;

		case CT_ATTRIB_DOUBLE_METADATA:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_DOUBLE_METADATA_NAME, FieldName) != IO_OK) break;
			if (CT_CURRENT_ATTRIB_IO::AskDblField(ITH_DOUBLE_METADATA_VALUE, FieldDoubleValue0) != IO_OK) break;
			OutMetaData.Add(FieldName.toUnicode(), FString::Printf(TEXT("%lf"), FieldDoubleValue0));
			break;

		case CT_ATTRIB_STRING_METADATA:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_STRING_METADATA_NAME, FieldName) != IO_OK) break;
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_STRING_METADATA_VALUE, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) break;
			if(ConfigName== FieldName.toUnicode())
			{
				NodeConfiguration = FieldStrValue.toUnicode();
			}
			OutMetaData.Add(FieldName.toUnicode(), FieldStrValue.toUnicode());
			break;

		case CT_ATTRIB_ORIGINAL_UNITS:
			if (CT_CURRENT_ATTRIB_IO::AskDblField(ITH_ORIGINAL_UNITS_MASS, FieldDoubleValue0) != IO_OK) break;
			if (CT_CURRENT_ATTRIB_IO::AskDblField(ITH_ORIGINAL_UNITS_LENGTH, FieldDoubleValue1) != IO_OK) break;
			if (CT_CURRENT_ATTRIB_IO::AskDblField(ITH_ORIGINAL_UNITS_DURATION, FieldDoubleValue2) != IO_OK) break;
			OutMetaData.Add(TEXT("OriginalUnitsMass"), FString::Printf(TEXT("%lf"), FieldDoubleValue0));
			OutMetaData.Add(TEXT("OriginalUnitsLength"), FString::Printf(TEXT("%lf"), FieldDoubleValue1));
			OutMetaData.Add(TEXT("OriginalUnitsDuration"), FString::Printf(TEXT("%lf"), FieldDoubleValue2));
			break;

		case CT_ATTRIB_ORIGINAL_TOLERANCE:
		case CT_ATTRIB_IGES_PARAMETERS:
		case CT_ATTRIB_READ_V4_MARKER:
			break;

		case CT_ATTRIB_PRODUCT:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_PRODUCT_REVISION, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) { break; }
			OutMetaData.Add(TEXT("ProductRevision"), FieldStrValue.toUnicode());
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_PRODUCT_DEFINITION, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) { break; }
			OutMetaData.Add(TEXT("ProductDefinition"), FieldStrValue.toUnicode());
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_PRODUCT_NOMENCLATURE, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) { break; }
			OutMetaData.Add(TEXT("ProductNomenclature"), FieldStrValue.toUnicode());
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_PRODUCT_SOURCE, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) { break; }
			OutMetaData.Add(TEXT("ProductSource"), FieldStrValue.toUnicode());
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_PRODUCT_DESCRIPTION, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) { break; }
			OutMetaData.Add(TEXT("ProductDescription"), FieldStrValue.toUnicode());
			break;

		case CT_ATTRIB_SIMPLIFY:
		case CT_ATTRIB_MIDFACE:
		case CT_ATTRIB_DEBUG_STRING:
		case CT_ATTRIB_DEFEATURING:
		case CT_ATTRIB_BREPLINKID:
		case CT_ATTRIB_MARKUPS_REF:
		case CT_ATTRIB_COLLISION:
			break;

		case CT_ATTRIB_EXTERNAL_ID:
			//ITH_EXTERNAL_ID_VALUE
			break;

		case CT_ATTRIB_MODIFIER:
		case CT_ATTRIB_ORIGINAL_SURF_OLD:
		case CT_ATTRIB_RESULT_BREPLINKID:
			break;

		case CT_ATTRIB_AREA:
			//ITH_AREA_VALUE
			break;

		case CT_ATTRIB_ACIS_SG_PIDNAME:
		case CT_ATTRIB_CURVE_ORIGINAL_BOUNDARY_PARAMS:
			break;

		case CT_ATTRIB_INTEGER_PARAMETER:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_INTEGER_PARAMETER_NAME, FieldName) != IO_OK) break;
			if (CT_CURRENT_ATTRIB_IO::AskIntField(ITH_INTEGER_PARAMETER_VALUE, FieldIntValue) != IO_OK) break;
			OutMetaData.Add(FieldName.toUnicode(), FString::FromInt(FieldIntValue));
			break;

		case CT_ATTRIB_DOUBLE_PARAMETER:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_DOUBLE_PARAMETER_NAME, FieldName) != IO_OK) break;
			if (CT_CURRENT_ATTRIB_IO::AskDblField(ITH_DOUBLE_PARAMETER_VALUE, FieldDoubleValue0) != IO_OK) break;
			OutMetaData.Add(FieldName.toUnicode(), FString::Printf(TEXT("%lf"), FieldDoubleValue0));
			break;

		case CT_ATTRIB_STRING_PARAMETER:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_STRING_PARAMETER_NAME, FieldName) != IO_OK) break;
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_STRING_PARAMETER_VALUE, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) { break; }
			OutMetaData.Add(FieldName.toUnicode(), FieldStrValue.toUnicode());
			break;

		case CT_ATTRIB_PARAMETER_ARRAY:
			//ITH_PARAMETER_ARRAY_NAME
			//ITH_PARAMETER_ARRAY_NUMBER
			//ITH_PARAMETER_ARRAY_VALUES
			break;

		case CT_ATTRIB_SAVE_OPTION:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_SAVE_OPTION_AUTHOR, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) { break; }
			OutMetaData.Add(TEXT("SaveOptionAuthor"), FieldStrValue.toUnicode());

			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_SAVE_OPTION_ORGANIZATION, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) { break; }
			OutMetaData.Add(TEXT("SaveOptionOrganization"), FieldStrValue.toUnicode());
	
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_SAVE_OPTION_FILE_DESCRIPTION, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) { break; }
			OutMetaData.Add(TEXT("SaveOptionFileDescription"), FieldStrValue.toUnicode());

			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_SAVE_OPTION_AUTHORISATION, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) { break; }
			OutMetaData.Add(TEXT("SaveOptionAuthorisation"), FieldStrValue.toUnicode());

			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_SAVE_OPTION_PREPROCESSOR, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) { break; }
			OutMetaData.Add(TEXT("SaveOptionPreprocessor"), FieldStrValue.toUnicode());
			break;

		case CT_ATTRIB_ORIGINAL_ID:
			GetAttributeValue(AttributeType, ITH_ORIGINAL_ID_VALUE, FieldValue);
			OutMetaData.Add(TEXT("OriginalId"), FieldValue);
			break;

		case CT_ATTRIB_ORIGINAL_ID_STRING:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_ORIGINAL_ID_VALUE_STRING, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) { break; }
			OutMetaData.Add(TEXT("OriginalIdStr"), FieldStrValue.toUnicode());
			break;

		case CT_ATTRIB_COLOR_RGB_DOUBLE:
			if (CT_CURRENT_ATTRIB_IO::AskDblField(ITH_ATTRIB_COLOR_R_DOUBLE, FieldDoubleValue0) != IO_OK) break;
			if (CT_CURRENT_ATTRIB_IO::AskDblField(ITH_ATTRIB_COLOR_G_DOUBLE, FieldDoubleValue1) != IO_OK) break;
			if (CT_CURRENT_ATTRIB_IO::AskDblField(ITH_ATTRIB_COLOR_B_DOUBLE, FieldDoubleValue2) != IO_OK) break;
			FieldValue = FString::Printf(TEXT("%lf"), FieldDoubleValue0) + TEXT(", ") + FString::Printf(TEXT("%lf"), FieldDoubleValue1) + TEXT(", ") + FString::Printf(TEXT("%lf"), FieldDoubleValue2);
			//OutMetaData.Add(TEXT("ColorRGBDouble"), FieldValue);
			break;

		case CT_ATTRIB_REVERSE_COLORID:
		case CT_ATTRIB_INITIAL_FILTER:
		case CT_ATTRIB_ORIGINAL_SURF:
		case CT_ATTRIB_LINKMANAGER_BRANCH_FACE:
		case CT_ATTRIB_LINKMANAGER_PMI:
		case CT_ATTRIB_NULL:
		case CT_ATTRIB_MEASURE_VALIDATION_ATTRIBUTE:
			break;

		case CT_ATTRIB_INTEGER_VALIDATION_ATTRIBUTE:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_INTEGER_VALIDATION_NAME, FieldName) != IO_OK) break;
			if (CT_CURRENT_ATTRIB_IO::AskIntField(ITH_INTEGER_VALIDATION_VALUE, FieldIntValue) != IO_OK) break;
			OutMetaData.Add(FieldName.toUnicode(), FString::FromInt(FieldIntValue));
			break;

		case CT_ATTRIB_DOUBLE_VALIDATION_ATTRIBUTE:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_DOUBLE_VALIDATION_NAME, FieldName) != IO_OK) break;
			if (CT_CURRENT_ATTRIB_IO::AskDblField(ITH_DOUBLE_VALIDATION_VALUE, FieldDoubleValue0) != IO_OK) break;
			OutMetaData.Add(FieldName.toUnicode(), FString::Printf(TEXT("%lf"), FieldDoubleValue0));
			break;

		case CT_ATTRIB_STRING_VALIDATION_ATTRIBUTE:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_STRING_VALIDATION_NAME, FieldName) != IO_OK) break;
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_STRING_VALIDATION_VALUE, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) { break; }
			OutMetaData.Add(FieldName.toUnicode(), FieldStrValue.toUnicode());
			break;

		case CT_ATTRIB_BOUNDING_BOX:
			//ITH_BOUNDING_BOX_XMIN, ITH_BOUNDING_BOX_YMIN, ITH_BOUNDING_BOX_ZMIN, ITH_BOUNDING_BOX_XMAX, ITH_BOUNDING_BOX_YMAX, ITH_BOUNDING_BOX_ZMAX
			break;

		case CT_ATTRIB_DATABASE:
		case CT_ATTRIB_CURVE_FONT:
		case CT_ATTRIB_CURVE_WEIGHT:
		case CT_ATTRIB_COMPARE_TOPO:
		case CT_ATTRIB_MONIKER_GUID_TABLE:
		case CT_ATTRIB_MONIKER_DATA:
		case CT_ATTRIB_MONIKER_BODY_ID:
		case CT_ATTRIB_NO_INSTANCE:
			break;

		case CT_ATTRIB_GROUPNAME:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_GROUPNAME_VALUE, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) { break; }
			OutMetaData.Add(TEXT("GroupName"), FieldStrValue.toUnicode());
			break;

		case CT_ATTRIB_ANALYZE_ID:
		case CT_ATTRIB_ANALYZER_DISPLAY_MODE:
		case CT_ATTRIB_ANIMATION_ID:
		case CT_ATTRIB_PROJECTED_SURFACE_ID:
		case CT_ATTRIB_ANALYZE_LINK:
		case CT_ATTRIB_TOPO_EVENT_ID:
		case CT_ATTRIB_ADDITIVE_MANUFACTURING:
		case CT_ATTRIB_MOLDING_RESULT:
		case CT_ATTRIB_AMF_ID:
		case CT_ATTRIB_PARAMETER_LINK:
			break;

		default:
			break;
		}
	}
}

uint32 GetSize(CT_TESS_DATA_TYPE type)
{
	switch (type)
	{
	case CT_TESS_USE_DEFAULT:
		return sizeof(uint32);
	case CT_TESS_UBYTE:
		return sizeof(uint8_t);
	case CT_TESS_BYTE:
		return sizeof(int8_t);
	case CT_TESS_USHORT:
		return sizeof(int16_t);
	case CT_TESS_SHORT:
		return sizeof(uint16_t);
	case CT_TESS_UINT:
		return sizeof(uint32);
	case CT_TESS_INT:
		return sizeof(int32);
	case CT_TESS_ULONG:
		return sizeof(uint64);
	case CT_TESS_LONG:
		return sizeof(int64);
	case CT_TESS_FLOAT:
		return sizeof(float);
	case CT_TESS_DOUBLE:
		return sizeof(double);
	}
	return 0;
}

}

#endif // CAD_INTERFACE
