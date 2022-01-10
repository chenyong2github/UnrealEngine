// Copyright Epic Games, Inc. All Rights Reserved.

#include "TechSoftFileParser.h"

#ifdef USE_TECHSOFT_SDK

#include "TechSoftInterface.h"
#include "TUniqueTechSoftObj.h"

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
		FString NewName = Name.Right(Index + 1);
		if (Name.FindLastChar(TEXT(')'), Index))
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
		FString NewName = Name.Left(Position) + TEXT("<") + Name.Right(Position + 1) + TEXT(">");
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
		FString Indice = Name.Right(Position + 1);
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
		FString Extension = Name.Right(Position);
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
bool IsUnloadedPrototype(const A3DAsmProductOccurrenceData& PrototypeData)
{
	bool bIsUnloaded = true;

	if (PrototypeData.m_uiPOccurrencesSize > 0)
	{
		bIsUnloaded = false;
	}
	else if (PrototypeData.m_uiEntityReferenceSize > 0)
	{
		bIsUnloaded = false;
	}
	else if (PrototypeData.m_pPart)
	{
		bIsUnloaded = false;
	}
	else if (PrototypeData.m_pPrototype)
	{
		TUniqueTSObj<A3DAsmProductOccurrenceData> SubPrototypeData(PrototypeData.m_pPrototype);
		if (SubPrototypeData.IsValid())
		{
			bIsUnloaded = TechSoftFileParserImpl::IsUnloadedPrototype(*SubPrototypeData);
		}
	}

	return bIsUnloaded;
}

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

	Importer.m_sLoadData.m_sGeneral.m_eReadGeomTessMode = kA3DReadGeomOnly;
	Importer.m_sLoadData.m_sGeneral.m_eDefaultUnit;
	Importer.m_sLoadData.m_sGeneral.m_bReadFeature = A3D_FALSE;

	Importer.m_sLoadData.m_sGeneral.m_bReadConstraints = A3D_FALSE;
	Importer.m_sLoadData.m_sGeneral.m_iNbMultiProcess = 1;

	Importer.m_sLoadData.m_sIncremental.m_bLoadNoDependencies = true;
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
}

FTechSoftFileParser::FTechSoftFileParser(FCADFileData& InCADData, const FString& EnginePluginsPath)
	: CADFileData(InCADData)
	, TechSoftInterface(GetTechSoftInterface())
{
}

ECADParsingResult FTechSoftFileParser::Process()
{
	// Process the file
	ECADParsingResult Result = ECADParsingResult::ProcessOk;

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

	return TraverseModel(TechSoftInterface.GetModelFile());
}

void FTechSoftFileParser::ReserveCADFileData()
{
	CountUnderModel(TechSoftInterface.GetModelFile());

	CADFileData.ReserveBodyMeshes(ComponentCount[EComponentType::Body]);

	FArchiveSceneGraph& SceneGraphArchive = CADFileData.GetSceneGraphArchive();
	SceneGraphArchive.Bodies.Reserve(ComponentCount[EComponentType::Body]);
	SceneGraphArchive.Components.Reserve(ComponentCount[EComponentType::Reference]);
	SceneGraphArchive.UnloadedComponents.Reserve(ComponentCount[EComponentType::Reference]);
	SceneGraphArchive.ExternalReferences.Reserve(ComponentCount[EComponentType::Reference]);
	SceneGraphArchive.Instances.Reserve(ComponentCount[EComponentType::Occurrence]);

	SceneGraphArchive.CADIdToBodyIndex.Reserve(ComponentCount[EComponentType::Body]);
	SceneGraphArchive.CADIdToComponentIndex.Reserve(ComponentCount[EComponentType::Reference]);
	SceneGraphArchive.CADIdToUnloadedComponentIndex.Reserve(ComponentCount[EComponentType::Reference]);
	SceneGraphArchive.CADIdToInstanceIndex.Reserve(ComponentCount[EComponentType::Occurrence]);

	uint32 MaterialNum = CountMaterial() + CountColor();
	SceneGraphArchive.MaterialHIdToMaterial.Reserve(MaterialNum);
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
	TraverseMetaData(ModelFile, MetaData);
	TraverseSpecificMetaData(ModelFile, MetaData);

	for (uint32 Index = 0; Index < ModelFileData->m_uiPOccurrencesSize; ++Index)
	{
		if (IsConfigurationSet(ModelFileData->m_ppPOccurrences[Index]))
		{
			//TraverseConfigurationSet(ModelFileData->m_ppPOccurrences[Index], MetaData);
		}
		else
		{
			TraverseReference(ModelFileData->m_ppPOccurrences[Index]);
		}
	}

	return ECADParsingResult::ProcessOk;
}

void FTechSoftFileParser::TraverseReference(const A3DAsmProductOccurrence* ReferencePtr)
{
	FEntityMetaData MetaData;
	TraverseMetaData(ReferencePtr, MetaData);

	if (MetaData.bRemoved || !MetaData.bShow)
	{
		return;
	}

	TraverseSpecificMetaData(ReferencePtr, MetaData);
	BuildReferenceName(MetaData.MetaData);

	TraverseMaterialProperties(ReferencePtr);

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
		if (FString* ConfigurationName = ComponentMetaData.MetaData.Find(TEXT("ConfigurationName")))
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
	FEntityMetaData InstanceMetaData;
	TraverseMetaData(OccurrencePtr, InstanceMetaData);

	if (InstanceMetaData.bRemoved || !InstanceMetaData.bShow)
	{
		return 0;
	}

	TraverseSpecificMetaData(OccurrencePtr, InstanceMetaData);
	BuildInstanceName(InstanceMetaData.MetaData);

	TraverseMaterialProperties(OccurrencePtr);
	TraverseLayer(OccurrencePtr);

	TUniqueTSObj<A3DAsmProductOccurrenceData> OccurrenceData(OccurrencePtr);
	if (!OccurrenceData.IsValid())
	{
		return 0;
	}

	FArchiveInstance& Instance = AddInstance(InstanceMetaData);

	Instance.TransformMatrix = TraverseTransformation(OccurrenceData->m_pLocation);

	FEntityMetaData PrototypeMetaData;
	bool bIsUnloadedPrototype = false;
	if (OccurrenceData->m_pPrototype)
	{
		FMatrix PrototypeMatrix;
		TraversePrototype(OccurrenceData->m_pPrototype, PrototypeMetaData, PrototypeMatrix);
		Instance.TransformMatrix *= PrototypeMatrix;
	}

	if (PrototypeMetaData.bUnloaded)
	{
		FArchiveUnloadedComponent& Prototype = AddUnloadedComponent(PrototypeMetaData, Instance);
	}
	else
	{
		FArchiveComponent& Component = AddComponent(PrototypeMetaData, Instance);

		for (uint32 Index = 0; Index < OccurrenceData->m_uiPOccurrencesSize; ++Index)
		{
			int32 ChildrenId = TraverseOccurrence(OccurrenceData->m_ppPOccurrences[Index]);
			Component.Children.Add(ChildrenId);
		}

		if (OccurrenceData->m_pPart)
		{
			TraversePartDefinition(OccurrenceData->m_pPart, Component);
		}
	}

	return Instance.ObjectId;
}

FFileDescriptor FTechSoftFileParser::GetOccurrenceFileName(const A3DAsmProductOccurrence* OccurrencePtr)
{
	TUniqueTSObj<A3DAsmProductOccurrenceData> OccurrenceData(OccurrencePtr);
	if (!OccurrenceData.IsValid())
	{
		return FFileDescriptor();
	}

	if (OccurrenceData->m_pPrototype)
	{
		return GetOccurrenceFileName(OccurrenceData->m_pPrototype);
	}

	A3DUTF8Char* FilePathUTF8Ptr = nullptr;
	int Ret = A3DAsmProductOccurrenceGetFilePathName(OccurrencePtr, &FilePathUTF8Ptr);
	if (Ret == A3D_SUCCESS && FilePathUTF8Ptr)
	{
		FString OccurrenceFilePath = UTF8_TO_TCHAR(FilePathUTF8Ptr);
		A3DAsmProductOccurrenceGetFilePathName(NULL, &FilePathUTF8Ptr);
		FString OccurrenceFileName = FPaths::GetCleanFilename(OccurrenceFilePath);
		if (OccurrenceFileName != CADFileData.GetCADFileDescription().GetFileName())
		{
			return FFileDescriptor(*OccurrenceFilePath, nullptr, *CADFileData.GetCADFileDescription().GetRootFolder());
		}
	}

	FilePathUTF8Ptr = nullptr;
	Ret = A3DAsmProductOccurrenceGetOriginalFilePathName(OccurrencePtr, &FilePathUTF8Ptr);
	if (Ret == A3D_SUCCESS && FilePathUTF8Ptr)
	{
		FString OriginalFilePath = UTF8_TO_TCHAR(FilePathUTF8Ptr);
		A3DAsmProductOccurrenceGetFilePathName(NULL, &FilePathUTF8Ptr);
		FString OriginalFileName = FPaths::GetCleanFilename(OriginalFilePath);
		if (OriginalFileName != CADFileData.GetCADFileDescription().GetFileName())
		{
			return FFileDescriptor(*OriginalFilePath, nullptr, *CADFileData.GetCADFileDescription().GetRootFolder());
		}
	}

	return FFileDescriptor();
}

void FTechSoftFileParser::TraversePrototype(const A3DAsmProductOccurrence* InPrototypePtr, FEntityMetaData& OutPrototypeMetaData, FMatrix& OutPrototypeMatrix)
{
	TUniqueTSObj<A3DAsmProductOccurrenceData> PrototypeData(InPrototypePtr);
	if (!PrototypeData.IsValid())
	{
		return;
	}

	TraverseMetaData(InPrototypePtr, OutPrototypeMetaData);
	TraverseSpecificMetaData(InPrototypePtr, OutPrototypeMetaData);

	TraverseMaterialProperties(InPrototypePtr);

	OutPrototypeMatrix = TraverseTransformation(PrototypeData->m_pLocation);

	OutPrototypeMetaData.bUnloaded = TechSoftFileParserImpl::IsUnloadedPrototype(*PrototypeData);
	if (OutPrototypeMetaData.bUnloaded)
	{
		OutPrototypeMetaData.ExternalFile = GetOccurrenceFileName(InPrototypePtr);
		OutPrototypeMetaData.MetaData.Add(TEXT("Name"), OutPrototypeMetaData.ExternalFile.GetFileName());
	}

	BuildReferenceName(OutPrototypeMetaData.MetaData);
}

void FTechSoftFileParser::TraversePartDefinition(const A3DAsmPartDefinition* PartDefinitionPtr, FArchiveComponent& Part)
{
	FEntityMetaData PartMetaData;
	TraverseMetaData(PartDefinitionPtr, PartMetaData);

	if (PartMetaData.bRemoved || !PartMetaData.bShow)
	{
		return;
	}

	TraverseSpecificMetaData(PartDefinitionPtr, PartMetaData);
	BuildPartName(PartMetaData.MetaData);

	TraverseMaterialProperties(PartDefinitionPtr);
	TraverseLayer(PartDefinitionPtr);

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

FCadId FTechSoftFileParser::TraverseRepresentationSet(const A3DRiSet* RepresentationSetPtr, FEntityMetaData& PartMetaData)
{
	TUniqueTSObj<A3DRiSetData> RepresentationSetData(RepresentationSetPtr);
	if (!RepresentationSetData.IsValid())
	{
		return 0;
	}

	FEntityMetaData RepresentationSetMetaData;
	TraverseMetaData(RepresentationSetPtr, RepresentationSetMetaData);

	if (RepresentationSetMetaData.bRemoved || !RepresentationSetMetaData.bShow)
	{
		return 0;
	}

	TraverseMaterialProperties(RepresentationSetPtr);

	FCadId RepresentationSetId = 0;
	FArchiveComponent& RepresentationSet = AddOccurence(RepresentationSetMetaData, RepresentationSetId);

	for (A3DUns32 ui = 0; ui < RepresentationSetData->m_uiRepItemsSize; ++ui)
	{
		int32 ChildId = TraverseRepresentationItem(RepresentationSetData->m_ppRepItems[ui], RepresentationSetMetaData);
		RepresentationSet.Children.Add(ChildId);
	}
	return RepresentationSetId;
}

FCadId FTechSoftFileParser::TraverseBRepModel(A3DRiBrepModel* BRepModelPtr, FEntityMetaData& PartMetaData)
{
	TUniqueTSObj<A3DRiBrepModelData> BodyData(BRepModelPtr);
	if (!BodyData.IsValid())
	{
		return 0;
	}

	FEntityMetaData BRepMetaData;
	TraverseMetaData(BRepModelPtr, BRepMetaData);
	if (!BRepMetaData.bShow || BRepMetaData.bRemoved)
	{
		return 0;
	}

	TraverseSpecificMetaData(BRepModelPtr, BRepMetaData);
	TraverseMaterialProperties(BRepModelPtr);

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
	TUniqueTSObj<A3DRiPolyBrepModelData> BodyData(PolygonalPtr);
	if (!BodyData.IsValid())
	{
		return 0;
	}

	FEntityMetaData BRepMetaData;
	TraverseMetaData(PolygonalPtr, BRepMetaData);
	if (!BRepMetaData.bShow || BRepMetaData.bRemoved)
	{
		return 0;
	}

	TraverseSpecificMetaData(PolygonalPtr, BRepMetaData);
	TraverseMaterialProperties(PolygonalPtr);

	FArchiveBody& Body = AddBody(BRepMetaData);
	TraverseRepresentationContent(PolygonalPtr, Body);

	return Body.ObjectId;
}

void FTechSoftFileParser::TraverseMetaData(const A3DEntity* Entity, FEntityMetaData& OutMetaData)
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
				TraverseGraphics(MetaDataWithGraphics->m_pGraphics, OutMetaData);
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

void FTechSoftFileParser::TraverseSpecificMetaData(const A3DAsmProductOccurrence* Occurrence, FEntityMetaData& OutMetaData)
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

void FTechSoftFileParser::TraverseGraphics(const A3DGraphics* Graphics, FEntityMetaData& OutMetaData)
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

	TraverseGraphStyleData(GraphicsData->m_uiStyleIndex, ColorName, MaterialName);

	if (ColorName)
	{
		OutMetaData.MetaData.Add(TEXT("ColorName"), FString::Printf(TEXT("%u"), ColorName));
	}

	if (MaterialName)
	{
		OutMetaData.MetaData.Add(TEXT("MaterialName"), FString::Printf(TEXT("%u"), MaterialName));
	}
}

void FTechSoftFileParser::TraverseGraphStyleData(uint32 StyleIndex, FCADUUID& OutColorName, FCADUUID& OutMaterialName)
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

void FTechSoftFileParser::TraverseMaterialProperties(const A3DEntity* Entity)
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

FMatrix FTechSoftFileParser::TraverseTransformation3D(const A3DMiscCartesianTransformation* CartesianTransformation)
{
	if (CartesianTransformation)
	{
		TUniqueTSObj<A3DMiscCartesianTransformationData> CartesianTransformationData(CartesianTransformation);

		if (CartesianTransformationData.IsValid())
		{
			FVector Origin(CartesianTransformationData->m_sOrigin.m_dX, CartesianTransformationData->m_sOrigin.m_dY, CartesianTransformationData->m_sOrigin.m_dZ);
			FVector XVector(CartesianTransformationData->m_sXVector.m_dX, CartesianTransformationData->m_sXVector.m_dY, CartesianTransformationData->m_sXVector.m_dZ);;
			FVector YVector(CartesianTransformationData->m_sYVector.m_dX, CartesianTransformationData->m_sYVector.m_dY, CartesianTransformationData->m_sYVector.m_dZ);;

			FVector ZVector = XVector ^ YVector;
			FMatrix Matrix(XVector, YVector, ZVector, FVector::Zero());

			FMatrix Scale = FMatrix::Identity;
			Scale.M[0][0] = CartesianTransformationData->m_sScale.m_dX;
			Scale.M[1][1] = CartesianTransformationData->m_sScale.m_dY;
			Scale.M[2][2] = CartesianTransformationData->m_sScale.m_dZ;
			Matrix = Matrix * Scale;

			Matrix = Matrix.ConcatTranslation(Origin);

			return Matrix;
		}
	}
	return FMatrix::Identity;
}

FMatrix FTechSoftFileParser::TraverseGeneralTransformation(const A3DMiscGeneralTransformation* GeneralTransformation)
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
		return Matrix;
	}
	return FMatrix::Identity;
}

FMatrix FTechSoftFileParser::TraverseTransformation(const A3DMiscCartesianTransformation* Transformation3D)
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

void FTechSoftFileParser::TraverseLayer(const A3DAsmProductOccurrence* Occurrence)
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
		//if (IsConfigurationSet(ModelFileData.m_ppPOccurrences[Index]))
		//{
		//	TraverseConfigurationSet(ModelFileData.m_ppPOccurrences[Index], MetaData);
		//}
		//else
		{
			CountUnderOccurrence(ModelFileData->m_ppPOccurrences[Index]);
		}
	}
}

void FTechSoftFileParser::CountUnderOccurrence(const A3DAsmProductOccurrence* Occurrence)
{
	TUniqueTSObj<A3DAsmProductOccurrenceData> OccurrenceData(Occurrence);
	if (!OccurrenceData.IsValid())
	{
		return;
	}

	ComponentCount[EComponentType::Occurrence] ++;

	if (OccurrenceData->m_pPrototype)
	{
		CountUnderPrototype(OccurrenceData->m_pPrototype);
	}

	for (uint32 Index = 0; Index < OccurrenceData->m_uiPOccurrencesSize; ++Index)
	{
		CountUnderOccurrence(OccurrenceData->m_ppPOccurrences[Index]);
	}

	if (OccurrenceData->m_pPart)
	{
		CountUnderPartDefinition(OccurrenceData->m_pPart);
	}
}

void FTechSoftFileParser::CountUnderPrototype(const A3DAsmProductOccurrence* Prototype)
{
	if (PrototypeCounted.Contains(Prototype))
	{
		return;
	}
	PrototypeCounted.Add(Prototype);

	TUniqueTSObj<A3DAsmProductOccurrenceData> PrototypeData(Prototype);
	if (!PrototypeData.IsValid())
	{
		return;
	}

	ComponentCount[EComponentType::Reference] ++;

	if (PrototypeData->m_pPrototype)
	{
		CountUnderPrototype(PrototypeData->m_pPrototype);
	}
}

void FTechSoftFileParser::CountUnderPartDefinition(const A3DAsmPartDefinition* PartDefinition)
{
	TUniqueTSObj<A3DAsmPartDefinitionData> PartData(PartDefinition);
	if (!PartData.IsValid())
	{
		return;
	}

	for (unsigned int Index = 0; Index < PartData->m_uiRepItemsSize; ++Index)
	{
		CountUnderRepresentation(PartData->m_ppRepItems[Index]);
	}

}

void FTechSoftFileParser::CountUnderRepresentation(const A3DRiRepresentationItem* RepresentationItem)
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

void FTechSoftFileParser::CountUnderRepresentationSet(const A3DRiSet* RepresentationSet)
{
	TUniqueTSObj<A3DRiSetData> RepresentationSetData(RepresentationSet);
	if (!RepresentationSetData.IsValid())
	{
		return;
	}
	ComponentCount[EComponentType::Body] += RepresentationSetData->m_uiRepItemsSize;
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

void AddFaceTriangleWithUniqueNormal(FTessellationData& Tessellation, const A3DTess3DData& Tessellation3DData, long& InOutStartIndex, unsigned long InTrinangleCount)
{
	TechSoftFileParserImpl::Reserve(Tessellation, InTrinangleCount, /*bWithTexture*/ false);

	unsigned long Index = InOutStartIndex;

	int32 FaceIndex[3] = { 0, 0, 0 };
	int32 NormalIndex[3] = { 0, 0, 0 };
	int32 VertexIndex = 0;

	NormalIndex[0] = Tessellation3DData.m_puiTriangulatedIndexes[Index++];
	NormalIndex[1] = NormalIndex[0];
	NormalIndex[2] = NormalIndex[0];

	// Get Triangles
	for (unsigned long TriangleIndex = 0; TriangleIndex < InTrinangleCount; TriangleIndex++)
	{
		FaceIndex[0] = Tessellation3DData.m_puiTriangulatedIndexes[Index++];
		FaceIndex[1] = Tessellation3DData.m_puiTriangulatedIndexes[Index++];
		FaceIndex[2] = Tessellation3DData.m_puiTriangulatedIndexes[Index++];

		if (!TechSoftFileParserImpl::AddFace(FaceIndex, Tessellation, VertexIndex))
		{
			continue;
		}

		TechSoftFileParserImpl::AddNormals(Tessellation3DData.m_pdNormals, NormalIndex, Tessellation.NormalArray);
	}

	InOutStartIndex = Index;
}

void AddFaceTriangleWithUniqueNormalAndTexture(FTessellationData& Tessellation, const A3DTess3DData& Tessellation3DData, long& InOutStartIndex, unsigned long InTrinangleCount, long TextureCount)
{
	TechSoftFileParserImpl::Reserve(Tessellation, InTrinangleCount, /*bWithTexture*/ true);

	unsigned long Index = InOutStartIndex;

	int32 FaceIndex[3] = { 0, 0, 0 };
	int32 NormalIndex[3] = { 0, 0, 0 };
	int32 TextureIndex[3] = { 0, 0, 0 };
	int32 VertexIndex = 0;

	NormalIndex[0] = Tessellation3DData.m_puiTriangulatedIndexes[Index++];
	NormalIndex[1] = NormalIndex[0];
	NormalIndex[2] = NormalIndex[0];

	// Get Triangles
	for (unsigned long TriangleIndex = 0; TriangleIndex < InTrinangleCount; TriangleIndex++)
	{
		TextureIndex[0] = Tessellation3DData.m_puiTriangulatedIndexes[Index];
		Index += TextureCount;
		FaceIndex[0] = Tessellation3DData.m_puiTriangulatedIndexes[Index++];
		TextureIndex[1] = Tessellation3DData.m_puiTriangulatedIndexes[Index];
		Index += TextureCount;
		FaceIndex[1] = Tessellation3DData.m_puiTriangulatedIndexes[Index++];
		TextureIndex[2] = Tessellation3DData.m_puiTriangulatedIndexes[Index];
		Index += TextureCount;
		FaceIndex[2] = Tessellation3DData.m_puiTriangulatedIndexes[Index++];

		if (!TechSoftFileParserImpl::AddFace(FaceIndex, Tessellation, VertexIndex))
		{
			continue;
		}

		TechSoftFileParserImpl::AddNormals(Tessellation3DData.m_pdNormals, NormalIndex, Tessellation.NormalArray);
		TechSoftFileParserImpl::AddTextureCoordinates(Tessellation3DData.m_pdTextureCoords, TextureIndex, Tessellation.TexCoordArray);
	}

	InOutStartIndex = Index;
}

void AddFaceTriangle(FTessellationData& Tessellation, const A3DTess3DData& Tessellation3DData, long& InOutStartIndex, unsigned long InTrinangleCount)
{
	TechSoftFileParserImpl::Reserve(Tessellation, InTrinangleCount, /*bWithTexture*/ false);

	unsigned long Index = InOutStartIndex;

	int32 FaceIndex[3] = { 0, 0, 0 };
	int32 NormalIndex[3] = { 0, 0, 0 };
	int32 VertexIndex = 0;

	// Get Triangles
	for (unsigned long TriangleIndex = 0; TriangleIndex < InTrinangleCount; TriangleIndex++)
	{
		NormalIndex[0] = Tessellation3DData.m_puiTriangulatedIndexes[Index++];
		FaceIndex[0] = Tessellation3DData.m_puiTriangulatedIndexes[Index++];
		NormalIndex[1] = Tessellation3DData.m_puiTriangulatedIndexes[Index++];
		FaceIndex[1] = Tessellation3DData.m_puiTriangulatedIndexes[Index++];
		NormalIndex[2] = Tessellation3DData.m_puiTriangulatedIndexes[Index++];
		FaceIndex[2] = Tessellation3DData.m_puiTriangulatedIndexes[Index++];

		if (!TechSoftFileParserImpl::AddFace(FaceIndex, Tessellation, VertexIndex))
		{
			continue;
		}

		TechSoftFileParserImpl::AddNormals(Tessellation3DData.m_pdNormals, NormalIndex, Tessellation.NormalArray);
	}

	InOutStartIndex = Index;
}

void AddFaceTriangleWithTexture(FTessellationData& Tessellation, const A3DTess3DData& Tessellation3DData, long& InOutStartIndex, unsigned long InTrinangleCount, long TextureCount)
{
	TechSoftFileParserImpl::Reserve(Tessellation, InTrinangleCount, /*bWithTexture*/ true);

	unsigned long Index = InOutStartIndex;

	int32 FaceIndex[3] = { 0, 0, 0 };
	int32 NormalIndex[3] = { 0, 0, 0 };
	int32 TextureIndex[3] = { 0, 0, 0 };
	int32 VertexIndex = 0;

	// Get Triangles
	for (unsigned long TriangleIndex = 0; TriangleIndex < InTrinangleCount; TriangleIndex++)
	{
		NormalIndex[0] = Tessellation3DData.m_puiTriangulatedIndexes[Index++];
		TextureIndex[0] = Tessellation3DData.m_puiTriangulatedIndexes[Index];
		Index += TextureCount;
		FaceIndex[0] = Tessellation3DData.m_puiTriangulatedIndexes[Index++];
		NormalIndex[1] = Tessellation3DData.m_puiTriangulatedIndexes[Index++];
		TextureIndex[1] = Tessellation3DData.m_puiTriangulatedIndexes[Index];
		Index += TextureCount;
		FaceIndex[1] = Tessellation3DData.m_puiTriangulatedIndexes[Index++];
		NormalIndex[2] = Tessellation3DData.m_puiTriangulatedIndexes[Index++];
		TextureIndex[2] = Tessellation3DData.m_puiTriangulatedIndexes[Index];
		Index += TextureCount;
		FaceIndex[2] = Tessellation3DData.m_puiTriangulatedIndexes[Index++];

		if (!TechSoftFileParserImpl::AddFace(FaceIndex, Tessellation, VertexIndex))
		{
			continue;
		}

		TechSoftFileParserImpl::AddNormals(Tessellation3DData.m_pdNormals, NormalIndex, Tessellation.NormalArray);
		TechSoftFileParserImpl::AddTextureCoordinates(Tessellation3DData.m_pdTextureCoords, TextureIndex, Tessellation.TexCoordArray);
	}

	InOutStartIndex = Index;
}

// TODO import fan and strip meshes
#ifdef UNUSED
double ComputeFaceAreaTriangleFan(double* InCoordinates, unsigned long* InTriangulatedIndexes, long& InOutStartIndex, unsigned long InTrinangleCount, bool bInOneNormal, long TextureCount, int& TriangleCount)
{
	double Area = 0;

	unsigned long OffSet = TextureCount + (bInOneNormal ? 0 : 1) + 1;
	unsigned long Index = InOutStartIndex + 2 + TextureCount;

	int Index0 = InTriangulatedIndexes[Index];
	double* Point0 = InCoordinates + (InTriangulatedIndexes[Index]);
	Index += OffSet;
	int Index1 = InTriangulatedIndexes[Index];
	double* Point1 = InCoordinates + (InTriangulatedIndexes[Index]);
	Index += OffSet;

	for (unsigned long TriangleIndex = 2; TriangleIndex < InTrinangleCount; TriangleIndex++)
	{
		double* Point2 = InCoordinates + (InTriangulatedIndexes[Index]);
		int Index2 = InTriangulatedIndexes[Index];
		if (Index0 == Index1 || Index0 == Index2 || Index1 == Index2)
		{
			TriangleCount--;
		}
		else
		{
			Area += FaceArea(Point0, Point1, Point2);
		}
		Index += OffSet;
		Point1 = Point2;
		Index1 = Index2;
	}
	InOutStartIndex = Index - OffSet;

	return Area;
}

double ComputeFaceAreaTriangleStrip(double* InCoordinates, unsigned long* InTriangulatedIndexes, long& InOutStartIndex, unsigned long InTrinangleCount, bool bInOneNormal, long TextureCount, int& TriangleCount)
{
	double Area = 0;

	unsigned long OffSet = TextureCount + (bInOneNormal ? 0 : 1) + 1;
	unsigned long Index = InOutStartIndex + 2 + TextureCount;

	int Index0 = InTriangulatedIndexes[Index];
	double* Point0 = InCoordinates + (InTriangulatedIndexes[Index]);
	Index += OffSet;
	int Index1 = InTriangulatedIndexes[Index];
	double* Point1 = InCoordinates + (InTriangulatedIndexes[Index]);
	Index += OffSet;

	for (unsigned long TriangleIndex = 2; TriangleIndex < InTrinangleCount; TriangleIndex++)
	{
		double* Point2 = InCoordinates + (InTriangulatedIndexes[Index]);
		int Index2 = InTriangulatedIndexes[Index];
		if (Index0 == Index1 || Index0 == Index2 || Index1 == Index2)
		{
			TriangleCount--;
		}
		else
		{
			Area += FaceArea(Point0, Point1, Point2);
		}

		Index += OffSet;
		Point0 = Point1;
		Point1 = Point2;
		Index0 = Index1;
		Index1 = Index2;
	}
	InOutStartIndex = Index - OffSet;

	return Area;
}
#endif

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
				TraverseGraphStyleData(StyleIndex, ColorName, MaterialName);
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

			unsigned int UsedEntitiesFlags = FaceTessData.m_usUsedEntitiesFlags;
			long StartTriangulated = FaceTessData.m_uiStartTriangulated;

			unsigned int FaceSetIndex = 0;
			// Triangles
			if (UsedEntitiesFlags & TessellationFaceDataWithTriangle)
			{
				bool bTessellationWithOneNormal = (UsedEntitiesFlags & TessellationFaceDataWithOneNormal);
				if (bTessellationWithOneNormal)
				{
					if (FaceTessData.m_uiTextureCoordIndexesSize)
					{
						AddFaceTriangleWithUniqueNormalAndTexture(Tessellation, *Tessellation3DData, StartTriangulated, FaceTessData.m_puiSizesTriangulated[0], FaceTessData.m_uiTextureCoordIndexesSize);
					}
					else
					{
						AddFaceTriangleWithUniqueNormal(Tessellation, *Tessellation3DData, StartTriangulated, FaceTessData.m_puiSizesTriangulated[0]);
					}
				}
				else
				{
					if (FaceTessData.m_uiTextureCoordIndexesSize)
					{
						AddFaceTriangleWithTexture(Tessellation, *Tessellation3DData, StartTriangulated, FaceTessData.m_puiSizesTriangulated[0], FaceTessData.m_uiTextureCoordIndexesSize);
					}
					else
					{
						AddFaceTriangle(Tessellation, *Tessellation3DData, StartTriangulated, FaceTessData.m_puiSizesTriangulated[0]);
					}
				}
				FaceSetIndex++;
			}

			// Fans TODO
			if (FaceTessData.m_uiSizesTriangulatedSize > FaceSetIndex)
			{
				if (UsedEntitiesFlags & TessellationFaceDataWithFan)
				{
					unsigned int LastFanIndex = 1 + FaceSetIndex + FaceTessData.m_puiSizesTriangulated[FaceSetIndex];
					FaceSetIndex++;
					for (; FaceSetIndex < LastFanIndex; FaceSetIndex++)
					{
						bool bTessellationWithOneNormal = (UsedEntitiesFlags & TessellationFaceDataWithOneNormal) && (FaceTessData.m_puiSizesTriangulated[FaceSetIndex] & kA3DTessFaceDataNormalSingle);
						A3DUns32 FanSize = (FaceTessData.m_puiSizesTriangulated[FaceSetIndex] & kA3DTessFaceDataNormalMask);
						//TriangleCount += (FanSize - 2);
						//BodyMeshMetaData.MeshArea += ComputeFaceAreaTriangleFan(TessellationBaseData.m_pdCoords, Tessellation3DData->m_puiTriangulatedIndexes, StartTriangulated, FanSize, bTessellationWithOneNormal, FaceTessData.m_uiTextureCoordIndexesSize, TriangleCount);
					}
				}
			}

			// Strip TODO
			if (FaceTessData.m_uiSizesTriangulatedSize > FaceSetIndex)
			{
				FaceSetIndex++;
				for (; FaceSetIndex < FaceTessData.m_uiSizesTriangulatedSize; FaceSetIndex++)
				{
					bool bTessellationWithOneNormal = (UsedEntitiesFlags & TessellationFaceDataWithOneNormal) && (FaceTessData.m_puiSizesTriangulated[FaceSetIndex] & kA3DTessFaceDataNormalSingle);
					A3DUns32 StripSize = (FaceTessData.m_puiSizesTriangulated[FaceSetIndex] & kA3DTessFaceDataNormalMask);
					//TriangleCount += (StripSize - 2);
					//BodyMeshMetaData.MeshArea += ComputeFaceAreaTriangleStrip(TessellationBaseData.m_pdCoords, Tessellation3DData->m_puiTriangulatedIndexes, StartTriangulated, StripSize, bTessellationWithOneNormal, FaceTessData.m_uiTextureCoordIndexesSize, TriangleCount);
				}
			}

		}
	}

	Body.ColorFaceSet = BodyMesh.ColorSet;
	Body.MaterialFaceSet = BodyMesh.MaterialSet;
}

} // ns

#endif