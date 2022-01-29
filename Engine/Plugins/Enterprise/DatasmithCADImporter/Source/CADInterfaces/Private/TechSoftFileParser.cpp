// Copyright Epic Games, Inc. All Rights Reserved.

#include "TechSoftFileParser.h"

#include "CADOptions.h"

#ifdef USE_TECHSOFT_SDK

#include "TechSoftInterface.h"

#include "Templates/UnrealTemplate.h"

namespace CADLibrary
{

namespace TechSoftFileParserImpl
{
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

FString CleanSdkName(const FString& Name)
{
	int32 Index;
	if (Name.FindLastChar(TEXT('['), Index))
	{
		return Name.Left(Index);
	}
	return Name;
}

FString CleanCatiaInstanceSdkName(const FString& Name)
{
	int32 Index;
	if (Name.FindChar(TEXT('('), Index))
	{
		FString NewName = Name.RightChop(Index + 1);
		if (NewName.FindLastChar(TEXT(')'), Index))
		{
			NewName = NewName.Left(Index);
		}
		return NewName;
	}
	return Name;
}

FString Clean3dxmlReferenceSdkName(const FString& Name)
{
	int32 Index;
	if (Name.FindChar(TEXT('('), Index))
	{
		FString NewName = Name.Left(Index);
		return NewName;
	}
	return Name;
}

FString CleanSwInstanceSdkName(const FString& Name)
{
	int32 Position;
	if (Name.FindLastChar(TEXT('-'), Position))
	{
		FString NewName = Name.Left(Position) + TEXT("<") + Name.RightChop(Position + 1) + TEXT(">");
		return NewName;
	}
	return Name;
}

FString CleanSwReferenceSdkName(const FString& Name)
{
	int32 Position;
	if (Name.FindLastChar(TEXT('-'), Position))
	{
		FString NewName = Name.Left(Position);
		return NewName;
	}
	return Name;
}

FString CleanCatiaReferenceName(const FString& Name)
{
	int32 Position;
	if (Name.FindLastChar(TEXT('.'), Position))
	{
		FString Indice = Name.RightChop(Position + 1);
		if (Indice.IsNumeric())
		{
			FString NewName = Name.Left(Position);
			return NewName;
		}
	}
	return Name;
}

FString CleanNameByRemoving_prt(const FString& Name)
{
	int32 Position;
	if (Name.FindLastChar(TEXT('.'), Position))
	{
		FString Extension = Name.RightChop(Position);
		if (Extension.Equals(TEXT("prt"), ESearchCase::IgnoreCase))
		{
			FString NewName = Name.Left(Position);
			return NewName;
		}
	}
	return Name;
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

bool ReplaceOrAddNameValue(TMap<FString, FString>& MetaData, TCHAR* Key)
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
	Importer.m_sLoadData.m_sGeneral.m_eDefaultUnit;
	Importer.m_sLoadData.m_sGeneral.m_bReadFeature = A3D_FALSE;

	Importer.m_sLoadData.m_sGeneral.m_bReadConstraints = A3D_FALSE;
	Importer.m_sLoadData.m_sGeneral.m_iNbMultiProcess = 1;

	Importer.m_sLoadData.m_sIncremental.m_bLoadNoDependencies = CADLibrary::FImportParameters::bGEnableCADCache;
	Importer.m_sLoadData.m_sIncremental.m_bLoadStructureOnly = false;
}

void UpdateIOOptionAccordingToFormat(const CADLibrary::ECADFormat Format, A3DImport& Importer)
{
	switch (Format)
	{
	case CADLibrary::ECADFormat::IGES:
	{
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

} // ns TechSoftFileParserImpl

FTechSoftFileParser::FTechSoftFileParser(FCADFileData& InCADData, const FString& EnginePluginsPath)
	: CADFileData(InCADData)
	, TechSoftInterface(TechSoftUtils::GetTechSoftInterface())
{
}

ECADParsingResult FTechSoftFileParser::Process()
{
	const FFileDescriptor& File = CADFileData.GetCADFileDescription();

	if (File.GetPathOfFileToLoad().IsEmpty())
	{
		return ECADParsingResult::FileNotFound;
	}

	A3DImport Import(*File.GetPathOfFileToLoad());

	TechSoftFileParserImpl::SetIOOption(Import);

	// Add specific options according to format
	Format = File.GetFileFormat();
	TechSoftFileParserImpl::UpdateIOOptionAccordingToFormat(Format, Import);

	A3DStatus IRet = TechSoftInterface.Import(Import);
	if (IRet != A3D_SUCCESS && IRet != A3D_LOAD_MULTI_MODELS_CADFILE && IRet != A3D_LOAD_MISSING_COMPONENTS)
	{
		return ECADParsingResult::ProcessFailed;
	}

	// save the file for the next load
	if (CADFileData.IsCacheDefined())
	{
		FString CacheFilePath = CADFileData.GetCADCachePath();
		if (CacheFilePath != File.GetPathOfFileToLoad())
		{
			// todo
		}
	}

	A3DAsmModelFile* ModelFile = TechSoftInterface.GetModelFile();

	if (CADFileData.GetImportParameters().GetStitchingTechnique() == StitchingSew && FImportParameters::bGDisableCADKernelTessellation)
	{
		TUniqueTSObj<A3DSewOptionsData> SewData;
		SewData->m_bComputePreferredOpenShellOrientation = false;
		double ToleranceMM = 0.01 / FileUnit;
		A3DStatus Status = TechSoftUtils::SewModel(&ModelFile, ToleranceMM, &*SewData);
		if(Status != A3DStatus::A3D_SUCCESS)
		{
			// To do but what ?
		}
	}

	ReserveCADFileData();

	ReadMaterialsAndColors();

	ECADParsingResult Result = TraverseModel(ModelFile);

	if (Result == ECADParsingResult::ProcessOk)
	{
		GenerateBodyMeshes();
	}

	FString TechsoftVersion = TechSoftUtils::GetTechSoftVersion();
	if (!TechsoftVersion.IsEmpty() && CADFileData.ComponentCount() > 0)
	{
		CADFileData.GetComponentAt(0).MetaData.Add(TEXT("TechsoftVersion"), TechsoftVersion);
	}

	TechSoftInterface.UnloadModel();

	return Result;
}

void FTechSoftFileParser::GenerateBodyMeshes()
{
	if (FImportParameters::bGDisableCADKernelTessellation)
	{
		for (TPair<A3DRiRepresentationItem*, int32>& Entry : RepresentationItemsCache)
		{
			A3DRiRepresentationItem* RepresentationItemPtr = Entry.Key;

			FArchiveBody& Body = CADFileData.GetBodyAt(Entry.Value);

			FBodyMesh& BodyMesh = CADFileData.AddBodyMesh(Body.ObjectId, Body);

			// todo
			//if (CADFileData.GetImportParameters().GetStitchingTechnique() == StitchingHeal)
			//{
			//	TUniqueTSObj<A3DSewOptionsData> SewData;
			//	SewData->m_bComputePreferredOpenShellOrientation = false;
			//	double ToleranceMM = 0.01 / FileUnit;
			//	A3DRiBrepModel** NewBReps;
			//	uint32 NewBRepCount = 0;
			//	A3DStatus Status = TechSoftUtils::HealBRep(&BRepModelPtr, ToleranceMM, &*SewData, &NewBReps, NewBRepCount);
			//	if (Status == A3DStatus::A3D_SUCCESS)
			//	{
			//		// Aggregate all the bodies into one?
			//		MeshRepresentationsWithTechSoft(NewBRepCount, NewBReps, Body);
			//		TechSoftInterface.FillBodyMesh(NewBRepCount, NewBReps, CADFileData.GetImportParameters(), FileUnit, BodyMesh);???
			//	}
			//}
			//else
			{
				TechSoftInterface.FillBodyMesh(RepresentationItemPtr, CADFileData.GetImportParameters(), FileUnit, BodyMesh);
			}

			// Convert material
			FCADUUID DefaultColorName = Body.ColorFaceSet.Num() > 0 ? *Body.ColorFaceSet.begin()  : 0;
			FCADUUID DefaultMaterialName = Body.MaterialFaceSet.Num() > 0 ? *Body.MaterialFaceSet.begin() : 0;

			for (FTessellationData& Tessellation : BodyMesh.Faces)
			{
				FCADUUID ColorName = DefaultColorName;
				FCADUUID MaterialName = DefaultMaterialName;

				// Extract proper color or material based on style index
				uint32 CachedStyleIndex = Tessellation.MaterialName;
				Tessellation.MaterialName = 0;

				if (CachedStyleIndex != 0xffffffff)
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

			// Write part's representation as hsf file
			A3DEEntityType Type;
			A3DEntityGetType(RepresentationItemPtr, &Type);

			if (Type == kA3DTypeRiBrepModel)
			{
				FString FilePath = CADFileData.GetBodyCachePath(Body.MeshActorName);
				if (!FilePath.IsEmpty())
				{
					this->TechSoftInterface.SaveBodyToHsfFile(RepresentationItemPtr, FilePath);
				}
			}

			// #ueent_techsoft: Take care of materials stored in BodyMesh

			Body.ColorFaceSet = BodyMesh.ColorSet;
			Body.MaterialFaceSet = BodyMesh.MaterialSet;
		}
	}
	else
	{
		// Mesh with CADKernel
	}
}

void FTechSoftFileParser::ReserveCADFileData()
{
	// TODO: Could be more accurate
	CountUnderModel(TechSoftInterface.GetModelFile());

	CADFileData.ReserveBodyMeshes(ComponentCount[EComponentType::Body]);

	FArchiveSceneGraph& SceneGraphArchive = CADFileData.GetSceneGraphArchive();
	SceneGraphArchive.Reserve(ComponentCount[EComponentType::Occurrence], ComponentCount[EComponentType::Reference], ComponentCount[EComponentType::Body]);

	uint32 MaterialNum = CountColorAndMaterial();
	SceneGraphArchive.MaterialHIdToMaterial.Reserve(MaterialNum);
}

void FTechSoftFileParser::CountUnderModel(const A3DAsmModelFile* AsmModel)
{
	TUniqueTSObj<A3DAsmModelFileData> ModelFileData(AsmModel);
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

ECADParsingResult FTechSoftFileParser::TraverseModel(const A3DAsmModelFile* ModelFile)
{
	TUniqueTSObj<A3DAsmModelFileData> ModelFileData(ModelFile);
	if (!ModelFileData.IsValid())
	{
		return ECADParsingResult::ProcessFailed;
	}

	ModellerType = (EModellerType)ModelFileData->m_eModellerType;
	FileUnit = ModelFileData->m_dUnit;

	FEntityMetaData MetaData;
	ExtractMetaData(ModelFile, MetaData);
	ExtractSpecificMetaData(ModelFile, MetaData);

	for (uint32 Index = 0; Index < ModelFileData->m_uiPOccurrencesSize; ++Index)
	{
		if (IsConfigurationSet(ModelFileData->m_ppPOccurrences[Index]))
		{
			TraverseConfigurationSet(ModelFileData->m_ppPOccurrences[Index]);
		}
		else
		{
			TraverseReference(ModelFileData->m_ppPOccurrences[Index]);
		}
	}

	return ECADParsingResult::ProcessOk;
}

void FTechSoftFileParser::TraverseConfigurationSet(const A3DAsmProductOccurrence* ConfigurationSetPtr)
{
	TUniqueTSObj<A3DAsmProductOccurrenceData> ConfigurationSetData(ConfigurationSetPtr);
	if (!ConfigurationSetData.IsValid())
	{
		return;
	}

	FEntityMetaData MetaData;
	ExtractMetaData(ConfigurationSetPtr, MetaData);
	ExtractSpecificMetaData(ConfigurationSetPtr, MetaData);

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
				TraverseReference(ConfigurationSetData->m_ppPOccurrences[Index]);
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
				TraverseReference(ConfigurationSetData->m_ppPOccurrences[Index]);
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

	TUniqueTSObj<A3DAsmProductOccurrenceData> ConfigurationData;
	for (unsigned int Index = 0; Index < ConfigurationSetData->m_uiPOccurrencesSize; ++Index)
	{
		ConfigurationData.FillFrom(ConfigurationSetData->m_ppPOccurrences[Index]);
		if (!ConfigurationData.IsValid())
		{
			return;
		}

		if (ConfigurationData->m_uiProductFlags & (A3D_PRODUCT_FLAG_DEFAULT | A3D_PRODUCT_FLAG_CONFIG))
		{
			CountUnderOccurrence(ConfigurationSetData->m_ppPOccurrences[Index]);
			return;
		}
	}

	// no default configuration, traverse the first
	if (ConfigurationSetData->m_uiPOccurrencesSize)
	{
		CountUnderOccurrence(ConfigurationSetData->m_ppPOccurrences[0]);
	}
}

void FTechSoftFileParser::TraverseReference(const A3DAsmProductOccurrence* ReferencePtr)
{
	FEntityMetaData MetaData;
	ExtractMetaData(ReferencePtr, MetaData);

	if (MetaData.bRemoved || !MetaData.bShow)
	{
		return;
	}

	ExtractSpecificMetaData(ReferencePtr, MetaData);
	BuildReferenceName(MetaData.MetaData);

	ExtractMaterialProperties(ReferencePtr);

	int32 ComponentId = LastEntityId++;
	int32 Index = CADFileData.AddComponent(ComponentId);
	FArchiveComponent& Component = CADFileData.GetComponentAt(Index);
	Component.MetaData = MoveTemp(MetaData.MetaData);

	TUniqueTSObj<A3DAsmProductOccurrenceData> ReferenceData(ReferencePtr);
	if (!ReferenceData.IsValid())
	{
		return;
	}

	for (uint32 OccurenceIndex = 0; OccurenceIndex < ReferenceData->m_uiPOccurrencesSize; ++OccurenceIndex)
	{
		int32 ChildrenId = TraverseOccurrence(ReferenceData->m_ppPOccurrences[OccurenceIndex]);
		Component.Children.Add(ChildrenId);
	}

	if (ReferenceData->m_pPart)
	{
		TraversePartDefinition(ReferenceData->m_pPart, Component);
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

int32 FTechSoftFileParser::AddBody(FEntityMetaData& BodyMetaData)
{
	FCadId BodyId = LastEntityId++;
	int32 BodyIndex = CADFileData.AddBody(BodyId);
	FArchiveBody& Body = CADFileData.GetBodyAt(BodyIndex);
	Body.MetaData = MoveTemp(BodyMetaData.MetaData);
	if(BodyMetaData.ColorName != 0)
	{
		Body.ColorFaceSet.Add(BodyMetaData.ColorName);
	}
	if (BodyMetaData.MaterialName != 0)
	{
		Body.MaterialFaceSet.Add(BodyMetaData.MaterialName);
	}
	return BodyIndex;
}

FCadId FTechSoftFileParser::TraverseOccurrence(const A3DAsmProductOccurrence* OccurrencePtr)
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

	FEntityMetaData InstanceMetaData;
	ExtractMetaData(OccurrencePtr, InstanceMetaData);

	if (InstanceMetaData.bRemoved || !InstanceMetaData.bShow)
	{
		return 0;
	}

	ExtractSpecificMetaData(OccurrencePtr, InstanceMetaData);
	BuildInstanceName(InstanceMetaData.MetaData);

	ExtractMaterialProperties(OccurrencePtr);

	FArchiveInstance& Instance = AddInstance(InstanceMetaData);

	A3DMiscTransformation* Location = OccurrenceData->m_pLocation;

	FEntityMetaData PrototypeMetaData;
	if (OccurrenceData->m_pPrototype)
	{
		ProcessPrototype(OccurrenceData->m_pPrototype, PrototypeMetaData, &Location);
	}

	if (Location)
	{
		Instance.TransformMatrix = TraverseTransformation(Location);
	}

	if (PrototypeMetaData.bUnloaded)
	{
		FArchiveUnloadedComponent& UnloadedComponent = AddUnloadedComponent(PrototypeMetaData, Instance);
		return Instance.ObjectId;
	}
	
	while (OccurrenceData->m_pPrototype != nullptr && OccurrenceData->m_pPart == nullptr && OccurrenceData->m_uiPOccurrencesSize == 0)
	{
		CachedOccurrencePtr = OccurrenceData->m_pPrototype;
		OccurrenceData.FillFrom(OccurrenceData->m_pPrototype);
	}

	if(OccurrenceData->m_pPart == nullptr && OccurrenceData->m_uiPOccurrencesSize == 0)
	{
		return Instance.ObjectId;
	}

	FArchiveComponent& Component = AddComponent(InstanceMetaData, Instance);

	// Add part
	while (OccurrenceData->m_pPrototype != nullptr && OccurrenceData->m_pPart == nullptr)
	{
		OccurrenceData.FillFrom(OccurrenceData->m_pPrototype);
	}
	if (OccurrenceData->m_pPart != nullptr)
	{
		A3DAsmPartDefinition* PartDefinition = OccurrenceData->m_pPart;
		TraversePartDefinition(PartDefinition, Component);
	}

	// Add Occurrence's Children
	OccurrenceData.FillFrom(CachedOccurrencePtr);
	while (OccurrenceData->m_pPrototype != nullptr && OccurrenceData->m_uiPOccurrencesSize == 0)
	{
		OccurrenceData.FillFrom(OccurrenceData->m_pPrototype);
	}

	uint32 ChildrenCount = OccurrenceData->m_uiPOccurrencesSize;
	A3DAsmProductOccurrence** Children = OccurrenceData->m_ppPOccurrences;
	for (uint32 Index = 0; Index < ChildrenCount; ++Index)
	{
		int32 ChildrenId = TraverseOccurrence(Children[Index]);
		Component.Children.Add(ChildrenId);
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

		A3DAsmProductOccurrence* PrototypePtr = OccurrenceData->m_pPrototype;
		A3DAsmPartDefinition* PartDefinition = OccurrenceData->m_pPart;

		while (!PartDefinition && PrototypePtr)
		{
			TUniqueTSObj<A3DAsmProductOccurrenceData> PrototypeOccurrenceData(PrototypePtr);
			PartDefinition = PrototypeOccurrenceData->m_pPart;
			PrototypePtr = PrototypeOccurrenceData->m_pPrototype;
		}

		CountUnderPartDefinition(PartDefinition);

		uint32 ChildrenCount = OccurrenceData->m_uiPOccurrencesSize;
		A3DAsmProductOccurrence** Children = OccurrenceData->m_ppPOccurrences;
		PrototypePtr = OccurrenceData->m_pPrototype;

		while (ChildrenCount == 0 && PrototypePtr)
		{
			TUniqueTSObj<A3DAsmProductOccurrenceData> PrototypeOccurrenceData(PrototypePtr);
			ChildrenCount = PrototypeOccurrenceData->m_uiPOccurrencesSize;
			Children = PrototypeOccurrenceData->m_ppPOccurrences;
			PrototypePtr = PrototypeOccurrenceData->m_pPrototype;
		}

		for (uint32 Index = 0; Index < ChildrenCount; ++Index)
		{
			CountUnderOccurrence(Children[Index]);
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

		if (PrototypeData->m_pPart != nullptr || PrototypeData->m_uiPOccurrencesSize != 0 || PrototypeData->m_pPrototype == nullptr)
		{
			ExtractMetaData(PrototypePtr, OutPrototypeMetaData);
			ExtractSpecificMetaData(PrototypePtr, OutPrototypeMetaData);
			ExtractMaterialProperties(PrototypePtr);

			TUniqueTSObj<A3DUTF8Char*> FilePathUTF8Ptr;
			FilePathUTF8Ptr.FillWith(&TechSoftUtils::GetFilePathName, PrototypePtr);
			if (!FilePathUTF8Ptr.IsValid())
			{
				FilePathUTF8Ptr.FillWith(&TechSoftUtils::GetOriginalFilePathName, PrototypePtr);
			}
			if (FilePathUTF8Ptr.IsValid())
			{
				FString FilePath = UTF8_TO_TCHAR(*FilePathUTF8Ptr);
				FPaths::NormalizeFilename(FilePath);
				FString FileName = FPaths::GetCleanFilename(FilePath);
				if (FileName != CADFileData.GetCADFileDescription().GetFileName())
				{
					OutPrototypeMetaData.ExternalFile = FFileDescriptor(*FilePath, nullptr, *CADFileData.GetCADFileDescription().GetRootFolder());
				}
			}
		}
		
		if (PrototypeData->m_pPart != nullptr || PrototypeData->m_uiPOccurrencesSize != 0)
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
		if(PrototypeData.IsValid())
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

	BuildReferenceName(OutPrototypeMetaData.MetaData);
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

void FTechSoftFileParser::TraversePartDefinition(const A3DAsmPartDefinition* PartDefinitionPtr, FArchiveComponent& Part)
{
	FEntityMetaData PartMetaData;
	ExtractMetaData(PartDefinitionPtr, PartMetaData);

	if (PartMetaData.bRemoved || !PartMetaData.bShow)
	{
		return;
	}

	ExtractSpecificMetaData(PartDefinitionPtr, PartMetaData);
	BuildPartName(PartMetaData.MetaData);

	ExtractMaterialProperties(PartDefinitionPtr);

	TUniqueTSObj<A3DAsmPartDefinitionData> PartData(PartDefinitionPtr);
	if (PartData.IsValid())
	{
		for (unsigned int Index = 0; Index < PartData->m_uiRepItemsSize; ++Index)
		{
			int32 ChildId = TraverseRepresentationItem(PartData->m_ppRepItems[Index], PartMetaData);
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

FCadId FTechSoftFileParser::TraverseRepresentationItem(A3DRiRepresentationItem* RepresentationItem, FEntityMetaData& PartMetaData)
{
	A3DEEntityType Type;
	A3DEntityGetType(RepresentationItem, &Type);

	switch (Type)
	{
	case kA3DTypeRiSet:
		return TraverseRepresentationSet(RepresentationItem, PartMetaData);
	case kA3DTypeRiBrepModel:
		return TraverseBRepModel(RepresentationItem, PartMetaData);
	case kA3DTypeRiPolyBrepModel:
		return TraversePolyBRepModel(RepresentationItem, PartMetaData);
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

FCadId FTechSoftFileParser::TraverseRepresentationSet(const A3DRiSet* RepresentationSetPtr, FEntityMetaData& PartMetaData)
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

	ExtractMaterialProperties(RepresentationSetPtr);

	FCadId RepresentationSetId = 0;
	FArchiveComponent& RepresentationSet = AddOccurence(RepresentationSetMetaData, RepresentationSetId);

	for (A3DUns32 Index = 0; Index < RepresentationSetData->m_uiRepItemsSize; ++Index)
	{
		int32 ChildId = TraverseRepresentationItem(RepresentationSetData->m_ppRepItems[Index], RepresentationSetMetaData);
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

FCadId FTechSoftFileParser::TraverseBRepModel(A3DRiBrepModel* BRepModelPtr, FEntityMetaData& PartMetaData)
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


	if (int32* BodyIndexPtr = RepresentationItemsCache.Find(BRepModelPtr))
	{
		return CADFileData.GetBodyAt(*BodyIndexPtr).ObjectId;
	}

	ExtractSpecificMetaData(BRepModelPtr, BRepMetaData);
	ExtractMaterialProperties(BRepModelPtr);

	int32 BodyIndex = AddBody(BRepMetaData);
	FArchiveBody& Body = CADFileData.GetBodyAt(BodyIndex);

	RepresentationItemsCache.Add(BRepModelPtr, BodyIndex);

	return Body.ObjectId;
}

FCadId FTechSoftFileParser::TraversePolyBRepModel(A3DRiPolyBrepModel* PolygonalPtr, FEntityMetaData& PartMetaData)
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

	if (int32* BodyIndexPtr = RepresentationItemsCache.Find(PolygonalPtr))
	{
		return CADFileData.GetBodyAt(*BodyIndexPtr).ObjectId;
	}

	ExtractSpecificMetaData(PolygonalPtr, BRepMetaData);
	ExtractMaterialProperties(PolygonalPtr);

	int32 BodyIndex = AddBody(BRepMetaData);
	FArchiveBody& Body = CADFileData.GetBodyAt(BodyIndex);

	RepresentationItemsCache.Add(PolygonalPtr, BodyIndex);

	return Body.ObjectId;
}

void FTechSoftFileParser::ExtractMetaData(const A3DEntity* Entity, FEntityMetaData& OutMetaData)
{
	TUniqueTSObj<A3DRootBaseData> MetaData(Entity);
	if (MetaData.IsValid())
	{
		if (false && MetaData->m_uiPersistentId > 0)
		{
			FString PersistentId = FString::FromInt(MetaData->m_uiPersistentId);
			OutMetaData.MetaData.Emplace(TEXT("PersistentId"), PersistentId);
		}

		if (MetaData->m_pcName && MetaData->m_pcName[0] != '\0')
		{
			FString SDKName = UTF8_TO_TCHAR(MetaData->m_pcName);
			SDKName = TechSoftFileParserImpl::CleanSdkName(SDKName);
			OutMetaData.MetaData.Emplace(TEXT("SDKName"), SDKName);
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

void FTechSoftFileParser::BuildReferenceName(TMap<FString, FString>& MetaData)
{
	if (MetaData.IsEmpty())
	{
		return;
	}

	FString* NamePtr = MetaData.Find(TEXT("InstanceName"));
	if (NamePtr != nullptr)
	{
		FString& Name = MetaData.FindOrAdd(TEXT("Name"));
		Name = *NamePtr;
		if (Format == ECADFormat::CATIA)
		{
			Name = TechSoftFileParserImpl::CleanCatiaReferenceName(Name);
		}
		return;
	}

	if (Format == ECADFormat::JT)
	{
		if (TechSoftFileParserImpl::ReplaceOrAddNameValue(MetaData, TEXT("SDKName")))
		{
			return;
		}
	}

	if (TechSoftFileParserImpl::CheckIfNameExists(MetaData))
	{
		return;
	}

	if (TechSoftFileParserImpl::ReplaceOrAddNameValue(MetaData, TEXT("PartNumber")))
	{
		return;
	}

	NamePtr = MetaData.Find(TEXT("SDKName"));
	if (NamePtr != nullptr)
	{
		FString SdkName = *NamePtr;

		switch (Format)
		{
		case ECADFormat::CATIA_3DXML:
			SdkName = TechSoftFileParserImpl::Clean3dxmlReferenceSdkName(SdkName);
			break;

		case ECADFormat::SOLIDWORKS:
			SdkName = TechSoftFileParserImpl::CleanSwReferenceSdkName(SdkName);
			break;

		default:
			break;
		}

		FString& Name = MetaData.FindOrAdd(TEXT("Name"));
		Name = SdkName;
		return;
	}
}

void FTechSoftFileParser::BuildInstanceName(TMap<FString, FString>& MetaData)
{
	if (MetaData.IsEmpty())
	{
		return;
	}

	if (TechSoftFileParserImpl::ReplaceOrAddNameValue(MetaData, TEXT("InstanceName")))
	{
		return;
	}

	if (TechSoftFileParserImpl::CheckIfNameExists(MetaData))
	{
		return;
	}

	FString* NamePtr = MetaData.Find(TEXT("SDKName"));
	if (NamePtr != nullptr)
	{
		FString SdkName = *NamePtr;

		switch (Format)
		{
		case ECADFormat::CATIA:
		case ECADFormat::CATIA_3DXML:
			SdkName = TechSoftFileParserImpl::CleanCatiaInstanceSdkName(SdkName);
			break;

		case ECADFormat::SOLIDWORKS:
			SdkName = TechSoftFileParserImpl::CleanSwInstanceSdkName(SdkName);
			break;

		default:
			break;
		}

		FString& Name = MetaData.FindOrAdd(TEXT("Name"));
		Name = SdkName;
		return;
	}

	if (TechSoftFileParserImpl::ReplaceOrAddNameValue(MetaData, TEXT("PartNumber")))
	{
		return;
	}

}

void FTechSoftFileParser::BuildPartName(TMap<FString, FString>& MetaData)
{
	if (MetaData.IsEmpty())
	{
		return;
	}

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

void FTechSoftFileParser::BuildBodyName(TMap<FString, FString>& MetaData)
{
	if (MetaData.IsEmpty())
	{
		return;
	}

	if (TechSoftFileParserImpl::CheckIfNameExists(MetaData))
	{
		return;
	}

	FString* NamePtr = MetaData.Find(TEXT("SDKName"));
	if (NamePtr != nullptr)
	{
		FString SdkName = *NamePtr;
		if (Format == ECADFormat::CREO)
		{
			SdkName = TechSoftFileParserImpl::CleanNameByRemoving_prt(SdkName);
		}

		FString& Name = MetaData.FindOrAdd(TEXT("Name"));
		Name = SdkName;
		return;
	}

	MetaData.Add(TEXT("Name"), TEXT("NoName"));
}

void FTechSoftFileParser::ExtractSpecificMetaData(const A3DAsmProductOccurrence* Occurrence, FEntityMetaData& OutMetaData)
{
	//----------- Export Specific information per CAD format -----------
	switch (ModellerType)
	{
	case kA3DModellerSlw:
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
	case kA3DModellerUnigraphics:
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

	case kA3DModellerCatiaV5:
	{
		TUniqueTSObj<A3DAsmProductOccurrenceDataCV5> CatiaV5SpecificData(Occurrence);
		if (CatiaV5SpecificData.IsValid())
		{
			if (CatiaV5SpecificData->m_psVersion)
			{
				FString Version = UTF8_TO_TCHAR(CatiaV5SpecificData->m_psVersion);
				OutMetaData.MetaData.Emplace(L"CatiaVersion", Version);
			}

			if (CatiaV5SpecificData->m_psPartNumber)
			{
				FString PartNumber = UTF8_TO_TCHAR(CatiaV5SpecificData->m_psPartNumber);
				OutMetaData.MetaData.Emplace(L"CatiaPartNumber", PartNumber);
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
	NewColor.Color = TechSoftFileParserImpl::GetColorAt(ColorIndex);
	NewColor.Color.A = Alpha;
	
	NewColor.UEMaterialName = BuildColorName(NewColor.Color);
	return NewColor;
}

FArchiveMaterial& FTechSoftFileParser::AddMaterialAt(uint32 MaterialIndexToSave, uint32 GraphMaterialIndex)
{
	FArchiveMaterial& NewMaterial = CADFileData.AddMaterial(GraphMaterialIndex);
	FCADMaterial& Material = NewMaterial.Material;

	TUniqueTSObjFromIndex<A3DGraphMaterialData> MaterialData(MaterialIndexToSave);
	if(MaterialData.IsValid())
	{
		Material.Diffuse = TechSoftFileParserImpl::GetColorAt(MaterialData->m_uiDiffuse);
		Material.Ambient = TechSoftFileParserImpl::GetColorAt(MaterialData->m_uiAmbient);
		Material.Specular = TechSoftFileParserImpl::GetColorAt(MaterialData->m_uiSpecular);
		Material.Shininess = MaterialData->m_dShininess;
		Material.Transparency = 1. - MaterialData->m_dDiffuseAlpha;
		// todo: find how to convert Emissive color into ? reflexion coef...
		// Material.Emissive = GetColor(MaterialData->m_uiEmissive);
		// Material.Reflexion;
	}
	NewMaterial.UEMaterialName = BuildMaterialName(Material);
	return NewMaterial;
}


FArchiveMaterial& FTechSoftFileParser::FindOrAddMaterial(uint32 MaterialIndex)
{
	if (FArchiveMaterial* MaterialArchive = CADFileData.FindMaterial(MaterialIndex))
	{
		return *MaterialArchive;
	}

	bool bIsTexture = TechSoftUtils::IsMaterialTexture(MaterialIndex);
	if (bIsTexture)
	{
		TUniqueTSObjFromIndex<A3DGraphTextureApplicationData> TextureData(MaterialIndex);
		if (TextureData.IsValid())
		{
			TextureData->m_uiMaterialIndex;
			return AddMaterialAt(TextureData->m_uiMaterialIndex, MaterialIndex);
			
#ifdef NOTYETDEFINE
			TUniqueTSObj<A3DGraphTextureDefinitionData> TextureDefinitionData(TextureData->m_uiTextureDefinitionIndex);
			if (TextureDefinitionData.IsValid())
			{
				TUniqueTSObj<A3DGraphPictureData> PictureData(TextureDefinitionData->m_uiPictureIndex);
			}
#endif
		}
		return AddMaterialAt(MaterialIndex, 0);
	}
	else
	{
		return AddMaterial(MaterialIndex);
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

void FTechSoftFileParser::ExtractGraphStyleProperties(uint32 StyleIndex, FCADUUID& OutColorName, FCADUUID& OutMaterialName)
{
	TUniqueTSObjFromIndex<A3DGraphStyleData> GraphStyleData(StyleIndex);

	if (GraphStyleData.IsValid())
	{
		if (GraphStyleData->m_bMaterial)
		{
			FArchiveMaterial& MaterialArchive = FindOrAddMaterial(GraphStyleData->m_uiRgbColorIndex);
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

void FTechSoftFileParser::ExtractMaterialProperties(const A3DEntity* Entity)
{
	TUniqueTSObj<A3DMiscMaterialPropertiesData> MaterialPropertiesData(Entity);
	if (!MaterialPropertiesData.IsValid())
	{
		return;
	}

	//xml->SetDoubleAttribute("m_dDensity", MaterialPropertiesData.m_dDensity);
	//xml->SetAttribute("m_pcMaterialName", MaterialPropertiesData.m_pcMaterialName ? MaterialPropertiesData.m_pcMaterialName : "NULL");
	//xml->SetAttribute("m_ePhysicType", MaterialPropertiesData.m_ePhysicType);
	switch (MaterialPropertiesData->m_ePhysicType)
	{
	case A3DPhysicType_None:
	case A3DPhysicType_Fiber:
	case A3DPhysicType_HoneyComb:
	case A3DPhysicType_Isotropic:
	case A3DPhysicType_Orthotropic2D:
	case A3DPhysicType_Orthotropic3D:
	case A3DPhysicType_Anisotropic:

	default:
		break;
	}
}

FMatrix FTechSoftFileParser::TraverseTransformation3D(const A3DMiscTransformation* CartesianTransformation)
{
	TUniqueTSObj<A3DMiscCartesianTransformationData> CartesianTransformationData(CartesianTransformation);

	if (CartesianTransformationData.IsValid())
	{
		FVector Origin(CartesianTransformationData->m_sOrigin.m_dX, CartesianTransformationData->m_sOrigin.m_dY, CartesianTransformationData->m_sOrigin.m_dZ);
		FVector XVector(CartesianTransformationData->m_sXVector.m_dX, CartesianTransformationData->m_sXVector.m_dY, CartesianTransformationData->m_sXVector.m_dZ);;
		FVector YVector(CartesianTransformationData->m_sYVector.m_dX, CartesianTransformationData->m_sYVector.m_dY, CartesianTransformationData->m_sYVector.m_dZ);;

		FVector ZVector = XVector ^ YVector;

		const A3DVector3dData& Scale = CartesianTransformationData->m_sScale;

		FMatrix Matrix(XVector * Scale.m_dX, YVector * Scale.m_dY, ZVector * Scale.m_dZ, FVector::Zero());

		if (CartesianTransformationData->m_ucBehaviour & kA3DTransformationMirror)
		{
			Matrix.M[2][0] *= -1;
			Matrix.M[2][1] *= -1;
			Matrix.M[2][2] *= -1;
		}

		Matrix.SetOrigin(Origin * FileUnit);

		return Matrix;
	}

	return FMatrix::Identity;
}

FMatrix FTechSoftFileParser::TraverseGeneralTransformation(const A3DMiscTransformation* GeneralTransformation)
{
	TUniqueTSObj<A3DMiscGeneralTransformationData> GeneralTransformationData(GeneralTransformation);
	if (GeneralTransformationData.IsValid())
	{
		FMatrix Matrix;
		int32 Index = 0;
		for (int32 Andex = 0; Andex < 4; ++Andex)
		{
			for (int32 Bndex = 0; Bndex < 4; ++Bndex, ++Index)
			{
				Matrix.M[Andex][Bndex] = GeneralTransformationData->m_adCoeff[Index];
			}
		}

		for (Index = 0; Index < 3; ++Index, ++Index)
		{
			Matrix.M[3][Index] *= FileUnit;
		}

		return Matrix;
	}
	return FMatrix::Identity;
}

FMatrix FTechSoftFileParser::TraverseTransformation(const A3DMiscTransformation* Transformation3D)
{
	if (Transformation3D == NULL)
	{
		return FMatrix::Identity;
	}

	A3DEEntityType Type = kA3DTypeUnknown;
	A3DEntityGetType(Transformation3D, &Type);

	if (Type == kA3DTypeMiscCartesianTransformation)
	{
		return TraverseTransformation3D(Transformation3D);
	}
	else if (Type == kA3DTypeMiscGeneralTransformation)
	{
		return TraverseGeneralTransformation(Transformation3D);
	}
	return FMatrix::Identity;
}

FMatrix FTechSoftFileParser::TraverseCoordinateSystem(const A3DRiCoordinateSystem* CoordinateSystem)
{
	TUniqueTSObj<A3DRiCoordinateSystemData> CoordinateSystemData(CoordinateSystem);
	if (CoordinateSystemData.IsValid())
	{
		return TraverseTransformation3D(CoordinateSystemData->m_pTransformation);
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
	A3DGlobal* GlobalPtr = nullptr;
	if (TechSoftUtils::GetGlobalPointer(&GlobalPtr) != A3D_SUCCESS)
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
	A3DGlobal* GlobalPtr = nullptr;
	if(TechSoftUtils::GetGlobalPointer(&GlobalPtr) != A3D_SUCCESS)
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
		uint32 MaterialCount = GlobalData->m_uiMaterialsSize;
		if (MaterialCount)
		{
			for (uint32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
			{
				FTechSoftFileParser::FindOrAddMaterial(MaterialIndex);
			}
		}
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
				A3DEntity* PicturePtr = TechSoftUtils::GetPointerFromIndex(PictureIndex, kA3DTypeGraphPicture);
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

} // ns CADLibrary

#endif  