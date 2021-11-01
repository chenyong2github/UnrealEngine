// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MaterialShared.cpp: Shared material implementation.
=============================================================================*/

#include "MaterialShared.h"
#include "Stats/StatsMisc.h"
#include "UObject/CoreObjectVersion.h"
#include "UObject/FrameworkObjectVersion.h"
#include "Misc/App.h"
#include "UObject/UObjectHash.h"
#include "LocalVertexFactory.h"
#include "Materials/MaterialInterface.h"
#include "MaterialExpressionIO.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceBasePropertyOverrides.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialUniformExpressions.h"
#include "Materials/MaterialShaderMapLayout.h"
#include "UObject/UObjectIterator.h"
#include "ComponentReregisterContext.h"
#include "Materials/MaterialExpressionBreakMaterialAttributes.h"
#include "Materials/MaterialExpressionShadingModel.h"
#include "Materials/MaterialExpressionRerouteBase.h"
#include "Materials/MaterialExpressionSingleLayerWaterMaterialOutput.h"
#include "ShaderCompiler.h"
#include "MaterialCompiler.h"
#include "MeshMaterialShaderType.h"
#include "RendererInterface.h"
#include "Materials/HLSLMaterialTranslator.h"
#include "ComponentRecreateRenderStateContext.h"
#include "EngineModule.h"
#include "Engine/Texture.h"
#include "SceneView.h"
#include "ShaderPlatformQualitySettings.h"
#include "MaterialShaderQualitySettings.h"
#include "DecalRenderingCommon.h"
#include "ExternalTexture.h"
#include "ShaderCodeLibrary.h"
#include "HAL/FileManager.h"
#include "ProfilingDebugging/LoadTimeTracker.h"
#include "UObject/CoreRedirects.h"
#include "RayTracingDefinitions.h"
#include "Interfaces/ITargetPlatform.h"
#include "Misc/ConfigCacheIni.h"
#if WITH_EDITOR
#include "Rendering/StaticLightingSystemInterface.h"
#endif
#if WITH_ODSC
#include "ODSC/ODSCManager.h"
#endif

#define LOCTEXT_NAMESPACE "MaterialShared"

DEFINE_LOG_CATEGORY(LogMaterial);

IMPLEMENT_TYPE_LAYOUT(FHashedMaterialParameterInfo);
IMPLEMENT_TYPE_LAYOUT(FUniformExpressionSet);
IMPLEMENT_TYPE_LAYOUT(FMaterialCompilationOutput);
IMPLEMENT_TYPE_LAYOUT(FMeshMaterialShaderMap);
IMPLEMENT_TYPE_LAYOUT(FMaterialProcessedSource);
IMPLEMENT_TYPE_LAYOUT(FMaterialShaderMapContent);
IMPLEMENT_TYPE_LAYOUT(FMaterialPreshaderData);
IMPLEMENT_TYPE_LAYOUT(FMaterialUniformPreshaderHeader);
IMPLEMENT_TYPE_LAYOUT(FMaterialScalarParameterInfo);
IMPLEMENT_TYPE_LAYOUT(FMaterialVectorParameterInfo);
IMPLEMENT_TYPE_LAYOUT(FMaterialTextureParameterInfo);
IMPLEMENT_TYPE_LAYOUT(FMaterialExternalTextureParameterInfo);
IMPLEMENT_TYPE_LAYOUT(FMaterialVirtualTextureStack);

int32 GDeferUniformExpressionCaching = 1;
FAutoConsoleVariableRef CVarDeferUniformExpressionCaching(
	TEXT("r.DeferUniformExpressionCaching"),
	GDeferUniformExpressionCaching,
	TEXT("Whether to defer caching of uniform expressions until a rendering command needs them up to date.  Deferring updates is more efficient because multiple SetVectorParameterValue calls in a frame will only result in one update."),
	ECVF_RenderThreadSafe
	);

struct FAllowCachingStaticParameterValues
{
	FAllowCachingStaticParameterValues(FMaterial& InMaterial)
#if WITH_EDITOR
		: Material(InMaterial)
#endif // WITH_EDITOR
	{
#if WITH_EDITOR
		Material.BeginAllowCachingStaticParameterValues();
#endif // WITH_EDITOR
	};

#if WITH_EDITOR
	~FAllowCachingStaticParameterValues()
	{
		Material.EndAllowCachingStaticParameterValues();
	}

private:
	FMaterial& Material;
#endif // WITH_EDITOR
};

static FAutoConsoleCommand GFlushMaterialUniforms(
	TEXT("r.FlushMaterialUniforms"),
	TEXT(""),
	FConsoleCommandDelegate::CreateStatic(
		[]()
{
	for (TObjectIterator<UMaterialInterface> It; It; ++It)
	{
		UMaterialInterface* Material = *It;
		FMaterialRenderProxy* MaterialProxy = Material->GetRenderProxy();
		if (MaterialProxy)
		{
			MaterialProxy->CacheUniformExpressions_GameThread(false);
		}
	}
})
);

bool AllowDitheredLODTransition(ERHIFeatureLevel::Type FeatureLevel)
{
	// On mobile support for 'Dithered LOD Transition' has to be explicitly enabled in projects settings
	if (FeatureLevel <= ERHIFeatureLevel::ES3_1)
	{
		static TConsoleVariableData<int32>* CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Mobile.AllowDitheredLODTransition"));
		return (CVar && CVar->GetValueOnAnyThread() != 0) ? true : false;
	}
	return true;
}

FName MaterialQualityLevelNames[] = 
{
	FName(TEXT("Low")),
	FName(TEXT("High")),
	FName(TEXT("Medium")),
	FName(TEXT("Epic")),
	FName(TEXT("Num"))
};

static_assert(UE_ARRAY_COUNT(MaterialQualityLevelNames) == EMaterialQualityLevel::Num + 1, "Missing entry from material quality level names.");

void GetMaterialQualityLevelName(EMaterialQualityLevel::Type InQualityLevel, FString& OutName)
{
	check(InQualityLevel < UE_ARRAY_COUNT(MaterialQualityLevelNames));
	MaterialQualityLevelNames[(int32)InQualityLevel].ToString(OutName);
}

FName GetMaterialQualityLevelFName(EMaterialQualityLevel::Type InQualityLevel)
{
	check(InQualityLevel < UE_ARRAY_COUNT(MaterialQualityLevelNames));
	return MaterialQualityLevelNames[(int32)InQualityLevel];
}

#if STORE_ONLY_ACTIVE_SHADERMAPS
const FMaterialResourceLocOnDisk* FindMaterialResourceLocOnDisk(
	const TArray<FMaterialResourceLocOnDisk>& DiskLocations,
	ERHIFeatureLevel::Type FeatureLevel,
	EMaterialQualityLevel::Type QualityLevel)
{
	for (const FMaterialResourceLocOnDisk& Loc : DiskLocations)
	{
		if (Loc.QualityLevel == QualityLevel && Loc.FeatureLevel == FeatureLevel)
		{
			return &Loc;
		}
	}
	return nullptr;
}

static void GetReloadInfo(const FString& PackageName, FString* OutFilename)
{
	check(!GIsEditor);
	check(!PackageName.IsEmpty());
	FString& Filename = *OutFilename;

	// Handle name redirection and localization
	const FCoreRedirectObjectName RedirectedName =
		FCoreRedirects::GetRedirectedName(
			ECoreRedirectFlags::Type_Package,
			FCoreRedirectObjectName(NAME_None, NAME_None, *PackageName));
	FString LocalizedName;
	LocalizedName = FPackageName::GetDelegateResolvedPackagePath(RedirectedName.PackageName.ToString());
	LocalizedName = FPackageName::GetLocalizedPackagePath(LocalizedName);
	bool bSucceed = FPackageName::DoesPackageExist(LocalizedName, nullptr, &Filename);
	Filename = FPaths::ChangeExtension(Filename, TEXT(".uexp"));

	// Dynamic material resource loading requires split export to work
	check(bSucceed && IFileManager::Get().FileExists(*Filename));
}

bool ReloadMaterialResource(
	FMaterialResource* InOutMaterialResource,
	const FString& PackageName,
	uint32 OffsetToFirstResource,
	ERHIFeatureLevel::Type FeatureLevel,
	EMaterialQualityLevel::Type QualityLevel)
{
	LLM_SCOPE(ELLMTag::Shaders);
	SCOPED_LOADTIMER(SerializeInlineShaderMaps);

	FString Filename;
	GetReloadInfo(PackageName, &Filename);

	UE_LOG(LogMaterial, VeryVerbose, TEXT("Attempting to load material resources for package %s (file name: %s)."), *PackageName, *Filename);

	FMaterialResourceProxyReader Ar(*Filename, OffsetToFirstResource, FeatureLevel, QualityLevel);
	FMaterialResource& Tmp = *InOutMaterialResource;
	Tmp.SerializeInlineShaderMap(Ar);
	if (Tmp.GetGameThreadShaderMap())
	{
		//Tmp.GetGameThreadShaderMap()->RegisterSerializedShaders(false);
		return true;
	}
	UE_LOG(LogMaterial, Warning, TEXT("Failed to reload material resources for package %s (file name: %s)."), *PackageName, *Filename);
	return false;
}
#endif // STORE_ONLY_ACTIVE_SHADERMAPS

int32 FMaterialCompiler::Errorf(const TCHAR* Format,...)
{
	TCHAR	ErrorText[2048];
	GET_VARARGS( ErrorText, UE_ARRAY_COUNT(ErrorText), UE_ARRAY_COUNT(ErrorText)-1, Format, Format );
	return Error(ErrorText);
}

IMPLEMENT_STRUCT(ExpressionInput);
IMPLEMENT_STRUCT(ColorMaterialInput);
IMPLEMENT_STRUCT(ScalarMaterialInput);
IMPLEMENT_STRUCT(VectorMaterialInput);
IMPLEMENT_STRUCT(Vector2MaterialInput);
IMPLEMENT_STRUCT(MaterialAttributesInput);

#if WITH_EDITOR
int32 FExpressionInput::Compile(class FMaterialCompiler* Compiler)
{
	if(Expression)
	{
		Expression->ValidateState();
		
		int32 ExpressionResult = Compiler->CallExpression(FMaterialExpressionKey(Expression, OutputIndex, Compiler->GetMaterialAttribute(), Compiler->IsCurrentlyCompilingForPreviousFrame()),Compiler);

		if(Mask && ExpressionResult != INDEX_NONE)
		{
			return Compiler->ComponentMask(
				ExpressionResult,
				!!MaskR,!!MaskG,!!MaskB,!!MaskA
				);
		}
		else
		{
			return ExpressionResult;
		}
	}
	else
		return INDEX_NONE;
}

void FExpressionInput::Connect( int32 InOutputIndex, class UMaterialExpression* InExpression )
{
	OutputIndex = InOutputIndex;
	Expression = InExpression;

	TArray<FExpressionOutput> Outputs;
	Outputs = Expression->GetOutputs();
	FExpressionOutput* Output = &Outputs[ OutputIndex ];
	Mask = Output->Mask;
	MaskR = Output->MaskR;
	MaskG = Output->MaskG;
	MaskB = Output->MaskB;
	MaskA = Output->MaskA;
}
#endif // WITH_EDITOR

FExpressionInput FExpressionInput::GetTracedInput() const
{
#if WITH_EDITORONLY_DATA
	if (Expression != nullptr && Expression->IsA(UMaterialExpressionRerouteBase::StaticClass()))
	{
		UMaterialExpressionRerouteBase* Reroute = CastChecked<UMaterialExpressionRerouteBase>(Expression);
		return Reroute->TraceInputsToRealInput();
	}
#endif
	return *this;
}

/** Native serialize for FMaterialExpression struct */
static bool SerializeExpressionInput(FArchive& Ar, FExpressionInput& Input)
{
	Ar.UsingCustomVersion(FCoreObjectVersion::GUID);
	Ar.UsingCustomVersion(FFrameworkObjectVersion::GUID);

	if (Ar.CustomVer(FCoreObjectVersion::GUID) < FCoreObjectVersion::MaterialInputNativeSerialize)
	{
		return false;
	}

#if WITH_EDITORONLY_DATA
	if (!Ar.IsFilterEditorOnly())
	{
		Ar << Input.Expression;
	}
#endif
	Ar << Input.OutputIndex;
	if (Ar.CustomVer(FFrameworkObjectVersion::GUID) >= FFrameworkObjectVersion::PinsStoreFName)
	{
		Ar << Input.InputName;
	}
	else
	{
		FString InputNameStr;
		Ar << InputNameStr;
		Input.InputName = *InputNameStr;
	}

#if WITH_EDITORONLY_DATA
	Ar << Input.Mask;
	Ar << Input.MaskR;
	Ar << Input.MaskG;
	Ar << Input.MaskB;
	Ar << Input.MaskA;
#else
	int32 Temp = 0;
	Ar << Temp << Temp << Temp << Temp << Temp;
#endif

	// Some expressions may have been stripped when cooking and Expression can be null after loading
	// so make sure we keep the information about the connected node in cooked packages
	if ( Ar.IsFilterEditorOnly() )
	{
#if WITH_EDITORONLY_DATA
		if (Ar.IsSaving())
		{
			Input.ExpressionName = Input.Expression ? Input.Expression->GetFName() : NAME_None;
		}
#endif // WITH_EDITORONLY_DATA
		Ar << Input.ExpressionName;
	}

	return true;
}

template <typename InputType>
static bool SerializeMaterialInput(FArchive& Ar, FMaterialInput<InputType>& Input)
{
	if (SerializeExpressionInput(Ar, Input))
	{
#if WITH_EDITORONLY_DATA
		bool bUseConstantValue = Input.UseConstant;
		Ar << bUseConstantValue;
		Input.UseConstant = bUseConstantValue;
		Ar << Input.Constant;
#else
		bool bTemp = false;
		Ar << bTemp;
		InputType TempType;
		Ar << TempType;
#endif
		return true;
	}
	else
	{
		return false;
	}
}

bool FExpressionInput::Serialize(FArchive& Ar)
{
	return SerializeExpressionInput(Ar, *this);
}

bool FColorMaterialInput::Serialize(FArchive& Ar)
{
	return SerializeMaterialInput<FColor>(Ar, *this);
}

bool FScalarMaterialInput::Serialize(FArchive& Ar)
{
	return SerializeMaterialInput<float>(Ar, *this);
}

bool FShadingModelMaterialInput::Serialize(FArchive& Ar)
{
	return SerializeMaterialInput<uint32>(Ar, *this);
}

bool FVectorMaterialInput::Serialize(FArchive& Ar)
{
	return SerializeMaterialInput<FVector>(Ar, *this);
}

bool FVector2MaterialInput::Serialize(FArchive& Ar)
{
	return SerializeMaterialInput<FVector2D>(Ar, *this);
}

bool FMaterialAttributesInput::Serialize(FArchive& Ar)
{
	return SerializeExpressionInput(Ar, *this);
}

#if WITH_EDITOR
int32 FColorMaterialInput::CompileWithDefault(class FMaterialCompiler* Compiler, EMaterialProperty Property)
{
	if (UseConstant)
	{
		FLinearColor LinearColor(Constant);
		return Compiler->Constant3(LinearColor.R, LinearColor.G, LinearColor.B);
	}
	else if (Expression)
	{
		int32 ResultIndex = FExpressionInput::Compile(Compiler);
		if (ResultIndex != INDEX_NONE)
		{
			return ResultIndex;
		}
	}

	return Compiler->ForceCast(FMaterialAttributeDefinitionMap::CompileDefaultExpression(Compiler, Property), MCT_Float3);
}

int32 FScalarMaterialInput::CompileWithDefault(class FMaterialCompiler* Compiler, EMaterialProperty Property)
{
	if (UseConstant)
	{
		return Compiler->Constant(Constant);
	}
	else if (Expression)
	{
		int32 ResultIndex = FExpressionInput::Compile(Compiler);
		if (ResultIndex != INDEX_NONE)
		{
			return ResultIndex;
		}
	}
	
	return Compiler->ForceCast(FMaterialAttributeDefinitionMap::CompileDefaultExpression(Compiler, Property), MCT_Float1);
}

int32 FShadingModelMaterialInput::CompileWithDefault(class FMaterialCompiler* Compiler, EMaterialProperty Property)
{
	if (Expression)
	{
		int32 ResultIndex = FExpressionInput::Compile(Compiler);
		if (ResultIndex != INDEX_NONE)
		{
			return ResultIndex;
		}
	}

	return Compiler->ForceCast(FMaterialAttributeDefinitionMap::CompileDefaultExpression(Compiler, Property), MCT_ShadingModel, MFCF_ExactMatch);
}

int32 FVectorMaterialInput::CompileWithDefault(class FMaterialCompiler* Compiler, EMaterialProperty Property)
{
	if (UseConstant)
	{
		return Compiler->Constant3(Constant.X, Constant.Y, Constant.Z);
	}
	else if(Expression)
	{
		int32 ResultIndex = FExpressionInput::Compile(Compiler);
		if (ResultIndex != INDEX_NONE)
		{
			return ResultIndex;
		}
	}
	return Compiler->ForceCast(FMaterialAttributeDefinitionMap::CompileDefaultExpression(Compiler, Property), MCT_Float3);
}

int32 FVector2MaterialInput::CompileWithDefault(class FMaterialCompiler* Compiler, EMaterialProperty Property)
{
	if (UseConstant)
	{
		return Compiler->Constant2(Constant.X, Constant.Y);
	}
	else if (Expression)
	{
		int32 ResultIndex = FExpressionInput::Compile(Compiler);
		if (ResultIndex != INDEX_NONE)
		{
			return ResultIndex;
		}
	}

	return Compiler->ForceCast(FMaterialAttributeDefinitionMap::CompileDefaultExpression(Compiler, Property), MCT_Float2);
}

int32 FMaterialAttributesInput::CompileWithDefault(class FMaterialCompiler* Compiler, const FGuid& AttributeID)
{
	int32 Ret = INDEX_NONE;
	if(Expression)
	{
		FScopedMaterialCompilerAttribute ScopedMaterialCompilerAttribute(Compiler, AttributeID);
		Ret = FExpressionInput::Compile(Compiler);

		if (Ret != INDEX_NONE && !Expression->IsResultMaterialAttributes(OutputIndex))
		{
			Compiler->Error(TEXT("Cannot connect a non MaterialAttributes node to a MaterialAttributes pin."));
		}
	}

	EMaterialProperty Property = FMaterialAttributeDefinitionMap::GetProperty(AttributeID);
	SetConnectedProperty(Property, Ret != INDEX_NONE);

	if( Ret == INDEX_NONE )
	{
		Ret = FMaterialAttributeDefinitionMap::CompileDefaultExpression(Compiler, AttributeID);
	}

	return Ret;
}
#endif  // WITH_EDITOR

void FMaterial::GetShaderMapId(EShaderPlatform Platform, const ITargetPlatform* TargetPlatform, FMaterialShaderMapId& OutId) const
{ 
	if (bLoadedCookedShaderMapId)
	{
		if (GameThreadShaderMap && (IsInGameThread() || IsInAsyncLoadingThread()))
		{
			OutId = GameThreadShaderMap->GetShaderMapId();
		}
		else if (RenderingThreadShaderMap && IsInParallelRenderingThread())
		{
			OutId = RenderingThreadShaderMap->GetShaderMapId();
		}
		else
		{
			UE_LOG(LogMaterial, Fatal, TEXT("Tried to access cooked shader map ID from unknown thread"));
		}
	}
	else
	{
#if WITH_EDITOR
		OutId.LayoutParams.InitializeForPlatform(TargetPlatform);

		TArray<FShaderType*> ShaderTypes;
		TArray<FVertexFactoryType*> VFTypes;
		TArray<const FShaderPipelineType*> ShaderPipelineTypes;

		GetDependentShaderAndVFTypes(Platform, OutId.LayoutParams, ShaderTypes, ShaderPipelineTypes, VFTypes);

		OutId.Usage = GetShaderMapUsage();
		OutId.BaseMaterialId = GetMaterialId();
		OutId.QualityLevel = GetQualityLevel();
		OutId.FeatureLevel = GetFeatureLevel();
		OutId.SetShaderDependencies(ShaderTypes, ShaderPipelineTypes, VFTypes, Platform);
		GetReferencedTexturesHash(Platform, OutId.TextureReferencesHash);

#else
		OutId.QualityLevel = GetQualityLevel();
		OutId.FeatureLevel = GetFeatureLevel();

		if (TargetPlatform != nullptr)
		{
			UE_LOG(LogMaterial, Error, TEXT("FMaterial::GetShaderMapId: TargetPlatform is not null, but a cooked executable cannot target platforms other than its own."));
		}
		OutId.LayoutParams.InitializeForCurrent();

		UE_LOG(LogMaterial, Error, TEXT("Tried to access an uncooked shader map ID in a cooked application"));
#endif
	}
}

#if WITH_EDITORONLY_DATA
void FMaterial::GetStaticParameterSet(EShaderPlatform Platform, FStaticParameterSet& OutSet) const
{
	// Clear the set in default implementation
	OutSet = FStaticParameterSet();
}
#endif // WITH_EDITORONLY_DATA

EMaterialTessellationMode FMaterial::GetTessellationMode() const 
{ 
	return MTM_NoTessellation; 
}

ERefractionMode FMaterial::GetRefractionMode() const 
{ 
	return RM_IndexOfRefraction; 
}

#if WITH_EDITOR
void FMaterial::GetShaderMapIDsWithUnfinishedCompilation(TArray<int32>& ShaderMapIds)
{
#if 0
	// Build an array of the shader map Id's are not finished compiling.
	if (GameThreadShaderMap && !GameThreadShaderMap->IsCompilationFinalized())
	{
		ShaderMapIds.Add(GameThreadShaderMap->GetCompilingId());
	}
	else if (CompilingShaderMapId != 0u)
	{
		ShaderMapIds.Add(CompilingShaderMapId);
	}
#endif
	if (GameThreadCompilingShaderMapId != 0u)
	{
		ShaderMapIds.Add(GameThreadCompilingShaderMapId);
	}
}

bool FMaterial::IsCompilationFinished() const
{
#if 0
	// Build an array of the shader map Id's are not finished compiling.
	if (GameThreadShaderMap && !GameThreadShaderMap->IsCompilationFinalized())
	{
		return false;
	}
	else if (CompilingShaderMapId != 0u)
	{
		return false;
	}
	return true;
#endif
	return GameThreadCompilingShaderMapId == 0u;
}

void FMaterial::CancelCompilation()
{
	TArray<int32> ShaderMapIdsToCancel;
	GetShaderMapIDsWithUnfinishedCompilation(ShaderMapIdsToCancel);

	if (ShaderMapIdsToCancel.Num() > 0)
	{
		// Cancel all compile jobs for these shader maps.
		GShaderCompilingManager->CancelCompilation(*GetFriendlyName(), ShaderMapIdsToCancel);
	}
}

void FMaterial::FinishCompilation()
{
	TArray<int32> ShaderMapIdsToFinish;
	GetShaderMapIDsWithUnfinishedCompilation(ShaderMapIdsToFinish);

	if (ShaderMapIdsToFinish.Num() > 0)
	{
		// Block until the shader maps that we will save have finished being compiled
		GShaderCompilingManager->FinishCompilation(*GetFriendlyName(), ShaderMapIdsToFinish);
	}
}
#endif // WITH_EDITOR

bool FMaterial::HasValidGameThreadShaderMap() const
{
	if(!GameThreadShaderMap || !GameThreadShaderMap->IsCompilationFinalized())
	{
		return false;
	}
	return true;
}

const FMaterialShaderMap* FMaterial::GetShaderMapToUse() const 
{ 
	const FMaterialShaderMap* ShaderMapToUse = NULL;

	if (IsInGameThread())
	{
		// If we are accessing uniform texture expressions on the game thread, use results from a shader map whose compile is in flight that matches this material
		// This allows querying what textures a material uses even when it is being asynchronously compiled
		ShaderMapToUse = GameThreadShaderMap;
		if (!ShaderMapToUse && GameThreadCompilingShaderMapId != 0u)
		{
			ShaderMapToUse = FMaterialShaderMap::FindCompilingShaderMap(GameThreadCompilingShaderMapId);
		}

		checkf(!ShaderMapToUse || ShaderMapToUse->GetNumRefs() > 0, TEXT("NumRefs %i, GameThreadShaderMap 0x%08x"), ShaderMapToUse->GetNumRefs(), GetGameThreadShaderMap());
	}
	else 
	{
		ShaderMapToUse = GetRenderingThreadShaderMap();
	}

	return ShaderMapToUse;
}

const FUniformExpressionSet& FMaterial::GetUniformExpressions() const
{ 
	const FMaterialShaderMap* ShaderMapToUse = GetShaderMapToUse();
	if (ShaderMapToUse)
	{
		return ShaderMapToUse->GetUniformExpressionSet();
	}

	static const FUniformExpressionSet EmptyExpressions;
	return EmptyExpressions;
}

TArrayView<const FMaterialTextureParameterInfo> FMaterial::GetUniformTextureExpressions(EMaterialTextureParameterType Type) const
{
	return GetUniformExpressions().UniformTextureParameters[(uint32)Type];
}

TArrayView<const FMaterialVectorParameterInfo> FMaterial::GetUniformVectorParameterExpressions() const
{ 
	return GetUniformExpressions().UniformVectorParameters;
}

TArrayView<const FMaterialScalarParameterInfo> FMaterial::GetUniformScalarParameterExpressions() const
{ 
	return GetUniformExpressions().UniformScalarParameters;
}

bool FMaterial::RequiresSceneColorCopy_GameThread() const
{
	return GameThreadShaderMap.GetReference() ? GameThreadShaderMap->RequiresSceneColorCopy() : false; 
}

bool FMaterial::RequiresSceneColorCopy_RenderThread() const
{
	check(IsInParallelRenderingThread());
	if (RenderingThreadShaderMap)
	{
		return RenderingThreadShaderMap->RequiresSceneColorCopy();
	}
	return false;
}

bool FMaterial::NeedsSceneTextures() const 
{
	check(IsInParallelRenderingThread());

	if (RenderingThreadShaderMap)
	{
		return RenderingThreadShaderMap->NeedsSceneTextures();
	}
	
	return false;
}

bool FMaterial::NeedsGBuffer() const
{
	check(IsInParallelRenderingThread());

	if ((IsOpenGLPlatform(GMaxRHIShaderPlatform) || FDataDrivenShaderPlatformInfo::GetOverrideFMaterial_NeedsGBufferEnabled(GMaxRHIShaderPlatform)) // @todo: TTP #341211 
		&& !IsMobilePlatform(GMaxRHIShaderPlatform)) 
	{
		return true;
	}

	if (RenderingThreadShaderMap)
	{
		return RenderingThreadShaderMap->NeedsGBuffer();
	}

	return false;
}


bool FMaterial::UsesEyeAdaptation() const 
{
	check(IsInParallelRenderingThread());

	if (RenderingThreadShaderMap)
	{
		return RenderingThreadShaderMap->UsesEyeAdaptation();
	}

	return false;
}

bool FMaterial::UsesGlobalDistanceField_GameThread() const 
{ 
	return GameThreadShaderMap.GetReference() ? GameThreadShaderMap->UsesGlobalDistanceField() : false; 
}

bool FMaterial::UsesWorldPositionOffset_GameThread() const 
{ 
	return GameThreadShaderMap.GetReference() ? GameThreadShaderMap->UsesWorldPositionOffset() : false; 
}

bool FMaterial::MaterialModifiesMeshPosition_RenderThread() const
{ 
	check(IsInParallelRenderingThread());
	bool bUsesWPO = RenderingThreadShaderMap ? RenderingThreadShaderMap->ModifiesMeshPosition() : false;

	return bUsesWPO || GetTessellationMode() != MTM_NoTessellation;
}

bool FMaterial::MaterialModifiesMeshPosition_GameThread() const
{
	check(IsInGameThread());
	FMaterialShaderMap* ShaderMap = GameThreadShaderMap.GetReference();
	bool bUsesWPO = ShaderMap ? ShaderMap->ModifiesMeshPosition() : false;

	return bUsesWPO || GetTessellationMode() != MTM_NoTessellation;
}

bool FMaterial::MaterialMayModifyMeshPosition() const
{
	// Conservative estimate when called before material translation has occurred. 
	// This function is only intended for use in deciding whether or not shader permutations are required.
	return HasVertexPositionOffsetConnected() || HasPixelDepthOffsetConnected() || HasMaterialAttributesConnected() || GetTessellationMode() != MTM_NoTessellation
		|| (GetMaterialDomain() == MD_DeferredDecal && GetDecalBlendMode() == DBM_Volumetric_DistanceFunction);
}

bool FMaterial::MaterialUsesPixelDepthOffset() const
{
	check(IsInParallelRenderingThread());
	return RenderingThreadShaderMap ? RenderingThreadShaderMap->UsesPixelDepthOffset() : false;
}

bool FMaterial::MaterialUsesDistanceCullFade_GameThread() const
{
	return GameThreadShaderMap ? GameThreadShaderMap->UsesDistanceCullFade() : false;
}

bool FMaterial::MaterialUsesSceneDepthLookup_RenderThread() const
{
	check(IsInParallelRenderingThread());
	return RenderingThreadShaderMap ? RenderingThreadShaderMap->UsesSceneDepthLookup() : false;
}

bool FMaterial::MaterialUsesSceneDepthLookup_GameThread() const
{
	return GameThreadShaderMap.GetReference() ? GameThreadShaderMap->UsesSceneDepthLookup() : false;
}

bool FMaterial::UsesCustomDepthStencil_GameThread() const
{
	return GameThreadShaderMap.GetReference() ? (GameThreadShaderMap->UsesSceneTexture(PPI_CustomDepth) || GameThreadShaderMap->UsesSceneTexture(PPI_CustomStencil)) : false;
}

uint8 FMaterial::GetRuntimeVirtualTextureOutputAttibuteMask_RenderThread() const
{
	return RenderingThreadShaderMap ? RenderingThreadShaderMap->GetRuntimeVirtualTextureOutputAttributeMask() : 0;
}

bool FMaterial::MaterialUsesAnisotropy_GameThread() const
{
	return GameThreadShaderMap ? GameThreadShaderMap->UsesAnisotropy() : false;
}

bool FMaterial::MaterialUsesAnisotropy_RenderThread() const
{
	check(IsInParallelRenderingThread());
	return RenderingThreadShaderMap ? RenderingThreadShaderMap->UsesAnisotropy() : false;
}

void FMaterial::SetGameThreadShaderMap(FMaterialShaderMap* InMaterialShaderMap)
{
	checkSlow(IsInGameThread() || IsInAsyncLoadingThread());
	const bool bIsComplete = InMaterialShaderMap ? InMaterialShaderMap->IsComplete(this, true) : false;
	GameThreadShaderMap = InMaterialShaderMap;
	bGameThreadShaderMapIsComplete = bIsComplete;

	TRefCountPtr<FMaterial> Material = this;
	TRefCountPtr<FMaterialShaderMap> ShaderMap = InMaterialShaderMap;
	ENQUEUE_RENDER_COMMAND(SetGameThreadShaderMap)([Material = MoveTemp(Material), ShaderMap = MoveTemp(ShaderMap), bIsComplete](FRHICommandListImmediate& RHICmdList) mutable
	{
		Material->RenderingThreadShaderMap = MoveTemp(ShaderMap);
		Material->bRenderingThreadShaderMapIsComplete = bIsComplete;
	});
}

void FMaterial::UpdateInlineShaderMapIsComplete()
{
	checkSlow(IsInGameThread() || IsInAsyncLoadingThread());
	check(bContainsInlineShaders);
	// We expect inline shader maps to be complete, so we want to log missing shaders here
	const bool bSilent = false;
	const bool bIsComplete = GameThreadShaderMap->IsComplete(this, bSilent);

	bGameThreadShaderMapIsComplete = bIsComplete;
	TRefCountPtr<FMaterial> Material = this;
	ENQUEUE_RENDER_COMMAND(UpdateGameThreadShaderMapIsComplete)([Material = MoveTemp(Material), bIsComplete](FRHICommandListImmediate& RHICmdList) mutable
	{
		Material->bRenderingThreadShaderMapIsComplete = bIsComplete;
	});
}

void FMaterial::SetInlineShaderMap(FMaterialShaderMap* InMaterialShaderMap)
{
	checkSlow(IsInGameThread() || IsInAsyncLoadingThread());
	check(InMaterialShaderMap);

	GameThreadShaderMap = InMaterialShaderMap;
	bContainsInlineShaders = true;
	bLoadedCookedShaderMapId = true;

	// SetInlineShaderMap is called during PostLoad(), before given UMaterial(Instance) is fully initialized
	// Can't check for completeness yet
	bGameThreadShaderMapIsComplete = false;

	TRefCountPtr<FMaterial> Material = this;
	TRefCountPtr<FMaterialShaderMap> ShaderMap = InMaterialShaderMap;
	ENQUEUE_RENDER_COMMAND(SetInlineShaderMap)([Material = MoveTemp(Material), ShaderMap = MoveTemp(ShaderMap)](FRHICommandListImmediate& RHICmdList) mutable
	{
		Material->RenderingThreadShaderMap = MoveTemp(ShaderMap);
		Material->bRenderingThreadShaderMapIsComplete = false;
	});
}

void FMaterial::SetCompilingShaderMap(FMaterialShaderMap* InMaterialShaderMap)
{
	checkSlow(IsInGameThread());
	const uint32 CompilingShaderMapId = InMaterialShaderMap->GetCompilingId();
	if (CompilingShaderMapId != GameThreadCompilingShaderMapId)
	{
		ReleaseGameThreadCompilingShaderMap();

		GameThreadCompilingShaderMapId = CompilingShaderMapId;
		check(GameThreadCompilingShaderMapId != 0u);
		InMaterialShaderMap->AddCompilingDependency(this);

		TRefCountPtr<FMaterial> Material = this;
		TRefCountPtr<FSharedShaderCompilerEnvironment> PendingCompilerEnvironment = InMaterialShaderMap->GetPendingCompilerEnvironment();
		ENQUEUE_RENDER_COMMAND(SetCompilingShaderMap)([Material = MoveTemp(Material), CompilingShaderMapId, PendingCompilerEnvironment = MoveTemp(PendingCompilerEnvironment)](FRHICommandListImmediate& RHICmdList) mutable
		{
			Material->RenderingThreadCompilingShaderMapId = CompilingShaderMapId;
			Material->RenderingThreadPendingCompilerEnvironment = MoveTemp(PendingCompilerEnvironment);
		});
	}
}

bool FMaterial::ReleaseGameThreadCompilingShaderMap()
{
	bool bReleased = false;
	if (GameThreadCompilingShaderMapId != 0u)
	{
		FMaterialShaderMap* PrevShaderMap = FMaterialShaderMap::FindCompilingShaderMap(GameThreadCompilingShaderMapId);
		if (PrevShaderMap)
		{
			PrevShaderMap->RemoveCompilingDependency(this);
		}
		GameThreadCompilingShaderMapId = 0u;
		bReleased = true;
	}
	return bReleased;
}

void FMaterial::ReleaseRenderThreadCompilingShaderMap()
{
	checkSlow(IsInGameThread());

	TRefCountPtr<FMaterial> Material = this;
	ENQUEUE_RENDER_COMMAND(DeferredDestroyMaterial)([Material = MoveTemp(Material)](FRHICommandListImmediate& RHICmdList) mutable
	{
		Material->PrepareDestroy_RenderThread();
	});
}

FMaterialShaderMap* FMaterial::GetRenderingThreadShaderMap() const 
{ 
	check(IsInParallelRenderingThread());
	return RenderingThreadShaderMap; 
}

void FMaterial::SetRenderingThreadShaderMap(TRefCountPtr<FMaterialShaderMap>& InMaterialShaderMap)
{
	check(IsInRenderingThread());
	RenderingThreadShaderMap = MoveTemp(InMaterialShaderMap);
	bRenderingThreadShaderMapIsComplete = RenderingThreadShaderMap ? RenderingThreadShaderMap->IsComplete(this, true) : false;
}

void FMaterial::AddReferencedObjects(FReferenceCollector& Collector)
{
#if WITH_EDITOR
	Collector.AddReferencedObjects(ErrorExpressions);
#endif
}

struct FLegacyTextureLookup
{
	void Serialize(FArchive& Ar)
	{
		Ar << TexCoordIndex;
		Ar << TextureIndex;
		Ar << UScale;
		Ar << VScale;
	}

	int32 TexCoordIndex;
	int32 TextureIndex;	

	float UScale;
	float VScale;
};

FArchive& operator<<(FArchive& Ar, FLegacyTextureLookup& Ref)
{
	Ref.Serialize( Ar );
	return Ar;
}

void FMaterial::LegacySerialize(FArchive& Ar)
{
	if (Ar.UE4Ver() < VER_UE4_PURGED_FMATERIAL_COMPILE_OUTPUTS)
	{
		TArray<FString> LegacyStrings;
		Ar << LegacyStrings;

		TMap<UMaterialExpression*,int32> LegacyMap;
		Ar << LegacyMap;
		int32 LegacyInt;
		Ar << LegacyInt;

		FeatureLevel = ERHIFeatureLevel::SM4_REMOVED;
		QualityLevel = EMaterialQualityLevel::High;

#if !WITH_EDITOR
		FGuid Id_DEPRECATED;
		UE_LOG(LogMaterial, Error, TEXT("Attempted to serialize legacy material data at runtime, this content should be re-saved and re-cooked"));
#endif	
		Ar << Id_DEPRECATED;

		TArray<UTexture*> LegacyTextures;
		Ar << LegacyTextures;

		bool bTemp2;
		Ar << bTemp2;

		bool bTemp;
		Ar << bTemp;

		TArray<FLegacyTextureLookup> LegacyLookups;
		Ar << LegacyLookups;

		uint32 DummyDroppedFallbackComponents = 0;
		Ar << DummyDroppedFallbackComponents;
	}

	SerializeInlineShaderMap(Ar);
}

void FMaterial::SerializeInlineShaderMap(FArchive& Ar)
{
	bool bCooked = Ar.IsCooking();
	Ar << bCooked;

	if (FPlatformProperties::RequiresCookedData() && !bCooked && Ar.IsLoading())
	{
		UE_LOG(LogShaders, Fatal, TEXT("This platform requires cooked packages, and shaders were not cooked into this material %s."), *GetFriendlyName());
	}

	if (bCooked)
	{
		if (Ar.IsCooking())
		{
#if WITH_EDITOR
			FinishCompilation();

			bool bValid = GameThreadShaderMap != nullptr && GameThreadShaderMap->CompiledSuccessfully();
			
			Ar << bValid;

			if (bValid)
			{
				// do not put shader code of certain materials into the library
				bool bEnableInliningWorkaround = false;

				checkf(GConfig, TEXT("We expect GConfig to exist at this point"));
				FString SettingName(TEXT("bEnableInliningWorkaround_"));
				SettingName += Ar.CookingTarget()->IniPlatformName();

				GConfig->GetBool(TEXT("ShaderCodeLibrary"), *SettingName, bEnableInliningWorkaround, GEngineIni);

				bool bInlineShaderCode = bEnableInliningWorkaround && ShouldInlineShaderCode();
				GameThreadShaderMap->Serialize(Ar, true, false, bInlineShaderCode);
			}
			else
			{
				UE_LOG(LogMaterial, Warning, TEXT("Cooking a material resource (in %s hierarchy) that doesn't have a valid ShaderMap! %s"),
					*GetFriendlyName(),
					(GameThreadShaderMap != nullptr) ? TEXT("Shadermap exists but wasn't compiled successfully (yet?)") : TEXT("Shadermap pointer is null.")
					);
			}
#else
			UE_LOG(LogMaterial, Fatal, TEXT("Internal error: cooking outside the editor is not possible."));
			// unreachable
#endif
		}
		else
		{
			bool bValid = false;
			Ar << bValid;

			if (bValid)
			{
				TRefCountPtr<FMaterialShaderMap> LoadedShaderMap = new FMaterialShaderMap();
				if (LoadedShaderMap->Serialize(Ar, true, bCooked && Ar.IsLoading()))
				{
					GameThreadShaderMap = MoveTemp(LoadedShaderMap);
#if WITH_EDITOR
					GameThreadShaderMap->AssociateWithAsset(GetAssetPath());
#endif
				}
			}
			else
			{
				UE_LOG(LogMaterial, Error, TEXT("Loading a material resource %s with an invalid ShaderMap!"), *GetFriendlyName());
			}
		}
	}
}

void FMaterial::RegisterInlineShaderMap(bool bLoadedByCookedMaterial)
{
	if (GameThreadShaderMap)
	{
		// Toss the loaded shader data if this is a server only instance
		//@todo - don't cook it in the first place
		if (FApp::CanEverRender())
		{
			RenderingThreadShaderMap = GameThreadShaderMap;
			bRenderingThreadShaderMapIsComplete = GameThreadShaderMap->IsValidForRendering();
		}
		//GameThreadShaderMap->RegisterSerializedShaders(bLoadedByCookedMaterial);
	}
}

void FMaterialResource::LegacySerialize(FArchive& Ar)
{
	FMaterial::LegacySerialize(Ar);

	if (Ar.UE4Ver() < VER_UE4_PURGED_FMATERIAL_COMPILE_OUTPUTS)
	{
		int32 BlendModeOverrideValueTemp = 0;
		Ar << BlendModeOverrideValueTemp;
		bool bDummyBool = false;
		Ar << bDummyBool;
		Ar << bDummyBool;
	}
}

TArrayView<UObject* const> FMaterialResource::GetReferencedTextures() const
{
	if (MaterialInstance)
	{
		const TArrayView<UObject* const> Textures = MaterialInstance->GetReferencedTextures();
		if (Textures.Num())
		{
			return Textures;
		}
	}
	
	if (Material)
	{
		return Material->GetReferencedTextures();
	}

	return UMaterial::GetDefaultMaterial(MD_Surface)->GetReferencedTextures();
}

void FMaterialResource::AddReferencedObjects(FReferenceCollector& Collector)
{
	FMaterial::AddReferencedObjects(Collector);

	Collector.AddReferencedObject(Material);
	Collector.AddReferencedObject(MaterialInstance);
}

bool FMaterialResource::GetAllowDevelopmentShaderCompile()const
{
	return Material->bAllowDevelopmentShaderCompile;
}

void FMaterial::ReleaseShaderMap()
{
	UE_CLOG(IsOwnerBeginDestroyed(), LogMaterial, Error, TEXT("ReleaseShaderMap called on FMaterial %s, owner is BeginDestroyed"), *GetDebugName());

	if (GameThreadShaderMap)
	{
		GameThreadShaderMap = nullptr;
		
		TRefCountPtr<FMaterial> Material = this;
		ENQUEUE_RENDER_COMMAND(ReleaseShaderMap)(
		[Material = MoveTemp(Material)](FRHICommandList& RHICmdList)
		{
			Material->RenderingThreadShaderMap = nullptr;
			Material->bRenderingThreadShaderMapIsComplete = false;
		});
	}
}

void FMaterial::DiscardShaderMap()
{
	check(RenderingThreadShaderMap == nullptr);
	if (GameThreadShaderMap)
	{
		//GameThreadShaderMap->DiscardSerializedShaders();
		GameThreadShaderMap = nullptr;
	}
}

EMaterialDomain FMaterialResource::GetMaterialDomain() const { return Material->MaterialDomain; }
bool FMaterialResource::IsTangentSpaceNormal() const { return Material->bTangentSpaceNormal || (!Material->Normal.IsConnected() && !Material->bUseMaterialAttributes); }
bool FMaterialResource::ShouldInjectEmissiveIntoLPV() const { return Material->bUseEmissiveForDynamicAreaLighting; }
bool FMaterialResource::ShouldBlockGI() const { return Material->bBlockGI; }
bool FMaterialResource::ShouldGenerateSphericalParticleNormals() const { return Material->bGenerateSphericalParticleNormals; }
bool FMaterialResource::ShouldDisableDepthTest() const { return Material->bDisableDepthTest; }
bool FMaterialResource::ShouldWriteOnlyAlpha() const { return Material->bWriteOnlyAlpha; }
bool FMaterialResource::ShouldEnableResponsiveAA() const { return Material->bEnableResponsiveAA; }
bool FMaterialResource::ShouldDoSSR() const { return Material->bScreenSpaceReflections; }
bool FMaterialResource::ShouldDoContactShadows() const { return Material->bContactShadows; }
bool FMaterialResource::IsWireframe() const { return Material->Wireframe; }
bool FMaterialResource::IsUIMaterial() const { return Material->MaterialDomain == MD_UI; }
bool FMaterialResource::IsLightFunction() const { return Material->MaterialDomain == MD_LightFunction; }
bool FMaterialResource::IsUsedWithEditorCompositing() const { return Material->bUsedWithEditorCompositing; }
bool FMaterialResource::IsDeferredDecal() const { return Material->MaterialDomain == MD_DeferredDecal; }
bool FMaterialResource::IsVolumetricPrimitive() const { return Material->MaterialDomain == MD_Volume; }
bool FMaterialResource::IsSpecialEngineMaterial() const { return Material->bUsedAsSpecialEngineMaterial; }
bool FMaterialResource::HasVertexPositionOffsetConnected() const { return HasMaterialAttributesConnected() || (!Material->bUseMaterialAttributes && Material->WorldPositionOffset.IsConnected()); }
bool FMaterialResource::HasPixelDepthOffsetConnected() const { return HasMaterialAttributesConnected() || (!Material->bUseMaterialAttributes && Material->PixelDepthOffset.IsConnected()); }
bool FMaterialResource::HasMaterialAttributesConnected() const { return Material->bUseMaterialAttributes && Material->MaterialAttributes.IsConnected(); }
EMaterialShadingRate FMaterialResource::GetShadingRate() const { return Material->ShadingRate; }
bool FMaterialResource::ShouldWriteDepthToTranslucentMaterial() const { return Material->WriteDepthToTranslucentMaterial; }
FString FMaterialResource::GetBaseMaterialPathName() const { return Material->GetPathName(); }
FString FMaterialResource::GetDebugName() const
{
	if (MaterialInstance)
	{
		return FString::Printf(TEXT("%s (MI:%s)"), *GetBaseMaterialPathName(), *MaterialInstance->GetPathName());
	}

	return GetBaseMaterialPathName();
}

bool FMaterialResource::IsUsedWithSkeletalMesh() const
{
	return Material->bUsedWithSkeletalMesh;
}

bool FMaterialResource::IsUsedWithGeometryCache() const
{
	return Material->bUsedWithGeometryCache;
}

bool FMaterialResource::IsUsedWithWater() const
{
	return Material->bUsedWithWater;
}

bool FMaterialResource::IsUsedWithHairStrands() const
{
	return Material->bUsedWithHairStrands;
}

bool FMaterialResource::IsUsedWithLidarPointCloud() const
{
	return Material->bUsedWithLidarPointCloud;
}

bool FMaterialResource::IsUsedWithVirtualHeightfieldMesh() const
{
	return Material->bUsedWithVirtualHeightfieldMesh;
}

bool FMaterialResource::IsUsedWithLandscape() const
{
	return false;
}

bool FMaterialResource::IsUsedWithParticleSystem() const
{
	return Material->bUsedWithParticleSprites || Material->bUsedWithBeamTrails;
}

bool FMaterialResource::IsUsedWithParticleSprites() const
{
	return Material->bUsedWithParticleSprites;
}

bool FMaterialResource::IsUsedWithBeamTrails() const
{
	return Material->bUsedWithBeamTrails;
}

bool FMaterialResource::IsUsedWithMeshParticles() const
{
	return Material->bUsedWithMeshParticles;
}

bool FMaterialResource::IsUsedWithNiagaraSprites() const
{
	return Material->bUsedWithNiagaraSprites;
}

bool FMaterialResource::IsUsedWithNiagaraRibbons() const
{
	return Material->bUsedWithNiagaraRibbons;
}

bool FMaterialResource::IsUsedWithNiagaraMeshParticles() const
{
	return Material->bUsedWithNiagaraMeshParticles;
}

bool FMaterialResource::IsUsedWithStaticLighting() const
{
	return Material->bUsedWithStaticLighting;
}

bool FMaterialResource::IsUsedWithMorphTargets() const
{
	return Material->bUsedWithMorphTargets;
}

bool FMaterialResource::IsUsedWithSplineMeshes() const
{
	return Material->bUsedWithSplineMeshes;
}

bool FMaterialResource::IsUsedWithInstancedStaticMeshes() const
{
	return Material->bUsedWithInstancedStaticMeshes;
}

bool FMaterialResource::IsUsedWithGeometryCollections() const
{
	return Material->bUsedWithGeometryCollections;
}

bool FMaterialResource::IsUsedWithAPEXCloth() const
{
	return Material->bUsedWithClothing;
}

EMaterialTessellationMode FMaterialResource::GetTessellationMode() const 
{ 
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	return (EMaterialTessellationMode)Material->D3D11TessellationMode; 
PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

bool FMaterialResource::IsCrackFreeDisplacementEnabled() const 
{ 
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	return Material->bEnableCrackFreeDisplacement;
PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

bool FMaterialResource::IsTranslucencyAfterDOFEnabled() const 
{ 
	return Material->bEnableSeparateTranslucency && !IsUIMaterial() && !IsDeferredDecal();
}

bool FMaterialResource::IsDualBlendingEnabled(EShaderPlatform Platform) const
{
	const bool bIsMaterialEnabled = (Material->ShadingModel == MSM_ThinTranslucent);
	const bool bIsPlatformSupported = RHISupportsDualSourceBlending(Platform);
	return bIsMaterialEnabled && bIsPlatformSupported;
}

bool FMaterialResource::IsMobileSeparateTranslucencyEnabled() const
{
	return Material->bEnableMobileSeparateTranslucency && !IsUIMaterial() && !IsDeferredDecal();
}

bool FMaterialResource::IsAdaptiveTessellationEnabled() const
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	return Material->bEnableAdaptiveTessellation;
PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

bool FMaterialResource::IsFullyRough() const
{
	return Material->bFullyRough;
}

bool FMaterialResource::UseNormalCurvatureToRoughness() const
{
	return Material->bNormalCurvatureToRoughness;
}

bool FMaterialResource::IsUsingFullPrecision() const
{
	return Material->bUseFullPrecision;
}

bool FMaterialResource::IsUsingAlphaToCoverage() const
{
	return Material->bUseAlphaToCoverage && Material->MaterialDomain == EMaterialDomain::MD_Surface && Material->GetBlendMode() == EBlendMode::BLEND_Masked;
}

bool FMaterialResource::IsUsingPreintegratedGFForSimpleIBL() const
{
	return Material->bForwardRenderUsePreintegratedGFForSimpleIBL;
}

bool FMaterialResource::IsUsingHQForwardReflections() const
{
	return Material->bUseHQForwardReflections;
}

bool FMaterialResource::GetForwardBlendsSkyLightCubemaps() const
{
	return Material->bForwardBlendsSkyLightCubemaps;
}

bool FMaterialResource::IsUsingPlanarForwardReflections() const
{
	return Material->bUsePlanarForwardReflections
		// Don't use planar reflection if it is used only for mobile pixel projected reflection.
		&& (GetFeatureLevel() <= ERHIFeatureLevel::ES3_1 || GetMobilePlanarReflectionMode() != EMobilePlanarReflectionMode::MobilePPRExclusive);
}

bool FMaterialResource::IsNonmetal() const
{
	return !Material->bUseMaterialAttributes ?
			(!Material->Metallic.IsConnected() && !Material->Specular.IsConnected()) :
			!(Material->MaterialAttributes.IsConnected(MP_Specular) || Material->MaterialAttributes.IsConnected(MP_Metallic));
}

bool FMaterialResource::UseLmDirectionality() const
{
	return Material->bUseLightmapDirectionality;
}

/**
 * Should shaders compiled for this material be saved to disk?
 */
bool FMaterialResource::IsPersistent() const { return true; }

FGuid FMaterialResource::GetMaterialId() const
{
	// It's possible for Material to become null due to AddReferencedObjects
	return Material ? Material->StateId : FGuid();
}

ETranslucencyLightingMode FMaterialResource::GetTranslucencyLightingMode() const { return (ETranslucencyLightingMode)Material->TranslucencyLightingMode; }

float FMaterialResource::GetOpacityMaskClipValue() const 
{
	return MaterialInstance ? MaterialInstance->GetOpacityMaskClipValue() : Material->GetOpacityMaskClipValue();
}

bool FMaterialResource::GetCastDynamicShadowAsMasked() const
{
	return MaterialInstance ? MaterialInstance->GetCastDynamicShadowAsMasked() : Material->GetCastDynamicShadowAsMasked();
}

EBlendMode FMaterialResource::GetBlendMode() const 
{
	return MaterialInstance ? MaterialInstance->GetBlendMode() : Material->GetBlendMode();
}

ERefractionMode FMaterialResource::GetRefractionMode() const
{
	return Material->RefractionMode;
}

FMaterialShadingModelField FMaterialResource::GetShadingModels() const 
{
	return MaterialInstance ? MaterialInstance->GetShadingModels() : Material->GetShadingModels();
}

bool FMaterialResource::IsShadingModelFromMaterialExpression() const 
{
	return MaterialInstance ? MaterialInstance->IsShadingModelFromMaterialExpression() : Material->IsShadingModelFromMaterialExpression(); 
}

bool FMaterialResource::IsTwoSided() const 
{
	return MaterialInstance ? MaterialInstance->IsTwoSided() : Material->IsTwoSided();
}

bool FMaterialResource::IsDitheredLODTransition() const 
{
	if (!AllowDitheredLODTransition(GetFeatureLevel()))
	{
		return false;
	}

	return MaterialInstance ? MaterialInstance->IsDitheredLODTransition() : Material->IsDitheredLODTransition();
}

bool FMaterialResource::IsTranslucencyWritingCustomDepth() const
{
	// We cannot call UMaterial::IsTranslucencyWritingCustomDepth because we need to check the instance potentially overriden blend mode.
	return  Material->AllowTranslucentCustomDepthWrites != 0 && IsTranslucentBlendMode(GetBlendMode());
}

bool FMaterialResource::IsTranslucencyWritingVelocity() const
{
	return Material->IsTranslucencyWritingVelocity();
}

bool FMaterialResource::IsMasked() const 
{
	return MaterialInstance ? MaterialInstance->IsMasked() : Material->IsMasked();
}

bool FMaterialResource::IsDitherMasked() const 
{
	return Material->DitherOpacityMask;
}

bool FMaterialResource::AllowNegativeEmissiveColor() const 
{
	return Material->bAllowNegativeEmissiveColor;
}

bool FMaterialResource::IsDistorted() const { return Material->bUsesDistortion && IsTranslucentBlendMode(GetBlendMode()); }
float FMaterialResource::GetTranslucencyDirectionalLightingIntensity() const { return Material->TranslucencyDirectionalLightingIntensity; }
float FMaterialResource::GetTranslucentShadowDensityScale() const { return Material->TranslucentShadowDensityScale; }
float FMaterialResource::GetTranslucentSelfShadowDensityScale() const { return Material->TranslucentSelfShadowDensityScale; }
float FMaterialResource::GetTranslucentSelfShadowSecondDensityScale() const { return Material->TranslucentSelfShadowSecondDensityScale; }
float FMaterialResource::GetTranslucentSelfShadowSecondOpacity() const { return Material->TranslucentSelfShadowSecondOpacity; }
float FMaterialResource::GetTranslucentBackscatteringExponent() const { return Material->TranslucentBackscatteringExponent; }
FLinearColor FMaterialResource::GetTranslucentMultipleScatteringExtinction() const { return Material->TranslucentMultipleScatteringExtinction; }
float FMaterialResource::GetTranslucentShadowStartOffset() const { return Material->TranslucentShadowStartOffset; }
float FMaterialResource::GetRefractionDepthBiasValue() const { return Material->RefractionDepthBias; }
PRAGMA_DISABLE_DEPRECATION_WARNINGS
float FMaterialResource::GetMaxDisplacement() const { return Material->MaxDisplacement; }
PRAGMA_ENABLE_DEPRECATION_WARNINGS
bool FMaterialResource::ShouldApplyFogging() const { return Material->bUseTranslucencyVertexFog; }
bool FMaterialResource::ShouldApplyCloudFogging() const { return Material->bApplyCloudFogging; }
bool FMaterialResource::IsSky() const { return Material->bIsSky; }
bool FMaterialResource::ComputeFogPerPixel() const {return Material->bComputeFogPerPixel;}
FString FMaterialResource::GetFriendlyName() const { return *GetNameSafe(Material); } //avoid using the material instance name here, we want materials that share a shadermap to also share a friendly name.
FString FMaterialResource::GetAssetName() const { return MaterialInstance ? *GetNameSafe(MaterialInstance) : *GetNameSafe(Material); }

uint32 FMaterialResource::GetDecalBlendMode() const
{
	return Material->GetDecalBlendMode();
}

uint32 FMaterialResource::GetMaterialDecalResponse() const
{
	return Material->GetMaterialDecalResponse();
}

bool FMaterialResource::HasBaseColorConnected() const
{
	return HasMaterialAttributesConnected() || Material->HasBaseColorConnected();
}

bool FMaterialResource::HasNormalConnected() const
{
	return HasMaterialAttributesConnected() || Material->HasNormalConnected();
}

bool FMaterialResource::HasRoughnessConnected() const
{
	return HasMaterialAttributesConnected() || Material->HasRoughnessConnected();
}

bool FMaterialResource::HasSpecularConnected() const
{
	return HasMaterialAttributesConnected() || Material->HasSpecularConnected();
}

bool FMaterialResource::HasEmissiveColorConnected() const
{
	return HasMaterialAttributesConnected() || Material->HasEmissiveColorConnected();
}

bool FMaterialResource::HasAnisotropyConnected() const
{
	return HasMaterialAttributesConnected() || Material->HasAnisotropyConnected();
}

bool FMaterialResource::HasAmbientOcclusionConnected() const
{
	return HasMaterialAttributesConnected() || Material->HasAmbientOcclusionConnected();
}

bool FMaterialResource::RequiresSynchronousCompilation() const
{
	return Material->IsDefaultMaterial();
}

bool FMaterialResource::IsDefaultMaterial() const
{
	return Material->IsDefaultMaterial();
}

int32 FMaterialResource::GetNumCustomizedUVs() const
{
	return Material->NumCustomizedUVs;
}

int32 FMaterialResource::GetBlendableLocation() const
{
	return Material->BlendableLocation;
}

bool FMaterialResource::GetBlendableOutputAlpha() const
{
	return Material->BlendableOutputAlpha;
}

bool FMaterialResource::IsStencilTestEnabled() const
{
	return GetMaterialDomain() == MD_PostProcess && Material->bEnableStencilTest;
}

uint32 FMaterialResource::GetStencilRefValue() const
{
	return GetMaterialDomain() == MD_PostProcess ? Material->StencilRefValue : 0;
}

uint32 FMaterialResource::GetStencilCompare() const
{
	return GetMaterialDomain() == MD_PostProcess ? uint32(Material->StencilCompare.GetValue()) : 0;
}

bool FMaterialResource::HasRuntimeVirtualTextureOutput() const
{
	return Material->GetCachedExpressionData().bHasRuntimeVirtualTextureOutput;
}

bool FMaterialResource::HasMaterialLayers() const
{
	return Material->GetCachedExpressionData().DefaultLayers.Num() > 0;
}

bool FMaterialResource::CastsRayTracedShadows() const
{
	return Material->bCastRayTracedShadows;
}

UMaterialInterface* FMaterialResource::GetMaterialInterface() const 
{ 
	return MaterialInstance ? (UMaterialInterface*)MaterialInstance : (UMaterialInterface*)Material;
}

#if WITH_EDITOR
void FMaterialResource::NotifyCompilationFinished()
{
	UMaterial::NotifyCompilationFinished(MaterialInstance ? (UMaterialInterface*)MaterialInstance : (UMaterialInterface*)Material);
}

FName FMaterialResource::GetAssetPath() const
{
	FName OutermostName;
	if (MaterialInstance)
	{
		OutermostName = MaterialInstance->GetOutermost()->GetFName();
	}
	else if (Material)
	{
		OutermostName = Material->GetOutermost()->GetFName();
	}
	else
	{
		// neither is known
		return NAME_None;
	}

	return OutermostName;
}

bool FMaterialResource::ShouldInlineShaderCode() const
{
	if (IsSpecialEngineMaterial() || IsDefaultMaterial())
	{
		UE_LOG(LogMaterial, Display, TEXT("%s: shader code is inlined because the workaround is enabled and it's a special or default material"), *GetFriendlyName());
		return true;
	}

	// Check the material name against those configured to be enabled.
	// For the cooker commandlet, this check could be cached, but due to concerns of that cache possibly getting stale for edge cases like COTF and general work-aroundness of this
	// function, let's check the configs every time.

	FString OutermostName;
	if (MaterialInstance)
	{
		OutermostName = MaterialInstance->GetOutermost()->GetName();
	}
	else if (Material)
	{
		OutermostName = Material->GetOutermost()->GetName();
	}

	bool bNeedsToBeInlined = false;
	const FConfigSection* ShaderLibrarySec = GConfig->GetSectionPrivate(TEXT("ShaderCodeLibrary"), false, true, GEngineIni);
	if (ShaderLibrarySec)
	{
		TArray<FString> ConfiguredMaterials;
		ShaderLibrarySec->MultiFind(TEXT("MaterialToInline"), ConfiguredMaterials);

		for(const FString& ConfiguredMaterial : ConfiguredMaterials)
		{
			if (ConfiguredMaterial == OutermostName)
			{
				bNeedsToBeInlined = true;
				break;
			}
		}
	}

	UE_LOG(LogMaterial, Display, TEXT("%s (package %s): shader code is %s to be inlined as a workaround"), *GetFriendlyName(), *OutermostName, bNeedsToBeInlined ? TEXT("configured") : TEXT("NOT configured"));
	return bNeedsToBeInlined;
}
#endif

FString FMaterialResource::GetFullPath() const
{
	if (Material)
	{
		return Material->GetPathName();
	}

	return FString();
}

void FMaterialResource::GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize)
{
	TSet<const FMaterialShaderMap*> UniqueShaderMaps;
	UniqueShaderMaps.Add(GetGameThreadShaderMap());

	for (TSet<const FMaterialShaderMap*>::TConstIterator It(UniqueShaderMaps); It; ++It)
	{
		const FMaterialShaderMap* MaterialShaderMap = *It;
		if (MaterialShaderMap)
		{
			CumulativeResourceSize.AddDedicatedSystemMemoryBytes(MaterialShaderMap->GetFrozenContentSize());

			const FShaderMapResource* Resource = MaterialShaderMap->GetResource();
			if (Resource)
			{
				CumulativeResourceSize.AddDedicatedSystemMemoryBytes(Resource->GetSizeBytes());
			}

			// TODO - account for shader code size somehow, either inline or shared code
		}
	}
}

#if UE_CHECK_FMATERIAL_LIFETIME
uint32 FMaterial::AddRef() const
{
	const int32 Refs = NumDebugRefs.Increment();
	UE_CLOG(Refs <= 0, LogMaterial, Fatal, TEXT("FMaterial::AddRef, Invalid NumDebugRefs %d"), Refs);
	UE_CLOG(Refs > 5000, LogMaterial, Warning, TEXT("FMaterial::AddRef, Suspicious NumDebugRefs %d"), Refs);
	return uint32(Refs);
}

uint32 FMaterial::Release() const
{
	const int32 Refs = NumDebugRefs.Decrement();
	UE_CLOG(Refs < 0, LogMaterial, Fatal, TEXT("FMaterial::Release, Invalid NumDebugRefs %d"), Refs);
	UE_CLOG(Refs > 5000, LogMaterial, Warning, TEXT("FMaterial::Release, Suspicious NumDebugRefs %d"), Refs);
	return uint32(Refs);
}
#endif // UE_CHECK_FMATERIAL_LIFETIME

bool FMaterial::PrepareDestroy_GameThread()
{
	check(IsInGameThread());

	const bool bReleasedCompilingId = ReleaseGameThreadCompilingShaderMap();

#if WITH_EDITOR
	if (GIsEditor)
	{
		const FSetElementId FoundId = EditorLoadedMaterialResources.FindId(this);
		if (FoundId.IsValidId())
		{
			// Remove the material from EditorLoadedMaterialResources if found
			EditorLoadedMaterialResources.Remove(FoundId);
		}
	}
#endif // WITH_EDITOR

	return bReleasedCompilingId;
}

void FMaterial::PrepareDestroy_RenderThread()
{
	check(IsInRenderingThread());

	RenderingThreadCompilingShaderMapId = 0u;
	RenderingThreadPendingCompilerEnvironment.SafeRelease();
}

void FMaterial::DeferredDelete(FMaterial* InMaterial)
{
	if (InMaterial)
	{
		if (InMaterial->PrepareDestroy_GameThread())
		{
			TRefCountPtr<FMaterial> Material(InMaterial);
			ENQUEUE_RENDER_COMMAND(DeferredDestroyMaterial)([Material = MoveTemp(Material)](FRHICommandListImmediate& RHICmdList) mutable
			{
				FMaterial* MaterialToDelete = Material.GetReference();
				MaterialToDelete->PrepareDestroy_RenderThread();
				Material.SafeRelease();
				delete MaterialToDelete;
			});
		}
		else
		{
			delete InMaterial;
		}
	}
}

/**
 * Destructor
 */
FMaterial::~FMaterial()
{
	check(GameThreadCompilingShaderMapId == 0u);
	check(RenderingThreadCompilingShaderMapId == 0u);
	check(!RenderingThreadPendingCompilerEnvironment.IsValid());

#if UE_CHECK_FMATERIAL_LIFETIME
	const uint32 NumRemainingRefs = GetRefCount();
	UE_CLOG(NumRemainingRefs > 0u, LogMaterial, Fatal, TEXT("%s Leaked %d refs"), *GetDebugName(), NumRemainingRefs);
#endif // UE_CHECK_FMATERIAL_LIFETIME

#if WITH_EDITOR
	checkf(!EditorLoadedMaterialResources.Contains(this), TEXT("FMaterial is still in EditorLoadedMaterialResources when destroyed, should use FMaterial::DeferredDestroy to remove"));
#endif // WITH_EDITOR
}

/** Populates OutEnvironment with defines needed to compile shaders for this material. */
void FMaterial::SetupMaterialEnvironment(
	EShaderPlatform Platform,
	const FShaderParametersMetadata& InUniformBufferStruct,
	const FUniformExpressionSet& InUniformExpressionSet,
	FShaderCompilerEnvironment& OutEnvironment
	) const
{
	// Add the material uniform buffer definition.
	FShaderUniformBufferParameter::ModifyCompilationEnvironment(TEXT("Material"), InUniformBufferStruct, Platform, OutEnvironment);

	// Mark as using external texture if uniform expression contains external texture
	if (InUniformExpressionSet.UniformExternalTextureParameters.Num() > 0)
	{
		OutEnvironment.CompilerFlags.Add(CFLAG_UsesExternalTexture);
	}

	if ((RHISupportsTessellation(Platform) == false) || (GetTessellationMode() == MTM_NoTessellation))
	{
		// Make sure this define is in sync with FShaderCompilerInput::IsUsingTessellation
		OutEnvironment.SetDefine(TEXT("USING_TESSELLATION"),TEXT("0"));
	}
	else
	{
		// Make sure this define is in sync with FShaderCompilerInput::IsUsingTessellation
		OutEnvironment.SetDefine(TEXT("USING_TESSELLATION"),TEXT("1"));

		if (GetTessellationMode() == MTM_FlatTessellation)
		{
			OutEnvironment.SetDefine(TEXT("TESSELLATION_TYPE_FLAT"),TEXT("1"));
		}
		else if (GetTessellationMode() == MTM_PNTriangles)
		{
			OutEnvironment.SetDefine(TEXT("TESSELLATION_TYPE_PNTRIANGLES"),TEXT("1"));
		}

		// This is dominant vertex/edge information.  Note, mesh must have preprocessed neighbors IB of material will fallback to default.
		//  PN triangles needs preprocessed buffers regardless of c
		if (IsCrackFreeDisplacementEnabled())
		{
			OutEnvironment.SetDefine(TEXT("DISPLACEMENT_ANTICRACK"),TEXT("1"));
		}
		else
		{
			OutEnvironment.SetDefine(TEXT("DISPLACEMENT_ANTICRACK"),TEXT("0"));
		}

		// Set whether to enable the adaptive tessellation, which tries to maintain a uniform number of pixels per triangle.
		if (IsAdaptiveTessellationEnabled())
		{
			OutEnvironment.SetDefine(TEXT("USE_ADAPTIVE_TESSELLATION_FACTOR"),TEXT("1"));
		}
		else
		{
			OutEnvironment.SetDefine(TEXT("USE_ADAPTIVE_TESSELLATION_FACTOR"),TEXT("0"));
		}
		
	}

	switch(GetBlendMode())
	{
	case BLEND_Opaque:
	case BLEND_Masked:
		{
			// Only set MATERIALBLENDING_MASKED if the material is truly masked
			//@todo - this may cause mismatches with what the shader compiles and what the renderer thinks the shader needs
			// For example IsTranslucentBlendMode doesn't check IsMasked
			if(!WritesEveryPixel())
			{
				OutEnvironment.SetDefine(TEXT("MATERIALBLENDING_MASKED"),TEXT("1"));
			}
			else
			{
				OutEnvironment.SetDefine(TEXT("MATERIALBLENDING_SOLID"),TEXT("1"));
			}
			break;
		}
	case BLEND_AlphaComposite:
	{
		// Blend mode will reuse MATERIALBLENDING_TRANSLUCENT
		OutEnvironment.SetDefine(TEXT("MATERIALBLENDING_ALPHACOMPOSITE"), TEXT("1"));
		OutEnvironment.SetDefine(TEXT("MATERIALBLENDING_TRANSLUCENT"), TEXT("1"));
		break;
	}
	case BLEND_AlphaHoldout:
	{
		// Blend mode will reuse MATERIALBLENDING_TRANSLUCENT
		OutEnvironment.SetDefine(TEXT("MATERIALBLENDING_ALPHAHOLDOUT"), TEXT("1"));
		OutEnvironment.SetDefine(TEXT("MATERIALBLENDING_TRANSLUCENT"), TEXT("1"));
		break;
	}
	case BLEND_Translucent: OutEnvironment.SetDefine(TEXT("MATERIALBLENDING_TRANSLUCENT"),TEXT("1")); break;
	case BLEND_Additive: OutEnvironment.SetDefine(TEXT("MATERIALBLENDING_ADDITIVE"),TEXT("1")); break;
	case BLEND_Modulate: OutEnvironment.SetDefine(TEXT("MATERIALBLENDING_MODULATE"),TEXT("1")); break;
	default: 
		UE_LOG(LogMaterial, Warning, TEXT("Unknown material blend mode: %u  Setting to BLEND_Opaque"),(int32)GetBlendMode());
		OutEnvironment.SetDefine(TEXT("MATERIALBLENDING_SOLID"),TEXT("1"));
	}

	{
		EMaterialDecalResponse MaterialDecalResponse = (EMaterialDecalResponse)GetMaterialDecalResponse();

		// bit 0:color/1:normal/2:roughness to enable/disable parts of the DBuffer decal effect
		int32 MaterialDecalResponseMask = 0;

		switch(MaterialDecalResponse)
		{
			case MDR_None:					MaterialDecalResponseMask = 0; break;
			case MDR_ColorNormalRoughness:	MaterialDecalResponseMask = 1 + 2 + 4; break;
			case MDR_Color:					MaterialDecalResponseMask = 1; break;
			case MDR_ColorNormal:			MaterialDecalResponseMask = 1 + 2; break;
			case MDR_ColorRoughness:		MaterialDecalResponseMask = 1 + 4; break;
			case MDR_Normal:				MaterialDecalResponseMask = 2; break;
			case MDR_NormalRoughness:		MaterialDecalResponseMask = 2 + 4; break;
			case MDR_Roughness:				MaterialDecalResponseMask = 4; break;
			default:
				check(0);
		}

		OutEnvironment.SetDefine(TEXT("MATERIALDECALRESPONSEMASK"), MaterialDecalResponseMask);
	}

	switch(GetRefractionMode())
	{
	case RM_IndexOfRefraction: OutEnvironment.SetDefine(TEXT("REFRACTION_USE_INDEX_OF_REFRACTION"),TEXT("1")); break;
	case RM_PixelNormalOffset: OutEnvironment.SetDefine(TEXT("REFRACTION_USE_PIXEL_NORMAL_OFFSET"),TEXT("1")); break;
	default: 
		UE_LOG(LogMaterial, Warning, TEXT("Unknown material refraction mode: %u  Setting to RM_IndexOfRefraction"),(int32)GetRefractionMode());
		OutEnvironment.SetDefine(TEXT("REFRACTION_USE_INDEX_OF_REFRACTION"),TEXT("1"));
	}

	OutEnvironment.SetDefine(TEXT("USE_DITHERED_LOD_TRANSITION_FROM_MATERIAL"), IsDitheredLODTransition());
	OutEnvironment.SetDefine(TEXT("MATERIAL_TWOSIDED"), IsTwoSided());
	OutEnvironment.SetDefine(TEXT("MATERIAL_TANGENTSPACENORMAL"), IsTangentSpaceNormal());
	OutEnvironment.SetDefine(TEXT("GENERATE_SPHERICAL_PARTICLE_NORMALS"),ShouldGenerateSphericalParticleNormals());
	OutEnvironment.SetDefine(TEXT("MATERIAL_USES_SCENE_COLOR_COPY"), RequiresSceneColorCopy_GameThread());
	OutEnvironment.SetDefine(TEXT("MATERIAL_USE_PREINTEGRATED_GF"), IsUsingPreintegratedGFForSimpleIBL());
	OutEnvironment.SetDefine(TEXT("MATERIAL_HQ_FORWARD_REFLECTION_CAPTURES"), IsUsingHQForwardReflections());
	OutEnvironment.SetDefine(TEXT("MATERIAL_FORWARD_BLENDS_SKYLIGHT_CUBEMAPS"), GetForwardBlendsSkyLightCubemaps());
	OutEnvironment.SetDefine(TEXT("MATERIAL_PLANAR_FORWARD_REFLECTIONS"), IsUsingPlanarForwardReflections());
	OutEnvironment.SetDefine(TEXT("MATERIAL_NONMETAL"), IsNonmetal());
	OutEnvironment.SetDefine(TEXT("MATERIAL_USE_LM_DIRECTIONALITY"), UseLmDirectionality());
	OutEnvironment.SetDefine(TEXT("MATERIAL_INJECT_EMISSIVE_INTO_LPV"), ShouldInjectEmissiveIntoLPV());
	OutEnvironment.SetDefine(TEXT("MATERIAL_SSR"), ShouldDoSSR() && IsTranslucentBlendMode(GetBlendMode()));
	OutEnvironment.SetDefine(TEXT("MATERIAL_CONTACT_SHADOWS"), ShouldDoContactShadows() && IsTranslucentBlendMode(GetBlendMode()));
	OutEnvironment.SetDefine(TEXT("MATERIAL_BLOCK_GI"), ShouldBlockGI());
	OutEnvironment.SetDefine(TEXT("MATERIAL_DITHER_OPACITY_MASK"), IsDitherMasked());
	OutEnvironment.SetDefine(TEXT("MATERIAL_NORMAL_CURVATURE_TO_ROUGHNESS"), UseNormalCurvatureToRoughness() ? TEXT("1") : TEXT("0"));
	OutEnvironment.SetDefine(TEXT("MATERIAL_ALLOW_NEGATIVE_EMISSIVECOLOR"), AllowNegativeEmissiveColor());
	OutEnvironment.SetDefine(TEXT("MATERIAL_OUTPUT_OPACITY_AS_ALPHA"), GetBlendableOutputAlpha());
	OutEnvironment.SetDefine(TEXT("TRANSLUCENT_SHADOW_WITH_MASKED_OPACITY"), GetCastDynamicShadowAsMasked());
	OutEnvironment.SetDefine(TEXT("MATERIAL_USE_ALPHA_TO_COVERAGE"), IsUsingAlphaToCoverage());

	if (IsUsingFullPrecision())
	{
		OutEnvironment.CompilerFlags.Add(CFLAG_UseFullPrecisionInPS);
	}

	if(GetMaterialDomain() == MD_DeferredDecal)
	{
		// to compare against DECAL_BLEND_MODE, we can expose more if needed
		OutEnvironment.SetDefine(TEXT("DECALBLENDMODEID_VOLUMETRIC"), (uint32)DBM_Volumetric_DistanceFunction);
		OutEnvironment.SetDefine(TEXT("DECALBLENDMODEID_STAIN"), (uint32)DBM_Stain);
		OutEnvironment.SetDefine(TEXT("DECALBLENDMODEID_NORMAL"), (uint32)DBM_Normal);
		OutEnvironment.SetDefine(TEXT("DECALBLENDMODEID_EMISSIVE"), (uint32)DBM_Emissive);
		OutEnvironment.SetDefine(TEXT("DECALBLENDMODEID_TRANSLUCENT"), (uint32)DBM_Translucent);
		OutEnvironment.SetDefine(TEXT("DECALBLENDMODEID_AO"), (uint32)DBM_AmbientOcclusion);
		OutEnvironment.SetDefine(TEXT("DECALBLENDMODEID_ALPHACOMPOSITE"), (uint32)DBM_AlphaComposite);
	}

	switch(GetMaterialDomain())
	{
		case MD_Surface:				OutEnvironment.SetDefine(TEXT("MATERIAL_DOMAIN_SURFACE"),			TEXT("1")); break;
		case MD_DeferredDecal:			OutEnvironment.SetDefine(TEXT("MATERIAL_DOMAIN_DEFERREDDECAL"),		TEXT("1")); break;
		case MD_LightFunction:			OutEnvironment.SetDefine(TEXT("MATERIAL_DOMAIN_LIGHTFUNCTION"),		TEXT("1")); break;
		case MD_Volume:					OutEnvironment.SetDefine(TEXT("MATERIAL_DOMAIN_VOLUME"),			TEXT("1")); break;
		case MD_PostProcess:			OutEnvironment.SetDefine(TEXT("MATERIAL_DOMAIN_POSTPROCESS"),		TEXT("1")); break;
		case MD_UI:						OutEnvironment.SetDefine(TEXT("MATERIAL_DOMAIN_UI"),				TEXT("1")); break;
		default:
			UE_LOG(LogMaterial, Warning, TEXT("Unknown material domain: %u  Setting to MD_Surface"),(int32)GetMaterialDomain());
			OutEnvironment.SetDefine(TEXT("MATERIAL_DOMAIN_SURFACE"),TEXT("1"));
	};

	if (IsTranslucentBlendMode(GetBlendMode()))
	{
		switch(GetTranslucencyLightingMode())
		{
		case TLM_VolumetricNonDirectional: OutEnvironment.SetDefine(TEXT("TRANSLUCENCY_LIGHTING_VOLUMETRIC_NONDIRECTIONAL"),TEXT("1")); break;
		case TLM_VolumetricDirectional: OutEnvironment.SetDefine(TEXT("TRANSLUCENCY_LIGHTING_VOLUMETRIC_DIRECTIONAL"),TEXT("1")); break;
		case TLM_VolumetricPerVertexNonDirectional: OutEnvironment.SetDefine(TEXT("TRANSLUCENCY_LIGHTING_VOLUMETRIC_PERVERTEX_NONDIRECTIONAL"),TEXT("1")); break;
		case TLM_VolumetricPerVertexDirectional: OutEnvironment.SetDefine(TEXT("TRANSLUCENCY_LIGHTING_VOLUMETRIC_PERVERTEX_DIRECTIONAL"),TEXT("1")); break;
		case TLM_Surface: OutEnvironment.SetDefine(TEXT("TRANSLUCENCY_LIGHTING_SURFACE_LIGHTINGVOLUME"),TEXT("1")); break;
		case TLM_SurfacePerPixelLighting: OutEnvironment.SetDefine(TEXT("TRANSLUCENCY_LIGHTING_SURFACE_FORWARDSHADING"),TEXT("1")); break;

		default: 
			UE_LOG(LogMaterial, Warning, TEXT("Unknown lighting mode: %u"),(int32)GetTranslucencyLightingMode());
			OutEnvironment.SetDefine(TEXT("TRANSLUCENCY_LIGHTING_VOLUMETRIC_NONDIRECTIONAL"),TEXT("1")); break;
		};
	}

	if( IsUsedWithEditorCompositing() )
	{
		OutEnvironment.SetDefine(TEXT("EDITOR_PRIMITIVE_MATERIAL"),TEXT("1"));
	}

	if (IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM5))
	{	
		static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.StencilForLODDither"));
		OutEnvironment.SetDefine(TEXT("USE_STENCIL_LOD_DITHER_DEFAULT"), CVar->GetValueOnAnyThread() != 0 ? 1 : 0);
	}

	if (GetShadingRate() != MSR_1x1)
	{
		OutEnvironment.SetDefine(TEXT("USING_VARIABLE_RATE_SHADING"), TEXT("1"));
	}

	{
		switch (GetMaterialDomain())
		{
			case MD_Surface:		OutEnvironment.SetDefine(TEXT("MATERIALDOMAIN_SURFACE"), 1u); break;
			case MD_DeferredDecal:	OutEnvironment.SetDefine(TEXT("MATERIALDOMAIN_DEFERREDDECAL"), 1u); break;
			case MD_LightFunction:	OutEnvironment.SetDefine(TEXT("MATERIALDOMAIN_LIGHTFUNCTION"), 1u); break;
			case MD_PostProcess:	OutEnvironment.SetDefine(TEXT("MATERIALDOMAIN_POSTPROCESS"), 1u); break;
			case MD_UI:				OutEnvironment.SetDefine(TEXT("MATERIALDOMAIN_UI"), 1u); break;
		}
	}
}

/**
 * Caches the material shaders for this material with no static parameters on the given platform.
 * This is used by material resources of UMaterials.
 */
bool FMaterial::CacheShaders(EShaderPlatform Platform, EMaterialShaderPrecompileMode PrecompileMode, const ITargetPlatform* TargetPlatform)
{
	FAllowCachingStaticParameterValues AllowCachingStaticParameterValues(*this);
	FMaterialShaderMapId NoStaticParametersId;
	GetShaderMapId(Platform, TargetPlatform, NoStaticParametersId);
	return CacheShaders(NoStaticParametersId, Platform, PrecompileMode, TargetPlatform);
}

/**
 * Caches the material shaders for the given static parameter set and platform.
 * This is used by material resources of UMaterialInstances.
 */
bool FMaterial::CacheShaders(const FMaterialShaderMapId& ShaderMapId, EShaderPlatform Platform, EMaterialShaderPrecompileMode PrecompileMode, const ITargetPlatform* TargetPlatform)
{
	UE_CLOG(!ShaderMapId.IsValid(), LogMaterial, Warning, TEXT("Invalid shader map ID caching shaders for '%s', will use default material."), *GetFriendlyName());
#if WITH_EDITOR
	FString DDCKeyHash;
#endif // WITH_EDITOR

	// If we loaded this material with inline shaders, use what was loaded (GameThreadShaderMap) instead of looking in the DDC
	if (bContainsInlineShaders)
	{
		TRefCountPtr<FMaterialShaderMap> ExistingShaderMap = nullptr;
		
		if (GameThreadShaderMap)
		{
			// Note: in the case of an inlined shader map, the shadermap Id will not be valid because we stripped some editor-only data needed to create it
			// Get the shadermap Id from the shadermap that was inlined into the package, if it exists
			ExistingShaderMap = FMaterialShaderMap::FindId(GameThreadShaderMap->GetShaderMapId(), Platform);
		}

		// Re-use an identical shader map in memory if possible, removing the reference to the inlined shader map
		if (ExistingShaderMap)
		{
			SetGameThreadShaderMap(ExistingShaderMap);
		}
		else if (GameThreadShaderMap)
		{
			// We are going to use the inlined shader map, register it so it can be re-used by other materials
			UpdateInlineShaderMapIsComplete();
			GameThreadShaderMap->Register(Platform);
		}
	}
	else
	{
#if WITH_EDITOR
		TRefCountPtr<FMaterialShaderMap> ShaderMap = FMaterialShaderMap::FindId(ShaderMapId, Platform);
		if (ShaderMap)
		{
			if (ShaderMap->GetCompilingId() != 0u)
			{
				SetCompilingShaderMap(ShaderMap);
				ShaderMap = ShaderMap->GetFinalizedClone();
			}
		}

		// Attempt to load from the derived data cache if we are uncooked and don't have any shadermap.
		// If we have an incomplete shadermap, continue with it to prevent creation of duplicate shadermaps for the same ShaderMapId
		if (!ShaderMap && !FPlatformProperties::RequiresCookedData())
		{
			TRefCountPtr<FMaterialShaderMap> LoadedShaderMap;
			FMaterialShaderMap::LoadFromDerivedDataCache(this, ShaderMapId, Platform, TargetPlatform, LoadedShaderMap, DDCKeyHash);
			ShaderMap = LoadedShaderMap;
		}

		check(!ShaderMap || ShaderMap->GetFrozenContentSize() > 0u);
		SetGameThreadShaderMap(ShaderMap);
#endif // WITH_EDITOR
	}

#if WITH_EDITOR
	// some of the above paths did not mark the shader map as associated with an asset, do so
	if (GameThreadShaderMap)
	{
		GameThreadShaderMap->AssociateWithAsset(GetAssetPath());
	}
#endif

	UMaterialInterface* MaterialInterface = GetMaterialInterface();
	const bool bMaterialInstance = MaterialInterface && MaterialInterface->IsA(UMaterialInstance::StaticClass());
	const bool bSpecialEngineMaterial = !bMaterialInstance && (IsSpecialEngineMaterial() || IsDefaultMaterial());

	bool bShaderMapValid = (bool)GameThreadShaderMap;
	if (bShaderMapValid && bSpecialEngineMaterial)
	{
		// Special engine materials (default materials) are required to be complete
		bShaderMapValid = GameThreadShaderMap->IsComplete(this, false);
	}

	if (!bShaderMapValid)
	{
		if (bContainsInlineShaders || FPlatformProperties::RequiresCookedData())
		{
			if (bSpecialEngineMaterial)
			{
				UMaterialInterface* Interface = GetMaterialInterface();
				FString Instance;
				if (Interface)
				{
					Instance = Interface->GetPathName();
				}

				//assert if the default material's shader map was not found, since it will cause problems later
				UE_LOG(LogMaterial, Fatal,TEXT("Failed to find shader map for default material %s(%s)! Please make sure cooking was successful (%s inline shaders, %s GTSM)"),
					*GetFriendlyName(),
					*Instance,
					bContainsInlineShaders ? TEXT("Contains") : TEXT("No"),
					GameThreadShaderMap ? TEXT("has") : TEXT("null")
				);
			}
			else
			{
				UE_LOG(LogMaterial, Log, TEXT("Can't compile %s with cooked content, will use default material instead"), *GetFriendlyName());
			}

			// Reset the shader map so the default material will be used.
			SetGameThreadShaderMap(nullptr);
		}
		else
		{
			const TCHAR* ShaderMapCondition;
			if (GameThreadShaderMap)
			{
				ShaderMapCondition = TEXT("Incomplete");
			}
			else
			{
				ShaderMapCondition = TEXT("Missing");
			}
#if WITH_EDITOR
			UE_LOG(LogMaterial, Display, TEXT("%s cached shadermap for %s in %s, %s, %s (DDC key hash: %s), compiling. %s"),
				ShaderMapCondition,
				*GetAssetName(),
				*LexToString(Platform),
				*LexToString(ShaderMapId.QualityLevel),
				*LexToString(ShaderMapId.FeatureLevel),
				*DDCKeyHash,
				IsSpecialEngineMaterial() ? TEXT("Is special engine material.") : TEXT("") 
			);
#else
			UE_LOG(LogMaterial, Display, TEXT("%s cached shader map for material %, compiling. %s"),
				ShaderMapCondition,
				*GetAssetName(),
				IsSpecialEngineMaterial() ? TEXT("Is special engine material.") : TEXT("")
			);
#endif

#if WITH_EDITORONLY_DATA
			FStaticParameterSet StaticParameterSet;
			GetStaticParameterSet(Platform, StaticParameterSet);

			// If there's no cached shader map for this material, compile a new one.
			// This is just kicking off the async compile, GameThreadShaderMap will not be complete yet
			bShaderMapValid = BeginCompileShaderMap(ShaderMapId, StaticParameterSet, Platform, PrecompileMode, TargetPlatform);
#endif // WITH_EDITORONLY_DATA

			if (!bShaderMapValid)
			{
				// If it failed to compile the material, reset the shader map so the material isn't used.
				SetGameThreadShaderMap(nullptr);

#if WITH_EDITOR
				if (IsDefaultMaterial())
				{
					for (int32 ErrorIndex = 0; ErrorIndex < CompileErrors.Num(); ErrorIndex++)
					{
						// Always log material errors in an unsuppressed category
						UE_LOG(LogMaterial, Warning, TEXT("	%s"), *CompileErrors[ErrorIndex]);
					}

					// Assert if the default material could not be compiled, since there will be nothing for other failed materials to fall back on.
					UE_LOG(LogMaterial, Fatal,TEXT("Failed to compile default material %s!"), *GetFriendlyName());
				}
#endif // WITH_EDITOR
			}
		}
	}
	else
	{
#if WITH_EDITOR
		// Clear outdated compile errors as we're not calling Translate on this path
		CompileErrors.Empty();
#endif // WITH_EDITOR
	}

	return bShaderMapValid;
}

/**
* Compiles this material for Platform
*
* @param ShaderMapId - the set of static parameters to compile
* @param Platform - the platform to compile for
* @param StaticParameterSet - static parameters
* @return - true if compile succeeded or was not necessary (shader map for ShaderMapId was found and was complete)
*/
bool FMaterial::BeginCompileShaderMap(
	const FMaterialShaderMapId& ShaderMapId, 
	const FStaticParameterSet &StaticParameterSet,
	EShaderPlatform Platform,
	EMaterialShaderPrecompileMode PrecompileMode,
	const ITargetPlatform* TargetPlatform)
{
#if WITH_EDITORONLY_DATA
	bool bSuccess = false;

	STAT(double MaterialCompileTime = 0);

	TRefCountPtr<FMaterialShaderMap> NewShaderMap = new FMaterialShaderMap();

	SCOPE_SECONDS_COUNTER(MaterialCompileTime);

#if WITH_EDITOR
	NewShaderMap->AssociateWithAsset(GetAssetPath());
#endif
	// Generate the material shader code.
	FMaterialCompilationOutput NewCompilationOutput;
	FHLSLMaterialTranslator MaterialTranslator(this, NewCompilationOutput, StaticParameterSet, Platform,GetQualityLevel(), ShaderMapId.FeatureLevel, TargetPlatform);
	bSuccess = MaterialTranslator.Translate();

	if(bSuccess)
	{
		// Create a shader compiler environment for the material that will be shared by all jobs from this material
		TRefCountPtr<FSharedShaderCompilerEnvironment> MaterialEnvironment = new FSharedShaderCompilerEnvironment();
		MaterialEnvironment->TargetPlatform = TargetPlatform;
		MaterialTranslator.GetMaterialEnvironment(Platform, *MaterialEnvironment);
		const FString MaterialShaderCode = MaterialTranslator.GetMaterialShaderCode();

		MaterialEnvironment->IncludeVirtualPathToContentsMap.Add(TEXT("/Engine/Generated/Material.ush"), MaterialShaderCode);

		// Setup the material compilation environment.
		{
			FShaderParametersMetadata* UniformBufferStruct = NewCompilationOutput.UniformExpressionSet.CreateBufferStruct();
			SetupMaterialEnvironment(Platform, *UniformBufferStruct, NewCompilationOutput.UniformExpressionSet, *MaterialEnvironment);
			delete UniformBufferStruct;
		}

		const bool bSkipCompilationForODSC = !(IsSpecialEngineMaterial() || IsDefaultMaterial()) && GShaderCompilingManager->IsShaderCompilationSkipped();
		if (bSkipCompilationForODSC)
		{
			// Force compilation off.
			PrecompileMode = EMaterialShaderPrecompileMode::None;
		}
		else if (RequiresSynchronousCompilation())
		{
			// Force sync compilation by material
			PrecompileMode = EMaterialShaderPrecompileMode::Synchronous;
		}
		else if (!GShaderCompilingManager->AllowAsynchronousShaderCompiling() && PrecompileMode != EMaterialShaderPrecompileMode::None)
		{
			// No support for background async compile
			PrecompileMode = EMaterialShaderPrecompileMode::Synchronous;
		}

		// Compile the shaders for the material.
		NewShaderMap->Compile(this, ShaderMapId, MaterialEnvironment, NewCompilationOutput, Platform, PrecompileMode);

		if (PrecompileMode == EMaterialShaderPrecompileMode::Synchronous)
		{
			// If this is a synchronous compile, assign the compile result to the output
			check(NewShaderMap->GetCompilingId() == 0u);
			if (NewShaderMap->CompiledSuccessfully())
			{
				NewShaderMap->FinalizeContent();
				SetGameThreadShaderMap(NewShaderMap);
			}
			else
			{
				SetGameThreadShaderMap(nullptr);
			}
		}
		else if (PrecompileMode == EMaterialShaderPrecompileMode::None && bSkipCompilationForODSC)
		{
			// We didn't perform a compile so do ODSC specific cleanup here.
			ReleaseGameThreadCompilingShaderMap();
			ReleaseRenderThreadCompilingShaderMap();

			NewShaderMap->ReleaseCompilingId();
			check(NewShaderMap->GetCompilingId() == 0u);

			// Tell the map it was successful even though we didn't compile shaders into.
			// This ensures the map will be saved and cooked out.
			NewShaderMap->SetCompiledSuccessfully(true);

			// We didn't compile any shaders but still assign the result
			NewShaderMap->FinalizeContent();
			SetGameThreadShaderMap(NewShaderMap);
		}
		else
		{
#if DEBUG_INFINITESHADERCOMPILE
			UE_LOG(LogTemp, Display, TEXT("Kicking off shader compilation for %s, GameThreadShaderMap 0x%08X%08X"), *GetFriendlyName(), (int)((int64)(NewShaderMap.GetReference()) >> 32), (int)((int64)(NewShaderMap.GetReference())));
#endif
			SetGameThreadShaderMap(NewShaderMap->AcquireFinalizedClone());
		}
	}

	INC_FLOAT_STAT_BY(STAT_ShaderCompiling_MaterialCompiling,(float)MaterialCompileTime);
	INC_FLOAT_STAT_BY(STAT_ShaderCompiling_MaterialShaders,(float)MaterialCompileTime);

	return bSuccess;
#else
	UE_LOG(LogMaterial, Fatal,TEXT("Not supported."));
	return false;
#endif
}

/**
 * Should the shader for this material with the given platform, shader type and vertex 
 * factory type combination be compiled
 *
 * @param Platform		The platform currently being compiled for
 * @param ShaderType	Which shader is being compiled
 * @param VertexFactory	Which vertex factory is being compiled (can be NULL)
 *
 * @return true if the shader should be compiled
 */
bool FMaterial::ShouldCache(EShaderPlatform Platform, const FShaderType* ShaderType, const FVertexFactoryType* VertexFactoryType) const
{
	return true;
}

bool FMaterial::ShouldCachePipeline(EShaderPlatform Platform, const FShaderPipelineType* PipelineType, const FVertexFactoryType* VertexFactoryType) const
{
	for (const FShaderType* ShaderType : PipelineType->GetStages())
	{
		if (!ShouldCache(Platform, ShaderType, VertexFactoryType))
		{
			return false;
		}
	}

	// Only include the pipeline if all shaders should be cached
	return true;
}

//
// FColoredMaterialRenderProxy implementation.
//

const FMaterial* FColoredMaterialRenderProxy::GetMaterialNoFallback(ERHIFeatureLevel::Type InFeatureLevel) const
{
	return Parent->GetMaterialNoFallback(InFeatureLevel);
}

const FMaterialRenderProxy* FColoredMaterialRenderProxy::GetFallback(ERHIFeatureLevel::Type InFeatureLevel) const
{
	return Parent->GetFallback(InFeatureLevel);
}

/**
 * Finds the shader matching the template type and the passed in vertex factory, asserts if not found.
 */
TShaderRef<FShader> FMaterial::GetShader(FMeshMaterialShaderType* ShaderType, FVertexFactoryType* VertexFactoryType, int32 PermutationId, bool bFatalIfMissing) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FMaterial::GetShader);
#if WITH_EDITOR && DO_CHECK
	// Attempt to get some more info for a rare crash (UE-35937)
	FMaterialShaderMap* GameThreadShaderMapPtr = GameThreadShaderMap;
	checkf( RenderingThreadShaderMap, TEXT("RenderingThreadShaderMap was NULL (GameThreadShaderMap is %p). This may relate to bug UE-35937"), GameThreadShaderMapPtr );
#endif
	const FMeshMaterialShaderMap* MeshShaderMap = RenderingThreadShaderMap->GetMeshShaderMap(VertexFactoryType);
	FShader* Shader = MeshShaderMap ? MeshShaderMap->GetShader(ShaderType, PermutationId) : nullptr;
	if (!Shader)
	{
		if (bFatalIfMissing)
		{
			auto noinline_lambda = [&](...) FORCENOINLINE
			{
				// we don't care about thread safety because we are about to crash 
				const auto CachedGameThreadShaderMap = GameThreadShaderMap;
				const auto CachedGameMeshShaderMap = CachedGameThreadShaderMap ? CachedGameThreadShaderMap->GetMeshShaderMap(VertexFactoryType) : nullptr;
				bool bShaderWasFoundInGameShaderMap = CachedGameMeshShaderMap && CachedGameMeshShaderMap->GetShader(ShaderType, PermutationId) != nullptr;

				// Get the ShouldCache results that determine whether the shader should be compiled
				auto ShaderPlatform = GShaderPlatformForFeatureLevel[GetFeatureLevel()];
				auto ShaderPermutation = GetCurrentShaderPermutationFlags();
				bool bMaterialShouldCache = ShouldCache(ShaderPlatform, ShaderType, VertexFactoryType);
				bool bVFShouldCache = FMeshMaterialShaderType::ShouldCompileVertexFactoryPermutation(VertexFactoryType, ShaderPlatform, this, ShaderPermutation);
				bool bShaderShouldCache = ShaderType->ShouldCompilePermutation(ShaderPlatform, this, VertexFactoryType, PermutationId, ShaderPermutation);
				FString MaterialUsage = GetMaterialUsageDescription();

				int BreakPoint = 0;

				// Assert with detailed information if the shader wasn't found for rendering.  
				// This is usually the result of an incorrect ShouldCache function.
				UE_LOG(LogMaterial, Error,
					TEXT("Couldn't find Shader (%s, %d) for Material Resource %s!\n")
					TEXT("		RenderMeshShaderMap %d, RenderThreadShaderMap %d\n")
					TEXT("		GameMeshShaderMap %d, GameThreadShaderMap %d, bShaderWasFoundInGameShaderMap %d\n")
					TEXT("		With VF=%s, Platform=%s\n")
					TEXT("		ShouldCache: Mat=%u, VF=%u, Shader=%u \n")
					TEXT("		MaterialUsageDesc: %s"),
					ShaderType->GetName(), PermutationId, *GetFriendlyName(),
					MeshShaderMap != nullptr, RenderingThreadShaderMap != nullptr,
					CachedGameMeshShaderMap != nullptr, CachedGameThreadShaderMap != nullptr, bShaderWasFoundInGameShaderMap,
					VertexFactoryType->GetName(), *LegacyShaderPlatformToShaderFormat(ShaderPlatform).ToString(),
					bMaterialShouldCache, bVFShouldCache, bShaderShouldCache,
					*MaterialUsage
				);

				if (MeshShaderMap)
				{
					TMap<FHashedName, TShaderRef<FShader>> List;
					MeshShaderMap->GetShaderList(*RenderingThreadShaderMap, List);

					for (const auto& ShaderPair : List)
					{
						FString TypeName = ShaderPair.Value.GetType()->GetName();
						UE_LOG(LogMaterial, Error, TEXT("ShaderType found in MaterialMap: %s"), *TypeName);
					}
				}

				UE_LOG(LogMaterial, Fatal, TEXT("Fatal Error Material not found"));
			};
			noinline_lambda();
		}

		return TShaderRef<FShader>();
	}

	return TShaderRef<FShader>(Shader, *RenderingThreadShaderMap);
}

bool FMaterial::TryGetShaders(const FMaterialShaderTypes& InTypes, const FVertexFactoryType* InVertexFactoryType, FMaterialShaders& OutShaders) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FMaterial::TryGetShaders);
	static const auto* CVarShaderPipelines = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.ShaderPipelines"));
	const bool bIsInGameThread = IsInGameThread();
	const FMaterialShaderMap* ShaderMap = bIsInGameThread ? GameThreadShaderMap : RenderingThreadShaderMap;
	const bool bShaderMapComplete = bIsInGameThread ? IsGameThreadShaderMapComplete() : IsRenderingThreadShaderMapComplete();
	const uint32 CompilingShaderMapId = bIsInGameThread ? GameThreadCompilingShaderMapId : RenderingThreadCompilingShaderMapId;

#if WITH_EDITOR && DO_CHECK
	// Attempt to get some more info for a rare crash (UE-35937)
	FMaterialShaderMap* GameThreadShaderMapPtr = GameThreadShaderMap;
	checkf(ShaderMap, TEXT("RenderingThreadShaderMap was NULL (GameThreadShaderMap is %p). This may relate to bug UE-35937"), GameThreadShaderMapPtr);
#endif
	
	OutShaders.ShaderMap = ShaderMap;
	const EShaderPlatform ShaderPlatform = ShaderMap->GetShaderPlatform();
	const EShaderPermutationFlags PermutationFlags = GetCurrentShaderPermutationFlags();
	const FShaderMapContent* ShaderMapContent = InVertexFactoryType
		? static_cast<const FShaderMapContent*>(ShaderMap->GetMeshShaderMap(InVertexFactoryType))
		: static_cast<const FShaderMapContent*>(ShaderMap->GetContent());

	TArray<FShaderCommonCompileJobPtr> CompileJobs;
	bool bMissingShader = false;

	if (InTypes.PipelineType &&
		RHISupportsShaderPipelines(ShaderPlatform) &&
		CVarShaderPipelines && CVarShaderPipelines->GetValueOnAnyThread(bIsInGameThread) != 0)
	{
		FShaderPipeline* Pipeline = ShaderMapContent ? ShaderMapContent->GetShaderPipeline(InTypes.PipelineType) : nullptr;
		if (Pipeline)
		{
			OutShaders.Pipeline = Pipeline;
			for (int32 FrequencyIndex = 0; FrequencyIndex < SF_NumGraphicsFrequencies; ++FrequencyIndex)
			{
				const FShaderType* ShaderType = InTypes.ShaderType[FrequencyIndex];
				FShader* Shader = Pipeline->GetShader((EShaderFrequency)FrequencyIndex);
				if (Shader)
				{
					check(Shader->GetType(ShaderMap->GetPointerTable()) == ShaderType);
					OutShaders.Shaders[FrequencyIndex] = Shader;
				}
				else
				{
					check(!ShaderType);
				}
			}
		}
		else
		{
			if (InTypes.PipelineType->ShouldOptimizeUnusedOutputs(ShaderPlatform))
			{
				bMissingShader = true;

#if WITH_ODSC
				if (FPlatformProperties::RequiresCookedData())
				{
					const FString MaterialName = GetFullPath();
					const FString VFTypeName(InVertexFactoryType ? InVertexFactoryType->GetName() : TEXT(""));
					const FString PipelineName(InTypes.PipelineType->GetName());
					TArray<FString> ShaderStageNamesToCompile;
					for (auto* ShaderType : InTypes.PipelineType->GetStages())
					{
						ShaderStageNamesToCompile.Add(ShaderType->GetName());
					}

					GODSCManager->AddThreadedShaderPipelineRequest(ShaderPlatform, MaterialName, VFTypeName, PipelineName, ShaderStageNamesToCompile);
				}
				else 
#endif
				if (CompilingShaderMapId != 0u)
				{
					check(!bShaderMapComplete);
					if (InVertexFactoryType)
					{
						FMeshMaterialShaderType::BeginCompileShaderPipeline(EShaderCompileJobPriority::ForceLocal, CompilingShaderMapId, kUniqueShaderPermutationId, ShaderPlatform, PermutationFlags, this, RenderingThreadPendingCompilerEnvironment, InVertexFactoryType, InTypes.PipelineType, CompileJobs, FString(), FString());
					}
					else
					{
						FMaterialShaderType::BeginCompileShaderPipeline(EShaderCompileJobPriority::ForceLocal, CompilingShaderMapId, ShaderPlatform, PermutationFlags, this, RenderingThreadPendingCompilerEnvironment, InTypes.PipelineType, CompileJobs, FString(), FString());
					}
				}
			}
		}
	}
	else
	{
		for (int32 FrequencyIndex = 0; FrequencyIndex < SF_NumGraphicsFrequencies; ++FrequencyIndex)
		{
			const FShaderType* ShaderType = InTypes.ShaderType[FrequencyIndex];
			if (ShaderType)
			{
				const int32 PermutationId = InTypes.PermutationId[FrequencyIndex];
				FShader* Shader = ShaderMapContent ? ShaderMapContent->GetShader(ShaderType, PermutationId) : nullptr;
				if (Shader)
				{
					OutShaders.Shaders[FrequencyIndex] = Shader;
				}
				else
				{
#if WITH_ODSC
					if (FPlatformProperties::RequiresCookedData())
					{
						bMissingShader = true;

						const FString MaterialName = GetFullPath();
						const FString VFTypeName(InVertexFactoryType ? InVertexFactoryType->GetName() : TEXT(""));
						const FString PipelineName;
						TArray<FString> ShaderStageNamesToCompile;
						ShaderStageNamesToCompile.Add(ShaderType->GetName());

						GODSCManager->AddThreadedShaderPipelineRequest(ShaderPlatform, MaterialName, VFTypeName, PipelineName, ShaderStageNamesToCompile);
					}
					else
#endif
					{
						check(!bShaderMapComplete);
						bMissingShader = true;
						if (CompilingShaderMapId != 0u)
						{
							if (InVertexFactoryType)
							{
								ShaderType->AsMeshMaterialShaderType()->BeginCompileShader(EShaderCompileJobPriority::ForceLocal, CompilingShaderMapId, PermutationId, ShaderPlatform, PermutationFlags, this, RenderingThreadPendingCompilerEnvironment, InVertexFactoryType, CompileJobs, FString(), FString());
							}
							else
							{
								ShaderType->AsMaterialShaderType()->BeginCompileShader(EShaderCompileJobPriority::ForceLocal, CompilingShaderMapId, PermutationId, this, RenderingThreadPendingCompilerEnvironment, ShaderPlatform, PermutationFlags, CompileJobs, FString(), FString());
							}
						}
					}
				}
			}
		}
	}

	if (CompileJobs.Num() > 0)
	{
		GShaderCompilingManager->SubmitJobs(CompileJobs, GetBaseMaterialPathName(), ShaderMap->GetDebugDescription());
	}

	return !bMissingShader;
}

bool FMaterial::HasShaders(const FMaterialShaderTypes& InTypes, const FVertexFactoryType* InVertexFactoryType) const
{
	FMaterialShaders UnusedShaders;
	return TryGetShaders(InTypes, InVertexFactoryType, UnusedShaders);
}

bool FMaterial::ShouldCacheShaders(const FMaterialShaderTypes& InTypes, const FVertexFactoryType* InVertexFactoryType) const
{
	const EShaderPlatform ShaderPlatform = RenderingThreadShaderMap->GetShaderPlatform();
	for (int32 FrequencyIndex = 0; FrequencyIndex < SF_NumGraphicsFrequencies; ++FrequencyIndex)
	{
		const FShaderType* ShaderType = InTypes.ShaderType[FrequencyIndex];
		if (ShaderType && !ShouldCache(ShaderPlatform, ShaderType, InVertexFactoryType))
		{
			return false;
		}
	}
	return true;
}

FShaderPipelineRef FMaterial::GetShaderPipeline(class FShaderPipelineType* ShaderPipelineType, FVertexFactoryType* VertexFactoryType, bool bFatalIfNotFound) const
{
	const FMeshMaterialShaderMap* MeshShaderMap = RenderingThreadShaderMap->GetMeshShaderMap(VertexFactoryType);
	FShaderPipeline* ShaderPipeline = MeshShaderMap ? MeshShaderMap->GetShaderPipeline(ShaderPipelineType) : nullptr;
	if (!ShaderPipeline)
	{
		if (bFatalIfNotFound)
		{
			auto noinline_lambda = [&](...) FORCENOINLINE
			{
				// Get the ShouldCache results that determine whether the shader should be compiled
				auto ShaderPlatform = GShaderPlatformForFeatureLevel[GetFeatureLevel()];
				auto ShaderPermutation = GetCurrentShaderPermutationFlags();
				FString MaterialUsage = GetMaterialUsageDescription();

				UE_LOG(LogMaterial, Error,
					TEXT("Couldn't find ShaderPipeline %s for Material Resource %s!"), ShaderPipelineType->GetName(), *GetFriendlyName());

				for (auto* ShaderType : ShaderPipelineType->GetStages())
				{
					FShader* Shader = MeshShaderMap ? MeshShaderMap->GetShader((FShaderType*)ShaderType) : RenderingThreadShaderMap->GetShader((FShaderType*)ShaderType).GetShader();
					if (!Shader)
					{
						UE_LOG(LogMaterial, Error, TEXT("Missing %s shader %s!"), GetShaderFrequencyString(ShaderType->GetFrequency(), false), ShaderType->GetName());
					}
					else if (ShaderType->GetMeshMaterialShaderType())
					{
						bool bMaterialShouldCache = ShouldCache(ShaderPlatform, ShaderType->GetMeshMaterialShaderType(), VertexFactoryType);
						bool bVFShouldCache = FMeshMaterialShaderType::ShouldCompileVertexFactoryPermutation(VertexFactoryType, ShaderPlatform, this, ShaderPermutation);
						bool bShaderShouldCache = ShaderType->GetMeshMaterialShaderType()->ShouldCompilePermutation(ShaderPlatform, this, VertexFactoryType, kUniqueShaderPermutationId, ShaderPermutation);

						UE_LOG(LogMaterial, Error, TEXT("%s %s ShouldCache: Mat=%u, VF=%u, Shader=%u"),
							GetShaderFrequencyString(ShaderType->GetFrequency(), false), ShaderType->GetName(), bMaterialShouldCache, bVFShouldCache, bShaderShouldCache);
					}
					else if (ShaderType->GetMaterialShaderType())
					{
						bool bMaterialShouldCache = ShouldCache(ShaderPlatform, ShaderType->GetMaterialShaderType(), VertexFactoryType);
						bool bShaderShouldCache = ShaderType->GetMaterialShaderType()->ShouldCompilePermutation(ShaderPlatform, this, kUniqueShaderPermutationId, ShaderPermutation);

						UE_LOG(LogMaterial, Error, TEXT("%s %s ShouldCache: Mat=%u, NO VF, Shader=%u"),
							GetShaderFrequencyString(ShaderType->GetFrequency(), false), ShaderType->GetName(), bMaterialShouldCache, bShaderShouldCache);
					}
				}

				int BreakPoint = 0;

				// Assert with detailed information if the shader wasn't found for rendering.  
				// This is usually the result of an incorrect ShouldCache function.
				UE_LOG(LogMaterial, Fatal,
					TEXT("		With VF=%s, Platform=%s\n")
					TEXT("		MaterialUsageDesc: %s"),
					VertexFactoryType->GetName(), *LegacyShaderPlatformToShaderFormat(ShaderPlatform).ToString(),
					*MaterialUsage
					);
			};
			noinline_lambda();
		}

		return FShaderPipelineRef();
	}

	return FShaderPipelineRef(ShaderPipeline, *RenderingThreadShaderMap);
}

#if WITH_EDITOR
TSet<FMaterial*> FMaterial::EditorLoadedMaterialResources;
#endif // WITH_EDITOR

/*-----------------------------------------------------------------------------
	FMaterialRenderContext
-----------------------------------------------------------------------------*/

/** 
 * Constructor
 */
FMaterialRenderContext::FMaterialRenderContext(
	const FMaterialRenderProxy* InMaterialRenderProxy,
	const FMaterial& InMaterial,
	const FSceneView* InView)
		: MaterialRenderProxy(InMaterialRenderProxy)
		, Material(InMaterial)
{
	bShowSelection = GIsEditor && InView && InView->Family->EngineShowFlags.Selection;
}

/*-----------------------------------------------------------------------------
	FMaterialVirtualTextureStack
-----------------------------------------------------------------------------*/

FMaterialVirtualTextureStack::FMaterialVirtualTextureStack()
	: NumLayers(0u)
	, PreallocatedStackTextureIndex(INDEX_NONE)
{
	for (uint32 i = 0u; i < VIRTUALTEXTURE_SPACE_MAXLAYERS; ++i)
	{
		LayerUniformExpressionIndices[i] = INDEX_NONE;
	}
}

FMaterialVirtualTextureStack::FMaterialVirtualTextureStack(int32 InPreallocatedStackTextureIndex)
	: NumLayers(0u)
	, PreallocatedStackTextureIndex(InPreallocatedStackTextureIndex)
{
	for (uint32 i = 0u; i < VIRTUALTEXTURE_SPACE_MAXLAYERS; ++i)
	{
		LayerUniformExpressionIndices[i] = INDEX_NONE;
	}
}

uint32 FMaterialVirtualTextureStack::AddLayer()
{
	const uint32 LayerIndex = NumLayers++;
	return LayerIndex;
}

uint32 FMaterialVirtualTextureStack::SetLayer(int32 LayerIndex, int32 UniformExpressionIndex)
{
	check(UniformExpressionIndex >= 0);
	check(LayerIndex >= 0 && LayerIndex < VIRTUALTEXTURE_SPACE_MAXLAYERS);
	LayerUniformExpressionIndices[LayerIndex] = UniformExpressionIndex;
	NumLayers = FMath::Max<uint32>(LayerIndex + 1, NumLayers);
	return LayerIndex;
}

int32 FMaterialVirtualTextureStack::FindLayer(int32 UniformExpressionIndex) const
{
	for (uint32 LayerIndex = 0u; LayerIndex < NumLayers; ++LayerIndex)
	{
		if (LayerUniformExpressionIndices[LayerIndex] == UniformExpressionIndex)
		{
			return LayerIndex;
		}
	}
	return -1;
}

void FMaterialVirtualTextureStack::GetTextureValues(const FMaterialRenderContext& Context, const FUniformExpressionSet& UniformExpressionSet, UTexture2D const** OutValues) const
{
	FMemory::Memzero(OutValues, sizeof(FVirtualTexture2DResource*) * VIRTUALTEXTURE_SPACE_MAXLAYERS);
	
	for (uint32 LayerIndex = 0u; LayerIndex < NumLayers; ++LayerIndex)
	{
		const int32 ParameterIndex = LayerUniformExpressionIndices[LayerIndex];
		if (ParameterIndex != INDEX_NONE)
		{
			const UTexture* Texture = nullptr;
			UniformExpressionSet.GetTextureValue(EMaterialTextureParameterType::Virtual, ParameterIndex, Context, Context.Material, Texture);
			OutValues[LayerIndex] = Cast<UTexture2D>(Texture);
		}
	}
}

void FMaterialVirtualTextureStack::GetTextureValue(const FMaterialRenderContext& Context, const FUniformExpressionSet& UniformExpressionSet, const URuntimeVirtualTexture*& OutValue) const
{
	OutValue = nullptr;
	if (NumLayers > 0)
	{
		const int32 ParameterIndex = LayerUniformExpressionIndices[0];
		if (ParameterIndex != INDEX_NONE)
		{
			const URuntimeVirtualTexture* Texture = nullptr;
			UniformExpressionSet.GetTextureValue(ParameterIndex, Context, Context.Material, Texture);
			OutValue = Texture;
		}
	}
}

void FMaterialVirtualTextureStack::Serialize(FArchive& Ar)
{
	uint32 SerializedNumLayers = NumLayers;
	Ar << SerializedNumLayers;
	NumLayers = FMath::Min(SerializedNumLayers, uint32(VIRTUALTEXTURE_SPACE_MAXLAYERS));

	for (uint32 LayerIndex = 0u; LayerIndex < NumLayers; ++LayerIndex)
	{
		Ar << LayerUniformExpressionIndices[LayerIndex];
	}

	for (uint32 LayerIndex = NumLayers; LayerIndex < SerializedNumLayers; ++LayerIndex)
	{
		int32 DummyIndex = INDEX_NONE;
		Ar << DummyIndex;
	}

	Ar << PreallocatedStackTextureIndex;
}

/*-----------------------------------------------------------------------------
	FMaterialRenderProxy
-----------------------------------------------------------------------------*/

static void OnVirtualTextureDestroyedCB(const FVirtualTextureProducerHandle& InHandle, void* Baton)
{
	FMaterialRenderProxy* MaterialProxy = static_cast<FMaterialRenderProxy*>(Baton);

	MaterialProxy->InvalidateUniformExpressionCache(false);
	UMaterialInterface::IterateOverActiveFeatureLevels([&](ERHIFeatureLevel::Type InFeatureLevel)
	{
		MaterialProxy->UpdateUniformExpressionCacheIfNeeded(InFeatureLevel);
	});
}

IAllocatedVirtualTexture* FMaterialRenderProxy::GetPreallocatedVTStack(const FMaterialRenderContext& Context, const FUniformExpressionSet& UniformExpressionSet, const FMaterialVirtualTextureStack& VTStack) const
{
	check(VTStack.IsPreallocatedStack())

	URuntimeVirtualTexture const* Texture;
	VTStack.GetTextureValue(Context, UniformExpressionSet, Texture);

	if (Texture == nullptr)
	{
		return nullptr;
	}

	GetRendererModule().AddVirtualTextureProducerDestroyedCallback(Texture->GetProducerHandle(), &OnVirtualTextureDestroyedCB, const_cast<FMaterialRenderProxy*>(this));
	HasVirtualTextureCallbacks = true;

	return Texture->GetAllocatedVirtualTexture();
}

IAllocatedVirtualTexture* FMaterialRenderProxy::AllocateVTStack(const FMaterialRenderContext& Context, const FUniformExpressionSet& UniformExpressionSet, const FMaterialVirtualTextureStack& VTStack) const
{
	check(!VTStack.IsPreallocatedStack());
	const uint32 NumLayers = VTStack.GetNumLayers();
	if (NumLayers == 0u)
	{
		return nullptr;
	}

	const UTexture2D* LayerTextures[VIRTUALTEXTURE_SPACE_MAXLAYERS] = { nullptr };
	VTStack.GetTextureValues(Context, UniformExpressionSet, LayerTextures);

	FAllocatedVTDescription VTDesc;
	VTDesc.Dimensions = 2;
	VTDesc.NumTextureLayers = NumLayers;
	bool bFoundValidLayer = false;
	for (uint32 LayerIndex = 0u; LayerIndex < NumLayers; ++LayerIndex)
	{
		const UTexture2D* Texture = LayerTextures[LayerIndex];
		const FVirtualTexture2DResource* VirtualTextureResourceForLayer = (Texture && Texture->IsCurrentlyVirtualTextured()) ? (FVirtualTexture2DResource*)Texture->Resource : nullptr;
		if (VirtualTextureResourceForLayer != nullptr)
		{
			// All tile sizes need to match
			check(!bFoundValidLayer || VTDesc.TileSize == VirtualTextureResourceForLayer->GetTileSize());
			check(!bFoundValidLayer || VTDesc.TileBorderSize == VirtualTextureResourceForLayer->GetBorderSize());

			VTDesc.TileSize = VirtualTextureResourceForLayer->GetTileSize();
			VTDesc.TileBorderSize = VirtualTextureResourceForLayer->GetBorderSize();
			const FVirtualTextureProducerHandle& ProducerHandle = VirtualTextureResourceForLayer->GetProducerHandle();
			VTDesc.ProducerHandle[LayerIndex] = ProducerHandle;
			VTDesc.ProducerLayerIndex[LayerIndex] = 0u;
			GetRendererModule().AddVirtualTextureProducerDestroyedCallback(ProducerHandle, &OnVirtualTextureDestroyedCB, const_cast<FMaterialRenderProxy*>(this));
			bFoundValidLayer = true;
		}
	}

	if (bFoundValidLayer)
	{
		HasVirtualTextureCallbacks = true;
		return GetRendererModule().AllocateVirtualTexture(VTDesc);
	}
	return nullptr;
}

FUniformExpressionCache::~FUniformExpressionCache()
{
	ResetAllocatedVTs();
	UniformBuffer.SafeRelease();
}

void FUniformExpressionCache::ResetAllocatedVTs()
{
	for (int32 i=0; i< OwnedAllocatedVTs.Num(); ++i)
	{
		GetRendererModule().DestroyVirtualTexture(OwnedAllocatedVTs[i]);
	}
	AllocatedVTs.Reset();
	OwnedAllocatedVTs.Reset();
}

void FMaterialRenderProxy::EvaluateUniformExpressions(FUniformExpressionCache& OutUniformExpressionCache, const FMaterialRenderContext& Context, FRHICommandList* CommandListIfLocalMode) const
{
	check(IsInParallelRenderingThread());

	SCOPE_CYCLE_COUNTER(STAT_CacheUniformExpressions);
	
	// Retrieve the material's uniform expression set.
	FMaterialShaderMap* ShaderMap = Context.Material.GetRenderingThreadShaderMap();
	const FUniformExpressionSet& UniformExpressionSet = ShaderMap->GetUniformExpressionSet();

	OutUniformExpressionCache.CachedUniformExpressionShaderMap = ShaderMap;
	OutUniformExpressionCache.ResetAllocatedVTs();
	OutUniformExpressionCache.AllocatedVTs.Empty(UniformExpressionSet.VTStacks.Num());
	OutUniformExpressionCache.OwnedAllocatedVTs.Empty(UniformExpressionSet.VTStacks.Num());
	
	if (HasVirtualTextureCallbacks)
	{
		GetRendererModule().RemoveAllVirtualTextureProducerDestroyedCallbacks(this);
		HasVirtualTextureCallbacks = false;
	}

	for (int32 i = 0; i < UniformExpressionSet.VTStacks.Num(); ++i)
	{
		const FMaterialVirtualTextureStack& VTStack = UniformExpressionSet.VTStacks[i];
		IAllocatedVirtualTexture* AllocatedVT = nullptr;
		if (VTStack.IsPreallocatedStack())
		{
			AllocatedVT = GetPreallocatedVTStack(Context, UniformExpressionSet, VTStack);
		}
		else
		{
			AllocatedVT = AllocateVTStack(Context, UniformExpressionSet, VTStack);
			if (AllocatedVT != nullptr)
			{
				OutUniformExpressionCache.OwnedAllocatedVTs.Add(AllocatedVT);
			}
		}
		OutUniformExpressionCache.AllocatedVTs.Add(AllocatedVT);
	}

	const FRHIUniformBufferLayout& UniformBufferLayout = UniformExpressionSet.GetUniformBufferLayout();
	FMemMark Mark(FMemStack::Get());
	uint8* TempBuffer = FMemStack::Get().PushBytes(UniformBufferLayout.ConstantBufferSize, SHADER_PARAMETER_STRUCT_ALIGNMENT);

	check(TempBuffer != nullptr);
	UniformExpressionSet.FillUniformBuffer(Context, OutUniformExpressionCache, TempBuffer, UniformBufferLayout.ConstantBufferSize);

	if (CommandListIfLocalMode)
	{
		OutUniformExpressionCache.LocalUniformBuffer = CommandListIfLocalMode->BuildLocalUniformBuffer(TempBuffer, UniformBufferLayout.ConstantBufferSize, UniformBufferLayout);
		check(OutUniformExpressionCache.LocalUniformBuffer.IsValid());
	}
	else
	{
		if (IsValidRef(OutUniformExpressionCache.UniformBuffer) && !OutUniformExpressionCache.UniformBuffer->IsValid())
		{
			UE_LOG(LogMaterial, Fatal, TEXT("The Uniformbuffer needs to be valid if it has been set"));
		}

		if (IsValidRef(OutUniformExpressionCache.UniformBuffer))
		{
			check(OutUniformExpressionCache.UniformBuffer->GetLayout() == UniformBufferLayout);
			RHIUpdateUniformBuffer(OutUniformExpressionCache.UniformBuffer, TempBuffer);
		}
		else
		{
			OutUniformExpressionCache.UniformBuffer = RHICreateUniformBuffer(TempBuffer, UniformBufferLayout, UniformBuffer_MultiFrame);
		}
	}

	OutUniformExpressionCache.ParameterCollections = UniformExpressionSet.ParameterCollections;

	++UniformExpressionCacheSerialNumber;
	OutUniformExpressionCache.bUpToDate = Context.Material.IsRenderingThreadShaderMapComplete();
}

void FMaterialRenderProxy::CacheUniformExpressions(bool bRecreateUniformBuffer)
{
	// Register the render proxy's as a render resource so it can receive notifications to free the uniform buffer.
	InitResource();

	bool bUsingNewLoader = EVENT_DRIVEN_ASYNC_LOAD_ACTIVE_AT_RUNTIME && GEventDrivenLoaderEnabled;

	check((bUsingNewLoader && GIsInitialLoad) || // The EDL at boot time maybe not load the default materials first; we need to intialize materials before the default materials are done
		UMaterial::GetDefaultMaterial(MD_Surface));


	if (IsMarkedForGarbageCollection())
	{
		UE_LOG(LogMaterial, Fatal, TEXT("Cannot queue the Expression Cache when it is about to be deleted"));
	}
	DeferredUniformExpressionCacheRequests.Add(this);

	InvalidateUniformExpressionCache(bRecreateUniformBuffer);

	if (!GDeferUniformExpressionCaching)
	{
		FMaterialRenderProxy::UpdateDeferredCachedUniformExpressions();
	}
}

void FMaterialRenderProxy::CacheUniformExpressions_GameThread(bool bRecreateUniformBuffer)
{
	if (FApp::CanEverRender())
	{
		UE_LOG(LogMaterial, Verbose, TEXT("Caching uniform expressions for material: %s"), *GetFriendlyName());

		FMaterialRenderProxy* RenderProxy = this;
		ENQUEUE_RENDER_COMMAND(FCacheUniformExpressionsCommand)(
			[RenderProxy, bRecreateUniformBuffer](FRHICommandListImmediate& RHICmdList)
			{
				RenderProxy->CacheUniformExpressions(bRecreateUniformBuffer);
			});
	}
}

void FMaterialRenderProxy::InvalidateUniformExpressionCache(bool bRecreateUniformBuffer)
{
	check(IsInRenderingThread());

#if WITH_EDITOR
	FStaticLightingSystemInterface::OnMaterialInvalidated.Broadcast(this);
#endif

	if (HasVirtualTextureCallbacks)
	{
		GetRendererModule().RemoveAllVirtualTextureProducerDestroyedCallbacks(this);
		HasVirtualTextureCallbacks = false;
	}

	++UniformExpressionCacheSerialNumber;
	for (int32 i = 0; i < ERHIFeatureLevel::Num; ++i)
	{
		UniformExpressionCache[i].bUpToDate = false;
		UniformExpressionCache[i].CachedUniformExpressionShaderMap = nullptr;
		UniformExpressionCache[i].ResetAllocatedVTs();

		if (bRecreateUniformBuffer)
		{
			// This is required if the FMaterial is being recompiled (the uniform buffer layout will change).
			// This should only be done if the calling code is using FMaterialUpdateContext to recreate the rendering state of primitives using this material, 
			// Since cached mesh commands also cache uniform buffer pointers.
			UniformExpressionCache[i].UniformBuffer = nullptr;
		}
	}
}

void FMaterialRenderProxy::UpdateUniformExpressionCacheIfNeeded(ERHIFeatureLevel::Type InFeatureLevel) const
{
	if (!UniformExpressionCache[InFeatureLevel].bUpToDate)
	{
		// Don't cache uniform expressions if an entirely different FMaterialRenderProxy is going to be used for rendering
		const FMaterial* Material = GetMaterialNoFallback(InFeatureLevel);
		if (Material)
		{
			FMaterialRenderContext MaterialRenderContext(this, *Material, nullptr);
			MaterialRenderContext.bShowSelection = GIsEditor;
			EvaluateUniformExpressions(UniformExpressionCache[InFeatureLevel], MaterialRenderContext);
		}
	}
}

FMaterialRenderProxy::FMaterialRenderProxy()
	: SubsurfaceProfileRT(0)
	, MarkedForGarbageCollection(0)
	, DeletedFlag(0)
	, HasVirtualTextureCallbacks(0)
{
}

FMaterialRenderProxy::~FMaterialRenderProxy()
{
	if(IsInitialized())
	{
		check(IsInRenderingThread());
		ReleaseResource();
	}

	if (HasVirtualTextureCallbacks)
	{
		check(IsInRenderingThread());
		GetRendererModule().RemoveAllVirtualTextureProducerDestroyedCallbacks(this);
		HasVirtualTextureCallbacks = false;
	}

	DeletedFlag = 1;
}

void FMaterialRenderProxy::InitDynamicRHI()
{
	// MaterialRenderProxyMap is only used by shader compiling
	if (!FPlatformProperties::RequiresCookedData())
	{
		FMaterialRenderProxy::MaterialRenderProxyMap.Add(this);
	}
}

void FMaterialRenderProxy::ReleaseDynamicRHI()
{
	if (!FPlatformProperties::RequiresCookedData())
	{
		FMaterialRenderProxy::MaterialRenderProxyMap.Remove(this);
	}

	DeferredUniformExpressionCacheRequests.Remove(this);

	InvalidateUniformExpressionCache(true);

	FExternalTextureRegistry::Get().RemoveMaterialRenderProxyReference(this);
}

void FMaterialRenderProxy::ReleaseResource()
{
	ReleaseResourceFlag = true;
	FRenderResource::ReleaseResource();
	if (HasVirtualTextureCallbacks)
	{
		GetRendererModule().RemoveAllVirtualTextureProducerDestroyedCallbacks(this);
		HasVirtualTextureCallbacks = false;
	}
}

void FMaterial::SubmitCompileJobs(EShaderCompileJobPriority Priority) const
{
	if (RenderingThreadCompilingShaderMapId != 0u && RenderingThreadShaderMap)
	{
		RenderingThreadShaderMap->SubmitCompileJobs(RenderingThreadCompilingShaderMapId, this, RenderingThreadPendingCompilerEnvironment, Priority);
	}
}

const FMaterial& FMaterialRenderProxy::GetMaterialWithFallback(ERHIFeatureLevel::Type InFeatureLevel, const FMaterialRenderProxy*& OutFallbackMaterialRenderProxy) const
{
	const FMaterial* BaseMaterial = GetMaterialNoFallback(InFeatureLevel);
	const FMaterial* Material = BaseMaterial;
	if (!Material || !Material->IsRenderingThreadShaderMapComplete())
	{
		const FMaterialRenderProxy* FallbackMaterialProxy = this;
		do
		{
			FallbackMaterialProxy = FallbackMaterialProxy->GetFallback(InFeatureLevel);
			check(FallbackMaterialProxy);
			Material = FallbackMaterialProxy->GetMaterialNoFallback(InFeatureLevel);
		}
		while (!Material || !Material->IsRenderingThreadShaderMapComplete());
		OutFallbackMaterialRenderProxy = FallbackMaterialProxy;

		if (BaseMaterial)
		{
			BaseMaterial->SubmitCompileJobs(EShaderCompileJobPriority::Normal);
		}
	}
	return *Material;
}

const FMaterial& FMaterialRenderProxy::GetIncompleteMaterialWithFallback(ERHIFeatureLevel::Type InFeatureLevel) const
{
	const FMaterial* Material = GetMaterialNoFallback(InFeatureLevel);
	if (!Material)
	{
		const FMaterialRenderProxy* FallbackMaterialProxy = this;
		do
		{
			FallbackMaterialProxy = FallbackMaterialProxy->GetFallback(InFeatureLevel);
			check(FallbackMaterialProxy);
			Material = FallbackMaterialProxy->GetMaterialNoFallback(InFeatureLevel);
		} while (!Material);
	}
	if (Material->GetTessellationMode() != MTM_NoTessellation)
	{
		// Incomplete tesselation shaders can result in a mismatch between rendering data and shaders used that cause fatal issues.
		// Because of this, it's better for us to return the fallback even if we're asked for an incomplete material.
		const FMaterialRenderProxy* Unused{};
		return GetMaterialWithFallback(InFeatureLevel, Unused);
	}
	return *Material;
}

void FMaterialRenderProxy::UpdateDeferredCachedUniformExpressions()
{
	LLM_SCOPE(ELLMTag::Materials);

	check(IsInRenderingThread());

	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(Material_UpdateDeferredCachedUniformExpressions);
	QUICK_SCOPE_CYCLE_COUNTER(STAT_UpdateDeferredCachedUniformExpressions);

	for (TSet<FMaterialRenderProxy*>::TConstIterator It(DeferredUniformExpressionCacheRequests); It; ++It)
	{
		FMaterialRenderProxy* MaterialProxy = *It;
		if (MaterialProxy->IsDeleted())
		{
			UE_LOG(LogMaterial, Fatal, TEXT("FMaterialRenderProxy deleted and GC mark was: %i"), MaterialProxy->IsMarkedForGarbageCollection());
		}

		UMaterialInterface::IterateOverActiveFeatureLevels([&](ERHIFeatureLevel::Type InFeatureLevel)
		{
			// Don't bother caching if we'll be falling back to a different FMaterialRenderProxy for rendering anyway
			const FMaterial* Material = MaterialProxy->GetMaterialNoFallback(InFeatureLevel);
			if (Material && Material->GetRenderingThreadShaderMap())
			{
				FMaterialRenderContext MaterialRenderContext(MaterialProxy, *Material, nullptr);
				MaterialRenderContext.bShowSelection = GIsEditor;
				MaterialProxy->EvaluateUniformExpressions(MaterialProxy->UniformExpressionCache[(int32)InFeatureLevel], MaterialRenderContext);
			}
		});
	}

	DeferredUniformExpressionCacheRequests.Reset();
}

TSet<FMaterialRenderProxy*> FMaterialRenderProxy::MaterialRenderProxyMap;
TSet<FMaterialRenderProxy*> FMaterialRenderProxy::DeferredUniformExpressionCacheRequests;

/*-----------------------------------------------------------------------------
	FColoredMaterialRenderProxy
-----------------------------------------------------------------------------*/

bool FColoredMaterialRenderProxy::GetVectorValue(const FHashedMaterialParameterInfo& ParameterInfo, FLinearColor* OutValue, const FMaterialRenderContext& Context) const
{
	if(ParameterInfo.Name == ColorParamName)
	{
		*OutValue = Color;
		return true;
	}
	else
	{
		return Parent->GetVectorValue(ParameterInfo, OutValue, Context);
	}
}

bool FColoredMaterialRenderProxy::GetScalarValue(const FHashedMaterialParameterInfo& ParameterInfo, float* OutValue, const FMaterialRenderContext& Context) const
{
	return Parent->GetScalarValue(ParameterInfo, OutValue, Context);
}

bool FColoredMaterialRenderProxy::GetTextureValue(const FHashedMaterialParameterInfo& ParameterInfo,const UTexture** OutValue, const FMaterialRenderContext& Context) const
{
	return Parent->GetTextureValue(ParameterInfo,OutValue,Context);
}

bool FColoredMaterialRenderProxy::GetTextureValue(const FHashedMaterialParameterInfo& ParameterInfo, const URuntimeVirtualTexture** OutValue, const FMaterialRenderContext& Context) const
{
	return Parent->GetTextureValue(ParameterInfo, OutValue, Context);
}

/*-----------------------------------------------------------------------------
	FColoredTexturedMaterialRenderProxy
-----------------------------------------------------------------------------*/

bool FColoredTexturedMaterialRenderProxy::GetTextureValue(const FHashedMaterialParameterInfo& ParameterInfo, const UTexture** OutValue, const FMaterialRenderContext& Context) const
{
	if (ParameterInfo.Name == TextureParamName)
	{
		*OutValue = Texture;
		return true;
	}
	else
	{
		return Parent->GetTextureValue(ParameterInfo, OutValue, Context);
	}
}

/*-----------------------------------------------------------------------------
	FOverrideSelectionColorMaterialRenderProxy
-----------------------------------------------------------------------------*/
const FMaterial* FOverrideSelectionColorMaterialRenderProxy::GetMaterialNoFallback(ERHIFeatureLevel::Type InFeatureLevel) const
{
	return Parent->GetMaterialNoFallback(InFeatureLevel);
}

const FMaterialRenderProxy* FOverrideSelectionColorMaterialRenderProxy::GetFallback(ERHIFeatureLevel::Type InFeatureLevel) const
{
	return Parent->GetFallback(InFeatureLevel);
}

bool FOverrideSelectionColorMaterialRenderProxy::GetVectorValue(const FHashedMaterialParameterInfo& ParameterInfo, FLinearColor* OutValue, const FMaterialRenderContext& Context) const
{
	if (ParameterInfo.Name == FName(NAME_SelectionColor))
	{
		*OutValue = SelectionColor;
		return true;
	}
	else
	{
		return Parent->GetVectorValue(ParameterInfo, OutValue, Context);
	}
}

bool FOverrideSelectionColorMaterialRenderProxy::GetScalarValue(const FHashedMaterialParameterInfo& ParameterInfo, float* OutValue, const FMaterialRenderContext& Context) const
{
	return Parent->GetScalarValue(ParameterInfo, OutValue, Context);
}

bool FOverrideSelectionColorMaterialRenderProxy::GetTextureValue(const FHashedMaterialParameterInfo& ParameterInfo, const UTexture** OutValue, const FMaterialRenderContext& Context) const
{
	return Parent->GetTextureValue(ParameterInfo, OutValue, Context);
}

bool FOverrideSelectionColorMaterialRenderProxy::GetTextureValue(const FHashedMaterialParameterInfo& ParameterInfo, const URuntimeVirtualTexture** OutValue, const FMaterialRenderContext& Context) const
{
	return Parent->GetTextureValue(ParameterInfo, OutValue, Context);
}

/*-----------------------------------------------------------------------------
	FLightingDensityMaterialRenderProxy
-----------------------------------------------------------------------------*/
static FName NAME_LightmapRes = FName(TEXT("LightmapRes"));

bool FLightingDensityMaterialRenderProxy::GetVectorValue(const FHashedMaterialParameterInfo& ParameterInfo, FLinearColor* OutValue, const FMaterialRenderContext& Context) const
{
	if (ParameterInfo.Name == NAME_LightmapRes)
	{
		*OutValue = FLinearColor(LightmapResolution.X, LightmapResolution.Y, 0.0f, 0.0f);
		return true;
	}
	return FColoredMaterialRenderProxy::GetVectorValue(ParameterInfo, OutValue, Context);
}

#if WITH_EDITOR
/** Returns the number of samplers used in this material, or -1 if the material does not have a valid shader map (compile error or still compiling). */
int32 FMaterialResource::GetSamplerUsage() const
{
	if (GetGameThreadShaderMap())
	{
		return GetGameThreadShaderMap()->GetMaxTextureSamplers();
	}

	return -1;
}
#endif // WITH_EDITOR

#if WITH_EDITOR
void FMaterialResource::GetUserInterpolatorUsage(uint32& NumUsedUVScalars, uint32& NumUsedCustomInterpolatorScalars) const
{
	NumUsedUVScalars = NumUsedCustomInterpolatorScalars = 0;

	if (const FMaterialShaderMap* ShaderMap = GetGameThreadShaderMap())
	{
		NumUsedUVScalars = ShaderMap->GetNumUsedUVScalars();
		NumUsedCustomInterpolatorScalars = ShaderMap->GetNumUsedCustomInterpolatorScalars();
	}
}

void FMaterialResource::GetEstimatedNumTextureSamples(uint32& VSSamples, uint32& PSSamples) const
{
	VSSamples = PSSamples = 0;
	if (const FMaterialShaderMap* ShaderMap = GetGameThreadShaderMap())
	{
		ShaderMap->GetEstimatedNumTextureSamples(VSSamples, PSSamples);
	}
}

uint32 FMaterialResource::GetEstimatedNumVirtualTextureLookups() const
{
	if (const FMaterialShaderMap* ShaderMap = GetGameThreadShaderMap())
	{
		return ShaderMap->GetEstimatedNumVirtualTextureLookups();
	}
	return 0;
}
#endif // WITH_EDITOR

uint32 FMaterialResource::GetNumVirtualTextureStacks() const
{
	if (const FMaterialShaderMap* ShaderMap = GetGameThreadShaderMap())
	{
		return ShaderMap->GetNumVirtualTextureStacks();
	}
	return 0;
}


FString FMaterialResource::GetMaterialUsageDescription() const
{
	check(Material);
	FString BaseDescription = FString::Printf(
		TEXT("LightingModel=%s, BlendMode=%s, "),
		*GetShadingModelFieldString(GetShadingModels()), *GetBlendModeString(GetBlendMode()));

	// this changed from ",SpecialEngine, TwoSided" to ",SpecialEngine=1, TwoSided=1, TSNormal=0, ..." to be more readable
	BaseDescription += FString::Printf(
		TEXT("SpecialEngine=%d, TwoSided=%d, TSNormal=%d, Masked=%d, Distorted=%d, WritesEveryPixel=%d, ModifiesMeshPosition=%d")
		TEXT(", Usage={"),
		(int32)IsSpecialEngineMaterial(), (int32)IsTwoSided(), (int32)IsTangentSpaceNormal(), (int32)IsMasked(), (int32)IsDistorted(), (int32)WritesEveryPixel(), (int32)MaterialMayModifyMeshPosition()
		);

	bool bFirst = true;
	for (int32 MaterialUsageIndex = 0; MaterialUsageIndex < MATUSAGE_MAX; MaterialUsageIndex++)
	{
		if (Material->GetUsageByFlag((EMaterialUsage)MaterialUsageIndex))
		{
			if (!bFirst)
			{
				BaseDescription += FString(TEXT(","));
			}
			BaseDescription += Material->GetUsageName((EMaterialUsage)MaterialUsageIndex);
			bFirst = false;
		}
	}
	BaseDescription += FString(TEXT("}"));

	return BaseDescription;
}

static void AddSortedShader(TArray<FShaderType*>& Shaders, FShaderType* Shader)
{
	const int32 SortedIndex = Algo::LowerBoundBy(Shaders, Shader->GetHashedName(), [](const FShaderType* InShaderType) { return InShaderType->GetHashedName(); });
	if (SortedIndex >= Shaders.Num() || Shaders[SortedIndex] != Shader)
	{
		Shaders.Insert(Shader, SortedIndex);
	}
}

static void AddSortedShaderPipeline(TArray<const FShaderPipelineType*>& Pipelines, const FShaderPipelineType* Pipeline)
{
	const int32 SortedIndex = Algo::LowerBoundBy(Pipelines, Pipeline->GetHashedName(), [](const FShaderPipelineType* InPiplelineType) { return InPiplelineType->GetHashedName(); });
	if (SortedIndex >= Pipelines.Num() || Pipelines[SortedIndex] != Pipeline)
	{
		Pipelines.Insert(Pipeline, SortedIndex);
	}
}

void FMaterial::GetDependentShaderAndVFTypes(EShaderPlatform Platform, const FPlatformTypeLayoutParameters& LayoutParams, TArray<FShaderType*>& OutShaderTypes, TArray<const FShaderPipelineType*>& OutShaderPipelineTypes, TArray<FVertexFactoryType*>& OutVFTypes) const
{
	const FMaterialShaderParameters MaterialParameters(this);
	const FMaterialShaderMapLayout& Layout = AcquireMaterialShaderMapLayout(Platform, GetShaderPermutationFlags(LayoutParams), MaterialParameters);

	for (const FShaderLayoutEntry& Shader : Layout.Shaders)
	{
		if (ShouldCache(Platform, Shader.ShaderType, nullptr))
		{
			AddSortedShader(OutShaderTypes, Shader.ShaderType);
		}
	}

	for (const FShaderPipelineType* Pipeline : Layout.ShaderPipelines)
	{
		if (ShouldCachePipeline(Platform, Pipeline, nullptr))
		{
			AddSortedShaderPipeline(OutShaderPipelineTypes, Pipeline);
			for (const FShaderType* Type : Pipeline->GetStages())
			{
				AddSortedShader(OutShaderTypes, (FShaderType*)Type);
			}
		}
	}

	for (const FMeshMaterialShaderMapLayout& MeshLayout : Layout.MeshShaderMaps)
	{
		bool bIncludeVertexFactory = false;
		for (const FShaderLayoutEntry& Shader : MeshLayout.Shaders)
		{
			if (ShouldCache(Platform, Shader.ShaderType, MeshLayout.VertexFactoryType))
			{
				bIncludeVertexFactory = true;
				AddSortedShader(OutShaderTypes, Shader.ShaderType);
			}
		}

		for (const FShaderPipelineType* Pipeline : MeshLayout.ShaderPipelines)
		{
			if (ShouldCachePipeline(Platform, Pipeline, MeshLayout.VertexFactoryType))
			{
				bIncludeVertexFactory = true;
				AddSortedShaderPipeline(OutShaderPipelineTypes, Pipeline);
				for (const FShaderType* Type : Pipeline->GetStages())
				{
					AddSortedShader(OutShaderTypes, (FShaderType*)Type);
				}
			}
		}

		if (bIncludeVertexFactory)
		{
			// Vertex factories are already sorted
			OutVFTypes.Add(MeshLayout.VertexFactoryType);
		}
	}
}

void FMaterial::GetReferencedTexturesHash(EShaderPlatform Platform, FSHAHash& OutHash) const
{
	FSHA1 HashState;

	const TArrayView<UObject* const> ReferencedTextures = GetReferencedTextures();
	// Hash the names of the uniform expression textures to capture changes in their order or values resulting from material compiler code changes
	for (int32 TextureIndex = 0; TextureIndex < ReferencedTextures.Num(); TextureIndex++)
	{
		FString TextureName;

		if (ReferencedTextures[TextureIndex])
		{
			TextureName = ReferencedTextures[TextureIndex]->GetName();
		}

		HashState.UpdateWithString(*TextureName, TextureName.Len());
	}

	UMaterialShaderQualitySettings* MaterialShaderQualitySettings = UMaterialShaderQualitySettings::Get();
	if(MaterialShaderQualitySettings->HasPlatformQualitySettings(Platform, QualityLevel))
	{
		MaterialShaderQualitySettings->GetShaderPlatformQualitySettings(Platform)->AppendToHashState(QualityLevel, HashState);
	}

	HashState.Final();
	HashState.GetHash(&OutHash.Hash[0]);
}

/**
 * Get user source code for the material, with a list of code snippets to highlight representing the code for each MaterialExpression
 * @param OutSource - generated source code
 * @param OutHighlightMap - source code highlight list
 * @return - true on Success
 */
bool FMaterial::GetMaterialExpressionSource( FString& OutSource )
{
#if WITH_EDITORONLY_DATA
	class FViewSourceMaterialTranslator : public FHLSLMaterialTranslator
	{
	public:
		FViewSourceMaterialTranslator(FMaterial* InMaterial,FMaterialCompilationOutput& InMaterialCompilationOutput,const FStaticParameterSet& StaticParameters,EShaderPlatform InPlatform,EMaterialQualityLevel::Type InQualityLevel,ERHIFeatureLevel::Type InFeatureLevel)
		:	FHLSLMaterialTranslator(InMaterial,InMaterialCompilationOutput,StaticParameters,InPlatform,InQualityLevel,InFeatureLevel)
		{}
	};

	FMaterialCompilationOutput TempOutput;
	FMaterialShaderMapId ShaderMapID;
	GetShaderMapId(GMaxRHIShaderPlatform, nullptr, ShaderMapID);
	FStaticParameterSet StaticParamSet;
	GetStaticParameterSet(GMaxRHIShaderPlatform, StaticParamSet);

	FViewSourceMaterialTranslator MaterialTranslator(this, TempOutput, StaticParamSet, GMaxRHIShaderPlatform, GetQualityLevel(), GetFeatureLevel());
	bool bSuccess = MaterialTranslator.Translate();

	if( bSuccess )
	{
		// Generate the HLSL
		OutSource = MaterialTranslator.GetMaterialShaderCode();
	}
	return bSuccess;
#else
	UE_LOG(LogMaterial, Fatal,TEXT("Not supported."));
	return false;
#endif
}

bool FMaterial::WritesEveryPixel(bool bShadowPass) const
{
	bool bLocalStencilDitheredLOD = FeatureLevel >= ERHIFeatureLevel::SM5 && bStencilDitheredLOD;
	return !IsMasked()
		// Render dithered material as masked if a stencil prepass is not used (UE-50064, UE-49537)
		&& !((bShadowPass || !bLocalStencilDitheredLOD) && IsDitheredLODTransition())
		&& !IsWireframe()
		&& !(bLocalStencilDitheredLOD && IsDitheredLODTransition() && IsUsedWithInstancedStaticMeshes())
		&& !IsStencilTestEnabled();
}

#if WITH_EDITOR
/** Recompiles any materials in the EditorLoadedMaterialResources list if they are not complete. */
void FMaterial::UpdateEditorLoadedMaterialResources(EShaderPlatform InShaderPlatform)
{
	for (TSet<FMaterial*>::TIterator It(EditorLoadedMaterialResources); It; ++It)
	{
		FMaterial* CurrentMaterial = *It;
		if (!CurrentMaterial->GetGameThreadShaderMap() || !CurrentMaterial->GetGameThreadShaderMap()->IsComplete(CurrentMaterial, true))
		{
			CurrentMaterial->CacheShaders(InShaderPlatform);
		}
	}
}

void FMaterial::BackupEditorLoadedMaterialShadersToMemory(TMap<FMaterialShaderMap*, TUniquePtr<TArray<uint8> > >& ShaderMapToSerializedShaderData)
{
	for (TSet<FMaterial*>::TIterator It(EditorLoadedMaterialResources); It; ++It)
	{
		FMaterial* CurrentMaterial = *It;
		FMaterialShaderMap* ShaderMap = CurrentMaterial->GetGameThreadShaderMap();

		if (ShaderMap && !ShaderMapToSerializedShaderData.Contains(ShaderMap))
		{
			TArray<uint8>* ShaderData = ShaderMap->BackupShadersToMemory();
			ShaderMapToSerializedShaderData.Emplace(ShaderMap, ShaderData);
		}
	}
}

void FMaterial::RestoreEditorLoadedMaterialShadersFromMemory(const TMap<FMaterialShaderMap*, TUniquePtr<TArray<uint8> > >& ShaderMapToSerializedShaderData)
{
	for (TSet<FMaterial*>::TIterator It(EditorLoadedMaterialResources); It; ++It)
	{
		FMaterial* CurrentMaterial = *It;
		FMaterialShaderMap* ShaderMap = CurrentMaterial->GetGameThreadShaderMap();

		if (ShaderMap)
		{
			const TUniquePtr<TArray<uint8> >* ShaderData = ShaderMapToSerializedShaderData.Find(ShaderMap);

			if (ShaderData)
			{
				ShaderMap->RestoreShadersFromMemory(**ShaderData);
			}
		}
	}
}
#endif // WITH_EDITOR

void FMaterial::DumpDebugInfo()
{
	if (GameThreadShaderMap)
	{
		FString FeatureLevelName;
		GetFeatureLevelName(FeatureLevel, FeatureLevelName);
		FString QualityLevelString;
		GetMaterialQualityLevelName(QualityLevel, QualityLevelString);

		UE_LOG(LogConsoleResponse, Display, TEXT("FMaterial:  FeatureLevel %s     Quality Level %s"), *FeatureLevelName, *QualityLevelString);

		GameThreadShaderMap->DumpDebugInfo();
	}
}

void FMaterial::SaveShaderStableKeys(EShaderPlatform TargetShaderPlatform, FStableShaderKeyAndValue& SaveKeyVal)
{
#if WITH_EDITOR
	if (GameThreadShaderMap)
	{
		FString FeatureLevelName;
		GetFeatureLevelName(FeatureLevel, FeatureLevelName);
		SaveKeyVal.FeatureLevel = FName(*FeatureLevelName);

		FString QualityLevelString;
		GetMaterialQualityLevelName(QualityLevel, QualityLevelString);
		SaveKeyVal.QualityLevel = FName(*QualityLevelString);

		GameThreadShaderMap->SaveShaderStableKeys(TargetShaderPlatform, SaveKeyVal);
	}
#endif
}


FMaterialUpdateContext::FMaterialUpdateContext(uint32 Options, EShaderPlatform InShaderPlatform)
{
	bool bReregisterComponents = (Options & EOptions::ReregisterComponents) != 0;
	bool bRecreateRenderStates = ((Options & EOptions::RecreateRenderStates) != 0) && FApp::CanEverRender();

	bSyncWithRenderingThread = (Options & EOptions::SyncWithRenderingThread) != 0;
	if (bReregisterComponents)
	{
		ComponentReregisterContext = MakeUnique<FGlobalComponentReregisterContext>();
	}
	else if (bRecreateRenderStates)
	{
		ComponentRecreateRenderStateContext = MakeUnique<FGlobalComponentRecreateRenderStateContext>();
	}
	if (bSyncWithRenderingThread)
	{
		FlushRenderingCommands();
	}
	ShaderPlatform = InShaderPlatform;
}

void FMaterialUpdateContext::AddMaterial(UMaterial* Material)
{
	UpdatedMaterials.Add(Material);
	UpdatedMaterialInterfaces.Add(Material);
}

void FMaterialUpdateContext::AddMaterialInstance(UMaterialInstance* Instance)
{
	UpdatedMaterials.Add(Instance->GetMaterial());
	UpdatedMaterialInterfaces.Add(Instance);
}

void FMaterialUpdateContext::AddMaterialInterface(UMaterialInterface* Interface)
{
	UpdatedMaterials.Add(Interface->GetMaterial());
	UpdatedMaterialInterfaces.Add(Interface);
}

FMaterialUpdateContext::~FMaterialUpdateContext()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FMaterialUpdateContext::~FMaterialUpdateContext);

	double StartTime = FPlatformTime::Seconds();
	bool bProcess = false;

	// if the shader platform that was processed is not the currently rendering shader platform, 
	// there's no reason to update all of the runtime components
	UMaterialInterface::IterateOverActiveFeatureLevels([&](ERHIFeatureLevel::Type InFeatureLevel)
	{
		if (ShaderPlatform == GShaderPlatformForFeatureLevel[InFeatureLevel])
		{
			bProcess = true;
		}
	});

	if (!bProcess)
	{
		return;
	}

	// Flush rendering commands even though we already did so in the constructor.
	// Anything may have happened since the constructor has run. The flush is
	// done once here to avoid calling it once per static permutation we update.
	if (bSyncWithRenderingThread)
	{
		FlushRenderingCommands();
	}

	TArray<const FMaterial*> MaterialResourcesToUpdate;
	TArray<UMaterialInstance*> InstancesToUpdate;

	bool bUpdateStaticDrawLists = !ComponentReregisterContext && !ComponentRecreateRenderStateContext && FApp::CanEverRender();

	// If static draw lists must be updated, gather material resources from all updated materials.
	if (bUpdateStaticDrawLists)
	{
		for (TSet<UMaterial*>::TConstIterator It(UpdatedMaterials); It; ++It)
		{
			UMaterial* Material = *It;
			MaterialResourcesToUpdate.Append(Material->MaterialResources);
		}
	}

	// Go through all loaded material instances and recompile their static permutation resources if needed
	// This is necessary since the parent UMaterial stores information about how it should be rendered, (eg bUsesDistortion)
	// but the child can have its own shader map which may not contain all the shaders that the parent's settings indicate that it should.
	for (TObjectIterator<UMaterialInstance> It; It; ++It)
	{
		UMaterialInstance* CurrentMaterialInstance = *It;
		UMaterial* BaseMaterial = CurrentMaterialInstance->GetMaterial();

		if (UpdatedMaterials.Contains(BaseMaterial))
		{
			// Check to see if this instance is dependent on any of the material interfaces we directly updated.
			for (auto InterfaceIt = UpdatedMaterialInterfaces.CreateConstIterator(); InterfaceIt; ++InterfaceIt)
			{
				if (CurrentMaterialInstance->IsDependent(*InterfaceIt))
				{
					InstancesToUpdate.Add(CurrentMaterialInstance);
					break;
				}
			}
		}
	}

	// Material instances that use this base material must have their uniform expressions recached 
	// However, some material instances that use this base material may also depend on another MI with static parameters
	// So we must traverse upwards and ensure all parent instances that need updating are recached first.
	int32 NumInstancesWithStaticPermutations = 0;

	TFunction<void(UMaterialInstance* MI)> UpdateInstance = [&](UMaterialInstance* MI)
	{
		if (MI->Parent && InstancesToUpdate.Contains(MI->Parent))
		{
			if (UMaterialInstance* ParentInst = Cast<UMaterialInstance>(MI->Parent))
			{
				UpdateInstance(ParentInst);
			}
		}

#if WITH_EDITOR
		MI->UpdateCachedLayerParameters();
#endif
		MI->RecacheUniformExpressions(true);
		MI->InitStaticPermutation(EMaterialShaderPrecompileMode::None);//bHasStaticPermutation can change.
		if (MI->bHasStaticPermutationResource)
		{
			NumInstancesWithStaticPermutations++;
			// Collect FMaterial's that have been recompiled
			if (bUpdateStaticDrawLists)
			{
				MaterialResourcesToUpdate.Append(MI->StaticPermutationMaterialResources);
			}
		}
		InstancesToUpdate.Remove(MI);
	};

	while (InstancesToUpdate.Num() > 0)
	{
		UpdateInstance(InstancesToUpdate.Last());
	}

	if (bUpdateStaticDrawLists)
	{
		// Update static draw lists affected by any FMaterials that were recompiled
		// This is only needed if we aren't reregistering components which is not always
		// safe, e.g. while a component is being registered.
		GetRendererModule().UpdateStaticDrawListsForMaterials(MaterialResourcesToUpdate);
	}
	else if (ComponentReregisterContext)
	{
		ComponentReregisterContext.Reset();
	}
	else if (ComponentRecreateRenderStateContext)
	{
		ComponentRecreateRenderStateContext.Reset();
	}

	double EndTime = FPlatformTime::Seconds();

	if (UpdatedMaterials.Num() > 0)
	{
		UE_LOG(LogMaterial, Verbose,
			   TEXT("%.2f seconds spent updating %d materials, %d interfaces, %d instances, %d with static permutations."),
			   (float)(EndTime - StartTime),
			   UpdatedMaterials.Num(),
			   UpdatedMaterialInterfaces.Num(),
			   InstancesToUpdate.Num(),
			   NumInstancesWithStaticPermutations
			);
	}
}

bool UMaterialInterface::IsPropertyActive(EMaterialProperty InProperty)const
{
	//TODO: Disable properties in instances based on the currently set overrides and other material settings?
	//For now just allow all properties in instances. 
	//This had to be refactored into the instance as some override properties alter the properties that are active.
	return false;
}

#if WITH_EDITOR
int32 UMaterialInterface::CompilePropertyEx( class FMaterialCompiler* Compiler, const FGuid& AttributeID )
{
	return INDEX_NONE;
}

int32 UMaterialInterface::CompileProperty(FMaterialCompiler* Compiler, EMaterialProperty Property, uint32 ForceCastFlags)
{
	int32 Result = INDEX_NONE;

	if (IsPropertyActive(Property))
	{
		Result = CompilePropertyEx(Compiler, FMaterialAttributeDefinitionMap::GetID(Property));
	}
	else
	{
		Result = FMaterialAttributeDefinitionMap::CompileDefaultExpression(Compiler, Property);
	}

	if (ForceCastFlags & MFCF_ForceCast)
	{
		Result = Compiler->ForceCast(Result, FMaterialAttributeDefinitionMap::GetValueType(Property), ForceCastFlags);
	}

	return Result;
}
#endif // WITH_EDITOR

void UMaterialInterface::AnalyzeMaterialProperty(EMaterialProperty InProperty, int32& OutNumTextureCoordinates, bool& bOutRequiresVertexData)
{
#if WITH_EDITORONLY_DATA
	// FHLSLMaterialTranslator collects all required information during translation, but these data are protected. Needs to
	// derive own class from it to get access to these data.
	class FMaterialAnalyzer : public FHLSLMaterialTranslator
	{
	public:
		FMaterialAnalyzer(FMaterial* InMaterial, FMaterialCompilationOutput& InMaterialCompilationOutput, const FStaticParameterSet& StaticParameters, EShaderPlatform InPlatform, EMaterialQualityLevel::Type InQualityLevel, ERHIFeatureLevel::Type InFeatureLevel)
			: FHLSLMaterialTranslator(InMaterial, InMaterialCompilationOutput, StaticParameters, InPlatform, InQualityLevel, InFeatureLevel)
		{}
		int32 GetTextureCoordsCount() const
		{
			return GetNumUserTexCoords();
		}
		bool UsesVertexColor() const
		{
			return bUsesVertexColor;
		}

		bool UsesTransformVector() const
		{
			return bUsesTransformVector;
		}

		bool UsesWorldPositionExcludingShaderOffsets() const
		{
			return bNeedsWorldPositionExcludingShaderOffsets;
		}

		bool UsesPrecomputedAOMask() const
		{
			return bUsesAOMaterialMask;
		}

		bool UsesVertexPosition() const 
		{
			return bUsesVertexPosition;
		}
	};

	FMaterialCompilationOutput TempOutput;
	FMaterialResource* MaterialResource = GetMaterialResource(GMaxRHIFeatureLevel);
	FMaterialShaderMapId ShaderMapID;
	MaterialResource->GetShaderMapId(GMaxRHIShaderPlatform, nullptr, ShaderMapID);
	FStaticParameterSet StaticParamSet;
	MaterialResource->GetStaticParameterSet(GMaxRHIShaderPlatform, StaticParamSet);
	FMaterialAnalyzer MaterialTranslator(MaterialResource, TempOutput, StaticParamSet, GMaxRHIShaderPlatform, MaterialResource->GetQualityLevel(), GMaxRHIFeatureLevel);
	
	static_cast<FMaterialCompiler*>(&MaterialTranslator)->SetMaterialProperty(InProperty); // FHLSLMaterialTranslator hides this interface, so cast to parent
	CompileProperty(&MaterialTranslator, InProperty);
	// Request data from translator
	OutNumTextureCoordinates = MaterialTranslator.GetTextureCoordsCount();
	bOutRequiresVertexData = MaterialTranslator.UsesVertexColor() || MaterialTranslator.UsesTransformVector() || MaterialTranslator.UsesWorldPositionExcludingShaderOffsets() || MaterialTranslator.UsesPrecomputedAOMask() || MaterialTranslator.UsesVertexPosition();
#endif
}

#if WITH_EDITOR
bool UMaterialInterface::IsTextureReferencedByProperty(EMaterialProperty InProperty, const UTexture* InTexture)
{
	class FFindTextureVisitor : public IMaterialExpressionVisitor
	{
	public:
		explicit FFindTextureVisitor(const UTexture* InTexture) : Texture(InTexture), FoundTexture(false) {}

		virtual EMaterialExpressionVisitResult Visit(UMaterialExpression* InExpression) override
		{
			if (InExpression->GetReferencedTexture() == Texture)
			{
				FoundTexture = true;
				return MVR_STOP;
			}
			return MVR_CONTINUE;
		}

		const UTexture* Texture;
		bool FoundTexture;
	};

	FMaterialCompilationOutput TempOutput;
	FMaterialResource* MaterialResource = GetMaterialResource(GMaxRHIFeatureLevel);
	FMaterialShaderMapId ShaderMapID;
	MaterialResource->GetShaderMapId(GMaxRHIShaderPlatform, nullptr, ShaderMapID);
	FStaticParameterSet StaticParamSet;
	MaterialResource->GetStaticParameterSet(GMaxRHIShaderPlatform, StaticParamSet);
	FHLSLMaterialTranslator MaterialTranslator(MaterialResource, TempOutput, StaticParamSet, GMaxRHIShaderPlatform, MaterialResource->GetQualityLevel(), GMaxRHIFeatureLevel);

	FFindTextureVisitor Visitor(InTexture);
	MaterialTranslator.VisitExpressionsForProperty(InProperty, Visitor);
	return Visitor.FoundTexture;
}
#endif // WITH_EDITOR

#if WITH_EDITORONLY_DATA
//Reorder the output index for any FExpressionInput connected to a UMaterialExpressionBreakMaterialAttributes.
//If the order of pins in the material results or the make/break attributes nodes changes 
//then the OutputIndex stored in any FExpressionInput coming from UMaterialExpressionBreakMaterialAttributes will be wrong and needs reordering.
void DoMaterialAttributeReorder(FExpressionInput* Input, int32 UE4Ver, int32 RenderObjVer)
{
	if( Input && Input->Expression && Input->Expression->IsA(UMaterialExpressionBreakMaterialAttributes::StaticClass()) )
	{
		if( UE4Ver < VER_UE4_MATERIAL_ATTRIBUTES_REORDERING )
		{
			switch(Input->OutputIndex)
			{
			case 4: Input->OutputIndex = 7; break;
			case 5: Input->OutputIndex = 4; break;
			case 6: Input->OutputIndex = 5; break;
			case 7: Input->OutputIndex = 6; break;
			}
		}
		
		if( UE4Ver < VER_UE4_FIX_REFRACTION_INPUT_MASKING && Input->OutputIndex == 13 )
		{
			Input->Mask = 1;
			Input->MaskR = 1;
			Input->MaskG = 1;
			Input->MaskB = 1;
			Input->MaskA = 0;
		}

		// closest version to the clear coat change
		if( UE4Ver < VER_UE4_ADD_ROOTCOMPONENT_TO_FOLIAGEACTOR && Input->OutputIndex >= 12 )
		{
			Input->OutputIndex += 2;
		}

		if (RenderObjVer < FRenderingObjectVersion::AnisotropicMaterial)
		{
			int32 OutputIdx = Input->OutputIndex;

			if (OutputIdx >= 4)
			{
				++Input->OutputIndex;
			}
			
			if (OutputIdx >= 8)
			{
				++Input->OutputIndex;
			}
		}
	}
}
#endif // WITH_EDITORONLY_DATA
//////////////////////////////////////////////////////////////////////////

FMaterialInstanceBasePropertyOverrides::FMaterialInstanceBasePropertyOverrides()
	:bOverride_OpacityMaskClipValue(false)
	,bOverride_BlendMode(false)
	,bOverride_ShadingModel(false)
	,bOverride_DitheredLODTransition(false)
	,bOverride_CastDynamicShadowAsMasked(false)
	,bOverride_TwoSided(false)
	,TwoSided(0)
	,DitheredLODTransition(0)
	,bCastDynamicShadowAsMasked(false)
	,BlendMode(BLEND_Opaque)
	,ShadingModel(MSM_DefaultLit)
	, OpacityMaskClipValue(.333333f)
{

}

bool FMaterialInstanceBasePropertyOverrides::operator==(const FMaterialInstanceBasePropertyOverrides& Other)const
{
	return	bOverride_OpacityMaskClipValue == Other.bOverride_OpacityMaskClipValue &&
			bOverride_BlendMode == Other.bOverride_BlendMode &&
			bOverride_ShadingModel == Other.bOverride_ShadingModel &&
			bOverride_TwoSided == Other.bOverride_TwoSided &&
			bOverride_DitheredLODTransition == Other.bOverride_DitheredLODTransition &&
			OpacityMaskClipValue == Other.OpacityMaskClipValue &&
			BlendMode == Other.BlendMode &&
			ShadingModel == Other.ShadingModel &&
			TwoSided == Other.TwoSided &&
			DitheredLODTransition == Other.DitheredLODTransition;
}

bool FMaterialInstanceBasePropertyOverrides::operator!=(const FMaterialInstanceBasePropertyOverrides& Other)const
{
	return !(*this == Other);
}

//////////////////////////////////////////////////////////////////////////
#if WITH_EDITOR
bool FMaterialShaderMapId::ContainsShaderType(const FShaderType* ShaderType, int32 PermutationId) const
{
	for (int32 TypeIndex = 0; TypeIndex < ShaderTypeDependencies.Num(); TypeIndex++)
	{
		if (ShaderTypeDependencies[TypeIndex].ShaderTypeName == ShaderType->GetHashedName() &&
			ShaderTypeDependencies[TypeIndex].PermutationId == PermutationId)
		{
			return true;
		}
	}

	return false;
}

bool FMaterialShaderMapId::ContainsShaderPipelineType(const FShaderPipelineType* ShaderPipelineType) const
{
	for (int32 TypeIndex = 0; TypeIndex < ShaderPipelineTypeDependencies.Num(); TypeIndex++)
	{
		if (ShaderPipelineTypeDependencies[TypeIndex].ShaderPipelineTypeName == ShaderPipelineType->GetHashedName())
		{
			return true;
		}
	}

	return false;
}

bool FMaterialShaderMapId::ContainsVertexFactoryType(const FVertexFactoryType* VFType) const
{
	for (int32 TypeIndex = 0; TypeIndex < VertexFactoryTypeDependencies.Num(); TypeIndex++)
	{
		if (VertexFactoryTypeDependencies[TypeIndex].VertexFactoryTypeName == VFType->GetHashedName())
		{
			return true;
		}
	}

	return false;
}
#endif // WITH_EDITOR
//////////////////////////////////////////////////////////////////////////

FMaterialAttributeDefintion::FMaterialAttributeDefintion(
		const FGuid& InAttributeID, const FString& InAttributeName, EMaterialProperty InProperty,
		EMaterialValueType InValueType, const FVector4& InDefaultValue, EShaderFrequency InShaderFrequency,
		int32 InTexCoordIndex /*= INDEX_NONE*/, bool bInIsHidden /*= false*/, MaterialAttributeBlendFunction InBlendFunction /*= nullptr*/)
	: AttributeID(InAttributeID)
	, DefaultValue(InDefaultValue)
	, AttributeName(InAttributeName)
	, Property(InProperty)
	, ValueType(InValueType)
	, ShaderFrequency(InShaderFrequency)
	, TexCoordIndex(InTexCoordIndex)
	, BlendFunction(InBlendFunction)
	, bIsHidden(bInIsHidden)
{
	checkf(ValueType & MCT_Float || ValueType == MCT_ShadingModel , TEXT("Unsupported type, only Float1 through Float4 or MCT_ShadingModel are allowed."));
}

int32 FMaterialAttributeDefintion::CompileDefaultValue(FMaterialCompiler* Compiler)
{
	int32 Ret;

	// TODO: Temporarily preserving hack from 4.13 to change default value for two-sided foliage model 
	if (Property == MP_SubsurfaceColor && Compiler->GetMaterialShadingModels().HasShadingModel(MSM_TwoSidedFoliage))
	{
		check(ValueType == MCT_Float3);
		return Compiler->Constant3(0, 0, 0);
	}

	if (Property == MP_ShadingModel)
	{
		check(ValueType == MCT_ShadingModel);
		// Default to the first shading model of the material. If the material is using a single shading model selected through the dropdown, this is how it gets written to the shader as a constant (optimizing out all the dynamic branches)
		return Compiler->ShadingModel(Compiler->GetMaterialShadingModels().GetFirstShadingModel());
	}

	if (TexCoordIndex == INDEX_NONE)
	{
		// Standard value type
		switch (ValueType)
		{
		case MCT_Float:
		case MCT_Float1: Ret = Compiler->Constant(DefaultValue.X); break;
		case MCT_Float2: Ret = Compiler->Constant2(DefaultValue.X, DefaultValue.Y); break;
		case MCT_Float3: Ret = Compiler->Constant3(DefaultValue.X, DefaultValue.Y, DefaultValue.Z); break;
		default: Ret = Compiler->Constant4(DefaultValue.X, DefaultValue.Y, DefaultValue.Z, DefaultValue.W);
		}
	}
	else
	{
		// Texture coordinates allow pass through for default	
		Ret = Compiler->TextureCoordinate(TexCoordIndex, false, false);
	}

	return Ret;
}

//////////////////////////////////////////////////////////////////////////

FMaterialCustomOutputAttributeDefintion::FMaterialCustomOutputAttributeDefintion(
		const FGuid& InAttributeID, const FString& InAttributeName, const FString& InFunctionName, EMaterialProperty InProperty,
		EMaterialValueType InValueType, const FVector4& InDefaultValue, EShaderFrequency InShaderFrequency, MaterialAttributeBlendFunction InBlendFunction /*= nullptr*/)
	: FMaterialAttributeDefintion(InAttributeID, InAttributeName, InProperty, InValueType, InDefaultValue, InShaderFrequency, INDEX_NONE, false, InBlendFunction)
	, FunctionName(InFunctionName)
{
}

//////////////////////////////////////////////////////////////////////////
FMaterialAttributeDefinitionMap FMaterialAttributeDefinitionMap::GMaterialPropertyAttributesMap;

void FMaterialAttributeDefinitionMap::InitializeAttributeMap()
{
	check(!bIsInitialized);
	bIsInitialized = true;
	const bool bHideAttribute = true;

	// All types plus default/missing attribute
	AttributeMap.Empty(MP_MAX + 1);

	// Basic attributes
	Add(FGuid(0x69B8D336, 0x16ED4D49, 0x9AA49729, 0x2F050F7A), TEXT("BaseColor"),		MP_BaseColor,		MCT_Float3,	FVector4(0,0,0,0),	SF_Pixel);
	Add(FGuid(0x57C3A161, 0x7F064296, 0xB00B24A5, 0xA496F34C), TEXT("Metallic"),		MP_Metallic,		MCT_Float,	FVector4(0,0,0,0),	SF_Pixel);
	Add(FGuid(0x9FDAB399, 0x25564CC9, 0x8CD2D572, 0xC12C8FED), TEXT("Specular"),		MP_Specular,		MCT_Float,	FVector4(.5,0,0,0), SF_Pixel);
	Add(FGuid(0xD1DD967C, 0x4CAD47D3, 0x9E6346FB, 0x08ECF210), TEXT("Roughness"),		MP_Roughness,		MCT_Float,	FVector4(.5,0,0,0), SF_Pixel);
	Add(FGuid(0x55E2B4FB, 0xC1C54DB2, 0x9F11875F, 0x7231EB1E), TEXT("Anisotropy"),		MP_Anisotropy,		MCT_Float,	FVector4(0,0,0,0),  SF_Pixel);
	Add(FGuid(0xB769B54D, 0xD08D4440, 0xABC21BA6, 0xCD27D0E2), TEXT("EmissiveColor"),	MP_EmissiveColor,	MCT_Float3,	FVector4(0,0,0,0),	SF_Pixel);
	Add(FGuid(0xB8F50FBA, 0x2A754EC1, 0x9EF672CF, 0xEB27BF51), TEXT("Opacity"),			MP_Opacity,			MCT_Float,	FVector4(1,0,0,0),	SF_Pixel);
	Add(FGuid(0x679FFB17, 0x2BB5422C, 0xAD520483, 0x166E0C75), TEXT("OpacityMask"),		MP_OpacityMask,		MCT_Float,	FVector4(1,0,0,0),	SF_Pixel);
	Add(FGuid(0x0FA2821A, 0x200F4A4A, 0xB719B789, 0xC1259C64), TEXT("Normal"),			MP_Normal,			MCT_Float3,	FVector4(0,0,1,0),	SF_Pixel);
	Add(FGuid(0xD5F8E9CF, 0xCDC3468D, 0xB10E4465, 0x596A7BBA), TEXT("Tangent"),			MP_Tangent,			MCT_Float3,	FVector4(1,0,0,0),	SF_Pixel);

	// Advanced attributes
	Add(FGuid(0xF905F895, 0xD5814314, 0x916D2434, 0x8C40CE9E), TEXT("WorldPositionOffset"),		MP_WorldPositionOffset,		MCT_Float3,	FVector4(0,0,0,0),	SF_Vertex);
	Add(FGuid(0x2091ECA2, 0xB59248EE, 0x8E2CD578, 0xD371926D), TEXT("WorldDisplacement"),		MP_WorldDisplacement,		MCT_Float3,	FVector4(0,0,0,0),	SF_Domain);
	Add(FGuid(0xA0119D44, 0xC456450D, 0x9C39C933, 0x1F72D8D1), TEXT("TessellationMultiplier"),	MP_TessellationMultiplier,	MCT_Float,	FVector4(1,0,0,0),	SF_Hull);
	Add(FGuid(0x5B8FC679, 0x51CE4082, 0x9D777BEE, 0xF4F72C44), TEXT("SubsurfaceColor"),			MP_SubsurfaceColor,			MCT_Float3,	FVector4(1,1,1,0),	SF_Pixel);
	Add(FGuid(0x9E502E69, 0x3C8F48FA, 0x94645CFD, 0x28E5428D), TEXT("ClearCoat"),				MP_CustomData0,				MCT_Float,	FVector4(1,0,0,0),	SF_Pixel);
	Add(FGuid(0xBE4F2FFD, 0x12FC4296, 0xB0124EEA, 0x12C28D92), TEXT("ClearCoatRoughness"),		MP_CustomData1,				MCT_Float,	FVector4(.1,0,0,0),	SF_Pixel);
	Add(FGuid(0xE8EBD0AD, 0xB1654CBE, 0xB079C3A8, 0xB39B9F15), TEXT("AmbientOcclusion"),		MP_AmbientOcclusion,		MCT_Float,	FVector4(1,0,0,0),	SF_Pixel);
	Add(FGuid(0xD0B0FA03, 0x14D74455, 0xA851BAC5, 0x81A0788B), TEXT("Refraction"),				MP_Refraction,				MCT_Float2,	FVector4(1,0,0,0),	SF_Pixel);
	Add(FGuid(0x0AC97EC3, 0xE3D047BA, 0xB610167D, 0xC4D919FF), TEXT("PixelDepthOffset"),		MP_PixelDepthOffset,		MCT_Float,	FVector4(0,0,0,0),	SF_Pixel);
	Add(FGuid(0xD9423FFF, 0xD77E4D82, 0x8FF9CF5E, 0x055D1255), TEXT("ShadingModel"),			MP_ShadingModel,			MCT_ShadingModel,	FVector4(0,0,0,0),	SF_Pixel, INDEX_NONE, false, &CompileShadingModelBlendFunction);

	// Texture coordinates
	Add(FGuid(0xD30EC284, 0xE13A4160, 0x87BB5230, 0x2ED115DC), TEXT("CustomizedUV0"), MP_CustomizedUVs0, MCT_Float2, FVector4(0,0,0,0), SF_Vertex, 0);
	Add(FGuid(0xC67B093C, 0x2A5249AA, 0xABC97ADE, 0x4A1F49C5), TEXT("CustomizedUV1"), MP_CustomizedUVs1, MCT_Float2, FVector4(0,0,0,0), SF_Vertex, 1);
	Add(FGuid(0x85C15B24, 0xF3E047CA, 0x85856872, 0x01AE0F4F), TEXT("CustomizedUV2"), MP_CustomizedUVs2, MCT_Float2, FVector4(0,0,0,0), SF_Vertex, 2);
	Add(FGuid(0x777819DC, 0x31AE4676, 0xB864EF77, 0xB807E873), TEXT("CustomizedUV3"), MP_CustomizedUVs3, MCT_Float2, FVector4(0,0,0,0), SF_Vertex, 3);
	Add(FGuid(0xDA63B233, 0xDDF44CAD, 0xB93D867B, 0x8DAFDBCC), TEXT("CustomizedUV4"), MP_CustomizedUVs4, MCT_Float2, FVector4(0,0,0,0), SF_Vertex, 4);
	Add(FGuid(0xC2F52B76, 0x4A034388, 0x89119528, 0x2071B190), TEXT("CustomizedUV5"), MP_CustomizedUVs5, MCT_Float2, FVector4(0,0,0,0), SF_Vertex, 5);
	Add(FGuid(0x8214A8CA, 0x0CB944CF, 0x9DFD78DB, 0xE48BB55F), TEXT("CustomizedUV6"), MP_CustomizedUVs6, MCT_Float2, FVector4(0,0,0,0), SF_Vertex, 6);
	Add(FGuid(0xD8F8D01F, 0xC6F74715, 0xA3CFB4FF, 0x9EF51FAC), TEXT("CustomizedUV7"), MP_CustomizedUVs7, MCT_Float2, FVector4(0,0,0,0), SF_Vertex, 7);

	// Lightmass attributes	
	Add(FGuid(0x68934E1B, 0x70EB411B, 0x86DF5AA5, 0xDF2F626C), TEXT("DiffuseColor"),	MP_DiffuseColor,	MCT_Float3, FVector4(0,0,0,0), SF_Pixel, INDEX_NONE, bHideAttribute);
	Add(FGuid(0xE89CBD84, 0x62EA48BE, 0x80F88521, 0x2B0C403C), TEXT("SpecularColor"),	MP_SpecularColor,	MCT_Float3, FVector4(0,0,0,0), SF_Pixel, INDEX_NONE, bHideAttribute);

	// Debug attributes
	Add(FGuid(0x5BF6BA94, 0xA3264629, 0xA253A05B, 0x0EABBB86), TEXT("Missing"), MP_MAX, MCT_Float, FVector4(0,0,0,0), SF_Pixel, INDEX_NONE, bHideAttribute);

	// UMaterialExpression custom outputs
	AddCustomAttribute(FGuid(0xfbd7b46e, 0xb1234824, 0xbde76b23, 0x609f984c), "BentNormal", "GetBentNormal", MCT_Float3, FVector4(0, 0, 1, 0));
	AddCustomAttribute(FGuid(0xAA3D5C04, 0x16294716, 0xBBDEC869, 0x6A27DD72), "ClearCoatBottomNormal", "ClearCoatBottomNormal", MCT_Float3, FVector4(0, 0, 1, 0));
	AddCustomAttribute(FGuid(0x8EAB2CB2, 0x73634A24, 0x8CD14F47, 0x3F9C8E55), "CustomEyeTangent", "GetTangentOutput", MCT_Float3, FVector4(0, 0, 0, 0));
}


void FMaterialAttributeDefinitionMap::Add(const FGuid& AttributeID, const FString& AttributeName, EMaterialProperty Property,
	EMaterialValueType ValueType, const FVector4& DefaultValue, EShaderFrequency ShaderFrequency,
	int32 TexCoordIndex /*= INDEX_NONE*/, bool bIsHidden /*= false*/, MaterialAttributeBlendFunction BlendFunction /*= nullptr*/)
{
	checkf(!AttributeMap.Contains(Property), TEXT("Tried to add duplicate material property."));
	AttributeMap.Add(Property, FMaterialAttributeDefintion(AttributeID, AttributeName, Property, ValueType, DefaultValue, ShaderFrequency, TexCoordIndex, bIsHidden, BlendFunction));
	if (!bIsHidden)
	{
		OrderedVisibleAttributeList.Add(AttributeID);
	}
}

FMaterialAttributeDefintion* FMaterialAttributeDefinitionMap::Find(const FGuid& AttributeID)
{
	for (auto& Attribute : CustomAttributes)
	{
		if (Attribute.AttributeID == AttributeID)
		{
			return &Attribute;
		}
	}

	for (auto& Attribute : AttributeMap)
	{
		if (Attribute.Value.AttributeID == AttributeID)
		{
			return &Attribute.Value;
		}
	}
	
	UE_LOG(LogMaterial, Warning, TEXT("Failed to find material attribute, AttributeID: %s."), *AttributeID.ToString(EGuidFormats::Digits));
	return Find(MP_MAX);
}

FMaterialAttributeDefintion* FMaterialAttributeDefinitionMap::Find(EMaterialProperty Property)
{
	if (FMaterialAttributeDefintion* Attribute = AttributeMap.Find(Property))
	{
		return Attribute;
	}

	UE_LOG(LogMaterial, Warning, TEXT("Failed to find material attribute, PropertyType: %i."), (uint32)Property);
	return Find(MP_MAX);
}

FText FMaterialAttributeDefinitionMap::GetAttributeOverrideForMaterial(const FGuid& AttributeID, UMaterial* Material)
{
	TArray<TKeyValuePair<EMaterialShadingModel, FString>> CustomPinNames;
	EMaterialProperty Property = GMaterialPropertyAttributesMap.Find(AttributeID)->Property;

	switch (Property)
	{
	case MP_EmissiveColor:
		return Material->IsUIMaterial() ? LOCTEXT("UIOutputColor", "Final Color") : LOCTEXT("EmissiveColor", "Emissive Color");
	case MP_Opacity:
		return LOCTEXT("Opacity", "Opacity");
	case MP_OpacityMask:
		return LOCTEXT("OpacityMask", "Opacity Mask");
	case MP_DiffuseColor:
		return LOCTEXT("DiffuseColor", "Diffuse Color");
	case MP_SpecularColor:
		return LOCTEXT("SpecularColor", "Specular Color");
	case MP_BaseColor:
		return Material->MaterialDomain == MD_Volume ? LOCTEXT("Albedo", "Albedo") : LOCTEXT("BaseColor", "Base Color");
	case MP_Metallic:
		CustomPinNames.Add({MSM_Hair, "Scatter"});
		return FText::FromString(GetPinNameFromShadingModelField(Material->GetShadingModels(), CustomPinNames, "Metallic"));
	case MP_Specular:
		return LOCTEXT("Specular", "Specular");
	case MP_Roughness:
		return LOCTEXT("Roughness", "Roughness");
	case MP_Anisotropy:
		return LOCTEXT("Anisotropy", "Anisotropy");
	case MP_Normal:
		CustomPinNames.Add({MSM_Hair, "Tangent"});
		return FText::FromString(GetPinNameFromShadingModelField(Material->GetShadingModels(), CustomPinNames, "Normal"));
	case MP_Tangent:
		return LOCTEXT("Tangent", "Tangent");
	case MP_WorldPositionOffset:
		return Material->IsUIMaterial() ? LOCTEXT("ScreenPosition", "Screen Position") : LOCTEXT("WorldPositionOffset", "World Position Offset");
	case MP_WorldDisplacement:
		return LOCTEXT("WorldDisplacement", "World Displacement");
	case MP_TessellationMultiplier:
		return LOCTEXT("TessellationMultiplier", "Tessellation Multiplier");
	case MP_SubsurfaceColor:
		if (Material->MaterialDomain == MD_Volume)
		{
			return LOCTEXT("Extinction", "Extinction");
		}
		CustomPinNames.Add({MSM_Cloth, "Fuzz Color"});
		return FText::FromString(GetPinNameFromShadingModelField(Material->GetShadingModels(), CustomPinNames, "Subsurface Color"));
	case MP_CustomData0:	
		CustomPinNames.Add({ MSM_ClearCoat, "Clear Coat" });
		CustomPinNames.Add({MSM_Hair, "Backlit"});
		CustomPinNames.Add({MSM_Cloth, "Cloth"});
		CustomPinNames.Add({MSM_Eye, "Iris Mask"});
		return FText::FromString(GetPinNameFromShadingModelField(Material->GetShadingModels(), CustomPinNames, "Custom Data 0"));
	case MP_CustomData1:
		CustomPinNames.Add({ MSM_ClearCoat, "Clear Coat Roughness" });
		CustomPinNames.Add({MSM_Eye, "Iris Distance"});
		return FText::FromString(GetPinNameFromShadingModelField(Material->GetShadingModels(), CustomPinNames, "Custom Data 1"));
	case MP_AmbientOcclusion:
		return LOCTEXT("AmbientOcclusion", "Ambient Occlusion");
	case MP_Refraction:
		return LOCTEXT("Refraction", "Refraction");
	case MP_CustomizedUVs0:
		return LOCTEXT("CustomizedUV0", "Customized UV 0");
	case MP_CustomizedUVs1:
		return LOCTEXT("CustomizedUV1", "Customized UV 1");
	case MP_CustomizedUVs2:
		return LOCTEXT("CustomizedUV2", "Customized UV 2");
	case MP_CustomizedUVs3:
		return LOCTEXT("CustomizedUV3", "Customized UV 3");
	case MP_CustomizedUVs4:
		return LOCTEXT("CustomizedUV4", "Customized UV 4");
	case MP_CustomizedUVs5:
		return LOCTEXT("CustomizedUV5", "Customized UV 5");
	case MP_CustomizedUVs6:
		return LOCTEXT("CustomizedUV6", "Customized UV 6");
	case MP_CustomizedUVs7:
		return LOCTEXT("CustomizedUV7", "Customized UV 7");
	case MP_PixelDepthOffset:
		return LOCTEXT("PixelDepthOffset", "Pixel Depth Offset");
	case MP_ShadingModel:
		return LOCTEXT("ShadingModel", "Shading Model");
	case MP_CustomOutput:
		return FText::FromString(GetAttributeName(AttributeID));
		
	}
	return  LOCTEXT("Missing", "Missing");
}

FString FMaterialAttributeDefinitionMap::GetPinNameFromShadingModelField(FMaterialShadingModelField InShadingModels, const TArray<TKeyValuePair<EMaterialShadingModel, FString>>& InCustomShadingModelPinNames, const FString& InDefaultPinName) 
{
	FString OutPinName;
	for (const TKeyValuePair<EMaterialShadingModel, FString>& CustomShadingModelPinName : InCustomShadingModelPinNames)
	{
		if (InShadingModels.HasShadingModel(CustomShadingModelPinName.Key))
		{
			// Add delimiter
			if (!OutPinName.IsEmpty())
			{
				OutPinName.Append(" or ");
			}

			// Append the name and remove the shading model from the temp field
			OutPinName.Append(CustomShadingModelPinName.Value);
			InShadingModels.RemoveShadingModel(CustomShadingModelPinName.Key);
		}
	}

	// There are other shading models present, these don't have their own specific name for this pin, so use a default one
	if (InShadingModels.CountShadingModels() != 0)
	{
		// Add delimiter
		if (!OutPinName.IsEmpty())
		{
			OutPinName.Append(" or ");
		}

		OutPinName.Append(InDefaultPinName);
	}

	ensure(!OutPinName.IsEmpty());
	return OutPinName;
}

void FMaterialAttributeDefinitionMap::AppendDDCKeyString(FString& String)
{
	FString& DDCString = GMaterialPropertyAttributesMap.AttributeDDCString;

	if (DDCString.Len() == 0)
	{
		FString AttributeIDs;

		for (const auto& Attribute : GMaterialPropertyAttributesMap.AttributeMap)
		{
			AttributeIDs += Attribute.Value.AttributeID.ToString(EGuidFormats::Digits);
		}

		for (const auto& Attribute : GMaterialPropertyAttributesMap.CustomAttributes)
		{
			AttributeIDs += Attribute.AttributeID.ToString(EGuidFormats::Digits);
		}

		FSHA1 HashState;
		HashState.UpdateWithString(*AttributeIDs, AttributeIDs.Len());
		HashState.Final();

		FSHAHash Hash;
		HashState.GetHash(&Hash.Hash[0]);
		DDCString = Hash.ToString();
	}
	else
	{
		// TODO: In debug force re-generate DDC string and compare to catch invalid runtime changes
	}

	String.Append(DDCString);
}

void FMaterialAttributeDefinitionMap::AddCustomAttribute(const FGuid& AttributeID, const FString& AttributeName, const FString& FunctionName, EMaterialValueType ValueType, const FVector4& DefaultValue, MaterialAttributeBlendFunction BlendFunction /*= nullptr*/)
{
	// Make sure that we init CustomAttributes before DDCString is initialized (before first shader load)
	check(GMaterialPropertyAttributesMap.AttributeDDCString.Len() == 0);

	FMaterialCustomOutputAttributeDefintion UserAttribute(AttributeID, AttributeName, FunctionName, MP_CustomOutput, ValueType, DefaultValue, SF_Pixel, BlendFunction);
#if DO_CHECK
	for (auto& Attribute : GMaterialPropertyAttributesMap.AttributeMap)
	{
		checkf(Attribute.Value.AttributeID != AttributeID, TEXT("Tried to add duplicate custom output attribute (%s) already in base attributes (%s)."), *AttributeName, *(Attribute.Value.AttributeName));
	}
	checkf(!GMaterialPropertyAttributesMap.CustomAttributes.Contains(UserAttribute), TEXT("Tried to add duplicate custom output attribute (%s)."), *AttributeName);
#endif
	GMaterialPropertyAttributesMap.CustomAttributes.Add(UserAttribute);

	if (!UserAttribute.bIsHidden)
	{
		GMaterialPropertyAttributesMap.OrderedVisibleAttributeList.Add(AttributeID);
	}
}

FGuid FMaterialAttributeDefinitionMap::GetCustomAttributeID(const FString& AttributeName)
{
	for (auto& Attribute : GMaterialPropertyAttributesMap.CustomAttributes)
	{
		if (Attribute.AttributeName == AttributeName)
		{
			return Attribute.AttributeID;
		}
	}

	return GMaterialPropertyAttributesMap.Find(MP_MAX)->AttributeID;
}

void FMaterialAttributeDefinitionMap::GetCustomAttributeList(TArray<FMaterialCustomOutputAttributeDefintion>& CustomAttributeList)
{
	CustomAttributeList.Empty(GMaterialPropertyAttributesMap.CustomAttributes.Num());
	for (auto& Attribute : GMaterialPropertyAttributesMap.CustomAttributes)
	{
		CustomAttributeList.Add(Attribute);
	}
}

void FMaterialAttributeDefinitionMap::GetAttributeNameToIDList(TArray<TPair<FString, FGuid>>& NameToIDList)
{
	NameToIDList.Empty(GMaterialPropertyAttributesMap.OrderedVisibleAttributeList.Num());
	for (const FGuid& AttributeID : GMaterialPropertyAttributesMap.OrderedVisibleAttributeList)
	{
		FMaterialAttributeDefintion* Attribute = GMaterialPropertyAttributesMap.Find(AttributeID);
		NameToIDList.Emplace(Attribute->AttributeName, AttributeID);
	}
}

FMaterialResourceMemoryWriter::FMaterialResourceMemoryWriter(FArchive& Ar) :
	FMemoryWriter(Bytes, Ar.IsPersistent(), false, TEXT("FShaderMapMemoryWriter")),
	ParentAr(&Ar)
{
	check(Ar.IsSaving());
	this->SetByteSwapping(Ar.IsByteSwapping());
	this->SetCookingTarget(Ar.CookingTarget());
}

FMaterialResourceMemoryWriter::~FMaterialResourceMemoryWriter()
{
	SerializeToParentArchive();
}

FArchive& FMaterialResourceMemoryWriter::operator<<(class FName& Name)
{
	const int32* Idx = Name2Indices.Find(Name.GetDisplayIndex());
	int32 NewIdx;
	if (Idx)
	{
		NewIdx = *Idx;
	}
	else
	{
		NewIdx = Name2Indices.Num();
		Name2Indices.Add(Name.GetDisplayIndex(), NewIdx);
	}
	int32 InstNum = Name.GetNumber();
	static_assert(sizeof(decltype(DeclVal<FName>().GetNumber())) == sizeof(int32), "FName serialization in FMaterialResourceMemoryWriter requires changing, InstNum is no longer 32-bit");
	*this << NewIdx << InstNum;
	return *this;
}

void FMaterialResourceMemoryWriter::SerializeToParentArchive()
{
	FArchive& Ar = *ParentAr;
	check(Ar.IsSaving() && this->IsByteSwapping() == Ar.IsByteSwapping());

	// Make a array of unique names used by the shader map
	TArray<FNameEntryId> DisplayIndices;
	auto NumNames = Name2Indices.Num();
	DisplayIndices.Empty(NumNames);
	DisplayIndices.AddDefaulted(NumNames);
	for (const auto& Pair : Name2Indices)
	{
		DisplayIndices[Pair.Value] = Pair.Key;
	}

	Ar << NumNames;
	for (FNameEntryId DisplayIdx : DisplayIndices)
	{
		FName::GetEntry(DisplayIdx)->Write(Ar);
	}
	
	Ar << Locs;
	auto NumBytes = Bytes.Num();
	Ar << NumBytes;
	Ar.Serialize(&Bytes[0], NumBytes);
}

static inline void AdjustForSingleRead(
	FArchive* RESTRICT ArPtr,
	const TArray<FMaterialResourceLocOnDisk>& Locs,
	int64 OffsetToFirstResource,
	ERHIFeatureLevel::Type FeatureLevel,
	EMaterialQualityLevel::Type QualityLevel)
{
#if STORE_ONLY_ACTIVE_SHADERMAPS
	FArchive& Ar = *ArPtr;

	if (FeatureLevel != ERHIFeatureLevel::Num)
	{
		check(QualityLevel != EMaterialQualityLevel::Num);
		const FMaterialResourceLocOnDisk* RESTRICT Loc =
			FindMaterialResourceLocOnDisk(Locs, FeatureLevel, QualityLevel);
		if (!Loc)
		{
			Loc = FindMaterialResourceLocOnDisk(Locs, FeatureLevel, EMaterialQualityLevel::Num);
			check(Loc);
		}
		if (Loc->Offset)
		{
			const int64 ActualOffset = OffsetToFirstResource + Loc->Offset;
			Ar.Seek(ActualOffset);
		}
	}
#endif
}

FMaterialResourceProxyReader::FMaterialResourceProxyReader(
	FArchive& Ar,
	ERHIFeatureLevel::Type FeatureLevel,
	EMaterialQualityLevel::Type QualityLevel) :
	FArchiveProxy(Ar),
	OffsetToEnd(-1)
{
	check(InnerArchive.IsLoading());
	Initialize(FeatureLevel, QualityLevel, FeatureLevel != ERHIFeatureLevel::Num);
}

FMaterialResourceProxyReader::FMaterialResourceProxyReader(
	const TCHAR* Filename,
	uint32 NameMapOffset,
	ERHIFeatureLevel::Type FeatureLevel,
	EMaterialQualityLevel::Type QualityLevel) :
	TUniquePtr(IFileManager::Get().CreateFileReader(Filename, FILEREAD_NoFail)), // Create and store the FileArchive
	FArchiveProxy(*Get()), // Now link it to the archive proxy
	OffsetToEnd(-1)
{
	InnerArchive.Seek(NameMapOffset);
	Initialize(FeatureLevel, QualityLevel);
}

FMaterialResourceProxyReader::~FMaterialResourceProxyReader()
{
	if (OffsetToEnd != -1)
	{
		InnerArchive.Seek(OffsetToEnd);
	}
}

FArchive& FMaterialResourceProxyReader::operator<<(class FName& Name)
{
	int32 NameIdx;
	int32 InstNum;
	static_assert(sizeof(decltype(DeclVal<FName>().GetNumber())) == sizeof(int32), "FName serialization in FMaterialResourceProxyReader requires changing, InstNum is no longer 32-bit");
	InnerArchive << NameIdx << InstNum;
	if (NameIdx >= 0 && NameIdx < Names.Num())
	{
		Name = FName(Names[NameIdx], InstNum);
	}
	else
	{
		UE_LOG(LogMaterial, Fatal, TEXT("FMaterialResourceProxyReader: deserialized an invalid FName, NameIdx=%d, Names.Num()=%d (Offset=%lld, InnerArchive.Tell()=%lld, OffsetToFirstResource=%lld)"), 
			NameIdx, Names.Num(), Tell(), InnerArchive.Tell(), OffsetToFirstResource);
	}
	return *this;
}

void FMaterialResourceProxyReader::Initialize(
	ERHIFeatureLevel::Type FeatureLevel,
	EMaterialQualityLevel::Type QualityLevel,
	bool bSeekToEnd)
{
	SCOPED_LOADTIMER(FMaterialResourceProxyReader_Initialize);

	decltype(Names.Num()) NumNames;
	InnerArchive << NumNames;
	Names.Empty(NumNames);
	for (int32 Idx = 0; Idx < NumNames; ++Idx)
	{
		FNameEntrySerialized Entry(ENAME_LinkerConstructor);
		InnerArchive << Entry;
		Names.Add(Entry);
	}

	TArray<FMaterialResourceLocOnDisk> Locs;
	InnerArchive << Locs;
	check(Locs[0].Offset == 0);
	decltype(DeclVal<TArray<uint8>>().Num()) NumBytes;
	InnerArchive << NumBytes;

	OffsetToFirstResource = InnerArchive.Tell();
	AdjustForSingleRead(&InnerArchive, Locs, OffsetToFirstResource, FeatureLevel, QualityLevel);

	if (bSeekToEnd)
	{
		OffsetToEnd = OffsetToFirstResource + NumBytes;
	}
}

typedef TMap<TRefCountPtr<FMaterial>, TRefCountPtr<FMaterialShaderMap>> FMaterialsToUpdateMap;

void SetShaderMapsOnMaterialResources_RenderThread(FRHICommandListImmediate& RHICmdList, FMaterialsToUpdateMap& MaterialsToUpdate)
{
	SCOPE_CYCLE_COUNTER(STAT_Scene_SetShaderMapsOnMaterialResources_RT);

	bool bUpdateFeatureLevel[ERHIFeatureLevel::Num] = { false };

	for (auto& It : MaterialsToUpdate)
	{
		FMaterial* Material = It.Key;
		Material->SetRenderingThreadShaderMap(It.Value);
		//check(!ShaderMap || ShaderMap->IsValidForRendering());
		bUpdateFeatureLevel[Material->GetFeatureLevel()] = true;
	}

	bool bFoundAnyInitializedMaterials = false;

	// Iterate through all loaded material render proxies and recache their uniform expressions if needed
	// This search does not scale well, but is only used when uploading async shader compile results
	for (int32 FeatureLevelIndex = 0; FeatureLevelIndex < UE_ARRAY_COUNT(bUpdateFeatureLevel); ++FeatureLevelIndex)
	{
		if (bUpdateFeatureLevel[FeatureLevelIndex])
		{
			const ERHIFeatureLevel::Type MaterialFeatureLevel = (ERHIFeatureLevel::Type) FeatureLevelIndex;

			for (TSet<FMaterialRenderProxy*>::TConstIterator It(FMaterialRenderProxy::GetMaterialRenderProxyMap()); It; ++It)
			{
				FMaterialRenderProxy* MaterialProxy = *It;
				const FMaterial* Material = MaterialProxy->GetMaterialNoFallback(MaterialFeatureLevel);

				// Using ContainsByHash so we can pass a raw-ptr to TMap method that wants a TRefCountPtr
				if (Material && Material->GetRenderingThreadShaderMap() && MaterialsToUpdate.ContainsByHash(GetTypeHash(Material), Material))
				{
					MaterialProxy->CacheUniformExpressions(true);
					bFoundAnyInitializedMaterials = true;

					/*const FMaterial& MaterialForRendering = *MaterialProxy->GetMaterial(MaterialFeatureLevel);
					check(MaterialForRendering.GetRenderingThreadShaderMap());
					check(!MaterialProxy->UniformExpressionCache[MaterialFeatureLevel].bUpToDate
						|| MaterialProxy->UniformExpressionCache[MaterialFeatureLevel].CachedUniformExpressionShaderMap == MaterialForRendering.GetRenderingThreadShaderMap());
					check(MaterialForRendering.GetRenderingThreadShaderMap()->IsValidForRendering());*/
				}
			}
		}
	}
}

void FMaterial::SetShaderMapsOnMaterialResources(const TMap<TRefCountPtr<FMaterial>, TRefCountPtr<FMaterialShaderMap>>& MaterialsToUpdate)
{
	for (const auto& It : MaterialsToUpdate)
	{
		FMaterial* Material = It.Key;
		const TRefCountPtr<FMaterialShaderMap>& ShaderMap = It.Value;
		Material->GameThreadShaderMap = ShaderMap;
		Material->bGameThreadShaderMapIsComplete = ShaderMap ? ShaderMap->IsComplete(Material, true) : false;
	}

	ENQUEUE_RENDER_COMMAND(FSetShaderMapOnMaterialResources)(
	[InMaterialsToUpdate = MaterialsToUpdate](FRHICommandListImmediate& RHICmdList) mutable
	{
		SetShaderMapsOnMaterialResources_RenderThread(RHICmdList, InMaterialsToUpdate);
	});
}
#undef LOCTEXT_NAMESPACE