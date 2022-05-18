// Copyright Epic Games, Inc. All Rights Reserved.

#include "TechSoftFileParser.h"

#include "CADFileData.h"
#include "CADOptions.h"
#include "TechSoftUtils.h"
#include "TechSoftUtilsPrivate.h"
#include "TUniqueTechSoftObj.h"

#include "HAL/FileManager.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Templates/UnrealTemplate.h"

namespace CADLibrary
{

#ifdef USE_TECHSOFT_SDK

namespace TechSoftFileParserImpl
{

// This code is a duplication code of CADFileReader::FindFile 
// This is done in 5.0.3 to avoid public header modification. 
// However this need to be rewrite in the next version. (Jira UE-152626)
bool UpdateFileDescriptor(FFileDescriptor& File)
{
	const FString& FileName = File.GetFileName();

	FString FilePath = FPaths::GetPath(File.GetSourcePath());
	FString RootFilePath = File.GetRootFolder();

	// Basic case: File exists at the initial path
	if (IFileManager::Get().FileExists(*File.GetSourcePath()))
	{
		return true;
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

	for (int32 IndexFolderPath = 0; IndexFolderPath < RootPaths.Num(); IndexFolderPath++)
	{
		for (int32 IndexFilePath = 0; IndexFilePath < FilePaths.Num(); IndexFilePath++)
		{
			FString NewFilePath = FPaths::Combine(RootPaths[IndexFolderPath], FilePaths[IndexFilePath]);
			if (IFileManager::Get().FileExists(*NewFilePath))
			{
				File.SetSourceFilePath(NewFilePath);
				return true;
			};
		}
	}

	return false;
}

// Functions to clean metadata

inline void RemoveUnwantedChar(FString& StringToClean, TCHAR UnwantedChar)
{
	FString NewString;
	NewString.Reserve(StringToClean.Len());
	for (const TCHAR& Char : StringToClean)
	{
		if (Char != UnwantedChar)
		{
			NewString.AppendChar(Char);
		}
	}
	StringToClean = MoveTemp(NewString);
}

bool CheckIfNameExists(TMap<FString, FString>& MetaData)
{
	FString* NamePtr = MetaData.Find(TEXT("Name"));
	if (NamePtr != nullptr)
	{
		return true;
	}
	return false;
}

bool ReplaceOrAddNameValue(TMap<FString, FString>& MetaData, const TCHAR* Key)
{
	FString* NamePtr = MetaData.Find(Key);
	if (NamePtr != nullptr)
	{
		FString& Name = MetaData.FindOrAdd(TEXT("Name"));
		Name = *NamePtr;
		return true;
	}
	return false;
}

// Functions used in traverse model process

void TraverseAttribute(const A3DMiscAttributeData& AttributeData, TMap<FString, FString>& OutMetaData)
{
	FString AttributeName;
	if (AttributeData.m_bTitleIsInt)
	{
		A3DUns32 UnsignedValue = 0;
		memcpy(&UnsignedValue, AttributeData.m_pcTitle, sizeof(A3DUns32));
		AttributeName = FString::Printf(TEXT("%u"), UnsignedValue);
	}
	else if (AttributeData.m_pcTitle && AttributeData.m_pcTitle[0] != '\0')
	{
		AttributeName = UTF8_TO_TCHAR(AttributeData.m_pcTitle);
	}

	for (A3DUns32 Index = 0; Index < AttributeData.m_uiSize; ++Index)
	{
		FString AttributeValue;
		switch (AttributeData.m_asSingleAttributesData[Index].m_eType)
		{
		case kA3DModellerAttributeTypeTime:
		case kA3DModellerAttributeTypeInt:
		{
			A3DInt32 Value;
			memcpy(&Value, AttributeData.m_asSingleAttributesData[Index].m_pcData, sizeof(A3DInt32));
			AttributeValue = FString::Printf(TEXT("%d"), Value);
			break;
		}

		case kA3DModellerAttributeTypeReal:
		{
			A3DDouble Value;
			memcpy(&Value, AttributeData.m_asSingleAttributesData[Index].m_pcData, sizeof(A3DDouble));
			AttributeValue = FString::Printf(TEXT("%f"), Value);
			break;
		}

		case kA3DModellerAttributeTypeString:
		{
			if (AttributeData.m_asSingleAttributesData[Index].m_pcData && AttributeData.m_asSingleAttributesData[Index].m_pcData[0] != '\0')
			{
				AttributeValue = UTF8_TO_TCHAR(AttributeData.m_asSingleAttributesData[Index].m_pcData);
			}
			break;
		}

		default:
			break;
		}

		if (AttributeName.Len())
		{
			if (Index)
			{
				OutMetaData.Emplace(FString::Printf(TEXT("%s_%u"), *AttributeName, (int32)Index), AttributeValue);
			}
			else
			{
				OutMetaData.Emplace(AttributeName, AttributeValue);
			}
		}
	}
}

void SetIOOption(A3DImport& Importer)
{
	// A3DRWParamsGeneralData Importer.m_sGeneral
	Importer.m_sLoadData.m_sGeneral.m_bReadSolids = A3D_TRUE;
	Importer.m_sLoadData.m_sGeneral.m_bReadSurfaces = A3D_TRUE;
	Importer.m_sLoadData.m_sGeneral.m_bReadWireframes = A3D_FALSE;
	Importer.m_sLoadData.m_sGeneral.m_bReadPmis = A3D_FALSE;
	Importer.m_sLoadData.m_sGeneral.m_bReadAttributes = A3D_TRUE;
	Importer.m_sLoadData.m_sGeneral.m_bReadHiddenObjects = A3D_FALSE;
	Importer.m_sLoadData.m_sGeneral.m_bReadConstructionAndReferences = A3D_FALSE;
	Importer.m_sLoadData.m_sGeneral.m_bReadActiveFilter = A3D_FALSE;
	Importer.m_sLoadData.m_sGeneral.m_eReadingMode2D3D = kA3DRead_3D;

	Importer.m_sLoadData.m_sGeneral.m_eReadGeomTessMode = kA3DReadGeomAndTess/*kA3DReadGeomOnly*/;
	Importer.m_sLoadData.m_sGeneral.m_bReadFeature = A3D_FALSE;

	Importer.m_sLoadData.m_sGeneral.m_bReadConstraints = A3D_FALSE;
	Importer.m_sLoadData.m_sGeneral.m_iNbMultiProcess = 1;

	Importer.m_sLoadData.m_sIncremental.m_bLoadNoDependencies = CADLibrary::FImportParameters::bGEnableCADCache;
	Importer.m_sLoadData.m_sIncremental.m_bLoadStructureOnly = false;
}

void UpdateIOOptionAccordingToFormat(const CADLibrary::ECADFormat Format, A3DImport& Importer, bool& OutForceSew)
{
	switch (Format)
	{
	case CADLibrary::ECADFormat::IGES:
	{
		OutForceSew = true;
		break;
	}

	case CADLibrary::ECADFormat::CATIA:
	{
		break;
	}

	case CADLibrary::ECADFormat::SOLIDWORKS:
	{
		Importer.m_sLoadData.m_sSpecifics.m_sSolidworks.m_bLoadAllConfigsData = true;
		break;
	}

	case CADLibrary::ECADFormat::JT:
	{
		if (FImportParameters::bGPreferJtFileEmbeddedTessellation)
		{
			Importer.m_sLoadData.m_sGeneral.m_eReadGeomTessMode = kA3DReadTessOnly;
			Importer.m_sLoadData.m_sSpecifics.m_sJT.m_eReadTessellationLevelOfDetail = A3DEJTReadTessellationLevelOfDetail::kA3DJTTessLODHigh;
			break;
		}
	}

	case CADLibrary::ECADFormat::INVENTOR:
	case CADLibrary::ECADFormat::CATIA_3DXML:
	{
		Importer.m_sLoadData.m_sIncremental.m_bLoadNoDependencies = false;
		break;
	}

	default:
		break;
	};
}

double ExtractUniformScale(FVector3d& Scale)
{
	double UniformScale = (Scale.X + Scale.Y + Scale.Z) / 3.;
	double Tolerance = UniformScale * KINDA_SMALL_NUMBER;

	if (!FMath::IsNearlyEqual(UniformScale, Scale.X, Tolerance) && !FMath::IsNearlyEqual(UniformScale, Scale.Y, Tolerance))
	{
		// non uniform scale 
		// Used in format like IFC or DGN to define pipe by their diameter and their length
		// we remove the diameter component of the scale to have a scale like (Length/diameter, 1, 1) to have a mesh tessellated according the meshing parameters
		if (FMath::IsNearlyEqual((double)Scale.X, (double)Scale.Y, Tolerance) || (FMath::IsNearlyEqual((double)Scale.X, (double)Scale.Z, Tolerance)))
		{
			UniformScale = Scale.X;
		}
		else if (FMath::IsNearlyEqual((double)Scale.Y, (double)Scale.Z, Tolerance))
		{
			UniformScale = Scale.Y;
		}
	}

	Scale.X /= UniformScale;
	Scale.Y /= UniformScale;
	Scale.Z /= UniformScale;

	return UniformScale;
}

} // ns TechSoftFileParserImpl

#endif

FTechSoftFileParser::FTechSoftFileParser(FCADFileData& InCADData, const FString& EnginePluginsPath)
	: CADFileData(InCADData)
	, TechSoftInterface(FTechSoftInterface::Get())
{
	TechSoftInterface.InitializeKernel(*EnginePluginsPath);
}

#ifdef USE_TECHSOFT_SDK
ECADParsingResult FTechSoftFileParser::Process()
{
	const FFileDescriptor& File = CADFileData.GetCADFileDescription();

	if (File.GetPathOfFileToLoad().IsEmpty())
	{
		return ECADParsingResult::FileNotFound;
	}

	A3DImport Import(TCHAR_TO_UTF8(*File.GetPathOfFileToLoad())); 

	TechSoftFileParserImpl::SetIOOption(Import);

	// Add specific options according to format
	Format = File.GetFileFormat();
	TechSoftFileParserImpl::UpdateIOOptionAccordingToFormat(Format, Import, bForceSew);

	A3DStatus LoadStatus = A3DStatus::A3D_SUCCESS;
	ModelFile = TechSoftInterface::LoadModelFileFromFile(Import, LoadStatus);

	if (!ModelFile.IsValid())
	{
		switch (LoadStatus)
		{
		case A3DStatus::A3D_LOAD_FILE_TOO_OLD:
		{
			CADFileData.AddWarningMessages(FString::Printf(TEXT("File %s hasn't been loaded because the version is less than the oldest supported version."), *File.GetFileName()));
			break;
		}

		case A3DStatus::A3D_LOAD_FILE_TOO_RECENT:
		{
			CADFileData.AddWarningMessages(FString::Printf(TEXT("File %s hasn't been loaded because the version is more recent than supported version."), *File.GetFileName()));
			break;
		}

		case A3DStatus::A3D_LOAD_CANNOT_ACCESS_CADFILE:
		{
			CADFileData.AddWarningMessages(FString::Printf(TEXT("File %s hasn't been loaded because the input path cannot be opened by the running process for reading."), *File.GetFileName()));
			break;
		}

		case A3DStatus::A3D_LOAD_INVALID_FILE_FORMAT:
		{
			CADFileData.AddWarningMessages(FString::Printf(TEXT("File %s hasn't been loaded because the format is not supported."), *File.GetFileName()));
			break;
		}

		default:
		{
			CADFileData.AddWarningMessages(FString::Printf(TEXT("File %s hasn't been loaded because an error occured while reading the file."), *File.GetFileName()));
			break;
		}
		};
		return ECADParsingResult::ProcessFailed;
	}

	{
		TUniqueTSObj<A3DAsmModelFileData> ModelFileData(ModelFile.Get());
		if (!ModelFileData.IsValid())
		{
			return ECADParsingResult::ProcessFailed;
		}

		ModellerType = (EModellerType)ModelFileData->m_eModellerType;
		FileUnit = TechSoftInterface::GetModelFileUnit(ModelFile.Get());
	}

	// save the file for the next load
	if (CADFileData.IsCacheDefined())
	{
		FString CacheFilePath = CADFileData.GetCADCachePath();
		if (CacheFilePath != File.GetPathOfFileToLoad())
		{
			TechSoftUtils::SaveModelFileToPrcFile(ModelFile.Get(), CacheFilePath);
		}
	}

	// Adapt BRep to CADKernel
	if (AdaptBRepModel() != A3D_SUCCESS)
	{
		return ECADParsingResult::ProcessFailed;
	}

	// Some formats (like IGES) require a sew all the time. In this case, bForceSew = true
	if (bForceSew || CADFileData.GetImportParameters().GetStitchingTechnique() == StitchingSew)
	{
		SewModel();
	}

	ReserveCADFileData();

	ReadMaterialsAndColors();

	ECADParsingResult Result = TraverseModel();

	if (Result == ECADParsingResult::ProcessOk)
	{
		GenerateBodyMeshes();
	}

	FString TechSoftVersion = TechSoftInterface::GetTechSoftVersion();
	if (!TechSoftVersion.IsEmpty() && CADFileData.ComponentCount() > 0)
	{
		CADFileData.GetComponentAt(0).MetaData.Add(TEXT("TechsoftVersion"), TechSoftVersion);
	}

	ModelFile.Reset();

	return Result;
}

void FTechSoftFileParser::SewModel()
{
	CADLibrary::TUniqueTSObj<A3DSewOptionsData> SewData;
	SewData->m_bComputePreferredOpenShellOrientation = false;
	
	TechSoftInterface::SewModel(ModelFile.Get(), CADLibrary::FImportParameters::GStitchingTolerance, SewData.GetPtr());
}


void FTechSoftFileParser::GenerateBodyMeshes()
{
	for (TPair<A3DRiRepresentationItem*, int32>& Entry : RepresentationItemsCache)
	{
		A3DRiRepresentationItem* RepresentationItemPtr = Entry.Key;
		FArchiveBody& Body = CADFileData.GetBodyAt(Entry.Value);
		GenerateBodyMesh(RepresentationItemPtr, Body);
	}
}

void FTechSoftFileParser::GenerateBodyMesh(A3DRiRepresentationItem* Representation, FArchiveBody& Body)
{
	FBodyMesh& BodyMesh = CADFileData.AddBodyMesh(Body.ObjectId, Body);

	uint32 NewBRepCount = 0;
	A3DRiBrepModel** NewBReps = nullptr;

	if (CADFileData.GetImportParameters().GetStitchingTechnique() == StitchingHeal)
	{
		TUniqueTSObj<A3DSewOptionsData> SewData;
		SewData->m_bComputePreferredOpenShellOrientation = false;
		const uint32 BRepCount = 1;
		A3DStatus Status = TechSoftInterface::SewBReps(&Representation, BRepCount, CADLibrary::FImportParameters::GStitchingTolerance, FileUnit, SewData.GetPtr(), &NewBReps, NewBRepCount);
		if (Status != A3DStatus::A3D_SUCCESS)
		{
			CADFileData.AddWarningMessages(TEXT("A body healing failed. A body could be missing."));
		}
	}

	if (NewBRepCount > 0)
	{
		for (uint32 Index = 0; Index < NewBRepCount; ++Index)
		{
			TechSoftUtils::FillBodyMesh(NewBReps[Index], CADFileData.GetImportParameters(), Body.BodyUnit, BodyMesh);
		}
	} 
	else
	{
		TechSoftUtils::FillBodyMesh(Representation, CADFileData.GetImportParameters(), Body.BodyUnit, BodyMesh);
	}

	if (BodyMesh.TriangleCount == 0)
	{
		// the mesh of the body is empty, the body is deleted.
		// Todo (jira UETOOL-5148): add a boolean in Body to flag that the body should not be build
		Body.ParentId = 0;
		Body.MeshActorName = 0;
	}

	// Convert material
	FCADUUID DefaultColorName = Body.ColorFaceSet.Num() > 0 ? *Body.ColorFaceSet.begin() : 0;
	FCADUUID DefaultMaterialName = Body.MaterialFaceSet.Num() > 0 ? *Body.MaterialFaceSet.begin() : 0;

	for (FTessellationData& Tessellation : BodyMesh.Faces)
	{
		FCADUUID ColorName = DefaultColorName;
		FCADUUID MaterialName = DefaultMaterialName;

		// Extract proper color or material based on style index
		uint32 CachedStyleIndex = Tessellation.MaterialName;
		Tessellation.MaterialName = 0;

		constexpr uint32 GraphStyleDataDefaultValue =  65535;
		if (CachedStyleIndex != GraphStyleDataDefaultValue)
		{
			ExtractGraphStyleProperties(CachedStyleIndex, ColorName, MaterialName);
		}

		if (ColorName)
		{
			Tessellation.ColorName = ColorName;
			BodyMesh.ColorSet.Add(ColorName);
		}

		if (MaterialName)
		{
			Tessellation.MaterialName = MaterialName;
			BodyMesh.MaterialSet.Add(MaterialName);
		}
	}

	Body.ColorFaceSet = BodyMesh.ColorSet;
	Body.MaterialFaceSet = BodyMesh.MaterialSet;

	// Write part's representation as Prc file if it is a BRep
	A3DEEntityType Type;
	A3DEntityGetType(Representation, &Type);

	if (Type == kA3DTypeRiBrepModel)
	{
		FString FilePath = CADFileData.GetBodyCachePath(Body.MeshActorName);
		if (!FilePath.IsEmpty())
		{
			TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();

			// Save body unit and default color and material attributes in a json string
			// This will be used when the file is reloaded
			JsonObject->SetNumberField(JSON_ENTRY_BODY_UNIT, Body.BodyUnit);
			JsonObject->SetNumberField(JSON_ENTRY_COLOR_NAME, DefaultColorName);
			JsonObject->SetNumberField(JSON_ENTRY_MATERIAL_NAME, DefaultMaterialName);

			FString JsonString;
			TSharedRef< TJsonWriter< TCHAR, TPrettyJsonPrintPolicy<TCHAR> > > JsonWriter = TJsonWriterFactory< TCHAR, TPrettyJsonPrintPolicy<TCHAR> >::Create(&JsonString);

			FJsonSerializer::Serialize(JsonObject.ToSharedRef(), JsonWriter);

			TechSoftUtils::SaveBodiesToPrcFile(&Representation, 1, FilePath, JsonString);
		}
	}
}

void FTechSoftFileParser::ReserveCADFileData()
{
	// TODO: Could be more accurate
	CountUnderModel();

	CADFileData.ReserveBodyMeshes(ComponentCount[EComponentType::Body]);

	FArchiveSceneGraph& SceneGraphArchive = CADFileData.GetSceneGraphArchive();
	SceneGraphArchive.Reserve(ComponentCount[EComponentType::Occurrence], ComponentCount[EComponentType::Reference], ComponentCount[EComponentType::Body]);

	uint32 MaterialNum = CountColorAndMaterial();
	SceneGraphArchive.MaterialHIdToMaterial.Reserve(MaterialNum);
}

void FTechSoftFileParser::CountUnderModel()
{
	TUniqueTSObj<A3DAsmModelFileData> ModelFileData(ModelFile.Get());
	if (!ModelFileData.IsValid())
	{
		return;
	}

	ComponentCount[EComponentType::Occurrence] ++;

	for (uint32 Index = 0; Index < ModelFileData->m_uiPOccurrencesSize; ++Index)
	{
		if (IsConfigurationSet(ModelFileData->m_ppPOccurrences[Index]))
		{
			CountUnderConfigurationSet(ModelFileData->m_ppPOccurrences[Index]);
		}
		else
		{
			CountUnderOccurrence(ModelFileData->m_ppPOccurrences[Index]);
		}
	}
}

ECADParsingResult FTechSoftFileParser::TraverseModel()
{
	TUniqueTSObj<A3DAsmModelFileData> ModelFileData(ModelFile.Get());
	if (!ModelFileData.IsValid())
	{
		return ECADParsingResult::ProcessFailed;
	}

	FEntityMetaData MetaData;
	ExtractMetaData(ModelFile.Get(), MetaData);
	ExtractSpecificMetaData(ModelFile.Get(), MetaData);
	BuildReferenceName(MetaData);

	for (uint32 Index = 0; Index < ModelFileData->m_uiPOccurrencesSize; ++Index)
	{
		if (IsConfigurationSet(ModelFileData->m_ppPOccurrences[Index]))
		{
			TraverseConfigurationSet(ModelFileData->m_ppPOccurrences[Index], FileUnit);
		}
		else
		{
			FString* ModelNamePtr = MetaData.MetaData.Find(TEXT("Name"));
			TraverseReference(ModelFileData->m_ppPOccurrences[Index], ModelNamePtr ? *ModelNamePtr : FString(), FMatrix::Identity, FileUnit);
		}
	}

	return ECADParsingResult::ProcessOk;
}

void FTechSoftFileParser::TraverseConfigurationSet(const A3DAsmProductOccurrence* ConfigurationSetPtr, double ParentUnit)
{
	TUniqueTSObj<A3DAsmProductOccurrenceData> ConfigurationSetData(ConfigurationSetPtr);
	if (!ConfigurationSetData.IsValid())
	{
		return;
	}

	FEntityMetaData MetaData;
	ExtractMetaData(ConfigurationSetPtr, MetaData);
	ExtractSpecificMetaData(ConfigurationSetPtr, MetaData);
	BuildReferenceName(MetaData);
	FString ConfigurationName = MetaData.MetaData.FindOrAdd(TEXT("Name"));

	const FString& ConfigurationToLoad = CADFileData.GetCADFileDescription().GetConfiguration();

	FMatrix TransformMatrix = FMatrix::Identity;;
	double OccurenceUnit = ParentUnit;
	A3DMiscTransformation* Location = ConfigurationSetData->m_pLocation;
	if (Location)
	{
		TransformMatrix = ExtractTransformation(Location, OccurenceUnit);
	}

	TUniqueTSObj<A3DAsmProductOccurrenceData> ConfigurationData;
	for (unsigned int Index = 0; Index < ConfigurationSetData->m_uiPOccurrencesSize; ++Index)
	{
		ConfigurationData.FillFrom(ConfigurationSetData->m_ppPOccurrences[Index]);
		if (!ConfigurationData.IsValid())
		{
			continue;
		}

		if (ConfigurationData->m_uiProductFlags & A3D_PRODUCT_FLAG_CONFIG)
		{
			bool bIsConfigurationToLoad = false;
			if (!ConfigurationToLoad.IsEmpty())
			{
				FEntityMetaData ConfigurationMetaData;
				ExtractMetaData(ConfigurationSetData->m_ppPOccurrences[Index], ConfigurationMetaData);
				const FString* Configuration = ConfigurationMetaData.MetaData.Find(TEXT("SDKName"));
				if (Configuration)
				{
					bIsConfigurationToLoad = (Configuration->Equals(ConfigurationToLoad));
				}
			}
			else
			{
				bIsConfigurationToLoad = ConfigurationData->m_uiProductFlags & A3D_PRODUCT_FLAG_DEFAULT;
			}

			if (bIsConfigurationToLoad)
			{
				TraverseReference(ConfigurationSetData->m_ppPOccurrences[Index], ConfigurationName, TransformMatrix, OccurenceUnit);
				return;
			}
		}
	}

	if (ConfigurationToLoad.IsEmpty())
	{
		// no default configuration, traverse the first configuration
		for (unsigned int Index = 0; Index < ConfigurationSetData->m_uiPOccurrencesSize; ++Index)
		{
			ConfigurationData.FillFrom(ConfigurationSetData->m_ppPOccurrences[Index]);
			if (!ConfigurationData.IsValid())
			{
				return;
			}

			if (ConfigurationData->m_uiProductFlags & A3D_PRODUCT_FLAG_CONFIG)
			{
				TraverseReference(ConfigurationSetData->m_ppPOccurrences[Index], ConfigurationName, TransformMatrix, OccurenceUnit);
			}
		}
	}
}

void FTechSoftFileParser::CountUnderConfigurationSet(const A3DAsmProductOccurrence* ConfigurationSetPtr)
{
	TUniqueTSObj<A3DAsmProductOccurrenceData> ConfigurationSetData(ConfigurationSetPtr);
	if (!ConfigurationSetData.IsValid())
	{
		return;
	}

	const FString& ConfigurationToLoad = CADFileData.GetCADFileDescription().GetConfiguration();

	TUniqueTSObj<A3DAsmProductOccurrenceData> ConfigurationData;
	for (unsigned int Index = 0; Index < ConfigurationSetData->m_uiPOccurrencesSize; ++Index)
	{
		ConfigurationData.FillFrom(ConfigurationSetData->m_ppPOccurrences[Index]);
		if (!ConfigurationData.IsValid())
		{
			continue;
		}

		if (ConfigurationData->m_uiProductFlags & A3D_PRODUCT_FLAG_CONFIG)
		{
			bool bIsConfigurationToLoad = false;
			if (!ConfigurationToLoad.IsEmpty())
			{
				FEntityMetaData ConfigurationMetaData;
				ExtractMetaData(ConfigurationSetData->m_ppPOccurrences[Index], ConfigurationMetaData);
				const FString* ConfigurationName = ConfigurationMetaData.MetaData.Find(TEXT("SDKName"));
				if (ConfigurationName)
				{
					bIsConfigurationToLoad = (ConfigurationName->Equals(ConfigurationToLoad));
				}
			}
			else
			{
				bIsConfigurationToLoad = ConfigurationData->m_uiProductFlags & A3D_PRODUCT_FLAG_DEFAULT;
			}

			if (bIsConfigurationToLoad)
			{
				CountUnderOccurrence(ConfigurationSetData->m_ppPOccurrences[Index]);
				return;
			}
		}
	}

	if (ConfigurationToLoad.IsEmpty())
	{
		// no default configuration, traverse the first configuration
		for (unsigned int Index = 0; Index < ConfigurationSetData->m_uiPOccurrencesSize; ++Index)
		{
			ConfigurationData.FillFrom(ConfigurationSetData->m_ppPOccurrences[Index]);
			if (!ConfigurationData.IsValid())
			{
				return;
			}

			if (ConfigurationData->m_uiProductFlags & A3D_PRODUCT_FLAG_CONFIG)
			{
				CountUnderOccurrence(ConfigurationSetData->m_ppPOccurrences[Index]);
			}
		}
	}
}

void FTechSoftFileParser::TraverseReference(const A3DAsmProductOccurrence* ReferencePtr, const FString& RootName, const FMatrix& ParentMatrix, double ParentUnit)
{
	FEntityMetaData MetaData;
	ExtractMetaData(ReferencePtr, MetaData);

	if (MetaData.bRemoved || !MetaData.bShow)
	{
		return;
	}

	ExtractSpecificMetaData(ReferencePtr, MetaData);

	if (!RootName.IsEmpty())
	{
		MetaData.MetaData.FindOrAdd(TEXT("Name"), RootName);
	}
	else
	{
		BuildReferenceName(MetaData);
	}

	FArchiveInstance EmptyInstance;
	FArchiveComponent& Component = FTechSoftFileParser::AddComponent(MetaData, EmptyInstance);

	TUniqueTSObj<A3DAsmProductOccurrenceData> ReferenceData(ReferencePtr);
	if (!ReferenceData.IsValid())
	{
		return;
	}

	FMatrix ReferenceMatrix = FMatrix::Identity;
	double ReferenceUnit = ParentUnit;

	A3DMiscTransformation* Location = ReferenceData->m_pLocation;
	if (Location)
	{
		ReferenceMatrix = ExtractTransformation(Location, ReferenceUnit);
	}

	Component.TransformMatrix = ParentMatrix * ReferenceMatrix;

	const FString& OccurenceNameBase = Component.MetaData.FindOrAdd(TEXT("Name"));
	for (uint32 OccurenceIndex = 0; OccurenceIndex < ReferenceData->m_uiPOccurrencesSize; ++OccurenceIndex)
	{
		int32 ChildrenId = TraverseOccurrence(ReferenceData->m_ppPOccurrences[OccurenceIndex], OccurenceNameBase + TEXT("_") + FString::FromInt(OccurenceIndex+1), ReferenceUnit);
		Component.Children.Add(ChildrenId);
	}

	if (ReferenceData->m_pPart)
	{
		TraversePartDefinition(ReferenceData->m_pPart, Component, ReferenceUnit);
	}
}

FArchiveInstance& FTechSoftFileParser::AddInstance(FEntityMetaData& InstanceMetaData)
{
	FCadId InstanceId = LastEntityId++;
	int32 InstanceIndex = CADFileData.AddInstance(InstanceId);
	FArchiveInstance& Instance = CADFileData.GetInstanceAt(InstanceIndex);
	Instance.MetaData = MoveTemp(InstanceMetaData.MetaData);
	return Instance;
}

FArchiveComponent& FTechSoftFileParser::AddComponent(FEntityMetaData& ComponentMetaData, FArchiveInstance& Instance)
{
	FCadId ComponentId = LastEntityId++;
	int32 ComponentIndex = CADFileData.AddComponent(ComponentId);
	FArchiveComponent& Prototype = CADFileData.GetComponentAt(ComponentIndex);
	Prototype.MetaData = MoveTemp(ComponentMetaData.MetaData);

	Instance.ReferenceNodeId = ComponentId;

	return Prototype;
}

FArchiveUnloadedComponent& FTechSoftFileParser::AddUnloadedComponent(FEntityMetaData& ComponentMetaData, FArchiveInstance& Instance)
{
	FCadId ComponentId = LastEntityId++;
	int32 ComponentIndex = CADFileData.AddUnloadedComponent(ComponentId);
	FArchiveUnloadedComponent& Component = CADFileData.GetUnloadedComponentAt(ComponentIndex);
	Component.MetaData = MoveTemp(ComponentMetaData.MetaData);

	Instance.bIsExternalReference = true;
	Instance.ReferenceNodeId = ComponentId;

	// Make sure that the ExternalFile path is right otherwise try to find the file and update.
	TechSoftFileParserImpl::UpdateFileDescriptor(ComponentMetaData.ExternalFile);

	Instance.ExternalReference = ComponentMetaData.ExternalFile;

	if (Format == ECADFormat::SOLIDWORKS /*|| CADFileData.FileFormat() == ECADFormat::JT*/)
	{
		if (FString* ConfigurationName = Component.MetaData.Find(TEXT("ConfigurationName")))
		{
			Instance.ExternalReference.SetConfiguration(*ConfigurationName);
		}
	}

	CADFileData.AddExternalRef(Instance.ExternalReference);

	return Component;
}

FArchiveComponent& FTechSoftFileParser::AddOccurence(FEntityMetaData& InstanceMetaData, FCadId& OutComponentId)
{
	FArchiveInstance& Instance = AddInstance(InstanceMetaData);
	OutComponentId = Instance.ObjectId;
	FEntityMetaData ReferenceMetaData;
	FArchiveComponent& Prototype = AddComponent(ReferenceMetaData, Instance);
	return Prototype;
}

FArchiveComponent& FTechSoftFileParser::AddOccurence(FEntityMetaData& InstanceMetaData, FEntityMetaData& ReferenceMetaData, FCadId& OutComponentId)
{
	FArchiveInstance& Instance = AddInstance(InstanceMetaData);
	OutComponentId = Instance.ObjectId;
	FArchiveComponent& Prototype = AddComponent(ReferenceMetaData, Instance);
	return Prototype;
}

int32 FTechSoftFileParser::AddBody(FEntityMetaData& BodyMetaData, const FMatrix& Matrix, const FCadId ParentId, double BodyUnit)
{
	FCadId BodyId = LastEntityId++;
	int32 BodyIndex = CADFileData.AddBody(BodyId);
	FArchiveBody& Body = CADFileData.GetBodyAt(BodyIndex);
	Body.ParentId = ParentId;
	Body.TransformMatrix = Matrix;
	Body.BodyUnit = BodyUnit;
	Body.MetaData = MoveTemp(BodyMetaData.MetaData);
	if (BodyMetaData.ColorName != 0)
	{
		Body.ColorFaceSet.Add(BodyMetaData.ColorName);
	}
	if (BodyMetaData.MaterialName != 0)
	{
		Body.MaterialFaceSet.Add(BodyMetaData.MaterialName);
	}
	return BodyIndex;
}

FCadId FTechSoftFileParser::TraverseOccurrence(const A3DAsmProductOccurrence* OccurrencePtr, const FString& DefaultOccurrenceName, double ParentUnit)
{
	// first product occurence with m_pPart != nullptr || m_uiPOccurrencesSize > 0
	const A3DAsmProductOccurrence* CachedOccurrencePtr = OccurrencePtr;
	TUniqueTSObj<A3DAsmProductOccurrenceData> OccurrenceData(OccurrencePtr);
	if (!OccurrenceData.IsValid())
	{
		return 0;
	}

	bool bContinueTraverse = OccurrenceData->m_pPrototype
		|| OccurrenceData->m_pExternalData
		|| OccurrenceData->m_pPart
		|| OccurrenceData->m_uiPOccurrencesSize > 0;
	if (!bContinueTraverse)
	{
		return 0;
	}

	// Todo
	// the life time of FEntityMetaData end at the creation of FArchiveInstance.
	// This variable should not be accessible after
	FEntityMetaData InstanceMetaData;
	ExtractMetaData(OccurrencePtr, InstanceMetaData);

	if (InstanceMetaData.bRemoved || !InstanceMetaData.bShow)
	{
		return 0;
	}

	ExtractSpecificMetaData(OccurrencePtr, InstanceMetaData);
	BuildInstanceName(InstanceMetaData, DefaultOccurrenceName);

	FArchiveInstance& Instance = AddInstance(InstanceMetaData);

	A3DMiscTransformation* Location = OccurrenceData->m_pLocation;

	// Todo
	// the life time of FEntityMetaData end at the creation of FArchive(Unloaded)Component.
	// This variable should not be accessible after
	FEntityMetaData PrototypeMetaData;
	if (OccurrenceData->m_pPrototype)
	{
		ProcessPrototype(OccurrenceData->m_pPrototype, PrototypeMetaData, &Location);
	}

	double OccurrenceUnit = ParentUnit;
	if (Location)
	{
		Instance.TransformMatrix = ExtractTransformation(Location, OccurrenceUnit);
	}

	if (PrototypeMetaData.bUnloaded)
	{
		FArchiveUnloadedComponent& UnloadedComponent = AddUnloadedComponent(PrototypeMetaData, Instance);
		return Instance.ObjectId;
	}
	
	// If the prototype hasn't name, set its name with the name of the instance 
	{
		FString& PrototypeName = PrototypeMetaData.MetaData.FindOrAdd(TEXT("Name"));
		if (PrototypeName.IsEmpty())
		{
			const FString& InstanceName = Instance.MetaData.FindOrAdd(TEXT("Name"));
			PrototypeName = InstanceName;
		}
	}

	FArchiveComponent& Component = AddComponent(PrototypeMetaData, Instance);

	while (OccurrenceData->m_pPrototype != nullptr && OccurrenceData->m_pPart == nullptr && OccurrenceData->m_uiPOccurrencesSize == 0 && OccurrenceData->m_pExternalData == nullptr)
	{
		CachedOccurrencePtr = OccurrenceData->m_pPrototype;
		OccurrenceData.FillFrom(OccurrenceData->m_pPrototype);
	}

	if(OccurrenceData->m_pPart == nullptr && OccurrenceData->m_uiPOccurrencesSize == 0 && OccurrenceData->m_pExternalData == nullptr)
	{
		return Instance.ObjectId;
	}

	// Add part
	while (OccurrenceData->m_pPrototype != nullptr && OccurrenceData->m_pPart == nullptr)
	{
		OccurrenceData.FillFrom(OccurrenceData->m_pPrototype);
	}
	if (OccurrenceData->m_pPart != nullptr)
	{
		A3DAsmPartDefinition* PartDefinition = OccurrenceData->m_pPart;
		TraversePartDefinition(PartDefinition, Component, OccurrenceUnit);
	}

	// Add Occurrence's Children
	OccurrenceData.FillFrom(CachedOccurrencePtr);
	while (OccurrenceData->m_pPrototype != nullptr && OccurrenceData->m_uiPOccurrencesSize == 0)
	{
		OccurrenceData.FillFrom(OccurrenceData->m_pPrototype);
	}

	uint32 ChildrenCount = OccurrenceData->m_uiPOccurrencesSize;
	A3DAsmProductOccurrence** Children = OccurrenceData->m_ppPOccurrences;
	const FString& InstanceName = Instance.MetaData.FindOrAdd(TEXT("Name"));
	for (uint32 Index = 0; Index < ChildrenCount; ++Index)
	{
		const FString DefaultChildName = InstanceName + TEXT("_") + FString::FromInt(Index + 1);
		int32 ChildrenId = TraverseOccurrence(Children[Index], DefaultChildName, OccurrenceUnit);
		Component.Children.Add(ChildrenId);
	}

	// Add External data
	OccurrenceData.FillFrom(CachedOccurrencePtr);
	while (OccurrenceData->m_pPrototype != nullptr && OccurrenceData->m_pExternalData == nullptr)
	{
		OccurrenceData.FillFrom(OccurrenceData->m_pPrototype);
	}
	if (OccurrenceData->m_pExternalData != nullptr)
	{
		TUniqueTSObj<A3DAsmProductOccurrenceData> ExternalData(OccurrenceData->m_pExternalData);
		if (ExternalData->m_pPart != nullptr)
		{
			A3DAsmPartDefinition* PartDefinition = ExternalData->m_pPart;
			TraversePartDefinition(PartDefinition, Component, OccurrenceUnit);
		}

		uint32 ExternalChildrenCount = ExternalData->m_uiPOccurrencesSize;
		A3DAsmProductOccurrence** ExternalChildren = ExternalData->m_ppPOccurrences;
		for (uint32 Index = 0; Index < ExternalChildrenCount; ++Index)
		{
			const FString DefaultChildName = InstanceName + TEXT("_") + FString::FromInt(Index + 1);
			int32 ChildId = TraverseOccurrence(ExternalChildren[Index], DefaultChildName, OccurrenceUnit);
			Component.Children.Add(ChildId);
		}
	}

	return Instance.ObjectId;
}

void FTechSoftFileParser::CountUnderOccurrence(const A3DAsmProductOccurrence* Occurrence)
{
	TUniqueTSObj<A3DAsmProductOccurrenceData> OccurrenceData(Occurrence);
	if (Occurrence && OccurrenceData.IsValid())
	{
		ComponentCount[EComponentType::Occurrence]++;
		ComponentCount[EComponentType::Reference]++;

		const A3DAsmProductOccurrence* CachedOccurrencePtr = Occurrence;
		while (OccurrenceData->m_pPrototype != nullptr && OccurrenceData->m_pPart == nullptr && OccurrenceData->m_uiPOccurrencesSize == 0 && OccurrenceData->m_pExternalData == nullptr)
		{
			CachedOccurrencePtr = OccurrenceData->m_pPrototype;
			OccurrenceData.FillFrom(OccurrenceData->m_pPrototype);
		}

		if (OccurrenceData->m_pPart == nullptr && OccurrenceData->m_uiPOccurrencesSize == 0 && OccurrenceData->m_pExternalData == nullptr)
		{
			return;
		}

		// count under part
		while (OccurrenceData->m_pPrototype != nullptr && OccurrenceData->m_pPart == nullptr)
		{
			OccurrenceData.FillFrom(OccurrenceData->m_pPrototype);
		}
		if (OccurrenceData->m_pPart != nullptr)
		{
			CountUnderPartDefinition(OccurrenceData->m_pPart);
		}

		// count under Occurrence
		OccurrenceData.FillFrom(CachedOccurrencePtr);
		while (OccurrenceData->m_pPrototype != nullptr && OccurrenceData->m_uiPOccurrencesSize == 0)
		{
			OccurrenceData.FillFrom(OccurrenceData->m_pPrototype);
		}

		uint32 ChildrenCount = OccurrenceData->m_uiPOccurrencesSize;
		A3DAsmProductOccurrence** Children = OccurrenceData->m_ppPOccurrences;
		for (uint32 Index = 0; Index < ChildrenCount; ++Index)
		{
			CountUnderOccurrence(Children[Index]);
		}

		// count under External data
		OccurrenceData.FillFrom(CachedOccurrencePtr);
		while (OccurrenceData->m_pPrototype != nullptr && OccurrenceData->m_pExternalData == nullptr)
		{
			OccurrenceData.FillFrom(OccurrenceData->m_pPrototype);
		}
		if (OccurrenceData->m_pExternalData != nullptr)
		{
			TUniqueTSObj<A3DAsmProductOccurrenceData> ExternalData(OccurrenceData->m_pExternalData);
			if (ExternalData->m_pPart != nullptr)
			{
				CountUnderPartDefinition(ExternalData->m_pPart);
			}

			uint32 ExternalChildrenCount = ExternalData->m_uiPOccurrencesSize;
			A3DAsmProductOccurrence** ExternalChildren = ExternalData->m_ppPOccurrences;
			for (uint32 Index = 0; Index < ExternalChildrenCount; ++Index)
			{
				CountUnderOccurrence(ExternalChildren[Index]);
			}
		}
	}
}

void FTechSoftFileParser::CountUnderSubPrototype(const A3DAsmProductOccurrence* InPrototypePtr)
{
	TUniqueTSObj<A3DAsmProductOccurrenceData> SubPrototypeData(InPrototypePtr);
	if (!SubPrototypeData.IsValid())
	{
		return;
	}

	for (uint32 Index = 0; Index < SubPrototypeData->m_uiPOccurrencesSize; ++Index)
	{
		CountUnderOccurrence(SubPrototypeData->m_ppPOccurrences[Index]);
	}

	if (SubPrototypeData->m_pPart)
	{
		CountUnderPartDefinition(SubPrototypeData->m_pPart);
	}

	if (SubPrototypeData->m_pPrototype && !SubPrototypeData->m_uiPOccurrencesSize && !SubPrototypeData->m_pPart)
	{
		CountUnderSubPrototype(SubPrototypeData->m_pPrototype);
	}
}

void FTechSoftFileParser::ProcessPrototype(const A3DAsmProductOccurrence* InPrototypePtr, FEntityMetaData& OutPrototypeMetaData, A3DMiscTransformation** OutLocation)
{
	OutPrototypeMetaData.bUnloaded = true;

	const A3DAsmProductOccurrence* PrototypePtr = InPrototypePtr;
	TUniqueTSObj<A3DAsmProductOccurrenceData> PrototypeData;
	while (PrototypePtr)
	{
		PrototypeData.FillFrom(PrototypePtr);
		if (!PrototypeData.IsValid())
		{
			return;
		}

		if (PrototypeData->m_pPart != nullptr || PrototypeData->m_uiPOccurrencesSize != 0 || PrototypeData->m_pExternalData != nullptr || PrototypeData->m_pPrototype == nullptr)
		{
			ExtractMetaData(PrototypePtr, OutPrototypeMetaData);
			ExtractSpecificMetaData(PrototypePtr, OutPrototypeMetaData);

			TUniqueTSObj<A3DUTF8Char*> FilePathUTF8Ptr;
			FilePathUTF8Ptr.FillWith(&TechSoftInterface::GetFilePathName, PrototypePtr);
			if (!FilePathUTF8Ptr.IsValid() || *FilePathUTF8Ptr == nullptr)
			{
				FilePathUTF8Ptr.FillWith(&TechSoftInterface::GetOriginalFilePathName, PrototypePtr);
			}
			if (FilePathUTF8Ptr.IsValid() && *FilePathUTF8Ptr != nullptr)
			{
				FString FilePath = UTF8_TO_TCHAR(*FilePathUTF8Ptr);
				FPaths::NormalizeFilename(FilePath);
				FString FileName = FPaths::GetCleanFilename(FilePath);
				if (!FileName.IsEmpty() && FileName != CADFileData.GetCADFileDescription().GetFileName())
				{
					OutPrototypeMetaData.ExternalFile = FFileDescriptor(*FilePath, nullptr, *CADFileData.GetCADFileDescription().GetRootFolder());
				}
			}
		}
		
		// Case of DWG file, external reference is in fact internal data as a part or a component
		// Case of CATProduct referencing CGR files: unloaded references are saved in ExternalData. In this case non empty ExternalFile means unloaded reference
		if (PrototypeData->m_pPart != nullptr|| PrototypeData->m_uiPOccurrencesSize != 0 || (PrototypeData->m_pExternalData != nullptr && OutPrototypeMetaData.ExternalFile.IsEmpty()))
		{
			OutPrototypeMetaData.bUnloaded = false;
			PrototypePtr = nullptr;
		}
		else
		{
			PrototypePtr = PrototypeData->m_pPrototype;
		}

		if (!*OutLocation)
		{
			*OutLocation = PrototypeData->m_pLocation;
		}
	}

	if (!*OutLocation)
	{
		while (PrototypeData.IsValid() && PrototypeData->m_pLocation == nullptr && PrototypeData->m_pPrototype != nullptr)
		{
			PrototypeData.FillFrom(PrototypeData->m_pPrototype);
		}
		if (PrototypeData.IsValid())
		{
			*OutLocation = PrototypeData->m_pLocation;
		}
	}

	if (OutPrototypeMetaData.bUnloaded)
	{
		OutPrototypeMetaData.MetaData.Add(TEXT("Name"), OutPrototypeMetaData.ExternalFile.GetFileName());
	}
	else
	{
		OutPrototypeMetaData.ExternalFile.Empty();
	}

	BuildReferenceName(OutPrototypeMetaData);
}

void FTechSoftFileParser::CountUnderPrototype(const A3DAsmProductOccurrence* Prototype)
{
	TUniqueTSObj<A3DAsmProductOccurrenceData> PrototypeData(Prototype);
	if (!PrototypeData.IsValid())
	{
		return;
	}

	ComponentCount[EComponentType::Reference] ++;
}

void FTechSoftFileParser::TraversePartDefinition(const A3DAsmPartDefinition* PartDefinitionPtr, FArchiveComponent& Part, double ParentUnit)
{
	FEntityMetaData PartMetaData;
	ExtractMetaData(PartDefinitionPtr, PartMetaData);

	if (PartMetaData.bRemoved || !PartMetaData.bShow)
	{
		return;
	}

	ExtractSpecificMetaData(PartDefinitionPtr, PartMetaData);

	// A top/down propagation of the color and material is needed. This is done in 5.0.3 with minimal code modification. 
	// However the model parsing need to be rewrite in the next version. (Jira UE-152691)
	const FString* Material = Part.MetaData.Find(TEXT("MaterialName"));
	const FString* Color = Part.MetaData.Find(TEXT("ColorName"));
	if (Material && !Material->IsEmpty() && PartMetaData.MaterialName == 0)
	{
		uint32 MaterialName = (uint32) FCString::Atoi64(**Material);
		PartMetaData.MetaData.Add(TEXT("Material"), *Material);
		PartMetaData.MaterialName = MaterialName;
	}
	if (Color && !Color->IsEmpty() && PartMetaData.ColorName == 0)
	{
		uint32 ColorName = (uint32)FCString::Atoi64(**Color);
		PartMetaData.MetaData.Add(TEXT("Color"), *Color);
		PartMetaData.ColorName = ColorName;
	}

	BuildPartName(PartMetaData, Part);

	TUniqueTSObj<A3DAsmPartDefinitionData> PartData(PartDefinitionPtr);
	if (PartData.IsValid())
	{
		for (unsigned int Index = 0; Index < PartData->m_uiRepItemsSize; ++Index)
		{
			int32 ChildId = TraverseRepresentationItem(PartData->m_ppRepItems[Index], PartMetaData, Part.ObjectId, ParentUnit, Index);
			Part.Children.Add(ChildId);
		}
	}
}

void FTechSoftFileParser::CountUnderPartDefinition(const A3DAsmPartDefinition* PartDefinition)
{
	TUniqueTSObj<A3DAsmPartDefinitionData> PartData(PartDefinition);
	if (PartDefinition && PartData.IsValid())
	{
		ComponentCount[EComponentType::Reference] ++;
		ComponentCount[EComponentType::Occurrence] ++;

		for (unsigned int Index = 0; Index < PartData->m_uiRepItemsSize; ++Index)
		{
			CountUnderRepresentationItem(PartData->m_ppRepItems[Index]);
		}
	}
}

FCadId FTechSoftFileParser::TraverseRepresentationItem(A3DRiRepresentationItem* RepresentationItem, const FEntityMetaData& PartMetaData, const FCadId ParentId, double ParentUnit, int32 ItemIndex)
{
	A3DEEntityType Type;
	A3DEntityGetType(RepresentationItem, &Type);

	switch (Type)
	{
	case kA3DTypeRiSet:
		return TraverseRepresentationSet(RepresentationItem, PartMetaData, ParentUnit);
	case kA3DTypeRiBrepModel:
		return TraverseBRepModel(RepresentationItem, PartMetaData, ParentId, ParentUnit, ItemIndex);
	case kA3DTypeRiPolyBrepModel:
		return TraversePolyBRepModel(RepresentationItem, PartMetaData, ParentId, ParentUnit, ItemIndex);
	default:
		break;
	}
	return 0;
}

void FTechSoftFileParser::CountUnderRepresentationItem(const A3DRiRepresentationItem* RepresentationItem)
{
	A3DEEntityType Type;
	A3DEntityGetType(RepresentationItem, &Type);

	switch (Type)
	{
	case kA3DTypeRiSet:
		CountUnderRepresentationSet(RepresentationItem);
		break;
	case kA3DTypeRiBrepModel:
	case kA3DTypeRiPolyBrepModel:
		ComponentCount[EComponentType::Body] ++;
		break;

	default:
		break;
	}
}

FCadId FTechSoftFileParser::TraverseRepresentationSet(const A3DRiSet* RepresentationSetPtr, const FEntityMetaData& PartMetaData, double ParentUnit)
{
	TUniqueTSObj<A3DRiSetData> RepresentationSetData(RepresentationSetPtr);
	if (!RepresentationSetData.IsValid())
	{
		return 0;
	}

	FEntityMetaData RepresentationSetMetaData;
	ExtractMetaData(RepresentationSetPtr, RepresentationSetMetaData);

	if (RepresentationSetMetaData.bRemoved || !RepresentationSetMetaData.bShow)
	{
		return 0;
	}

	FCadId RepresentationSetId = 0;
	FArchiveComponent& RepresentationSet = AddOccurence(RepresentationSetMetaData, RepresentationSetId);

	for (A3DUns32 Index = 0; Index < RepresentationSetData->m_uiRepItemsSize; ++Index)
	{
		int32 ChildId = TraverseRepresentationItem(RepresentationSetData->m_ppRepItems[Index], RepresentationSetMetaData, RepresentationSet.ObjectId, ParentUnit, Index);
		RepresentationSet.Children.Add(ChildId);
	}
	return RepresentationSetId;
}

void FTechSoftFileParser::CountUnderRepresentationSet(const A3DRiSet* RepresentationSet)
{
	TUniqueTSObj<A3DRiSetData> RepresentationSetData(RepresentationSet);
	if (RepresentationSet && RepresentationSetData.IsValid())
	{
		ComponentCount[EComponentType::Occurrence] ++;
		ComponentCount[EComponentType::Reference] ++;

		for (A3DUns32 Index = 0; Index < RepresentationSetData->m_uiRepItemsSize; ++Index)
		{
			CountUnderRepresentationItem(RepresentationSetData->m_ppRepItems[Index]);
		}
	}
}

FCadId FTechSoftFileParser::TraverseBRepModel(A3DRiBrepModel* BRepModelPtr, const FEntityMetaData& PartMetaData, const FCadId ParentId, double ParentUnit, int32 ItemIndex)
{
	if (!BRepModelPtr)
	{
		return 0;
	}

	FEntityMetaData BRepMetaData;
	ExtractMetaData(BRepModelPtr, BRepMetaData);

	if (!BRepMetaData.bShow || BRepMetaData.bRemoved)
	{
		return 0;
	}

	TUniqueTSObj<A3DRiBrepModelData> BRepModelData(BRepModelPtr);
	BuildBodyName(BRepMetaData, PartMetaData, ItemIndex, (bool) BRepModelData->m_bSolid);

	if (int32* BodyIndexPtr = RepresentationItemsCache.Find(BRepModelPtr))
	{
		return CADFileData.GetBodyAt(*BodyIndexPtr).ObjectId;
	}

	// if BRep model has not material or color, use the ones from the part
	// A top/down propagation of the color and material is needed. This is done in 5.0.3 with minimal code modification. 
	// However the model parsing need to be rewrite in the next version. (Jira UE-152691)
	if (BRepMetaData.MaterialName == 0 && BRepMetaData.ColorName == 0)
	{
		if (PartMetaData.MaterialName)
		{
			BRepMetaData.MaterialName = PartMetaData.MaterialName;
			BRepMetaData.MetaData.Add(TEXT("MaterialName"), FString::Printf(TEXT("%u"), PartMetaData.MaterialName));
		}
		if (PartMetaData.ColorName)
		{
			BRepMetaData.ColorName = PartMetaData.ColorName;
			BRepMetaData.MetaData.Add(TEXT("ColorName"), FString::Printf(TEXT("%u"), PartMetaData.ColorName));
		}
	}

	ExtractSpecificMetaData(BRepModelPtr, BRepMetaData);

	FMatrix Matrix = FMatrix::Identity;
	double BRepUnit = ParentUnit;
	TUniqueTSObj<A3DRiRepresentationItemData> RepresentationData(BRepModelPtr);
	if (RepresentationData->m_pCoordinateSystem)
	{
		Matrix = ExtractCoordinateSystem(RepresentationData->m_pCoordinateSystem, BRepUnit);
	}

	int32 BodyIndex = AddBody(BRepMetaData, Matrix, ParentId, BRepUnit);
	FArchiveBody& Body = CADFileData.GetBodyAt(BodyIndex);

	RepresentationItemsCache.Add(BRepModelPtr, BodyIndex);

	return Body.ObjectId;
}

FCadId FTechSoftFileParser::TraversePolyBRepModel(A3DRiPolyBrepModel* PolygonalPtr, const FEntityMetaData& PartMetaData, const FCadId ParentId, double ParentUnit, int32 ItemIndex)
{
	if (!PolygonalPtr)
	{
		return 0;
	}

	FEntityMetaData BRepMetaData;
	ExtractMetaData(PolygonalPtr, BRepMetaData);

	if (!BRepMetaData.bShow || BRepMetaData.bRemoved)
	{
		return 0;
	}

	TUniqueTSObj<A3DRiPolyBrepModelData> BRepModelData(PolygonalPtr);
	BuildBodyName(BRepMetaData, PartMetaData, ItemIndex, (bool) BRepModelData->m_bIsClosed);

	if (int32* BodyIndexPtr = RepresentationItemsCache.Find(PolygonalPtr))
	{
		return CADFileData.GetBodyAt(*BodyIndexPtr).ObjectId;
	}

	// if BRep model has not material or color, add part one
	if (BRepMetaData.MaterialName == 0 && BRepMetaData.ColorName == 0)
	{
		if (PartMetaData.MaterialName)
		{
			BRepMetaData.MaterialName = PartMetaData.MaterialName;
			BRepMetaData.MetaData.Add(TEXT("MaterialName"), FString::Printf(TEXT("%u"), PartMetaData.MaterialName));
		}
		if (PartMetaData.ColorName)
		{
			BRepMetaData.ColorName = PartMetaData.ColorName;
			BRepMetaData.MetaData.Add(TEXT("ColorName"), FString::Printf(TEXT("%u"), PartMetaData.ColorName));
		}
	}

	ExtractSpecificMetaData(PolygonalPtr, BRepMetaData);

	FMatrix Matrix = FMatrix::Identity;
	double BRepUnit = ParentUnit;
	TUniqueTSObj<A3DRiRepresentationItemData> RepresentationData(PolygonalPtr);
	if (RepresentationData->m_pCoordinateSystem)
	{
		Matrix = ExtractCoordinateSystem(RepresentationData->m_pCoordinateSystem, BRepUnit);
	}

	int32 BodyIndex = AddBody(BRepMetaData, Matrix, ParentId, BRepUnit);
	FArchiveBody& Body = CADFileData.GetBodyAt(BodyIndex);

	RepresentationItemsCache.Add(PolygonalPtr, BodyIndex);

	return Body.ObjectId;
}

void FTechSoftFileParser::ExtractMetaData(const A3DEntity* Entity, FEntityMetaData& OutMetaData)
{
	TUniqueTSObj<A3DRootBaseData> MetaData(Entity);
	if (MetaData.IsValid())
	{
		if (MetaData->m_pcName && MetaData->m_pcName[0] != '\0')
		{
			FString SDKName = UTF8_TO_TCHAR(MetaData->m_pcName);
			if(SDKName != TEXT("unnamed"))  // "unnamed" is create by Techsoft. This name is ignored 
			{
				SDKName = TechSoftUtils::CleanSdkName(SDKName);
				OutMetaData.MetaData.Emplace(TEXT("SDKName"), SDKName);
			}
		}

		TUniqueTSObj<A3DMiscAttributeData> AttributeData;
		for (A3DUns32 Index = 0; Index < MetaData->m_uiSize; ++Index)
		{
			AttributeData.FillFrom(MetaData->m_ppAttributes[Index]);
			if (AttributeData.IsValid())
			{
				TechSoftFileParserImpl::TraverseAttribute(*AttributeData, OutMetaData.MetaData);
			}
		}
	}

	if (A3DEntityIsBaseWithGraphicsType(Entity))
	{
		TUniqueTSObj<A3DRootBaseWithGraphicsData> MetaDataWithGraphics(Entity);
		if (MetaDataWithGraphics.IsValid())
		{
			if (MetaDataWithGraphics->m_pGraphics != NULL)
			{
				ExtractGraphicProperties(MetaDataWithGraphics->m_pGraphics, OutMetaData);
			}
		}
	}
}

void FTechSoftFileParser::BuildReferenceName(FEntityMetaData& ReferenceData)
{
	TMap<FString, FString>& MetaData = ReferenceData.MetaData;
	if (MetaData.IsEmpty())
	{
		return;
	}

	FString* NamePtr = MetaData.Find(TEXT("InstanceName"));
	if (NamePtr != nullptr && !NamePtr->IsEmpty())
	{
		FString& Name = MetaData.FindOrAdd(TEXT("Name"));
		Name = *NamePtr;
		if (Format == ECADFormat::CATIA)
		{
			Name = TechSoftUtils::CleanCatiaReferenceName(Name);
		}
		return;
	}

	if (Format == ECADFormat::JT)
	{
		if (TechSoftUtils::ReplaceOrAddNameValue(MetaData, TEXT("SDKName")))
		{
			return;
		}
	}

	if (TechSoftUtils::CheckIfNameExists(MetaData))
	{
		return;
	}

	if (TechSoftUtils::ReplaceOrAddNameValue(MetaData, TEXT("PartNumber")))
	{
		return;
	}

	NamePtr = MetaData.Find(TEXT("SDKName"));
	if (NamePtr != nullptr && !NamePtr->IsEmpty())
	{
		FString SdkName = *NamePtr;

		switch (Format)
		{
		case ECADFormat::CATIA_3DXML:
			SdkName = TechSoftUtils::Clean3dxmlReferenceSdkName(SdkName);
			break;

		case ECADFormat::SOLIDWORKS:
			SdkName = TechSoftUtils::CleanSwReferenceSdkName(SdkName);
			break;

		default:
			break;
		}

		MetaData.FindOrAdd(TEXT("Name"), SdkName);
		return;
	}
}

void FTechSoftFileParser::BuildInstanceName(FEntityMetaData& InstanceData, const FString& DefaultName)
{
	TMap<FString, FString>& MetaData = InstanceData.MetaData;

	if (TechSoftFileParserImpl::ReplaceOrAddNameValue(MetaData, TEXT("InstanceName")))
	{
		return;
	}

	if (TechSoftFileParserImpl::CheckIfNameExists(MetaData))
	{
		return;
	}

	FString* NamePtr = MetaData.Find(TEXT("SDKName"));
	if (NamePtr != nullptr && !NamePtr->IsEmpty())
	{
		FString SdkName = *NamePtr;

		switch (Format)
		{
		case ECADFormat::CATIA:
			SdkName = TechSoftUtils::CleanCatiaInstanceSdkName(SdkName);
			break;
		
		case ECADFormat::CATIA_3DXML:
			SdkName = TechSoftUtils::Clean3dxmlInstanceSdkName(SdkName);
			break;

		case ECADFormat::SOLIDWORKS:
			SdkName = TechSoftUtils::CleanSwInstanceSdkName(SdkName);
			break;

		default:
			break;
		}

		MetaData.FindOrAdd(TEXT("Name"), SdkName);
		return;
	}

	if (TechSoftFileParserImpl::ReplaceOrAddNameValue(MetaData, TEXT("PartNumber")))
	{
		return;
	}

	FString& Name = MetaData.FindOrAdd(TEXT("Name"));
	if (Name.IsEmpty())
	{
		Name = DefaultName;
	}
}

void FTechSoftFileParser::BuildPartName(FEntityMetaData& PartData, const FArchiveComponent& Component)
{
	TMap<FString, FString>& MetaData = PartData.MetaData;
	if (!MetaData.IsEmpty())
	{
		if (TechSoftFileParserImpl::CheckIfNameExists(MetaData))
		{
			return;
		}

		if (TechSoftFileParserImpl::ReplaceOrAddNameValue(MetaData, TEXT("PartNumber")))
		{
			return;
		}

		if (TechSoftFileParserImpl::ReplaceOrAddNameValue(MetaData, TEXT("SDKName")))
		{
			return;
		}
	}

	// If a name hasn't been found for the Part, the name of its parent (component) is used
	const FString* ComponentName = Component.MetaData.Find(TEXT("Name"));
	if (ComponentName && !ComponentName->IsEmpty())
	{
		MetaData.Add(TEXT("Name"), *ComponentName);
		return;
	}
}

void FTechSoftFileParser::BuildBodyName(FEntityMetaData& BodyData, const FEntityMetaData& PartMetaData, int32 ItemIndex, bool bIsSolid)
{
	TMap<FString, FString>& MetaData = BodyData.MetaData;

	if (TechSoftUtils::CheckIfNameExists(MetaData))
	{
		return;
	}

	FString* NamePtr = MetaData.Find(TEXT("SDKName"));
	if (NamePtr != nullptr)
	{
		FString SdkName = *NamePtr;
		if (Format == ECADFormat::CREO)
		{
			SdkName = TechSoftUtils::CleanNameByRemoving_prt(SdkName);
		}

		MetaData.FindOrAdd(TEXT("Name"), SdkName);
		return;
	}

	if (Format == ECADFormat::CATIA)
	{
		NamePtr = MetaData.Find(TEXT("BodyID"));
		if (NamePtr != nullptr && !NamePtr->IsEmpty())
		{
			MetaData.FindOrAdd(TEXT("Name"), *NamePtr);
			return;
		}
	}

	FString& Name = MetaData.FindOrAdd(TEXT("Name"));
	if (Name.IsEmpty())
	{
		const FString* PartName = PartMetaData.MetaData.Find(TEXT("Name"));
		if (PartName != nullptr && !PartName->IsEmpty())
		{
			Name = *PartName;
		}

		if (!Name.IsEmpty())
		{
			Name += TEXT("_body");
		}
		else
		{
			if (bIsSolid)
			{
				Name = TEXT("Solid");
			}
			else
			{
				Name = TEXT("Shell");
			}
		}
		Name += FString::FromInt(ItemIndex + 1);
	}
}

void FTechSoftFileParser::ExtractSpecificMetaData(const A3DAsmProductOccurrence* Occurrence, FEntityMetaData& OutMetaData)
{
	//----------- Export Specific information per CAD format -----------
	switch (ModellerType)
	{
	case ModellerSlw:
	{
		TUniqueTSObj<A3DAsmProductOccurrenceDataSLW> SolidWorksSpecificData(Occurrence);
		if (SolidWorksSpecificData.IsValid())
		{
			if (SolidWorksSpecificData->m_psCfgName)
			{
				FString ConfigurationName = UTF8_TO_TCHAR(SolidWorksSpecificData->m_psCfgName);
				OutMetaData.MetaData.Emplace(TEXT("ConfigurationName"), ConfigurationName);
				FString ConfigurationIndex = FString::FromInt(SolidWorksSpecificData->m_iIndexCfg);
				OutMetaData.MetaData.Emplace(TEXT("ConfigurationIndex"), ConfigurationIndex);
			}
		}
		break;
	}
	case ModellerUnigraphics:
	{
		TUniqueTSObj<A3DAsmProductOccurrenceDataUg> UnigraphicsSpecificData(Occurrence);
		if (UnigraphicsSpecificData.IsValid())
		{
			if (UnigraphicsSpecificData->m_psPartUID)
			{
				FString PartUID = UTF8_TO_TCHAR(UnigraphicsSpecificData->m_psPartUID);
				OutMetaData.MetaData.Emplace(TEXT("UnigraphicsPartUID"), PartUID);
			}
			if (UnigraphicsSpecificData->m_psInstanceFileName)
			{
				FString InstanceFileName = UTF8_TO_TCHAR(UnigraphicsSpecificData->m_psInstanceFileName);
				OutMetaData.MetaData.Emplace(TEXT("UnigraphicsPartUID"), InstanceFileName);
			}

			if (UnigraphicsSpecificData->m_uiInstanceTag)
			{
				FString InstanceTag = FString::FromInt(UnigraphicsSpecificData->m_uiInstanceTag);
				OutMetaData.MetaData.Emplace(TEXT("UnigraphicsInstanceTag"), InstanceTag);
			}
		}
		break;
	}

	case ModellerCatiaV5:
	{
		TUniqueTSObj<A3DAsmProductOccurrenceDataCV5> CatiaV5SpecificData(Occurrence);
		if (CatiaV5SpecificData.IsValid())
		{
			if (CatiaV5SpecificData->m_psVersion)
			{
				FString Version = UTF8_TO_TCHAR(CatiaV5SpecificData->m_psVersion);
				OutMetaData.MetaData.Emplace(TEXT("CatiaVersion"), Version);
			}

			if (CatiaV5SpecificData->m_psPartNumber)
			{
				FString PartNumber = UTF8_TO_TCHAR(CatiaV5SpecificData->m_psPartNumber);
				OutMetaData.MetaData.Emplace(TEXT("CatiaPartNumber"), PartNumber);
			}
		}
		break;
	}

	default:
		break;
	}
}

FArchiveColor& FTechSoftFileParser::FindOrAddColor(uint32 ColorIndex, uint8 Alpha)
{
	uint32 ColorHId = BuildColorId(ColorIndex, Alpha);
	if (FArchiveColor* Color = CADFileData.FindColor(ColorHId))
	{
		return *Color;
	}

	FArchiveColor& NewColor = CADFileData.AddColor(ColorHId);
	NewColor.Color = TechSoftUtils::GetColorAt(ColorIndex);
	NewColor.Color.A = Alpha;
	
	NewColor.UEMaterialName = BuildColorName(NewColor.Color);
	return NewColor;
}

FArchiveMaterial& FTechSoftFileParser::AddMaterialAt(uint32 MaterialIndexToSave, uint32 GraphMaterialIndex, const A3DGraphStyleData& GraphStyleData)
{
	FArchiveMaterial& NewMaterial = CADFileData.AddMaterial(GraphMaterialIndex);
	FCADMaterial& Material = NewMaterial.Material;

	TUniqueTSObjFromIndex<A3DGraphMaterialData> MaterialData(MaterialIndexToSave);
	if(MaterialData.IsValid())
	{
		Material.Diffuse = TechSoftUtils::GetColorAt(MaterialData->m_uiDiffuse);
		Material.Ambient = TechSoftUtils::GetColorAt(MaterialData->m_uiAmbient);
		Material.Specular = TechSoftUtils::GetColorAt(MaterialData->m_uiSpecular);
		Material.Shininess = MaterialData->m_dShininess;
		if(GraphStyleData.m_bIsTransparencyDefined)
		{
			Material.Transparency = 1. - GraphStyleData.m_ucTransparency/255.;
		}
		// todo: find how to convert Emissive color into ? reflexion coef...
		// Material.Emissive = GetColor(MaterialData->m_uiEmissive);
		// Material.Reflexion;
	}
	NewMaterial.UEMaterialName = BuildMaterialName(Material);
	return NewMaterial;
}


// Look at TechSoftUtils::BuildCADMaterial if any loigc changes in this method
// or any of the methos it calls
FArchiveMaterial& FTechSoftFileParser::FindOrAddMaterial(uint32 MaterialIndex, const A3DGraphStyleData& GraphStyleData)
{
	if (FArchiveMaterial* MaterialArchive = CADFileData.FindMaterial(MaterialIndex))
	{
		return *MaterialArchive;
	}

	bool bIsTexture = TechSoftInterface::IsMaterialTexture(MaterialIndex);
	if (bIsTexture)
	{
		TUniqueTSObjFromIndex<A3DGraphTextureApplicationData> TextureData(MaterialIndex);
		if (TextureData.IsValid())
		{
			return AddMaterialAt(TextureData->m_uiMaterialIndex, MaterialIndex, GraphStyleData);
			
#ifdef NOTYETDEFINE
			TUniqueTSObj<A3DGraphTextureDefinitionData> TextureDefinitionData(TextureData->m_uiTextureDefinitionIndex);
			if (TextureDefinitionData.IsValid())
			{
				TUniqueTSObj<A3DGraphPictureData> PictureData(TextureDefinitionData->m_uiPictureIndex);
			}
#endif
		}
		return AddMaterialAt(MaterialIndex, 0, GraphStyleData);
	}
	else
	{
		return AddMaterial(MaterialIndex, GraphStyleData);
	}
}

void FTechSoftFileParser::ExtractGraphicProperties(const A3DGraphics* Graphics, FEntityMetaData& OutMetaData)
{
	TUniqueTSObj<A3DGraphicsData> GraphicsData(Graphics);
	if (!GraphicsData.IsValid())
	{
		return;
	}

	bool bFatherHeritColor = GraphicsData->m_usBehaviour & kA3DGraphicsFatherHeritColor;
	bool bSonHeritColor = GraphicsData->m_usBehaviour & kA3DGraphicsSonHeritColor;

	//To do if needed
	//bool bFatherHeritLayer = GraphicsData->m_usBehaviour & kA3DGraphicsFatherHeritLayer;
	//bool bSonHeritLayer = GraphicsData->m_usBehaviour & kA3DGraphicsSonHeritLayer;
	//bool bFatherHeritTransparency = GraphicsData->m_usBehaviour & kA3DGraphicsFatherHeritTransparency;
	//bool bSonHeritTransparency = GraphicsData->m_usBehaviour & kA3DGraphicsSonHeritTransparency;

	OutMetaData.bRemoved = GraphicsData->m_usBehaviour & kA3DGraphicsRemoved;
	OutMetaData.bShow = GraphicsData->m_usBehaviour & kA3DGraphicsShow;

	if (GraphicsData->m_uiStyleIndex == A3D_DEFAULT_STYLE_INDEX)
	{
		return;
	}

	FCADUUID& ColorName = OutMetaData.ColorName;
	FCADUUID& MaterialName = OutMetaData.MaterialName;

	ExtractGraphStyleProperties(GraphicsData->m_uiStyleIndex, ColorName, MaterialName);

	if(bSonHeritColor)
	{
		OutMetaData.MetaData.Add(TEXT("GraphicsBehaviour"), TEXT("SonHerit"));
	}
	else if (bFatherHeritColor)
	{
		OutMetaData.MetaData.Add(TEXT("GraphicsBehaviour"), TEXT("FatherHerit"));
	}

	if (ColorName)
	{
		OutMetaData.MetaData.Add(TEXT("ColorName"), FString::Printf(TEXT("%u"), ColorName));
	}

	if (MaterialName)
	{
		OutMetaData.MetaData.Add(TEXT("MaterialName"), FString::Printf(TEXT("%u"), MaterialName));
	}
}

// Please review TechSoftUtils::GetMaterialValues if anything changes
// in this method or the methods it calls
void FTechSoftFileParser::ExtractGraphStyleProperties(uint32 StyleIndex, FCADUUID& OutColorName, FCADUUID& OutMaterialName)
{
	TUniqueTSObjFromIndex<A3DGraphStyleData> GraphStyleData(StyleIndex);

	if (GraphStyleData.IsValid())
	{
		if (GraphStyleData->m_bMaterial)
		{
			FArchiveMaterial& MaterialArchive = FindOrAddMaterial(GraphStyleData->m_uiRgbColorIndex, *GraphStyleData);
			OutMaterialName = MaterialArchive.UEMaterialName;
		}
		else
		{
			uint8 Alpha = 255;
			if (GraphStyleData->m_bIsTransparencyDefined)
			{
				Alpha = GraphStyleData->m_ucTransparency;
			}

			FArchiveColor& ColorArchive = FindOrAddColor(GraphStyleData->m_uiRgbColorIndex, Alpha);
			OutColorName = ColorArchive.UEMaterialName;
		}
	}
}

FMatrix FTechSoftFileParser::ExtractTransformation3D(const A3DMiscTransformation* CartesianTransformation, double& InOutUnit)
{
	TUniqueTSObj<A3DMiscCartesianTransformationData> CartesianTransformationData(CartesianTransformation);

	if (CartesianTransformationData.IsValid())
	{
		FVector Origin(CartesianTransformationData->m_sOrigin.m_dX, CartesianTransformationData->m_sOrigin.m_dY, CartesianTransformationData->m_sOrigin.m_dZ);
		FVector XVector(CartesianTransformationData->m_sXVector.m_dX, CartesianTransformationData->m_sXVector.m_dY, CartesianTransformationData->m_sXVector.m_dZ);;
		FVector YVector(CartesianTransformationData->m_sYVector.m_dX, CartesianTransformationData->m_sYVector.m_dY, CartesianTransformationData->m_sYVector.m_dZ);;

		FVector ZVector = XVector ^ YVector;

		Origin *= InOutUnit;

		const A3DVector3dData& A3DScale = CartesianTransformationData->m_sScale;
		FVector3d Scale(A3DScale.m_dX, A3DScale.m_dY, A3DScale.m_dZ);
		double UniformScale = TechSoftFileParserImpl::ExtractUniformScale(Scale);

		XVector *= Scale.X;
		YVector *= Scale.Y;
		ZVector *= Scale.Z;

		InOutUnit *= UniformScale;

		FMatrix Matrix(XVector, YVector, ZVector, FVector::ZeroVector);

		if (CartesianTransformationData->m_ucBehaviour & kA3DTransformationMirror)
		{
			Matrix.M[2][0] *= -1;
			Matrix.M[2][1] *= -1;
			Matrix.M[2][2] *= -1;
		}

		Matrix.SetOrigin(Origin);
		return Matrix;
	}

	return FMatrix::Identity;
}

FMatrix FTechSoftFileParser::ExtractGeneralTransformation(const A3DMiscTransformation* GeneralTransformation, double& InOutUnit)
{
	TUniqueTSObj<A3DMiscGeneralTransformationData> GeneralTransformationData(GeneralTransformation);
	if (GeneralTransformationData.IsValid())
	{
		FMatrix Matrix = FMatrix::Identity;;
		int32 Index = 0;
		for (int32 Andex = 0; Andex < 4; ++Andex)
		{
			for (int32 Bndex = 0; Bndex < 4; ++Bndex, ++Index)
			{
				Matrix.M[Andex][Bndex] = GeneralTransformationData->m_adCoeff[Index];
			}
		}

		FTransform3d Transform(Matrix);
		FVector3d Scale = Transform.GetScale3D();
		if (Scale.Equals(FVector3d::OneVector, KINDA_SMALL_NUMBER))
		{
			for (Index = 0; Index < 3; ++Index, ++Index)
			{
				Matrix.M[3][Index] *= InOutUnit;
			}
			return Matrix;
		}

		FVector3d Translation = Transform.GetTranslation();

		double UniformScale = TechSoftFileParserImpl::ExtractUniformScale(Scale);
		InOutUnit *= UniformScale;

		FQuat4d Rotation = Transform.GetRotation();

		FTransform3d NewTransform;
		NewTransform.SetScale3D(Scale);
		NewTransform.SetRotation(Rotation);

		FMatrix NewMatrix = NewTransform.ToMatrixWithScale();
		NewMatrix.SetOrigin(Translation);

		return NewMatrix;
	}
	return FMatrix::Identity;
}

FMatrix FTechSoftFileParser::ExtractTransformation(const A3DMiscTransformation* Transformation3D, double& InOutUnit)
{
	if (Transformation3D == NULL)
	{
		return FMatrix::Identity;
	}

	A3DEEntityType Type = kA3DTypeUnknown;
	A3DEntityGetType(Transformation3D, &Type);

	if (Type == kA3DTypeMiscCartesianTransformation)
	{
		return ExtractTransformation3D(Transformation3D, InOutUnit);
	}
	else if (Type == kA3DTypeMiscGeneralTransformation)
	{
		return ExtractGeneralTransformation(Transformation3D, InOutUnit);
	}
	return FMatrix::Identity;
}

FMatrix FTechSoftFileParser::ExtractCoordinateSystem(const A3DRiCoordinateSystem* CoordinateSystem, double& InOutUnit)
{
	TUniqueTSObj<A3DRiCoordinateSystemData> CoordinateSystemData(CoordinateSystem);
	if (CoordinateSystemData.IsValid())
	{
		return ExtractTransformation3D(CoordinateSystemData->m_pTransformation, InOutUnit);
	}
	return FMatrix::Identity;
}

bool FTechSoftFileParser::IsConfigurationSet(const A3DAsmProductOccurrence* Occurrence)
{
	TUniqueTSObj<A3DAsmProductOccurrenceData> OccurrenceData(Occurrence);
	if (!OccurrenceData.IsValid())
	{
		return false;
	}

	bool bIsConfiguration = false;
	if (OccurrenceData->m_uiPOccurrencesSize)
	{
		TUniqueTSObj<A3DAsmProductOccurrenceData> ChildData;
		for (uint32 Index = 0; Index < OccurrenceData->m_uiPOccurrencesSize; ++Index)
		{
			if (ChildData.FillFrom(OccurrenceData->m_ppPOccurrences[Index]) == A3D_SUCCESS)
			{
				if (ChildData->m_uiProductFlags & A3D_PRODUCT_FLAG_CONFIG)
				{
					bIsConfiguration = true;
				}
				break;
			}
		}
	}
	return bIsConfiguration;
}

uint32 FTechSoftFileParser::CountColorAndMaterial()
{
	A3DGlobal* GlobalPtr = TechSoftInterface::GetGlobalPointer();
	if (GlobalPtr == nullptr)
	{
		return 0;
	}

	A3DInt32 iRet = A3D_SUCCESS;
	TUniqueTSObj<A3DGlobalData> GlobalData(GlobalPtr);
	if (!GlobalData.IsValid())
	{
		return 0;
	}

	uint32 ColorCount = GlobalData->m_uiColorsSize;
	uint32 MaterialCount = GlobalData->m_uiMaterialsSize;
	uint32 TextureDefinitionCount = GlobalData->m_uiTextureDefinitionsSize;

	return ColorCount + MaterialCount + TextureDefinitionCount;
}

void ExtractTextureDefinition(const A3DGraphTextureDefinitionData& TextureDefinitionData)
{
	// To do
	//TextureDefinitionData.m_uiPictureIndex;
	//TextureDefinitionData.m_ucTextureDimension;
	//TextureDefinitionData.m_eMappingType;
	//TextureDefinitionData.m_eMappingOperator;

	//TextureDefinitionData.m_pOperatorTransfo;

	//TextureDefinitionData.m_uiMappingAttributes;
	//TextureDefinitionData.m_uiMappingAttributesIntensitySize,
	//TextureDefinitionData.m_pdMappingAttributesIntensity;
	//TextureDefinitionData.m_uiMappingAttributesComponentsSize,
	//TextureDefinitionData.m_pucMappingAttributesComponents;
	//TextureDefinitionData.m_eTextureFunction;
	//TextureDefinitionData.m_dRed;
	//TextureDefinitionData.m_dGreen;
	//TextureDefinitionData.m_dBlue;
	//TextureDefinitionData.m_dAlpha;
	//TextureDefinitionData.m_eBlend_src_RGB;
	//TextureDefinitionData.m_eBlend_dst_RGB;
	//TextureDefinitionData.m_eBlend_src_Alpha;
	//TextureDefinitionData.m_eBlend_dst_Alpha;
	//TextureDefinitionData.m_ucTextureApplyingMode;
	//TextureDefinitionData.m_eTextureAlphaTest;
	//TextureDefinitionData.m_dAlphaTestReference;
	//TextureDefinitionData.m_eTextureWrappingModeS;
	//TextureDefinitionData.m_eTextureWrappingModeT;

	//if (TextureDefinitionData.m_pTextureTransfo != nullptr)
	//{
	//	TUniqueTSObj<A3DGraphTextureTransformationData> TransfoData(TextureDefinitionData.m_pTextureTransfo);
	//}
}

void FTechSoftFileParser::ReadMaterialsAndColors()
{
	A3DGlobal* GlobalPtr = TechSoftInterface::GetGlobalPointer();
	if (GlobalPtr == nullptr)
	{
		return;
	}

	A3DInt32 iRet = A3D_SUCCESS;
	TUniqueTSObj<A3DGlobalData> GlobalData(GlobalPtr);
	if (!GlobalData.IsValid())
	{
		return;
	}

	{
		uint32 TextureDefinitionCount = GlobalData->m_uiTextureDefinitionsSize;
		if (TextureDefinitionCount)
		{
			TUniqueTSObjFromIndex<A3DGraphTextureDefinitionData> TextureDefinitionData;
			for (uint32 TextureIndex = 0; TextureIndex < TextureDefinitionCount; ++TextureIndex)
			{
				TextureDefinitionData.FillFrom(TextureIndex);
				ExtractTextureDefinition(*TextureDefinitionData);
			}
		}
	}

	{
		uint32 PictureCount = GlobalData->m_uiPicturesSize;
		if (PictureCount)
		{
			TUniqueTSObjFromIndex<A3DGraphPictureData> PictureData;
			for (uint32 PictureIndex = 0; PictureIndex < PictureCount; ++PictureIndex)
			{
				A3DEntity* PicturePtr = TechSoftInterface::GetPointerFromIndex(PictureIndex, kA3DTypeGraphPicture);
				if (PicturePtr)
				{
					FEntityMetaData PictureMetaData;
					ExtractMetaData(PicturePtr, PictureMetaData);
				}

				PictureData.FillFrom(PictureIndex);
				// To do
			}
		}
	}
}

#endif  

} // ns CADLibrary
