// Copyright Epic Games, Inc. All Rights Reserved.

#include "TechSoftFileParser.h"

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

	if (CADFileData.GetImportParameters().GetStitchingTechnique() != StitchingNone && !FImportParameters::bGDisableCADKernelTessellation)
	{
		// todo
		//TechSoftInterface.Repair(CADFileData.GetStitchingTechnique());
	}

	ReserveCADFileData();

	ReadMaterialsAndColors();

	ECADParsingResult Result = TraverseModel(TechSoftInterface.GetModelFile());

	TechSoftInterface.UnloadModel();

	return Result;
}

void FTechSoftFileParser::ReserveCADFileData()
{
	// TODO: Could be more accurate
	CountUnderModel(TechSoftInterface.GetModelFile());

	CADFileData.ReserveBodyMeshes(ComponentCount[EComponentType::Body]);

	FArchiveSceneGraph& SceneGraphArchive = CADFileData.GetSceneGraphArchive();
	SceneGraphArchive.Reserve(ComponentCount[EComponentType::Occurrence], ComponentCount[EComponentType::Reference], ComponentCount[EComponentType::Body]);

	uint32 MaterialNum = CountMaterial() + CountColor();
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

#ifndef NEW_CODE
	// Is this really necessary ????
	if (ReferenceData->m_pPrototype && !ReferenceData->m_uiPOccurrencesSize && !ReferenceData->m_pPart)
	{
		TraversePrototype(ReferenceData->m_pPrototype, Component);
	}
#endif
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

FArchiveBody& FTechSoftFileParser::AddBody(FEntityMetaData& BodyMetaData)
{
	FCadId BodyId = LastEntityId++;
	int32 BodyIndex = CADFileData.AddBody(BodyId);
	FArchiveBody& Body = CADFileData.GetBodyAt(BodyIndex);
	Body.MetaData = MoveTemp(BodyMetaData.MetaData);

	return Body;
}

FCadId FTechSoftFileParser::TraverseOccurrence(const A3DAsmProductOccurrence* OccurrencePtr)
{
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
	ExtractLayer(OccurrencePtr);

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
	
	A3DAsmProductOccurrence* PrototypePtr = OccurrenceData->m_pPrototype;
	while (OccurrenceData->m_pPrototype != nullptr && OccurrenceData->m_pPart == nullptr && OccurrenceData->m_uiPOccurrencesSize == 0)
	{
		OccurrenceData.FillFrom(OccurrenceData->m_pPrototype);
	}

	if(OccurrenceData->m_pPart == nullptr && OccurrenceData->m_uiPOccurrencesSize == 0)
	{
		return Instance.ObjectId;
	}

	FArchiveComponent& Component = AddComponent(InstanceMetaData, Instance);

	if (OccurrenceData->m_pPart != nullptr)
	{
		A3DAsmPartDefinition* PartDefinition = OccurrenceData->m_pPart;
		TraversePartDefinition(PartDefinition, Component);
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

void FTechSoftFileParser::ProcessOccurrence(TUniqueTSObj<A3DAsmProductOccurrenceData>& OccurrenceData, FArchiveComponent& Component)
{
	if (!OccurrenceData.IsValid())
	{
		return;
	}

	if (OccurrenceData->m_pPart)
	{
		TraversePartDefinition(OccurrenceData->m_pPart, Component);
	}

	if (OccurrenceData->m_pExternalData)
	{
		int32 ChildrenId = TraverseOccurrence(OccurrenceData->m_pExternalData);
		Component.Children.Add(ChildrenId);
	}

	for (uint32 Index = 0; Index < OccurrenceData->m_uiPOccurrencesSize; ++Index)
	{
		int32 ChildrenId = TraverseOccurrence(OccurrenceData->m_ppPOccurrences[Index]);
		Component.Children.Add(ChildrenId);
	}
};

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

void FTechSoftFileParser::TraversePrototype(const A3DAsmProductOccurrence* InPrototypePtr, FArchiveComponent& Component)
{
	TUniqueTSObj<A3DAsmProductOccurrenceData> SubPrototypeData(InPrototypePtr);
	if (!SubPrototypeData.IsValid())
	{
		return;
	}

	for (uint32 Index = 0; Index < SubPrototypeData->m_uiPOccurrencesSize; ++Index)
	{
		int32 ChildrenId = TraverseOccurrence(SubPrototypeData->m_ppPOccurrences[Index]);
		Component.Children.Add(ChildrenId);
	}

	if (SubPrototypeData->m_pPart)
	{
		TraversePartDefinition(SubPrototypeData->m_pPart, Component);
	}

	if (SubPrototypeData->m_pPrototype && !SubPrototypeData->m_uiPOccurrencesSize && !SubPrototypeData->m_pPart)
	{
		TraversePrototype(SubPrototypeData->m_pPrototype, Component);
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
	ExtractLayer(PartDefinitionPtr);

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

	if (FCadId* CadIdPtr = RepresentationItemsCache.Find(BRepModelPtr))
	{
		return *CadIdPtr;
	}

	TUniqueTSObj<A3DRiBrepModelData> BodyData(BRepModelPtr);
	if (!BodyData.IsValid())
	{
		return 0;
	}

	FEntityMetaData BRepMetaData;
	ExtractMetaData(BRepModelPtr, BRepMetaData);
	if (!BRepMetaData.bShow || BRepMetaData.bRemoved)
	{
		return 0;
	}

	ExtractSpecificMetaData(BRepModelPtr, BRepMetaData);
	ExtractMaterialProperties(BRepModelPtr);

	FArchiveBody& Body = AddBody(BRepMetaData);

	TraverseRepresentationContent(BRepModelPtr, Body);
	if (FImportParameters::bGDisableCADKernelTessellation)
	{
		MeshRepresentationWithTechSoft(BRepModelPtr, Body);
	}
	else
	{
		// Mesh with CADKernel
	}

	RepresentationItemsCache.Add(BRepModelPtr, Body.ObjectId);

	return Body.ObjectId;
}

void FTechSoftFileParser::TraverseRepresentationContent(const A3DRiRepresentationItem* RepresentationItemPtr, FArchiveBody& Body)
{
	TUniqueTSObj<A3DRiRepresentationItemData> RepresentationItemData(RepresentationItemPtr);
	if (!RepresentationItemData.IsValid())
	{
		return;
	}

	if (RepresentationItemData->m_pCoordinateSystem)
	{
		TraverseCoordinateSystem(RepresentationItemData->m_pCoordinateSystem);
	}

	if (RepresentationItemData->m_pTessBase)
	{
		TraverseTessellationBase(RepresentationItemData->m_pTessBase, Body);
	}
}

FCadId FTechSoftFileParser::TraversePolyBRepModel(const A3DRiPolyBrepModel* PolygonalPtr, FEntityMetaData& PartMetaData)
{
	if (!PolygonalPtr)
	{
		return 0;
	}

	if (FCadId* CadIdPtr = RepresentationItemsCache.Find(PolygonalPtr))
	{
		return *CadIdPtr;
	}

	TUniqueTSObj<A3DRiPolyBrepModelData> BodyData(PolygonalPtr);
	if (!BodyData.IsValid())
	{
		return 0;
	}

	FEntityMetaData BRepMetaData;
	ExtractMetaData(PolygonalPtr, BRepMetaData);
	if (!BRepMetaData.bShow || BRepMetaData.bRemoved)
	{
		return 0;
	}

	ExtractSpecificMetaData(PolygonalPtr, BRepMetaData);
	ExtractMaterialProperties(PolygonalPtr);

	FArchiveBody& Body = AddBody(BRepMetaData);
	TraverseRepresentationContent(PolygonalPtr, Body);

	RepresentationItemsCache.Add(PolygonalPtr, Body.ObjectId);

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
	FColor& Color = NewColor.Color;
	TUniqueTSObjFromIndex<A3DGraphRgbColorData> ColorData(ColorIndex);
	if (ColorData.IsValid())
	{
		Color.R = (uint8)(ColorData->m_dRed * 255);
		Color.G = (uint8)(ColorData->m_dGreen * 255);
		Color.B = (uint8)(ColorData->m_dBlue * 255);
		Color.A = Alpha;
	}
	else
	{
		Color = FColor(200, 200, 200);
	}

	NewColor.UEMaterialName = BuildColorName(NewColor.Color);
	return NewColor;
}

FArchiveMaterial& FTechSoftFileParser::FindOrAddMaterial(uint32 MaterialIndex)
{
	if (FArchiveMaterial* MaterialArchive = CADFileData.FindMaterial(MaterialIndex))
	{
		return *MaterialArchive;
	}

	TUniqueTSObjFromIndex<A3DGraphRgbColorData> ColorData;
	TFunction<const FColor(uint32)> GetColor = [&](uint32 ColorIndex) -> const FColor
	{
		ColorData.FillFrom(ColorIndex);
		if (ColorData.IsValid())
		{
			return FColor((uint8)ColorData->m_dRed * 255,
				(uint8)ColorData->m_dGreen * 255,
				(uint8)ColorData->m_dBlue * 255);
		}
		else
		{
			return FColor(200, 200, 200);
		}
	};

	FArchiveMaterial& NewMaterial = CADFileData.AddMaterial(MaterialIndex);
	FCADMaterial& Material = NewMaterial.Material;

	A3DBool bIsTexture = false;
	A3DGlobalIsMaterialTexture(MaterialIndex, &bIsTexture);
	if (bIsTexture)
	{
#ifdef NOTYETDEFINE
		// style is a texture
		TUniqueTSObj<A3DGraphTextureDefinitionData> TextureDefinitionData(TextureIndex);
		if (TextureDefinitionData.IsValid())
		{
			TUniqueTSObj<A3DGraphPictureData> PictureData(TextureDefinitionData->m_uiPictureIndex);
		}
#endif
	}
	else
	{
		TUniqueTSObjFromIndex<A3DGraphMaterialData> MaterialData(MaterialIndex);
		Material.Diffuse = GetColor(MaterialData->m_uiDiffuse);
		Material.Ambient = GetColor(MaterialData->m_uiAmbient);
		Material.Specular = GetColor(MaterialData->m_uiSpecular);
		Material.Shininess = MaterialData->m_dShininess;
		Material.Transparency = MaterialData->m_dAmbientAlpha;
		// todo: find how to convert Emissive color into ? reflexion coef...
		// Material.Emissive = GetColor(MaterialData->m_uiEmissive);
		// Material.Reflexion;
	}

	return NewMaterial;
}

void FTechSoftFileParser::ExtractGraphicProperties(const A3DGraphics* Graphics, FEntityMetaData& OutMetaData)
{
	TUniqueTSObj<A3DGraphicsData> GraphicsData(Graphics);
	if (!GraphicsData.IsValid())
	{
		return;
	}
	FEntityBehaviour GraphicsBehaviour;

	GraphicsBehaviour.bFatherHeritColor = GraphicsData->m_usBehaviour & kA3DGraphicsFatherHeritColor;
	GraphicsBehaviour.bFatherHeritLayer = GraphicsData->m_usBehaviour & kA3DGraphicsFatherHeritLayer;
	GraphicsBehaviour.bFatherHeritLinePattern = GraphicsData->m_usBehaviour & kA3DGraphicsFatherHeritLinePattern;
	GraphicsBehaviour.bFatherHeritLineWidth = GraphicsData->m_usBehaviour & kA3DGraphicsFatherHeritLineWidth;
	GraphicsBehaviour.bFatherHeritShow = GraphicsData->m_usBehaviour & kA3DGraphicsFatherHeritShow;
	GraphicsBehaviour.bFatherHeritTransparency = GraphicsData->m_usBehaviour & kA3DGraphicsFatherHeritTransparency;
	GraphicsBehaviour.bRemoved = GraphicsData->m_usBehaviour & kA3DGraphicsRemoved;
	GraphicsBehaviour.bShow = GraphicsData->m_usBehaviour & kA3DGraphicsShow;
	GraphicsBehaviour.bSonHeritColor = GraphicsData->m_usBehaviour & kA3DGraphicsSonHeritColor;
	GraphicsBehaviour.bSonHeritLayer = GraphicsData->m_usBehaviour & kA3DGraphicsSonHeritLayer;
	GraphicsBehaviour.bSonHeritLinePattern = GraphicsData->m_usBehaviour & kA3DGraphicsSonHeritLinePattern;
	GraphicsBehaviour.bSonHeritLineWidth = GraphicsData->m_usBehaviour & kA3DGraphicsSonHeritLineWidth;
	GraphicsBehaviour.bSonHeritShow = GraphicsData->m_usBehaviour & kA3DGraphicsSonHeritShow;
	GraphicsBehaviour.bSonHeritTransparency = GraphicsData->m_usBehaviour & kA3DGraphicsSonHeritTransparency;

	OutMetaData.bRemoved = GraphicsBehaviour.bRemoved;
	OutMetaData.bShow = GraphicsBehaviour.bShow;

	FCADUUID ColorName;
	FCADUUID MaterialName;

	ExtractGraphStyleProperties(GraphicsData->m_uiStyleIndex, ColorName, MaterialName);

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
	OutColorName = 0;
	OutMaterialName = 0;

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

void FTechSoftFileParser::ExtractLayer(const A3DAsmProductOccurrence* Occurrence)
{
	// TODO

	A3DUns32 LayerCount = 0;
	A3DAsmLayer* AsmLayer = 0;
	if (A3DAsmProductOccurrenceGetLayerList(Occurrence, &LayerCount, &AsmLayer) == A3D_SUCCESS)
	{
		if (LayerCount)
		{
			for (A3DUns32 Index = 0; Index < LayerCount; ++Index)
			{
				//Layer->SetAttribute("Name", asLayers[Index].m_pcLayerName ? asLayers[Index].m_pcLayerName : "null");
				//Layer->SetAttribute("Layer", asLayers[Index].m_usLayer);
			}
		}
		A3DAsmProductOccurrenceGetLayerList(NULL, &LayerCount, &AsmLayer);
	}
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

int32 FTechSoftFileParser::CountMaterial()
{
	// TODO

	return 0;
}

int32 FTechSoftFileParser::CountColor()
{
	// TODO

	return 0;
}

void FTechSoftFileParser::ReadMaterialsAndColors()
{
	// TODO
}

void FTechSoftFileParser::MeshRepresentationWithTechSoft(A3DRiRepresentationItem* RepresentationItemPtr, FArchiveBody& Body)
{
	TUniqueTSObj<A3DRiRepresentationItemData> RepresentationItemData;

	// TUniqueTechSoftObj does not work in this case
	A3DRWParamsTessellationData TessellationParameters;
	A3D_INITIALIZE_DATA(A3DRWParamsTessellationData, TessellationParameters);

	if (true)
	{
		TessellationParameters.m_eTessellationLevelOfDetail = kA3DTessLODMedium;		/*!< Enum to specify predefined values for some following members. */
	}
	else
	{
		// TODO
		// Check unity conversion
		ensure(false);

		const FImportParameters& ImportParameters = CADFileData.GetImportParameters();
		////A3DRWParamsTessellationData /*!< The tessellation reading parameters. */
		TessellationParameters.m_eTessellationLevelOfDetail = kA3DTessLODUserDefined;		/*!< Enum to specify predefined values for some following members. */
		TessellationParameters.m_bUseHeightInsteadOfRatio = A3D_TRUE;
		TessellationParameters.m_dMaxChordHeight = ImportParameters.GetChordTolerance();
		TessellationParameters.m_dAngleToleranceDeg = ImportParameters.GetMaxNormalAngle();
		TessellationParameters.m_dMaximalTriangleEdgeLength = 0; //ImportParameters.MaxEdgeLength;

		TessellationParameters.m_bAccurateTessellation = A3D_FALSE;  // A3D_FALSE' indicates the tessellation is set for visualization
		TessellationParameters.m_bAccurateTessellationWithGrid = A3D_FALSE; /*!< Enable accurate tessellation with faces inner points on a grid.*/
		TessellationParameters.m_dAccurateTessellationWithGridMaximumStitchLength = 0; 	/*!< Maximal grid stitch length. Disabled if value is 0. Be careful, a too small value can generate a huge tessellation. */
	}

	TessellationParameters.m_bKeepUVPoints = A3D_TRUE; /*!< Keep parametric points as texture points. */

	// Get the tessellation
	A3DStatus Status = A3DRiRepresentationItemComputeTessellation(RepresentationItemPtr, &TessellationParameters);
	Status = A3DRiRepresentationItemGet(RepresentationItemPtr, RepresentationItemData.GetEmptyDataPtr());
	TraverseTessellationBase(RepresentationItemData->m_pTessBase, Body);
}

void FTechSoftFileParser::TraverseTessellationBase(const A3DTessBase* Tessellation, FArchiveBody& Body)
{
	A3DEEntityType eType;
	if (A3DEntityGetType(Tessellation, &eType) == A3D_SUCCESS)
	{
		switch (eType)
		{
		case kA3DTypeTess3D:
			TraverseTessellation3D(Tessellation, Body);
			break;
		case kA3DTypeTess3DWire:
		case kA3DTypeTessMarkup:
		default:
			break;
		}
	}
}

namespace TechSoftFileParserImpl
{

uint32 CountTriangles(const A3DTessFaceData& FaceTessData)
{
	const int32 TessellationFaceDataWithTriangle = 0x2222;
	const int32 TessellationFaceDataWithFan = 0x4444;
	const int32 TessellationFaceDataWithStrip = 0x8888;
	const int32 TessellationFaceDataWithOneNormal = 0xE0E0;

	uint32 UsedEntitiesFlags = FaceTessData.m_usUsedEntitiesFlags;

	uint32 TriangleCount = 0;
	uint32 FaceSetIndex = 0;
	if (UsedEntitiesFlags & TessellationFaceDataWithTriangle)
	{
		TriangleCount += FaceTessData.m_puiSizesTriangulated[FaceSetIndex];
		FaceSetIndex++;
	}

	if (FaceTessData.m_uiSizesTriangulatedSize > FaceSetIndex)
	{
		if (UsedEntitiesFlags & TessellationFaceDataWithFan)
		{
			uint32 LastFanIndex = 1 + FaceSetIndex + FaceTessData.m_puiSizesTriangulated[FaceSetIndex];
			FaceSetIndex++;
			for (; FaceSetIndex < LastFanIndex; FaceSetIndex++)
			{
				uint32 FanSize = (FaceTessData.m_puiSizesTriangulated[FaceSetIndex] & kA3DTessFaceDataNormalMask);
				TriangleCount += (FanSize - 2);
			}
		}
	}

	if (FaceTessData.m_uiSizesTriangulatedSize > FaceSetIndex)
	{
		FaceSetIndex++;
		for (; FaceSetIndex < FaceTessData.m_uiSizesTriangulatedSize; FaceSetIndex++)
		{
			uint32 StripSize = (FaceTessData.m_puiSizesTriangulated[FaceSetIndex] & kA3DTessFaceDataNormalMask);
			TriangleCount += (StripSize - 2);
		}
	}
	return TriangleCount;
}

void AddFaceTriangleWithUniqueNormal(FTessellationData& Tessellation, const A3DTess3DData& Tessellation3DData, const uint32 InTriangleCount, uint32& InOutLastTriangleIndex, int32& InOutLastVertexIndex)
{
	int32 FaceIndex[3] = { 0, 0, 0 };
	int32 NormalIndex[3] = { 0, 0, 0 };

	// Get Triangles
	for (uint32 TriangleIndex = 0; TriangleIndex < InTriangleCount; TriangleIndex++)
	{
		NormalIndex[0] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex++];
		NormalIndex[1] = NormalIndex[0];
		NormalIndex[2] = NormalIndex[0];

		FaceIndex[0] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex++] / 3;
		FaceIndex[1] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex++] / 3;
		FaceIndex[2] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex++] / 3;

		if (!TechSoftFileParserImpl::AddFace(FaceIndex, Tessellation, InOutLastVertexIndex))
		{
			continue;
		}

		TechSoftFileParserImpl::AddNormals(Tessellation3DData.m_pdNormals, NormalIndex, Tessellation.NormalArray);
	}
}

void AddFaceTriangleWithUniqueNormalAndTexture(FTessellationData& Tessellation, const A3DTess3DData& Tessellation3DData, const uint32 InTriangleCount, const uint32 TextureCount, uint32& InOutStartIndex, int32& InOutLastVertexIndex)
{
	int32 FaceIndex[3] = { 0, 0, 0 };
	int32 NormalIndex[3] = { 0, 0, 0 };
	int32 TextureIndex[3] = { 0, 0, 0 };

	// Get Triangles
	for (uint32 TriangleIndex = 0; TriangleIndex < InTriangleCount; TriangleIndex++)
	{
		NormalIndex[0] = Tessellation3DData.m_puiTriangulatedIndexes[InOutStartIndex++];
		NormalIndex[1] = NormalIndex[0];
		NormalIndex[2] = NormalIndex[0];

		TextureIndex[0] = Tessellation3DData.m_puiTriangulatedIndexes[InOutStartIndex];
		InOutStartIndex += TextureCount;
		FaceIndex[0] = Tessellation3DData.m_puiTriangulatedIndexes[InOutStartIndex++] / 3;
		TextureIndex[1] = Tessellation3DData.m_puiTriangulatedIndexes[InOutStartIndex];
		InOutStartIndex += TextureCount;
		FaceIndex[1] = Tessellation3DData.m_puiTriangulatedIndexes[InOutStartIndex++] / 3;
		TextureIndex[2] = Tessellation3DData.m_puiTriangulatedIndexes[InOutStartIndex];
		InOutStartIndex += TextureCount;
		FaceIndex[2] = Tessellation3DData.m_puiTriangulatedIndexes[InOutStartIndex++] / 3;

		if (!TechSoftFileParserImpl::AddFace(FaceIndex, Tessellation, InOutLastVertexIndex))
		{
			continue;
		}

		TechSoftFileParserImpl::AddNormals(Tessellation3DData.m_pdNormals, NormalIndex, Tessellation.NormalArray);
		TechSoftFileParserImpl::AddTextureCoordinates(Tessellation3DData.m_pdTextureCoords, TextureIndex, Tessellation.TexCoordArray);
	}
}

void AddFaceTriangle(FTessellationData& Tessellation, const A3DTess3DData& Tessellation3DData, const uint32 InTriangleCount, uint32& InOutStartIndex, int32& InOutLastVertexIndex)
{
	int32 FaceIndex[3] = { 0, 0, 0 };
	int32 NormalIndex[3] = { 0, 0, 0 };

	// Get Triangles
	for (uint32 TriangleIndex = 0; TriangleIndex < InTriangleCount; TriangleIndex++)
	{
		NormalIndex[0] = Tessellation3DData.m_puiTriangulatedIndexes[InOutStartIndex++];
		FaceIndex[0] = Tessellation3DData.m_puiTriangulatedIndexes[InOutStartIndex++] / 3;
		NormalIndex[1] = Tessellation3DData.m_puiTriangulatedIndexes[InOutStartIndex++];
		FaceIndex[1] = Tessellation3DData.m_puiTriangulatedIndexes[InOutStartIndex++] / 3;
		NormalIndex[2] = Tessellation3DData.m_puiTriangulatedIndexes[InOutStartIndex++];
		FaceIndex[2] = Tessellation3DData.m_puiTriangulatedIndexes[InOutStartIndex++] / 3;

		if (TechSoftFileParserImpl::AddFace(FaceIndex, Tessellation, InOutLastVertexIndex))
		{
			TechSoftFileParserImpl::AddNormals(Tessellation3DData.m_pdNormals, NormalIndex, Tessellation.NormalArray);
		}
	}
}

void AddFaceTriangleWithTexture(FTessellationData& Tessellation, const A3DTess3DData& Tessellation3DData, const uint32 InTriangleCount, const uint32 InTextureCount, uint32& InOutStartIndex, int32& inOutLastVertexIndex)
{
	int32 FaceIndex[3] = { 0, 0, 0 };
	int32 NormalIndex[3] = { 0, 0, 0 };
	int32 TextureIndex[3] = { 0, 0, 0 };

	// Get Triangles
	for (uint64 TriangleIndex = 0; TriangleIndex < InTriangleCount; TriangleIndex++)
	{
		NormalIndex[0] = Tessellation3DData.m_puiTriangulatedIndexes[InOutStartIndex++];
		TextureIndex[0] = Tessellation3DData.m_puiTriangulatedIndexes[InOutStartIndex];
		InOutStartIndex += InTextureCount;
		FaceIndex[0] = Tessellation3DData.m_puiTriangulatedIndexes[InOutStartIndex++] / 3;
		NormalIndex[1] = Tessellation3DData.m_puiTriangulatedIndexes[InOutStartIndex++];
		TextureIndex[1] = Tessellation3DData.m_puiTriangulatedIndexes[InOutStartIndex];
		InOutStartIndex += InTextureCount;
		FaceIndex[1] = Tessellation3DData.m_puiTriangulatedIndexes[InOutStartIndex++] / 3;
		NormalIndex[2] = Tessellation3DData.m_puiTriangulatedIndexes[InOutStartIndex++];
		TextureIndex[2] = Tessellation3DData.m_puiTriangulatedIndexes[InOutStartIndex];
		InOutStartIndex += InTextureCount;
		FaceIndex[2] = Tessellation3DData.m_puiTriangulatedIndexes[InOutStartIndex++] / 3;

		if (TechSoftFileParserImpl::AddFace(FaceIndex, Tessellation, inOutLastVertexIndex))
		{
			TechSoftFileParserImpl::AddNormals(Tessellation3DData.m_pdNormals, NormalIndex, Tessellation.NormalArray);
			TechSoftFileParserImpl::AddTextureCoordinates(Tessellation3DData.m_pdTextureCoords, TextureIndex, Tessellation.TexCoordArray);
		}
	}
}

void AddFaceTriangleFanWithUniqueNormal(FTessellationData& Tessellation, const A3DTess3DData& Tessellation3DData, const uint32 InTriangleCount, uint32& InOutLastTriangleIndex, int32& LastVertexIndex)
{
	int32 FaceIndex[3] = { 0, 0, 0 };
	int32 NormalIndex[3] = { 0, 0, 0 };

	NormalIndex[0] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex++];
	NormalIndex[1] = NormalIndex[0];
	NormalIndex[2] = NormalIndex[0];

	FaceIndex[0] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex++] / 3;
	FaceIndex[1] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex++] / 3;

	// Get Triangles
	for (uint32 TriangleIndex = 2; TriangleIndex < InTriangleCount; TriangleIndex++)
	{
		FaceIndex[2] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex++] / 3;

		if (TechSoftFileParserImpl::AddFace(FaceIndex, Tessellation, LastVertexIndex))
		{
			TechSoftFileParserImpl::AddNormals(Tessellation3DData.m_pdNormals, NormalIndex, Tessellation.NormalArray);
		}

		FaceIndex[1] = FaceIndex[2];
	}
}

void AddFaceTriangleFanWithUniqueNormalAndTexture(FTessellationData& Tessellation, const A3DTess3DData& Tessellation3DData, const uint32 InTriangleCount, const uint32 TextureCount, uint32& InOutLastTriangleIndex, int32& InOutLastVertexIndex)
{
	int32 FaceIndex[3] = { 0, 0, 0 };
	int32 NormalIndex[3] = { 0, 0, 0 };
	int32 TextureIndex[3] = { 0, 0, 0 };

	NormalIndex[0] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex++];
	NormalIndex[1] = NormalIndex[0];
	NormalIndex[2] = NormalIndex[0];

	TextureIndex[0] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex];
	InOutLastTriangleIndex += TextureCount;
	FaceIndex[0] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex++] / 3;

	TextureIndex[1] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex];
	InOutLastTriangleIndex += TextureCount;
	FaceIndex[1] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex++] / 3;

	for (uint32 TriangleIndex = 2; TriangleIndex < InTriangleCount; TriangleIndex++)
	{
		TextureIndex[2] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex];
		InOutLastTriangleIndex += TextureCount;
		FaceIndex[2] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex++] / 3;

		if (TechSoftFileParserImpl::AddFace(FaceIndex, Tessellation, InOutLastVertexIndex))
		{
			TechSoftFileParserImpl::AddNormals(Tessellation3DData.m_pdNormals, NormalIndex, Tessellation.NormalArray);
			TechSoftFileParserImpl::AddTextureCoordinates(Tessellation3DData.m_pdTextureCoords, TextureIndex, Tessellation.TexCoordArray);
		}

		FaceIndex[1] = FaceIndex[2];
		TextureIndex[1] = TextureIndex[2];
	}
}

void AddFaceTriangleFan(FTessellationData& Tessellation, const A3DTess3DData& Tessellation3DData, const uint32 InTriangleCount, uint32& InOutLastTriangleIndex, int32& InOutLastVertexIndex)
{
	int32 FaceIndex[3] = { 0, 0, 0 };
	int32 NormalIndex[3] = { 0, 0, 0 };

	NormalIndex[0] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex++];
	FaceIndex[0] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex++] / 3;
	NormalIndex[1] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex++];
	FaceIndex[1] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex++] / 3;

	for (unsigned long TriangleIndex = 2; TriangleIndex < InTriangleCount; TriangleIndex++)
	{
		NormalIndex[2] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex++];
		FaceIndex[2] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex++] / 3;

		if (TechSoftFileParserImpl::AddFace(FaceIndex, Tessellation, InOutLastVertexIndex))
		{
			TechSoftFileParserImpl::AddNormals(Tessellation3DData.m_pdNormals, NormalIndex, Tessellation.NormalArray);
		}

		NormalIndex[1] = NormalIndex[2];
		FaceIndex[1] = FaceIndex[2];
	}
}

void AddFaceTriangleFanWithTexture(FTessellationData& Tessellation, const A3DTess3DData& Tessellation3DData, const uint32 InTriangleCount, const uint32 TextureCount, uint32& InOutLastTriangleIndex, int32& InOutLastVertexIndex)
{
	int32 FaceIndex[3] = { 0, 0, 0 };
	int32 NormalIndex[3] = { 0, 0, 0 };
	int32 TextureIndex[3] = { 0, 0, 0 };

	NormalIndex[0] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex++];
	TextureIndex[0] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex];
	InOutLastTriangleIndex += TextureCount;
	FaceIndex[0] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex++] / 3;

	NormalIndex[1] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex++];
	TextureIndex[1] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex];
	InOutLastTriangleIndex += TextureCount;
	FaceIndex[1] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex++] / 3;

	for (uint32 TriangleIndex = 2; TriangleIndex < InTriangleCount; TriangleIndex++)
	{
		NormalIndex[2] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex++];
		TextureIndex[2] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex];
		InOutLastTriangleIndex += TextureCount;
		FaceIndex[2] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex++] / 3;

		if (TechSoftFileParserImpl::AddFace(FaceIndex, Tessellation, InOutLastVertexIndex))
		{
			TechSoftFileParserImpl::AddNormals(Tessellation3DData.m_pdNormals, NormalIndex, Tessellation.NormalArray);
			TechSoftFileParserImpl::AddTextureCoordinates(Tessellation3DData.m_pdTextureCoords, TextureIndex, Tessellation.TexCoordArray);
		}

		NormalIndex[1] = NormalIndex[2];
		TextureIndex[1] = TextureIndex[2];
		FaceIndex[1] = FaceIndex[2];
	}
}

void AddFaceTriangleStripWithUniqueNormal(FTessellationData& Tessellation, const A3DTess3DData& Tessellation3DData, const uint32 InTriangleCount, uint32& InOutLastTriangleIndex, int32& InOutLastVertexIndex)
{
	int32 FaceIndex[3] = { 0, 0, 0 };
	int32 NormalIndex[3] = { 0, 0, 0 };

	NormalIndex[0] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex++];
	NormalIndex[1] = NormalIndex[0];
	NormalIndex[2] = NormalIndex[0];

	FaceIndex[0] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex++] / 3;
	FaceIndex[1] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex++] / 3;

	for (unsigned long TriangleIndex = 0; TriangleIndex < InTriangleCount; TriangleIndex++)
	{
		FaceIndex[2] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex++] / 3;

		if (TechSoftFileParserImpl::AddFace(FaceIndex, Tessellation, InOutLastVertexIndex))
		{
			TechSoftFileParserImpl::AddNormals(Tessellation3DData.m_pdNormals, NormalIndex, Tessellation.NormalArray);
		}

		TriangleIndex++;
		if (TriangleIndex == InTriangleCount)
		{
			break;
		}

		Swap(FaceIndex[1], FaceIndex[2]);

		NormalIndex[0] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex++];
		FaceIndex[0] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex++] / 3;

		if (TechSoftFileParserImpl::AddFace(FaceIndex, Tessellation, InOutLastVertexIndex))
		{
			TechSoftFileParserImpl::AddNormals(Tessellation3DData.m_pdNormals, NormalIndex, Tessellation.NormalArray);
		}

		Swap(FaceIndex[0], FaceIndex[1]);
	}
}

void AddFaceTriangleStripWithUniqueNormalAndTexture(FTessellationData& Tessellation, const A3DTess3DData& Tessellation3DData, const uint32 InTriangleCount, const uint32 TextureCount, uint32& InOutLastTriangleIndex, int32& InOutLastVertexIndex)
{
	int32 FaceIndex[3] = { 0, 0, 0 };
	int32 NormalIndex[3] = { 0, 0, 0 };
	int32 TextureIndex[3] = { 0, 0, 0 };

	NormalIndex[0] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex++];
	NormalIndex[1] = NormalIndex[0];
	NormalIndex[2] = NormalIndex[0];

	TextureIndex[0] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex];
	InOutLastTriangleIndex += TextureCount;
	FaceIndex[0] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex++];

	TextureIndex[1] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex];
	InOutLastTriangleIndex += TextureCount;
	FaceIndex[1] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex++];

	for (unsigned long TriangleIndex = 0; TriangleIndex < InTriangleCount; TriangleIndex++)
	{
		TextureIndex[2] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex];
		InOutLastTriangleIndex += TextureCount;
		FaceIndex[2] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex++];

		if (TechSoftFileParserImpl::AddFace(FaceIndex, Tessellation, InOutLastVertexIndex))
		{
			TechSoftFileParserImpl::AddNormals(Tessellation3DData.m_pdNormals, NormalIndex, Tessellation.NormalArray);
			TechSoftFileParserImpl::AddTextureCoordinates(Tessellation3DData.m_pdTextureCoords, TextureIndex, Tessellation.TexCoordArray);
		}

		TriangleIndex++;
		if (TriangleIndex == InTriangleCount)
		{
			break;
		}

		Swap(FaceIndex[1], FaceIndex[2]);
		Swap(TextureIndex[1], TextureIndex[2]);

		FaceIndex[0] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex++] / 3;

		if (TechSoftFileParserImpl::AddFace(FaceIndex, Tessellation, InOutLastVertexIndex))
		{
			TechSoftFileParserImpl::AddNormals(Tessellation3DData.m_pdNormals, NormalIndex, Tessellation.NormalArray);
		}

		Swap(FaceIndex[0], FaceIndex[1]);
		Swap(TextureIndex[0], TextureIndex[1]);
	}
}

void AddFaceTriangleStrip(FTessellationData& Tessellation, const A3DTess3DData& Tessellation3DData, const uint32 InTriangleCount, uint32& InOutLastTriangleIndex, int32& LastVertexIndex)
{
	int32 FaceIndex[3] = { 0, 0, 0 };
	int32 NormalIndex[3] = { 0, 0, 0 };

	NormalIndex[0] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex++];
	FaceIndex[0] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex++] / 3;
	NormalIndex[1] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex++];
	FaceIndex[1] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex++] / 3;

	for (unsigned long TriangleIndex = 2; TriangleIndex < InTriangleCount; TriangleIndex++)
	{
		NormalIndex[2] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex++];
		FaceIndex[2] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex++] / 3;

		if (TechSoftFileParserImpl::AddFace(FaceIndex, Tessellation, LastVertexIndex))
		{
			TechSoftFileParserImpl::AddNormals(Tessellation3DData.m_pdNormals, NormalIndex, Tessellation.NormalArray);
		}

		TriangleIndex++;
		if (TriangleIndex == InTriangleCount)
		{
			break;
		}

		Swap(FaceIndex[1], FaceIndex[2]);
		Swap(NormalIndex[1], NormalIndex[2]);

		NormalIndex[0] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex++];
		FaceIndex[0] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex++] / 3;

		if (TechSoftFileParserImpl::AddFace(FaceIndex, Tessellation, LastVertexIndex))
		{
			TechSoftFileParserImpl::AddNormals(Tessellation3DData.m_pdNormals, NormalIndex, Tessellation.NormalArray);
		}

		Swap(FaceIndex[0], FaceIndex[1]);
		Swap(NormalIndex[0], NormalIndex[1]);
	}
}

void AddFaceTriangleStripWithTexture(FTessellationData& Tessellation, const A3DTess3DData& Tessellation3DData, const uint32 InTriangleCount, const uint32 TextureCount, uint32& InOutLastTriangleIndex, int32& InOutLastVertexIndex)
{
	int32 FaceIndex[3] = { 0, 0, 0 };
	int32 NormalIndex[3] = { 0, 0, 0 };
	int32 TextureIndex[3] = { 0, 0, 0 };

	NormalIndex[0] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex++];
	TextureIndex[0] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex];
	InOutLastTriangleIndex += TextureCount;
	FaceIndex[0] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex++];
	NormalIndex[1] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex++];
	TextureIndex[1] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex];
	InOutLastTriangleIndex += TextureCount;
	FaceIndex[1] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex++];

	for (unsigned long TriangleIndex = 0; TriangleIndex < InTriangleCount; TriangleIndex++)
	{
		NormalIndex[2] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex++];
		TextureIndex[2] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex];
		InOutLastTriangleIndex += TextureCount;
		FaceIndex[2] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex++];

		if (TechSoftFileParserImpl::AddFace(FaceIndex, Tessellation, InOutLastVertexIndex))
		{
			TechSoftFileParserImpl::AddNormals(Tessellation3DData.m_pdNormals, NormalIndex, Tessellation.NormalArray);
			TechSoftFileParserImpl::AddTextureCoordinates(Tessellation3DData.m_pdTextureCoords, TextureIndex, Tessellation.TexCoordArray);
		}

		TriangleIndex++;
		if (TriangleIndex == InTriangleCount)
		{
			break;
		}

		Swap(FaceIndex[1], FaceIndex[2]);
		Swap(NormalIndex[1], NormalIndex[2]);
		Swap(TextureIndex[1], TextureIndex[2]);

		NormalIndex[0] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex++];
		FaceIndex[0] = Tessellation3DData.m_puiTriangulatedIndexes[InOutLastTriangleIndex++] / 3;

		if (TechSoftFileParserImpl::AddFace(FaceIndex, Tessellation, InOutLastVertexIndex))
		{
			TechSoftFileParserImpl::AddNormals(Tessellation3DData.m_pdNormals, NormalIndex, Tessellation.NormalArray);
		}

		Swap(FaceIndex[0], FaceIndex[1]);
		Swap(NormalIndex[0], NormalIndex[1]);
		Swap(TextureIndex[0], TextureIndex[1]);
	}
}

} //ns FTechSoftFileParserImpl


#pragma optimize ("",off)
void FTechSoftFileParser::TraverseTessellation3D(const A3DTess3D* TessellationPtr, FArchiveBody& Body)
{
	FBodyMesh& BodyMesh = CADFileData.AddBodyMesh(Body.ObjectId, Body);

	const int TessellationFaceDataWithTriangle = 0x2222;
	const int TessellationFaceDataWithFan = 0x4444;
	const int TessellationFaceDataWithStrip = 0x8888;
	const int TessellationFaceDataWithOneNormal = 0xE0E0;

	// Coordinates
	TUniqueTSObj<A3DTessBaseData> TessellationBaseData(TessellationPtr);
	{
		if (TessellationBaseData.IsValid() && TessellationBaseData->m_uiCoordSize > 0)
		{
			int32 VertexCount = TessellationBaseData->m_uiCoordSize / 3;
			BodyMesh.VertexArray.Reserve(VertexCount);

			double* Coordinates = TessellationBaseData->m_pdCoords;
			for (unsigned int Index = 0; Index < TessellationBaseData->m_uiCoordSize; ++Index)
			{
				Coordinates[Index] *= FileUnit;
			}

			for (unsigned int Index = 0; Index < TessellationBaseData->m_uiCoordSize; Index += 3)
			{
				BodyMesh.VertexArray.Emplace(Coordinates[Index], Coordinates[Index + 1], Coordinates[Index + 2]);
			}
		}
		else
		{
			// No vertex, no mesh...
			return;
		}
	}

	TUniqueTSObj<A3DTess3DData> Tessellation3DData(TessellationPtr);
	if (Tessellation3DData.IsValid())
	{
		for (unsigned int Index = 0; Index < Tessellation3DData->m_uiFaceTessSize; ++Index)
		{
			const A3DTessFaceData& FaceTessData = Tessellation3DData->m_psFaceTessData[Index];
			FTessellationData& Tessellation = BodyMesh.Faces.Emplace_GetRef();

			if (FaceTessData.m_uiStyleIndexesSize == 1)
			{
				A3DUns32 StyleIndex = FaceTessData.m_puiStyleIndexes[0];
				FCADUUID ColorName;
				FCADUUID MaterialName;
				ExtractGraphStyleProperties(StyleIndex, ColorName, MaterialName);
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

			uint32 TriangleCount = TechSoftFileParserImpl::CountTriangles(FaceTessData);
			TechSoftFileParserImpl::Reserve(Tessellation, TriangleCount, /*bWithTexture*/ FaceTessData.m_uiTextureCoordIndexesSize > 0);

			uint32 UsedEntitiesFlags = FaceTessData.m_usUsedEntitiesFlags;
			uint32 LastTrianguleIndex = FaceTessData.m_uiStartTriangulated;

			uint32 FaceSetIndex = 0;
			int32 LastVertexIndex = 0;

			if (UsedEntitiesFlags & TessellationFaceDataWithTriangle)
			{
				bool bTessellationWithOneNormal = (UsedEntitiesFlags & TessellationFaceDataWithOneNormal);
				if (bTessellationWithOneNormal)
				{
					if (FaceTessData.m_uiTextureCoordIndexesSize)
					{
						TechSoftFileParserImpl::AddFaceTriangleWithUniqueNormalAndTexture(Tessellation, *Tessellation3DData, FaceTessData.m_puiSizesTriangulated[0], FaceTessData.m_uiTextureCoordIndexesSize, LastTrianguleIndex, LastVertexIndex);
					}
					else
					{
						TechSoftFileParserImpl::AddFaceTriangleWithUniqueNormal(Tessellation, *Tessellation3DData, FaceTessData.m_puiSizesTriangulated[0], LastTrianguleIndex, LastVertexIndex);
					}
				}
				else
				{
					if (FaceTessData.m_uiTextureCoordIndexesSize)
					{
						TechSoftFileParserImpl::AddFaceTriangleWithTexture(Tessellation, *Tessellation3DData, FaceTessData.m_puiSizesTriangulated[0], FaceTessData.m_uiTextureCoordIndexesSize, LastTrianguleIndex, LastVertexIndex);
					}
					else
					{
						TechSoftFileParserImpl::AddFaceTriangle(Tessellation, *Tessellation3DData, FaceTessData.m_puiSizesTriangulated[0], LastTrianguleIndex, LastVertexIndex);
					}
				}
				FaceSetIndex++;
			}

			if (FaceTessData.m_uiSizesTriangulatedSize > FaceSetIndex)
			{
				//if (UsedEntitiesFlags & TessellationFaceDataWithFan)
				//{
				//	unsigned int LastFanIndex = 1 + FaceSetIndex + FaceTessData.m_puiSizesTriangulated[FaceSetIndex];
				//	FaceSetIndex++;
				//	for (; FaceSetIndex < LastFanIndex; FaceSetIndex++)
				//	{
				//		bool bTessellationWithOneNormal = (UsedEntitiesFlags & TessellationFaceDataWithOneNormal) && (FaceTessData.m_puiSizesTriangulated[FaceSetIndex] & kA3DTessFaceDataNormalSingle);
				//		A3DUns32 FanSize = (FaceTessData.m_puiSizesTriangulated[FaceSetIndex] & kA3DTessFaceDataNormalMask);
				//		if (bTessellationWithOneNormal)
				//		{
				//			if (FaceTessData.m_uiTextureCoordIndexesSize)
				//			{
				//				TechSoftFileParserImpl::AddFaceTriangleFanWithUniqueNormalAndTexture(Tessellation, *Tessellation3DData, FaceTessData.m_puiSizesTriangulated[FaceSetIndex], FaceTessData.m_uiTextureCoordIndexesSize, LastTrianguleIndex, LastVertexIndex);
				//			}
				//			else
				//			{
				//				TechSoftFileParserImpl::AddFaceTriangleFanWithUniqueNormal(Tessellation, *Tessellation3DData, FaceTessData.m_puiSizesTriangulated[FaceSetIndex], LastTrianguleIndex, LastVertexIndex);
				//			}
				//		}
				//		else
				//		{
				//			if (FaceTessData.m_uiTextureCoordIndexesSize)
				//			{
				//				TechSoftFileParserImpl::AddFaceTriangleFanWithTexture(Tessellation, *Tessellation3DData, FaceTessData.m_puiSizesTriangulated[FaceSetIndex], FaceTessData.m_uiTextureCoordIndexesSize, LastTrianguleIndex, LastVertexIndex);
				//			}
				//			else
				//			{
				//				TechSoftFileParserImpl::AddFaceTriangleFan(Tessellation, *Tessellation3DData, FaceTessData.m_puiSizesTriangulated[FaceSetIndex], LastTrianguleIndex, LastVertexIndex);
				//			}
				//		}
				//	}
				//}

				if (UsedEntitiesFlags & kA3DTessFaceDataTriangleFan)
				{
					uint32 FanCount = FaceTessData.m_puiSizesTriangulated[FaceSetIndex++];
					for (uint32 FanIndex = 0; FanIndex < FanCount; ++FanIndex)
					{
						uint32 VertexCount = FaceTessData.m_puiSizesTriangulated[FaceSetIndex++];
						TechSoftFileParserImpl::AddFaceTriangleFan(Tessellation, *Tessellation3DData, VertexCount, LastTrianguleIndex, LastVertexIndex);
					}
				}

				if (UsedEntitiesFlags & kA3DTessFaceDataTriangleFanOneNormal)
				{
					uint32 FanCount = FaceTessData.m_puiSizesTriangulated[FaceSetIndex++] & kA3DTessFaceDataNormalMask;
					for (uint32 FanIndex = 0; FanIndex < FanCount; ++FanIndex)
					{
						ensure((FaceTessData.m_puiSizesTriangulated[FaceSetIndex] & kA3DTessFaceDataNormalSingle) != 0);

						uint32 VertexCount = FaceTessData.m_puiSizesTriangulated[FaceSetIndex++] & kA3DTessFaceDataNormalMask;
						TechSoftFileParserImpl::AddFaceTriangleFanWithUniqueNormal(Tessellation, *Tessellation3DData, VertexCount, LastTrianguleIndex, LastVertexIndex);
					}
				}

				if (UsedEntitiesFlags & kA3DTessFaceDataTriangleFanTextured)
				{
					uint32 FanCount = FaceTessData.m_puiSizesTriangulated[FaceSetIndex++];
					for (uint32 FanIndex = 0; FanIndex < FanCount; ++FanIndex)
					{
						uint32 VertexCount = FaceTessData.m_puiSizesTriangulated[FaceSetIndex++];
						TechSoftFileParserImpl::AddFaceTriangleFanWithTexture(Tessellation, *Tessellation3DData, VertexCount, FaceTessData.m_uiTextureCoordIndexesSize, LastTrianguleIndex, LastVertexIndex);
					}
				}

				if (UsedEntitiesFlags & kA3DTessFaceDataTriangleFanOneNormalTextured)
				{
					uint32 FanCount = FaceTessData.m_puiSizesTriangulated[FaceSetIndex++] & kA3DTessFaceDataNormalMask;
					for (uint32 FanIndex = 0; FanIndex < FanCount; ++FanIndex)
					{
						ensure((FaceTessData.m_puiSizesTriangulated[FaceSetIndex] & kA3DTessFaceDataNormalSingle) != 0);

						uint32 VertexCount = FaceTessData.m_puiSizesTriangulated[FaceSetIndex++] & kA3DTessFaceDataNormalMask;
						TechSoftFileParserImpl::AddFaceTriangleFanWithUniqueNormalAndTexture(Tessellation, *Tessellation3DData, VertexCount, FaceTessData.m_uiTextureCoordIndexesSize, LastTrianguleIndex, LastVertexIndex);
					}
				}
			}

			if (FaceTessData.m_uiSizesTriangulatedSize > FaceSetIndex)
			{
				FaceSetIndex++;
				for (; FaceSetIndex < FaceTessData.m_uiSizesTriangulatedSize; FaceSetIndex++)
				{
					bool bTessellationWithOneNormal = (UsedEntitiesFlags & TessellationFaceDataWithOneNormal) && (FaceTessData.m_puiSizesTriangulated[FaceSetIndex] & kA3DTessFaceDataNormalSingle);
					A3DUns32 StripSize = (FaceTessData.m_puiSizesTriangulated[FaceSetIndex] & kA3DTessFaceDataNormalMask);
					if (bTessellationWithOneNormal)
					{
						if (FaceTessData.m_uiTextureCoordIndexesSize)
						{
							TechSoftFileParserImpl::AddFaceTriangleStripWithUniqueNormalAndTexture(Tessellation, *Tessellation3DData, FaceTessData.m_puiSizesTriangulated[FaceSetIndex], FaceTessData.m_uiTextureCoordIndexesSize, LastTrianguleIndex, LastVertexIndex);
						}
						else
						{
							TechSoftFileParserImpl::AddFaceTriangleStripWithUniqueNormal(Tessellation, *Tessellation3DData, FaceTessData.m_puiSizesTriangulated[FaceSetIndex], LastTrianguleIndex, LastVertexIndex);
						}
					}
					else
					{
						if (FaceTessData.m_uiTextureCoordIndexesSize)
						{
							TechSoftFileParserImpl::AddFaceTriangleStripWithTexture(Tessellation, *Tessellation3DData, FaceTessData.m_puiSizesTriangulated[FaceSetIndex], FaceTessData.m_uiTextureCoordIndexesSize, LastTrianguleIndex, LastVertexIndex);
						}
						else
						{
							TechSoftFileParserImpl::AddFaceTriangleStrip(Tessellation, *Tessellation3DData, FaceTessData.m_puiSizesTriangulated[FaceSetIndex], LastTrianguleIndex, LastVertexIndex);
						}
					}
				}
			}

		}
	}

	Body.ColorFaceSet = BodyMesh.ColorSet;
	Body.MaterialFaceSet = BodyMesh.MaterialSet;
}
#pragma optimize ("",on)

} // ns CADLibrary

#endif  