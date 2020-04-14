// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreTechFileParser.h"

#ifdef CAD_INTERFACE

#include "CADData.h"
#include "CADOptions.h"

#include "CoreTechTypes.h"
#include "DatasmithUtils.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/FileManager.h"
#include "Internationalization/Text.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Templates/TypeHash.h"

namespace CADLibrary 
{

namespace {

	double Distance(const CT_COORDINATE& Point1, const CT_COORDINATE& Point2)
	{
		return sqrt((Point2.xyz[0] - Point1.xyz[0]) * (Point2.xyz[0] - Point1.xyz[0]) + (Point2.xyz[1] - Point1.xyz[1]) * (Point2.xyz[1] - Point1.xyz[1]) + (Point2.xyz[2] - Point1.xyz[2]) * (Point2.xyz[2] - Point1.xyz[2]));
	};

	void ScaleUV(CT_OBJECT_ID FaceID, TArray<FVector2D>& TexCoordArray, float Scale)
	{
		float VMin, VMax, UMin, UMax;
		VMin = UMin = HUGE_VALF;
		VMax = UMax = -HUGE_VALF;

		for (const FVector2D& TexCoord : TexCoordArray)
		{
			UMin = FMath::Min(TexCoord[0], UMin);
			UMax = FMath::Max(TexCoord[0], UMax);
			VMin = FMath::Min(TexCoord[1], VMin);
			VMax = FMath::Max(TexCoord[1], VMax);
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

		float DeltaU = (PuMax - PuMin) / (NbIsoCurves - 1);
		float DeltaV = (PvMax - PvMin) / (NbIsoCurves - 1);
		float U = PuMin, V = PvMin;

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
		float LengthU[NbIsoCurves];
		float LengthUMin = HUGE_VAL;
		float LengthUMax = 0;
		float LengthUMed = 0;

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
		float LengthV[NbIsoCurves];
		float LengthVMin = HUGE_VAL;
		float LengthVMax = 0;
		float LengthVMed = 0;

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
		case CT_TORUS_TYPE:
			Swap(LengthUMed, LengthVMed);
			break;
		case CT_S_REVOL_TYPE:
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
		float VScale = Scale * LengthVMed * 1 / (VMax - VMin) / 100;
		float UScale = Scale * LengthUMed * 1 / (UMax - UMin) / 100;

		for (FVector2D& TexCoord : TexCoordArray)
		{
			TexCoord[0] *= UScale;
			TexCoord[1] *= VScale;
		}
	}
}

FString AsFString(CT_STR CtName)
{
	return CtName.IsEmpty() ? FString() : CtName.toUnicode();
};

uint32 GetFileHash(const CADLibrary::FFileDescription& FileDescription, const FImportParameters& ImportParam)
{
	FFileStatData FileStatData = IFileManager::Get().GetStatData(*FileDescription.Path);

	int64 FileSize = FileStatData.FileSize;
	FDateTime ModificationTime = FileStatData.ModificationTime;

	uint32 FileHash = GetTypeHash(FileDescription);
	FileHash = HashCombine(FileHash, GetTypeHash(FileSize));
	FileHash = HashCombine(FileHash, GetTypeHash(ModificationTime));
	FileHash = HashCombine(FileHash, GetTypeHash(ImportParam.StitchingTechnique));

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

template<typename ValueType>
void FillArrayOfVector(int32 ElementCount, void* InCTValueArray, FVector* OutValueArray)
{
	ValueType* Values = (ValueType*)InCTValueArray;
	for (int Indice = 0; Indice < ElementCount; ++Indice)
	{
		OutValueArray[Indice].Set((float)Values[Indice * 3], (float)Values[Indice * 3 + 1], (float)Values[Indice * 3 + 2]);
	}
}

template<typename ValueType>
void FillArrayOfVector2D(int32 ElementCount, void* InCTValueArray, FVector2D* OutValueArray)
{
	ValueType* Values = (ValueType*)InCTValueArray;
	for (int Indice = 0; Indice < ElementCount; ++Indice)
	{
		OutValueArray[Indice].Set((float)Values[Indice * 2], (float)Values[Indice * 2 + 1]);
	}
}

template<typename ValueType>
void FillArrayOfInt(int32 ElementCount, void* InCTValueArray, int32* OutValueArray)
{
	ValueType* Values = (ValueType*)InCTValueArray;
	for (int Indice = 0; Indice < ElementCount; ++Indice)
	{
		OutValueArray[Indice] = (int32)Values[Indice];
	}
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
	Tessellation.IndexArray.SetNum(IndexCount);

	switch (IndexType)
	{
	case CT_TESS_UBYTE:
		FillArrayOfInt<uint8>(IndexCount, IndexArray, Tessellation.IndexArray.GetData());
		break;
	case CT_TESS_USHORT:
		FillArrayOfInt<uint16>(IndexCount, IndexArray, Tessellation.IndexArray.GetData());
		break;
	case CT_TESS_UINT:
		FillArrayOfInt<uint32>(IndexCount, IndexArray, Tessellation.IndexArray.GetData());
		break;
	}

	Tessellation.VertexArray.SetNum(VertexCount);
	switch (VertexType)
	{
	case CT_TESS_FLOAT:
		FillArrayOfVector<float>(VertexCount, VertexArray, Tessellation.VertexArray.GetData());
		break;
	case CT_TESS_DOUBLE:
		FillArrayOfVector<double>(VertexCount, VertexArray, Tessellation.VertexArray.GetData());
		break;
	}

	Tessellation.NormalArray.SetNum(NormalCount);
	switch (NormalType)
	{
	case CT_TESS_BYTE:
		Tessellation.NormalArray.SetNumZeroed(NormalCount);
		break;
	case CT_TESS_SHORT:
	{
		int8* InCTValueArray = (int8*)NormalArray;
		for (CT_UINT32 Indice = 0; Indice < NormalCount; ++Indice)
		{
			Tessellation.NormalArray[Indice].Set(((float)InCTValueArray[Indice]) / 255.f, ((float)InCTValueArray[Indice + 1]) / 255.f, ((float)InCTValueArray[Indice + 2]) / 255.f);
		}
		break;
	}
	case CT_TESS_FLOAT:
		FillArrayOfVector<float>(NormalCount, NormalArray, Tessellation.NormalArray.GetData());
		break;
	}

	if (TexCoordArray)
	{
		Tessellation.TexCoordArray.SetNum(VertexCount);
		switch (TexCoordType)
		{
		case CT_TESS_SHORT:
		{
			int8* InCTValueArray = (int8*)TexCoordArray;
			for (CT_UINT32 Indice = 0; Indice < VertexCount; ++Indice)
			{
				Tessellation.TexCoordArray[Indice].Set(((float)InCTValueArray[Indice]) / 255.f, ((float)InCTValueArray[Indice + 1]) / 255.f);
			}
			break;
		}
		case CT_TESS_FLOAT:
			FillArrayOfVector2D<float>(VertexCount, TexCoordArray, Tessellation.TexCoordArray.GetData());
			break;
		case CT_TESS_DOUBLE:
			FillArrayOfVector2D<double>(VertexCount, TexCoordArray, Tessellation.TexCoordArray.GetData());
			break;
		}
	}

	if (ImportParams.bScaleUVMap && Tessellation.TexCoordArray.Num() != 0)
	{
		ScaleUV(FaceID, Tessellation.TexCoordArray, (float) ImportParams.ScaleFactor);
	}

	return Tessellation.IndexArray.Num() / 3;
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
	if (FArchiveMaterial* NewMaterial = SceneGraphArchive.MaterialHIdToMaterial.Find(MaterialId))
	{
		return *NewMaterial;
	}

	FArchiveMaterial& NewMaterial = SceneGraphArchive.MaterialHIdToMaterial.Emplace(MaterialId, MaterialId);
	GetMaterial(MaterialId, NewMaterial.Material);
	NewMaterial.UEMaterialName = BuildMaterialName(NewMaterial.Material);
	return NewMaterial;
}

FArchiveColor& FCoreTechFileParser::FindOrAddColor(uint32 ColorHId)
{
	if (FArchiveColor* Color = SceneGraphArchive.ColorHIdToColor.Find(ColorHId))
	{
		return *Color;
	}

	FArchiveColor& NewColor = SceneGraphArchive.ColorHIdToColor.Add(ColorHId, ColorHId);
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

	OutMaterial.MaterialName = AsFString(CtName);
	OutMaterial.Diffuse = FColor(CtDiffuse[0], CtDiffuse[1], CtDiffuse[2], 255);
	OutMaterial.Ambient = FColor(CtAmbient[0], CtAmbient[1], CtAmbient[2], 255);
	OutMaterial.Specular = FColor(CtSpecular[0], CtSpecular[1], CtSpecular[2], 255);
	OutMaterial.Shininess = CtShininess;
	OutMaterial.Transparency = CtTransparency;
	OutMaterial.Reflexion = CtReflexion;
	OutMaterial.TextureName = AsFString(CtTextureName);

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
	SceneGraphArchive.SerializeMockUp(*FPaths::Combine(CachePath, TEXT("scene"), SceneGraphArchive.ArchiveFileName + TEXT(".sg")));
}

void FCoreTechFileParser::ExportMeshArchiveFile()
{
	SerializeBodyMeshSet(*MeshArchiveFilePath, BodyMeshes);
}

void FCoreTechFileParser::LoadSceneGraphArchive(const FString& SGFile)
{
	SceneGraphArchive.DeserializeMockUpFile(*SGFile);
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

		FArchiveMaterial& MaterialObject = SceneGraphArchive.MaterialHIdToMaterial.Emplace(MaterialId, MaterialId);
		MaterialObject.UEMaterialName = BuildMaterialName(Material);
		MaterialObject.Material = Material; 

		MaterialId++;
	}
}

FCoreTechFileParser::FCoreTechFileParser(const FImportParameters& ImportParams, const FString& EnginePluginsPath, const FString& InCachePath)
	: CachePath(InCachePath)
	, ImportParameters(ImportParams)
{
	CTKIO_InitializeKernel(ImportParameters.MetricUnit, *EnginePluginsPath);
}

bool FCoreTechFileParser::FindFile(FFileDescription& File)
{
	FString FileName = File.Name;

	FString FilePath = FPaths::GetPath(File.Path);
	FString RootFilePath = File.MainCadFilePath;

	// Basic case: FilePath is, or is in a sub-folder of, RootFilePath
	if (FilePath.StartsWith(RootFilePath))
	{
		return IFileManager::Get().FileExists(*File.Path);
	}

	// Advance case: end of FilePath is in a upper-folder of RootFilePath
	// e.g.
	// FilePath = D:\\data temp\\Unstructured project\\Folder2\\Added_Object.SLDPRT
	//                                                 ----------------------------
	// RootFilePath = D:\\data\\CAD Files\\SolidWorks\\p033 - Unstructured project\\Folder1
	//                ------------------------------------------------------------
	// NewPath = D:\\data\\CAD Files\\SolidWorks\\p033 - Unstructured project\\Folder2\\Added_Object.SLDPRT
	TArray<FString> RootPaths;
	RootPaths.Reserve(30);
	do
	{
		RootFilePath = FPaths::GetPath(RootFilePath);
		RootPaths.Emplace(RootFilePath);
	} while (!FPaths::IsDrive(RootFilePath) && !RootFilePath.IsEmpty());

	TArray<FString> FilePaths;
	FilePaths.Reserve(30);
	FilePaths.Emplace(FileName);
	while (!FPaths::IsDrive(FilePath) && !FilePath.IsEmpty())
	{
		FString FolderName = FPaths::GetCleanFilename(FilePath);
		FilePath = FPaths::GetPath(FilePath);
		FilePaths.Emplace(FPaths::Combine(FolderName, FilePaths.Last()));
	};

	for(int32 IndexFolderPath = 0; IndexFolderPath < RootPaths.Num(); IndexFolderPath++)
	{
		for (int32 IndexFilePath = 0; IndexFilePath < FilePaths.Num(); IndexFilePath++)
		{
			FString NewFilePath = FPaths::Combine(RootPaths[IndexFolderPath], FilePaths[IndexFilePath]);
			if(IFileManager::Get().FileExists(*NewFilePath))
			{
				File.Path = NewFilePath;
				return true;
			};
		}
	}

	// Last case: the FilePath is elsewhere and the file exist
	// A Warning is launch because the file could be expected to not be loaded
	if(IFileManager::Get().FileExists(*File.Path))
	{
		WarningMessages.Add(FString::Printf(TEXT("File %s has been loaded but seems to be localize in an external folder: %s."), *FileName, *FPaths::GetPath(FileDescription.Path)));
		return true;
	}

	return false;
}


FCoreTechFileParser::EProcessResult FCoreTechFileParser::ProcessFile(const FFileDescription& InFileDescription)
{
	FileDescription = InFileDescription;

	if (!FindFile(FileDescription))
	{
		return EProcessResult::FileNotFound;
	}

	uint32 FileHash = GetFileHash(FileDescription, ImportParameters);

	SceneGraphArchive.ArchiveFileName = FString::Printf(TEXT("UEx%08x"), FileHash);

	FString SceneGraphArchiveFilePath = FPaths::Combine(CachePath, TEXT("scene"), SceneGraphArchive.ArchiveFileName + TEXT(".sg"));
	FString CTFilePath = FPaths::Combine(CachePath, TEXT("cad"), SceneGraphArchive.ArchiveFileName + TEXT(".ct"));

	uint32 MeshFileHash = GetGeomFileHash(FileHash, ImportParameters);
	MeshArchiveFile = FString::Printf(TEXT("UEx%08x"), MeshFileHash);
	MeshArchiveFilePath = FPaths::Combine(CachePath, TEXT("mesh"), MeshArchiveFile + TEXT(".gm"));

	bool bNeedToProceed = true;
#ifndef IGNORE_CACHE
	if (ImportParameters.bEnableCacheUsage && IFileManager::Get().FileExists(*SceneGraphArchiveFilePath))
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
			FileDescription.ReplaceByKernelIOBackup(CTFilePath);
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

	CT_KERNEL_IO::UnloadModel();

	SceneGraphArchive.FullPath = FileDescription.Path;
	SceneGraphArchive.CADFileName = FileDescription.Name;

	CT_FLAGS CTImportOption = SetCoreTechImportOption(FileDescription.Extension);

	FString LoadOption;
	CT_UINT32 NumberOfIds = 1;

	if (!FileDescription.Configuration.IsEmpty())
	{
		NumberOfIds = CT_KERNEL_IO::AskFileNbOfIds(*FileDescription.Path);
		if (NumberOfIds > 1)
		{
			CT_UINT32 ActiveConfig = CT_KERNEL_IO::AskFileActiveConfig(*FileDescription.Path);
			for (CT_UINT32 i = 0; i < NumberOfIds; i++)
			{
				CT_STR ConfValue = CT_KERNEL_IO::AskFileIdIthName(*FileDescription.Path, i);
				if (FileDescription.Configuration == AsFString(ConfValue)) {
					ActiveConfig = i;
					break;
				}
			}

			CTImportOption |= CT_LOAD_FLAGS_READ_SPECIFIC_OBJECT;
			LoadOption = FString::FromInt((int32) ActiveConfig);
		}
	}

	Result = CT_KERNEL_IO::LoadFile(*FileDescription.Path, MainId, CTImportOption, 0, *LoadOption);
	if (Result == IO_ERROR_EMPTY_ASSEMBLY)
	{
		CT_KERNEL_IO::UnloadModel();
		Result = CT_KERNEL_IO::LoadFile(*FileDescription.Path, MainId, CTImportOption | CT_LOAD_FLAGS_LOAD_EXTERNAL_REF);
	}

	// the file is loaded but it's empty, so no data is generate
	if (Result == IO_ERROR_EMPTY_ASSEMBLY)
	{
		CT_KERNEL_IO::UnloadModel();
		WarningMessages.Emplace(FString::Printf(TEXT("File %s has been loaded but no assembly has been detected."), *FileDescription.Name));
		ExportSceneGraphFile();
		return EProcessResult::ProcessOk;
	}

	if (Result != IO_OK && Result != IO_OK_MISSING_LICENSES)
	{
		CT_KERNEL_IO::UnloadModel();
		return EProcessResult::ProcessFailed;
	}

	SetCoreTechTessellationState(ImportParameters);

	SceneGraphArchive.FullPath = FileDescription.Path;
	SceneGraphArchive.CADFileName = FileDescription.Name;

	const CT_OBJECT_TYPE TypeSet[] = { CT_INSTANCE_TYPE, CT_ASSEMBLY_TYPE, CT_PART_TYPE, CT_COMPONENT_TYPE, CT_BODY_TYPE, CT_UNLOADED_COMPONENT_TYPE, CT_UNLOADED_ASSEMBLY_TYPE, CT_UNLOADED_PART_TYPE};
	enum EObjectTypeIndex : uint8	{ CT_INSTANCE_INDEX = 0, CT_ASSEMBLY_INDEX, CT_PART_INDEX, CT_COMPONENT_INDEX, CT_BODY_INDEX, CT_UNLOADED_COMPONENT_INDEX, CT_UNLOADED_ASSEMBLY_INDEX, CT_UNLOADED_PART_INDEX };

	uint32 NbElements[8], NbTotal = 10;
	for (int32 index = 0; index < 8; index++)
	{
		CT_KERNEL_IO::AskNbObjectsType(NbElements[index], TypeSet[index]);
		NbTotal += NbElements[index];
	}

	BodyMeshes.Reserve(NbElements[CT_BODY_INDEX]);

	SceneGraphArchive.BodySet.Reserve(NbElements[CT_BODY_INDEX]);
	SceneGraphArchive.ComponentSet.Reserve(NbElements[CT_ASSEMBLY_INDEX] + NbElements[CT_PART_INDEX] + NbElements[CT_COMPONENT_INDEX]);
	SceneGraphArchive.UnloadedComponentSet.Reserve(NbElements[CT_UNLOADED_COMPONENT_INDEX] + NbElements[CT_UNLOADED_ASSEMBLY_INDEX] + NbElements[CT_UNLOADED_PART_INDEX]);
	SceneGraphArchive.Instances.Reserve(NbElements[CT_INSTANCE_INDEX]);

	SceneGraphArchive.CADIdToBodyIndex.Reserve(NbElements[CT_BODY_INDEX]);
	SceneGraphArchive.CADIdToComponentIndex.Reserve(NbElements[CT_ASSEMBLY_INDEX] + NbElements[CT_PART_INDEX] + NbElements[CT_COMPONENT_INDEX]);
	SceneGraphArchive.CADIdToUnloadedComponentIndex.Reserve(NbElements[CT_UNLOADED_COMPONENT_INDEX] + NbElements[CT_UNLOADED_ASSEMBLY_INDEX] + NbElements[CT_UNLOADED_PART_INDEX]);
	SceneGraphArchive.CADIdToInstanceIndex.Reserve(NbElements[CT_INSTANCE_INDEX]);

	uint32 MaterialNum = GetMaterialNum();
	SceneGraphArchive.MaterialHIdToMaterial.Reserve(MaterialNum);

	ReadMaterials();

	// Parse the file
	uint32 DefaultMaterialHash = 0;
	bool bReadNodeSucceed = ReadNode(MainId, DefaultMaterialHash);
	// End of parsing

	CT_STR KernelIO_Version = CT_KERNEL_IO::AskVersion();
	if (!KernelIO_Version.IsEmpty())
	{
		SceneGraphArchive.ComponentSet[0].MetaData.Add(TEXT("KernelIOVersion"), AsFString(KernelIO_Version));
	}

	if (bNeedSaveCTFile)
	{
		CT_LIST_IO ObjectList;
		ObjectList.PushBack(MainId);

		CT_KERNEL_IO::SaveFile(ObjectList, *FPaths::Combine(CachePath, TEXT("cad"), SceneGraphArchive.ArchiveFileName + TEXT(".ct")), L"Ct");
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
		Flags |= CT_LOAD_FLAG_COMPLETE_TOPOLOGY;
		Flags |= CT_LOAD_FLAG_SEARCH_NEW_TOPOLOGY;
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
		if (int32* Index = SceneGraphArchive.CADIdToInstanceIndex.Find(NodeId))
		{
			return true;
		}
		return ReadInstance(NodeId, DefaultMaterialHash);

	case CT_ASSEMBLY_TYPE:
	case CT_PART_TYPE:
	case CT_COMPONENT_TYPE:
		if (int32* Index = SceneGraphArchive.CADIdToComponentIndex.Find(NodeId))
		{
			return true;
		}
		return ReadComponent(NodeId, DefaultMaterialHash);

	case CT_UNLOADED_ASSEMBLY_TYPE:
	case CT_UNLOADED_COMPONENT_TYPE:
	case CT_UNLOADED_PART_TYPE:
		if (int32* Index = SceneGraphArchive.CADIdToUnloadedComponentIndex.Find(NodeId))
		{
			return true;
		}
		return ReadUnloadedComponent(NodeId);

	case CT_BODY_TYPE:
		break;

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

	int32 Index = SceneGraphArchive.UnloadedComponentSet.Emplace(ComponentId);
	SceneGraphArchive.CADIdToUnloadedComponentIndex.Add(ComponentId, Index);
	ReadNodeMetaData(ComponentId, SceneGraphArchive.UnloadedComponentSet[Index].MetaData);

	SceneGraphArchive.UnloadedComponentSet[Index].FileName = AsFString(Filename);
	SceneGraphArchive.UnloadedComponentSet[Index].FileType = AsFString(FileType);

	return true;
}


void GetInstancesAndBodies(CT_OBJECT_ID InComponentId, TArray<CT_OBJECT_ID>& OutInstances, TArray<CT_OBJECT_ID>& OutBodies)
{
	CT_LIST_IO Children;
	CT_COMPONENT_IO::AskChildren(InComponentId, Children);

	int32 NbChildren = Children.Count();
	OutInstances.Empty(NbChildren);
	OutBodies.Empty(NbChildren);

	Children.IteratorInitialize();
	CT_OBJECT_ID ChildId;
	while ((ChildId = Children.IteratorIter()) != 0)
	{
		CT_OBJECT_TYPE Type;
		CT_OBJECT_IO::AskType(ChildId, Type);

		switch (Type)
		{
		case CT_INSTANCE_TYPE:
		{
			OutInstances.Add(ChildId);
			break;
		}

		case CT_BODY_TYPE:
		{
			OutBodies.Add(ChildId);
			break;
		}

		// we don't manage CURVE, POINT, and COORDSYSTEM (the other kind of child of the component).
		default:
			break;
		}
	}
}

bool FCoreTechFileParser::ReadComponent(CT_OBJECT_ID ComponentId, uint32 DefaultMaterialHash)
{
	int32 Index = SceneGraphArchive.ComponentSet.Emplace(ComponentId);
	SceneGraphArchive.CADIdToComponentIndex.Add(ComponentId, Index);
	ReadNodeMetaData(ComponentId, SceneGraphArchive.ComponentSet[Index].MetaData);

	if (uint32 MaterialHash = GetObjectMaterial(SceneGraphArchive.ComponentSet[Index]))
	{
		DefaultMaterialHash = MaterialHash;
	}


	TArray<CT_OBJECT_ID> Instances, Bodies;
	GetInstancesAndBodies(ComponentId, Instances, Bodies);


	// Kernel_IO's Stitching action always ends by the split of the new bodies into a set of connected patches
	// a CT_Component (reference in the concept of instance/reference) can have bodies and instances
	// The SEW stitching rule in UE is:
	//   - case 1 : the component has only a set of bodies: the bodies are merged, stitched and splitted into a new set of topologically correct bodies
	//   - case 2 : the component has only one body or has bodies + instance, only a topology healing is done on the bodies 
	//
	// Case 1: Repair is done before processing bodies of a component
	// Case 2: Repair is done before getting the mesh of the body. As the body is exploded, the new bodies have to be discovered by comparing parent's bodies before and after the repair action

	bool bNeedRepair = true;
	if (!Instances.Num() && Bodies.Num() > 1 && ImportParameters.StitchingTechnique == StitchingSew)
	{
		// Case 1: Repair is done before processing bodies of a component
		// Bodies.Num() > 1 so merge all bodies, sew and split into connected bodies (Repair)
		Repair(ComponentId, StitchingSew);
		GetInstancesAndBodies(ComponentId, Instances, Bodies);
		SetCoreTechTessellationState(ImportParameters);
		bNeedRepair = false;
	}

	for (CT_OBJECT_ID InstanceId : Instances)
	{
		if (ReadInstance(InstanceId, DefaultMaterialHash))
		{
			SceneGraphArchive.ComponentSet[Index].Children.Add(InstanceId);
		}
	}

	for (CT_OBJECT_ID BodyId : Bodies)
	{
		if (ReadBody(BodyId, ComponentId, DefaultMaterialHash, bNeedRepair))
		{
			SceneGraphArchive.ComponentSet[Index].Children.Add(BodyId);
		}
	}

	return true;
}

bool FCoreTechFileParser::ReadInstance(CT_OBJECT_ID InstanceNodeId, uint32 DefaultMaterialHash)
{

	int32 Index = SceneGraphArchive.Instances.Emplace(InstanceNodeId);
	SceneGraphArchive.CADIdToInstanceIndex.Add(InstanceNodeId, Index);

	ReadNodeMetaData(InstanceNodeId, SceneGraphArchive.Instances[Index].MetaData);

	if (uint32 MaterialHash = GetObjectMaterial(SceneGraphArchive.Instances[Index]))
	{
		DefaultMaterialHash = MaterialHash;
	}

	// Ask the transformation of the instance
	double Matrix[16];
	if (CT_INSTANCE_IO::AskTransformation(InstanceNodeId, Matrix) == IO_OK)
	{
		float* MatrixFloats = (float*)SceneGraphArchive.Instances[Index].TransformMatrix.M;
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
	SceneGraphArchive.Instances[Index].ReferenceNodeId = ReferenceNodeId;

	CT_OBJECT_TYPE type;
	CT_OBJECT_IO::AskType(ReferenceNodeId, type);
	if (type == CT_UNLOADED_PART_TYPE || type == CT_UNLOADED_COMPONENT_TYPE || type == CT_UNLOADED_ASSEMBLY_TYPE)
	{
		SceneGraphArchive.Instances[Index].bIsExternalRef = true;

		CT_STR ComponentFile, FileType;
		CT_COMPONENT_IO::AskExternalDefinition(ReferenceNodeId, ComponentFile, FileType);
		FString ExternalRefFullPath = AsFString(ComponentFile);

		const FString ConfigName = TEXT("Configuration Name");
		FString Configuration = SceneGraphArchive.Instances[Index].MetaData.FindRef(ConfigName);
		FFileDescription NewFileDescription(*ExternalRefFullPath, *Configuration, *FileDescription.MainCadFilePath);
		SceneGraphArchive.Instances[Index].ExternalRef = NewFileDescription;
		SceneGraphArchive.ExternalRefSet.Add(NewFileDescription);
	}
	else
	{
		SceneGraphArchive.Instances[Index].bIsExternalRef = false;
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

void FCoreTechFileParser::GetBodyTessellation(CT_OBJECT_ID BodyId, CT_OBJECT_ID ParentId, FBodyMesh& OutBodyMesh, uint32 DefaultMaterialHash, bool bNeedRepair)
{
	TArray<CT_OBJECT_ID> BodyFaces;
	{
		CT_LIST_IO FaceList;
		CT_BODY_IO::AskFaces(BodyId, FaceList);
		BodyFaces.Reserve((int32)(1.3 * FaceList.Count()));
	}

	FObjectDisplayDataId BodyMaterial;
	BodyMaterial.DefaultMaterialName = DefaultMaterialHash;
	GetCTObjectDisplayDataIds(BodyId, BodyMaterial);

	TArray<CT_OBJECT_ID> BodiesToProcess ;
	if (bNeedRepair && ImportParameters.StitchingTechnique != StitchingNone)
	{
		// Case 2: Repair is done before getting the mesh of the body.
		// Repair may have created new bodies including discarding initial body, so the list of bodies has to be retrieved by comparing parent's bodies before (InitialBodies) and after (AfterRepairBodies) the repair action

		TArray<CT_OBJECT_ID> Instances;
		TArray<CT_OBJECT_ID> InitialBodies;
		GetInstancesAndBodies(ParentId, Instances, InitialBodies);

		Repair(BodyId, ImportParameters.StitchingTechnique);
		SetCoreTechTessellationState(ImportParameters);

		TArray<CT_OBJECT_ID> AfterRepairBodies;
		GetInstancesAndBodies(ParentId, Instances, AfterRepairBodies);

		BodiesToProcess .Reserve(AfterRepairBodies.Num());
		for (CT_OBJECT_ID Body : AfterRepairBodies)
		{
			if (Body == BodyId)
			{
				BodiesToProcess .Add(Body);
			}
			else if(InitialBodies.Find(Body) == INDEX_NONE)
			{
				BodiesToProcess .Add(Body);
			}
		}
	}
	else
	{
		BodiesToProcess .Add(BodyId);
	}

	FBox& BBox = OutBodyMesh.BBox;
	for (CT_OBJECT_ID Body : BodiesToProcess )
	{
		CT_LIST_IO FaceList;
		CT_BODY_IO::AskFaces(Body, FaceList);

		// Compute Body BBox based on CAD data
		uint32 VerticesSize;
		CT_BODY_IO::AskVerticesSizeArray(Body, VerticesSize);

		TArray<CT_COORDINATE> VerticesArray;
		VerticesArray.SetNum(VerticesSize);
		CT_BODY_IO::AskVerticesArray(Body, VerticesArray.GetData());

		for (const CT_COORDINATE& Point : VerticesArray)
		{
			BBox += FVector((float)Point.xyz[0], (float)Point.xyz[1], (float)Point.xyz[2]);
		}

		CT_OBJECT_ID FaceID;
		FaceList.IteratorInitialize();
		while ((FaceID = FaceList.IteratorIter()) != 0)
		{
			BodyFaces.Add(FaceID);
		}
	}
	uint32 FaceSize = BodyFaces.Num();

	// Allocate memory space for tessellation data
	OutBodyMesh.Faces.Reserve(FaceSize);
	OutBodyMesh.ColorSet.Reserve(FaceSize);
	OutBodyMesh.MaterialSet.Reserve(FaceSize);

	// Loop through the face of bodies and collect all tessellation data
	int32 FaceIndex = 0;
	for(CT_OBJECT_ID FaceID : BodyFaces)
	{
		uint32 TriangleNum = GetFaceTessellation(FaceID, OutBodyMesh.Faces, ImportParameters);

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

bool FCoreTechFileParser::ReadBody(CT_OBJECT_ID BodyId, CT_OBJECT_ID ParentId, uint32 DefaultMaterialHash, bool bNeedRepair)
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

	int32 Index = SceneGraphArchive.BodySet.Emplace(BodyId);
	SceneGraphArchive.CADIdToBodyIndex.Add(BodyId, Index);
	ReadNodeMetaData(BodyId, SceneGraphArchive.BodySet[Index].MetaData);

	int32 BodyMeshIndex = BodyMeshes.Emplace(BodyId);

	if (uint32 MaterialHash = GetObjectMaterial(SceneGraphArchive.BodySet[Index]))
	{
		DefaultMaterialHash = MaterialHash;
	}

	bNeedSaveCTFile = true;

	SceneGraphArchive.BodySet[Index].MeshActorName = GetStaticMeshUuid(*SceneGraphArchive.ArchiveFileName, BodyId);
	BodyMeshes[BodyMeshIndex].MeshActorName = SceneGraphArchive.BodySet[Index].MeshActorName;

	// Save Body in CT file for re-tessellation before getBody because GetBody can call repair and modify the body (delete and build a new one with a new Id)
	CT_LIST_IO ObjectList;
	ObjectList.PushBack(BodyId);
	FString BodyFile = FString::Printf(TEXT("UEx%08x"), SceneGraphArchive.BodySet[Index].MeshActorName);
	CT_KERNEL_IO::SaveFile(ObjectList, *FPaths::Combine(CachePath, TEXT("body"), BodyFile + TEXT(".ct")), L"Ct");

	GetBodyTessellation(BodyId, ParentId, BodyMeshes[BodyMeshIndex], DefaultMaterialHash, bNeedRepair);

	SceneGraphArchive.BodySet[Index].ColorFaceSet = BodyMeshes[BodyMeshIndex].ColorSet;
	SceneGraphArchive.BodySet[Index].MaterialFaceSet = BodyMeshes[BodyMeshIndex].MaterialSet;

	return true;
}

void FCoreTechFileParser::GetAttributeValue(CT_ATTRIB_TYPE AttributType, int IthField, FString& Value)
{
	CT_STR               FieldName;
	CT_ATTRIB_FIELD_TYPE FieldType;

	Value = "";

	if (CT_ATTRIB_DEFINITION_IO::AskFieldDefinition(AttributType, IthField, FieldType, FieldName) != IO_OK) 
	{
		return;
	}

	switch (FieldType) {
		case CT_ATTRIB_FIELD_UNKNOWN:
		{
			break;
		}
		case CT_ATTRIB_FIELD_INTEGER:
		{
			int IValue;
			if (CT_CURRENT_ATTRIB_IO::AskIntField(IthField, IValue) != IO_OK) 
			{
				break;
			}
			Value = FString::FromInt(IValue);
			break;
		}
		case CT_ATTRIB_FIELD_DOUBLE:
		{
			double DValue;
			if (CT_CURRENT_ATTRIB_IO::AskDblField(IthField, DValue) != IO_OK)
			{
				break;
			}
			Value = FString::Printf(TEXT("%lf"), DValue);
			break;
		}
		case CT_ATTRIB_FIELD_STRING:
		{
			CT_STR StrValue;
			if (CT_CURRENT_ATTRIB_IO::AskStrField(IthField, StrValue) != IO_OK)
			{
				break;
			}
			Value = AsFString(StrValue);
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
		if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_STRING_METADATA_NAME, FieldName) != IO_OK)
		{
			break;
		}
		if (!FCString::Strcmp(InMetaDataName, *AsFString(FieldName)))
		{
			CT_STR FieldStrValue;
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_STRING_METADATA_VALUE, FieldStrValue) != IO_OK)
			{
				break;
			}
			OutMetaDataValue = AsFString(FieldStrValue);
			return;
		}
	}
}

void FCoreTechFileParser::ReadNodeMetaData(CT_OBJECT_ID NodeId, TMap<FString, FString>& OutMetaData)
{
	if (CT_COMPONENT_IO::IsA(NodeId, CT_COMPONENT_TYPE))
	{
		CT_STR FileName, FileType;
		CT_COMPONENT_IO::AskExternalDefinition(NodeId, FileName, FileType);
		OutMetaData.Add(TEXT("ExternalDefinition"), AsFString(FileName));
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


		if (CT_CURRENT_ATTRIB_IO::AskAttributeType(AttributeType) != IO_OK)
		{
			continue;
		}

		switch (AttributeType) {

		case CT_ATTRIB_SPLT:
			break;

		case CT_ATTRIB_NAME:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_NAME_VALUE, FieldStrValue) == IO_OK)
			{
				OutMetaData.Add(TEXT("CTName"), AsFString(FieldStrValue));
			}
			break;

		case CT_ATTRIB_ORIGINAL_NAME:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_NAME_VALUE, FieldStrValue) == IO_OK)
			{
				OutMetaData.Add(TEXT("Name"), AsFString(FieldStrValue));
			}
			break;

		case CT_ATTRIB_ORIGINAL_FILENAME:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_FILENAME_VALUE, FieldStrValue) == IO_OK)
			{
				OutMetaData.Add(TEXT("FileName"), AsFString(FieldStrValue));
			}
			break;

		case CT_ATTRIB_UUID:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_UUID_VALUE, FieldStrValue) == IO_OK)
			{
				OutMetaData.Add(TEXT("UUID"), AsFString(FieldStrValue));
			}
			break;

		case CT_ATTRIB_INPUT_FORMAT_AND_EMETTOR:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_INPUT_FORMAT_AND_EMETTOR, FieldStrValue) == IO_OK)
			{
				OutMetaData.Add(TEXT("Input_Format_and_Emitter"), AsFString(FieldStrValue));
			}
			break;

		case CT_ATTRIB_CONFIGURATION_NAME:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_NAME_VALUE, FieldStrValue) == IO_OK)
			{
				OutMetaData.Add(TEXT("ConfigurationName"), AsFString(FieldStrValue));
			}
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
				if (CT_CURRENT_ATTRIB_IO::AskIntField(ITH_COLORID_VALUE, FieldIntValue) != IO_OK)
				{
					break;
				}
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
			if (CT_CURRENT_ATTRIB_IO::AskIntField(ITH_MATERIALID_VALUE, FieldIntValue) != IO_OK)
			{
				break;
			}
			if (FArchiveMaterial* Material = SceneGraphArchive.MaterialHIdToMaterial.Find(FieldIntValue))
			{
				OutMetaData.Add(TEXT("MaterialName"), FString::FromInt(Material->UEMaterialName));
			}
			break;
		}

		case CT_ATTRIB_TRANSPARENCY:
			if (CT_CURRENT_ATTRIB_IO::AskDblField(ITH_TRANSPARENCY_VALUE, FieldDoubleValue0) != IO_OK)
			{
				break;
			}
			FieldIntValue = FMath::Max((1. - FieldDoubleValue0), FieldDoubleValue0) * 255.;
			OutMetaData.Add(TEXT("Transparency"), FString::FromInt(FieldIntValue));
			break;

		case CT_ATTRIB_COMMENT:
			//ITH_COMMENT_POSX, ITH_COMMENT_POSY, ITH_COMMENT_POSZ, ITH_COMMENT_TEXT
			break;

		case CT_ATTRIB_REFCOUNT:
			if (CT_CURRENT_ATTRIB_IO::AskIntField(ITH_REFCOUNT_VALUE, FieldIntValue) != IO_OK)
			{
				break;
			}
			//OutMetaData.Add(TEXT("RefCount"), FString::FromInt(FieldIntValue));
			break;

		case CT_ATTRIB_TESS_PARAMS:
		case CT_ATTRIB_COMPARE_RESULT:
			break;

		case CT_ATTRIB_DENSITY:
			//ITH_VOLUME_DENSITY_VALUE
			break;

		case CT_ATTRIB_MASS_PROPERTIES:
			if (CT_CURRENT_ATTRIB_IO::AskDblField(ITH_MASS_PROPERTIES_AREA, FieldDoubleValue0) != IO_OK)
			{
				break;
			}
			OutMetaData.Add(TEXT("Area"), FString::Printf(TEXT("%lf"), FieldDoubleValue0));
			if (CT_CURRENT_ATTRIB_IO::AskDblField(ITH_MASS_PROPERTIES_VOLUME, FieldDoubleValue0) != IO_OK)
			{
				break;
			}
			OutMetaData.Add(TEXT("Volume"), FString::Printf(TEXT("%lf"), FieldDoubleValue0));
			if (CT_CURRENT_ATTRIB_IO::AskDblField(ITH_MASS_PROPERTIES_MASS, FieldDoubleValue0) != IO_OK)
			{
				break;
			}
			OutMetaData.Add(TEXT("Mass"), FString::Printf(TEXT("%lf"), FieldDoubleValue0));
			if (CT_CURRENT_ATTRIB_IO::AskDblField(ITH_MASS_PROPERTIES_LENGTH, FieldDoubleValue0) != IO_OK)
			{
				break;
			}
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
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_INTEGER_METADATA_NAME, FieldName) != IO_OK)
			{
				break;
			}
			if (CT_CURRENT_ATTRIB_IO::AskIntField(ITH_INTEGER_METADATA_VALUE, FieldIntValue) != IO_OK)
			{
				break;
			}
			OutMetaData.Add(AsFString(FieldName), FString::FromInt(FieldIntValue));
			break;

		case CT_ATTRIB_DOUBLE_METADATA:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_DOUBLE_METADATA_NAME, FieldName) != IO_OK)
			{
				break;
			}
			if (CT_CURRENT_ATTRIB_IO::AskDblField(ITH_DOUBLE_METADATA_VALUE, FieldDoubleValue0) != IO_OK)
			{
				break;
			}
			OutMetaData.Add(AsFString(FieldName), FString::Printf(TEXT("%lf"), FieldDoubleValue0));
			break;

		case CT_ATTRIB_STRING_METADATA:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_STRING_METADATA_NAME, FieldName) != IO_OK)
			{
				break;
			}
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_STRING_METADATA_VALUE, FieldStrValue) != IO_OK)
			{
				break;
			}
			OutMetaData.Add(AsFString(FieldName), AsFString(FieldStrValue));
			break;

		case CT_ATTRIB_ORIGINAL_UNITS:
			if (CT_CURRENT_ATTRIB_IO::AskDblField(ITH_ORIGINAL_UNITS_MASS, FieldDoubleValue0) != IO_OK)
			{
				break;
			}
			if (CT_CURRENT_ATTRIB_IO::AskDblField(ITH_ORIGINAL_UNITS_LENGTH, FieldDoubleValue1) != IO_OK)
			{
				break;
			}
			if (CT_CURRENT_ATTRIB_IO::AskDblField(ITH_ORIGINAL_UNITS_DURATION, FieldDoubleValue2) != IO_OK)
			{
				break;
			}
			OutMetaData.Add(TEXT("OriginalUnitsMass"), FString::Printf(TEXT("%lf"), FieldDoubleValue0));
			OutMetaData.Add(TEXT("OriginalUnitsLength"), FString::Printf(TEXT("%lf"), FieldDoubleValue1));
			OutMetaData.Add(TEXT("OriginalUnitsDuration"), FString::Printf(TEXT("%lf"), FieldDoubleValue2));
			break;

		case CT_ATTRIB_ORIGINAL_TOLERANCE:
		case CT_ATTRIB_IGES_PARAMETERS:
		case CT_ATTRIB_READ_V4_MARKER:
			break;

		case CT_ATTRIB_PRODUCT:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_PRODUCT_REVISION, FieldStrValue) == IO_OK)
			{
				OutMetaData.Add(TEXT("ProductRevision"), AsFString(FieldStrValue));
			}

			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_PRODUCT_DEFINITION, FieldStrValue) == IO_OK)
			{
				OutMetaData.Add(TEXT("ProductDefinition"), AsFString(FieldStrValue));
			}

			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_PRODUCT_NOMENCLATURE, FieldStrValue) == IO_OK)
			{
				OutMetaData.Add(TEXT("ProductNomenclature"), AsFString(FieldStrValue));
			}

			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_PRODUCT_SOURCE, FieldStrValue) == IO_OK)
			{
				OutMetaData.Add(TEXT("ProductSource"), AsFString(FieldStrValue));
			}

			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_PRODUCT_DESCRIPTION, FieldStrValue) != IO_OK)
			{
				OutMetaData.Add(TEXT("ProductDescription"), AsFString(FieldStrValue));
			}
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
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_INTEGER_PARAMETER_NAME, FieldName) != IO_OK)
			{
				break;
			}
			if (CT_CURRENT_ATTRIB_IO::AskIntField(ITH_INTEGER_PARAMETER_VALUE, FieldIntValue) != IO_OK)
			{
				break;
			}
			OutMetaData.Add(AsFString(FieldName), FString::FromInt(FieldIntValue));
			break;

		case CT_ATTRIB_DOUBLE_PARAMETER:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_DOUBLE_PARAMETER_NAME, FieldName) != IO_OK)
			{
				break;
			}
			if (CT_CURRENT_ATTRIB_IO::AskDblField(ITH_DOUBLE_PARAMETER_VALUE, FieldDoubleValue0) != IO_OK)
			{
				break;
			}
			OutMetaData.Add(AsFString(FieldName), FString::Printf(TEXT("%lf"), FieldDoubleValue0));
			break;

		case CT_ATTRIB_STRING_PARAMETER:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_STRING_PARAMETER_NAME, FieldName) != IO_OK)
			{
				break;
			}
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_STRING_PARAMETER_VALUE, FieldStrValue) != IO_OK)
			{
				break;
			}
			OutMetaData.Add(AsFString(FieldName), AsFString(FieldStrValue));
			break;

		case CT_ATTRIB_PARAMETER_ARRAY:
			//ITH_PARAMETER_ARRAY_NAME
			//ITH_PARAMETER_ARRAY_NUMBER
			//ITH_PARAMETER_ARRAY_VALUES
			break;

		case CT_ATTRIB_SAVE_OPTION:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_SAVE_OPTION_AUTHOR, FieldStrValue) == IO_OK)
			{
				OutMetaData.Add(TEXT("SaveOptionAuthor"), AsFString(FieldStrValue));
			}

			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_SAVE_OPTION_ORGANIZATION, FieldStrValue) == IO_OK)
			{
				OutMetaData.Add(TEXT("SaveOptionOrganization"), AsFString(FieldStrValue));
			}

			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_SAVE_OPTION_FILE_DESCRIPTION, FieldStrValue) == IO_OK)
			{
				OutMetaData.Add(TEXT("SaveOptionFileDescription"), AsFString(FieldStrValue));
			}

			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_SAVE_OPTION_AUTHORISATION, FieldStrValue) == IO_OK)
			{
				OutMetaData.Add(TEXT("SaveOptionAuthorisation"), AsFString(FieldStrValue));
			}

			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_SAVE_OPTION_PREPROCESSOR, FieldStrValue) == IO_OK)
			{
				OutMetaData.Add(TEXT("SaveOptionPreprocessor"), AsFString(FieldStrValue));
			}
			break;

		case CT_ATTRIB_ORIGINAL_ID:
			GetAttributeValue(AttributeType, ITH_ORIGINAL_ID_VALUE, FieldValue);
			OutMetaData.Add(TEXT("OriginalId"), FieldValue);
			break;

		case CT_ATTRIB_ORIGINAL_ID_STRING:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_ORIGINAL_ID_VALUE_STRING, FieldStrValue) != IO_OK)
			{
				break;
			}
			OutMetaData.Add(TEXT("OriginalIdStr"), AsFString(FieldStrValue));
			break;

		case CT_ATTRIB_COLOR_RGB_DOUBLE:
			if (CT_CURRENT_ATTRIB_IO::AskDblField(ITH_ATTRIB_COLOR_R_DOUBLE, FieldDoubleValue0) != IO_OK)
			{
				break;
			}
			if (CT_CURRENT_ATTRIB_IO::AskDblField(ITH_ATTRIB_COLOR_G_DOUBLE, FieldDoubleValue1) != IO_OK)
			{
				break;
			}
			if (CT_CURRENT_ATTRIB_IO::AskDblField(ITH_ATTRIB_COLOR_B_DOUBLE, FieldDoubleValue2) != IO_OK)
			{
				break;
			}
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
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_INTEGER_VALIDATION_NAME, FieldName) != IO_OK)
			{
				break;
			}
			if (CT_CURRENT_ATTRIB_IO::AskIntField(ITH_INTEGER_VALIDATION_VALUE, FieldIntValue) != IO_OK)
			{
				break;
			}
			OutMetaData.Add(AsFString(FieldName), FString::FromInt(FieldIntValue));
			break;

		case CT_ATTRIB_DOUBLE_VALIDATION_ATTRIBUTE:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_DOUBLE_VALIDATION_NAME, FieldName) != IO_OK)
			{
				break;
			}
			if (CT_CURRENT_ATTRIB_IO::AskDblField(ITH_DOUBLE_VALIDATION_VALUE, FieldDoubleValue0) != IO_OK)
			{
				break;
			}
			OutMetaData.Add(AsFString(FieldName), FString::Printf(TEXT("%lf"), FieldDoubleValue0));
			break;

		case CT_ATTRIB_STRING_VALIDATION_ATTRIBUTE:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_STRING_VALIDATION_NAME, FieldName) != IO_OK)
			{
				break;
			}
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_STRING_VALIDATION_VALUE, FieldStrValue) != IO_OK)
			{
				break;
			}
			OutMetaData.Add(AsFString(FieldName), AsFString(FieldStrValue));
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
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_GROUPNAME_VALUE, FieldStrValue) != IO_OK)
			{
				break;
			}
			OutMetaData.Add(TEXT("GroupName"), AsFString(FieldStrValue));
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

	// Clean metadata value i.e. remove all unprintable characters
	for (auto& MetaPair : OutMetaData)
	{
		FDatasmithUtils::SanitizeStringInplace(MetaPair.Value);
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
