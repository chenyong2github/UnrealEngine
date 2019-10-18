// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DatasmithWireTranslator.h"

#include "Containers/List.h"
#include "DatasmithImportOptions.h"
#include "DatasmithMeshHelper.h"
#include "DatasmithSceneFactory.h"
#include "DatasmithUtils.h"
#include "DatasmithWireTranslatorModule.h"
#include "IDatasmithSceneElements.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "OpenModelUtils.h"
#include "Translators\DatasmithTranslator.h"

#ifdef CAD_LIBRARY
#include "AliasCoretechWrapper.h" // requires CoreTech as public dependency
#include "CoreTechParametricSurfaceExtension.h"
#endif

#ifdef USE_OPENMODEL
#include <AlChannel.h>
#include <AlDagNode.h>
#include <AlGroupNode.h>
#include <AlLayer.h>
#include <AlLinkItem.h>
#include <AlList.h>
#include <AlMesh.h>
#include <AlMeshNode.h>
#include <AlPersistentID.h>
#include <AlRetrieveOptions.h>
#include <AlShader.h>
#include <AlShadingFieldItem.h>
#include <AlShell.h>
#include <AlShellNode.h>
#include <AlSurface.h>
#include <AlSurfaceNode.h>
#include <AlTesselate.h>
#include <AlTrimRegion.h>
#include <AlTM.h>
#include <AlUniverse.h>
#endif

#ifdef USE_OPENMODEL

using namespace OpenModelUtils;

class BodyData
{
public:
	BodyData(const char* InShaderName, const char* InLayerName, bool bInCadData)
		: bCadData(bInCadData)
	{
		ShaderName = InShaderName;
		LayerName = InLayerName;
	};

	TArray<AlDagNode*> ShellSet;
	FString ShaderName;
	FString LayerName;
	FString Label;
	bool bCadData = true;
};

class FWireTranslatorImpl
{
public:
	FWireTranslatorImpl(const FString& InSceneFullName, TSharedRef<IDatasmithScene> InScene)
		: DatasmithScene(InScene)
		, SceneName(FPaths::GetBaseFilename(InSceneFullName))
		, CurrentPath(FPaths::GetPath(InSceneFullName))
		, SceneFullPath(InSceneFullName)
		, TessellationOptionsHash(0)
		, AlRootNode(nullptr)
		, FileVersion(0)
		, ArchiveWireVersion(0)
		, FileLength(0)
		, NumCRCErrors(0)
	{
		DatasmithScene->SetHost(TEXT("Alias"));
		DatasmithScene->SetVendor(TEXT("Autodesk"));
		DatasmithScene->SetExporterSDKVersion(TEXT("2019"));
#ifdef CAD_LIBRARY
		LocalSession = FAliasCoretechWrapper::GetSharedSession();
#endif
	}

	~FWireTranslatorImpl()
	{
		AlUniverse::deleteAll();
#ifdef CAD_LIBRARY
		LocalSession.Reset();
#endif
	}

	bool Read();
	TOptional<FMeshDescription> GetMeshDescription(TSharedRef<IDatasmithMeshElement> MeshElement, FMeshParameters& MeshParameters);
	TOptional<FMeshDescription> GetMeshDescription(TSharedRef<IDatasmithMeshElement> MeshElement, FMeshParameters& MeshParameters, TSharedRef<BodyData> BodyTemp);

	void SetTessellationOptions(const FDatasmithTessellationOptions& Options);
	void SetOutputPath(const FString& Path) { OutputPath = Path; }

	//double GetMetricUnit() const { return LocalSession->GetImportParameters().MetricUnit; }
	FImportParameters& GetImportParameters() 
	{
		return LocalSession->GetImportParameters(); 
	}


private:

	struct FDagNodeInfo
	{
		FString UEuuid;  // Use for actor name
		FString Label;
		TSharedPtr< IDatasmithActorElement > ActorElement;
	};

	bool GetDagLeaves();
	bool GetShader();
	bool RecurseDagForLeaves(AlDagNode* DagNode, const FDagNodeInfo& ParentInfo);
	bool RecurseDagForLeavesNoMerge(AlDagNode* DagNode, const FDagNodeInfo& ParentInfo);
	bool ProcessAlGroupNode(AlGroupNode& GroupNode, const FDagNodeInfo& ParentInfo);
	bool ProcessAlShellNode(AlDagNode& GroupNode, const FDagNodeInfo& ParentInfo, const char* ShaderName);
	bool ProcessBodyNode(TSharedRef<BodyData> Body, const FDagNodeInfo& ParentInfo);
	TSharedPtr< IDatasmithMeshElement > AddMeshElement(TSharedRef<BodyData> Body, const FDagNodeInfo& ParentInfo);
	TSharedPtr< IDatasmithMeshElement > FindOrAddMeshElement(AlDagNode& ShellNode, const FDagNodeInfo& ParentInfo, const char* ShaderName);
	void GetDagNodeInfo(AlDagNode& GroupNode, const FDagNodeInfo& ParentInfo, FDagNodeInfo& CurrentNodeInfo);
	void GetDagNodeInfo(TSharedRef<BodyData> CurrentNode, const FDagNodeInfo& ParentInfo, FDagNodeInfo& CurrentNodeInfo);
	void GetDagNodeMeta(AlDagNode& CurrentNode, TSharedPtr< IDatasmithActorElement > ActorElement);

	TOptional<FMeshDescription> GetMeshOfShellNode(AlDagNode& DagNode, TSharedRef<IDatasmithMeshElement> MeshElement, FMeshParameters& MeshParameters);
	TOptional<FMeshDescription> GetMeshOfNodeMesh(AlMeshNode& DagNode, TSharedRef<IDatasmithMeshElement> MeshElement, FMeshParameters& MeshParameters, AlMatrix4x4* AlMeshInvGlobalMatrix = nullptr);
	TOptional<FMeshDescription> GetMeshOfShellBody(TSharedRef<BodyData> DagNode, TSharedRef<IDatasmithMeshElement> MeshElement, FMeshParameters& MeshParameters);
	TOptional<FMeshDescription> GetMeshOfMeshBody(TSharedRef<BodyData> DagNode, TSharedRef<IDatasmithMeshElement> MeshElement, FMeshParameters& MeshParameters);

	void AddNodeInBodySet(AlDagNode& DagNode, const char* ShaderName, TMap<uint32, TSharedPtr<BodyData>>& ShellToProcess, bool bIsAPatch, uint32 MaxSize);

#ifdef CAD_LIBRARY
	TOptional<FMeshDescription> MeshDagNodeWithExternalMesher(AlDagNode& DagNode, TSharedRef<IDatasmithMeshElement> MeshElement, FMeshParameters& MeshParameters);
	TOptional<FMeshDescription> MeshDagNodeWithExternalMesher(TSharedRef<BodyData> DagNode, TSharedRef<IDatasmithMeshElement> MeshElement, FMeshParameters& MeshParameters);
#endif

 	TOptional< FMeshDescription > ImportMesh(AlMesh& Mesh, FMeshParameters& MeshParameters);

	void CreateAlCommonMaterial(AlShader *Shader, TSharedRef<IDatasmithUEPbrMaterialElement> MaterialElement);
	void AddAlBlinnParameters(AlShader *Shader, TSharedRef<IDatasmithUEPbrMaterialElement> MaterialElement);
	void AddAlLambertParameters(AlShader *Shader, TSharedRef<IDatasmithUEPbrMaterialElement> MaterialElement);
	void AddAlLightSourceParameters(AlShader *Shader, TSharedRef<IDatasmithUEPbrMaterialElement> MaterialElement);
	void AddAlPhongParameters(AlShader *Shader, TSharedRef<IDatasmithUEPbrMaterialElement> MaterialElement);

private:
	TSharedRef<IDatasmithScene> DatasmithScene;
	FString SceneName;
	FString CurrentPath;
	FString OutputPath;
	FString SceneFullPath;

	FDatasmithTessellationOptions TessellationOptions;
	uint32 TessellationOptionsHash;
	AlDagNode* AlRootNode;

	/** Table of correspondence between mesh identifier and associated Datasmith mesh element */
	TMap< uint32, TSharedPtr< IDatasmithMeshElement > > ShellUUIDToMeshElementMap;

	/** Datasmith mesh elements to OpenModel objects */
	TMap< IDatasmithMeshElement*, AlDagNode* > MeshElementToAlDagNodeMap;

	TMap< IDatasmithMeshElement*, TSharedPtr<BodyData>> MeshElementToBodyMap;

	TMap <FString, TSharedPtr< IDatasmithMaterialIDElement >> ShaderNameToUEMaterialId;

	// start section information
	int32 FileVersion;
	int32 ArchiveWireVersion;

	// length of archive returned by ON_BinaryArchive::Read3dmEndMark()
	size_t FileLength;

	// Number of crc errors found during archive reading.
	// If > 0, then the archive is corrupt.
	int32 NumCRCErrors;

#ifdef CAD_LIBRARY
	TSharedPtr<FAliasCoretechWrapper> LocalSession;
#endif
};

void FWireTranslatorImpl::SetTessellationOptions(const FDatasmithTessellationOptions& Options)
{
	TessellationOptions = Options;
	TessellationOptionsHash = Options.GetHash();
}
#endif



#ifdef USE_OPENMODEL

bool FWireTranslatorImpl::Read()
{
	// Initialize Alias.
	AlUniverse::initialize();

	char* fileName = TCHAR_TO_UTF8(*SceneFullPath);
	if (AlUniverse::retrieve(fileName) != sSuccess)
	{
		return false;
	}

	LocalSession->SetImportParameters(TessellationOptions.ChordTolerance, TessellationOptions.MaxEdgeLength, TessellationOptions.NormalTolerance, (CADLibrary::EStitchingTechnique) TessellationOptions.StitchingTechnique);

	AlRetrieveOptions options;
	AlUniverse::retrieveOptions(options);

	// Make material
	if (!GetShader())
	{
		return false;
	}

	// Parse and extract the DAG leaf nodes.
	// Note that Alias file unit is cm like UE
	if (!GetDagLeaves())
	{
		return false;
	}

	return true;
}

void FWireTranslatorImpl::CreateAlCommonMaterial(AlShader *Shader, TSharedRef<IDatasmithUEPbrMaterialElement> MaterialElement)
{
	float Color[3], Transparency[3], Incandescence[3];
	float Glow = 0;
	float TransparencyDepth = 0;
	float TransparencyShade = 0;

	Color[0] = 0.f;
	Color[1] = 0.f;
	Color[2] = 0.f;
	Transparency[0] = 0.f;
	Transparency[1] = 0.f;
	Transparency[2] = 0.f;
	Incandescence[0] = 0.f;
	Incandescence[1] = 0.f;
	Incandescence[2] = 0.f;

	bool TransparencyDefined = false;
	bool IncandescenceDefined = false;

	AlList *list = Shader->fields();
	AlShadingFieldItem *item = static_cast<AlShadingFieldItem *>(list->first());
	while (item)
	{
		double Value = 0.0;
		statusCode errorCode = Shader->parameter(item->field(), Value);
		if (errorCode != 0)
		{
			item = item->nextField();
			continue;
		}

		switch (item->field())
		{
		case AlShadingFields::kFLD_SHADING_COMMON_COLOR_R:
			Color[0] = Value;
			break;
		case AlShadingFields::kFLD_SHADING_COMMON_COLOR_G:
			Color[1] = Value;
			break;
		case  AlShadingFields::kFLD_SHADING_COMMON_COLOR_B:
			Color[2] = Value;
			break;

		case  AlShadingFields::kFLD_SHADING_COMMON_TRANSPARENCY_R:
			Transparency[0] = Value;
			TransparencyDefined = true;
			break;
		case  AlShadingFields::kFLD_SHADING_COMMON_TRANSPARENCY_G:
			Transparency[1] = Value;
			TransparencyDefined = true;
			break;
		case  AlShadingFields::kFLD_SHADING_COMMON_TRANSPARENCY_B:
			Transparency[2] = Value;
			TransparencyDefined = true;
			break;

		case  AlShadingFields::kFLD_SHADING_COMMON_TRANSPARENCY_DEPTH:
			TransparencyDepth = Value;
			TransparencyDefined = true;
			break;

		case  AlShadingFields::kFLD_SHADING_COMMON_TRANSPARENCY_SHADE:
			TransparencyShade = Value;
			TransparencyDefined = true;
			break;

		case AlShadingFields::kFLD_SHADING_COMMON_INCANDESCENCE_R:
			Incandescence[0] = Value;
			IncandescenceDefined = true;
			break;
		case AlShadingFields::kFLD_SHADING_COMMON_INCANDESCENCE_G:
			Incandescence[1] = Value;
			IncandescenceDefined = true;
			break;
		case AlShadingFields::kFLD_SHADING_COMMON_INCANDESCENCE_B:
			Incandescence[2] = Value;
			IncandescenceDefined = true;

		case AlShadingFields::kFLD_SHADING_COMMON_GLOW_INTENSITY:
			Glow = Value;
			break;

		}
		item = item->nextField();
	}

	FLinearColor BaseColor = FLinearColor::FromPow22Color(FColor(Color[0], Color[1], Color[2], 255));
	IDatasmithMaterialExpressionColor* BaseColorExpression = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionColor>();
	BaseColorExpression->SetName(TEXT("Color"));
	BaseColorExpression->GetColor() = BaseColor;
	MaterialElement->GetBaseColor().SetExpression(BaseColorExpression);

	if (TransparencyDefined)
	{
		FLinearColor TransparencyColor = FLinearColor::FromPow22Color(FColor(Transparency[0], Transparency[1], Transparency[2], 255));
		IDatasmithMaterialExpressionColor* TransparencyExpression = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionColor>();
		TransparencyExpression->SetName(TEXT("Transparency"));
		TransparencyExpression->GetColor() = TransparencyColor;

		IDatasmithMaterialExpressionGeneric* OneMinus = MaterialElement->AddMaterialExpression< IDatasmithMaterialExpressionGeneric >();
		OneMinus->SetExpressionName(TEXT("OneMinus"));
		TransparencyExpression->ConnectExpression(*OneMinus->GetInput(0));
		MaterialElement->GetOpacity().SetExpression(OneMinus);
	}

	IDatasmithMaterialExpressionGeneric* GlowAdd = MaterialElement->AddMaterialExpression< IDatasmithMaterialExpressionGeneric >();
	GlowAdd->SetExpressionName(TEXT("Add"));

	if (IncandescenceDefined)
	{
		FLinearColor IncandescenceColor = FLinearColor::FromPow22Color(FColor(Incandescence[0], Incandescence[1], Incandescence[2], 255));
		IDatasmithMaterialExpressionColor* IncandescenceExpression = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionColor>();
		IncandescenceExpression->SetName(TEXT("Incandescence"));
		IncandescenceExpression->GetColor() = IncandescenceColor;
		IncandescenceExpression->ConnectExpression(*GlowAdd->GetInput(1));
	}

	IDatasmithMaterialExpressionGeneric* GlowMultiply = MaterialElement->AddMaterialExpression< IDatasmithMaterialExpressionGeneric >();
	GlowMultiply->SetExpressionName(TEXT("Multiply"));

	BaseColorExpression->ConnectExpression(*GlowMultiply->GetInput(0));

	IDatasmithMaterialExpressionScalar* GlowExpression = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionScalar>();
	GlowExpression->GetScalar() = Glow;
	GlowExpression->SetName(TEXT("Glow Intensity"));

	GlowExpression->ConnectExpression(*GlowMultiply->GetInput(1));

	GlowMultiply->ConnectExpression(*GlowAdd->GetInput(0));

	MaterialElement->GetEmissiveColor().SetExpression(GlowAdd);
}

void FWireTranslatorImpl::AddAlBlinnParameters(AlShader *Shader, TSharedRef<IDatasmithUEPbrMaterialElement> MaterialElement)
{
	float Specular[3];
	Specular[0] = Specular[1] = Specular[2] = 0;
	bool SpecularDefined = false;
	float Reflectivity = 0;


	AlList *list = Shader->fields();
	AlShadingFieldItem *item = static_cast<AlShadingFieldItem *>(list->first());
	while (item)
	{
		double Value = 0.0;
		statusCode errorCode = Shader->parameter(item->field(), Value);
		if (errorCode != 0)
		{
			item = item->nextField();
			continue;
		}

		switch (item->field())
		{
		case  AlShadingFields::kFLD_SHADING_BLINN_DIFFUSE:
		case  AlShadingFields::kFLD_SHADING_BLINN_ECCENTRICITY:
		case  AlShadingFields::kFLD_SHADING_BLINN_SPECULAR_ROLLOFF:
			break;

		case  AlShadingFields::kFLD_SHADING_BLINN_REFLECTIVITY:
			Reflectivity = Value;
			break;

		case  AlShadingFields::kFLD_SHADING_BLINN_SPECULAR_R:
			Specular[0] = Value;
			SpecularDefined = true;
			break;
		case  AlShadingFields::kFLD_SHADING_BLINN_SPECULAR_G:
			Specular[1] = Value;
			SpecularDefined = true;
			break;
		case  AlShadingFields::kFLD_SHADING_BLINN_SPECULAR_B:
			Specular[2] = Value;
			SpecularDefined = true;
			break;

		}
		item = item->nextField();
	}

	if (SpecularDefined)
	{
		FLinearColor SpecularColor = FLinearColor::FromPow22Color(FColor(Specular[0], Specular[1], Specular[2], 255.));
		IDatasmithMaterialExpressionColor* SpecularExpression = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionColor>();
		SpecularExpression->SetName(TEXT("Specular"));
		SpecularExpression->GetColor() = SpecularColor;
		MaterialElement->GetSpecular().SetExpression(SpecularExpression);
	}

	IDatasmithMaterialExpressionScalar* ReflectivityScalar = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionScalar>();
	ReflectivityScalar->GetScalar() = Reflectivity;
	ReflectivityScalar->SetName(TEXT("Reflectivity"));
	MaterialElement->GetMetallic().SetExpression(ReflectivityScalar);

	IDatasmithMaterialExpressionGeneric* OneMinus = MaterialElement->AddMaterialExpression< IDatasmithMaterialExpressionGeneric >();
	OneMinus->SetExpressionName(TEXT("OneMinus"));
	ReflectivityScalar->ConnectExpression(*OneMinus->GetInput(0));
	MaterialElement->GetRoughness().SetExpression(OneMinus);


	MaterialElement->SetParentLabel(TEXT("BLINN"));
}

void FWireTranslatorImpl::AddAlLambertParameters(AlShader *Shader, TSharedRef<IDatasmithUEPbrMaterialElement> MaterialElement)
{
	float Diffuse = 0.8;

	AlList *list = Shader->fields();
	AlShadingFieldItem *item = static_cast<AlShadingFieldItem *>(list->first());
	while (item)
	{
		double Value = 0.0;
		statusCode errorCode = Shader->parameter(item->field(), Value);
		if (errorCode != 0)
		{
			item = item->nextField();
			continue;
		}

		switch (item->field())
		{
		case  AlShadingFields::kFLD_SHADING_LAMBERT_DIFFUSE:
			Diffuse = Value;
			break;
		}
		item = item->nextField();
	}

	IDatasmithMaterialExpressionScalar* DiffuseScalar = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionScalar>();
	DiffuseScalar->GetScalar() = Diffuse;
	DiffuseScalar->SetName(TEXT("Diffuse"));

	IDatasmithMaterialExpressionGeneric* DiffuseMultiply = MaterialElement->AddMaterialExpression< IDatasmithMaterialExpressionGeneric >();
	DiffuseMultiply->SetExpressionName(TEXT("Multiply"));

	//BaseColor->ConnectExpression(*DiffuseMultiply->GetInput(0));
	DiffuseScalar->ConnectExpression(*DiffuseMultiply->GetInput(1));

	MaterialElement->GetBaseColor().SetExpression(DiffuseMultiply);

	MaterialElement->SetParentLabel(TEXT("LAMBERT"));
}

void FWireTranslatorImpl::AddAlLightSourceParameters(AlShader *Shader, TSharedRef<IDatasmithUEPbrMaterialElement> MaterialElement)
{
	IDatasmithMaterialExpressionFlattenNormal* LightSource = MaterialElement->AddMaterialExpression< IDatasmithMaterialExpressionFlattenNormal >();
	MaterialElement->GetNormal().SetExpression(LightSource);
	MaterialElement->SetParentLabel(TEXT("LIGHTSOURCE"));
}

void FWireTranslatorImpl::AddAlPhongParameters(AlShader *Shader, TSharedRef<IDatasmithUEPbrMaterialElement> MaterialElement)
{
	float Specular[3];
	Specular[0] = Specular[1] = Specular[2] = 0;
	bool SpecularDefined = false;

	float Reflectivity = 0;
	float Diffuse = 0;
	float Shinyness = 0;


	AlList *list = Shader->fields();
	AlShadingFieldItem *item = static_cast<AlShadingFieldItem *>(list->first());
	while (item)
	{
		double Value = 0.0;
		statusCode errorCode = Shader->parameter(item->field(), Value);
		if (errorCode != 0)
		{
			item = item->nextField();
			continue;
		}

		switch (item->field())
		{
		case  AlShadingFields::kFLD_SHADING_PHONG_DIFFUSE:
			Diffuse = Value;
			break;
		case  AlShadingFields::kFLD_SHADING_PHONG_REFLECTIVITY:
			Reflectivity = Value;
			break;
		case  AlShadingFields::kFLD_SHADING_PHONG_SHINYNESS:
			Shinyness = Value;
			break;
		case  AlShadingFields::kFLD_SHADING_PHONG_SPECULAR_R:
			Specular[0] = Value;
			SpecularDefined = true;
			break;
		case  AlShadingFields::kFLD_SHADING_PHONG_SPECULAR_G:
			Specular[1] = Value;
			SpecularDefined = true;
			break;
		case  AlShadingFields::kFLD_SHADING_PHONG_SPECULAR_B:
			Specular[2] = Value;
			SpecularDefined = true;
			break;
		}
		item = item->nextField();
	}

	if (SpecularDefined)
	{
		FLinearColor SpecularColor = FLinearColor::FromPow22Color(FColor(Specular[0], Specular[1], Specular[2], 255));
		IDatasmithMaterialExpressionColor* SpecularExpression = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionColor>();
		SpecularExpression->SetName(TEXT("Specular"));
		SpecularExpression->GetColor() = SpecularColor;
		MaterialElement->GetSpecular().SetExpression(SpecularExpression);
	}

	IDatasmithMaterialExpressionScalar* ReflectivityScalar = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionScalar>();
	ReflectivityScalar->GetScalar() = Reflectivity;
	ReflectivityScalar->SetName(TEXT("Reflectivity"));

	//MaterialElement->GetMetallic().SetExpression(ReflectivityScalar);

	IDatasmithMaterialExpressionGeneric* OneMinus = MaterialElement->AddMaterialExpression< IDatasmithMaterialExpressionGeneric >();
	OneMinus->SetExpressionName(TEXT("OneMinus"));
	ReflectivityScalar->ConnectExpression(*OneMinus->GetInput(0));
	MaterialElement->GetRoughness().SetExpression(OneMinus);

	MaterialElement->SetParentLabel(TEXT("PHONG"));
}
//#endif



// Make material
bool FWireTranslatorImpl::GetShader()
{
	AlShader *Shader = AlUniverse::firstShader();
	while (Shader)
	{
		FString ShaderName = Shader->name();
		FString ShaderModelName = Shader->shadingModel();
		
		uint32 ShaderUUID = fabs((int32)GetTypeHash(*ShaderName));

		TSharedRef<IDatasmithUEPbrMaterialElement> MaterialElement = FDatasmithSceneFactory::CreateUEPbrMaterial(*ShaderName);

		MaterialElement->SetLabel(*ShaderName);
		MaterialElement->SetName(*FString::FromInt(ShaderUUID));

		CreateAlCommonMaterial(Shader, MaterialElement);

		if (ShaderModelName.Equals(TEXT("BLINN"))) 
		{
			AddAlBlinnParameters(Shader, MaterialElement);
		}
		else if (ShaderModelName.Equals(TEXT("LAMBERT"))) 
		{
			AddAlLambertParameters(Shader, MaterialElement);
		}
		else if (ShaderModelName.Equals(TEXT("LIGHTSOURCE"))) 
		{
			AddAlLightSourceParameters(Shader, MaterialElement);
		}
		else if (ShaderModelName.Equals(TEXT("PHONG"))) 
		{
			AddAlPhongParameters(Shader, MaterialElement);
		}
		else
		{
		}

		DatasmithScene->AddMaterial(MaterialElement);

		TSharedPtr< IDatasmithMaterialIDElement > MaterialIDElement = FDatasmithSceneFactory::CreateMaterialId(MaterialElement->GetName());
		ShaderNameToUEMaterialId.Add(*ShaderName, MaterialIDElement);

		AlShader *curShader = Shader;
		Shader = AlUniverse::nextShader(Shader);
		delete curShader;
	}
	return true;
}



bool FWireTranslatorImpl::GetDagLeaves()
{
	FDagNodeInfo RootContainer;
	AlRootNode = AlUniverse::firstDagNode();
	return RecurseDagForLeaves(AlRootNode, RootContainer);
}


void FWireTranslatorImpl::GetDagNodeMeta(AlDagNode& CurrentNode, TSharedPtr< IDatasmithActorElement > ActorElement)
{
	AlLayer* layer = CurrentNode.layer();
	if (layer != nullptr)
	{
		FString LayerName = CurrentNode.layer()->name();
		ActorElement->SetLayer(*LayerName);
	}

    // TODO import other Meta

}


void FWireTranslatorImpl::GetDagNodeInfo(AlDagNode& CurrentNode, const FDagNodeInfo& ParentInfo, FDagNodeInfo& CurrentNodeInfo)
{
	CurrentNodeInfo.Label = CurrentNode.name();

	FString ThisGroupNodeID;
	AlPersistentID* GroupNodeId = new AlPersistentID();

	CurrentNode.persistentID(GroupNodeId);
	ThisGroupNodeID = GetPersistentIDString(GroupNodeId);

    // Limit length of UUID by combining hash of parent UUID and container's UUID if ParentUuid is not empty
	CurrentNodeInfo.UEuuid = GetUEUUIDFromAIPersistentID(ParentInfo.UEuuid, ThisGroupNodeID);
}

void FWireTranslatorImpl::GetDagNodeInfo(TSharedRef<BodyData> CurrentNode, const FDagNodeInfo& ParentInfo, FDagNodeInfo& CurrentNodeInfo)
{
	CurrentNodeInfo.Label = ParentInfo.Label; // +TEXT(" ") + CurrentNode->LayerName + TEXT(" ") + CurrentNode->ShaderName;
	CurrentNode->Label = CurrentNodeInfo.Label;

	FString ThisGroupNodeID;
	AlPersistentID* GroupNodeId = new AlPersistentID();

	// Limit length of UUID by combining hash of parent UUID and container's UUID if ParentUuid is not empty
	CurrentNodeInfo.UEuuid = GetUEUUIDFromAIPersistentID(ParentInfo.UEuuid, CurrentNodeInfo.Label);
}


bool FWireTranslatorImpl::ProcessAlGroupNode(AlGroupNode& GroupNode, const FDagNodeInfo& ParentInfo)
{
	FDagNodeInfo ThisGroupNodeInfo;

	GetDagNodeInfo(GroupNode, ParentInfo, ThisGroupNodeInfo);

	ThisGroupNodeInfo.ActorElement = FDatasmithSceneFactory::CreateActor(*ThisGroupNodeInfo.UEuuid);
	ThisGroupNodeInfo.ActorElement->SetLabel(*ThisGroupNodeInfo.Label);
	GetDagNodeMeta(GroupNode, ThisGroupNodeInfo.ActorElement);

	AlDagNode * ChildNode = GroupNode.childNode();
	if (AlIsValid(ChildNode) == TRUE)
	{
		RecurseDagForLeaves(ChildNode, ThisGroupNodeInfo);
	}

	// add the resulting actor to the scene
	if (IsValidActor(ThisGroupNodeInfo.ActorElement))
	{
		// Apply local transform to actor element
		SetActorTransform(ThisGroupNodeInfo.ActorElement, GroupNode);

		if (ParentInfo.ActorElement.IsValid())
		{
			ParentInfo.ActorElement->AddChild(ThisGroupNodeInfo.ActorElement);
		}
		else
		{
			DatasmithScene->AddActor(ThisGroupNodeInfo.ActorElement);
		}
	}

	return true;
}

TSharedPtr< IDatasmithMeshElement > FWireTranslatorImpl::AddMeshElement(TSharedRef<BodyData> Body, const FDagNodeInfo& NodeInfo)
{
	uint32 BodyUUID = GetTypeHash(NodeInfo.UEuuid);

	TSharedPtr< IDatasmithMeshElement > MeshElement = FDatasmithSceneFactory::CreateMesh(*FString::Printf(TEXT("0x%08x"), BodyUUID));
	MeshElement->SetLabel(*NodeInfo.Label);
	MeshElement->SetLightmapSourceUV(-1);

	if (*Body->ShaderName)
	{
		TSharedPtr< IDatasmithMaterialIDElement > MaterialElement = ShaderNameToUEMaterialId[Body->ShaderName];
		MeshElement->SetMaterial(MaterialElement->GetName(), 0);
	}

	DatasmithScene->AddMesh(MeshElement);

	ShellUUIDToMeshElementMap.Add(BodyUUID, MeshElement);
	MeshElementToBodyMap.Add(MeshElement.Get(), Body);

	return MeshElement;
}

TSharedPtr< IDatasmithMeshElement > FWireTranslatorImpl::FindOrAddMeshElement(AlDagNode& ShellNode, const FDagNodeInfo& ParentInfo, const char* ShaderName)
{
	uint32 ShellUUID = GetUUIDFromAIPersistentID(ShellNode);

	// Look if geometry has not been already processed, return it if found
	TSharedPtr< IDatasmithMeshElement >* MeshElementPtr = ShellUUIDToMeshElementMap.Find(ShellUUID);
	if (MeshElementPtr != nullptr)
	{
		return *MeshElementPtr;
	}

	FDagNodeInfo MeshInfo;
	GetDagNodeInfo(ShellNode, ParentInfo, MeshInfo);

	TSharedPtr< IDatasmithMeshElement > MeshElement = FDatasmithSceneFactory::CreateMesh(*MeshInfo.UEuuid);
	MeshElement->SetLabel(*MeshInfo.Label);
	MeshElement->SetLightmapSourceUV(-1);

// TODO
	// Get bounding box saved by GPure
	double BoundingBox[8][4];
	ShellNode.boundingBox(BoundingBox);
	//float BoundingBox[6];
	//FString Buffer = GetStringAttribute(GeomID, TEXT("UE_MESH_BBOX"));
	//if (FString::ToHexBlob(Buffer, (uint8*)BoundingBox, sizeof(BoundingBox)))
	//{
	//	MeshElement->SetDimensions(BoundingBox[3] - BoundingBox[0], BoundingBox[4] - BoundingBox[1], BoundingBox[5] - BoundingBox[2], 0.0f);
	//}
	 

	if (ShaderName)
	{
		TSharedPtr< IDatasmithMaterialIDElement > MaterialElement = ShaderNameToUEMaterialId[FString(ShaderName)];
		MeshElement->SetMaterial(MaterialElement->GetName(), 0);
	}

	DatasmithScene->AddMesh(MeshElement);

	ShellUUIDToMeshElementMap.Add(ShellUUID, MeshElement);
	MeshElementToAlDagNodeMap.Add(MeshElement.Get(), &ShellNode);

	return MeshElement;
}

bool FWireTranslatorImpl::ProcessAlShellNode(AlDagNode& ShellNode, const FDagNodeInfo& ParentInfo, const char* ShaderName)
{
	FDagNodeInfo ShellInfo;
	GetDagNodeInfo(ShellNode, ParentInfo, ShellInfo);

	TSharedPtr< IDatasmithMeshElement > MeshElement = FindOrAddMeshElement(ShellNode, ShellInfo, ShaderName);
	if (!MeshElement.IsValid())
	{
		return false;
	}

	TSharedPtr< IDatasmithMeshActorElement > ActorElement = FDatasmithSceneFactory::CreateMeshActor(*ShellInfo.UEuuid);
	if (!ActorElement.IsValid())
	{
		return false;
	}

	ActorElement->SetLabel(*ShellInfo.Label);
	ActorElement->SetStaticMeshPathName(MeshElement->GetName());
	ShellInfo.ActorElement = ActorElement;

	GetDagNodeMeta(ShellNode, ActorElement);

	SetActorTransform(ShellInfo.ActorElement, ShellNode);

	//// Apply materials on the current part 
	if (ShaderName)
	{
		TSharedPtr< IDatasmithMaterialIDElement > MaterialIDElement = ShaderNameToUEMaterialId[FString(ShaderName)];
		if (MaterialIDElement.IsValid()) {
			for (int32 Index = 0; Index < MeshElement->GetMaterialSlotCount(); ++Index)
			{
				MaterialIDElement->SetId(MeshElement->GetMaterialSlotAt(Index)->GetId());
				ActorElement->AddMaterialOverride(MaterialIDElement);
			}
		}
	}

	if (ActorElement.IsValid() && IsValidActor(ActorElement))
	{
		if (ParentInfo.ActorElement.IsValid())
		{
			ParentInfo.ActorElement->AddChild(ActorElement);
		}
		else
		{
			DatasmithScene->AddActor(ActorElement);
		}
	}	
	return true;
}

bool FWireTranslatorImpl::ProcessBodyNode(TSharedRef<BodyData> Body, const FDagNodeInfo& ParentInfo)
{
	if (Body->ShellSet.Num() == 1)
	{
		RecurseDagForLeavesNoMerge(Body->ShellSet[0], ParentInfo);
	}

	FDagNodeInfo ShellInfo;
	GetDagNodeInfo(Body, ParentInfo, ShellInfo);

	TSharedPtr< IDatasmithMeshElement > MeshElement = AddMeshElement(Body, ShellInfo);
	if (!MeshElement.IsValid())
	{
		return false;
	}

	TSharedPtr< IDatasmithMeshActorElement > ActorElement = FDatasmithSceneFactory::CreateMeshActor(*ShellInfo.UEuuid);
	if (!ActorElement.IsValid())
	{
		return false;
	}

	ActorElement->SetLabel(*ShellInfo.Label);
	ActorElement->SetStaticMeshPathName(MeshElement->GetName());
	ShellInfo.ActorElement = ActorElement;

	ActorElement->SetLayer(*Body->LayerName);

	//SetActorTransform(ShellInfo.ActorElement, ShellNode);

	//// Apply materials on the current part 
	if (*Body->ShaderName)
	{
		TSharedPtr< IDatasmithMaterialIDElement > MaterialIDElement = ShaderNameToUEMaterialId[FString(*Body->ShaderName)];
		if (MaterialIDElement.IsValid()) {
			for (int32 Index = 0; Index < MeshElement->GetMaterialSlotCount(); ++Index)
			{
				MaterialIDElement->SetId(MeshElement->GetMaterialSlotAt(Index)->GetId());
				ActorElement->AddMaterialOverride(MaterialIDElement);
			}
		}
	}

	if (ActorElement.IsValid() && IsValidActor(ActorElement))
	{
		if (ParentInfo.ActorElement.IsValid())
		{
			ParentInfo.ActorElement->AddChild(ActorElement);
		}
		else
		{
			DatasmithScene->AddActor(ActorElement);
		}
	}
	return true;
}

AlDagNode * GetNextNode(AlDagNode * DagNode)
{
	// Grab the next sibling before deleting the node.
	AlDagNode * SiblingNode = DagNode->nextNode();
	if (AlIsValid(SiblingNode))
	{
		return SiblingNode;
	}
	else
	{
		return NULL;
	}
}

bool IsHidden(AlDagNode * DagNode)
{
	/*
	AlObjectType objectType = DagNode->type();
	switch (objectType)
	{
		case kShellNodeType:
		case kSurfaceNodeType:
		case kMeshNodeType:
		{
			boolean isVisible = !DagNode->isDisplayModeSet(AlDisplayModeType::kDisplayModeInvisible);
			if (!isVisible)
			{
				return true;
			}

			AlLayer* Layer = DagNode->layer();
			if (Layer)
			{
				if (Layer->invisible())
				{
					return true;
				}
			}
		}
	}
	*/
	return false;
}


uint32 getBodySetId(const char* ShaderName, const char* LayerName, bool bCadData)
{
	uint32 UUID = HashCombine(GetTypeHash(ShaderName), GetTypeHash(bCadData));
	UUID = HashCombine(GetTypeHash(LayerName), UUID);
	return UUID;
}

uint32 getNumOfPatch(AlShell& Shell)
{
	uint32 NbFace = 0;
	AlTrimRegion *TrimRegion = Shell.firstTrimRegion();
	while (TrimRegion)
	{
		NbFace++;
		TrimRegion = TrimRegion->nextRegion();
	}
	return NbFace;
}

void FWireTranslatorImpl::AddNodeInBodySet(AlDagNode& DagNode, const char* ShaderName, TMap<uint32, TSharedPtr<BodyData>>& ShellToProcess, bool bIsAPatch, uint32 MaxSize)
{
	const char* LayerName = nullptr;
	if (AlLayer* Layer = DagNode.layer())
	{
		LayerName = Layer->name();
	}

	uint32 SetId = getBodySetId(ShaderName, LayerName, bIsAPatch);

	TSharedPtr<BodyData> Body;
	if (TSharedPtr<BodyData>* PBody = ShellToProcess.Find(SetId))
	{
		Body = *PBody;
	}
	else
	{
		TSharedRef<BodyData> BodyRef = MakeShared<BodyData>(ShaderName, LayerName, bIsAPatch);
		ShellToProcess.Add(SetId, BodyRef);
		BodyRef->ShellSet.Reserve(MaxSize);
		Body = BodyRef;
	}
	Body->ShellSet.Add(&DagNode);
}

bool FWireTranslatorImpl::RecurseDagForLeaves(AlDagNode* FirstDagNode, const FDagNodeInfo& ParentInfo)
{
	if (TessellationOptions.StitchingTechnique != EDatasmithCADStitchingTechnique::StitchingSew)
	{
		return RecurseDagForLeavesNoMerge(FirstDagNode, ParentInfo);
	}

	AlDagNode* DagNode = FirstDagNode;
	uint32 MaxSize = 0;
	while (DagNode)
	{
		MaxSize++;
		DagNode = GetNextNode(DagNode);
	}

	DagNode = FirstDagNode;

	TSet<AlDagNode*> UnProcessedNodes;
	TMap<uint32, TSharedPtr<BodyData>> ShellToProcess;

	const char* ShaderName = nullptr;

	while (DagNode)
	{
		// Filter invalid nodes.
		if (!AlIsValid(DagNode))
		{
			break;
		}
		if (IsHidden(DagNode))
		{
			return true;
		}

		AlObjectType objectType = DagNode->type();

		// Process the current node.
		switch (objectType)
		{
			// Push all leaf nodes into 'leaves'
		case kShellNodeType:
		{
			AlShellNode *ShellNode = DagNode->asShellNodePtr();
			AlShell *Shell = ShellNode->shell();
			uint32 NbPatch = getNumOfPatch(*Shell);
			if (NbPatch == 1)
			{
				if (AlShader * Shader = Shell->firstShader())
				{
					ShaderName = Shader->name();
				}
				AddNodeInBodySet(*DagNode, ShaderName, ShellToProcess, true, MaxSize);
			}
			else
			{
				ProcessAlShellNode(*DagNode, ParentInfo, ShaderName);
			}
			break;
		}
		case kSurfaceNodeType:
		{
			AlSurfaceNode *SurfaceNode = DagNode->asSurfaceNodePtr();
			AlSurface *Surface = SurfaceNode->surface();
			if (AlShader * Shader = Surface->firstShader())
			{
				ShaderName = Shader->name();
			}
			AddNodeInBodySet(*DagNode, ShaderName, ShellToProcess, true, MaxSize);
			break;
		}

		case kMeshNodeType:
		{
			AlMeshNode *MeshNode = DagNode->asMeshNodePtr();
			AlMesh *Mesh = MeshNode->mesh();
			if (AlShader * Shader = Mesh->firstShader())
			{
				ShaderName = Shader->name();
			}
			AddNodeInBodySet(*DagNode, ShaderName, ShellToProcess, false, MaxSize);
			break;
		}

		// Traverse down through groups
		case kGroupNodeType:
		{
			AlGroupNode * GroupNode = DagNode->asGroupNodePtr();
			if (AlIsValid(GroupNode))
			{
				ProcessAlGroupNode(*GroupNode, ParentInfo);
			}
			break;
		}
		default:
		{
		}

		}
		DagNode = GetNextNode(DagNode);
	}

	for (auto Body : ShellToProcess)
	{
		ProcessBodyNode(Body.Value.ToSharedRef(), ParentInfo);
	}
	return true;
}

bool FWireTranslatorImpl::RecurseDagForLeavesNoMerge(AlDagNode* FirstDagNode, const FDagNodeInfo& ParentInfo)
{
	AlDagNode* DagNode = FirstDagNode;

	TSet<AlDagNode*> UnProcessedNodes;
	TMap<uint32, BodyData> ShellToProcess;

	const char* ShaderName = nullptr;

	DagNode = FirstDagNode;
	while (DagNode)
	{
		// Filter invalid nodes.
		if (AlIsValid(DagNode) == FALSE)
		{
			break;
		}

		if (IsHidden(DagNode))
		{
			return true;
		}

		// Process the current node.
		AlObjectType objectType = DagNode->type();
		switch (objectType)
		{
			// Push all leaf nodes into 'leaves'
		case kShellNodeType:
		{
			AlShellNode *ShellNode = DagNode->asShellNodePtr();
			AlShell *Shell = ShellNode->shell();
			if (AlShader * Shader = Shell->firstShader())
			{
				ShaderName = Shader->name();
			}

			ProcessAlShellNode(*DagNode, ParentInfo, ShaderName);
			break;
		}
		case kSurfaceNodeType:
		{
			AlSurfaceNode *SurfaceNode = DagNode->asSurfaceNodePtr();
			AlSurface *Surface = SurfaceNode->surface();
			if (AlShader * Shader = Surface->firstShader())
			{
				ShaderName = Shader->name();
			}

			ProcessAlShellNode(*DagNode, ParentInfo, ShaderName);
			break;
		}

		case kMeshNodeType:
		{
			AlMeshNode *MeshNode = DagNode->asMeshNodePtr();
			AlMesh *Mesh = MeshNode->mesh();
			if (AlShader * Shader = Mesh->firstShader())
			{
				ShaderName = Shader->name();
			}

			ProcessAlShellNode(*DagNode, ParentInfo, ShaderName);
			break;
		}

		// Traverse down through groups
		case kGroupNodeType:
		{
			AlGroupNode * GroupNode = DagNode->asGroupNodePtr();
			if (AlIsValid(GroupNode))
			{
				ProcessAlGroupNode(*GroupNode, ParentInfo);
			}
			break;
		}

		default:
		{
		}

		}

		DagNode = GetNextNode(DagNode);
	}
	return true;
}

#ifdef CAD_LIBRARY

TOptional<FMeshDescription> FWireTranslatorImpl::MeshDagNodeWithExternalMesher(AlDagNode& DagNode, TSharedRef<IDatasmithMeshElement> MeshElement, FMeshParameters& MeshParameters)
{
	CADLibrary::CheckedCTError Result;

	LocalSession->ClearData();

	FString Filename = DagNode.name();

	TArray<AlDagNode*> DagNodeSet;
	DagNodeSet.Add(&DagNode);
	Result = LocalSession->AddBRep(DagNodeSet, MeshParameters.bIsSymmetric);

	Filename += TEXT(".ct");

	FString FilePath = FPaths::Combine(OutputPath, Filename);
	Result = LocalSession->SaveBrep(FilePath);
	if (Result)
	{
		MeshElement->SetFile(*FilePath);
	}

	//Result = LocalSession->TopoFixes();

	FMeshDescription MeshDescription;
	DatasmithMeshHelper::PrepareAttributeForStaticMesh(MeshDescription);

	Result = LocalSession->Tessellate(MeshDescription, MeshParameters);

	return MoveTemp(MeshDescription);
}

TOptional<FMeshDescription> FWireTranslatorImpl::MeshDagNodeWithExternalMesher(TSharedRef<BodyData> Body, TSharedRef<IDatasmithMeshElement> MeshElement, FMeshParameters& MeshParameters)
{
	CADLibrary::CheckedCTError Result;

	LocalSession->ClearData();

	Result = LocalSession->AddBRep(Body->ShellSet, MeshParameters.bIsSymmetric);

	//const char*Name = DagNode->name();
	FString Filename = Body->Label;
	Filename += TEXT(".ct");

	FString FilePath = FPaths::Combine(OutputPath, Filename);
	Result = LocalSession->SaveBrep(FilePath);
	if (Result)
	{
		MeshElement->SetFile(*FilePath);
	}

	//Result = LocalSession->TopoFixes();

	FMeshDescription MeshDescription;
	DatasmithMeshHelper::PrepareAttributeForStaticMesh(MeshDescription);

	Result = LocalSession->Tessellate(MeshDescription, MeshParameters);

	return MoveTemp(MeshDescription);
}

#endif

TOptional<FMeshDescription> FWireTranslatorImpl::GetMeshOfShellNode(AlDagNode& DagNode, TSharedRef<IDatasmithMeshElement> MeshElement, FMeshParameters& MeshParameters)
{
	static bool bUseExternalMesher = true;
#ifdef CAD_LIBRARY
	if (bUseExternalMesher)
	{
		TOptional< FMeshDescription > UEMesh = MeshDagNodeWithExternalMesher(DagNode, MeshElement, MeshParameters);
		return UEMesh;
	}
	else
#endif
	{
		AlMatrix4x4 AlMatrix;
		DagNode.inverseGlobalTransformationMatrix(AlMatrix);
		// TODO: the best way, should be to don't have to apply inverse global transform to the generated mesh
		AlDagNode *TesselatedNode = TesselateDagLeaf(&DagNode, ETesselatorType::Fast, TessellationOptions.ChordTolerance);
		if (TesselatedNode != nullptr)
		{
			// Get the meshes from the dag nodes. Note that removing the mesh's DAG.
			// will also removes the meshes, so we have to do it later.
			TOptional< FMeshDescription > UEMesh = GetMeshOfNodeMesh(*(TesselatedNode->asMeshNodePtr()), MeshElement, MeshParameters, &AlMatrix);
			delete TesselatedNode;
			return UEMesh;
		}
	}
	return TOptional< FMeshDescription >();
}


TOptional<FMeshDescription> FWireTranslatorImpl::GetMeshOfShellBody(TSharedRef<BodyData> Body, TSharedRef<IDatasmithMeshElement> MeshElement, FMeshParameters& MeshParameters)
{
	TOptional< FMeshDescription > UEMesh = MeshDagNodeWithExternalMesher(Body, MeshElement, MeshParameters);
	return UEMesh;
}

TOptional<FMeshDescription> FWireTranslatorImpl::GetMeshOfMeshBody(TSharedRef<BodyData> Body, TSharedRef<IDatasmithMeshElement> MeshElement, FMeshParameters& MeshParameters)
{
	FMeshDescription MeshDescription;
	DatasmithMeshHelper::PrepareAttributeForStaticMesh(MeshDescription);
	bool True = true;

	for (auto DagNode : Body->ShellSet)
	{
		AlMeshNode *MeshNode = DagNode->asMeshNodePtr();
		AlMesh *Mesh = MeshNode->mesh();		
		if (Mesh)
		{
			//TransferAlMeshToMeshDescription(*Mesh, MeshDescription, MeshParameters, True);
			// TODO
		}
	}

	return MoveTemp(MeshDescription);
}

TOptional<FMeshDescription> FWireTranslatorImpl::GetMeshOfNodeMesh(AlMeshNode& MeshNode, TSharedRef<IDatasmithMeshElement> MeshElement, FMeshParameters& MeshParameters, AlMatrix4x4* AlMeshInvGlobalMatrix)
{
	AlMesh * Mesh = MeshNode.mesh();
	if (AlIsValid(Mesh))
	{
		if (AlMeshInvGlobalMatrix != nullptr)
		{
			Mesh->transform(*AlMeshInvGlobalMatrix);
		}
		return ImportMesh(*Mesh, MeshParameters);
	}
	return TOptional< FMeshDescription >();
}


TOptional<FMeshDescription> FWireTranslatorImpl::GetMeshDescription(TSharedRef<IDatasmithMeshElement> MeshElement, FMeshParameters& MeshParameters, TSharedRef<BodyData> Body)
{
	if(Body->ShellSet.Num() == 0 )
	{
		return TOptional< FMeshDescription >();
	}

	AlDagNode& DagNode = *Body->ShellSet[0];

	if (DagNode.layer() && DagNode.layer()->isSymmetric())
	{
		MeshParameters.bIsSymmetric = true;
		double Normal[3], Origin[3];
		DagNode.layer()->symmetricNormal(Normal[0], Normal[1], Normal[2]);
		DagNode.layer()->symmetricOrigin(Origin[0], Origin[1], Origin[2]);

		MeshParameters.SymmetricOrigin.X = (float)Origin[0];
		MeshParameters.SymmetricOrigin.Y = (float)Origin[1];
		MeshParameters.SymmetricOrigin.Z = (float)Origin[2];
		MeshParameters.SymmetricNormal.X = (float)Normal[0];
		MeshParameters.SymmetricNormal.Y = (float)Normal[1];
		MeshParameters.SymmetricNormal.Z = (float)Normal[2];
	}

	if(Body->bCadData)
	{
		return GetMeshOfShellBody(Body, MeshElement, MeshParameters);
	} 
	else
	{
		return GetMeshOfMeshBody(Body, MeshElement, MeshParameters);
	}
}


TOptional<FMeshDescription> FWireTranslatorImpl::GetMeshDescription(TSharedRef<IDatasmithMeshElement> MeshElement, FMeshParameters& MeshParameters)
{
	AlDagNode ** DagNodeTemp = MeshElementToAlDagNodeMap.Find(&MeshElement.Get());
	if (DagNodeTemp == nullptr || *DagNodeTemp == nullptr)
	{
		TSharedPtr<BodyData>* BodyTemp = MeshElementToBodyMap.Find(&MeshElement.Get());
		if (BodyTemp != nullptr && (*BodyTemp).IsValid())
		{
			return GetMeshDescription(MeshElement, MeshParameters, (*BodyTemp).ToSharedRef());
		}

		return TOptional< FMeshDescription >();
	}

	AlDagNode& DagNode = **DagNodeTemp;
	AlObjectType objectType = DagNode.type();

	if (objectType == kShellNodeType || objectType == kSurfaceNodeType || objectType == kMeshNodeType)
	{
		boolean bAlOrientation;
		DagNode.getSurfaceOrientation(bAlOrientation);
		MeshParameters.bNeedSwapOrientation = (bool)bAlOrientation;

		if (DagNode.layer() && DagNode.layer()->isSymmetric())
		{
			MeshParameters.bIsSymmetric = true;
			double Normal[3], Origin[3];
			DagNode.layer()->symmetricNormal(Normal[0], Normal[1], Normal[2]);
			DagNode.layer()->symmetricOrigin(Origin[0], Origin[1], Origin[2]);

			MeshParameters.SymmetricOrigin.X = (float)Origin[0];
			MeshParameters.SymmetricOrigin.Y = (float)Origin[1];
			MeshParameters.SymmetricOrigin.Z = (float)Origin[2];
			MeshParameters.SymmetricNormal.X = (float)Normal[0];
			MeshParameters.SymmetricNormal.Y = (float)Normal[1];
			MeshParameters.SymmetricNormal.Z = (float)Normal[2];
		}
	}

	switch (objectType)
	{
		case kShellNodeType:
		case kSurfaceNodeType:
		{
			return GetMeshOfShellNode(DagNode, MeshElement, MeshParameters);
			break;
		}

		case kMeshNodeType:
		{
			return GetMeshOfNodeMesh(*(DagNode.asMeshNodePtr()), MeshElement, MeshParameters);
			break;
		}
	}

	return TOptional< FMeshDescription >();
}


// Note that Alias file unit is cm like UE
TOptional< FMeshDescription > FWireTranslatorImpl::ImportMesh(AlMesh& CurrentMesh, FMeshParameters& MeshParameters)
{

	FMeshDescription MeshDescription;
	DatasmithMeshHelper::PrepareAttributeForStaticMesh(MeshDescription);
	bool True = true;
	TransferAlMeshToMeshDescription(CurrentMesh, MeshDescription, MeshParameters, True);

	return MoveTemp(MeshDescription);
}

#endif

//////////////////////////////////////////////////////////////////////////
// UDatasmithWireTranslator
//////////////////////////////////////////////////////////////////////////

FDatasmithWireTranslator::FDatasmithWireTranslator()
	: Translator(nullptr)
{
}

void FDatasmithWireTranslator::Initialize(FDatasmithTranslatorCapabilities& OutCapabilities)
{
#ifdef USE_OPENMODEL
	if (FPlatformProcess::GetDllHandle(TEXT("libalias_api.dll")))
	{
		OutCapabilities.SupportedFileFormats.Add(FFileFormatInfo{ TEXT("wire"), TEXT("AliasStudio, Model files") });
		return;
	} 
#endif
	OutCapabilities.bIsEnabled = false;
}

bool FDatasmithWireTranslator::IsSourceSupported(const FDatasmithSceneSource& Source)
{
#ifdef USE_OPENMODEL
	return true;
#else
	return false;
#endif
}

bool FDatasmithWireTranslator::LoadScene(TSharedRef<IDatasmithScene> OutScene)
{
#ifdef USE_OPENMODEL
	const FString& Filename = GetSource().GetSourceFile();

	Translator = MakeShared<FWireTranslatorImpl>(Filename, OutScene);
	if (!Translator)
	{
		return false;
	}

	FString OutputPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FDatasmithWireTranslatorModule::Get().GetTempDir(), TEXT("Cache"), GetSource().GetSceneName()));
	IFileManager::Get().MakeDirectory(*OutputPath, true);
	Translator->SetOutputPath(OutputPath);

	Translator->SetTessellationOptions(GetCommonTessellationOptions());

	return Translator->Read();

#else
	return false;
#endif
}

void FDatasmithWireTranslator::UnloadScene()
{
}

bool FDatasmithWireTranslator::LoadStaticMesh(const TSharedRef<IDatasmithMeshElement> MeshElement, FDatasmithMeshElementPayload& OutMeshPayload)
{
#ifdef USE_OPENMODEL

	FImportParameters& ImportParameters = Translator->GetImportParameters();
	FMeshParameters MeshParameters;
	if (TOptional< FMeshDescription > Mesh = Translator->GetMeshDescription(MeshElement, MeshParameters))
	{
		OutMeshPayload.LodMeshes.Add(MoveTemp(Mesh.GetValue()));

#ifdef CAD_LIBRARY
		// Store CoreTech additional data if provided
		const TCHAR* CoretechFile = MeshElement->GetFile();
		if (FPaths::FileExists(CoretechFile))
		{
			TArray<uint8> ByteArray;
			if (FFileHelper::LoadFileToArray(ByteArray, CoretechFile))
			{
				UCoreTechParametricSurfaceData* CoreTechData = Datasmith::MakeAdditionalData<UCoreTechParametricSurfaceData>();
				CoreTechData->SourceFile = CoretechFile;
				CoreTechData->RawData = MoveTemp(ByteArray);
				CoreTechData->SceneParameters.ModelCoordSys = uint8(FDatasmithUtils::EModelCoordSystem::ZUp_RightHanded);
				CoreTechData->SceneParameters.MetricUnit = ImportParameters.MetricUnit;
				CoreTechData->SceneParameters.ScaleFactor = ImportParameters.ScaleFactor;

				CoreTechData->MeshParameters.bNeedSwapOrientation = MeshParameters.bNeedSwapOrientation;
				CoreTechData->MeshParameters.bIsSymmetric = MeshParameters.bIsSymmetric;
				CoreTechData->MeshParameters.SymmetricNormal = MeshParameters.SymmetricNormal;
				CoreTechData->MeshParameters.SymmetricOrigin = MeshParameters.SymmetricOrigin;

				CoreTechData->LastTessellationOptions = GetCommonTessellationOptions();

				OutMeshPayload.AdditionalData.Add(CoreTechData);
			}
		}
#endif

	}
	return OutMeshPayload.LodMeshes.Num() > 0;
#else
	return false;
#endif
}

void FDatasmithWireTranslator::SetSceneImportOptions(TArray<TStrongObjectPtr<UObject>>& Options)
{
#ifdef USE_OPENMODEL
	FDatasmithCoreTechTranslator::SetSceneImportOptions(Options);

	if (Translator)
	{
		Translator->SetTessellationOptions( GetCommonTessellationOptions() );
	}
#endif
}




