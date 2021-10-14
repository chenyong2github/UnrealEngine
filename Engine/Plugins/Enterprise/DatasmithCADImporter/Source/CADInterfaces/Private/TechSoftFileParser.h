// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "CADData.h"
#include "CADFileData.h"
#include "CADFileParser.h"
#include "CADOptions.h"
#include "CADSceneGraph.h"

#include "TechSoftInterface.h"

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
		kA3DModellerUnknown = 0,              /*!< User modeller. */
		kA3DModellerCatia = 2,                /*!< CATIA modeller. */
		kA3DModellerCatiaV5 = 3,              /*!< CATIA V5 modeller. */
		kA3DModellerCadds = 4,                /*!< CADDS modeller. */
		kA3DModellerUnigraphics = 5,          /*!< Unigraphics modeller. */
		kA3DModellerParasolid = 6,            /*!< Parasolid modeller. */
		kA3DModellerEuclid = 7,               /*!< Euclid modeller. */
		kA3DModellerIges = 9,                 /*!< IGES modeller. */
		kA3DModellerUnisurf = 10,             /*!< Unisurf modeller. */
		kA3DModellerVda = 11,                 /*!< VDA modeller. */
		kA3DModellerStl = 12,                 /*!< STL modeller. */
		kA3DModellerWrl = 13,                 /*!< WRL modeller. */
		kA3DModellerDxf = 14,                 /*!< DXF modeller. */
		kA3DModellerAcis = 15,                /*!< ACIS modeller. */
		kA3DModellerProE = 16,                /*!< Pro/E modeller. */
		kA3DModellerStep = 18,                /*!< STEP modeller. */
		kA3DModellerIdeas = 19,               /*!< I-DEAS modeller. */
		kA3DModellerJt = 20,                  /*!< JT modeller. */
		kA3DModellerSlw = 22,                 /*!< SolidWorks modeller. */
		kA3DModellerCgr = 23,                 /*!< CGR modeller. */
		kA3DModellerPrc = 24,                 /*!< PRC modeller. */
		kA3DModellerXvl = 25,                 /*!< XVL modeller. */
		kA3DModellerHpgl = 26,                /*!< HPGL modeller. */
		kA3DModellerTopSolid = 27,            /*!< TopSolid modeller. */
		kA3DModellerOneSpaceDesigner = 28,    /*!< OneSpace designer modeller. */
		kA3DModeller3dxml = 29,               /*!< 3DXML modeller. */
		kA3DModellerInventor = 30,            /*!< Inventor modeller. */
		kA3DModellerPostScript = 31,          /*!< Postscript modeller. */
		kA3DModellerPDF = 32,                 /*!< PDF modeller. */
		kA3DModellerU3D = 33,                 /*!< U3D modeller. */
		kA3DModellerIFC = 34,                 /*!< IFC modeller. */
		kA3DModellerDWG = 35,                 /*!< DWG modeller. */
		kA3DModellerDWF = 36,                 /*!< DWF modeller. */
		kA3DModellerSE = 37,                  /*!< SolidEdge modeller. */
		kA3DModellerOBJ = 38,                 /*!< OBJ modeller. */
		kA3DModellerKMZ = 39,                 /*!< KMZ modeller. */
		kA3DModellerDAE = 40,                 /*!< COLLADA modeller. */
		kA3DModeller3DS = 41,                 /*!< 3DS modeller. */
		kA3DModellerRhino = 43,               /*!< Rhino modeller. */
		kA3DModellerXML = 44,                 /*!< XML modeller. */
		kA3DModeller3mf = 45,                 /*!< 3MF modeller. */
		kA3DModellerScs = 46,                 /*!< SCS modeller. */
		kA3DModeller3dHtml = 47,              /*!< 3DHTML modeller. */
		kA3DModellerHsf = 48,                 /*!< Hsf modeller. */
		kA3DModellerGltf = 49,                /*!< GL modeller. */
		kA3DModellerRevit = 50,               /*!< Revit modeller. */
		kA3DModellerFBX = 51,                 /*!< FBX modeller. */
		kA3DModellerStepXML = 52,             /*!< StepXML modeller. */
		kA3DModellerPLMXML = 53,              /*!< PLMXML modeller. */
		kA3DModellerNavisworks = 54,			/*!< For Future Use: Navisworks modeller. */
		kA3DModellerLast
	};

	struct FEntityBehaviour
	{
		bool bFatherHeritColor = false;
		bool bFatherHeritLayer = false;
		bool bFatherHeritLinePattern = false;
		bool bFatherHeritLineWidth = false;
		bool bFatherHeritShow = false;
		bool bFatherHeritTransparency = false;
		bool bRemoved = false;
		bool bShow = true;
		bool bSonHeritColor = false;
		bool bSonHeritLayer = false;
		bool bSonHeritLinePattern = false;
		bool bSonHeritLineWidth = false;
		bool bSonHeritShow = false;
		bool bSonHeritTransparency = false;
	};

	struct FEntityMetaData
	{
		TMap<FString, FString> MetaData;
		bool bRemoved = false;
		bool bShow = true;
		bool bUnloaded = false;
		FFileDescriptor ExternalFile;
	};

	class CADINTERFACES_API FTechSoftFileParser : public ICADFileParser
	{
	public:

		/**
		 * @param InCADData TODO
		 * @param EnginePluginsPath Full Path of EnginePlugins. Mandatory to set KernelIO to import DWG, or DGN files
		 */
		FTechSoftFileParser(FCADFileData& InCADData, const FString& EnginePluginsPath = TEXT(""))
			: CADFileData(InCADData)
		{
		}

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
		void CountUnderOccurrence(const A3DAsmProductOccurrence* Occurrence);
		void CountUnderPrototype(const A3DAsmProductOccurrence* Prototype);
		void CountUnderPartDefinition(const A3DAsmPartDefinition* PartDefinition);
		void CountUnderRepresentation(const A3DRiRepresentationItem* RepresentationItem);
		void CountUnderRepresentationSet(const A3DRiSet* RepresentationSet);

		void ReserveCADFileData();

		// Materials and colors
		int32 CountMaterial();
		int32 CountColor();
		void ReadMaterialsAndColors();

		// Traverse ASM tree by starting from the model
		ECADParsingResult TraverseModel(const A3DAsmModelFile* AsmModel);
		void TraverseReference(const A3DAsmProductOccurrence* Reference);
		bool IsConfigurationSet(const A3DAsmProductOccurrence* Occurrence);
		// TODO for SW //void TraverseReferenceFromConfiguration(const A3DAsmProductOccurrence* Reference, FEntityData& ParentMetaData);
		// TODO for SW //void TraverseConfigurationSet(const A3DAsmProductOccurrence* ConfigurationSet, FEntityData& ParentMetaData);
		// TODO for SW //void TraverseConfiguration(const A3DAsmProductOccurrence* OccurrConfigurationence, FEntityData& ParentMetaData);
		FCadId TraverseOccurrence(const A3DAsmProductOccurrence* Occurrence);
		void TraversePrototype(const A3DAsmProductOccurrence* InPrototype, FEntityMetaData& OutMetaData, FMatrix& OutPrototypeMatrix);
		FCadId TraversePartDefinition(const A3DAsmPartDefinition* PartDefinition);
		FCadId TraverseRepresentationSet(const A3DRiSet* pSet);
		FCadId TraverseRepresentationItem(A3DRiRepresentationItem* RepresentationItem);
		FCadId TraverseBRepModel(A3DRiBrepModel* BrepModel);
		FCadId TraversePolyBRepModel(const A3DRiPolyBrepModel* PolygonalBrepModel);

		// Tessellation methods
		void MeshRepresentationWithTechSoft(A3DRiRepresentationItem* RepresentationItem, FArchiveBody& Body);
		void TraverseRepresentationContent(const A3DRiRepresentationItem* RepresentationItem, FArchiveBody& Body);
		void TraverseTessellationBase(const A3DTessBase* Tessellation, FArchiveBody& Body);
		void TraverseTessellation3D(const A3DTess3D* Tessellation, FArchiveBody& Body);

		// MetaData
		void TraverseMetaData(const A3DEntity* Entity, FEntityMetaData& OutMetaData);
		void TraverseSpecificMetaData(const A3DAsmProductOccurrence* Occurrence, FEntityMetaData& OutMetaData);
		FString DefineEntityName(TMap<FString, FString>& OutMetaData, EComponentType EntityType);

		// Graphic properties
		void TraverseGraphics(const A3DGraphics* Graphics, FEntityBehaviour& GraphicsBehaviour);
		void TraverseMaterialProperties(const A3DEntity* Entity);
		void TraverseLayer(const A3DAsmProductOccurrence* Occurrence);

		// Transform
		FMatrix TraverseCoordinateSystem(const A3DRiCoordinateSystem* CoordinateSystem);
		FMatrix TraverseTransformation(const A3DMiscCartesianTransformation* Transformation3d);
		FMatrix TraverseGeneralTransformation(const A3DMiscGeneralTransformation* GeneralTransformation);
		FMatrix TraverseTransformation3D(const A3DMiscCartesianTransformation* CartesianTransformation);

		FFileDescriptor GetOccurrenceFileName(const A3DAsmProductOccurrence* OccurrencePtr);

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
		uint32 ComponentCount[EComponentType::LastType] = {0};

		FCADFileData& CADFileData;
		TSharedPtr<ITechSoftInterface> TechSoftInterface;

		EModellerType ModellerType;
		double FileUnit = 1;
		FCadId LastEntityId = 1;
	};
} // ns CADLibrary

namespace TechSoftFileParserImpl
{
	// Methodes used by TraverseTessellation3D
	
	typedef double A3DDouble;
	inline bool AddFace(int32 FaceIndex[3], CADLibrary::FTessellationData& Tessellation, int32& VertexIndex)
	{
		if (FaceIndex[0] == FaceIndex[1] || FaceIndex[0] == FaceIndex[2] || FaceIndex[1] == FaceIndex[2])
		{
			return false;
		}

		for (int32 Index = 0; Index < 3; ++Index)
		{
			FaceIndex[Index] /= 3;
			Tessellation.VertexIndices.Add(VertexIndex++);
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

}