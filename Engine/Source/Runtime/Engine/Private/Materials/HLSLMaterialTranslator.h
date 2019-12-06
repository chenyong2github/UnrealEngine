// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
/*=============================================================================
	HLSLMaterialTranslator.h: Translates material expressions into HLSL code.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "Algo/Transform.h"
#include "Misc/Guid.h"
#include "HAL/IConsoleManager.h"
#include "ShaderParameters.h"
#include "StaticParameterSet.h"
#include "MaterialShared.h"
#include "Stats/StatsMisc.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialFunctionInstance.h"
#include "Materials/MaterialExpressionSingleLayerWaterMaterialOutput.h"
#include "MaterialCompiler.h"
#include "RenderUtils.h"
#include "EngineGlobals.h"
#include "Engine/Engine.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Hash/CityHash.h"
#include "VT/RuntimeVirtualTexture.h"

#if WITH_EDITORONLY_DATA
#include "Materials/MaterialExpressionSceneTexture.h"
#include "Materials/MaterialExpressionNoise.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionCustomOutput.h"
#include "Materials/MaterialExpressionVectorNoise.h"
#include "Materials/MaterialExpressionVertexInterpolator.h"
#include "Materials/MaterialUniformExpressions.h"
#include "ParameterCollection.h"
#include "Materials/MaterialParameterCollection.h"
#include "Containers/LazyPrintf.h"
#include "Containers/HashTable.h"
#include "Engine/Texture2D.h"
#endif

class Error;

#if WITH_EDITORONLY_DATA

enum EMaterialExpressionVisitResult
{
	MVR_CONTINUE,
	MVR_STOP,
};

class IMaterialExpressionVisitor
{
public:
	virtual ~IMaterialExpressionVisitor() {}
	virtual EMaterialExpressionVisitResult Visit(UMaterialExpression* InExpression) = 0;
};

struct FShaderCodeChunk
{
	/**
	 * Hash of the code chunk, used to determine equivalent chunks created from different expressions
	 * By default this is simply the hash of the code string
	 */
	uint64 Hash;
	/** 
	 * Definition string of the code chunk. 
	 * If !bInline && !UniformExpression || UniformExpression->IsConstant(), this is the definition of a local variable named by SymbolName.
	 * Otherwise if bInline || (UniformExpression && UniformExpression->IsConstant()), this is a code expression that needs to be inlined.
	 */
	FString Definition;
	/** 
	 * Name of the local variable used to reference this code chunk. 
	 * If bInline || UniformExpression, there will be no symbol name and Definition should be used directly instead.
	 */
	FString SymbolName;
	/** Reference to a uniform expression, if this code chunk has one. */
	TRefCountPtr<FMaterialUniformExpression> UniformExpression;
	EMaterialValueType Type;
	/** Whether the code chunk should be inlined or not.  If true, SymbolName is empty and Definition contains the code to inline. */
	bool bInline;

	/** Ctor for creating a new code chunk with no associated uniform expression. */
	FShaderCodeChunk(uint64 InHash, const TCHAR* InDefinition,const FString& InSymbolName,EMaterialValueType InType,bool bInInline):
		Hash(InHash),
		Definition(InDefinition),
		SymbolName(InSymbolName),
		UniformExpression(NULL),
		Type(InType),
		bInline(bInInline)
	{}

	/** Ctor for creating a new code chunk with a uniform expression. */
	FShaderCodeChunk(uint64 InHash, FMaterialUniformExpression* InUniformExpression,const TCHAR* InDefinition,EMaterialValueType InType):
		Hash(InHash),
		Definition(InDefinition),
		UniformExpression(InUniformExpression),
		Type(InType),
		bInline(false)
	{}
};

struct FMaterialVTStackEntry
{
	uint64 ScopeID;
	uint64 CoordinateHash;
	uint64 MipValue0Hash;
	uint64 MipValue1Hash;
	ETextureMipValueMode MipValueMode;
	TextureAddress AddressU;
	TextureAddress AddressV;
	int32 DebugCoordinateIndex;
	int32 DebugMipValue0Index;
	int32 DebugMipValue1Index;
	int32 PreallocatedStackTextureIndex;
	bool bGenerateFeedback;
	float AspectRatio;

	int32 CodeIndex;
};

class FHLSLMaterialTranslator : public FMaterialCompiler
{
protected:

	/** The shader frequency of the current material property being compiled. */
	EShaderFrequency ShaderFrequency;
	/** The current material property being compiled.  This affects the behavior of all compiler functions except GetFixedParameterCode. */
	EMaterialProperty MaterialProperty;
	/** Stack of currently compiling material attributes*/
	TArray<FGuid> MaterialAttributesStack;
	/** Stack of currently compiling material parameter owners*/
	TArray<FMaterialParameterInfo> ParameterOwnerStack;
	/** The code chunks corresponding to the currently compiled property or custom output. */
	TArray<FShaderCodeChunk>* CurrentScopeChunks;
	uint64 CurrentScopeID;
	uint64 NextTempScopeID;

	// List of Shared pixel properties. Used to share generated code
	bool SharedPixelProperties[CompiledMP_MAX];

	/* Stack that tracks compiler state specific to the function currently being compiled. */
	TArray<FMaterialFunctionCompileState*> FunctionStacks[SF_NumFrequencies];

	/** Material being compiled.  Only transient compilation output like error information can be stored on the FMaterial. */
	FMaterial* Material;
	/** Compilation output which will be stored in the DDC. */
	FMaterialCompilationOutput& MaterialCompilationOutput;
	FStaticParameterSet StaticParameters;
	EShaderPlatform Platform;
	/** Quality level being compiled for. */
	EMaterialQualityLevel::Type QualityLevel;

	/** Feature level being compiled for. */
	ERHIFeatureLevel::Type FeatureLevel;

	/** Code chunk definitions corresponding to each of the material inputs, only initialized after Translate has been called. */
	FString TranslatedCodeChunkDefinitions[CompiledMP_MAX];

	/** Code chunks corresponding to each of the material inputs, only initialized after Translate has been called. */
	FString TranslatedCodeChunks[CompiledMP_MAX];

	/** Line number of the #line in MaterialTemplate.usf */
	int32 MaterialTemplateLineNumber;

	/** Stores the resource declarations */
	FString ResourcesString;

	/** Contents of the MaterialTemplate.usf file */
	FString MaterialTemplate;

	// Array of code chunks per material property
	TArray<FShaderCodeChunk> SharedPropertyCodeChunks[SF_NumFrequencies];

	// Uniform expressions used across all material properties
	TArray<FShaderCodeChunk> UniformExpressions;

	/** Parameter collections referenced by this material.  The position in this array is used as an index on the shader parameter. */
	TArray<UMaterialParameterCollection*> ParameterCollections;

	// Index of the next symbol to create
	int32 NextSymbolIndex;

	/** Any custom expression function implementations */
	TArray<FString> CustomExpressionImplementations;

	/** Any custom output function implementations */
	TArray<FString> CustomOutputImplementations;

	/** Custom vertex interpolators */
	TArray<UMaterialExpressionVertexInterpolator*> CustomVertexInterpolators;
	/** Index to assign to next vertex interpolator. */
	int32 NextVertexInterpolatorIndex;
	/** Current float-width offset for custom vertex interpolators */
	int32 CurrentCustomVertexInterpolatorOffset;

	/** VT Stacks */
	TArray<FMaterialVTStackEntry> VTStacks;
	FHashTable VTStackHash;

	/** Used by interpolator pre-translation to hold potential errors until actually confirmed. */
	TArray<FString>* CompileErrorsSink;
	TArray<UMaterialExpression*>* CompileErrorExpressionsSink;

	/** Whether the translation succeeded. */
	uint32 bSuccess : 1;
	/** Whether the compute shader material inputs were compiled. */
	uint32 bCompileForComputeShader : 1;
	/** Whether the compiled material uses scene depth. */
	uint32 bUsesSceneDepth : 1;
	/** true if the material needs particle position. */
	uint32 bNeedsParticlePosition : 1;
	/** true if the material needs particle velocity. */
	uint32 bNeedsParticleVelocity : 1;
	/** true if the material needs particle relative time. */
	uint32 bNeedsParticleTime : 1;
	/** true if the material uses particle motion blur. */
	uint32 bUsesParticleMotionBlur : 1;
	/** true if the material needs particle random value. */
	uint32 bNeedsParticleRandom : 1;
	/** true if the material uses spherical particle opacity. */
	uint32 bUsesSphericalParticleOpacity : 1;
	/** true if the material uses particle sub uvs. */
	uint32 bUsesParticleSubUVs : 1;
	/** Boolean indicating using LightmapUvs */
	uint32 bUsesLightmapUVs : 1;
	/** Whether the material uses AO Material Mask */
	uint32 bUsesAOMaterialMask : 1;
	/** true if needs SpeedTree code */
	uint32 bUsesSpeedTree : 1;
	/** Boolean indicating the material uses worldspace position without shader offsets applied */
	uint32 bNeedsWorldPositionExcludingShaderOffsets : 1;
	/** true if the material needs particle size. */
	uint32 bNeedsParticleSize : 1;
	/** true if any scene texture expressions are reading from post process inputs */
	uint32 bNeedsSceneTexturePostProcessInputs : 1;
	/** true if any atmospheric fog expressions are used */
	uint32 bUsesAtmosphericFog : 1;
	/** true if any SkyAtmosphere expressions are used */
	uint32 bUsesSkyAtmosphere : 1;
	/** true if the material reads vertex color in the pixel shader. */
	uint32 bUsesVertexColor : 1;
	/** true if the material reads particle color in the pixel shader. */
	uint32 bUsesParticleColor : 1;
	/** true if the material reads mesh particle transform in the pixel shader. */
	uint32 bUsesParticleTransform : 1;

	/** true if the material uses any type of vertex position */
	uint32 bUsesVertexPosition : 1;

	uint32 bUsesTransformVector : 1;
	// True if the current property requires last frame's information
	uint32 bCompilingPreviousFrame : 1;
	/** True if material will output accurate velocities during base pass rendering. */
	uint32 bOutputsBasePassVelocities : 1;
	uint32 bUsesPixelDepthOffset : 1;
	uint32 bUsesWorldPositionOffset : 1;
	uint32 bUsesEmissiveColor : 1;
	uint32 bUsesDistanceCullFade : 1;
	/** true if the Roughness input evaluates to a constant 1.0 */
	uint32 bIsFullyRough : 1;
	/** true if allowed to generate code chunks. Translator operates in two phases; generate all code chunks & query meta data based on generated code chunks. */
	uint32 bAllowCodeChunkGeneration : 1;
	/** Tracks the texture coordinates used by this material. */
	TBitArray<> AllocatedUserTexCoords;
	/** Tracks the texture coordinates used by the vertex shader in this material. */
	TBitArray<> AllocatedUserVertexTexCoords;

	uint32 DynamicParticleParameterMask;

	/** Will contain all the shading models picked up from the material expression graph */
	FMaterialShadingModelField ShadingModelsFromCompilation;

	/** Tracks the total number of vt samples in the shader. */
	uint32 NumVtSamples;

	const ITargetPlatform* TargetPlatform;
public: 

	FHLSLMaterialTranslator(FMaterial* InMaterial,
		FMaterialCompilationOutput& InMaterialCompilationOutput,
		const FStaticParameterSet& InStaticParameters,
		EShaderPlatform InPlatform,
		EMaterialQualityLevel::Type InQualityLevel,
		ERHIFeatureLevel::Type InFeatureLevel,
		const ITargetPlatform* InTargetPlatform = nullptr);

	~FHLSLMaterialTranslator();

	int32 GetNumUserTexCoords() const;
	int32 GetNumUserVertexTexCoords() const;

	void ClearAllFunctionStacks();
	void ClearFunctionStack(uint32 Frequency);

	void AssignTempScope(TArray<FShaderCodeChunk>& InScope);
	void AssignShaderFrequencyScope(EShaderFrequency InShaderFrequency);

	void GatherCustomVertexInterpolators(TArray<UMaterialExpression*> Expressions);

	void CompileCustomOutputs(TArray<UMaterialExpressionCustomOutput*>& CustomOutputExpressions, TSet<UClass*>& SeenCustomOutputExpressionsClasses, bool bIsBeforeAttributes);

	EMaterialExpressionVisitResult VisitExpressionsRecursive(TArray<UMaterialExpression*> Expressions, IMaterialExpressionVisitor& InVisitor);
	EMaterialExpressionVisitResult VisitExpressionsForProperty(EMaterialProperty InProperty, IMaterialExpressionVisitor& InVisitor);

	void ValidateVtPropertyLimits();
 
	bool Translate();

	void GetMaterialEnvironment(EShaderPlatform InPlatform, FShaderCompilerEnvironment& OutEnvironment);
	
	// Assign custom interpolators to slots, packing them as much as possible in unused slots.
	TBitArray<> GetVertexInterpolatorsOffsets(FString& VertexInterpolatorsOffsetsDefinitionCode) const;

	void GetSharedInputsMaterialCode(FString& PixelMembersDeclaration, FString& NormalAssignment, FString& PixelMembersInitializationEpilog);

	FString GetMaterialShaderCode();

protected:

	bool IsMaterialPropertyUsed(EMaterialProperty Property, int32 PropertyChunkIndex, const FLinearColor& ReferenceValue, int32 NumComponents);

	// only used by GetMaterialShaderCode()
	// @param Index ECompiledMaterialProperty or EMaterialProperty
	FString GenerateFunctionCode(uint32 Index) const;

	// GetParameterCode
	virtual FString GetParameterCode(int32 Index, const TCHAR* Default = 0);

	uint64 GetParameterHash(int32 Index);

	/** Creates a string of all definitions needed for the given material input. */
	FString GetDefinitions(TArray<FShaderCodeChunk>& CodeChunks, int32 StartChunk, int32 EndChunk) const;

	// GetFixedParameterCode
	void GetFixedParameterCode(int32 StartChunk, int32 EndChunk, int32 ResultIndex, TArray<FShaderCodeChunk>& CodeChunks, FString& OutDefinitions, FString& OutValue);

	void GetFixedParameterCode(int32 ResultIndex, TArray<FShaderCodeChunk>& CodeChunks, FString& OutDefinitions, FString& OutValue);

	/** Used to get a user friendly type from EMaterialValueType */
	const TCHAR* DescribeType(EMaterialValueType Type) const;

	/** Used to get an HLSL type from EMaterialValueType */
	const TCHAR* HLSLTypeString(EMaterialValueType Type) const;

	int32 NonPixelShaderExpressionError();

	int32 ErrorUnlessFeatureLevelSupported(ERHIFeatureLevel::Type RequiredFeatureLevel);

	int32 NonVertexShaderExpressionError();
	int32 NonVertexOrPixelShaderExpressionError();

	void AddEstimatedTextureSample(const uint32 Count = 1);

	/** Creates a unique symbol name and adds it to the symbol list. */
	FString CreateSymbolName(const TCHAR* SymbolNameHint);

	/** Adds an already formatted inline or referenced code chunk */
	int32 AddCodeChunkInner(uint64 Hash, const TCHAR* FormattedCode, EMaterialValueType Type, bool bInlined);

	/** 
	 * Constructs the formatted code chunk and creates a new local variable definition from it. 
	 * This should be used over AddInlinedCodeChunk when the code chunk adds actual instructions, and especially when calling a function.
	 * Creating local variables instead of inlining simplifies the generated code and reduces redundant expression chains,
	 * Making compiles faster and enabling the shader optimizer to do a better job.
	 */
	int32 AddCodeChunk(EMaterialValueType Type, const TCHAR* Format, ...);
	int32 AddCodeChunkWithHash(uint64 BaseHash, EMaterialValueType Type, const TCHAR* Format, ...);

	/** 
	 * Constructs the formatted code chunk and creates an inlined code chunk from it. 
	 * This should be used instead of AddCodeChunk when the code chunk does not add any actual shader instructions, for example a component mask.
	 */
	int32 AddInlinedCodeChunk(EMaterialValueType Type, const TCHAR* Format, ...);
	int32 AddInlinedCodeChunkWithHash(uint64 BaseHash, EMaterialValueType Type, const TCHAR* Format, ...);

	int32 AddUniformExpressionInner(uint64 Hash, FMaterialUniformExpression* UniformExpression, EMaterialValueType Type, const TCHAR* FormattedCode);

	// AddUniformExpression - Adds an input to the Code array and returns its index.
	int32 AddUniformExpression(FMaterialUniformExpression* UniformExpression, EMaterialValueType Type, const TCHAR* Format, ...);
	int32 AddUniformExpressionWithHash(uint64 BaseHash, FMaterialUniformExpression* UniformExpression, EMaterialValueType Type, const TCHAR* Format, ...);

	// AccessUniformExpression - Adds code to access the value of a uniform expression to the Code array and returns its index.
	int32 AccessUniformExpression(int32 Index);

	// CoerceParameter
	FString CoerceParameter(int32 Index, EMaterialValueType DestType);

	// GetParameterType
	virtual EMaterialValueType GetParameterType(int32 Index) const override;

	// GetParameterUniformExpression
	virtual FMaterialUniformExpression* GetParameterUniformExpression(int32 Index) const override;

	virtual bool GetTextureForExpression(int32 Index, int32& OutTextureIndex, EMaterialSamplerType& OutSamplerType, TOptional<FName>& OutParameterName) const override;

	// GetArithmeticResultType
	EMaterialValueType GetArithmeticResultType(EMaterialValueType TypeA, EMaterialValueType TypeB);

	EMaterialValueType GetArithmeticResultType(int32 A, int32 B);

	// FMaterialCompiler interface.

	/** 
	 * Sets the current material property being compiled.  
	 * This affects the internal state of the compiler and the results of all functions except GetFixedParameterCode.
	 * @param OverrideShaderFrequency SF_NumFrequencies to not override
	 */
	virtual void SetMaterialProperty(EMaterialProperty InProperty, EShaderFrequency OverrideShaderFrequency = SF_NumFrequencies, bool bUsePreviousFrameTime = false) override;
	virtual void PushMaterialAttribute(const FGuid& InAttributeID) override;
	virtual FGuid PopMaterialAttribute() override;
	virtual const FGuid GetMaterialAttribute() override;
	virtual void SetBaseMaterialAttribute(const FGuid& InAttributeID) override;

	virtual void PushParameterOwner(const FMaterialParameterInfo& InOwnerInfo) override;
	virtual FMaterialParameterInfo PopParameterOwner() override;

	FORCEINLINE FMaterialParameterInfo GetParameterAssociationInfo()
	{
		check(ParameterOwnerStack.Num());
		return ParameterOwnerStack.Last();
	}

	virtual EShaderFrequency GetCurrentShaderFrequency() const override;

	virtual FMaterialShadingModelField GetMaterialShadingModels() const override;

	virtual int32 Error(const TCHAR* Text) override;

	virtual void AppendExpressionError(UMaterialExpression* Expression, const TCHAR* Text) override;

	virtual int32 CallExpression(FMaterialExpressionKey ExpressionKey, FMaterialCompiler* Compiler) override;

	virtual EMaterialValueType GetType(int32 Code) override;
	virtual EMaterialQualityLevel::Type GetQualityLevel() override;
	virtual ERHIFeatureLevel::Type GetFeatureLevel() override;
	virtual EShaderPlatform GetShaderPlatform() override;
	virtual const ITargetPlatform* GetTargetPlatform() const override;

	/** 
	 * Casts the passed in code to DestType, or generates a compile error if the cast is not valid. 
	 * This will truncate a type (float4 -> float3) but not add components (float2 -> float3), however a float1 can be cast to any float type by replication. 
	 */
	virtual int32 ValidCast(int32 Code, EMaterialValueType DestType) override;

	virtual int32 ForceCast(int32 Code, EMaterialValueType DestType, uint32 ForceCastFlags = 0) override;

	/** Pushes a function onto the compiler's function stack, which indicates that compilation is entering a function. */
	virtual void PushFunction(FMaterialFunctionCompileState* FunctionState) override;

	/** Pops a function from the compiler's function stack, which indicates that compilation is leaving a function. */
	virtual FMaterialFunctionCompileState* PopFunction() override;

	virtual int32 GetCurrentFunctionStackDepth() override;

	virtual int32 AccessCollectionParameter(UMaterialParameterCollection* ParameterCollection, int32 ParameterIndex, int32 ComponentIndex) override;

	virtual int32 ScalarParameter(FName ParameterName, float DefaultValue) override;

	virtual int32 VectorParameter(FName ParameterName, const FLinearColor& DefaultValue) override;

	virtual int32 Constant(float X) override;
	virtual int32 Constant2(float X, float Y) override;
	virtual int32 Constant3(float X, float Y, float Z) override;
	virtual int32 Constant4(float X, float Y, float Z, float W) override;
	
	virtual int32 ViewProperty(EMaterialExposedViewProperty Property, bool InvProperty) override;

	virtual int32 GameTime(bool bPeriodic, float Period) override;
	virtual int32 RealTime(bool bPeriodic, float Period) override;
	virtual int32 DeltaTime() override;

	virtual int32 PeriodicHint(int32 PeriodicCode) override;
	
	virtual int32 Sine(int32 X) override;
	virtual int32 Cosine(int32 X) override;
	virtual int32 Tangent(int32 X) override;
	virtual int32 Arcsine(int32 X) override;
	virtual int32 ArcsineFast(int32 X) override;
	virtual int32 Arccosine(int32 X) override;
	virtual int32 ArccosineFast(int32 X) override;
	virtual int32 Arctangent(int32 X) override;
	virtual int32 ArctangentFast(int32 X) override;
	virtual int32 Arctangent2(int32 Y, int32 X) override;
	virtual int32 Arctangent2Fast(int32 Y, int32 X) override;
	virtual int32 Floor(int32 X) override;
	virtual int32 Ceil(int32 X) override;
	virtual int32 Round(int32 X) override;
	virtual int32 Truncate(int32 X) override;
	virtual int32 Sign(int32 X) override;
	virtual int32 Frac(int32 X) override;
	virtual int32 Fmod(int32 A, int32 B) override;

	/**
	* Creates the new shader code chunk needed for the Abs expression
	*
	* @param	X - Index to the FMaterialCompiler::CodeChunk entry for the input expression
	* @return	Index to the new FMaterialCompiler::CodeChunk entry for this expression
	*/	
	virtual int32 Abs(int32 X) override;

	virtual int32 ReflectionVector() override;

	virtual int32 ReflectionAboutCustomWorldNormal(int32 CustomWorldNormal, int32 bNormalizeCustomWorldNormal) override;

	virtual int32 CameraVector() override;
	virtual int32 LightVector() override;

	virtual int32 GetViewportUV() override;

	virtual int32 GetPixelPosition() override;

	virtual int32 ParticleMacroUV() override;
	virtual int32 ParticleSubUV(int32 TextureIndex, EMaterialSamplerType SamplerType, bool bBlend) override;

	virtual int32 ParticleColor() override;
	virtual int32 ParticlePosition() override;
	virtual int32 ParticleRadius() override;

	virtual int32 SphericalParticleOpacity(int32 Density) override;

	virtual int32 ParticleRelativeTime() override;
	virtual int32 ParticleMotionBlurFade() override;
	virtual int32 ParticleRandom() override;
	virtual int32 ParticleDirection() override;
	virtual int32 ParticleSpeed() override;
	virtual int32 ParticleSize() override;

	virtual int32 WorldPosition(EWorldPositionIncludedOffsets WorldPositionIncludedOffsets) override;

	virtual int32 ObjectWorldPosition() override;
	virtual int32 ObjectRadius() override;
	virtual int32 ObjectBounds() override;

	virtual int32 PreSkinnedLocalBounds(int32 OutputIndex) override;

	virtual int32 DistanceCullFade() override;

	virtual int32 ActorWorldPosition() override;

	virtual int32 If(int32 A, int32 B, int32 AGreaterThanB, int32 AEqualsB, int32 ALessThanB, int32 ThresholdArg) override;

	void AllocateSlot(TBitArray<>& InBitArray, int32 InSlotIndex, int32 InSlotCount = 1) const;

#if WITH_EDITOR
	virtual int32 MaterialBakingWorldPosition() override;
#endif

	virtual int32 TextureCoordinate(uint32 CoordinateIndex, bool UnMirrorU, bool UnMirrorV) override;

	//static const TCHAR* GetVTAddressMode(TextureAddress Address);

	uint32 AcquireVTStackIndex(ETextureMipValueMode MipValueMode, TextureAddress AddressU, TextureAddress AddressV, float AspectRatio, int32 CoordinateIndex, int32 MipValue0Index, int32 MipValue1Index, int32 PreallocatedStackTextureIndex, bool bGenerateFeedback);

	virtual int32 TextureSample(
		int32 TextureIndex,
		int32 CoordinateIndex,
		EMaterialSamplerType SamplerType,
		int32 MipValue0Index = INDEX_NONE,
		int32 MipValue1Index = INDEX_NONE,
		ETextureMipValueMode MipValueMode = TMVM_None,
		ESamplerSourceMode SamplerSource = SSM_FromTextureAsset,
		int32 TextureReferenceIndex = INDEX_NONE,
		bool AutomaticViewMipBias = false
	) override;

	virtual int32 TextureProperty(int32 TextureIndex, EMaterialExposedTextureProperty Property) override;
	virtual int32 TextureDecalMipmapLevel(int32 TextureSizeInput) override;
	virtual int32 TextureDecalDerivative(bool bDDY) override;

	virtual int32 DecalLifetimeOpacity() override;

	virtual int32 PixelDepth() override;

	/** Calculate screen aligned UV coordinates from an offset fraction or texture coordinate */
	int32 GetScreenAlignedUV(int32 Offset, int32 ViewportUV, bool bUseOffset);

	virtual int32 SceneDepth(int32 Offset, int32 ViewportUV, bool bUseOffset) override;
	
	// @param SceneTextureId of type ESceneTextureId e.g. PPI_SubsurfaceColor
	virtual int32 SceneTextureLookup(int32 ViewportUV, uint32 InSceneTextureId, bool bFiltered) override;

	virtual int32 GetSceneTextureViewSize(int32 SceneTextureId, bool InvProperty) override;

	// @param bTextureLookup true: texture, false:no texture lookup, usually to get the size
	void UseSceneTextureId(ESceneTextureId SceneTextureId, bool bTextureLookup);

	virtual int32 SceneColor(int32 Offset, int32 ViewportUV, bool bUseOffset) override;

	virtual int32 Texture(UTexture* InTexture, int32& TextureReferenceIndex, EMaterialSamplerType SamplerType, ESamplerSourceMode SamplerSource = SSM_FromTextureAsset, ETextureMipValueMode MipValueMode = TMVM_None) override;
	virtual int32 TextureParameter(FName ParameterName, UTexture* DefaultValue, int32& TextureReferenceIndex, EMaterialSamplerType SamplerType, ESamplerSourceMode SamplerSource = SSM_FromTextureAsset) override;

	virtual int32 VirtualTexture(URuntimeVirtualTexture* InTexture, int32 TextureLayerIndex, int32 PageTableLayerIndex, int32& TextureReferenceIndex, EMaterialSamplerType SamplerType) override;
	virtual int32 VirtualTextureParameter(FName ParameterName, URuntimeVirtualTexture* DefaultValue, int32 TextureLayerIndex, int32 PageTableLayerIndex, int32& TextureReferenceIndex, EMaterialSamplerType SamplerType) override;
	virtual int32 VirtualTextureUniform(int32 TextureIndex, int32 VectorIndex) override;
	virtual int32 VirtualTextureUniform(FName ParameterName, int32 TextureIndex, int32 VectorIndex) override;
	virtual int32 VirtualTextureWorldToUV(int32 WorldPositionIndex, int32 P0, int32 P1, int32 P2) override;
	virtual int32 VirtualTextureUnpack(int32 CodeIndex0, int32 CodeIndex1, int32 CodeIndex2, EVirtualTextureUnpackType UnpackType) override;

	virtual int32 ExternalTexture(const FGuid& ExternalTextureGuid) override;
	virtual int32 ExternalTexture(UTexture* InTexture, int32& TextureReferenceIndex) override;
	virtual int32 ExternalTextureParameter(FName ParameterName, UTexture* DefaultValue, int32& TextureReferenceIndex) override;
	virtual int32 ExternalTextureCoordinateScaleRotation(int32 TextureReferenceIndex, TOptional<FName> ParameterName) override;
	virtual int32 ExternalTextureCoordinateScaleRotation(const FGuid& ExternalTextureGuid) override;
	virtual int32 ExternalTextureCoordinateOffset(int32 TextureReferenceIndex, TOptional<FName> ParameterName) override;
	virtual int32 ExternalTextureCoordinateOffset(const FGuid& ExternalTextureGuid) override;

	virtual UObject* GetReferencedTexture(int32 Index);

	virtual int32 StaticBool(bool bValue) override;
	virtual int32 StaticBoolParameter(FName ParameterName, bool bDefaultValue) override;
	virtual int32 StaticComponentMask(int32 Vector, FName ParameterName, bool bDefaultR, bool bDefaultG, bool bDefaultB, bool bDefaultA) override;
	virtual const FMaterialLayersFunctions* StaticMaterialLayersParameter(FName ParameterName) override;

	virtual bool GetStaticBoolValue(int32 BoolIndex, bool& bSucceeded) override;

	virtual int32 StaticTerrainLayerWeight(FName ParameterName, int32 Default) override;

	virtual int32 VertexColor() override;

	virtual int32 PreSkinnedPosition() override;
	virtual int32 PreSkinnedNormal() override;

	virtual int32 VertexInterpolator(uint32 InterpolatorIndex) override;

	virtual int32 Add(int32 A, int32 B) override;
	virtual int32 Sub(int32 A, int32 B) override;
	virtual int32 Mul(int32 A, int32 B) override;
	virtual int32 Div(int32 A, int32 B) override;
	virtual int32 Dot(int32 A, int32 B) override;
	virtual int32 Cross(int32 A, int32 B) override;
	virtual int32 Power(int32 Base, int32 Exponent) override;
	virtual int32 Logarithm2(int32 X) override;
	virtual int32 Logarithm10(int32 X) override;
	virtual int32 SquareRoot(int32 X) override;
	virtual int32 Length(int32 X) override;
	virtual int32 Lerp(int32 X, int32 Y, int32 A) override;
	virtual int32 Min(int32 A, int32 B) override;
	virtual int32 Max(int32 A, int32 B) override;
	virtual int32 Clamp(int32 X, int32 A, int32 B) override;
	virtual int32 Saturate(int32 X) override;
	virtual int32 ComponentMask(int32 Vector, bool R, bool G, bool B, bool A) override;
	virtual int32 AppendVector(int32 A, int32 B) override;

	int32 TransformBase(EMaterialCommonBasis SourceCoordBasis, EMaterialCommonBasis DestCoordBasis, int32 A, int AWComponent);
	
	virtual int32 TransformVector(EMaterialCommonBasis SourceCoordBasis, EMaterialCommonBasis DestCoordBasis, int32 A) override;
	virtual int32 TransformPosition(EMaterialCommonBasis SourceCoordBasis, EMaterialCommonBasis DestCoordBasis, int32 A) override;
	virtual int32 DynamicParameter(FLinearColor& DefaultValue, uint32 ParameterIndex = 0) override;
	virtual int32 LightmapUVs() override;
	virtual int32 PrecomputedAOMask() override;
	virtual int32 LightmassReplace(int32 Realtime, int32 Lightmass) override;
	virtual int32 GIReplace(int32 Direct, int32 StaticIndirect, int32 DynamicIndirect) override;
	virtual int32 ShadowReplace(int32 Default, int32 Shadow) override;

	virtual int32 RayTracingQualitySwitchReplace(int32 Normal, int32 RayTraced);

	virtual int32 MaterialProxyReplace(int32 Realtime, int32 MaterialProxy) override;

	virtual int32 VirtualTextureOutputReplace(int32 Default, int32 VirtualTexture) override;

	virtual int32 ObjectOrientation() override;

	virtual int32 RotateAboutAxis(int32 NormalizedRotationAxisAndAngleIndex, int32 PositionOnAxisIndex, int32 PositionIndex) override;

	virtual int32 TwoSidedSign() override;
	virtual int32 VertexNormal() override;
	virtual int32 PixelNormalWS() override;
	virtual int32 DDX(int32 X) override;
	virtual int32 DDY(int32 X) override;

	virtual int32 AntialiasedTextureMask(int32 Tex, int32 UV, float Threshold, uint8 Channel) override;
	virtual int32 DepthOfFieldFunction(int32 Depth, int32 FunctionValueIndex) override;
	virtual int32 Sobol(int32 Cell, int32 Index, int32 Seed) override;
	virtual int32 TemporalSobol(int32 Index, int32 Seed) override;
	virtual int32 Noise(int32 Position, float Scale, int32 Quality, uint8 NoiseFunction, bool bTurbulence, int32 Levels, float OutputMin, float OutputMax, float LevelScale, int32 FilterWidth, bool bTiling, uint32 RepeatSize) override;
	virtual int32 VectorNoise(int32 Position, int32 Quality, uint8 NoiseFunction, bool bTiling, uint32 TileSize) override;

	virtual int32 BlackBody(int32 Temp) override;
	virtual int32 DistanceToNearestSurface(int32 PositionArg) override;
	virtual int32 DistanceFieldGradient(int32 PositionArg) override;
	virtual int32 AtmosphericFogColor(int32 WorldPosition) override;
	virtual int32 AtmosphericLightVector() override;
	virtual int32 AtmosphericLightColor() override;

	virtual int32 SkyAtmosphereLightIlluminance(int32 WorldPosition, int32 LightIndex) override;
	virtual int32 SkyAtmosphereLightDirection(int32 LightIndex) override;
	virtual int32 SkyAtmosphereLightDiskLuminance(int32 LightIndex) override;
	virtual int32 SkyAtmosphereViewLuminance() override;
	virtual int32 SkyAtmosphereAerialPerspective(int32 WorldPosition) override;
	virtual int32 SkyAtmosphereDistantLightScatteredLuminance() override;

	virtual int32 CustomPrimitiveData(int32 OutputIndex, EMaterialValueType Type) override;

	virtual int32 ShadingModel(EMaterialShadingModel InSelectedShadingModel) override;

	virtual int32 MapARPassthroughCameraUV(int32 UV) override;

	virtual int32 CustomExpression(class UMaterialExpressionCustom* Custom, TArray<int32>& CompiledInputs) override;
	virtual int32 CustomOutput(class UMaterialExpressionCustomOutput* Custom, int32 OutputIndex, int32 OutputCode) override;

	virtual int32 VirtualTextureOutput() override;

#if HANDLE_CUSTOM_OUTPUTS_AS_MATERIAL_ATTRIBUTES
	/** Used to translate code for custom output attributes such as ClearCoatBottomNormal */
	void GenerateCustomAttributeCode(int32 OutputIndex, int32 OutputCode, EMaterialValueType OutputType, FString& DisplayName);
#endif

	/**
	 * Adds code to return a random value shared by all geometry for any given instanced static mesh
	 *
	 * @return	Code index
	 */
	virtual int32 PerInstanceRandom() override;

	/**
	 * Returns a mask that either enables or disables selection on a per-instance basis when instancing
	 *
	 * @return	Code index
	 */
	virtual int32 PerInstanceFadeAmount() override;

	/**
	 * Returns a float2 texture coordinate after 2x2 transform and offset applied
	 *
	 * @return	Code index
	 */
	virtual int32 RotateScaleOffsetTexCoords(int32 TexCoordCodeIndex, int32 RotationScale, int32 Offset) override;

	/**
	* Handles SpeedTree vertex animation (wind, smooth LOD)
	*
	* @return	Code index
	*/
	virtual int32 SpeedTree(int32 GeometryArg, int32 WindArg, int32 LODArg, float BillboardThreshold, bool bAccurateWindVelocities, bool bExtraBend, int32 ExtraBendArg) override;

	/**
	 * Adds code for texture coordinate offset to localize large UV
	 *
	 * @return	Code index
	 */
	virtual int32 TextureCoordinateOffset() override;

	/**Experimental access to the EyeAdaptation RT for Post Process materials. Can be one frame behind depending on the value of BlendableLocation. */
	virtual int32 EyeAdaptation() override;

	// to only have one piece of code dealing with error handling if the Primitive constant buffer is not used.
	// @param Name e.g. TEXT("ObjectWorldPositionAndRadius.w")
	int32 GetPrimitiveProperty(EMaterialValueType Type, const TCHAR* ExpressionName, const TCHAR* HLSLName);

	// The compiler can run in a different state and this affects caching of sub expression, Expressions are different (e.g. View.PrevWorldViewOrigin) when using previous frame's values
	virtual bool IsCurrentlyCompilingForPreviousFrame() const;

	virtual bool IsDevelopmentFeatureEnabled(const FName& FeatureName) const override;
};

#endif // WITH_EDITORONLY_DATA
