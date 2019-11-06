// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "CoreTechFileParser.h"

#ifdef CAD_INTERFACE


#include "CADData.h"
#include "CADOptions.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Containers/Queue.h"
#include "CoreTechTypes.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/FileManager.h"
#include "Internationalization/Text.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#define SGSIZE 100000
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



uint32 GetFaceTessellation(CT_OBJECT_ID FaceID, TArray<FTessellationData>& FaceTessellationSet, int32& OutRawDataSize, const float ScaleFactor)
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
		OutRawDataSize = 0;
		return 0;
	}

	int32 Index = FaceTessellationSet.Emplace();
	FTessellationData& Tessellation = FaceTessellationSet[Index];
	if (TexCoordArray != nullptr)
	{
		switch (TexCoordType)
		{
		case CT_TESS_FLOAT:
			ScaleUV<float>(FaceID, TexCoordArray, VertexCount, ScaleFactor);
			break;
		case CT_TESS_DOUBLE:
			ScaleUV<double>(FaceID, TexCoordArray, VertexCount, (double)ScaleFactor);
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

	Tessellation.VertexArray.Empty(Tessellation.VertexCount * Tessellation.SizeOfVertexType);
	Tessellation.NormalArray.Empty(Tessellation.NormalCount * Tessellation.SizeOfNormalType);
	Tessellation.IndexArray.Empty(Tessellation.IndexCount * Tessellation.SizeOfIndexType);
	Tessellation.TexCoordArray.Empty(Tessellation.TexCoordCount * Tessellation.SizeOfTexCoordType);

	Tessellation.VertexArray.Append((uint8*) VertexArray, 3 * Tessellation.VertexCount * Tessellation.SizeOfVertexType);
	Tessellation.NormalArray.Append((uint8*)NormalArray, 3 * Tessellation.NormalCount * Tessellation.SizeOfNormalType);
	Tessellation.IndexArray.Append((uint8*)IndexArray, Tessellation.IndexCount * Tessellation.SizeOfIndexType);

	if (TexCoordArray)
	{
		Tessellation.TexCoordArray.Append((uint8*)TexCoordArray, 2 * Tessellation.TexCoordCount * Tessellation.SizeOfTexCoordType);
	}

	OutRawDataSize = Tessellation.VertexArray.Num() + Tessellation.NormalArray.Num() + Tessellation.IndexArray.Num() + Tessellation.TexCoordArray.Num();

	return (uint32)Tessellation.IndexCount / 3;
}


void GetCTObjectDisplayDataIds(CT_OBJECT_ID ObjectID, FObjectDisplayDataId& Material)
{
	if (CT_OBJECT_IO::SearchAttribute(ObjectID, CT_ATTRIB_MATERIALID) == IO_OK)
	{
		CT_UINT32 MaterialId = 0;
		if (CT_CURRENT_ATTRIB_IO::AskIntField(ITH_MATERIALID_VALUE, MaterialId) == IO_OK && MaterialId > 0)
		{
			Material.MaterialId = (uint32)MaterialId;
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
			Material.ColorHId = BuildFastColorHash(ColorId, alpha);
		}
	}
}

int32 BuildColorHash(CT_COLOR CtColor, uint8 Alpha)
{
	FColor Color(CtColor[0], CtColor[1], CtColor[2], Alpha);
	return BuildColorHash(Color);
}

int32 BuildColorHash(uint32 ColorHId)
{
	uint32 ColorId;
	uint8 Alpha;
	UnhashFastColorHash(ColorHId, ColorId, Alpha);

	// Ref. BaseHelper.cpp getColorData
	CT_COLOR CtColor = { 200, 200, 200 };
	if (ColorId > 0)
	{
		CT_MATERIAL_IO::AskIndexedColor((CT_OBJECT_ID) ColorId, CtColor);
	}
	return BuildColorHash(CtColor, Alpha);
}

int32 BuildMaterialHash(uint32 MaterialId)
{
	FCADMaterial OutMaterial;
	bool bReturn = GetMaterial(MaterialId, OutMaterial);
	if (!bReturn)
	{
		return 0;
	}
	return BuildMaterialHash(OutMaterial);
}

void GetColor(uint32 ColorHash, FColor& OutColor)
{
	uint32 ColorId;
	uint8 Alpha;
	UnhashFastColorHash(ColorHash, ColorId, Alpha);

	CT_COLOR CtColor = { 200, 200, 200 };
	if (ColorId > 0)
	{
		CT_MATERIAL_IO::AskIndexedColor((CT_OBJECT_ID)ColorId, CtColor);
	}

	OutColor.R = CtColor[0];
	OutColor.G = CtColor[1];
	OutColor.B = CtColor[2];
	OutColor.A = Alpha;
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

	OutMaterial.MaterialId = MaterialId;
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



void GetBodiesMaterials(TArray<CT_OBJECT_ID>& BodySet, TMap<uint32, uint32>& MaterialIdToHash, bool bPreferPartData)
{
	CT_MATERIAL_ID BodyCtMaterialId = 0;
	for (int Index = 0; Index < BodySet.Num(); Index++)
	{
		FObjectDisplayDataId BodyMaterialId;
		GetCTObjectDisplayDataIds(BodySet[Index], BodyMaterialId);

		// Loop through the face of the first body and collect material data
		CT_LIST_IO FaceList;
		CT_BODY_IO::AskFaces(BodySet[Index], FaceList);
		FaceList.IteratorInitialize();

		CT_OBJECT_ID FaceID;
		while ((FaceID = FaceList.IteratorIter()) != 0)
		{
			FTessellationData Tessellation;
			FObjectDisplayDataId FaceMaterialId;
			GetCTObjectDisplayDataIds(FaceID, FaceMaterialId);
			SetFaceMainMaterial(FaceMaterialId, BodyMaterialId, MaterialIdToHash, Tessellation);
		}
	}
}


void SetFaceMainMaterial(FObjectDisplayDataId& InFaceMaterial, FObjectDisplayDataId& InBodyMaterial, TMap<uint32, uint32>& MaterialIdToMaterialHashMap, FTessellationData& OutFaceTessellations)
{
	uint32 FaceMaterialId = 0; // either CT Material ID or CT Color/Alpha ID (ColorId & Alpha << 24)
	uint32 BodyMaterialId = 0; // either CT Material ID or CT Color/Alpha ID (ColorId & Alpha << 24)

	uint32 FaceMaterialHash = 0;
	uint32 BodyMaterialHash = 0;

	if (InFaceMaterial.MaterialId > 0)
	{
		FaceMaterialId = InFaceMaterial.MaterialId;
		if (uint32* MHash = MaterialIdToMaterialHashMap.Find(InFaceMaterial.MaterialId))
		{
			FaceMaterialHash = *MHash;
		}
		else
		{
			FaceMaterialHash = BuildMaterialHash(InFaceMaterial.MaterialId);
			MaterialIdToMaterialHashMap.Add(InFaceMaterial.MaterialId, FaceMaterialHash);
		}
	}
	else if (InFaceMaterial.ColorHId > 0)
	{
		FaceMaterialId = InFaceMaterial.ColorHId;
		if (uint32* MHash = MaterialIdToMaterialHashMap.Find(InFaceMaterial.ColorHId))
		{
			FaceMaterialHash = *MHash;
		}
		else
		{
			FaceMaterialHash = BuildColorHash(InFaceMaterial.ColorHId);
			MaterialIdToMaterialHashMap.Add(InFaceMaterial.ColorHId, FaceMaterialHash);
		}
	}

	if (InBodyMaterial.MaterialId > 0)
	{
		BodyMaterialId = InBodyMaterial.MaterialId;
		if (uint32* MHash = MaterialIdToMaterialHashMap.Find(InBodyMaterial.MaterialId))
		{
			BodyMaterialHash = *MHash;
		}
		else
		{
			BodyMaterialHash = BuildMaterialHash(InBodyMaterial.MaterialId);
			MaterialIdToMaterialHashMap.Add(InBodyMaterial.MaterialId, BodyMaterialHash);
		}
	}
	else if (InBodyMaterial.ColorHId > 0)
	{
		BodyMaterialId = InBodyMaterial.ColorHId;
		if (uint32* MHash = MaterialIdToMaterialHashMap.Find(InBodyMaterial.ColorHId))
		{
			BodyMaterialHash = *MHash;
		}
		else
		{
			BodyMaterialHash = BuildColorHash(InBodyMaterial.ColorHId);
			MaterialIdToMaterialHashMap.Add(InBodyMaterial.ColorHId, BodyMaterialHash);
		}
	}

	uint32 MaterialId = 0;
	uint32 MaterialHash = 0;

	// set output
	if (true)
	{
		if (BodyMaterialId)
		{
			MaterialId = BodyMaterialId;
			MaterialHash = BodyMaterialHash;
		} 
		else
		{
			MaterialId = FaceMaterialId;
			MaterialHash = FaceMaterialHash;
		}
	}
	else
	{
		if (FaceMaterialId)
		{
			MaterialId = FaceMaterialId;
			MaterialHash = FaceMaterialHash;
		}
		else
		{
			MaterialId = BodyMaterialId;
			MaterialHash = BodyMaterialHash;
		}
	}

	OutFaceTessellations.MaterialId = MaterialId;
	OutFaceTessellations.MaterialHash = MaterialHash;
}

int32 BuildColorHash(uint32 ColorId, uint8 Alpha)
{
	// Ref. BaseHelper.cpp getColorData
	CT_COLOR CtColor = { 200, 200, 200 };
	if (ColorId > 0)
	{
		CT_MATERIAL_IO::AskIndexedColor(ColorId, CtColor);
	}
	return BuildColorHash(CtColor, Alpha);
}

uint32 GetStaticMeshUuid(const TCHAR* OutSgFile, const int32 BodyId)
{
	uint32 BodyUUID = GetTypeHash(OutSgFile);
	FString BodyStr = TEXT("B ") + FString::FromInt(BodyId);
	BodyUUID = HashCombine(BodyUUID, GetTypeHash(*BodyStr));
	return BodyUUID;
}

void FCoreTechFileParser::ExportFileSceneGraph()
{
	FFileHelper::SaveStringArrayToFile(SceneGraphDescription, *FPaths::Combine(CachePath, TEXT("scene"), SceneGraphFile + TEXT(".sg"))); 
	SceneGraphDescription.Empty();
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
		FCADMaterial Material;
		bool bReturn = GetMaterial(MaterialId, Material);
		if (!bReturn)
		{
			break;
		}

		int32 MaterialHash = BuildMaterialHash(Material);

		SceneGraphDescription.Add(FString::Printf(TEXT("%d %s"), MaterialId, *Material.MaterialName));        // COLORSETLINE 3
		SceneGraphDescription.Add(FString::Printf(TEXT("%d %d %d %d %d %d %d %d %d %d %d %f %f %f %s"), 
			MaterialId, MaterialHash, 
			Material.Diffuse.R, Material.Diffuse.G, Material.Diffuse.B, 
			Material.Ambient.R, Material.Ambient.G, Material.Ambient.B, 
			Material.Specular.R, Material.Specular.G, Material.Specular.B, 
			Material.Shininess, Material.Transparency, Material.Reflexion, *Material.TextureName));
		MaterialIdToMaterialHashMap.Add(MaterialId, MaterialHash);
		MaterialId++;
	}
	SceneGraphDescription.Add(TEXT(""));
}

FCoreTechFileParser::FCoreTechFileParser(const FString& InCADFullPath, const FString& InCachePath, const FImportParameters& ImportParams, bool bInPreferBodyData, bool bScaleUVMap)
	: CachePath(InCachePath)
	, FullPath(InCADFullPath)
	, bNeedSaveCTFile(false)
	, bPreferBodyData(bInPreferBodyData)
	, bScaleUVMap(bScaleUVMap)
	, ImportParameters(ImportParams)
{
	CTIdToRawLineMap.Reserve(EXTREFNUM);
	CTKIO_InitializeKernel(ImportParameters.MetricUnit);
}

void GetRawDataFileExternalRef(const FString& InRawDataFile, TSet<FString>& ExternalReferences)
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
		ExternalReferences.Add(SGDescription[Ref]);
	}
}

EProcessState FCoreTechFileParser::ProcessFile()
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
		return FileNotFound;
	}

	FFileStatData FileStatData = IFileManager::Get().GetStatData(*FullPath);
	uint32 FileHash = GetFileHash(CADFile, FileStatData, FileConfiguration, ImportParameters);

	SceneGraphFile = FString::Printf(TEXT("UEx%08x"), FileHash);

	FString RawDataFile = FPaths::Combine(CachePath, TEXT("scene"), SceneGraphFile + TEXT(".sg"));
	FString CTFile = FPaths::Combine(CachePath, TEXT("cad"), SceneGraphFile + TEXT(".ct"));

	uint32 MeshFileHash = GetGeomFileHash(FileHash, ImportParameters);
	MeshFile = FString::Printf(TEXT("UEx%08x"), MeshFileHash);
	RawDataGeom = FPaths::Combine(CachePath, TEXT("mesh"), MeshFile + TEXT(".gm"));

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

	if (!bNeedToProceed)
	{
		// The file has been yet proceed, get ExternalRef
		GetRawDataFileExternalRef(RawDataFile, ExternalRefSet);
		return ProcessOk;
	}

	// Process the file
	return ReadFileWithKernelIO();
}




EProcessState FCoreTechFileParser::ReadFileWithKernelIO()
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
			return ProcessFailed;
		}
		Result = CT_KERNEL_IO::LoadFile(*FullPath, MainId, CTImportOption | CT_LOAD_FLAGS_LOAD_EXTERNAL_REF);
	}

	if (Result != IO_OK && Result != IO_OK_MISSING_LICENSES)
	{
		CT_KERNEL_IO::UnloadModel();
		return ProcessFailed;
	}

	Repair(MainId, ImportParameters.StitchingTechnique);
	SetCoreTechTessellationState(ImportParameters);

	const CT_OBJECT_TYPE TypeSet[] = { CT_INSTANCE_TYPE, CT_ASSEMBLY_TYPE, CT_PART_TYPE, CT_COMPONENT_TYPE, CT_BODY_TYPE, CT_UNLOADED_COMPONENT_TYPE, CT_UNLOADED_ASSEMBLY_TYPE, CT_UNLOADED_PART_TYPE };
	uint32 NbElements[8], NbTotal = 10;
	for (int32 index = 0; index < 8; index++)
	{
		CT_KERNEL_IO::AskNbObjectsType(NbElements[index], TypeSet[index]);
		NbTotal += NbElements[index];
	}

	{
		TArray<uint8> RawData;
		RawData.Append((uint8*)&NbElements[4], sizeof(uint32));
		FFileHelper::SaveArrayToFile(RawData, *RawDataGeom, &IFileManager::Get());
	}

	SceneGraphDescription.Reset(NbTotal * 20);
	ExternalRefSet.Empty(NbTotal);
	CTIdToRawLineMap.Reset();

	// Header
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
	MaterialIdToMaterialHashMap.Empty(MaterialNum);

	ReadColor();
	ReadMaterial();

	// Parse the file
	bool Ret = ReadNode(MainId);
	// End of parsing

	if (bNeedSaveCTFile)
	{
		CT_LIST_IO ObjectList;
		ObjectList.PushBack(MainId);

		CT_KERNEL_IO::SaveFile(ObjectList, *FPaths::Combine(CachePath, TEXT("cad"), SceneGraphFile+TEXT(".ct")), L"Ct");
	}

	CT_KERNEL_IO::UnloadModel();

	if (!Ret)
	{
		return ProcessFailed;
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

	return ProcessOk;
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
	CT_COMPONENT_IO::AskChildren(ComponentId, Children);

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

	NodeConfiguration.Empty(); // #ueent_CAD
	CT_OBJECT_TYPE type;
	CT_OBJECT_IO::AskType(ReferenceNodeId, type);
	if (type == CT_UNLOADED_PART_TYPE || type == CT_UNLOADED_COMPONENT_TYPE || type == CT_UNLOADED_ASSEMBLY_TYPE)
	{
		CT_STR ComponentFile, FileType;
		CT_COMPONENT_IO::AskExternalDefinition(ReferenceNodeId, ComponentFile, FileType);
		FString ExternalRef = FPaths::GetCleanFilename(ComponentFile.toUnicode());
		if(!NodeConfiguration.IsEmpty())
		{
			ExternalRef += TEXT("|") + NodeConfiguration;
		}

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

uint32 GetBodiesTessellations(TArray<CT_OBJECT_ID>& BodySet, TArray<FTessellationData>& FaceTessellationSet, TMap<uint32, uint32>& MaterialIdToMaterialHash, int32& OutRawDataSize, const float ScaleFactor)
{
	uint32 FaceSize = GetBodiesFaceSetNum(BodySet);

	// Allocate memory space for tessellation data
	FaceTessellationSet.Reserve(FaceSize);

	uint32 GlobalTriangleCount = 0;

	CT_MATERIAL_ID BodyCtMaterialId = 0;
	for (int Index = 0; Index < BodySet.Num(); Index++)
	{
		FObjectDisplayDataId BodyMaterial;
		GetCTObjectDisplayDataIds(BodySet[Index], BodyMaterial);

		// Loop through the face of the first body and collect all tessellation data
		CT_LIST_IO FaceList;
		CT_BODY_IO::AskFaces(BodySet[Index], FaceList);
		FaceList.IteratorInitialize();

		int32 LastIndex = FaceTessellationSet.Num();
		int32 FaceIndex = LastIndex;

		CT_OBJECT_ID FaceID;
		while ((FaceID = FaceList.IteratorIter()) != 0)
		{
			int32 FaceRawSize = 0;
			uint32 TriangleCount = GetFaceTessellation(FaceID, FaceTessellationSet, FaceRawSize, ScaleFactor);

			if (TriangleCount == 0)
			{
				continue;
			}

			GlobalTriangleCount += TriangleCount;
			OutRawDataSize += (FaceRawSize + ECTTessellationLine::LastLine * sizeof(uint32));
			FObjectDisplayDataId FaceMaterial;
			GetCTObjectDisplayDataIds(FaceID, FaceMaterial);
			SetFaceMainMaterial(FaceMaterial, BodyMaterial, MaterialIdToMaterialHash, FaceTessellationSet[FaceIndex]);
			FaceIndex++;
		}

		int32 FaceNum = FaceTessellationSet.Num() - LastIndex;
		for (FaceIndex = LastIndex; FaceIndex < FaceTessellationSet.Num(); FaceIndex++)
		{
			FaceTessellationSet[FaceIndex].BodyId = (uint32)BodySet[Index];
			FaceTessellationSet[FaceIndex].BodyFaceNum = FaceNum;
		}
	}
	return GlobalTriangleCount;
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
	TArray<FTessellationData> FaceTessellationSet;

	uint32 BodyUuId = GetStaticMeshUuid(*SceneGraphFile, BodyId);
	double ScaleFactor = 0.1;
                
	int32 BodyRawDataSize = 0;
	uint32 NbTriangles = GetBodiesTessellations(BodySet, FaceTessellationSet, MaterialIdToMaterialHashMap, BodyRawDataSize, ScaleFactor);
	TArray<uint8> GlobalRawData;

	GlobalRawData.Reserve(BodyRawDataSize);

	int32 FaceNum = FaceTessellationSet.Num();
	for (FTessellationData& Tessellation : FaceTessellationSet)
	{
		Tessellation.BodyUuId = BodyUuId;
		Tessellation.BodyFaceNum = FaceNum;
		WriteTessellationInRawData(Tessellation, GlobalRawData);
	}

	bool bWriteOk = FFileHelper::SaveArrayToFile(GlobalRawData, *RawDataGeom, &IFileManager::Get(), FILEWRITE_Append);
	ensure(bWriteOk);
	GlobalRawData.Empty();

	CT_LIST_IO ObjectList;
	ObjectList.PushBack(BodyId);
	FString BodyFile = FString::Printf(TEXT("UEx%08x"), BodyUuId);
	CT_KERNEL_IO::SaveFile(ObjectList, *FPaths::Combine(CachePath, TEXT("body"), BodyFile + TEXT(".ct")), L"Ct");

	FString MapCTId;
	MapCTId.Reserve((MaterialIdToMaterialHashMap.Num()+1) * 22);

	MapCTId.Append(TEXT("materialMap "));
	for (auto Material : MaterialIdToMaterialHashMap)
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
	const FString ConfigName = TEXT("Configuration Name");
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
				MaterialIdToMaterialHashMap.Add(ColorUuId, ColorHash);
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
			if(ConfigName== FieldName.toUnicode())
			{
				NodeConfiguration = FieldStrValue.toUnicode();
			}
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
