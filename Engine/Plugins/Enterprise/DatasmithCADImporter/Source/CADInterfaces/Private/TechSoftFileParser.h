// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "CADData.h"
#include "CADFileParser.h"
#include "TechSoftInterface.h"
#include "TUniqueTechSoftObj.h"

typedef void A3DAsmModelFile;
typedef void A3DAsmPartDefinition;
typedef void A3DAsmProductOccurrence;
typedef void A3DEntity;
typedef void A3DGraphics;
typedef void A3DMiscAttribute;
typedef void A3DMiscCartesianTransformation;
typedef void A3DMiscCartesianTransformation;
typedef void A3DMiscGeneralTransformation;
typedef void A3DRiBrepModel;
typedef void A3DRiCoordinateSystem;
typedef void A3DRiPolyBrepModel;
typedef void A3DRiRepresentationItem;
typedef void A3DRiSet;
typedef void A3DTess3D;
typedef void A3DTessBase;
typedef void A3DTopoBrepData;

namespace CADLibrary
{

class FArchiveBody;
class FArchiveComponent;
class FArchiveColor;
class FArchiveInstance;
class FArchiveMaterial;
class FArchiveUnloadedComponent;
class FCADFileData;
class FTechSoftInterface;

struct FEntityMetaData;

enum EComponentType : uint32
{
	Reference = 0,
	Occurrence,
	UnloadedComponent,
	Body,
	Undefined,
	LastType
};

class CADINTERFACES_API FTechSoftFileParser : public ICADFileParser
{
public:

	/**
	 * @param InCADData TODO
	 * @param EnginePluginsPath Full Path of EnginePlugins. Mandatory to set KernelIO to import DWG, or DGN files
	 */
	FTechSoftFileParser(FCADFileData& InCADData, const FString& EnginePluginsPath = TEXT(""));

#ifndef USE_TECHSOFT_SDK
	virtual ECADParsingResult Process() override
	{
		return ECADParsingResult::ProcessFailed;
	}
#else
	virtual ECADParsingResult Process() override;

private:

	// Methods to parse a first time the file to count the number of components
	// Needed to reserve CADFileData
	// Start with CountUnderModel
	void CountUnderModel();
	void CountUnderConfigurationSet(const A3DAsmProductOccurrence* Occurrence);
	void CountUnderOccurrence(const A3DAsmProductOccurrence* Occurrence);
	void CountUnderPrototype(const A3DAsmProductOccurrence* Prototype);
	void CountUnderSubPrototype(const A3DAsmProductOccurrence* Prototype);
	void CountUnderPartDefinition(const A3DAsmPartDefinition* PartDefinition);
	void CountUnderRepresentationItem(const A3DRiRepresentationItem* RepresentationItem);
	void CountUnderRepresentationSet(const A3DRiSet* RepresentationSet);

	void ReserveCADFileData();

	// Materials and colors
	uint32 CountColorAndMaterial();
	void ReadMaterialsAndColors();

	// Traverse ASM tree by starting from the model
	ECADParsingResult TraverseModel();
	void TraverseReference(const A3DAsmProductOccurrence* Reference, const FMatrix& ParentMatrix);
	bool IsConfigurationSet(const A3DAsmProductOccurrence* Occurrence);
	void TraverseConfigurationSet(const A3DAsmProductOccurrence* ConfigurationSet);
	FCadId TraverseOccurrence(const A3DAsmProductOccurrence* Occurrence);
	void ProcessPrototype(const A3DAsmProductOccurrence* InPrototype, FEntityMetaData& OutMetaData, A3DMiscTransformation** OutLocation);
	void TraversePartDefinition(const A3DAsmPartDefinition* PartDefinition, FArchiveComponent& Component);
	FCadId TraverseRepresentationSet(const A3DRiSet* pSet, FEntityMetaData& PartMetaData);
	FCadId TraverseRepresentationItem(A3DRiRepresentationItem* RepresentationItem, FEntityMetaData& PartMetaData);
	FCadId TraverseBRepModel(A3DRiBrepModel* BrepModel, FEntityMetaData& PartMetaData);
	FCadId TraversePolyBRepModel(A3DRiPolyBrepModel* PolygonalBrepModel, FEntityMetaData& PartMetaData);

	// Tessellation methods
	void GenerateBodyMeshes();

	// MetaData
	void ExtractMetaData(const A3DEntity* Entity, FEntityMetaData& OutMetaData);
	void ExtractSpecificMetaData(const A3DAsmProductOccurrence* Occurrence, FEntityMetaData& OutMetaData);

	void BuildInstanceName(TMap<FString, FString>& MetaData);
	void BuildReferenceName(TMap<FString, FString>& MetaData);
	void BuildPartName(TMap<FString, FString>& MetaData);
	void BuildBodyName(TMap<FString, FString>& MetaData);

	// Graphic properties
	void ExtractGraphicProperties(const A3DGraphics* Graphics, FEntityMetaData& OutMetaData);

	/**
	 * ColorName and MaterialName have to be initialized before.
	 * This method update the value ColorName or MaterialName accordingly of the GraphStyleData type (material or color)
	 */
	void ExtractGraphStyleProperties(uint32 StyleIndex, FCADUUID& ColorName, FCADUUID& MaterialName);
	void ExtractMaterialProperties(const A3DEntity* Entity);
	FArchiveColor& FindOrAddColor(uint32 ColorIndex, uint8 Alpha);
	FArchiveMaterial& FindOrAddMaterial(uint32 MaterialId, const A3DGraphStyleData& GraphStyleData);

	/**
	 * @param GraphMaterialIndex is the techsoft index of the graphic data
	 * @param MaterialIndexToSave is the index of the material really saved (i.e. for texture, at the texture index, with saved the material used by the texture)
	 */
	FArchiveMaterial& AddMaterialAt(uint32 MaterialIndexToSave, uint32 GraphMaterialIndex, const A3DGraphStyleData& GraphStyleData);
	FArchiveMaterial& AddMaterial(uint32 MaterialIndex, const A3DGraphStyleData& GraphStyleData)
	{
		return AddMaterialAt(MaterialIndex, MaterialIndex, GraphStyleData);
	}

	// Transform
	FMatrix TraverseCoordinateSystem(const A3DRiCoordinateSystem* CoordinateSystem);
	FMatrix TraverseTransformation(const A3DMiscTransformation* Transformation3d);
	FMatrix TraverseGeneralTransformation(const A3DMiscTransformation* GeneralTransformation);
	FMatrix TraverseTransformation3D(const A3DMiscTransformation* CartesianTransformation);

	// Archive methods
	FArchiveInstance& AddInstance(FEntityMetaData& InstanceMetaData);
	FArchiveComponent& AddComponent(FEntityMetaData& ComponentMetaData, FArchiveInstance& Instance);
	FArchiveUnloadedComponent& AddUnloadedComponent(FEntityMetaData& ComponentMetaData, FArchiveInstance& Instance);
	FArchiveComponent& AddOccurence(FEntityMetaData& InstanceMetaData, FEntityMetaData& ReferenceMetaData, FCadId& OutComponentId);
	FArchiveComponent& AddOccurence(FEntityMetaData& InstanceMetaData, FCadId& OutComponentId);
	int32 AddBody(FEntityMetaData& BodyMetaData, const FMatrix& Matrix);
#endif

private:

	FUniqueTechSoftModelFile ModelFile;

	TSet<const A3DAsmProductOccurrence*> PrototypeCounted;
	uint32 ComponentCount[EComponentType::LastType] = { 0 };

	FCADFileData& CADFileData;
	FTechSoftInterface& TechSoftInterface;

	ECADFormat Format;

	EModellerType ModellerType;
	double FileUnit = 1;
	FCadId LastEntityId = 1;

	// 
	TMap<A3DRiRepresentationItem*, int32> RepresentationItemsCache;
};

} // ns CADLibrary