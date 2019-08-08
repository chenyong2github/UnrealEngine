// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "HairStrandsLoader.h"
#include "HairStrandsDatas.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/PlatformFilemanager.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#endif

THIRD_PARTY_INCLUDES_START
#include <fbxsdk.h>
#include <Alembic/AbcGeom/All.h>
#include <Alembic/AbcCoreFactory/IFactory.h>
#include <Alembic/Abc/IArchive.h>
#include <Alembic/Abc/IObject.h>
THIRD_PARTY_INCLUDES_END

#if PLATFORM_WINDOWS
#include "Windows/HideWindowsPlatformTypes.h"
#endif

#define HAIR_FILE_SEGMENTS_BIT		1
#define HAIR_FILE_POINTS_BIT		2
#define HAIR_FILE_THICKNESS_BIT		4
#define HAIR_FILE_TRANSPARENCY_BIT	8
#define HAIR_FILE_COLORS_BIT		16  

static const float RootRadius = 0.0001f; // m
static const float TipRadius = 0.00005f; // m

void FHairFormat::ParseFile(const FString& FileName, FHairStrandsDatas& HairStrands)  
{
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	IFileHandle* FileHandle = PlatformFile.OpenRead(*FileName);

	if (FileHandle)
	{
		FFileHeader FileHeader;
		if (FileHandle->Read(reinterpret_cast<uint8*>(&FileHeader), sizeof(FFileHeader)))
		{
			if (strncmp(FileHeader.FileSignature, "HAIR", 4) == 0)
			{
				HairStrands.StrandsPoints.SetNum(FileHeader.NumPoints);
				HairStrands.StrandsCurves.SetNum(FileHeader.NumStrands);

				// Read strands counts
				uint16* StrandsCounts = nullptr;
				if (FileHeader.BitArrays & HAIR_FILE_SEGMENTS_BIT)
				{
					FileHandle->Read(reinterpret_cast<uint8*>(HairStrands.StrandsCurves.CurvesCount.GetData()), sizeof(uint16)*FileHeader.NumStrands);
				}
				else
				{
					HairStrands.StrandsCurves.CurvesCount.Init(FileHeader.StrandThickness, FileHeader.NumStrands);
				}

				// Read strands positions
				float* StrandsPositions = nullptr;
				if (FileHeader.BitArrays & HAIR_FILE_POINTS_BIT)
				{
					FileHandle->Read(reinterpret_cast<uint8*>(&HairStrands.StrandsPoints.PointsPosition.GetData()->X), sizeof(float)*FileHeader.NumPoints * 3);
				}
				else
				{
					HairStrands.StrandsPoints.PointsPosition.Init(FVector(0.0, 0.0, 0.0), FileHeader.NumPoints);
				}

				// Read strands positions thickness
				float* StrandsThickness = nullptr;
				if (FileHeader.BitArrays & HAIR_FILE_THICKNESS_BIT)
				{
					FileHandle->Read(reinterpret_cast<uint8*>(HairStrands.StrandsPoints.PointsRadius.GetData()), sizeof(float)*FileHeader.NumPoints);
				}
				else
				{
					HairStrands.StrandsPoints.PointsRadius.Init(FileHeader.StrandThickness, FileHeader.NumPoints);
				}
			}
		}
		delete FileHandle;
	}
}

static void ParseNode(FbxNode* FileNode, FHairStrandsDatas& HairStrands)
{
	if (FileNode->GetNodeAttribute() != NULL && FileNode->GetNodeAttribute()->GetAttributeType() == FbxNodeAttribute::eNurbsCurve)
	{
		FbxNurbsCurve* NurbsCurve = static_cast<FbxNurbsCurve*>(FileNode->GetNodeAttribute());
		FbxArray<FbxVector4> PointArray;
		const uint32 PointCount = NurbsCurve->TessellateCurve(PointArray, 2);
		
		const uint32 NumPoints = HairStrands.GetNumPoints();
		const uint32 NumCurves = HairStrands.GetNumCurves();

		HairStrands.StrandsPoints.SetNum(NumPoints+PointCount);
		HairStrands.StrandsCurves.SetNum(NumCurves + 1);
		HairStrands.StrandsCurves.CurvesCount[NumCurves] = PointCount;

		for (uint32 PointIndex = 0; PointIndex < PointCount; ++PointIndex)
		{
			const float CoordU = PointIndex / static_cast<float>(PointCount - 1);
			const float Radius = FMath::Lerp(RootRadius, TipRadius, CoordU);
			const FVector Position(PointArray[PointIndex][0], PointArray[PointIndex][1], PointArray[PointIndex][2]);

			HairStrands.StrandsPoints.PointsPosition[NumPoints+ PointIndex] = Position;
			HairStrands.StrandsPoints.PointsRadius[NumPoints + PointIndex] = Radius;
		}
	}
	//	#todo_hair
	// * Load user/custom properties (eccentricity, width/thickness, ...)
	// * Load root/hierarchy transform

	const uint32 NumChildren = FileNode->GetChildCount();
	if (NumChildren > 0)
	{
		for (int32 ChildIndex = 0; ChildIndex < FileNode->GetChildCount(); ++ChildIndex)
		{
			ParseNode(FileNode->GetChild(ChildIndex), HairStrands);
		}
	}
}

void FFbxFormat::ParseFile(const FString& FileName, FHairStrandsDatas& HairStrands)
{
	FbxManager* FileManager = FbxManager::Create();
	FbxIOSettings* FileSettings = FbxIOSettings::Create(FileManager, IOSROOT);

	FileManager->SetIOSettings(FileSettings);

	FbxScene* FileScene = FbxScene::Create(FileManager, "StrandAssetFbx");

	// Create an importer.
	FbxImporter* FileImporter = FbxImporter::Create(FileManager, "");

	// Initialize the importer by providing a filename.
	bool FileStatus = FileImporter->Initialize(TCHAR_TO_UTF8(*FileName), -1, FileSettings);

	if (FileImporter->IsFBX())
	{
		// Only load geometry data
		FileSettings->SetBoolProp(IMP_FBX_MATERIAL, false);
		FileSettings->SetBoolProp(IMP_FBX_TEXTURE, false);
		FileSettings->SetBoolProp(IMP_FBX_LINK, false);
		FileSettings->SetBoolProp(IMP_FBX_SHAPE, true);
		FileSettings->SetBoolProp(IMP_FBX_GOBO, false);
		FileSettings->SetBoolProp(IMP_FBX_ANIMATION, false);
		FileSettings->SetBoolProp(IMP_FBX_GLOBAL_SETTINGS, false);
	}

	// Import the scene and destroy the importer.
	FileStatus = FileImporter->Import(FileScene);
	FileImporter->Destroy();

	if (FbxNode* FileNode = FileScene->GetRootNode())
	{
		for (int32 ChildIndex = 0; ChildIndex < FileNode->GetChildCount(); ++ChildIndex)
		{
			ParseNode(FileNode->GetChild(ChildIndex), HairStrands);
		}
	}
}

FMatrix ConvertAlembicMatrix(const Alembic::Abc::M44d& AbcMatrix)
{
	FMatrix Matrix;
	for (uint32 i = 0; i < 16; ++i)
	{
		Matrix.M[i >> 2][i % 4] = (float)AbcMatrix.getValue()[i];
	}

	return Matrix;
}

static void ParseObject(const Alembic::Abc::IObject& InObject, FHairStrandsDatas& HairStrands, const FMatrix& ParentMatrix)
{
	// Get Header and MetaData info from current Alembic Object
	Alembic::AbcCoreAbstract::ObjectHeader Header = InObject.getHeader();
	const Alembic::Abc::MetaData ObjectMetaData = InObject.getMetaData();
	const uint32 NumChildren = InObject.getNumChildren();

	FMatrix LocalMatrix = ParentMatrix;

	bool bHandled = false;
	if (Alembic::AbcGeom::ICurves::matches(ObjectMetaData))
	{
		Alembic::AbcGeom::ICurves Curves = Alembic::AbcGeom::ICurves(InObject, Alembic::Abc::kWrapExisting);
		Alembic::AbcGeom::ICurves::schema_type::Sample Sample = Curves.getSchema().getValue();

		Alembic::Abc::FloatArraySamplePtr Widths = Curves.getSchema().getWidthsParam() ? Curves.getSchema().getWidthsParam().getExpandedValue().getVals() : nullptr;
		Alembic::Abc::P3fArraySamplePtr Positions = Sample.getPositions();
		Alembic::Abc::Int32ArraySamplePtr Counts = Sample.getCurvesNumVertices();

		const uint32 PointsSize = Positions ? Positions->size() : 0;
		const uint32 CurvesSize = Counts->size();

		const uint32 NumPoints = HairStrands.GetNumPoints();
		const uint32 NumCurves = HairStrands.GetNumCurves();

		HairStrands.StrandsCurves.SetNum(NumCurves+CurvesSize);
		HairStrands.StrandsPoints.SetNum(NumPoints+PointsSize);

		uint32 GlobalIndex = 0;
		for (uint32 CurveIndex = 0; CurveIndex < CurvesSize; ++CurveIndex)
		{
			const uint32 PointCount = (*Counts)[CurveIndex];
			HairStrands.StrandsCurves.CurvesCount[NumCurves+CurveIndex] = PointCount;
			
			for (uint32 PointIndex = 0; PointIndex < PointCount; ++PointIndex, ++GlobalIndex)
			{
				const float CoordU = PointIndex / static_cast<float>(PointCount - 1);
				const float Radius = (Widths) ? (*Widths)[GlobalIndex] : FMath::Lerp(RootRadius, TipRadius, CoordU);
				Alembic::Abc::P3fArraySample::value_type Position = (*Positions)[GlobalIndex];

				HairStrands.StrandsPoints.PointsPosition[NumPoints + GlobalIndex] = ParentMatrix.TransformPosition( FVector(Position.x, Position.y, Position.z) );
				HairStrands.StrandsPoints.PointsRadius[NumPoints + GlobalIndex] = Radius;
			}
		}
	}
	else if (Alembic::AbcGeom::IXform::matches(ObjectMetaData))
	{
		Alembic::AbcGeom::IXform Xform = Alembic::AbcGeom::IXform(InObject, Alembic::Abc::kWrapExisting);
		Alembic::AbcGeom::XformSample MatrixSample; 
		Xform.getSchema().get(MatrixSample);

		LocalMatrix =  ParentMatrix * ConvertAlembicMatrix(MatrixSample.getMatrix());
	}

	if (NumChildren > 0)
	{
		for (uint32 ChildIndex = 0; ChildIndex < NumChildren; ++ChildIndex)
		{
			ParseObject(InObject.getChild(ChildIndex), HairStrands, LocalMatrix);
		}
	}
}

void FAbcFormat::ParseFile(const FString& FileName, FHairStrandsDatas& HairStrands)
{
	/** Factory used to generate objects*/
	Alembic::AbcCoreFactory::IFactory Factory;
	Alembic::AbcCoreFactory::IFactory::CoreType CompressionType = Alembic::AbcCoreFactory::IFactory::kUnknown;
	/** Archive-typed ABC file */
	Alembic::Abc::IArchive Archive;
	/** Alembic typed root (top) object*/
	Alembic::Abc::IObject TopObject;

	Factory.setPolicy(Alembic::Abc::ErrorHandler::kThrowPolicy);
	Factory.setOgawaNumStreams(12);

	// Extract Archive and compression type from file
	Archive = Factory.getArchive(TCHAR_TO_UTF8(*FileName), CompressionType);
	if (!Archive.valid())
	{
		return;
	}

	// Get Top/root object
	TopObject = Alembic::Abc::IObject(Archive, Alembic::Abc::kTop);
	if (!TopObject.valid())
	{
		return;
	}

	FMatrix ParentMatrix = FMatrix::Identity;
	ParseObject(TopObject, HairStrands, ParentMatrix);
}

template<typename FileFormat>
void THairStrandsLoader<FileFormat>::LoadHairStrands(const FString& FileName, FHairStrandsDatas& HairStrands)
{
	HairStrands.StrandsCurves.Reset();
	HairStrands.StrandsPoints.Reset();

	FileFormat::ParseFile(FileName,HairStrands);

	TArray<FVector>::TIterator PositionIterator = HairStrands.StrandsPoints.PointsPosition.CreateIterator();
	TArray<float>::TIterator RadiusIterator = HairStrands.StrandsPoints.PointsRadius.CreateIterator();

	for (uint32 PointIndex = 0; PointIndex < HairStrands.GetNumPoints(); ++PointIndex, ++PositionIterator, ++RadiusIterator)
	{
		*PositionIterator *= FileFormat::UNIT_TO_CM;
		*RadiusIterator *= FileFormat::UNIT_TO_CM;
	}

	HairStrands.BuildInternalDatas();
}

template struct THairStrandsLoader<FHairFormat>;
template struct THairStrandsLoader<FFbxFormat>;
template struct THairStrandsLoader<FAbcFormat>;
