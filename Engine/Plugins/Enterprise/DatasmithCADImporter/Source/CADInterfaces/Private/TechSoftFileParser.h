// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "CADData.h"
#include "CADFileParser.h"
#include "TechSoftInterface.h"

typedef void A3DAsmProductOccurrence;
typedef void A3DRiRepresentationItem;

namespace CADLibrary
{

class FArchiveBody;
class FArchiveComponent;
class FArchiveColor;
class FArchiveInstance;
class FArchiveMaterial;
class FArchiveUnloadedComponent;
class FCADFileData;

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

class FTechSoftFileParser : public ICADFileParser
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

	double GetFileUnit()
	{
		return FileUnit;
	}

	void ExtractMetaData(const A3DEntity* Entity, FEntityMetaData& OutMetaData);

protected:

	virtual A3DStatus AdaptBRepModel()
	{
		return A3DStatus::A3D_SUCCESS;
	}

	/**
	 * If the tessellator is TechSoft, SewModel call TechSoftInterface::SewModel
	 * If the tessellator is CADKernel, SewModel do nothing as the file is not yet parsed. In this case, the sew is done in GenerateBodyMeshes.
	 */
	virtual void SewModel();

	// Tessellation methods
	virtual void GenerateBodyMeshes();
	virtual void GenerateBodyMesh(A3DRiRepresentationItem* Representation, FArchiveBody& Body);


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

	// Note:
	// Due to dataprep purpose, node without name cannot have a generic name (e.g. Product, Part, Body, Shell, etc...) but must have a name based on its parent's name.
	// To be able to build a name, the name of the parent has to be known.
	// The implementation of the naming policy is done in 5.0.3 with minimal code modification. However the model parsing need to be rewrite in the next version. (Jira UE-152624)

	void TraverseReference(const A3DAsmProductOccurrence* Reference, const FString& RootName, const FMatrix& ParentMatrix, double ParentUnit);
	bool IsConfigurationSet(const A3DAsmProductOccurrence* Occurrence);
	void TraverseConfigurationSet(const A3DAsmProductOccurrence* ConfigurationSet, double ParentUnit);
	FCadId TraverseOccurrence(const A3DAsmProductOccurrence* Occurrence, const FString& DefaultOccurrenceName, double ParentUnit);
	void ProcessPrototype(const A3DAsmProductOccurrence* InPrototype, FEntityMetaData& OutMetaData, A3DMiscTransformation** OutLocation);
	void TraversePartDefinition(const A3DAsmPartDefinition* PartDefinition, FArchiveComponent& Component, double ParentUnit);
	FCadId TraverseRepresentationSet(const A3DRiSet* pSet, const FEntityMetaData& PartMetaData, double ParentUnit);
	FCadId TraverseRepresentationItem(A3DRiRepresentationItem* RepresentationItem, const FEntityMetaData& PartMetaData, const FCadId ParentId, double ParentUnit, int32 ItemIndex);
	FCadId TraverseBRepModel(A3DRiBrepModel* BrepModel, const FEntityMetaData& PartMetaData, const FCadId ParentId, double ParentUnit, int32 ItemIndex);
	FCadId TraversePolyBRepModel(A3DRiPolyBrepModel* PolygonalBrepModel, const FEntityMetaData& PartMetaData, const FCadId ParentId, double ParentUnit, int32 ItemIndex);

	// MetaData
	void ExtractSpecificMetaData(const A3DAsmProductOccurrence* Occurrence, FEntityMetaData& OutMetaData);

	void BuildInstanceName(FEntityMetaData& MetaData, const FString& DefaultInstanceName);
	void BuildReferenceName(FEntityMetaData& MetaData);
	void BuildPartName(FEntityMetaData& MetaData, const FArchiveComponent& Component);
	void BuildBodyName(FEntityMetaData& MetaData, const FEntityMetaData& PartMetaData, int32 ItemIndex, bool bIsSolid);

	// Graphic properties
	void ExtractGraphicProperties(const A3DGraphics* Graphics, FEntityMetaData& OutMetaData);

	/**
	 * ColorName and MaterialName have to be initialized before.
	 * This method update the value ColorName or MaterialName accordingly of the GraphStyleData type (material or color)
	 */
	void ExtractGraphStyleProperties(uint32 StyleIndex, FCADUUID& ColorName, FCADUUID& MaterialName);
	FArchiveColor& FindOrAddColor(uint32 ColorIndex, uint8 Alpha);
	FArchiveMaterial& FindOrAddMaterial(uint32 MaterialId, const A3DGraphStyleData& GraphStyleData);

	/**
	 * @param GraphMaterialIndex is the Techsoft index of the graphic data
	 * @param MaterialIndexToSave is the index of the material really saved (i.e. for texture, at the texture index, with saved the material used by the texture)
	 */
	FArchiveMaterial& AddMaterialAt(uint32 MaterialIndexToSave, uint32 GraphMaterialIndex, const A3DGraphStyleData& GraphStyleData);
	FArchiveMaterial& AddMaterial(uint32 MaterialIndex, const A3DGraphStyleData& GraphStyleData)
	{
		return AddMaterialAt(MaterialIndex, MaterialIndex, GraphStyleData);
	}

	// Transform
	FMatrix ExtractCoordinateSystem(const A3DRiCoordinateSystem* CoordinateSystem, double& InOutUnit);
	FMatrix ExtractTransformation(const A3DMiscTransformation* Transformation3d, double& InOutUnit);
	FMatrix ExtractGeneralTransformation(const A3DMiscTransformation* GeneralTransformation, double& InOutUnit);
	FMatrix ExtractTransformation3D(const A3DMiscTransformation* CartesianTransformation, double& InOutUnit);

	// Archive methods
	FArchiveInstance& AddInstance(FEntityMetaData& InstanceMetaData);
	FArchiveComponent& AddComponent(FEntityMetaData& ComponentMetaData, FArchiveInstance& Instance);
	FArchiveUnloadedComponent& AddUnloadedComponent(FEntityMetaData& ComponentMetaData, FArchiveInstance& Instance);
	FArchiveComponent& AddOccurence(FEntityMetaData& InstanceMetaData, FEntityMetaData& ReferenceMetaData, FCadId& OutComponentId);
	FArchiveComponent& AddOccurence(FEntityMetaData& InstanceMetaData, FCadId& OutComponentId);
	int32 AddBody(FEntityMetaData& BodyMetaData, const FMatrix& Matrix, const FCadId ParentId, double BodyUnit);
#endif

protected:

	FUniqueTechSoftModelFile ModelFile;

	TSet<const A3DAsmProductOccurrence*> PrototypeCounted;
	uint32 ComponentCount[EComponentType::LastType] = { 0 };

	FCADFileData& CADFileData;
	FTechSoftInterface& TechSoftInterface;
	ECADFormat Format;
	bool bForceSew = false;

	EModellerType ModellerType;
	double FileUnit = 1;

	FCadId LastEntityId = 1;

	TMap<A3DRiRepresentationItem*, int32> RepresentationItemsCache;
};

} // ns CADLibrary