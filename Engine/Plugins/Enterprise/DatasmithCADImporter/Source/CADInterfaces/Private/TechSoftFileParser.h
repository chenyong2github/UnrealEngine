// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "CADData.h"
#include "CADFileData.h"
#include "CADFileParser.h"
#include "CADOptions.h"
#include "CADSceneGraph.h"

#include "TUniqueTechSoftObj.h"

#define NEW_CODE

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

class FEntityData;
class FComponentData;

enum EComponentType : uint32
{
	Reference = 0,
	Occurrence,
	UnloadedComponent,
	Body,
	Undefined,
	LastType
};

enum EModellerType : uint32
{
	ModellerUnknown = 0,              /*!< User modeller. */
	ModellerCatia = 2,                /*!< CATIA modeller. */
	ModellerCatiaV5 = 3,              /*!< CATIA V5 modeller. */
	ModellerCadds = 4,                /*!< CADDS modeller. */
	ModellerUnigraphics = 5,          /*!< Unigraphics modeller. */
	ModellerParasolid = 6,            /*!< Parasolid modeller. */
	ModellerEuclid = 7,               /*!< Euclid modeller. */
	ModellerIges = 9,                 /*!< IGES modeller. */
	ModellerUnisurf = 10,             /*!< Unisurf modeller. */
	ModellerVda = 11,                 /*!< VDA modeller. */
	ModellerStl = 12,                 /*!< STL modeller. */
	ModellerWrl = 13,                 /*!< WRL modeller. */
	ModellerDxf = 14,                 /*!< DXF modeller. */
	ModellerAcis = 15,                /*!< ACIS modeller. */
	ModellerProE = 16,                /*!< Pro/E modeller. */
	ModellerStep = 18,                /*!< STEP modeller. */
	ModellerIdeas = 19,               /*!< I-DEAS modeller. */
	ModellerJt = 20,                  /*!< JT modeller. */
	ModellerSlw = 22,                 /*!< SolidWorks modeller. */
	ModellerCgr = 23,                 /*!< CGR modeller. */
	ModellerPrc = 24,                 /*!< PRC modeller. */
	ModellerXvl = 25,                 /*!< XVL modeller. */
	ModellerHpgl = 26,                /*!< HPGL modeller. */
	ModellerTopSolid = 27,            /*!< TopSolid modeller. */
	ModellerOneSpaceDesigner = 28,    /*!< OneSpace designer modeller. */
	Modeller3dxml = 29,               /*!< 3DXML modeller. */
	ModellerInventor = 30,            /*!< Inventor modeller. */
	ModellerPostScript = 31,          /*!< Postscript modeller. */
	ModellerPDF = 32,                 /*!< PDF modeller. */
	ModellerU3D = 33,                 /*!< U3D modeller. */
	ModellerIFC = 34,                 /*!< IFC modeller. */
	ModellerDWG = 35,                 /*!< DWG modeller. */
	ModellerDWF = 36,                 /*!< DWF modeller. */
	ModellerSE = 37,                  /*!< SolidEdge modeller. */
	ModellerOBJ = 38,                 /*!< OBJ modeller. */
	ModellerKMZ = 39,                 /*!< KMZ modeller. */
	ModellerDAE = 40,                 /*!< COLLADA modeller. */
	Modeller3DS = 41,                 /*!< 3DS modeller. */
	ModellerRhino = 43,               /*!< Rhino modeller. */
	ModellerXML = 44,                 /*!< XML modeller. */
	Modeller3mf = 45,                 /*!< 3MF modeller. */
	ModellerScs = 46,                 /*!< SCS modeller. */
	Modeller3dHtml = 47,              /*!< 3DHTML modeller. */
	ModellerHsf = 48,                 /*!< Hsf modeller. */
	ModellerGltf = 49,                /*!< GL modeller. */
	ModellerRevit = 50,               /*!< Revit modeller. */
	ModellerFBX = 51,                 /*!< FBX modeller. */
	ModellerStepXML = 52,             /*!< StepXML modeller. */
	ModellerPLMXML = 53,              /*!< PLMXML modeller. */
	ModellerNavisworks = 54,			/*!< For Future Use: Navisworks modeller. */
	ModellerLast
};

struct FEntityMetaData
{
	TMap<FString, FString> MetaData;
	bool bRemoved = false;
	bool bShow = true;
	bool bUnloaded = false;
	FCADUUID ColorName = 0;
	FCADUUID MaterialName = 0;
	FFileDescriptor ExternalFile;
};


class FTechSoftInterface;

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
	void CountUnderModel(const A3DAsmModelFile* AsmModel);
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
	ECADParsingResult TraverseModel(const A3DAsmModelFile* AsmModel);
	void TraverseReference(const A3DAsmProductOccurrence* Reference);
	bool IsConfigurationSet(const A3DAsmProductOccurrence* Occurrence);
	void TraverseConfigurationSet(const A3DAsmProductOccurrence* ConfigurationSet);
	FCadId TraverseOccurrence(const A3DAsmProductOccurrence* Occurrence);
	void TraversePrototype(const A3DAsmProductOccurrence* Prototype, FArchiveComponent& Component);
	void ProcessPrototype(const A3DAsmProductOccurrence* InPrototype, FEntityMetaData& OutMetaData, A3DMiscTransformation** OutLocation);
	void ProcessOccurrence(TUniqueTSObj<A3DAsmProductOccurrenceData>& Occurrence, FArchiveComponent& Component);
	void TraversePartDefinition(const A3DAsmPartDefinition* PartDefinition, FArchiveComponent& Component);
	FCadId TraverseRepresentationSet(const A3DRiSet* pSet, FEntityMetaData& PartMetaData);
	FCadId TraverseRepresentationItem(A3DRiRepresentationItem* RepresentationItem, FEntityMetaData& PartMetaData);
	FCadId TraverseBRepModel(A3DRiBrepModel* BrepModel, FEntityMetaData& PartMetaData);
	FCadId TraversePolyBRepModel(const A3DRiPolyBrepModel* PolygonalBrepModel, FEntityMetaData& PartMetaData);

	// Tessellation methods
	void MeshRepresentationWithTechSoft(A3DRiRepresentationItem* RepresentationItem, FArchiveBody& Body);
	void TraverseRepresentationContent(const A3DRiRepresentationItem* RepresentationItem, FArchiveBody& Body);
	void TraverseTessellationBase(const A3DTessBase* Tessellation, FArchiveBody& Body);
	void TraverseTessellation3D(const A3DTess3D* Tessellation, FArchiveBody& Body);

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
	FArchiveMaterial& FindOrAddMaterial(uint32 MaterialId);

	/**
	 * @param GraphMaterialIndex is the techsoft index of the graphic data
	 * @param MaterialIndexToSave is the index of the material really saved (i.e. for texture, at the texture index, with saved the material used by the texture)
	 */
	FArchiveMaterial& AddMaterialAt(uint32 MaterialIndexToSave, uint32 GraphMaterialIndex);
	FArchiveMaterial& AddMaterial(uint32 MaterialIndex)
	{
		return AddMaterialAt(MaterialIndex, MaterialIndex);
	}

	// Transform
	FMatrix TraverseCoordinateSystem(const A3DRiCoordinateSystem* CoordinateSystem);
	FMatrix TraverseTransformation(const A3DMiscTransformation* Transformation3d);
	FMatrix TraverseGeneralTransformation(const A3DMiscTransformation* GeneralTransformation);
	FMatrix TraverseTransformation3D(const A3DMiscTransformation* CartesianTransformation);

	// Archive methodes
	FArchiveInstance& AddInstance(FEntityMetaData& InstanceMetaData);
	FArchiveComponent& AddComponent(FEntityMetaData& ComponentMetaData, FArchiveInstance& Instance);
	FArchiveUnloadedComponent& AddUnloadedComponent(FEntityMetaData& ComponentMetaData, FArchiveInstance& Instance);
	FArchiveComponent& AddOccurence(FEntityMetaData& InstanceMetaData, FEntityMetaData& ReferenceMetaData, FCadId& OutComponentId);
	FArchiveComponent& AddOccurence(FEntityMetaData& InstanceMetaData, FCadId& OutComponentId);
	FArchiveBody& AddBody(FEntityMetaData& BodyMetaData);
#endif

private:

	TSet<const A3DAsmProductOccurrence*> PrototypeCounted;
	uint32 ComponentCount[EComponentType::LastType] = { 0 };

	FCADFileData& CADFileData;
	FTechSoftInterface& TechSoftInterface;

	ECADFormat Format;

	EModellerType ModellerType;
	double FileUnit = 1;
	FCadId LastEntityId = 1;

	// 
	TMap<const A3DRiRepresentationItem*, FCadId> RepresentationItemsCache;
};

namespace TechSoftFileParserImpl
{
// Methodes used by TraverseTessellation3D

typedef double A3DDouble;
inline bool AddFace(int32 FaceIndex[3], CADLibrary::FTessellationData& Tessellation, int32& InOutVertexIndex)
{
	if (FaceIndex[0] == FaceIndex[1] || FaceIndex[0] == FaceIndex[2] || FaceIndex[1] == FaceIndex[2])
	{
		return false;
	}

	for (int32 Index = 0; Index < 3; ++Index)
	{
		Tessellation.VertexIndices.Add(InOutVertexIndex++);
	}
	Tessellation.PositionIndices.Append(FaceIndex, 3);
	return true;
};

inline void AddNormals(const A3DDouble* Normals, const int32 Indices[3], TArray<FVector>& TessellationNormals)
{
	for (int32 Index = 0; Index < 3; ++Index)
	{
		int32 NormalIndex = Indices[Index];
		TessellationNormals.Emplace(Normals[NormalIndex], Normals[NormalIndex + 1], Normals[NormalIndex + 2]);
	}
};

inline void AddTextureCoordinates(const A3DDouble* TextureCoords, const int32 Indices[3], TArray<FVector2D>& TessellationTextures)
{
	for (int32 Index = 0; Index < 3; ++Index)
	{
		int32 TextureIndex = Indices[Index];
		TessellationTextures.Emplace(TextureCoords[TextureIndex], TextureCoords[TextureIndex + 1]);
	}
};

inline void Reserve(CADLibrary::FTessellationData& Tessellation, int32 InTrinangleCount, bool bWithTexture)
{
	Tessellation.PositionIndices.Reserve(3 * InTrinangleCount);
	Tessellation.VertexIndices.Reserve(3 * InTrinangleCount);
	Tessellation.NormalArray.Reserve(3 * InTrinangleCount);
	if (bWithTexture)
	{
		Tessellation.TexCoordArray.Reserve(3 * InTrinangleCount);
	}
};

#ifdef USE_TECHSOFT_SDK
inline FColor GetColorAt(uint32 ColorIndex)
{
	TUniqueTSObjFromIndex<A3DGraphRgbColorData> ColorData(ColorIndex);
	if (ColorData.IsValid())
	{
		return FColor((uint8)(ColorData->m_dRed * 255)
			, (uint8)(ColorData->m_dGreen * 255)
			, (uint8)(ColorData->m_dBlue * 255));
	}
	else
	{
		return FColor(200, 200, 200);
	}
}
#endif

} // ns TechSoftFileParserImpl

} // ns CADLibrary