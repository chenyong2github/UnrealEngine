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
	Input->Init(NewInput, TargetTypeDef);
	return Input;
}

UNiagaraScriptConversionContextInput* UFXConverterUtilitiesLibrary::CreateScriptInputFloat(float Value)
{
	UNiagaraClipboardFunctionInput* NewInput = UNiagaraClipboardEditorScriptingUtilities::CreateFloatLocalValueInput(GetTransientPackage(), FName(), false, false, Value);
	const FNiagaraTypeDefinition& TargetTypeDef = FNiagaraTypeDefinition::GetFloatDef();
	UNiagaraScriptConversionContextInput* Input = NewObject<UNiagaraScriptConversionContextInput>();
	Input->Init(NewInput, TargetTypeDef);
	return Input;
}

UNiagaraScriptConversionContextInput* UFXConverterUtilitiesLibrary::CreateScriptInputVector(FVector Value)
{
	UNiagaraClipboardFunctionInput* NewInput = UNiagaraClipboardEditorScriptingUtilities::CreateVec3LocalValueInput(GetTransientPackage(), FName(), false, false, Value);
	const FNiagaraTypeDefinition& TargetTypeDef = FNiagaraTypeDefinition::GetVec3Def();
	UNiagaraScriptConversionContextInput* Input = NewObject<UNiagaraScriptConversionContextInput>();
	Input->Init(NewInput, TargetTypeDef);
	return Input;
}

UNiagaraScriptConversionContextInput* UFXConverterUtilitiesLibrary::CreateScriptInputStruct(UUserDefinedStruct* Value)
{
	UNiagaraClipboardFunctionInput* NewInput = UNiagaraClipboardEditorScriptingUtilities::CreateStructLocalValueInput(GetTransientPackage(), FName(), false, false, Value);
	if (NewInput != nullptr)
	{
		UNiagaraScriptConversionContextInput* Input = NewObject<UNiagaraScriptConversionContextInput>();
		Input->Init(NewInput, NewInput->GetTypeDef());
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
		Input->Init(NewInput, NewInput->GetTypeDef());
		return Input;
	}
	return nullptr;
}

UNiagaraScriptConversionContextInput* UFXConverterUtilitiesLibrary::CreateScriptInputInt(int32 Value)
{
	UNiagaraClipboardFunctionInput* NewInput = UNiagaraClipboardEditorScriptingUtilities::CreateIntLocalValueInput(GetTransientPackage(), FName(), false, false, Value);
	const FNiagaraTypeDefinition& TargetTypeDef = FNiagaraTypeDefinition::GetIntDef();
	UNiagaraScriptConversionContextInput* Input = NewObject<UNiagaraScriptConversionContextInput>();
	Input->Init(NewInput, TargetTypeDef);
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
	Input->Init(NewInput, TargetTypeDef);
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
		Input->Init(NewInput, NewInput->GetTypeDef());
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

UNiagaraDataInterfaceCurve* UFXConverterUtilitiesLibrary::CreateFloatCurveDI()
{
	return NewObject<UNiagaraDataInterfaceCurve>();
}

UNiagaraDataInterfaceVector2DCurve* UFXConverterUtilitiesLibrary::CreateVec2CurveDI()
{
	return NewObject<UNiagaraDataInterfaceVector2DCurve>();
}

UNiagaraDataInterfaceVectorCurve* UFXConverterUtilitiesLibrary::CreateVec3CurveDI()
{
	return NewObject<UNiagaraDataInterfaceVectorCurve>();
}

UNiagaraDataInterfaceVector4Curve* UFXConverterUtilitiesLibrary::CreateVec4CurveDI()
{
	return NewObject<UNiagaraDataInterfaceVector4Curve>();
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

UClass* UFXConverterUtilitiesLibrary::GetParticleModuleSpawnClass()
{
	return UParticleModuleSpawn::StaticClass();
}

UClass* UFXConverterUtilitiesLibrary::GetParticleModuleRequiredClass()
{
	return UParticleModuleRequired::StaticClass();
}

UClass* UFXConverterUtilitiesLibrary::GetParticleModuleColorClass()
{
	return UParticleModuleColor::StaticClass();
}

UClass* UFXConverterUtilitiesLibrary::GetParticleModuleColorOverLifeClass()
{
	return UParticleModuleColorOverLife::StaticClass();
}

UClass* UFXConverterUtilitiesLibrary::GetParticleModuleLifetimeClass()
{
	return UParticleModuleLifetime::StaticClass();
}

UClass* UFXConverterUtilitiesLibrary::GetParticleModuleSizeClass()
{
	return UParticleModuleSize::StaticClass();
}

UClass* UFXConverterUtilitiesLibrary::GetParticleModuleVelocityClass()
{
	return UParticleModuleVelocity::StaticClass();
}

UClass* UFXConverterUtilitiesLibrary::GetParticleModuleTypeDataGPUClass()
{
	return UParticleModuleTypeDataGpu::StaticClass();
}

UClass* UFXConverterUtilitiesLibrary::GetParticleModuleTypeDataMeshClass()
{
	return UParticleModuleTypeDataMesh::StaticClass();
}

UClass* UFXConverterUtilitiesLibrary::GetParticleModuleConstantAccelerationClass()
{
	return UParticleModuleAccelerationConstant::StaticClass();
}

UClass* UFXConverterUtilitiesLibrary::GetParticleModuleLocationPrimitiveSphereClass()
{
	return UParticleModuleLocationPrimitiveSphere::StaticClass();
}

UClass* UFXConverterUtilitiesLibrary::GetParticleModuleMeshRotationClass()
{
	return UParticleModuleMeshRotation::StaticClass();
}

UClass* UFXConverterUtilitiesLibrary::GetParticleModuleCollisionClass()
{
	return UParticleModuleCollision::StaticClass();
}

UClass* UFXConverterUtilitiesLibrary::GetParticleModuleSizeScaleBySpeedClass()
{
	return UParticleModuleSizeScaleBySpeed::StaticClass();
}

UClass* UFXConverterUtilitiesLibrary::GetParticleModuleVectorFieldLocalClass()
{
	return UParticleModuleVectorFieldLocal::StaticClass();
}

UClass* UFXConverterUtilitiesLibrary::GetParticleModuleVectorFieldRotationRateClass()
{
	return UParticleModuleVectorFieldRotationRate::StaticClass();
}

UClass* UFXConverterUtilitiesLibrary::GetParticleModuleOrbitClass()
{
	return UParticleModuleOrbit::StaticClass();
}

UClass* UFXConverterUtilitiesLibrary::GetParticleModuleSizeMultipleLifeClass()
{
	return UParticleModuleSizeMultiplyLife::StaticClass();
}

UClass* UFXConverterUtilitiesLibrary::GetParticleModuleColorScaleOverLifeClass()
{
	return UParticleModuleColorScaleOverLife::StaticClass();
}

UClass* UFXConverterUtilitiesLibrary::GetParticleModuleRotationClass()
{
	return UParticleModuleRotation::StaticClass();
}

UClass* UFXConverterUtilitiesLibrary::GetParticleModuleRotationRateClass()
{
	return UParticleModuleRotationRate::StaticClass();
}

UClass* UFXConverterUtilitiesLibrary::GetParticleModuleSubUVClass()
{
	return UParticleModuleSubUV::StaticClass();
}

UClass* UFXConverterUtilitiesLibrary::GetParticleModuleCameraOffsetClass()
{
	return UParticleModuleCameraOffset::StaticClass();
}

UClass* UFXConverterUtilitiesLibrary::GetParticleModuleSubUVMovieClass()
{
	return UParticleModuleSubUVMovie::StaticClass();
}

UClass* UFXConverterUtilitiesLibrary::GetParticleModuleParameterDynamicClass()
{
	return UParticleModuleParameterDynamic::StaticClass();
}

UClass* UFXConverterUtilitiesLibrary::GetParticleModuleAccelerationDragClass()
{
	return UParticleModuleAccelerationDrag::StaticClass();
}

UClass* UFXConverterUtilitiesLibrary::GetParticleModuleAccelerationClass()
{
	return UParticleModuleAcceleration::StaticClass();
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
	, TEnumAsByte<EOpacitySourceMode>& OutOpacitySourceMode)
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

void UFXConverterUtilitiesLibrary::GetParticleModuleSizeMultipleLifeProps(
	UParticleModuleSizeMultiplyLife* ParticleModule
	, UDistribution*& OutLifeMultiplier
	, int32& OutMultiplyX
	, int32& OutMultiplyY
	, int32& OutMultiplyZ)
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

void UFXConverterUtilitiesLibrary::CopyCascadeFloatCurveToNiagaraCurveDI(UNiagaraDataInterfaceCurve* CurveDI, FInterpCurveFloat InterpCurveFloat)
{
	TArray<FRichCurveKey> NewRichCurveKeys;
	for(FInterpCurvePoint<float> Point : InterpCurveFloat.Points)
	{ 
		NewRichCurveKeys.Emplace(FRichCurveKey(Point));
	}
	FRichCurve NewRichCurve = FRichCurve();
	NewRichCurve.SetKeys(NewRichCurveKeys);
	CurveDI->Curve = NewRichCurve;
}

void UFXConverterUtilitiesLibrary::CopyCascadeVectorCurveToNiagaraCurveDI(UNiagaraDataInterfaceVectorCurve* CurveDI, FInterpCurveVector InterpCurveVector)
{
	TArray<FRichCurveKey> NewRichCurveKeys_X;
	TArray<FRichCurveKey> NewRichCurveKeys_Y;
	TArray<FRichCurveKey> NewRichCurveKeys_Z;
	for (FInterpCurvePoint<FVector> Point : InterpCurveVector.Points)
	{
		NewRichCurveKeys_X.Emplace(FRichCurveKey(Point, 0));
		NewRichCurveKeys_Y.Emplace(FRichCurveKey(Point, 1));
		NewRichCurveKeys_Z.Emplace(FRichCurveKey(Point, 2));
	}
	FRichCurve NewRichCurve_X = FRichCurve();
	FRichCurve NewRichCurve_Y = FRichCurve();
	FRichCurve NewRichCurve_Z = FRichCurve();
	NewRichCurve_X.SetKeys(NewRichCurveKeys_X);
	NewRichCurve_Y.SetKeys(NewRichCurveKeys_Y);
	NewRichCurve_Z.SetKeys(NewRichCurveKeys_Z);
	CurveDI->XCurve = NewRichCurve_X;
	CurveDI->YCurve = NewRichCurve_Y;
	CurveDI->ZCurve = NewRichCurve_Z;
}

void UFXConverterUtilitiesLibrary::CopyCascadeVector2DCurveToNiagaraCurveDI(UNiagaraDataInterfaceVector2DCurve* CurveDI, FInterpCurveVector2D InterpCurveVector2D)
{
	TArray<FRichCurveKey> NewRichCurveKeys_X;
	TArray<FRichCurveKey> NewRichCurveKeys_Y;
	for (FInterpCurvePoint<FVector2D> Point : InterpCurveVector2D.Points)
	{
		NewRichCurveKeys_X.Emplace(FRichCurveKey(Point, 0));
		NewRichCurveKeys_Y.Emplace(FRichCurveKey(Point, 1));
	}
	FRichCurve NewRichCurve_X = FRichCurve();
	FRichCurve NewRichCurve_Y = FRichCurve();
	NewRichCurve_X.SetKeys(NewRichCurveKeys_X);
	NewRichCurve_Y.SetKeys(NewRichCurveKeys_Y);
	CurveDI->XCurve = NewRichCurve_X;
	CurveDI->YCurve = NewRichCurve_Y;
}

void UFXConverterUtilitiesLibrary::CopyCascadeTwoVectorCurveToNiagaraCurveDI(UNiagaraDataInterfaceVector4Curve* CurveDI, FInterpCurveTwoVectors InterpCurveTwoVectors)
{
	TArray<FRichCurveKey> NewRichCurveKeys_X;
	TArray<FRichCurveKey> NewRichCurveKeys_Y;
	TArray<FRichCurveKey> NewRichCurveKeys_Z;
	TArray<FRichCurveKey> NewRichCurveKeys_W;
	for (FInterpCurvePoint<FTwoVectors> Point : InterpCurveTwoVectors.Points)
	{
		NewRichCurveKeys_X.Emplace(FRichCurveKey(Point, 0));
		NewRichCurveKeys_Y.Emplace(FRichCurveKey(Point, 1));
		NewRichCurveKeys_Z.Emplace(FRichCurveKey(Point, 2));
		NewRichCurveKeys_W.Emplace(FRichCurveKey(Point, 3));
	}
	FRichCurve NewRichCurve_X = FRichCurve();
	FRichCurve NewRichCurve_Y = FRichCurve();
	FRichCurve NewRichCurve_Z = FRichCurve();
	FRichCurve NewRichCurve_W = FRichCurve();
	NewRichCurve_X.SetKeys(NewRichCurveKeys_X);
	NewRichCurve_Y.SetKeys(NewRichCurveKeys_Y);
	NewRichCurve_Z.SetKeys(NewRichCurveKeys_Z);
	NewRichCurve_W.SetKeys(NewRichCurveKeys_W);
	CurveDI->XCurve = NewRichCurve_X;
	CurveDI->YCurve = NewRichCurve_Y;
	CurveDI->ZCurve = NewRichCurve_Z;
	CurveDI->WCurve = NewRichCurve_W;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////	UNiagaraSystemConversionContext																			  /////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
UNiagaraEmitterConversionContext* UNiagaraSystemConversionContext::AddEmptyEmitter(FString NewEmitterNameString)
{
	const TSharedPtr<FNiagaraSystemViewModel>& SystemViewModel = UFXConverterUtilitiesLibrary::GuidToNiagaraSystemViewModelMap.FindChecked(SystemViewModelGuid);

	UNiagaraEmitterFactoryNew* Factory = NewObject<UNiagaraEmitterFactoryNew>();
	UPackage* Pkg = CreatePackage(NULL, NULL);
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
	const FNiagaraVariable TargetVariable = FNiagaraVariable(ParameterInput->TargetTypeDefinition, ParameterName);
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

		UNiagaraStackItemGroup* const* StackItemGroup = GetStackItemGroupForScriptExecutionCategory(ExecutionCategory);
		if (StackItemGroup == nullptr)
		{
			return;
		}

		TArray<UNiagaraStackEntry*> TargetStackEntryArr;
		TargetStackEntryArr.Add(*StackItemGroup);

		for(const int32 Idx : ParameterSetIndices.Indices)
		{ 
			UNiagaraClipboardContent* ClipboardContent = UNiagaraClipboardContent::Create();
			ClipboardContent->Functions.Add(StagedParameterSets[Idx]);

			FNiagaraEditorModule::Get().GetClipboard().SetClipboardContent(ClipboardContent);
			FText PasteWarning = FText();
			FNiagaraStackClipboardUtilities::PasteSelection(TargetStackEntryArr, PasteWarning);

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

		UNiagaraClipboardFunction* ClipboardFunction = UNiagaraClipboardFunction::CreateScriptFunction(ClipboardContent, "Function", NiagaraScript); //@todo(ng) proper name here
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

	
	UNiagaraStackItemGroup* const* RendererStackItemGroup = StackItemGroups.FindByPredicate([](const UNiagaraStackItemGroup* EmitterItemGroup) {
		return EmitterItemGroup->GetExecutionCategoryName() == UNiagaraStackEntry::FExecutionCategoryNames::Render
			&& EmitterItemGroup->GetExecutionSubcategoryName() == UNiagaraStackEntry::FExecutionSubcategoryNames::Render; });
	TArray<UNiagaraStackEntry*> TargetRendererStackEntryArr;
	TargetRendererStackEntryArr.Add(*RendererStackItemGroup);

	// Add the staged renderer properties
	for (auto It = RendererNameToStagedRendererPropertiesMap.CreateIterator(); It; ++It)
	{	
		UNiagaraRendererProperties* NewRendererProperties = It.Value();

		UNiagaraClipboardContent* ClipboardContent = UNiagaraClipboardContent::Create();
		ClipboardContent->Renderers.Add(NewRendererProperties);
		FNiagaraEditorModule::Get().GetClipboard().SetClipboardContent(ClipboardContent);
		FText PasteWarning = FText();
		FNiagaraStackClipboardUtilities::PasteSelection(TargetRendererStackEntryArr, PasteWarning);
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
	bEnabled = true;
	// @todo(ng) build id table
	//static_cast<UNiagaraScriptSource*>(GetScript()->GetSource())->NodeGraph->FindInputNodes()
}

bool UNiagaraScriptConversionContext::SetParameter(FString ParameterName, UNiagaraScriptConversionContextInput* ParameterInput, bool bInHasEditCondition /*= false*/, bool bInEditConditionValue /* = false*/)
{
	//@todo(ng) assert on ParameterInput.TypeDefinition
	ParameterInput->ClipboardFunctionInput->bHasEditCondition = bInHasEditCondition;
	ParameterInput->ClipboardFunctionInput->bEditConditionValue = bInEditConditionValue;
	ParameterInput->ClipboardFunctionInput->InputName = FName(*ParameterName);
	FunctionInputs.Add(ParameterInput->ClipboardFunctionInput);
	return true;
}

void UNiagaraScriptConversionContext::Log(FString Message, ENiagaraMessageSeverity Severity, bool bIsVerbose /* = false*/)
{
	StackMessages.Add(FGenericConverterMessage(Message, Severity, bIsVerbose));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////	UNiagaraScriptConversionContextInput																	  /////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void UNiagaraScriptConversionContextInput::Init(UNiagaraClipboardFunctionInput* InClipboardFunctionInput, const FNiagaraTypeDefinition& InTargetTypeDefinition)
{
	ClipboardFunctionInput = InClipboardFunctionInput;
	TargetTypeDefinition = InTargetTypeDefinition;
}

bool UNiagaraScriptConversionContextInput::TryGetValueRangeFloat(float& OutMinValue, float& OutMaxValue)
{
	if (ClipboardFunctionInput == nullptr)
	{
		return false;
	}
	
	switch (ClipboardFunctionInput->ValueMode) {
	case ENiagaraClipboardFunctionInputValueMode::Data:
		if (ClipboardFunctionInput->Data != nullptr)
		{
			if (ClipboardFunctionInput->Data->IsA<UNiagaraDataInterfaceCurve>())
			{
				UNiagaraDataInterfaceCurve* CurveDI = Cast<UNiagaraDataInterfaceCurve>(ClipboardFunctionInput->Data);
				CurveDI->Curve.GetValueRange(OutMinValue, OutMaxValue);
				return true;
			}
		}
	case ENiagaraClipboardFunctionInputValueMode::Local:
		if (ClipboardFunctionInput->GetTypeDef() == FNiagaraTypeDefinition::GetFloatDef())
		{
			memcpy(&OutMaxValue, ClipboardFunctionInput->Local.GetData(), sizeof(float));
			memcpy(&OutMinValue, ClipboardFunctionInput->Local.GetData(), sizeof(float));
			return true;
		}
	default:
		return false;
	}
	return false;
}

bool UNiagaraScriptConversionContextInput::TryGetValueRangeVector(FVector& OutMinValue, FVector& OutMaxValue)
{
	if (ClipboardFunctionInput == nullptr)
	{
		return false;
	}

	switch (ClipboardFunctionInput->ValueMode) {
	case ENiagaraClipboardFunctionInputValueMode::Data:
		if (ClipboardFunctionInput->Data != nullptr)
		{
			if (ClipboardFunctionInput->Data->IsA<UNiagaraDataInterfaceVectorCurve>())
			{
				UNiagaraDataInterfaceVectorCurve* CurveDI = Cast<UNiagaraDataInterfaceVectorCurve>(ClipboardFunctionInput->Data);

				OutMaxValue = FVector(INT32_MIN);
				OutMinValue = FVector(INT32_MAX);
				float MinX, MaxX, MinY, MaxY, MinZ, MaxZ;
				CurveDI->XCurve.GetValueRange(MinX, MaxX);
				CurveDI->YCurve.GetValueRange(MinY, MaxY);
				CurveDI->ZCurve.GetValueRange(MinZ, MaxZ);
				OutMaxValue = OutMaxValue.ComponentMax(FVector(MaxX, MaxY, MaxZ));
				OutMinValue = OutMinValue.ComponentMin(FVector(MinX, MinY, MinZ));
				return true;
			}
		}
	case ENiagaraClipboardFunctionInputValueMode::Local:
		if (ClipboardFunctionInput->GetTypeDef() == FNiagaraTypeDefinition::GetVec3Def())
		{
			memcpy(&OutMaxValue, ClipboardFunctionInput->Local.GetData(), sizeof(FVector));
			memcpy(&OutMinValue, ClipboardFunctionInput->Local.GetData(), sizeof(FVector));
			return true;
		}
	default:
		return false;
	}
	return false;
}

bool UNiagaraScriptConversionContextInput::ValueIsAlwaysEqual(TArray<float> ConstValues)
{
	float MinFloat, MaxFloat = 0.0f; 
	if (TryGetValueRangeFloat(MinFloat, MaxFloat))
	{
		if (MinFloat != MaxFloat)
		{
			return false;
		}
		
		for (const float& ConstValue : ConstValues)
		{
			if (ConstValue == MinFloat)
			{
				return true;
			}
		}
		return false;
	}

	FVector MinVector, MaxVector = FVector(0.0f);
	if (TryGetValueRangeVector(MinVector, MaxVector))
	{
		if (MinVector != MaxVector)
		{
			return false;
		}

		for (const float& ConstValue : ConstValues)
		{
			if (FVector(ConstValue) == MinVector)
			{
				return true;
			}
		}
		return false;
	}

	return false;
}
