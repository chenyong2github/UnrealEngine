// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithWireTranslator.h"

#include "Containers/List.h"
#include "DatasmithImportOptions.h"
#include "DatasmithSceneFactory.h"
#include "DatasmithTranslator.h"
#include "DatasmithUtils.h"
#include "DatasmithWireTranslatorModule.h"
#include "IDatasmithSceneElements.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "OpenModelUtils.h"
#include "Utility/DatasmithMeshHelper.h"

#include "StaticMeshDescription.h"
#include "StaticMeshOperations.h"

#if WITH_EDITOR
#include "Editor.h"
#include "IMessageLogListing.h"
#include "Logging/TokenizedMessage.h"
#include "MessageLogModule.h"
#include "Modules/ModuleManager.h"
#endif

#include "AliasCoretechWrapper.h" // requires CoreTech as public dependency
#include "CADInterfacesModule.h"
#include "CoreTechParametricSurfaceExtension.h"

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

DEFINE_LOG_CATEGORY_STATIC(LogDatasmithWireTranslator, Log, All);

#define LOCTEXT_NAMESPACE "DatasmithWireTranslator"

#define WRONG_VERSION_TEXT "Unsupported version of Alias detected. Please downgrade to Alias 2020.0 (or earlier version) or upgrade to Alias 2021 (or later version)."
#define CAD_INTERFACE_UNAVAILABLE "CAD Interface module is unavailable. Meshing will be done by Alias."

#ifdef USE_OPENMODEL

using namespace OpenModelUtils;
using namespace CADLibrary;

const uint64 LibAlias2020_Version = 7318349414924288;
const uint64 LibAlias2021_Version = 7599824377020416;
const uint64 LibAlias2021_3_0_Version = 7599824424206339;


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

	// Generates BodyData's unique id from AlDagNode objects
	FString GetUUID(const FString& ParentUUID)
	{
		if(ShellSet.Num() == 0)
		{
			return ParentUUID;
		}

		auto GetLongPersistentID = []( AlDagNode& DagNode ) -> int64
		{
			union {
				int a[2];
				int64 b;
			} Value;

			Value.b = -1;

			AlPersistentID* PersistentID = nullptr;
			DagNode.persistentID( PersistentID );

			if( PersistentID != nullptr )
			{
				int Dummy;
				PersistentID->id( Value.a[0], Value.a[1], Dummy, Dummy );
			}

			return Value.b;
		};

		if(ShellSet.Num() > 1)
		{
			ShellSet.Sort([&](AlDagNode& A, AlDagNode& B)
			{
				return GetLongPersistentID( A ) < GetLongPersistentID( B );
			});
		}

		FString Buffer;
		for(AlDagNode* DagNode : ShellSet)
		{
			Buffer += FString::Printf(TEXT("%016lx"), GetLongPersistentID( *DagNode ) );
		}

		return GetUEUUIDFromAIPersistentID( ParentUUID, Buffer );
	}
};

uint32 GetSceneFileHash(const FString& FullPath, const FString& FileName)
{
	FFileStatData FileStatData = IFileManager::Get().GetStatData(*FullPath);

	int64 FileSize = FileStatData.FileSize;
	FDateTime ModificationTime = FileStatData.ModificationTime;

	uint32 FileHash = GetTypeHash(FileName);
	FileHash = HashCombine(FileHash, GetTypeHash(FileSize));
	FileHash = HashCombine(FileHash, GetTypeHash(ModificationTime));

	return FileHash;
}

class FWireTranslatorImpl
{
public:
	FWireTranslatorImpl(const FString& InSceneFullName, TSharedRef<IDatasmithScene> InScene)
		: DatasmithScene(InScene)
		, SceneName(FPaths::GetBaseFilename(InSceneFullName))
		, CurrentPath(FPaths::GetPath(InSceneFullName))
		, SceneFullPath(InSceneFullName)
		, SceneFileHash(0)
		, AlRootNode(nullptr)
		, FileVersion(0)
		, ArchiveWireVersion(0)
		, FileLength(0)
		, NumCRCErrors(0)
	{
		// Set ProductName, ProductVersion in DatasmithScene for Analytics purpose
		DatasmithScene->SetHost(TEXT("Alias"));
		DatasmithScene->SetVendor(TEXT("Autodesk"));
		DatasmithScene->SetExporterSDKVersion(TEXT("2022"));
		DatasmithScene->SetProductName(TEXT("Alias Tools"));
		DatasmithScene->SetProductVersion(TEXT("Alias 2022"));

		LocalSession = FAliasCoretechWrapper::GetSharedSession();
	}

	~FWireTranslatorImpl()
	{
		for (AlDagNode* Node : AlDagNodeArray)
		{
			delete Node;
		}
		AlUniverse::deleteAll();
		LocalSession.Reset();
	}

	bool Read();
	TOptional<FMeshDescription> GetMeshDescription(TSharedRef<IDatasmithMeshElement> MeshElement, CADLibrary::FMeshParameters& MeshParameters);
	TOptional<FMeshDescription> GetMeshDescription(TSharedRef<IDatasmithMeshElement> MeshElement, CADLibrary::FMeshParameters& MeshParameters, TSharedRef<BodyData> BodyTemp);

	void SetTessellationOptions(const FDatasmithTessellationOptions& Options);
	void SetOutputPath(const FString& Path) { OutputPath = Path; }

	CADLibrary::FImportParameters& GetImportParameters()
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
	TSharedPtr< IDatasmithMeshElement > FindOrAddMeshElement(TSharedRef<BodyData> Body, const FDagNodeInfo& ParentInfo);
	TSharedPtr< IDatasmithMeshElement > FindOrAddMeshElement(AlDagNode& ShellNode, const FDagNodeInfo& ParentInfo, const char* ShaderName);
	void GetDagNodeInfo(AlDagNode& GroupNode, const FDagNodeInfo& ParentInfo, FDagNodeInfo& CurrentNodeInfo);
	void GetDagNodeInfo(TSharedRef<BodyData> CurrentNode, const FDagNodeInfo& ParentInfo, FDagNodeInfo& CurrentNodeInfo);
	void GetDagNodeMeta(AlDagNode& CurrentNode, TSharedPtr< IDatasmithActorElement > ActorElement);

	TOptional<FMeshDescription> GetMeshOfShellNode(AlDagNode& DagNode, TSharedRef<IDatasmithMeshElement> MeshElement, CADLibrary::FMeshParameters& MeshParameters);
	TOptional<FMeshDescription> GetMeshOfNodeMesh(AlMeshNode& DagNode, TSharedRef<IDatasmithMeshElement> MeshElement, CADLibrary::FMeshParameters& MeshParameters, AlMatrix4x4* AlMeshInvGlobalMatrix = nullptr);
	TOptional<FMeshDescription> GetMeshOfShellBody(TSharedRef<BodyData> DagNode, TSharedRef<IDatasmithMeshElement> MeshElement, CADLibrary::FMeshParameters& MeshParameters);
	TOptional<FMeshDescription> GetMeshOfMeshBody(TSharedRef<BodyData> DagNode, TSharedRef<IDatasmithMeshElement> MeshElement, CADLibrary::FMeshParameters& MeshParameters);

	void AddNodeInBodySet(AlDagNode& DagNode, const char* ShaderName, TMap<uint32, TSharedPtr<BodyData>>& ShellToProcess, bool bIsAPatch, uint32 MaxSize);

	TOptional<FMeshDescription> MeshDagNodeWithExternalMesher(AlDagNode& DagNode, TSharedRef<IDatasmithMeshElement> MeshElement, CADLibrary::FMeshParameters& MeshParameters);
	TOptional<FMeshDescription> MeshDagNodeWithExternalMesher(TSharedRef<BodyData> DagNode, TSharedRef<IDatasmithMeshElement> MeshElement, CADLibrary::FMeshParameters& MeshParameters);

 	TOptional< FMeshDescription > ImportMesh(AlMesh& Mesh, CADLibrary::FMeshParameters& MeshParameters);

	FORCEINLINE bool IsTransparent(FColor& TransparencyColor)
	{
		float Opacity = 1.0f - ((float)(TransparencyColor.R + TransparencyColor.G + TransparencyColor.B)) / 765.0f;
		return !FMath::IsNearlyEqual(Opacity, 1.0f);
	}

	FORCEINLINE bool GetCommonParameters(AlShadingFields Field, double Value, FColor& Color, FColor& TransparencyColor, FColor& IncandescenceColor, double GlowIntensity)
	{
		switch (Field)
		{
		case AlShadingFields::kFLD_SHADING_COMMON_COLOR_R:
			Color.R = (uint8)Value;
			return true;
		case AlShadingFields::kFLD_SHADING_COMMON_COLOR_G:
			Color.G = (uint8)Value;
			return true;
		case  AlShadingFields::kFLD_SHADING_COMMON_COLOR_B:
			Color.B = (uint8)Value;
			return true;
		case AlShadingFields::kFLD_SHADING_COMMON_INCANDESCENCE_R:
			IncandescenceColor.R = (uint8)Value;
			return true;
		case AlShadingFields::kFLD_SHADING_COMMON_INCANDESCENCE_G:
			IncandescenceColor.G = (uint8)Value;
			return true;
		case AlShadingFields::kFLD_SHADING_COMMON_INCANDESCENCE_B:
			IncandescenceColor.B = (uint8)Value;
			return true;
		case  AlShadingFields::kFLD_SHADING_COMMON_TRANSPARENCY_R:
			TransparencyColor.R = (uint8)Value;
			return true;
		case  AlShadingFields::kFLD_SHADING_COMMON_TRANSPARENCY_G:
			TransparencyColor.G = (uint8)Value;
			return true;
		case  AlShadingFields::kFLD_SHADING_COMMON_TRANSPARENCY_B:
			TransparencyColor.B = (uint8)Value;
			return true;
		case AlShadingFields::kFLD_SHADING_COMMON_GLOW_INTENSITY:
			GlowIntensity = Value;
			return true;
		default :
			return false;
		}
	}

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
	// Hash value of the scene file used to check if the file has been modified for re-import
	uint32 SceneFileHash;

	AlDagNode* AlRootNode;

	/** Table of correspondence between mesh identifier and associated Datasmith mesh element */
	TMap< uint32, TSharedPtr< IDatasmithMeshElement > > ShellUUIDToMeshElementMap;
	TMap< FString, TSharedPtr< IDatasmithMeshElement > > BodyToMeshElementMap;

	/** All DagNode to delete */
	TArray<AlDagNode*> AlDagNodeArray;

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

	TSharedPtr<FAliasCoretechWrapper> LocalSession;
};

void FWireTranslatorImpl::SetTessellationOptions(const FDatasmithTessellationOptions& Options)
{
	TessellationOptions = Options;
	SceneFileHash = HashCombine(Options.GetHash(), GetSceneFileHash(SceneFullPath, SceneName));
}

bool FWireTranslatorImpl::Read()
{
	// Initialize Alias.
	AlUniverse::initialize();

	if (AlUniverse::retrieve(TCHAR_TO_UTF8(*SceneFullPath)) != sSuccess)
	{
		return false;
	}

	LocalSession->SetImportParameters(TessellationOptions.ChordTolerance, TessellationOptions.MaxEdgeLength, TessellationOptions.NormalTolerance, (CADLibrary::EStitchingTechnique) TessellationOptions.StitchingTechnique, true);

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

void FWireTranslatorImpl::AddAlBlinnParameters(AlShader *Shader, TSharedRef<IDatasmithUEPbrMaterialElement> MaterialElement)
{
	// Default values for a Blinn material
	FColor Color(145, 148, 153);
	FColor TransparencyColor(0, 0, 0);
	FColor IncandescenceColor(0, 0, 0);
	FColor SpecularColor(38, 38, 38);
	double Diffuse = 1.0;
	double GlowIntensity = 0.0;
	double Gloss = 0.8;
	double Eccentricity = 0.35;
	double Specularity = 1.0;
	double Reflectivity = 0.5;
	double SpecularRolloff = 0.5;

	AlList* List = Shader->fields();
	for (AlShadingFieldItem* Item = static_cast<AlShadingFieldItem *>(List->first()); Item; Item = Item->nextField())
	{
		double Value = 0.0f;
		statusCode ErrorCode = Shader->parameter(Item->field(), Value);
		if (ErrorCode != 0)
		{
			continue;
		}

		if (GetCommonParameters(Item->field(), Value, Color, TransparencyColor, IncandescenceColor, GlowIntensity))
		{
			continue;
		}

		switch (Item->field())
		{
		case AlShadingFields::kFLD_SHADING_BLINN_DIFFUSE:
			Diffuse = Value;
			break;
		case AlShadingFields::kFLD_SHADING_BLINN_GLOSS_:
			Gloss = Value;
			break;
		case AlShadingFields::kFLD_SHADING_BLINN_SPECULAR_R:
			SpecularColor.R = (uint8) (255.f * Value);
			break;
		case AlShadingFields::kFLD_SHADING_BLINN_SPECULAR_G:
			SpecularColor.G = (uint8)(255.f * Value);;
			break;
		case AlShadingFields::kFLD_SHADING_BLINN_SPECULAR_B:
			SpecularColor.B = (uint8)(255.f * Value);;
			break;
		case AlShadingFields::kFLD_SHADING_BLINN_SPECULARITY_:
			Specularity = Value;
			break;
		case AlShadingFields::kFLD_SHADING_BLINN_SPECULAR_ROLLOFF:
			SpecularRolloff = Value;
			break;
		case AlShadingFields::kFLD_SHADING_BLINN_ECCENTRICITY:
			Eccentricity = Value;
			break;
		case AlShadingFields::kFLD_SHADING_BLINN_REFLECTIVITY:
			Reflectivity = Value;
			break;
		}
	}

	bool bIsTransparent = IsTransparent(TransparencyColor);

	// Construct parameter expressions
	IDatasmithMaterialExpressionScalar* DiffuseExpression = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionScalar>();
	DiffuseExpression->GetScalar() = Diffuse;
	DiffuseExpression->SetName(TEXT("Diffuse"));

	IDatasmithMaterialExpressionScalar* GlossExpression = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionScalar>();
	GlossExpression->GetScalar() = Gloss;
	GlossExpression->SetName(TEXT("Gloss"));

	IDatasmithMaterialExpressionColor* SpecularColorExpression = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionColor>();
	SpecularColorExpression->SetName(TEXT("SpecularColor"));
	SpecularColorExpression->GetColor() = FLinearColor::FromSRGBColor(SpecularColor);

	IDatasmithMaterialExpressionScalar* SpecularityExpression = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionScalar>();
	SpecularityExpression->GetScalar() = Specularity * 0.3;
	SpecularityExpression->SetName(TEXT("Specularity"));

	IDatasmithMaterialExpressionScalar* SpecularRolloffExpression = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionScalar>();
	SpecularRolloffExpression->GetScalar() = SpecularRolloff;
	SpecularRolloffExpression->SetName(TEXT("SpecularRolloff"));

	IDatasmithMaterialExpressionScalar* EccentricityExpression = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionScalar>();
	EccentricityExpression->GetScalar() = Eccentricity;
	EccentricityExpression->SetName(TEXT("Eccentricity"));

	IDatasmithMaterialExpressionScalar* ReflectivityExpression = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionScalar>();
	ReflectivityExpression->GetScalar() = Reflectivity;
	ReflectivityExpression->SetName(TEXT("Reflectivity"));

	IDatasmithMaterialExpressionColor* ColorExpression = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionColor>();
	ColorExpression->SetName(TEXT("Color"));
	ColorExpression->GetColor() = FLinearColor::FromSRGBColor(Color);

	IDatasmithMaterialExpressionColor* IncandescenceColorExpression = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionColor>();
	IncandescenceColorExpression->SetName(TEXT("IncandescenceColor"));
	IncandescenceColorExpression->GetColor() = FLinearColor::FromSRGBColor(IncandescenceColor);

	IDatasmithMaterialExpressionColor* TransparencyColorExpression = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionColor>();
	TransparencyColorExpression->SetName(TEXT("TransparencyColor"));
	TransparencyColorExpression->GetColor() = FLinearColor::FromSRGBColor(TransparencyColor);

	IDatasmithMaterialExpressionScalar* GlowIntensityExpression = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionScalar>();
	GlowIntensityExpression->GetScalar() = GlowIntensity;
	GlowIntensityExpression->SetName(TEXT("GlowIntensity"));

	// Create aux expressions
	IDatasmithMaterialExpressionGeneric* ColorSpecLerp = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionGeneric>();
	ColorSpecLerp->SetExpressionName(TEXT("LinearInterpolate"));

	IDatasmithMaterialExpressionScalar* ColorSpecLerpValue = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionScalar>();
	ColorSpecLerpValue->GetScalar() = 0.96f;

	IDatasmithMaterialExpressionGeneric* ColorMetallicLerp = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionGeneric>();
	ColorMetallicLerp->SetExpressionName(TEXT("LinearInterpolate"));

	IDatasmithMaterialExpressionGeneric* DiffuseLerp = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionGeneric>();
	DiffuseLerp->SetExpressionName(TEXT("LinearInterpolate"));

	IDatasmithMaterialExpressionScalar* DiffuseLerpA = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionScalar>();
	DiffuseLerpA->GetScalar() = 0.04f;

	IDatasmithMaterialExpressionScalar* DiffuseLerpB = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionScalar>();
	DiffuseLerpB->GetScalar() = 1.0f;

	IDatasmithMaterialExpressionGeneric* BaseColorMultiply = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionGeneric>();
	BaseColorMultiply->SetExpressionName(TEXT("Multiply"));

	IDatasmithMaterialExpressionGeneric* BaseColorAdd = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionGeneric>();
	BaseColorAdd->SetExpressionName(TEXT("Add"));

	IDatasmithMaterialExpressionGeneric* BaseColorTransparencyMultiply = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionGeneric>();
	BaseColorTransparencyMultiply->SetExpressionName(TEXT("Multiply"));

	IDatasmithMaterialExpressionGeneric* IncandescenceMultiply = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionGeneric>();
	IncandescenceMultiply->SetExpressionName(TEXT("Multiply"));

	IDatasmithMaterialExpressionGeneric* IncandescenceScaleMultiply = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionGeneric>();
	IncandescenceScaleMultiply->SetExpressionName(TEXT("Multiply"));

	IDatasmithMaterialExpressionScalar* IncandescenceScale = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionScalar>();
	IncandescenceScale->GetScalar() = 100.0f;

	IDatasmithMaterialExpressionGeneric* EccentricityMultiply = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionGeneric>();
	EccentricityMultiply->SetExpressionName(TEXT("Multiply"));

	IDatasmithMaterialExpressionGeneric* EccentricityOneMinus = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionGeneric>();
	EccentricityOneMinus->SetExpressionName(TEXT("OneMinus"));

	IDatasmithMaterialExpressionGeneric* RoughnessOneMinus = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionGeneric>();
	RoughnessOneMinus->SetExpressionName(TEXT("OneMinus"));

	IDatasmithMaterialExpressionScalar* FresnelExponent = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionScalar>();
	FresnelExponent->GetScalar() = 4.0f;

	IDatasmithMaterialExpressionFunctionCall* FresnelFunc = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionFunctionCall>();
	FresnelFunc->SetFunctionPathName(TEXT("/Engine/Functions/Engine_MaterialFunctions02/Fresnel_Function.Fresnel_Function"));

	IDatasmithMaterialExpressionGeneric* FresnelLerp = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionGeneric>();
	FresnelLerp->SetExpressionName(TEXT("LinearInterpolate"));

	IDatasmithMaterialExpressionScalar* FresnelLerpA = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionScalar>();
	FresnelLerpA->GetScalar() = 1.0f;

	IDatasmithMaterialExpressionScalar* SpecularPowerExp = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionScalar>();
	SpecularPowerExp->GetScalar() = 0.5f;

	IDatasmithMaterialExpressionGeneric* Power = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionGeneric>();
	Power->SetExpressionName(TEXT("Power"));

	IDatasmithMaterialExpressionGeneric* FresnelMultiply = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionGeneric>();
	FresnelMultiply->SetExpressionName(TEXT("Multiply"));

	IDatasmithMaterialExpressionGeneric* TransparencyOneMinus = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionGeneric>();
	TransparencyOneMinus->SetExpressionName(TEXT("OneMinus"));

	IDatasmithMaterialExpressionFunctionCall* BreakFloat3 = nullptr;
	IDatasmithMaterialExpressionGeneric* AddRG = nullptr;
	IDatasmithMaterialExpressionGeneric* AddRGB = nullptr;
	IDatasmithMaterialExpressionGeneric* Divide = nullptr;
	IDatasmithMaterialExpressionScalar* DivideConstant = nullptr;
	if (bIsTransparent)
	{
		BreakFloat3 = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionFunctionCall>();
		BreakFloat3->SetFunctionPathName(TEXT("/Engine/Functions/Engine_MaterialFunctions02/Utility/BreakFloat3Components.BreakFloat3Components"));

		AddRG = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionGeneric>();
		AddRG->SetExpressionName(TEXT("Add"));

		AddRGB = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionGeneric>();
		AddRGB->SetExpressionName(TEXT("Add"));

		Divide = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionGeneric>();
		Divide->SetExpressionName(TEXT("Divide"));

		DivideConstant = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionScalar>();
		DivideConstant->GetScalar() = 3.0f;
	}

	// Connect expressions
	SpecularColorExpression->ConnectExpression(*ColorSpecLerp->GetInput(0));
	ColorExpression->ConnectExpression(*ColorSpecLerp->GetInput(1));
	ColorSpecLerpValue->ConnectExpression(*ColorSpecLerp->GetInput(2));

	ColorExpression->ConnectExpression(*ColorMetallicLerp->GetInput(0));
	ColorSpecLerp->ConnectExpression(*ColorMetallicLerp->GetInput(1));
	GlossExpression->ConnectExpression(*ColorMetallicLerp->GetInput(2));

	DiffuseLerpA->ConnectExpression(*DiffuseLerp->GetInput(0));
	DiffuseLerpB->ConnectExpression(*DiffuseLerp->GetInput(1));
	DiffuseExpression->ConnectExpression(*DiffuseLerp->GetInput(2));

	ColorMetallicLerp->ConnectExpression(*BaseColorMultiply->GetInput(0));
	DiffuseLerp->ConnectExpression(*BaseColorMultiply->GetInput(1));

	BaseColorMultiply->ConnectExpression(*BaseColorAdd->GetInput(0));
	IncandescenceColorExpression->ConnectExpression(*BaseColorAdd->GetInput(1));

	BaseColorAdd->ConnectExpression(*BaseColorTransparencyMultiply->GetInput(0));
	TransparencyOneMinus->ConnectExpression(*BaseColorTransparencyMultiply->GetInput(1));

	GlowIntensityExpression->ConnectExpression(*IncandescenceScaleMultiply->GetInput(0));
	IncandescenceScale->ConnectExpression(*IncandescenceScaleMultiply->GetInput(1));

	BaseColorTransparencyMultiply->ConnectExpression(*IncandescenceMultiply->GetInput(0));
	IncandescenceScaleMultiply->ConnectExpression(*IncandescenceMultiply->GetInput(1));

	EccentricityExpression->ConnectExpression(*EccentricityOneMinus->GetInput(0));

	EccentricityOneMinus->ConnectExpression(*EccentricityMultiply->GetInput(0));
	SpecularityExpression->ConnectExpression(*EccentricityMultiply->GetInput(1));

	EccentricityMultiply->ConnectExpression(*RoughnessOneMinus->GetInput(0));

	FresnelExponent->ConnectExpression(*FresnelFunc->GetInput(3));

	SpecularRolloffExpression->ConnectExpression(*Power->GetInput(0));
	SpecularPowerExp->ConnectExpression(*Power->GetInput(1));

	FresnelLerpA->ConnectExpression(*FresnelLerp->GetInput(0));
	FresnelFunc->ConnectExpression(*FresnelLerp->GetInput(1));
	Power->ConnectExpression(*FresnelLerp->GetInput(2));

	FresnelLerp->ConnectExpression(*FresnelMultiply->GetInput(0));
	ReflectivityExpression->ConnectExpression(*FresnelMultiply->GetInput(1));

	TransparencyColorExpression->ConnectExpression(*TransparencyOneMinus->GetInput(0));

	if (bIsTransparent)
	{
		TransparencyOneMinus->ConnectExpression(*BreakFloat3->GetInput(0));

		BreakFloat3->ConnectExpression(*AddRG->GetInput(0), 0);
		BreakFloat3->ConnectExpression(*AddRG->GetInput(1), 1);

		AddRG->ConnectExpression(*AddRGB->GetInput(0));
		BreakFloat3->ConnectExpression(*AddRGB->GetInput(1), 2);

		AddRGB->ConnectExpression(*Divide->GetInput(0));
		DivideConstant->ConnectExpression(*Divide->GetInput(1));
	}

	// Connect material outputs
	MaterialElement->GetBaseColor().SetExpression(BaseColorTransparencyMultiply);
	MaterialElement->GetMetallic().SetExpression(GlossExpression);
	MaterialElement->GetSpecular().SetExpression(FresnelMultiply);
	MaterialElement->GetRoughness().SetExpression(RoughnessOneMinus);
	MaterialElement->GetEmissiveColor().SetExpression(IncandescenceMultiply);

	if (bIsTransparent)
	{
		MaterialElement->GetOpacity().SetExpression(Divide);
		MaterialElement->SetParentLabel(TEXT("M_DatasmithAliasBlinnTransparent"));
	}
	else 
	{
		MaterialElement->SetParentLabel(TEXT("M_DatasmithAliasBlinn"));
	}

}

void FWireTranslatorImpl::AddAlLambertParameters(AlShader *Shader, TSharedRef<IDatasmithUEPbrMaterialElement> MaterialElement)
{
	// Default values for a Lambert material
	FColor Color(145, 148, 153);
	FColor TransparencyColor(0, 0, 0);
	FColor IncandescenceColor(0, 0, 0);
	double Diffuse = 1.0;
	double GlowIntensity = 0.0;

	AlList* List = Shader->fields();
	for (AlShadingFieldItem* Item = static_cast<AlShadingFieldItem *>(List->first()); Item; Item = Item->nextField())
	{
		double Value = 0.0f;
		statusCode ErrorCode = Shader->parameter(Item->field(), Value);
		if (ErrorCode != 0)
		{
			continue;
		}

		if (GetCommonParameters(Item->field(), Value, Color, TransparencyColor, IncandescenceColor, GlowIntensity))
		{
			continue;
		}

		switch (Item->field())
		{
		case AlShadingFields::kFLD_SHADING_LAMBERT_DIFFUSE:
			Diffuse = Value;
			break;
		}
	}

	bool bIsTransparent = IsTransparent(TransparencyColor);

	// Construct parameter expressions
	IDatasmithMaterialExpressionScalar* DiffuseExpression = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionScalar>();
	DiffuseExpression->GetScalar() = Diffuse;
	DiffuseExpression->SetName(TEXT("Diffuse"));

	IDatasmithMaterialExpressionColor* ColorExpression = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionColor>();
	ColorExpression->SetName(TEXT("Color"));
	ColorExpression->GetColor() = FLinearColor::FromSRGBColor(Color);

	IDatasmithMaterialExpressionColor* IncandescenceColorExpression = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionColor>();
	IncandescenceColorExpression->SetName(TEXT("IncandescenceColor"));
	IncandescenceColorExpression->GetColor() = FLinearColor::FromSRGBColor(IncandescenceColor);

	IDatasmithMaterialExpressionColor* TransparencyColorExpression = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionColor>();
	TransparencyColorExpression->SetName(TEXT("TransparencyColor"));
	TransparencyColorExpression->GetColor() = FLinearColor::FromSRGBColor(TransparencyColor);

	IDatasmithMaterialExpressionScalar* GlowIntensityExpression = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionScalar>();
	GlowIntensityExpression->GetScalar() = GlowIntensity;
	GlowIntensityExpression->SetName(TEXT("GlowIntensity"));

	// Create aux expressions
	IDatasmithMaterialExpressionGeneric* DiffuseLerp = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionGeneric>();
	DiffuseLerp->SetExpressionName(TEXT("LinearInterpolate"));

	IDatasmithMaterialExpressionScalar* DiffuseLerpA = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionScalar>();
	DiffuseLerpA->GetScalar() = 0.04f;

	IDatasmithMaterialExpressionScalar* DiffuseLerpB = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionScalar>();
	DiffuseLerpB->GetScalar() = 1.0f;

	IDatasmithMaterialExpressionGeneric* BaseColorMultiply = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionGeneric>();
	BaseColorMultiply->SetExpressionName(TEXT("Multiply"));

	IDatasmithMaterialExpressionGeneric* BaseColorAdd = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionGeneric>();
	BaseColorAdd->SetExpressionName(TEXT("Add"));

	IDatasmithMaterialExpressionGeneric* BaseColorTransparencyMultiply = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionGeneric>();
	BaseColorTransparencyMultiply->SetExpressionName(TEXT("Multiply"));

	IDatasmithMaterialExpressionGeneric* IncandescenceMultiply = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionGeneric>();
	IncandescenceMultiply->SetExpressionName(TEXT("Multiply"));

	IDatasmithMaterialExpressionGeneric* IncandescenceScaleMultiply = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionGeneric>();
	IncandescenceScaleMultiply->SetExpressionName(TEXT("Multiply"));

	IDatasmithMaterialExpressionScalar* IncandescenceScale = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionScalar>();
	IncandescenceScale->GetScalar() = 100.0f;

	IDatasmithMaterialExpressionGeneric* TransparencyOneMinus = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionGeneric>();
	TransparencyOneMinus->SetExpressionName(TEXT("OneMinus"));

	IDatasmithMaterialExpressionFunctionCall* BreakFloat3 = nullptr;
	IDatasmithMaterialExpressionGeneric* AddRG = nullptr;
	IDatasmithMaterialExpressionGeneric* AddRGB = nullptr;
	IDatasmithMaterialExpressionGeneric* Divide = nullptr;
	IDatasmithMaterialExpressionScalar* DivideConstant = nullptr;
	if (bIsTransparent)
	{
		BreakFloat3 = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionFunctionCall>();
		BreakFloat3->SetFunctionPathName(TEXT("/Engine/Functions/Engine_MaterialFunctions02/Utility/BreakFloat3Components.BreakFloat3Components"));

		AddRG = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionGeneric>();
		AddRG->SetExpressionName(TEXT("Add"));

		AddRGB = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionGeneric>();
		AddRGB->SetExpressionName(TEXT("Add"));

		Divide = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionGeneric>();
		Divide->SetExpressionName(TEXT("Divide"));

		DivideConstant = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionScalar>();
		DivideConstant->GetScalar() = 3.0f;
	}

	// Connect expressions
	DiffuseLerpA->ConnectExpression(*DiffuseLerp->GetInput(0));
	DiffuseLerpB->ConnectExpression(*DiffuseLerp->GetInput(1));
	DiffuseExpression->ConnectExpression(*DiffuseLerp->GetInput(2));

	ColorExpression->ConnectExpression(*BaseColorMultiply->GetInput(0));
	DiffuseLerp->ConnectExpression(*BaseColorMultiply->GetInput(1));

	BaseColorMultiply->ConnectExpression(*BaseColorAdd->GetInput(0));
	IncandescenceColorExpression->ConnectExpression(*BaseColorAdd->GetInput(1));

	BaseColorAdd->ConnectExpression(*BaseColorTransparencyMultiply->GetInput(0));
	TransparencyOneMinus->ConnectExpression(*BaseColorTransparencyMultiply->GetInput(1));

	GlowIntensityExpression->ConnectExpression(*IncandescenceScaleMultiply->GetInput(0));
	IncandescenceScale->ConnectExpression(*IncandescenceScaleMultiply->GetInput(1));

	BaseColorTransparencyMultiply->ConnectExpression(*IncandescenceMultiply->GetInput(0));
	IncandescenceScaleMultiply->ConnectExpression(*IncandescenceMultiply->GetInput(1));

	TransparencyColorExpression->ConnectExpression(*TransparencyOneMinus->GetInput(0));

	if (bIsTransparent)
	{
		TransparencyOneMinus->ConnectExpression(*BreakFloat3->GetInput(0));

		BreakFloat3->ConnectExpression(*AddRG->GetInput(0), 0);
		BreakFloat3->ConnectExpression(*AddRG->GetInput(1), 1);

		AddRG->ConnectExpression(*AddRGB->GetInput(0));
		BreakFloat3->ConnectExpression(*AddRGB->GetInput(1), 2);

		AddRGB->ConnectExpression(*Divide->GetInput(0));
		DivideConstant->ConnectExpression(*Divide->GetInput(1));
	}

	// Connect material outputs
	MaterialElement->GetBaseColor().SetExpression(BaseColorTransparencyMultiply);
	MaterialElement->GetEmissiveColor().SetExpression(IncandescenceMultiply);
	if (bIsTransparent)
	{
		MaterialElement->GetOpacity().SetExpression(Divide);
		MaterialElement->SetParentLabel(TEXT("M_DatasmithAliasLambertTransparent"));
	}
	else {
		MaterialElement->SetParentLabel(TEXT("M_DatasmithAliasLambert"));
	}

}

void FWireTranslatorImpl::AddAlLightSourceParameters(AlShader *Shader, TSharedRef<IDatasmithUEPbrMaterialElement> MaterialElement)
{
	// Default values for a LightSource material
	FColor Color(145, 148, 153);
	FColor TransparencyColor(0, 0, 0);
	FColor IncandescenceColor(0, 0, 0);
	double GlowIntensity = 0.0;

	AlList* List = Shader->fields();
	for (AlShadingFieldItem* Item = static_cast<AlShadingFieldItem *>(List->first()); Item; Item = Item->nextField())
	{
		double Value = 0.0f;
		statusCode ErrorCode = Shader->parameter(Item->field(), Value);
		if (ErrorCode != 0)
		{
			continue;
		}

		GetCommonParameters(Item->field(), Value, Color, TransparencyColor, IncandescenceColor, GlowIntensity);
	}

	bool bIsTransparent = IsTransparent(TransparencyColor);

	// Construct parameter expressions
	IDatasmithMaterialExpressionColor* ColorExpression = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionColor>();
	ColorExpression->SetName(TEXT("Color"));
	ColorExpression->GetColor() = FLinearColor::FromSRGBColor(Color);

	IDatasmithMaterialExpressionColor* IncandescenceColorExpression = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionColor>();
	IncandescenceColorExpression->SetName(TEXT("IncandescenceColor"));
	IncandescenceColorExpression->GetColor() = FLinearColor::FromSRGBColor(IncandescenceColor);

	IDatasmithMaterialExpressionColor* TransparencyColorExpression = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionColor>();
	TransparencyColorExpression->SetName(TEXT("TransparencyColor"));
	TransparencyColorExpression->GetColor() = FLinearColor::FromSRGBColor(TransparencyColor);

	IDatasmithMaterialExpressionScalar* GlowIntensityExpression = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionScalar>();
	GlowIntensityExpression->GetScalar() = GlowIntensity;
	GlowIntensityExpression->SetName(TEXT("GlowIntensity"));

	// Create aux expressions
	IDatasmithMaterialExpressionGeneric* BaseColorAdd = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionGeneric>();
	BaseColorAdd->SetExpressionName(TEXT("Add"));

	IDatasmithMaterialExpressionGeneric* BaseColorTransparencyMultiply = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionGeneric>();
	BaseColorTransparencyMultiply->SetExpressionName(TEXT("Multiply"));

	IDatasmithMaterialExpressionGeneric* IncandescenceMultiply = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionGeneric>();
	IncandescenceMultiply->SetExpressionName(TEXT("Multiply"));

	IDatasmithMaterialExpressionGeneric* IncandescenceScaleMultiply = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionGeneric>();
	IncandescenceScaleMultiply->SetExpressionName(TEXT("Multiply"));

	IDatasmithMaterialExpressionScalar* IncandescenceScale = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionScalar>();
	IncandescenceScale->GetScalar() = 100.0f;

	IDatasmithMaterialExpressionGeneric* TransparencyOneMinus = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionGeneric>();
	TransparencyOneMinus->SetExpressionName(TEXT("OneMinus"));

	IDatasmithMaterialExpressionFunctionCall* BreakFloat3 = nullptr;
	IDatasmithMaterialExpressionGeneric* AddRG = nullptr;
	IDatasmithMaterialExpressionGeneric* AddRGB = nullptr;
	IDatasmithMaterialExpressionGeneric* Divide = nullptr;
	IDatasmithMaterialExpressionScalar* DivideConstant = nullptr;
	if (bIsTransparent)
	{
		BreakFloat3 = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionFunctionCall>();
		BreakFloat3->SetFunctionPathName(TEXT("/Engine/Functions/Engine_MaterialFunctions02/Utility/BreakFloat3Components.BreakFloat3Components"));

		AddRG = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionGeneric>();
		AddRG->SetExpressionName(TEXT("Add"));

		AddRGB = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionGeneric>();
		AddRGB->SetExpressionName(TEXT("Add"));

		Divide = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionGeneric>();
		Divide->SetExpressionName(TEXT("Divide"));

		DivideConstant = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionScalar>();
		DivideConstant->GetScalar() = 3.0f;
	}

	// Connect expressions
	ColorExpression->ConnectExpression(*BaseColorAdd->GetInput(0));
	IncandescenceColorExpression->ConnectExpression(*BaseColorAdd->GetInput(1));

	BaseColorAdd->ConnectExpression(*BaseColorTransparencyMultiply->GetInput(0));
	TransparencyOneMinus->ConnectExpression(*BaseColorTransparencyMultiply->GetInput(1));

	GlowIntensityExpression->ConnectExpression(*IncandescenceScaleMultiply->GetInput(0));
	IncandescenceScale->ConnectExpression(*IncandescenceScaleMultiply->GetInput(1));

	BaseColorTransparencyMultiply->ConnectExpression(*IncandescenceMultiply->GetInput(0));
	IncandescenceScaleMultiply->ConnectExpression(*IncandescenceMultiply->GetInput(1));

	TransparencyColorExpression->ConnectExpression(*TransparencyOneMinus->GetInput(0));

	if (bIsTransparent)
	{
		TransparencyOneMinus->ConnectExpression(*BreakFloat3->GetInput(0));

		BreakFloat3->ConnectExpression(*AddRG->GetInput(0), 0);
		BreakFloat3->ConnectExpression(*AddRG->GetInput(1), 1);

		AddRG->ConnectExpression(*AddRGB->GetInput(0));
		BreakFloat3->ConnectExpression(*AddRGB->GetInput(1), 2);

		AddRGB->ConnectExpression(*Divide->GetInput(0));
		DivideConstant->ConnectExpression(*Divide->GetInput(1));
	}

	// Connect material outputs
	MaterialElement->GetBaseColor().SetExpression(BaseColorTransparencyMultiply);
	MaterialElement->GetEmissiveColor().SetExpression(IncandescenceMultiply);

	if (bIsTransparent)
	{
		MaterialElement->GetOpacity().SetExpression(Divide);
		MaterialElement->SetParentLabel(TEXT("M_DatasmithAliasLightSourceTransparent"));
	}
	else {
		MaterialElement->SetParentLabel(TEXT("M_DatasmithAliasLightSource"));
	}

}

void FWireTranslatorImpl::AddAlPhongParameters(AlShader *Shader, TSharedRef<IDatasmithUEPbrMaterialElement> MaterialElement)
{
	// Default values for a Phong material
	FColor Color(145, 148, 153);
	FColor TransparencyColor(0, 0, 0);
	FColor IncandescenceColor(0, 0, 0);
	FColor SpecularColor(38, 38, 38);
	double Diffuse = 1.0;
	double GlowIntensity = 0.0;
	double Gloss = 0.8;
	double Shinyness = 20.0;
	double Specularity = 1.0;
	double Reflectivity = 0.5;

	AlList* List = Shader->fields();
	for (AlShadingFieldItem* Item = static_cast<AlShadingFieldItem *>(List->first()); Item; Item = Item->nextField())
	{
		double Value = 0.0f;
		statusCode ErrorCode = Shader->parameter(Item->field(), Value);
		if (ErrorCode != 0)
		{
			continue;
		}

		if (GetCommonParameters(Item->field(), Value, Color, TransparencyColor, IncandescenceColor, GlowIntensity))
		{
			continue;
		}

		switch (Item->field())
		{
		case AlShadingFields::kFLD_SHADING_PHONG_DIFFUSE:
			Diffuse = Value;
			break;
		case AlShadingFields::kFLD_SHADING_PHONG_GLOSS_:
			Gloss = Value;
			break;
		case AlShadingFields::kFLD_SHADING_PHONG_SPECULAR_R:
			SpecularColor.R = (uint8)(255.f * Value);;
			break;
		case AlShadingFields::kFLD_SHADING_PHONG_SPECULAR_G:
			SpecularColor.G = (uint8)(255.f * Value);;
			break;
		case AlShadingFields::kFLD_SHADING_PHONG_SPECULAR_B:
			SpecularColor.B = (uint8)(255.f * Value);;
			break;
		case AlShadingFields::kFLD_SHADING_PHONG_SPECULARITY_:
			Specularity = Value;
			break;
		case AlShadingFields::kFLD_SHADING_PHONG_SHINYNESS:
			Shinyness = Value;
			break;
		case AlShadingFields::kFLD_SHADING_PHONG_REFLECTIVITY:
			Reflectivity = Value;
			break;
		}
	}

	bool bIsTransparent = IsTransparent(TransparencyColor);

	// Construct parameter expressions
	IDatasmithMaterialExpressionScalar* DiffuseExpression = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionScalar>();
	DiffuseExpression->GetScalar() = Diffuse;
	DiffuseExpression->SetName(TEXT("Diffuse"));

	IDatasmithMaterialExpressionScalar* GlossExpression = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionScalar>();
	GlossExpression->GetScalar() = Gloss;
	GlossExpression->SetName(TEXT("Gloss"));

	IDatasmithMaterialExpressionColor* SpecularColorExpression = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionColor>();
	SpecularColorExpression->SetName(TEXT("SpecularColor"));
	SpecularColorExpression->GetColor() = FLinearColor::FromSRGBColor(SpecularColor);

	IDatasmithMaterialExpressionScalar* SpecularityExpression = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionScalar>();
	SpecularityExpression->GetScalar() = Specularity * 0.3;
	SpecularityExpression->SetName(TEXT("Specularity"));

	IDatasmithMaterialExpressionScalar* ShinynessExpression = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionScalar>();
	ShinynessExpression->GetScalar() = Shinyness;
	ShinynessExpression->SetName(TEXT("Shinyness"));

	IDatasmithMaterialExpressionScalar* ReflectivityExpression = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionScalar>();
	ReflectivityExpression->GetScalar() = Reflectivity;
	ReflectivityExpression->SetName(TEXT("Reflectivity"));

	IDatasmithMaterialExpressionColor* ColorExpression = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionColor>();
	ColorExpression->SetName(TEXT("Color"));
	ColorExpression->GetColor() = FLinearColor::FromSRGBColor(Color);

	IDatasmithMaterialExpressionColor* IncandescenceColorExpression = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionColor>();
	IncandescenceColorExpression->SetName(TEXT("IncandescenceColor"));
	IncandescenceColorExpression->GetColor() = FLinearColor::FromSRGBColor(IncandescenceColor);

	IDatasmithMaterialExpressionColor* TransparencyColorExpression = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionColor>();
	TransparencyColorExpression->SetName(TEXT("TransparencyColor"));
	TransparencyColorExpression->GetColor() = FLinearColor::FromSRGBColor(TransparencyColor);

	IDatasmithMaterialExpressionScalar* GlowIntensityExpression = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionScalar>();
	GlowIntensityExpression->GetScalar() = GlowIntensity;
	GlowIntensityExpression->SetName(TEXT("GlowIntensity"));

	// Create aux expressions
	IDatasmithMaterialExpressionGeneric* ColorSpecLerp = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionGeneric>();
	ColorSpecLerp->SetExpressionName(TEXT("LinearInterpolate"));

	IDatasmithMaterialExpressionScalar* ColorSpecLerpValue = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionScalar>();
	ColorSpecLerpValue->GetScalar() = 0.96f;

	IDatasmithMaterialExpressionGeneric* ColorMetallicLerp = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionGeneric>();
	ColorMetallicLerp->SetExpressionName(TEXT("LinearInterpolate"));

	IDatasmithMaterialExpressionGeneric* DiffuseLerp = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionGeneric>();
	DiffuseLerp->SetExpressionName(TEXT("LinearInterpolate"));

	IDatasmithMaterialExpressionScalar* DiffuseLerpA = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionScalar>();
	DiffuseLerpA->GetScalar() = 0.04f;

	IDatasmithMaterialExpressionScalar* DiffuseLerpB = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionScalar>();
	DiffuseLerpB->GetScalar() = 1.0f;

	IDatasmithMaterialExpressionGeneric* BaseColorMultiply = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionGeneric>();
	BaseColorMultiply->SetExpressionName(TEXT("Multiply"));

	IDatasmithMaterialExpressionGeneric* BaseColorAdd = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionGeneric>();
	BaseColorAdd->SetExpressionName(TEXT("Add"));

	IDatasmithMaterialExpressionGeneric* BaseColorTransparencyMultiply = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionGeneric>();
	BaseColorTransparencyMultiply->SetExpressionName(TEXT("Multiply"));

	IDatasmithMaterialExpressionGeneric* IncandescenceMultiply = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionGeneric>();
	IncandescenceMultiply->SetExpressionName(TEXT("Multiply"));

	IDatasmithMaterialExpressionGeneric* IncandescenceScaleMultiply = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionGeneric>();
	IncandescenceScaleMultiply->SetExpressionName(TEXT("Multiply"));

	IDatasmithMaterialExpressionScalar* IncandescenceScale = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionScalar>();
	IncandescenceScale->GetScalar() = 100.0f;

	IDatasmithMaterialExpressionGeneric* ShinynessSubtract = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionGeneric>();
	ShinynessSubtract->SetExpressionName(TEXT("Subtract"));

	IDatasmithMaterialExpressionScalar* ShinynessSubtract2 = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionScalar>();
	ShinynessSubtract2->GetScalar() = 2.0f;

	IDatasmithMaterialExpressionGeneric* ShinynessDivide = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionGeneric>();
	ShinynessDivide->SetExpressionName(TEXT("Divide"));

	IDatasmithMaterialExpressionScalar* ShinynessDivide98 = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionScalar>();
	ShinynessDivide98->GetScalar() = 98.0f;

	IDatasmithMaterialExpressionGeneric* SpecularityMultiply = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionGeneric>();
	SpecularityMultiply->SetExpressionName(TEXT("Multiply"));

	IDatasmithMaterialExpressionGeneric* RoughnessOneMinus = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionGeneric>();
	RoughnessOneMinus->SetExpressionName(TEXT("OneMinus"));

	IDatasmithMaterialExpressionGeneric* TransparencyOneMinus = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionGeneric>();
	TransparencyOneMinus->SetExpressionName(TEXT("OneMinus"));

	IDatasmithMaterialExpressionFunctionCall* BreakFloat3 = nullptr;
	IDatasmithMaterialExpressionGeneric* AddRG = nullptr;
	IDatasmithMaterialExpressionGeneric* AddRGB = nullptr;
	IDatasmithMaterialExpressionGeneric* Divide = nullptr;
	IDatasmithMaterialExpressionScalar* DivideConstant = nullptr;
	if (bIsTransparent)
	{
		BreakFloat3 = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionFunctionCall>();
		BreakFloat3->SetFunctionPathName(TEXT("/Engine/Functions/Engine_MaterialFunctions02/Utility/BreakFloat3Components.BreakFloat3Components"));

		AddRG = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionGeneric>();
		AddRG->SetExpressionName(TEXT("Add"));

		AddRGB = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionGeneric>();
		AddRGB->SetExpressionName(TEXT("Add"));

		Divide = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionGeneric>();
		Divide->SetExpressionName(TEXT("Divide"));

		DivideConstant = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionScalar>();
		DivideConstant->GetScalar() = 3.0f;
	}

	// Connect expressions
	SpecularColorExpression->ConnectExpression(*ColorSpecLerp->GetInput(0));
	ColorExpression->ConnectExpression(*ColorSpecLerp->GetInput(1));
	ColorSpecLerpValue->ConnectExpression(*ColorSpecLerp->GetInput(2));

	ColorExpression->ConnectExpression(*ColorMetallicLerp->GetInput(0));
	ColorSpecLerp->ConnectExpression(*ColorMetallicLerp->GetInput(1));
	GlossExpression->ConnectExpression(*ColorMetallicLerp->GetInput(2));

	DiffuseLerpA->ConnectExpression(*DiffuseLerp->GetInput(0));
	DiffuseLerpB->ConnectExpression(*DiffuseLerp->GetInput(1));
	DiffuseExpression->ConnectExpression(*DiffuseLerp->GetInput(2));

	ColorMetallicLerp->ConnectExpression(*BaseColorMultiply->GetInput(0));
	DiffuseLerp->ConnectExpression(*BaseColorMultiply->GetInput(1));

	BaseColorMultiply->ConnectExpression(*BaseColorAdd->GetInput(0));
	IncandescenceColorExpression->ConnectExpression(*BaseColorAdd->GetInput(1));

	BaseColorAdd->ConnectExpression(*BaseColorTransparencyMultiply->GetInput(0));
	TransparencyOneMinus->ConnectExpression(*BaseColorTransparencyMultiply->GetInput(1));

	GlowIntensityExpression->ConnectExpression(*IncandescenceScaleMultiply->GetInput(0));
	IncandescenceScale->ConnectExpression(*IncandescenceScaleMultiply->GetInput(1));

	BaseColorTransparencyMultiply->ConnectExpression(*IncandescenceMultiply->GetInput(0));
	IncandescenceScaleMultiply->ConnectExpression(*IncandescenceMultiply->GetInput(1));

	ShinynessExpression->ConnectExpression(*ShinynessSubtract->GetInput(0));
	ShinynessSubtract2->ConnectExpression(*ShinynessSubtract->GetInput(1));

	ShinynessSubtract->ConnectExpression(*ShinynessDivide->GetInput(0));
	ShinynessDivide98->ConnectExpression(*ShinynessDivide->GetInput(1));

	ShinynessDivide->ConnectExpression(*SpecularityMultiply->GetInput(0));
	SpecularityExpression->ConnectExpression(*SpecularityMultiply->GetInput(1));

	SpecularityMultiply->ConnectExpression(*RoughnessOneMinus->GetInput(0));

	TransparencyColorExpression->ConnectExpression(*TransparencyOneMinus->GetInput(0));

	if (bIsTransparent)
	{
		TransparencyOneMinus->ConnectExpression(*BreakFloat3->GetInput(0));

		BreakFloat3->ConnectExpression(*AddRG->GetInput(0), 0);
		BreakFloat3->ConnectExpression(*AddRG->GetInput(1), 1);

		AddRG->ConnectExpression(*AddRGB->GetInput(0));
		BreakFloat3->ConnectExpression(*AddRGB->GetInput(1), 2);

		AddRGB->ConnectExpression(*Divide->GetInput(0));
		DivideConstant->ConnectExpression(*Divide->GetInput(1));
	}

	// Connect material outputs
	MaterialElement->GetBaseColor().SetExpression(BaseColorTransparencyMultiply);
	MaterialElement->GetMetallic().SetExpression(GlossExpression);
	MaterialElement->GetSpecular().SetExpression(ReflectivityExpression);
	MaterialElement->GetRoughness().SetExpression(RoughnessOneMinus);
	MaterialElement->GetEmissiveColor().SetExpression(IncandescenceMultiply);
	if (bIsTransparent)
	{
		MaterialElement->GetOpacity().SetExpression(Divide);
		MaterialElement->SetParentLabel(TEXT("M_DatasmithAliasPhongTransparent"));
	}
	else {
		MaterialElement->SetParentLabel(TEXT("M_DatasmithAliasPhong"));
	}
}

// Make material
bool FWireTranslatorImpl::GetShader()
{
	AlShader *Shader = AlUniverse::firstShader();
	while (Shader)
	{
		FString ShaderName = Shader->name();
		FString ShaderModelName = Shader->shadingModel();

		uint32 ShaderUUID = fabs((float)(int32)GetTypeHash(*ShaderName));

		TSharedRef<IDatasmithUEPbrMaterialElement> MaterialElement = FDatasmithSceneFactory::CreateUEPbrMaterial(*ShaderName);

		MaterialElement->SetLabel(*ShaderName);
		MaterialElement->SetName(*FString::FromInt(ShaderUUID));

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

		DatasmithScene->AddMaterial(MaterialElement);

		TSharedPtr< IDatasmithMaterialIDElement > MaterialIDElement = FDatasmithSceneFactory::CreateMaterialId(MaterialElement->GetName());
		ShaderNameToUEMaterialId.Add(*ShaderName, MaterialIDElement);

		AlShader *CurrentShader = Shader;
		Shader = AlUniverse::nextShader(Shader);
		delete CurrentShader;
	}
	return true;
}



bool FWireTranslatorImpl::GetDagLeaves()
{
	FDagNodeInfo RootContainer;
	AlRootNode = AlUniverse::firstDagNode();
	AlDagNodeArray.Add(AlRootNode);
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

	AlPersistentID* GroupNodeId = nullptr;
	CurrentNode.persistentID(GroupNodeId);

	FString ThisGroupNodeID( GetPersistentIDString(GroupNodeId) );

    // Limit length of UUID by combining hash of parent UUID and container's UUID if ParentUuid is not empty
	CurrentNodeInfo.UEuuid = GetUEUUIDFromAIPersistentID(ParentInfo.UEuuid, ThisGroupNodeID);
}

void FWireTranslatorImpl::GetDagNodeInfo(TSharedRef<BodyData> CurrentNode, const FDagNodeInfo& ParentInfo, FDagNodeInfo& CurrentNodeInfo)
{
	CurrentNodeInfo.Label = ParentInfo.Label; // +TEXT(" ") + CurrentNode->LayerName + TEXT(" ") + CurrentNode->ShaderName;
	CurrentNode->Label = CurrentNodeInfo.Label;

	// Limit length of UUID by combining hash of parent UUID and container's UUID if ParentUuid is not empty
	CurrentNodeInfo.UEuuid = GetUEUUIDFromAIPersistentID( ParentInfo.UEuuid, CurrentNode->GetUUID( ParentInfo.UEuuid ) );
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
		AlDagNodeArray.Add(ChildNode);
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

TSharedPtr< IDatasmithMeshElement > FWireTranslatorImpl::FindOrAddMeshElement(TSharedRef<BodyData> Body, const FDagNodeInfo& NodeInfo)
{
	TSharedPtr< IDatasmithMeshElement >* MeshElementPtr = BodyToMeshElementMap.Find( NodeInfo.UEuuid );
	if (MeshElementPtr != nullptr)
	{
		return *MeshElementPtr;
	}
	
	TSharedPtr< IDatasmithMeshElement > MeshElement = FDatasmithSceneFactory::CreateMesh( *NodeInfo.UEuuid );
	MeshElement->SetLabel(*NodeInfo.Label);
	MeshElement->SetLightmapSourceUV(-1);

	if (*Body->ShaderName)
	{
		TSharedPtr< IDatasmithMaterialIDElement > MaterialElement = ShaderNameToUEMaterialId[Body->ShaderName];
		MeshElement->SetMaterial(MaterialElement->GetName(), 0);
	}

	DatasmithScene->AddMesh(MeshElement);

	ShellUUIDToMeshElementMap.Add(FCString::Atoi(*NodeInfo.UEuuid), MeshElement);
	MeshElementToBodyMap.Add(MeshElement.Get(), Body);

	BodyToMeshElementMap.Add( NodeInfo.UEuuid, MeshElement );

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

	// Set MeshElement FileHash used for re-import task 
	FMD5 MD5; // unique Value that define the mesh
	MD5.Update(reinterpret_cast<const uint8*>(&SceneFileHash), sizeof SceneFileHash);
	// MeshActor Name
	MD5.Update(reinterpret_cast<const uint8*>(&ShellUUID), sizeof ShellUUID);
	FMD5Hash Hash;
	Hash.Set(MD5);
	MeshElement->SetFileHash(Hash);

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

	// Apply materials on the current part
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
		return RecurseDagForLeavesNoMerge(Body->ShellSet[0], ParentInfo);
	}

	FDagNodeInfo ShellInfo;
	GetDagNodeInfo(Body, ParentInfo, ShellInfo);

	TSharedPtr< IDatasmithMeshElement > MeshElement = FindOrAddMeshElement(Body, ShellInfo);
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
		AlDagNode* NextDagNode = GetNextNode(DagNode);
		if(DagNode != FirstDagNode) 
		{
			delete DagNode;
		}
		DagNode = NextDagNode;
	}

	DagNode = FirstDagNode;

	TSet<AlDagNode*> UnProcessedNodes;
	TMap<uint32, TSharedPtr<BodyData>> ShellToProcess;

	const char* ShaderName = nullptr;

	while (DagNode)
	{
		// Filter invalid nodes.
		if (AlIsValid(DagNode) && !IsHidden(DagNode))
		{
			AlObjectType objectType = DagNode->type();

			// Process the current node.
			switch (objectType)
			{

			case kShellNodeType:
			{
				AlShellNode* ShellNode = DagNode->asShellNodePtr();
				AlShell* Shell = ShellNode->shell();
				uint32 NbPatch = getNumOfPatch(*Shell);

				if (AlShader* Shader = Shell->firstShader())
				{
					ShaderName = Shader->name();
				}

				if (NbPatch == 1)
				{
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
				AlSurfaceNode* SurfaceNode = DagNode->asSurfaceNodePtr();
				AlSurface* Surface = SurfaceNode->surface();
				if (AlShader* Shader = Surface->firstShader())
				{
					ShaderName = Shader->name();
				}
				AddNodeInBodySet(*DagNode, ShaderName, ShellToProcess, true, MaxSize);
				break;
			}

			case kMeshNodeType:
			{
				AlMeshNode* MeshNode = DagNode->asMeshNodePtr();
				AlMesh* Mesh = MeshNode->mesh();
				if (AlShader* Shader = Mesh->firstShader())
				{
					ShaderName = Shader->name();
				}
				AddNodeInBodySet(*DagNode, ShaderName, ShellToProcess, false, MaxSize);
				break;
			}

			// Traverse down through groups
			case kGroupNodeType:
			{
				AlGroupNode* GroupNode = DagNode->asGroupNodePtr();
				if (AlIsValid(GroupNode))
				{
					ProcessAlGroupNode(*GroupNode, ParentInfo);
				}
				break;
			}

			default:
			{
				break;
			}
			}
		}

		DagNode = GetNextNode(DagNode);
		AlDagNodeArray.Add(DagNode);

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
		if (AlIsValid(DagNode) && !IsHidden(DagNode))
		{
			// Process the current node.
			AlObjectType objectType = DagNode->type();
			switch (objectType)
			{

			case kShellNodeType:
			{
				AlShellNode* ShellNode = DagNode->asShellNodePtr();
				AlShell* Shell = ShellNode->shell();
				if (AlShader* Shader = Shell->firstShader())
				{
					ShaderName = Shader->name();
				}

				ProcessAlShellNode(*DagNode, ParentInfo, ShaderName);
				break;
			}

			case kSurfaceNodeType:
			{
				AlSurfaceNode* SurfaceNode = DagNode->asSurfaceNodePtr();
				AlSurface* Surface = SurfaceNode->surface();
				if (AlShader* Shader = Surface->firstShader())
				{
					ShaderName = Shader->name();
				}

				ProcessAlShellNode(*DagNode, ParentInfo, ShaderName);
				break;
			}

			case kMeshNodeType:
			{
				AlMeshNode* MeshNode = DagNode->asMeshNodePtr();
				AlMesh* Mesh = MeshNode->mesh();
				if (AlShader* Shader = Mesh->firstShader())
				{
					ShaderName = Shader->name();
				}

				ProcessAlShellNode(*DagNode, ParentInfo, ShaderName);
				break;
			}

			// Traverse down through groups
			case kGroupNodeType:
			{
				AlGroupNode* GroupNode = DagNode->asGroupNodePtr();
				if (AlIsValid(GroupNode))
				{
					ProcessAlGroupNode(*GroupNode, ParentInfo);
				}
				break;
			}

			default:
			{
				break;
			}

			}
		}

		DagNode = GetNextNode(DagNode);
		AlDagNodeArray.Add(DagNode);
	}
	return true;
}

TOptional<FMeshDescription> FWireTranslatorImpl::MeshDagNodeWithExternalMesher(AlDagNode& DagNode, TSharedRef<IDatasmithMeshElement> MeshElement, CADLibrary::FMeshParameters& MeshParameters)
{
	LocalSession->ClearData();

	// Wire unit is cm
	LocalSession->SetSceneUnit(0.01);

	FString Filename = DagNode.name();

	EAliasObjectReference ObjectReference = EAliasObjectReference::LocalReference;

	if (MeshParameters.bIsSymmetric)
	{
		// All actors of a Alias symmetric layer are defined in the world Reference i.e. they have identity transform. So Mesh actor has to be defined in the world reference. 
		ObjectReference = EAliasObjectReference::WorldReference;
	}

	TArray<AlDagNode*> DagNodeSet;
	DagNodeSet.Add(&DagNode);
	LocalSession->AddBRep(DagNodeSet, ObjectReference);

	Filename += TEXT(".ct");

	FString FilePath = FPaths::Combine(OutputPath, Filename);
	if (LocalSession->SaveBrep(FilePath))
	{
		MeshElement->SetFile(*FilePath);
	}

	FMeshDescription MeshDescription;
	DatasmithMeshHelper::PrepareAttributeForStaticMesh(MeshDescription);

	LocalSession->Tessellate(MeshDescription, MeshParameters);

	return MoveTemp(MeshDescription);
}

TOptional<FMeshDescription> FWireTranslatorImpl::MeshDagNodeWithExternalMesher(TSharedRef<BodyData> Body, TSharedRef<IDatasmithMeshElement> MeshElement, CADLibrary::FMeshParameters& MeshParameters)
{
	LocalSession->ClearData();

	// Wire unit is cm
	LocalSession->SetSceneUnit(0.01);

	EAliasObjectReference ObjectReference = EAliasObjectReference::LocalReference;
	if (MeshParameters.bIsSymmetric)
	{
		// All actors of a Alias symmetric layer are defined in the world Reference i.e. they have identity transform. So Mesh actor has to be defined in the world reference. 
		ObjectReference = EAliasObjectReference::WorldReference;
	}
	else if (GetImportParameters().StitchingTechnique == StitchingSew)
	{
		// In the case of StitchingSew, AlDagNode children of a GroupNode are merged together. To be merged, they have to be defined in the reference of parent GroupNode.
		ObjectReference = EAliasObjectReference::ParentReference;
	}

	LocalSession->AddBRep(Body->ShellSet, ObjectReference);

	FString Filename = Body->Label + TEXT(".ct");

	FString FilePath = FPaths::Combine(OutputPath, Filename);
	if (LocalSession->SaveBrep(FilePath))
	{
		MeshElement->SetFile(*FilePath);
	}

	FMeshDescription MeshDescription;
	DatasmithMeshHelper::PrepareAttributeForStaticMesh(MeshDescription);

	LocalSession->Tessellate(MeshDescription, MeshParameters);

	return MoveTemp(MeshDescription);
}

TOptional<FMeshDescription> FWireTranslatorImpl::GetMeshOfShellNode(AlDagNode& DagNode, TSharedRef<IDatasmithMeshElement> MeshElement, CADLibrary::FMeshParameters& MeshParameters)
{
	if (LocalSession->IsSessionValid())
	{
		TOptional< FMeshDescription > UEMesh = MeshDagNodeWithExternalMesher(DagNode, MeshElement, MeshParameters);
		return UEMesh;
	}
	else
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


TOptional<FMeshDescription> FWireTranslatorImpl::GetMeshOfShellBody(TSharedRef<BodyData> Body, TSharedRef<IDatasmithMeshElement> MeshElement, CADLibrary::FMeshParameters& MeshParameters)
{
	TOptional< FMeshDescription > UEMesh = MeshDagNodeWithExternalMesher(Body, MeshElement, MeshParameters);
	return UEMesh;
}

TOptional<FMeshDescription> FWireTranslatorImpl::GetMeshOfMeshBody(TSharedRef<BodyData> Body, TSharedRef<IDatasmithMeshElement> MeshElement, CADLibrary::FMeshParameters& MeshParameters)
{
	FMeshDescription MeshDescription;
	DatasmithMeshHelper::PrepareAttributeForStaticMesh(MeshDescription);
	MeshDescription.Empty();
	bool True = true;

	for (auto DagNode : Body->ShellSet)
	{
		AlMeshNode* MeshNode = DagNode->asMeshNodePtr();
		AlMesh* Mesh = MeshNode->mesh();
		if (Mesh)
		{
			TransferAlMeshToMeshDescription(*Mesh, MeshDescription, MeshParameters, True, true);
		}
	}

	return MoveTemp(MeshDescription);
}

TOptional<FMeshDescription> FWireTranslatorImpl::GetMeshOfNodeMesh(AlMeshNode& MeshNode, TSharedRef<IDatasmithMeshElement> MeshElement, CADLibrary::FMeshParameters& MeshParameters, AlMatrix4x4* AlMeshInvGlobalMatrix)
{
	AlMesh* Mesh = MeshNode.mesh();
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


TOptional<FMeshDescription> FWireTranslatorImpl::GetMeshDescription(TSharedRef<IDatasmithMeshElement> MeshElement, CADLibrary::FMeshParameters& MeshParameters, TSharedRef<BodyData> Body)
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


TOptional<FMeshDescription> FWireTranslatorImpl::GetMeshDescription(TSharedRef<IDatasmithMeshElement> MeshElement, CADLibrary::FMeshParameters& MeshParameters)
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
TOptional< FMeshDescription > FWireTranslatorImpl::ImportMesh(AlMesh& CurrentMesh, CADLibrary::FMeshParameters& MeshParameters)
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
#if WITH_EDITOR
	if (GIsEditor && !GEditor->PlayWorld && !GIsPlayInEditorWorld)
	{
#ifdef USE_OPENMODEL
		if (FPlatformProcess::GetDllHandle(TEXT("libalias_api.dll")))
		{
			// Check installed version of Alias Tools because binaries before 2021.3 are not compatible with Alias 2022
			uint64 FileVersion = FPlatformMisc::GetFileVersion(TEXT("libalias_api.dll"));

			if (FileVersion > LibAlias2020_Version && FileVersion < LibAlias2021_Version)
			{
				UE_LOG(LogDatasmithWireTranslator, Warning, TEXT(WRONG_VERSION_TEXT));
				OutCapabilities.bIsEnabled = false;
				return;
			}
			else if(FileVersion >= LibAlias2021_3_0_Version)
			{
				OutCapabilities.bIsEnabled = false;
				return;
			}

			OutCapabilities.SupportedFileFormats.Add(FFileFormatInfo{ TEXT("wire"), TEXT("AliasStudio 2021, Model files") });
			return;
		}
#endif
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
	CADLibrary::FImportParameters& ImportParameters = Translator->GetImportParameters();
	CADLibrary::FMeshParameters MeshParameters;
	if (TOptional< FMeshDescription > Mesh = Translator->GetMeshDescription(MeshElement, MeshParameters))
	{
		OutMeshPayload.LodMeshes.Add(MoveTemp(Mesh.GetValue()));

		DatasmithCoreTechParametricSurfaceData::AddCoreTechSurfaceDataForMesh(MeshElement, ImportParameters, MeshParameters, GetCommonTessellationOptions(), OutMeshPayload);
	}
	return OutMeshPayload.LodMeshes.Num() > 0;
#else
	return false;
#endif
}

void FDatasmithWireTranslator::SetSceneImportOptions(TArray<TStrongObjectPtr<UDatasmithOptionsBase>>& Options)
{
#ifdef USE_OPENMODEL
	FDatasmithCoreTechTranslator::SetSceneImportOptions(Options);

	if (Translator)
	{
		Translator->SetTessellationOptions( GetCommonTessellationOptions() );
	}
#endif
}

#undef LOCTEXT_NAMESPACE // "DatasmithWireTranslator"

