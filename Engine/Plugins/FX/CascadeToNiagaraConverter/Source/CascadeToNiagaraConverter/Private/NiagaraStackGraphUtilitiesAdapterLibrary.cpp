// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraStackGraphUtilitiesAdapterLibrary.h"
#include "AssetRegistryModule.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraEditorUtilities.h"
#include "Particles/ParticleSystem.h"
#include "Particles/Acceleration/ParticleModuleAcceleration.h"
#include "Particles/Acceleration/ParticleModuleAccelerationDrag.h"
#include "Particles/Collision/ParticleModuleCollision.h"
#include "Particles/Color/ParticleModuleColor.h"
#include "Particles/Color/ParticleModuleColorOverLife.h"
#include "Particles/Color/ParticleModuleColorScaleOverLife.h"
#include "Particles/Lifetime/ParticleModuleLifetime.h"
#include "Particles/Location/ParticleModuleLocationPrimitiveSphere.h"
#include "Particles/Rotation/ParticleModuleRotation.h"
#include "Particles/Rotation/ParticleModuleMeshRotation.h"
#include "Particles/RotationRate/ParticleModuleRotationRate.h"
#include "Particles/Size/ParticleModuleSize.h"
#include "Particles/Size/ParticleModuleSizeScaleBySpeed.h"
#include "Particles/Size/ParticleModuleSizeMultiplyLife.h"
#include "Particles/Spawn/ParticleModuleSpawn.h"
#include "Particles/SubUV/ParticleModuleSubUV.h"
#include "Particles/SubUV/ParticleModuleSubUVMovie.h"
#include "Particles/VectorField/ParticleModuleVectorFieldLocal.h"
#include "Particles/VectorField/ParticleModuleVectorFieldRotationRate.h"
#include "Particles/Velocity/ParticleModuleVelocity.h"
#include "Particles/Acceleration/ParticleModuleAccelerationConstant.h"
#include "Particles/TypeData/ParticleModuleTypeDataBase.h"
#include "Particles/TypeData/ParticleModuleTypeDataGpu.h"
#include "Particles/TypeData/ParticleModuleTypeDataMesh.h"
#include "Particles/TypeData/ParticleModuleTypeDataRibbon.h"

#include "Particles/ParticleLODLevel.h"
#include "NiagaraScriptSource.h"
#include "NiagaraClipboard.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "NiagaraEditorModule.h"
#include "ViewModels/NiagaraEmitterHandleViewModel.h"
#include "ViewModels/Stack/NiagaraStackClipboardUtilities.h"
#include "ViewModels/Stack/NiagaraStackViewModel.h"
#include "NiagaraEmitterFactoryNew.h"
#include "ViewModels/Stack/NiagaraStackItemGroup.h"
#include "NiagaraRibbonRendererProperties.h"
#include "NiagaraMeshRendererProperties.h"
#include "NiagaraDataInterfaceCurve.h"
#include "NiagaraDataInterfaceVector2DCurve.h"
#include "NiagaraDataInterfaceVectorCurve.h"
#include "NiagaraDataInterfaceVector4Curve.h"
#include "NiagaraMessages.h"
#include "NiagaraTypes.h"
#include "Math/InterpCurvePoint.h"

#include "Distributions/DistributionFloatConstant.h"
#include "Distributions/DistributionFloatConstantCurve.h"
#include "Distributions/DistributionFloatUniform.h"
#include "Distributions/DistributionFloatUniformCurve.h"
#include "Distributions/DistributionFloatParticleParameter.h"

#include "Distributions/DistributionVectorConstant.h"
#include "Distributions/DistributionVectorConstantCurve.h"
#include "Distributions/DistributionVectorUniform.h"
#include "Distributions/DistributionVectorUniformCurve.h"
#include "Distributions/DistributionVectorParticleParameter.h"
#include "MessageLogModule.h"
#include "IMessageLogListing.h"
#include "EdGraph/EdGraphNode.h"
#include "NiagaraNode.h"
#include "NiagaraNodeFunctionCall.h"
#include "CascadeToNiagaraConverterModule.h"
#include "Curves/RichCurve.h"


TMap<FGuid, TSharedPtr<FNiagaraEmitterHandleViewModel>> UFXConverterUtilitiesLibrary::GuidToNiagaraEmitterHandleViewModelMap;
TMap<FGuid, TSharedPtr<FNiagaraSystemViewModel>> UFXConverterUtilitiesLibrary::GuidToNiagaraSystemViewModelMap;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////	UFXConverterUtilitiesLibrary																			  /////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void UFXConverterUtilitiesLibrary::Cleanup()
{
	GuidToNiagaraEmitterHandleViewModelMap.Empty();
	GuidToNiagaraSystemViewModelMap.Empty();
}

FName UFXConverterUtilitiesLibrary::GetNiagaraScriptInputTypeName(ENiagaraScriptInputType InputType)
{
	switch (InputType) {
	case ENiagaraScriptInputType::Int:
		return FName("NiagaraInt32");
	case ENiagaraScriptInputType::Float:
		return FName("NiagaraFloat");
	case ENiagaraScriptInputType::Vec2:
		return FName("Vector2D");
	case ENiagaraScriptInputType::Vec3:
		return FName("Vector");
	case ENiagaraScriptInputType::Vec4:
		return FName("Vector4");
	case ENiagaraScriptInputType::LinearColor:
		return FName("LinearColor");
	case ENiagaraScriptInputType::Quaternion:
		return FName("Quat");
	};
	checkf(false, TEXT("Tried to get FName for unknown ENiagaraScriptInputType!"));
	return FName();
}

TArray<UParticleEmitter*> UFXConverterUtilitiesLibrary::GetCascadeSystemEmitters(const UParticleSystem* System)
{
	return System->Emitters;
}

UParticleLODLevel* UFXConverterUtilitiesLibrary::GetCascadeEmitterLodLevel(UParticleEmitter* Emitter, const int32 Idx)
{
	return Emitter->GetLODLevel(Idx);
}

bool UFXConverterUtilitiesLibrary::GetLodLevelIsEnabled(UParticleLODLevel* LodLevel)
{
	return LodLevel->bEnabled;
}

TArray<UParticleModule*> UFXConverterUtilitiesLibrary::GetLodLevelModules(UParticleLODLevel* LodLevel)
{
	return LodLevel->Modules;
}

UParticleModuleSpawn* UFXConverterUtilitiesLibrary::GetLodLevelSpawnModule(UParticleLODLevel* LodLevel)
{
	return LodLevel->SpawnModule;
}

UParticleModuleRequired* UFXConverterUtilitiesLibrary::GetLodLevelRequiredModule(UParticleLODLevel* LodLevel)
	{
	return LodLevel->RequiredModule;
	}

UParticleModuleTypeDataBase* UFXConverterUtilitiesLibrary::GetLodLevelTypeDataModule(UParticleLODLevel* LodLevel)
{
	return LodLevel->TypeDataModule;
}

FName UFXConverterUtilitiesLibrary::GetCascadeEmitterName(UParticleEmitter* Emitter)
{
	return Emitter->GetEmitterName();
}

UNiagaraScriptConversionContext* UFXConverterUtilitiesLibrary::CreateScriptContext(FAssetData NiagaraScriptAssetData)
{
	UNiagaraScriptConversionContext* ScriptContext = NewObject<UNiagaraScriptConversionContext>();
	ScriptContext->Init(NiagaraScriptAssetData);
	return ScriptContext;
}

UNiagaraScriptConversionContextInput* UFXConverterUtilitiesLibrary::CreateScriptInputLinkedParameter(FString ParameterNameString, ENiagaraScriptInputType InputType)
{
	const FName InputTypeName = GetNiagaraScriptInputTypeName(InputType);
	UNiagaraClipboardFunctionInput* NewInput = UNiagaraClipboardEditorScriptingUtilities::CreateLinkedValueInput(GetTransientPackage(), FName(), InputTypeName, false, false, FName(ParameterNameString));
	const FNiagaraTypeDefinition& TargetTypeDef = UNiagaraClipboardEditorScriptingUtilities::GetRegisteredTypeDefinitionByName(InputTypeName);
	UNiagaraScriptConversionContextInput* Input = NewObject<UNiagaraScriptConversionContextInput>();
	Input->Init(NewInput, InputType, TargetTypeDef); 
	return Input;
}

UNiagaraScriptConversionContextInput* UFXConverterUtilitiesLibrary::CreateScriptInputFloat(float Value)
{
	UNiagaraClipboardFunctionInput* NewInput = UNiagaraClipboardEditorScriptingUtilities::CreateFloatLocalValueInput(GetTransientPackage(), FName(), false, false, Value);
	const FNiagaraTypeDefinition& TargetTypeDef = FNiagaraTypeDefinition::GetFloatDef();
	UNiagaraScriptConversionContextInput* Input = NewObject<UNiagaraScriptConversionContextInput>();
	Input->Init(NewInput, ENiagaraScriptInputType::Float, TargetTypeDef);
	return Input;
}

UNiagaraScriptConversionContextInput* UFXConverterUtilitiesLibrary::CreateScriptInputVec2(FVector2D Value)
{
	UNiagaraClipboardFunctionInput* NewInput = UNiagaraClipboardEditorScriptingUtilities::CreateVec2LocalValueInput(GetTransientPackage(), FName(), false, false, Value);
	const FNiagaraTypeDefinition& TargetTypeDef = FNiagaraTypeDefinition::GetVec2Def();
	UNiagaraScriptConversionContextInput* Input = NewObject<UNiagaraScriptConversionContextInput>();
	Input->Init(NewInput, ENiagaraScriptInputType::Vec2, TargetTypeDef);
	return Input;
}

UNiagaraScriptConversionContextInput* UFXConverterUtilitiesLibrary::CreateScriptInputVector(FVector Value)
{
	UNiagaraClipboardFunctionInput* NewInput = UNiagaraClipboardEditorScriptingUtilities::CreateVec3LocalValueInput(GetTransientPackage(), FName(), false, false, Value);
	const FNiagaraTypeDefinition& TargetTypeDef = FNiagaraTypeDefinition::GetVec3Def();
	UNiagaraScriptConversionContextInput* Input = NewObject<UNiagaraScriptConversionContextInput>();
	Input->Init(NewInput, ENiagaraScriptInputType::Vec3, TargetTypeDef);
	return Input;
}

UNiagaraScriptConversionContextInput* UFXConverterUtilitiesLibrary::CreateScriptInputStruct(UUserDefinedStruct* Value)
{
	UNiagaraClipboardFunctionInput* NewInput = UNiagaraClipboardEditorScriptingUtilities::CreateStructLocalValueInput(GetTransientPackage(), FName(), false, false, Value);
	if (NewInput != nullptr)
	{
		UNiagaraScriptConversionContextInput* Input = NewObject<UNiagaraScriptConversionContextInput>();
		Input->Init(NewInput, ENiagaraScriptInputType::Struct, NewInput->GetTypeDef());
		return Input;
	}
	return nullptr;
}

UNiagaraScriptConversionContextInput* UFXConverterUtilitiesLibrary::CreateScriptInputEnum(UUserDefinedEnum* Value)
{
	UNiagaraClipboardFunctionInput* NewInput = UNiagaraClipboardEditorScriptingUtilities::CreateEnumLocalValueInput(GetTransientPackage(), FName(), false, false, Value);
	if (NewInput != nullptr)
	{
		UNiagaraScriptConversionContextInput* Input = NewObject<UNiagaraScriptConversionContextInput>();
		Input->Init(NewInput, ENiagaraScriptInputType::Enum, NewInput->GetTypeDef());
		return Input;
	}
	return nullptr;
}

UNiagaraScriptConversionContextInput* UFXConverterUtilitiesLibrary::CreateScriptInputInt(int32 Value)
{
	UNiagaraClipboardFunctionInput* NewInput = UNiagaraClipboardEditorScriptingUtilities::CreateIntLocalValueInput(GetTransientPackage(), FName(), false, false, Value);
	const FNiagaraTypeDefinition& TargetTypeDef = FNiagaraTypeDefinition::GetIntDef();
	UNiagaraScriptConversionContextInput* Input = NewObject<UNiagaraScriptConversionContextInput>();
	Input->Init(NewInput, ENiagaraScriptInputType::Int, TargetTypeDef);
	return Input;
}

UNiagaraScriptConversionContextInput* UFXConverterUtilitiesLibrary::CreateScriptInputDynamic(UNiagaraScriptConversionContext* DynamicInputScriptContext, ENiagaraScriptInputType InputType)
{
	const FName InputTypeName = GetNiagaraScriptInputTypeName(InputType);
	UNiagaraClipboardFunctionInput* NewInput = UNiagaraClipboardEditorScriptingUtilities::CreateDynamicValueInput(
		GetTransientPackage()
		, FName()
		, InputTypeName
		, false
		, false
		, FString()
		, DynamicInputScriptContext->GetScript());
	
	// copy over the original function inputs to the new dynamic input script associated with this clipboard function input
	NewInput->Dynamic->Inputs = DynamicInputScriptContext->GetClipboardFunctionInputs();
	const FNiagaraTypeDefinition& TargetTypeDef = UNiagaraClipboardEditorScriptingUtilities::GetRegisteredTypeDefinitionByName(InputTypeName);
	UNiagaraScriptConversionContextInput* Input = NewObject<UNiagaraScriptConversionContextInput>();
	Input->Init(NewInput, InputType, TargetTypeDef);
	Input->StackMessages = DynamicInputScriptContext->GetStackMessages();
	return Input;
}

UNiagaraScriptConversionContextInput* UFXConverterUtilitiesLibrary::CreateScriptInputDI(UNiagaraDataInterface* Value)
{
	UNiagaraClipboardFunctionInput* NewInput = UNiagaraClipboardEditorScriptingUtilities::CreateDataValueInput(
		GetTransientPackage()
		, FName()
		, false
		, false
		, Value);

	if (NewInput != nullptr)
	{
		UNiagaraScriptConversionContextInput* Input = NewObject<UNiagaraScriptConversionContextInput>();
		Input->Init(NewInput, ENiagaraScriptInputType::DataInterface, NewInput->GetTypeDef());
		return Input;
	}
	return nullptr;
}

UNiagaraRibbonRendererProperties* UFXConverterUtilitiesLibrary::CreateRibbonRendererProperties()
{
	return NewObject<UNiagaraRibbonRendererProperties>();
}

UNiagaraMeshRendererProperties* UFXConverterUtilitiesLibrary::CreateMeshRendererProperties()
{
	return NewObject<UNiagaraMeshRendererProperties>();
}

UNiagaraDataInterfaceCurve* UFXConverterUtilitiesLibrary::CreateFloatCurveDI(TArray<FRichCurveKeyBP> Keys)
{
	UNiagaraDataInterfaceCurve* DI_Curve = NewObject<UNiagaraDataInterfaceCurve>();
	const TArray<FRichCurveKey> BaseKeys = FRichCurveKeyBP::KeysToBase(Keys);
	DI_Curve->Curve.SetKeys(BaseKeys);
	return DI_Curve;
}

UNiagaraDataInterfaceVector2DCurve* UFXConverterUtilitiesLibrary::CreateVec2CurveDI(TArray<FRichCurveKeyBP> X_Keys, TArray<FRichCurveKeyBP> Y_Keys)
{
	UNiagaraDataInterfaceVector2DCurve* DI_Curve = NewObject<UNiagaraDataInterfaceVector2DCurve>();
	const TArray<FRichCurveKey> X_BaseKeys = FRichCurveKeyBP::KeysToBase(X_Keys);
	const TArray<FRichCurveKey> Y_BaseKeys = FRichCurveKeyBP::KeysToBase(Y_Keys);
	DI_Curve->XCurve.SetKeys(X_BaseKeys);
	DI_Curve->YCurve.SetKeys(Y_BaseKeys);
	return DI_Curve;
}

UNiagaraDataInterfaceVectorCurve* UFXConverterUtilitiesLibrary::CreateVec3CurveDI(
	TArray<FRichCurveKeyBP> X_Keys,
	TArray<FRichCurveKeyBP> Y_Keys,
	TArray<FRichCurveKeyBP> Z_Keys
	)
{
	UNiagaraDataInterfaceVectorCurve* DI_Curve = NewObject<UNiagaraDataInterfaceVectorCurve>();
	const TArray<FRichCurveKey> X_BaseKeys = FRichCurveKeyBP::KeysToBase(X_Keys);
	const TArray<FRichCurveKey> Y_BaseKeys = FRichCurveKeyBP::KeysToBase(Y_Keys);
	const TArray<FRichCurveKey> Z_BaseKeys = FRichCurveKeyBP::KeysToBase(Z_Keys);
	DI_Curve->XCurve.SetKeys(X_BaseKeys);
	DI_Curve->YCurve.SetKeys(Y_BaseKeys);
	DI_Curve->ZCurve.SetKeys(Z_BaseKeys);
	return DI_Curve;
}

UNiagaraDataInterfaceVector4Curve* UFXConverterUtilitiesLibrary::CreateVec4CurveDI(
	TArray<FRichCurveKeyBP> X_Keys,
	TArray<FRichCurveKeyBP> Y_Keys,
	TArray<FRichCurveKeyBP> Z_Keys,
	TArray<FRichCurveKeyBP> W_Keys
	)
{
	UNiagaraDataInterfaceVector4Curve* DI_Curve = NewObject<UNiagaraDataInterfaceVector4Curve>();
	const TArray<FRichCurveKey> X_BaseKeys = FRichCurveKeyBP::KeysToBase(X_Keys);
	const TArray<FRichCurveKey> Y_BaseKeys = FRichCurveKeyBP::KeysToBase(Y_Keys);
	const TArray<FRichCurveKey> Z_BaseKeys = FRichCurveKeyBP::KeysToBase(Z_Keys);
	const TArray<FRichCurveKey> W_BaseKeys = FRichCurveKeyBP::KeysToBase(W_Keys);
	DI_Curve->XCurve.SetKeys(X_BaseKeys);
	DI_Curve->YCurve.SetKeys(Y_BaseKeys);
	DI_Curve->ZCurve.SetKeys(Z_BaseKeys);
	DI_Curve->WCurve.SetKeys(W_BaseKeys);
	return DI_Curve;
}

UNiagaraSystemConversionContext* UFXConverterUtilitiesLibrary::CreateSystemConversionContext(UNiagaraSystem* InSystem)
{
	TSharedPtr<FNiagaraSystemViewModel> SystemViewModel = MakeShared<FNiagaraSystemViewModel>();
	FNiagaraSystemViewModelOptions SystemViewModelOptions = FNiagaraSystemViewModelOptions();
	SystemViewModelOptions.bCanAutoCompile = false;
	SystemViewModelOptions.bCanSimulate = false;
	SystemViewModelOptions.EditMode = ENiagaraSystemViewModelEditMode::SystemAsset;
	SystemViewModelOptions.MessageLogGuid = InSystem->GetAssetGuid();
	SystemViewModel->Initialize(*InSystem, SystemViewModelOptions);
	FGuid SystemViewModelGuid = FGuid::NewGuid();
	GuidToNiagaraSystemViewModelMap.Add(SystemViewModelGuid, SystemViewModel);
	UNiagaraSystemConversionContext* SystemConversionContext = NewObject<UNiagaraSystemConversionContext>();
	SystemConversionContext->Init(InSystem, SystemViewModelGuid);
	return SystemConversionContext;
}

void UFXConverterUtilitiesLibrary::GetParticleModuleTypeDataGpuProps(UParticleModuleTypeDataGpu* ParticleModule)
{
	// empty impl, method arg taking UParticleModuleTypeDataGpu exposes this UObject type to python scripting reflection
	return;
}

void UFXConverterUtilitiesLibrary::GetParticleModuleTypeDataMeshProps(
	UParticleModuleTypeDataMesh* ParticleModule
	, UStaticMesh*& OutMesh
	, float& OutLODSizeScale
	, bool& bOutUseStaticMeshLODs
	, bool& bOutCastShadows
	, bool& bOutDoCollisions
	, TEnumAsByte<EMeshScreenAlignment>& OutMeshAlignment
	, bool& bOutOverrideMaterial
	, bool& bOutOverrideDefaultMotionBlurSettings
	, bool& bOutEnableMotionBlur
	, UDistribution*& OutRollPitchYawRange
	, TEnumAsByte<EParticleAxisLock>& OutAxisLockOption
	, bool& bOutCameraFacing
	, TEnumAsByte<EMeshCameraFacingUpAxis>& OutCameraFacingUpAxisOption_DEPRECATED
	, TEnumAsByte<EMeshCameraFacingOptions>& OutCameraFacingOption
	, bool& bOutApplyParticleRotationAsSpin
	, bool& bOutFacingCameraDirectionRatherThanPosition
	, bool& bOutCollisionsConsiderParticleSize)
{
	OutMesh = ParticleModule->Mesh;
	OutLODSizeScale = ParticleModule->LODSizeScale;
	bOutUseStaticMeshLODs = ParticleModule->bUseStaticMeshLODs;
	bOutCastShadows = ParticleModule->CastShadows;
	bOutDoCollisions = ParticleModule->DoCollisions;
	OutMeshAlignment = ParticleModule->MeshAlignment;
	bOutOverrideMaterial = ParticleModule->bOverrideMaterial;
	bOutOverrideDefaultMotionBlurSettings = ParticleModule->bOverrideDefaultMotionBlurSettings;
	bOutEnableMotionBlur = ParticleModule->bEnableMotionBlur;
	OutRollPitchYawRange = ParticleModule->RollPitchYawRange.Distribution;
	OutAxisLockOption = ParticleModule->AxisLockOption;
	bOutCameraFacing = ParticleModule->bCameraFacing;
	OutCameraFacingUpAxisOption_DEPRECATED = ParticleModule->CameraFacingUpAxisOption_DEPRECATED;
	OutCameraFacingOption = ParticleModule->CameraFacingOption;
	bOutApplyParticleRotationAsSpin = ParticleModule->bApplyParticleRotationAsSpin;
	bOutFacingCameraDirectionRatherThanPosition = ParticleModule->bFaceCameraDirectionRatherThanPosition;
	bOutCollisionsConsiderParticleSize = ParticleModule->bCollisionsConsiderPartilceSize;
}

UClass* UFXConverterUtilitiesLibrary::GetParticleModuleTypeDataRibbonClass()
{
	return UParticleModuleTypeDataRibbon::StaticClass();
}

void UFXConverterUtilitiesLibrary::GetParticleModuleTypeDataRibbonProps(
	UParticleModuleTypeDataRibbon* ParticleModule
	, int32& OutMaxTessellationBetweenParticles
	, int32& OutSheetsPerTrail
	, int32& OutMaxTrailCount
	, int32& OutMaxParticleInTrailCount
	, bool& bOutDeadTrailsOnDeactivate
	, bool& bOutClipSourceSegment
	, bool& bOutEnablePreviousTangentRecalculation
	, bool& bOutTangentRecalculationEveryFrame
	, bool& bOutSpawnInitialParticle
	, TEnumAsByte<ETrailsRenderAxisOption>& OutRenderAxis
	, float& OutTangentSpawningScalar
	, bool& bOutRenderGeometry
	, bool& bOutRenderSpawnPoints
	, bool& bOutRenderTangents
	, bool& bOutRenderTessellation
	, float& OutTilingDistance
	, float& OutDistanceTessellationStepSize
	, bool& bOutEnableTangentDiffInterpScale
	, float& OutTangentTessellationScalar)
{
	OutMaxTessellationBetweenParticles = ParticleModule->MaxTessellationBetweenParticles;
	OutSheetsPerTrail = ParticleModule->SheetsPerTrail;
	OutMaxTrailCount = ParticleModule->MaxTrailCount;
	OutMaxParticleInTrailCount = ParticleModule->MaxParticleInTrailCount;
	bOutDeadTrailsOnDeactivate = ParticleModule->bDeadTrailsOnDeactivate;
	bOutClipSourceSegment = ParticleModule->bClipSourceSegement;
	bOutEnablePreviousTangentRecalculation = ParticleModule->bEnablePreviousTangentRecalculation;
	bOutTangentRecalculationEveryFrame = ParticleModule->bTangentRecalculationEveryFrame;
	bOutSpawnInitialParticle = ParticleModule->bSpawnInitialParticle;
	OutRenderAxis = ParticleModule->RenderAxis;
	OutTangentSpawningScalar = ParticleModule->TangentSpawningScalar;
	bOutRenderGeometry = ParticleModule->bRenderGeometry;
	bOutRenderSpawnPoints = ParticleModule->bRenderSpawnPoints;
	bOutRenderTangents = ParticleModule->bRenderTangents;
	bOutRenderTessellation = ParticleModule->bRenderTessellation;
	OutTilingDistance = ParticleModule->TilingDistance;
	OutDistanceTessellationStepSize = ParticleModule->DistanceTessellationStepSize;
	bOutEnableTangentDiffInterpScale = ParticleModule->bEnableTangentDiffInterpScale;
	OutTangentTessellationScalar = ParticleModule->TangentTessellationScalar;
}

void UFXConverterUtilitiesLibrary::GetParticleModuleSpawnProps(
	  UParticleModuleSpawn* ParticleModuleSpawn
	, UDistribution*& OutRate
	, UDistribution*& OutRateScale
	, TEnumAsByte<EParticleBurstMethod>& OutBurstMethod
	, TArray<FParticleBurstBlueprint>& OutBurstList
	, UDistribution*& OutBurstScale
	, bool& bOutApplyGlobalSpawnRateScale
	, bool& bOutProcessSpawnRate
	, bool& bOutProcessSpawnBurst)
{
	OutRate = ParticleModuleSpawn->Rate.Distribution;
	OutRateScale = ParticleModuleSpawn->RateScale.Distribution;
	OutBurstMethod = ParticleModuleSpawn->ParticleBurstMethod;
	OutBurstList = TArray<FParticleBurstBlueprint>(ParticleModuleSpawn->BurstList);
	OutBurstScale = ParticleModuleSpawn->BurstScale.Distribution;
	bOutApplyGlobalSpawnRateScale = ParticleModuleSpawn->bApplyGlobalSpawnRateScale;
	bOutProcessSpawnRate = ParticleModuleSpawn->bProcessSpawnRate;
	bOutProcessSpawnBurst = ParticleModuleSpawn->bProcessBurstList;
}

void UFXConverterUtilitiesLibrary::GetParticleModuleRequiredProps(
	UParticleModuleRequired* ParticleModuleRequired
	, UMaterialInterface*& OutMaterialInterface
	, TEnumAsByte<EParticleScreenAlignment>& OutScreenAlignment
	, bool& bOutUseLocalSpace
	, int32& OutSubImages_Horizontal
	, int32& OutSubImages_Vertical
	, TEnumAsByte<EParticleSortMode>& OutSortMode
	, TEnumAsByte<EParticleSubUVInterpMethod>& OutInterpolationMethod
	, uint8& bOutRemoveHMDRoll
	, float& OutMinFacingCameraBlendDistance
	, float& OutMaxFacingCameraBlendDistance
	, UTexture2D*& OutCutoutTexture
	, TEnumAsByte<ESubUVBoundingVertexCount>& OutBoundingMode
	, TEnumAsByte<EOpacitySourceMode>& OutOpacitySourceMode
	, float& OutAlphaThreshold)
{
	OutMaterialInterface = ParticleModuleRequired->Material;
	OutScreenAlignment = ParticleModuleRequired->ScreenAlignment;
	bOutUseLocalSpace = ParticleModuleRequired->bUseLocalSpace;
	OutSubImages_Horizontal = ParticleModuleRequired->SubImages_Horizontal;
	OutSubImages_Vertical = ParticleModuleRequired->SubImages_Vertical;
	OutSortMode = ParticleModuleRequired->SortMode;
	OutInterpolationMethod = ParticleModuleRequired->InterpolationMethod;
	bOutRemoveHMDRoll = ParticleModuleRequired->bRemoveHMDRoll;
	OutMinFacingCameraBlendDistance = ParticleModuleRequired->MinFacingCameraBlendDistance;
	OutMaxFacingCameraBlendDistance = ParticleModuleRequired->MaxFacingCameraBlendDistance;
	OutCutoutTexture = ParticleModuleRequired->CutoutTexture;
	OutBoundingMode = ParticleModuleRequired->BoundingMode;
	OutOpacitySourceMode = ParticleModuleRequired->OpacitySourceMode;
	OutAlphaThreshold = ParticleModuleRequired->AlphaThreshold;
}

void UFXConverterUtilitiesLibrary::GetParticleModuleColorProps(UParticleModuleColor* ParticleModule, UDistribution*& OutStartColor, UDistribution*& OutStartAlpha, bool& bOutClampAlpha)
{
	OutStartColor = ParticleModule->StartColor.Distribution;
	OutStartAlpha = ParticleModule->StartAlpha.Distribution;
	bOutClampAlpha = ParticleModule->bClampAlpha;
}

void UFXConverterUtilitiesLibrary::GetParticleModuleColorOverLifeProps(UParticleModuleColorOverLife* ParticleModule, UDistribution*& OutColorOverLife, UDistribution*& OutAlphaOverLife, bool& bOutClampAlpha)
{
	OutColorOverLife = ParticleModule->ColorOverLife.Distribution;
	OutAlphaOverLife = ParticleModule->AlphaOverLife.Distribution;
	bOutClampAlpha = ParticleModule->bClampAlpha;
}

void UFXConverterUtilitiesLibrary::GetParticleModuleLifetimeProps(UParticleModuleLifetime* ParticleModule, UDistribution*& OutLifetime)
{
	OutLifetime = ParticleModule->Lifetime.Distribution;
}

void UFXConverterUtilitiesLibrary::GetParticleModuleSizeProps(UParticleModuleSize* ParticleModule, UDistribution*& OutStartSize)
{
	OutStartSize = ParticleModule->StartSize.Distribution;
}

void UFXConverterUtilitiesLibrary::GetParticleModuleVelocityProps(UParticleModuleVelocity* ParticleModule, UDistribution*& OutStartVelocity, UDistribution*& OutStartVelocityRadial, bool& bOutInWorldSpace, bool& bOutApplyOwnerScale)
{
	OutStartVelocity = ParticleModule->StartVelocity.Distribution;
	OutStartVelocityRadial = ParticleModule->StartVelocityRadial.Distribution;
	bOutInWorldSpace = ParticleModule->bInWorldSpace;
	bOutApplyOwnerScale = ParticleModule->bApplyOwnerScale;
}

void UFXConverterUtilitiesLibrary::GetParticleModuleConstantAccelerationProps(UParticleModuleAccelerationConstant* ParticleModule, FVector& OutConstAcceleration)
{
	OutConstAcceleration = ParticleModule->Acceleration;
}

void UFXConverterUtilitiesLibrary::GetParticleModuleLocationPrimitiveSphereProps(UParticleModuleLocationPrimitiveSphere* ParticleModule, UDistribution*& OutStartRadius)
{
	OutStartRadius = ParticleModule->StartRadius.Distribution;
}

void UFXConverterUtilitiesLibrary::GetParticleModuleMeshRotationProps(UParticleModuleMeshRotation* ParticleModule, UDistribution*& OutStartRotation, bool& bOutInheritParentRotation)
{
	OutStartRotation = ParticleModule->StartRotation.Distribution;
	bOutInheritParentRotation = ParticleModule->bInheritParent;
}

void UFXConverterUtilitiesLibrary::GetParticleModuleCollisionProps(
	  UParticleModuleCollision* ParticleModule
	, UDistribution*& OutDampingFactor
	, UDistribution*& OutDampingFactorRotation
	, UDistribution*& OutMaxCollisions
	, TEnumAsByte<EParticleCollisionComplete>& OutCollisionCompleteOption
	, TArray<TEnumAsByte<EObjectTypeQuery>>& OutCollisionTypes
	, bool& bOutApplyPhysics
	, bool& bOutIgnoreTriggerVolumes
	, UDistribution*& OutParticleMass
	, float& OutDirScalar
	, bool& bOutPawnsDoNotDecrementCount
	, bool& bOutOnlyVerticalNormalsDecrementCount
	, float& OutVerticalFudgeFactor
	, UDistribution*& OutDelayAmount
	, bool& bOutDropDetail
	, bool& bOutCollideOnlyIfVisible
	, bool& bOutIgnoreSourceActor
	, float& OutMaxCollisionDistance)
{
	OutDampingFactor = ParticleModule->DampingFactor.Distribution;
	OutDampingFactorRotation = ParticleModule->DampingFactorRotation.Distribution;
	OutMaxCollisions = ParticleModule->MaxCollisions.Distribution;
	OutCollisionCompleteOption = ParticleModule->CollisionCompletionOption;
	OutCollisionTypes = ParticleModule->CollisionTypes;
	bOutApplyPhysics = ParticleModule->bApplyPhysics;
	bOutIgnoreTriggerVolumes = ParticleModule->bIgnoreTriggerVolumes;
	OutParticleMass = ParticleModule->ParticleMass.Distribution;
	OutDirScalar = ParticleModule->DirScalar;
	bOutPawnsDoNotDecrementCount = ParticleModule->bPawnsDoNotDecrementCount;
	bOutOnlyVerticalNormalsDecrementCount = ParticleModule->bOnlyVerticalNormalsDecrementCount;
	OutVerticalFudgeFactor = ParticleModule->VerticalFudgeFactor;
	OutDelayAmount = ParticleModule->DelayAmount.Distribution;
	bOutDropDetail = ParticleModule->bDropDetail;
	bOutCollideOnlyIfVisible = ParticleModule->bCollideOnlyIfVisible;
	bOutIgnoreSourceActor = ParticleModule->bIgnoreSourceActor;
	OutMaxCollisionDistance = ParticleModule->MaxCollisionDistance;
}

void UFXConverterUtilitiesLibrary::GetParticleModuleSizeScaleBySpeedProps(UParticleModuleSizeScaleBySpeed* ParticleModule, FVector2D& OutSpeedScale, FVector2D& OutMaxScale)
{
	OutSpeedScale = ParticleModule->SpeedScale;
	OutMaxScale = ParticleModule->MaxScale;
}

void UFXConverterUtilitiesLibrary::GetParticleModuleVectorFieldLocalProps(
	  UParticleModuleVectorFieldLocal* ParticleModule
	, UVectorField* OutVectorField
	, FVector& OutRelativeTranslation
	, FRotator& OutRelativeRotation
	, FVector& OutRelativeScale3D
	, float& OutIntensity
	, float& OutTightness
	, bool& bOutIgnoreComponentTransform
	, bool& bOutTileX
	, bool& bOutTileY
	, bool& bOutTileZ
	, bool& bOutUseFixDT)
{
	OutVectorField = ParticleModule->VectorField;
	OutRelativeTranslation = ParticleModule->RelativeTranslation;
	OutRelativeRotation = ParticleModule->RelativeRotation;
	OutRelativeScale3D = ParticleModule->RelativeScale3D;
	OutIntensity = ParticleModule->Intensity;
	OutTightness = ParticleModule->Tightness;
	bOutIgnoreComponentTransform = ParticleModule->bIgnoreComponentTransform;
	bOutTileX = ParticleModule->bTileX;
	bOutTileY = ParticleModule->bTileY;
	bOutTileZ = ParticleModule->bTileZ;
	bOutUseFixDT = ParticleModule->bUseFixDT;
}

void UFXConverterUtilitiesLibrary::GetParticleModuleVectorFieldRotationRateProps(UParticleModuleVectorFieldRotationRate* ParticleModule, FVector& OutRotationRate)
{
	OutRotationRate = ParticleModule->RotationRate;
}

void UFXConverterUtilitiesLibrary::GetParticleModuleOrbitProps(
	  UParticleModuleOrbit* ParticleModule
	, TEnumAsByte<enum EOrbitChainMode>& OutChainMode
	, UDistribution*& OutOffsetAmount
	, FOrbitOptionsBP& OutOffsetOptions
	, UDistribution*& OutRotationAmount
	, FOrbitOptionsBP& OutRotationOptions
	, UDistribution*& OutRotationRateAmount
	, FOrbitOptionsBP& OutRotationRateOptions)
{
	OutChainMode = ParticleModule->ChainMode;
	OutOffsetAmount = ParticleModule->OffsetAmount.Distribution;
	OutOffsetOptions = ParticleModule->OffsetOptions;
	OutRotationAmount = ParticleModule->RotationAmount.Distribution;
	OutRotationOptions = ParticleModule->RotationOptions;
	OutRotationRateAmount = ParticleModule->RotationRateAmount.Distribution;
	OutRotationRateOptions = ParticleModule->RotationRateOptions;
}

void UFXConverterUtilitiesLibrary::GetParticleModuleSizeMultiplyLifeProps(
	UParticleModuleSizeMultiplyLife* ParticleModule
	, UDistribution*& OutLifeMultiplier
	, bool& OutMultiplyX
	, bool& OutMultiplyY
	, bool& OutMultiplyZ)
{
	OutLifeMultiplier = ParticleModule->LifeMultiplier.Distribution;
	OutMultiplyX = ParticleModule->MultiplyX;
	OutMultiplyY = ParticleModule->MultiplyY;
	OutMultiplyZ = ParticleModule->MultiplyZ;
}

void UFXConverterUtilitiesLibrary::GetParticleModuleColorScaleOverLifeProps(
	UParticleModuleColorScaleOverLife* ParticleModule
	, UDistribution*& OutColorScaleOverLife
	, UDistribution*& OutAlphaScaleOverLife
	, bool& bOutEmitterTime)
{
	OutColorScaleOverLife = ParticleModule->ColorScaleOverLife.Distribution;
	OutAlphaScaleOverLife = ParticleModule->AlphaScaleOverLife.Distribution;
	bOutEmitterTime = ParticleModule->bEmitterTime;
}

void UFXConverterUtilitiesLibrary::GetParticleModuleRotationProps(UParticleModuleRotation* ParticleModule, UDistribution*& OutStartRotation)
{
	OutStartRotation = ParticleModule->StartRotation.Distribution;
}

void UFXConverterUtilitiesLibrary::GetParticleModuleRotationRateProps(UParticleModuleRotationRate* ParticleModule, UDistribution*& OutStartRotationRate)
{
	OutStartRotationRate = ParticleModule->StartRotationRate.Distribution;
}

void UFXConverterUtilitiesLibrary::GetParticleModuleSubUVProps(
	UParticleModuleSubUV* ParticleModule
	, USubUVAnimation*& OutAnimation
	, UDistribution*& OutSubImageIndex
	, bool& bOutUseRealTime)
{
	OutAnimation = ParticleModule->Animation;
	OutSubImageIndex = ParticleModule->SubImageIndex.Distribution;
	bOutUseRealTime = ParticleModule->bUseRealTime;
}

void UFXConverterUtilitiesLibrary::GetParticleModuleCameraOffsetProps(
	UParticleModuleCameraOffset* ParticleModule
	, UDistribution*& OutCameraOffset
	, bool& bOutSpawnTimeOnly
	, TEnumAsByte<EParticleCameraOffsetUpdateMethod>& OutUpdateMethod)
{
	OutCameraOffset = ParticleModule->CameraOffset.Distribution;
	bOutSpawnTimeOnly = ParticleModule->bSpawnTimeOnly;
	OutUpdateMethod = ParticleModule->UpdateMethod;
}

void UFXConverterUtilitiesLibrary::GetParticleModuleSubUVMovieProps(
	UParticleModuleSubUVMovie* ParticleModule
	, bool& bOutUseEmitterTime
	, UDistribution*& OutFrameRate
	, int32& OutStartingFrame)
{
	bOutUseEmitterTime = ParticleModule->bUseEmitterTime;
	OutFrameRate = ParticleModule->FrameRate.Distribution;
	OutStartingFrame = ParticleModule->StartingFrame;
}

void UFXConverterUtilitiesLibrary::GetParticleModuleParameterDynamicProps(UParticleModuleParameterDynamic* ParticleModule, TArray<FEmitterDynamicParameterBP>& OutDynamicParams, bool& bOutUsesVelocity)
{	
	OutDynamicParams.Reserve(ParticleModule->DynamicParams.Num());
	for (const FEmitterDynamicParameter& DynamicParam : ParticleModule->DynamicParams)
	{
		OutDynamicParams.Add(DynamicParam);
	}
	bOutUsesVelocity = ParticleModule->bUsesVelocity;

	//@todo(ng) consider adding these flags to payload
// 	/** Flags for optimizing update */
// 	UPROPERTY()
// 	int32 UpdateFlags;
}

void UFXConverterUtilitiesLibrary::GetParticleModuleAccelerationDragProps(UParticleModuleAccelerationDrag* ParticleModule, UDistribution*& OutDragCoefficientRaw)
{
	OutDragCoefficientRaw = ParticleModule->DragCoefficientRaw.Distribution;
}

void UFXConverterUtilitiesLibrary::GetParticleModuleAccelerationProps(UParticleModuleAcceleration* ParticleModule, UDistribution*& OutAcceleration, bool& bOutApplyOwnerScale)
{
	OutAcceleration = ParticleModule->Acceleration.Distribution;
	bOutApplyOwnerScale = ParticleModule->bApplyOwnerScale;
}

void UFXConverterUtilitiesLibrary::GetDistributionMinMaxValues(
	  UDistribution* Distribution
	, bool& bOutSuccess
	, FVector& OutMinValue
	, FVector& OutMaxValue)
{
	if (Distribution->IsA<UDistributionFloatConstant>())
	{	
		float DistributionValue = 0.0f;
		GetFloatDistributionConstValues(static_cast<UDistributionFloatConstant*>(Distribution), DistributionValue);
		bOutSuccess = true;
		OutMinValue = FVector(DistributionValue, 0.0, 0.0);
		OutMaxValue = FVector(DistributionValue, 0.0, 0.0);
		return;
	}

	else if (Distribution->IsA<UDistributionVectorConstant>())
	{
		FVector DistributionValue = FVector(0.0f);
		GetVectorDistributionConstValues(static_cast<UDistributionVectorConstant*>(Distribution), DistributionValue);
		bOutSuccess = true;
		OutMinValue = DistributionValue;
		OutMaxValue = DistributionValue;
		return;
	}

	else if (Distribution->IsA<UDistributionFloatConstantCurve>())
	{
		UDistributionFloatConstantCurve* FloatCurveDistribution = static_cast<UDistributionFloatConstantCurve*>(Distribution);
		if (FloatCurveDistribution->ConstantCurve.Points.Num() == 0)
		{
			bOutSuccess = false;
			return;
		}

		float MinValue = FloatCurveDistribution->ConstantCurve.Points[0].OutVal;
		float MaxValue = FloatCurveDistribution->ConstantCurve.Points[0].OutVal;

		if (FloatCurveDistribution->ConstantCurve.Points.Num() > 1)
		{ 
			for (int i = 1; i < FloatCurveDistribution->ConstantCurve.Points.Num(); ++i)
			{
				const float& OutVal = FloatCurveDistribution->ConstantCurve.Points[i].OutVal;
				MinValue = OutVal < MinValue ? OutVal : MinValue;
				MaxValue = OutVal > MaxValue ? OutVal : MaxValue;
			}
		}

		bOutSuccess = true;
		OutMinValue = FVector(MinValue, 0.0, 0.0);
		OutMaxValue = FVector(MaxValue, 0.0, 0.0);
		return;
	}

	else if (Distribution->IsA<UDistributionVectorConstantCurve>())
	{
		UDistributionVectorConstantCurve* VectorCurveDistribution = static_cast<UDistributionVectorConstantCurve*>(Distribution);
		if (VectorCurveDistribution->ConstantCurve.Points.Num() == 0)
		{
			bOutSuccess = false;
			return;
		}

		OutMinValue = VectorCurveDistribution->ConstantCurve.Points[0].OutVal;
		OutMaxValue = VectorCurveDistribution->ConstantCurve.Points[0].OutVal;

		if (VectorCurveDistribution->ConstantCurve.Points.Num() > 1)
		{
			for (int i = 1; i < VectorCurveDistribution->ConstantCurve.Points.Num(); ++i)
			{
				const FVector& OutVal = VectorCurveDistribution->ConstantCurve.Points[i].OutVal;
				OutMinValue = OutVal.ComponentMin(OutMinValue);
				OutMaxValue = OutVal.ComponentMax(OutMaxValue);
			}
		}

		bOutSuccess = true;
		return;
	}

	else if (Distribution->IsA<UDistributionFloatUniform>())
	{
		float DistributionValueMin = 0.0f;
		float DistributionValueMax = 0.0f;
		GetFloatDistributionUniformValues(static_cast<UDistributionFloatUniform*>(Distribution), DistributionValueMin, DistributionValueMax);
		bOutSuccess = true;
		OutMinValue = FVector(DistributionValueMin, 0.0, 0.0);
		OutMaxValue = FVector(DistributionValueMax, 0.0, 0.0);
		return;
	}

	else if (Distribution->IsA<UDistributionVectorUniform>())
	{
		GetVectorDistributionUniformValues(static_cast<UDistributionVectorUniform*>(Distribution), OutMinValue, OutMaxValue);
		bOutSuccess = true;
		return;
	}

	else if (Distribution->IsA<UDistributionFloatUniformCurve>())
	{
		UDistributionFloatUniformCurve* FloatCurveDistribution = static_cast<UDistributionFloatUniformCurve*>(Distribution);
		if (FloatCurveDistribution->ConstantCurve.Points.Num() == 0)
		{
			bOutSuccess = false;
			return;
		}

		float MinValue = FloatCurveDistribution->ConstantCurve.Points[0].OutVal.X;
		float MaxValue = FloatCurveDistribution->ConstantCurve.Points[0].OutVal.Y;

		if (FloatCurveDistribution->ConstantCurve.Points.Num() > 1)
		{
			for (int i = 1; i < FloatCurveDistribution->ConstantCurve.Points.Num(); ++i)
			{
				const FVector2D& OutVal = FloatCurveDistribution->ConstantCurve.Points[i].OutVal;
				MinValue = OutVal.X < MinValue ? OutVal.X : MinValue;
				MaxValue = OutVal.Y > MaxValue ? OutVal.Y : MaxValue;
			}
		}

		bOutSuccess = true;
		OutMinValue = FVector(MinValue, 0.0, 0.0);
		OutMaxValue = FVector(MaxValue, 0.0, 0.0);
		return;
	}

	else if (Distribution->IsA<UDistributionVectorUniformCurve>())
	{
		UDistributionVectorUniformCurve* VectorCurveDistribution = static_cast<UDistributionVectorUniformCurve*>(Distribution);
		if (VectorCurveDistribution->ConstantCurve.Points.Num() == 0)
		{
			bOutSuccess = false;
			return;
		}
			
		OutMinValue = VectorCurveDistribution->ConstantCurve.Points[0].OutVal.v1;
		OutMaxValue = VectorCurveDistribution->ConstantCurve.Points[0].OutVal.v2;

		if (VectorCurveDistribution->ConstantCurve.Points.Num() > 1)
		{
			for (int i = 1; i < VectorCurveDistribution->ConstantCurve.Points.Num(); ++i)
			{
				const FTwoVectors& OutVal = VectorCurveDistribution->ConstantCurve.Points[i].OutVal;
				OutMinValue = OutVal.v1.ComponentMin(OutMinValue);
				OutMaxValue = OutVal.v2.ComponentMax(OutMaxValue);
			}
		}

		bOutSuccess = true;
		return;
	}

	else if (Distribution->IsA<UDistributionFloatParameterBase>())
	{
		bOutSuccess = false;
		return;
	}

	else if (Distribution->IsA<UDistributionVectorParameterBase>())
	{
		bOutSuccess = false;
		return;
	}

	bOutSuccess = false;
}

void UFXConverterUtilitiesLibrary::GetDistributionType(
	UDistribution* Distribution
	, EDistributionType& OutDistributionType
	, EDistributionValueType& OutCascadeDistributionValueType)
{
	if (Distribution->IsA<UDistributionFloatConstant>())
	{
		OutDistributionType = EDistributionType::Const;
		OutCascadeDistributionValueType = EDistributionValueType::Float;
		return;
	}
	else if (Distribution->IsA<UDistributionVectorConstant>())
	{
		OutDistributionType = EDistributionType::Const;
		OutCascadeDistributionValueType = EDistributionValueType::Vector;
		return;
	}
	else if (Distribution->IsA<UDistributionFloatConstantCurve>())
	{
		OutDistributionType = EDistributionType::ConstCurve;
		OutCascadeDistributionValueType = EDistributionValueType::Float;
		return;
	}
	else if (Distribution->IsA<UDistributionVectorConstantCurve>())
	{
		OutDistributionType = EDistributionType::ConstCurve;
		OutCascadeDistributionValueType = EDistributionValueType::Vector;
		return;
	}
	else if (Distribution->IsA<UDistributionFloatUniform>())
	{
		OutDistributionType = EDistributionType::Uniform;
		OutCascadeDistributionValueType = EDistributionValueType::Float;
		return;
	}
	else if (Distribution->IsA<UDistributionVectorUniform>())
	{
		OutDistributionType = EDistributionType::Uniform;
		OutCascadeDistributionValueType = EDistributionValueType::Vector;
		return;
	}
	else if (Distribution->IsA<UDistributionFloatUniformCurve>())
	{
		OutDistributionType = EDistributionType::UniformCurve;
		OutCascadeDistributionValueType = EDistributionValueType::Float;
		return;
	}
	else if (Distribution->IsA<UDistributionVectorUniformCurve>())
	{
		OutDistributionType = EDistributionType::UniformCurve;
		OutCascadeDistributionValueType = EDistributionValueType::Vector;
		return;
	}
	else if (Distribution->IsA<UDistributionFloatParameterBase>())
	{
		OutDistributionType = EDistributionType::Parameter;
		OutCascadeDistributionValueType = EDistributionValueType::Float;
		return;
	}
	else if (Distribution->IsA<UDistributionVectorParameterBase>())
	{
		OutDistributionType = EDistributionType::Parameter;
		OutCascadeDistributionValueType = EDistributionValueType::Vector;
		return;
	}

	OutDistributionType = EDistributionType::NONE;
	OutCascadeDistributionValueType = EDistributionValueType::NONE;
}

void UFXConverterUtilitiesLibrary::GetFloatDistributionConstValues(UDistributionFloatConstant* Distribution, float& OutConstFloat)
{
	OutConstFloat = Distribution->GetValue();
}

void UFXConverterUtilitiesLibrary::GetVectorDistributionConstValues(UDistributionVectorConstant* Distribution, FVector& OutConstVector)
{
	OutConstVector = Distribution->GetValue();
}

void UFXConverterUtilitiesLibrary::GetFloatDistributionUniformValues(UDistributionFloatUniform* Distribution, float& OutMin, float& OutMax)
{
	OutMin = Distribution->Min;
	OutMax = Distribution->Max;
}

void UFXConverterUtilitiesLibrary::GetVectorDistributionUniformValues(UDistributionVectorUniform* Distribution, FVector& OutMin, FVector& OutMax)
{
	OutMin = Distribution->Min;
	OutMax = Distribution->Max;
}

void UFXConverterUtilitiesLibrary::GetFloatDistributionConstCurveValues(UDistributionFloatConstantCurve* Distribution, FInterpCurveFloat& OutInterpCurveFloat)
{
	OutInterpCurveFloat = Distribution->ConstantCurve;
}

void UFXConverterUtilitiesLibrary::GetVectorDistributionConstCurveValues(UDistributionVectorConstantCurve* Distribution, FInterpCurveVector& OutInterpCurveVector)
{
	OutInterpCurveVector = Distribution->ConstantCurve;
}

void UFXConverterUtilitiesLibrary::GetFloatDistributionUniformCurveValues(UDistributionFloatUniformCurve* Distribution, FInterpCurveVector2D& OutInterpCurveVector2D)
{
	OutInterpCurveVector2D = Distribution->ConstantCurve;
}

void UFXConverterUtilitiesLibrary::GetVectorDistributionUniformCurveValues(UDistributionVectorUniformCurve* Distribution, FInterpCurveTwoVectors& OutInterpCurveTwoVectors)
{
	OutInterpCurveTwoVectors = Distribution->ConstantCurve;
}

void UFXConverterUtilitiesLibrary::GetFloatDistributionParameterValues(UDistributionFloatParameterBase* Distribution, FName& OutParameterName, float& OutMinInput, float& OutMaxInput, float& OutMinOutput, float& OutMaxOutput)
{
	OutParameterName = Distribution->ParameterName;
	OutMinInput = Distribution->MinInput;
	OutMaxInput = Distribution->MaxInput;
	OutMinOutput = Distribution->MinOutput;
	OutMaxOutput = Distribution->MaxOutput;
}

void UFXConverterUtilitiesLibrary::GetVectorDistributionParameterValues(UDistributionVectorParameterBase* Distribution, FName& OutParameterName, FVector& OutMinInput, FVector& OutMaxInput, FVector& OutMinOutput, FVector& OutMaxOutput)
{
	OutParameterName = Distribution->ParameterName;
	OutMinInput = Distribution->MinInput;
	OutMaxInput = Distribution->MaxInput;
	OutMinOutput = Distribution->MinOutput;
	OutMaxOutput = Distribution->MaxOutput;
}

TArray<FRichCurveKeyBP> UFXConverterUtilitiesLibrary::KeysFromInterpCurveFloat(FInterpCurveFloat Curve)
{
	TArray<FRichCurveKeyBP> Keys;
	for (const FInterpCurvePoint<float>& Point : Curve.Points)
	{
		Keys.Emplace(FRichCurveKey(Point));
	}
	return Keys;
}

TArray<FRichCurveKeyBP> UFXConverterUtilitiesLibrary::KeysFromInterpCurveVector(FInterpCurveVector Curve, int32 ComponentIdx)
{
	TArray<FRichCurveKeyBP> Keys;
	for (const FInterpCurvePoint<FVector>& Point : Curve.Points)
	{
		Keys.Emplace(FRichCurveKey(Point, ComponentIdx));
	}
	return Keys;
}

TArray<FRichCurveKeyBP> UFXConverterUtilitiesLibrary::KeysFromInterpCurveVector2D(FInterpCurveVector2D Curve, int32 ComponentIdx)
{
	TArray<FRichCurveKeyBP> Keys;
	for (const FInterpCurvePoint<FVector2D>& Point : Curve.Points)
	{
		Keys.Emplace(FRichCurveKey(Point, ComponentIdx));
	}
	return Keys;
}

TArray<FRichCurveKeyBP> UFXConverterUtilitiesLibrary::KeysFromInterpCurveTwoVectors(FInterpCurveTwoVectors Curve, int32 ComponentIdx)
{
	TArray<FRichCurveKeyBP> Keys;
	for (const FInterpCurvePoint<FTwoVectors>& Point : Curve.Points)
	{
		Keys.Emplace(FRichCurveKey(Point, ComponentIdx));
	}
	return Keys;
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////	UNiagaraSystemConversionContext																			  /////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
UNiagaraEmitterConversionContext* UNiagaraSystemConversionContext::AddEmptyEmitter(FString NewEmitterNameString)
{
	const TSharedPtr<FNiagaraSystemViewModel>& SystemViewModel = UFXConverterUtilitiesLibrary::GuidToNiagaraSystemViewModelMap.FindChecked(SystemViewModelGuid);

	UNiagaraEmitterFactoryNew* Factory = NewObject<UNiagaraEmitterFactoryNew>();
	UPackage* Pkg = CreatePackage(nullptr);
	FName NewEmitterName = FName(*NewEmitterNameString);
	EObjectFlags Flags = RF_Public | RF_Standalone;
	UNiagaraEmitter* NewEmitter = CastChecked<UNiagaraEmitter>(Factory->FactoryCreateNew(UNiagaraEmitter::StaticClass(), Pkg, NewEmitterName, Flags, NULL, GWarn));
	TSharedPtr<FNiagaraEmitterHandleViewModel> NewEmitterHandleViewModel = SystemViewModel->AddEmitter(*NewEmitter);

	FGuid NiagaraEmitterHandleViewModelGuid = FGuid::NewGuid();
	UFXConverterUtilitiesLibrary::GuidToNiagaraEmitterHandleViewModelMap.Add(NiagaraEmitterHandleViewModelGuid, NewEmitterHandleViewModel);
	UNiagaraEmitterConversionContext* EmitterConversionContext = NewObject<UNiagaraEmitterConversionContext>();
	EmitterConversionContext->Init(NewEmitterHandleViewModel->GetEmitterHandle()->GetInstance(), NiagaraEmitterHandleViewModelGuid);
	return EmitterConversionContext;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////	UNiagaraEmitterConversionContext																		  /////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
UNiagaraScriptConversionContext* UNiagaraEmitterConversionContext::FindOrAddModuleScript(FString ScriptNameString, FAssetData NiagaraScriptAssetData, EScriptExecutionCategory ExecutionCategory)
{
	FScriptConversionContextAndExecutionCategory* StagedScriptContextInfo = ScriptNameToStagedScriptMap.Find(ScriptNameString);
	if (StagedScriptContextInfo != nullptr)
	{
		return StagedScriptContextInfo->ScriptConversionContext;
	}

	UNiagaraScriptConversionContext* ScriptContext = NewObject<UNiagaraScriptConversionContext>();
	ScriptContext->Init(NiagaraScriptAssetData);
	FScriptConversionContextAndExecutionCategory StagedScriptInfo = FScriptConversionContextAndExecutionCategory(ScriptContext, ExecutionCategory);
	ScriptNameToStagedScriptMap.Add(ScriptNameString, StagedScriptInfo);
	return ScriptContext;
}

UNiagaraScriptConversionContext* UNiagaraEmitterConversionContext::FindModuleScript(FString ScriptNameString)
{
	FScriptConversionContextAndExecutionCategory* StagedScript = ScriptNameToStagedScriptMap.Find(ScriptNameString);
	if (StagedScript != nullptr)
	{
		return StagedScript->ScriptConversionContext;
	}
	return nullptr;
}

void UNiagaraEmitterConversionContext::AddModuleScript(UNiagaraScriptConversionContext* ScriptConversionContext, FString ScriptNameString, EScriptExecutionCategory ExecutionCategory)
{
	FScriptConversionContextAndExecutionCategory StagedScript = FScriptConversionContextAndExecutionCategory(ScriptConversionContext, ExecutionCategory);
	ScriptNameToStagedScriptMap.Add(ScriptNameString, StagedScript);
}

void UNiagaraEmitterConversionContext::SetParameterDirectly(FString ParameterNameString, UNiagaraScriptConversionContextInput* ParameterInput, EScriptExecutionCategory TargetExecutionCategory)
{
	const FName ParameterName = FName(ParameterNameString);
	const FNiagaraVariable TargetVariable = FNiagaraVariable(ParameterInput->TypeDefinition, ParameterName);
	const TArray<FNiagaraVariable> InVariables = {TargetVariable};
	const TArray<FString> InVariableDefaults = {FString()};
	UNiagaraClipboardFunction* Assignment = UNiagaraClipboardFunction::CreateAssignmentFunction(this, "SetParameter", InVariables, InVariableDefaults);
	ParameterInput->ClipboardFunctionInput->InputName = ParameterName;
	Assignment->Inputs.Add(ParameterInput->ClipboardFunctionInput);
	const int32 Idx = StagedParameterSets.Add(Assignment);
	ScriptExecutionCategoryToParameterSetIndicesMap.FindOrAdd(TargetExecutionCategory).Indices.Add(Idx);
}

void UNiagaraEmitterConversionContext::AddRenderer(FString RendererNameString, UNiagaraRendererProperties* NewRendererProperties)
{
	RendererNameToStagedRendererPropertiesMap.Add(RendererNameString, NewRendererProperties);
}

UNiagaraRendererProperties* UNiagaraEmitterConversionContext::FindRenderer(FString RendererNameString)
	{
	UNiagaraRendererProperties** PropsPtr = RendererNameToStagedRendererPropertiesMap.Find(RendererNameString);
	if (PropsPtr == nullptr)
	{
		return nullptr;
	}
	return *PropsPtr;
}

void UNiagaraEmitterConversionContext::Log(FString Message, ENiagaraMessageSeverity Severity, bool bIsVerbose /*= false*/)
{
	EmitterMessages.Add(FGenericConverterMessage(Message, Severity, bIsVerbose));
}

void UNiagaraEmitterConversionContext::Finalize()
{
	const TSharedPtr<FNiagaraEmitterHandleViewModel>& TargetEmitterHandleViewModel = UFXConverterUtilitiesLibrary::GuidToNiagaraEmitterHandleViewModelMap.FindChecked(EmitterHandleViewModelGuid);
	TSharedRef<FNiagaraSystemViewModel> OwningSystemViewModel = TargetEmitterHandleViewModel->GetOwningSystemViewModel();
	TArray<UNiagaraStackItemGroup*> StackItemGroups;
	TargetEmitterHandleViewModel->GetEmitterStackViewModel()->GetRootEntry()->GetUnfilteredChildrenOfType<UNiagaraStackItemGroup>(StackItemGroups);

	auto GetStackItemGroupForScriptExecutionCategory = [StackItemGroups](const EScriptExecutionCategory ExecutionCategory)->UNiagaraStackItemGroup* const* {
		FName ExecutionCategoryName;
		FName ExecutionSubcategoryName;
		switch (ExecutionCategory) {
		case EScriptExecutionCategory::EmitterSpawn:
			ExecutionCategoryName = UNiagaraStackEntry::FExecutionCategoryNames::Emitter;
			ExecutionSubcategoryName = UNiagaraStackEntry::FExecutionSubcategoryNames::Spawn;
			break;
		case EScriptExecutionCategory::EmitterUpdate:
			ExecutionCategoryName = UNiagaraStackEntry::FExecutionCategoryNames::Emitter;
			ExecutionSubcategoryName = UNiagaraStackEntry::FExecutionSubcategoryNames::Update;
			break;
		case EScriptExecutionCategory::ParticleSpawn:
			ExecutionCategoryName = UNiagaraStackEntry::FExecutionCategoryNames::Particle;
			ExecutionSubcategoryName = UNiagaraStackEntry::FExecutionSubcategoryNames::Spawn;
			break;
		case EScriptExecutionCategory::ParticleUpdate:
			ExecutionCategoryName = UNiagaraStackEntry::FExecutionCategoryNames::Particle;
			ExecutionSubcategoryName = UNiagaraStackEntry::FExecutionSubcategoryNames::Update;
			break;
		default:
			UE_LOG(LogTemp, Error, TEXT("Encountered unknown EScriptExecutionCategory when choosing script to add module to emitter!"));
			return nullptr;
		};

		UNiagaraStackItemGroup* const* StackItemGroup = StackItemGroups.FindByPredicate([ExecutionCategoryName, ExecutionSubcategoryName](const UNiagaraStackItemGroup* EmitterItemGroup) {
			return EmitterItemGroup->GetExecutionCategoryName() == ExecutionCategoryName && EmitterItemGroup->GetExecutionSubcategoryName() == ExecutionSubcategoryName; });
		return StackItemGroup;
	};

	// Set the Emitter enabled state
	TargetEmitterHandleViewModel->SetIsEnabled(bEnabled);

	// Add the staged parameter set modules
	for (auto It = ScriptExecutionCategoryToParameterSetIndicesMap.CreateConstIterator(); It; ++It)
	{
		const EScriptExecutionCategory ExecutionCategory = It.Key();
		const FParameterSetIndices& ParameterSetIndices = It.Value();
		if (ParameterSetIndices.Indices.Num() == 0)
		{
			continue;
		}

		UNiagaraStackItemGroup* const* StackItemGroupPtr = GetStackItemGroupForScriptExecutionCategory(ExecutionCategory);
		if (StackItemGroupPtr == nullptr)
		{
			return;
		}

		for(const int32 Idx : ParameterSetIndices.Indices)
		{ 
			UNiagaraClipboardContent* ClipboardContent = UNiagaraClipboardContent::Create();
			ClipboardContent->Functions.Add(StagedParameterSets[Idx]);

			FText PasteWarning = FText();
			UNiagaraStackItemGroup* StackItemGroup = *StackItemGroupPtr;
			StackItemGroup->Paste(ClipboardContent, PasteWarning);

			if (PasteWarning.IsEmpty() == false)
			{
				UE_LOG(LogTemp, Warning, TEXT("%s"), *PasteWarning.ToString());
			}
		}
	}

	// Add the staged script conversion contexts
	for (auto It = ScriptNameToStagedScriptMap.CreateIterator(); It; ++It)
	{
		FScriptConversionContextAndExecutionCategory StagedScriptContextInfo = It.Value();
		UNiagaraScriptConversionContext* StagedScriptContext = StagedScriptContextInfo.ScriptConversionContext;
		EScriptExecutionCategory TargetExecutionCategory = StagedScriptContextInfo.ScriptExecutionCategory;
		UNiagaraStackItemGroup* const* StackItemGroup = GetStackItemGroupForScriptExecutionCategory(TargetExecutionCategory);
		if (StackItemGroup == nullptr)
		{
			return;
		}

		UNiagaraClipboardContent* ClipboardContent = UNiagaraClipboardContent::Create();
		UNiagaraScript* NiagaraScript = StagedScriptContext->GetScript();

		UNiagaraClipboardFunction* ClipboardFunction = UNiagaraClipboardFunction::CreateScriptFunction(ClipboardContent, "Function", NiagaraScript);
		ClipboardFunction->Inputs = StagedScriptContext->GetClipboardFunctionInputs();
		ClipboardContent->Functions.Add(ClipboardFunction);

		ClipboardFunction->OnPastedFunctionCallNodeDelegate.BindDynamic(this, &UNiagaraEmitterConversionContext::SetPastedFunctionCallNode);

		// Commit the clipboard content to the target stack entry
		FText PasteWarning = FText();
		UNiagaraStackItemGroup* TargetStackEntry = *StackItemGroup;
		TargetStackEntry->Paste(ClipboardContent, PasteWarning);
		ClipboardFunction->OnPastedFunctionCallNodeDelegate.Unbind();

		if (PasteWarning.IsEmpty() == false)
		{
			UE_LOG(LogTemp, Warning, TEXT("%s"), *PasteWarning.ToString());
		}
		
		if (PastedFunctionCallNode != nullptr)
		{
			// Set the module enabled state
			if (StagedScriptContext->GetEnabled() == false)
			{ 
				FNiagaraStackGraphUtilities::SetModuleIsEnabled(*PastedFunctionCallNode, false);
			}

			// Push the per module messages
			for (const FGenericConverterMessage& Message : StagedScriptContext->GetStackMessages())
			{
				UNiagaraMessageDataText* NewMessageDataText = NewObject<UNiagaraMessageDataText>(PastedFunctionCallNode);
				const FName TopicName = Message.bIsVerbose ? FNiagaraConverterMessageTopics::VerboseConversionEventTopicName : FNiagaraConverterMessageTopics::ConversionEventTopicName;
				NewMessageDataText->Init(FText::FromString(Message.Message), Message.MessageSeverity, TopicName);
				OwningSystemViewModel->AddStackMessage(NewMessageDataText, PastedFunctionCallNode);
			}
		}
		else 
		{
			ensureAlwaysMsgf(false, TEXT("Did not receive a function call from the paste event!"));
		}

		PastedFunctionCallNode = nullptr;
	}

	
	UNiagaraStackItemGroup* const* RendererStackItemGroupPtr = StackItemGroups.FindByPredicate([](const UNiagaraStackItemGroup* EmitterItemGroup) {
		return EmitterItemGroup->GetExecutionCategoryName() == UNiagaraStackEntry::FExecutionCategoryNames::Render
			&& EmitterItemGroup->GetExecutionSubcategoryName() == UNiagaraStackEntry::FExecutionSubcategoryNames::Render; });

	if (RendererStackItemGroupPtr == nullptr)
	{
		return;
	}

	// Add the staged renderer properties
	for (auto It = RendererNameToStagedRendererPropertiesMap.CreateIterator(); It; ++It)
	{	
		UNiagaraRendererProperties* NewRendererProperties = It.Value();

		UNiagaraClipboardContent* ClipboardContent = UNiagaraClipboardContent::Create();
		ClipboardContent->Renderers.Add(NewRendererProperties);

		FText PasteWarning = FText();
		UNiagaraStackItemGroup* RendererStackItemGroup = *RendererStackItemGroupPtr;
		RendererStackItemGroup->Paste(ClipboardContent, PasteWarning);
		if (PasteWarning.IsEmpty() == false)
		{
			UE_LOG(LogTemp, Warning, TEXT("%s"), *PasteWarning.ToString());
		}
	}

	// Push the messages
	for (FGenericConverterMessage& Message : EmitterMessages)
	{	
		UNiagaraMessageDataText* NewMessageDataText = NewObject<UNiagaraMessageDataText>(Emitter);
		const FName TopicName = Message.bIsVerbose ? FNiagaraConverterMessageTopics::VerboseConversionEventTopicName : FNiagaraConverterMessageTopics::ConversionEventTopicName;
		NewMessageDataText->Init(FText::FromString(Message.Message), Message.MessageSeverity, TopicName);
		TargetEmitterHandleViewModel->AddMessage(NewMessageDataText);
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////	UNiagaraScriptConversionContext																			  /////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void UNiagaraScriptConversionContext::Init(const FAssetData& InNiagaraScriptAssetData)
{
	Script = static_cast<UNiagaraScript*>(InNiagaraScriptAssetData.GetAsset());
	if (Script == nullptr)
	{
		Log("Failed to create script! AssetData path was invalid!: " + InNiagaraScriptAssetData.PackagePath.ToString(), ENiagaraMessageSeverity::Error);
		return;
	}
	bEnabled = true;

	// Gather the inputs to this script and add them to the lookup table for validating UNiagaraScriptConversionContextInputs that are set.
	TArray<UNiagaraNodeInput*> InputNodes;

	const TMap<FNiagaraVariable, FInputPinsAndOutputPins> VarToPinsMap = static_cast<UNiagaraScriptSource*>(Script->GetSource())->NodeGraph->CollectVarsToInOutPinsMap();
	for (auto It = VarToPinsMap.CreateConstIterator(); It; ++It)
	{
		if (It->Value.OutputPins.Num() > 0)
		{
			const FNiagaraVariable& Var = It->Key;
			InputNameToTypeDefMap.Add(FNiagaraEditorUtilities::GetNamespacelessVariableNameString(Var.GetName()), Var.GetType());
		}
	}
}

bool UNiagaraScriptConversionContext::SetParameter(FString ParameterName, UNiagaraScriptConversionContextInput* ParameterInput, bool bInHasEditCondition /*= false*/, bool bInEditConditionValue /* = false*/)
{
	if (ParameterInput->ClipboardFunctionInput == nullptr)
	{
		return false;
	}
	
	const FNiagaraTypeDefinition* InputTypeDef = InputNameToTypeDefMap.Find(ParameterName);
	if (InputTypeDef == nullptr)
	{
		Log("Failed to set parameter " + ParameterName + ": Could not find input with this name!", ENiagaraMessageSeverity::Error);
		return false;
	}
	else if (ParameterInput->TypeDefinition != *InputTypeDef)
	{
		Log("Failed to set parameter " + ParameterName + ": Input types did not match! /n Tried to set: " + ParameterInput->TypeDefinition.GetName() + " | Input type was: " + InputTypeDef->GetName(), ENiagaraMessageSeverity::Error);
		return false;
	}

	ParameterInput->ClipboardFunctionInput->bHasEditCondition = bInHasEditCondition;
	ParameterInput->ClipboardFunctionInput->bEditConditionValue = bInEditConditionValue;
	ParameterInput->ClipboardFunctionInput->InputName = FName(*ParameterName);
	FunctionInputs.Add(ParameterInput->ClipboardFunctionInput);
	StackMessages.Append(ParameterInput->StackMessages);
	return true;
}

void UNiagaraScriptConversionContext::Log(FString Message, ENiagaraMessageSeverity Severity, bool bIsVerbose /* = false*/)
{	
	StackMessages.Add(FGenericConverterMessage(Message, Severity, bIsVerbose));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////	UNiagaraScriptConversionContextInput																	  /////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void UNiagaraScriptConversionContextInput::Init(
	  UNiagaraClipboardFunctionInput* InClipboardFunctionInput
	, const ENiagaraScriptInputType InInputType
	, const FNiagaraTypeDefinition& InTypeDefinition)
{
	ClipboardFunctionInput = InClipboardFunctionInput;
	InputType = InInputType;
	TypeDefinition = InTypeDefinition;
}


TArray<FRichCurveKey> FRichCurveKeyBP::KeysToBase(const TArray<FRichCurveKeyBP>& InKeyBPs)
{
	TArray<FRichCurveKey> Keys;
	Keys.AddUninitialized(InKeyBPs.Num());
	for (int i = 0; i < InKeyBPs.Num(); ++i)
	{
		Keys[i] = InKeyBPs[i].ToBase();
	}
	return Keys;
}
