// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraStackGraphUtilitiesAdapterLibrary.h"
#include "AssetRegistryModule.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraEditorUtilities.h"
#include "Particles/ParticleSystem.h"
#include "Particles/Spawn/ParticleModuleSpawn.h"
#include "Particles/ParticleModuleRequired.h"
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

TArray<UParticleEmitter*> UFXConverterUtilitiesLibrary::GetCascadeSystemEmitters(const UParticleSystem* System)
{
	return System->Emitters;
}

UParticleLODLevel* UFXConverterUtilitiesLibrary::GetCascadeEmitterLodLevel(UParticleEmitter* Emitter, const int32 Idx)
{
	return Emitter->GetLODLevel(Idx);
}

TArray<UParticleModule*> UFXConverterUtilitiesLibrary::GetLodLevelModules(UParticleLODLevel* LodLevel)
{
	TArray<UParticleModule*> OutModules;
	OutModules.Append(LodLevel->Modules);
	OutModules.Append(LodLevel->SpawningModules);
	OutModules.Add(LodLevel->RequiredModule);
	OutModules.Add(LodLevel->SpawnModule);
	return OutModules;
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

FNiagaraScriptContextInput UFXConverterUtilitiesLibrary::CreateScriptInputFloat(float Value)
{
	UNiagaraClipboardFunctionInput* NewInput = UNiagaraClipboardEditorScriptingUtilities::CreateFloatLocalValueInput(GetTransientPackage(), FName(), false, false, Value);
	const FNiagaraTypeDefinition& TargetTypeDef = FNiagaraTypeDefinition::GetFloatDef();
	return FNiagaraScriptContextInput(NewInput, TargetTypeDef);
}

FNiagaraScriptContextInput UFXConverterUtilitiesLibrary::CreateScriptInputDI(UNiagaraScriptConversionContext* DynamicInputScriptContext, FString InputType)
{
	FName InputTypeName = FName(*InputType);
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
	return FNiagaraScriptContextInput(NewInput, TargetTypeDef);
}

FGuid UFXConverterUtilitiesLibrary::CreateSystemViewModelForSystem(UNiagaraSystem* InSystem)
{
	TSharedPtr<FNiagaraSystemViewModel> SystemViewModel = MakeShared<FNiagaraSystemViewModel>();
	FNiagaraSystemViewModelOptions SystemViewModelOptions = FNiagaraSystemViewModelOptions();
	SystemViewModelOptions.bCanAutoCompile = false;
	SystemViewModelOptions.bCanSimulate = false;
	SystemViewModelOptions.EditMode = ENiagaraSystemViewModelEditMode::SystemAsset;
	SystemViewModel->Initialize(*InSystem, SystemViewModelOptions);
	FGuid SystemViewModelGuid = FGuid::NewGuid();
	GuidToNiagaraSystemViewModelMap.Add(SystemViewModelGuid, SystemViewModel);
	return SystemViewModelGuid;
}

UNiagaraEmitterConversionContext* UFXConverterUtilitiesLibrary::AddEmptyEmitterToSystem(FGuid TargetSystemViewModelGuid, FString NewEmitterNameString)
{
	const TSharedPtr<FNiagaraSystemViewModel>& SystemViewModel = GuidToNiagaraSystemViewModelMap.FindChecked(TargetSystemViewModelGuid);

	UNiagaraEmitterFactoryNew* Factory = NewObject<UNiagaraEmitterFactoryNew>();
	UPackage* Pkg = CreatePackage(NULL, NULL);
	FName NewEmitterName = FName(*NewEmitterNameString);
	EObjectFlags Flags = RF_Public | RF_Standalone;
	UNiagaraEmitter* NewEmitter = CastChecked<UNiagaraEmitter>(Factory->FactoryCreateNew(UNiagaraEmitter::StaticClass(), Pkg, NewEmitterName, Flags, NULL, GWarn));
	TSharedPtr<FNiagaraEmitterHandleViewModel> NewEmitterHandleViewModel = SystemViewModel->AddEmitter(*NewEmitter);

	FGuid NiagaraEmitterHandleViewModelGuid = FGuid::NewGuid();
	GuidToNiagaraEmitterHandleViewModelMap.Add(NiagaraEmitterHandleViewModelGuid, NewEmitterHandleViewModel);
	UNiagaraEmitterConversionContext* EmitterConversionContext = NewObject<UNiagaraEmitterConversionContext>();
	EmitterConversionContext->Init(NewEmitter, NiagaraEmitterHandleViewModelGuid);
	return EmitterConversionContext;
}

void UFXConverterUtilitiesLibrary::AddNiagaraEmitterStackMessage(UNiagaraEmitterConversionContext* EmitterContext, FString Message)
{
	//@todo(ng) impl
	UE_LOG(LogTemp, Warning, TEXT("%s"), *Message);
}

void UFXConverterUtilitiesLibrary::AddNiagaraScriptStackMessage(UNiagaraScriptConversionContext* ScriptContext, FString Message)
{
	//@todo(ng) impl
	UE_LOG(LogTemp, Warning, TEXT("%s"), *Message);
}

UClass* UFXConverterUtilitiesLibrary::GetParticleModuleSpawnClass()
{
	return UParticleModuleSpawn::StaticClass();
}

UClass* UFXConverterUtilitiesLibrary::GetParticleModuleRequiredClass()
{
	return UParticleModuleRequired::StaticClass();
}

FParticleModuleSpawnProps UFXConverterUtilitiesLibrary::GetCascadeModuleSpawnProps(UParticleModuleSpawn* ParticleModuleSpawn)
{
	FParticleModuleSpawnProps ParticleModuleSpawnProps;

	ParticleModuleSpawnProps.Rate = ParticleModuleSpawn->Rate.Distribution;
	ParticleModuleSpawnProps.RateScale = ParticleModuleSpawn->RateScale.Distribution;
	ParticleModuleSpawnProps.ParticleBurstMethod = ParticleModuleSpawn->ParticleBurstMethod;
	ParticleModuleSpawnProps.BurstList = TArray<FParticleBurstBlueprint>(ParticleModuleSpawn->BurstList);
	ParticleModuleSpawnProps.BurstScale = ParticleModuleSpawn->BurstScale.Distribution;
	ParticleModuleSpawnProps.bApplyGlobalSpawnRateScale = ParticleModuleSpawn->bApplyGlobalSpawnRateScale;
	ParticleModuleSpawnProps.bProcessSpawnRate = ParticleModuleSpawn->bProcessSpawnRate;
	ParticleModuleSpawnProps.bProcessSpawnBurst = ParticleModuleSpawn->bProcessBurstList;
	return ParticleModuleSpawnProps;
}

FCascadeSpriteRendererProps UFXConverterUtilitiesLibrary::GetCascadeSpriteRendererProps(UParticleModuleRequired* ParticleModuleRequired)
{
	FCascadeSpriteRendererProps CascadeSpriteRendererProps;

	CascadeSpriteRendererProps.Material = ParticleModuleRequired->Material;
	CascadeSpriteRendererProps.ScreenAlignment = ParticleModuleRequired->ScreenAlignment;
	CascadeSpriteRendererProps.SubImages_Horizontal = ParticleModuleRequired->SubImages_Horizontal;
	CascadeSpriteRendererProps.SubImages_Vertical = ParticleModuleRequired->SubImages_Vertical;
	CascadeSpriteRendererProps.SortMode = ParticleModuleRequired->SortMode;
	CascadeSpriteRendererProps.InterpolationMethod = ParticleModuleRequired->InterpolationMethod;
	CascadeSpriteRendererProps.bRemoveHMDRoll = ParticleModuleRequired->bRemoveHMDRoll;
	CascadeSpriteRendererProps.MinFacingCameraBlendDistance = ParticleModuleRequired->MinFacingCameraBlendDistance;
	CascadeSpriteRendererProps.MaxFacingCameraBlendDistance = ParticleModuleRequired->MaxFacingCameraBlendDistance;
	CascadeSpriteRendererProps.CutoutTexture = ParticleModuleRequired->CutoutTexture;
	CascadeSpriteRendererProps.BoundingMode = ParticleModuleRequired->BoundingMode;
	CascadeSpriteRendererProps.OpacitySourceMode = ParticleModuleRequired->OpacitySourceMode;
	return CascadeSpriteRendererProps;
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

bool UFXConverterUtilitiesLibrary::GetIsDistributionOfType(
	UDistribution* Distribution
	, const EDistributionType TargetDistributionType
	, const EDistributionValueType TargetDistributionValueType
	, FText& OutStatus)
{
	const static UEnum* EDistributionTypeEnum = StaticEnum<EDistributionType>();
	const static UEnum* EDistributionValueTypeEnum = StaticEnum<EDistributionValueType>();

	EDistributionType DistributionType;
	EDistributionValueType DistributionValueType;
	GetDistributionType(Distribution, DistributionType, DistributionValueType);
	if (TargetDistributionType != DistributionType)
	{
		OutStatus = NSLOCTEXT("FXConverterLib", "DistributionTypeCheck", "Expected Distribution Type {0} but received Distribution Type {1}!");
		const FText TargetDistributionTypeText = EDistributionTypeEnum->GetDisplayNameTextByValue((int64)TargetDistributionType);
		const FText DistributionTypeText = EDistributionTypeEnum->GetDisplayNameTextByValue((int64)DistributionType);
		FText::Format(OutStatus, TargetDistributionTypeText, DistributionTypeText);
		return false;
	}
	else if (TargetDistributionValueType != DistributionValueType)
	{
		OutStatus = NSLOCTEXT("FXConverterLib", "DistributionValueTypeCheck", "Expected Distribution Value Type {0} but received Distribution Value Type {1}!");
		const FText TargetDistributionValueTypeText = EDistributionValueTypeEnum->GetDisplayNameTextByValue((int64)TargetDistributionValueType);
		const FText DistributionValueTypeText = EDistributionValueTypeEnum->GetDisplayNameTextByValue((int64)DistributionValueType);
		FText::Format(OutStatus, TargetDistributionValueTypeText, DistributionValueTypeText);
		return false;
	}
	return true;
}

void UFXConverterUtilitiesLibrary::GetFloatDistributionConstValues(UDistribution* Distribution, FText& OutStatus, float& OutConstFloat)
{
	if (GetIsDistributionOfType(Distribution, EDistributionType::Const, EDistributionValueType::Float, OutStatus))
	{
		OutConstFloat = CastChecked<UDistributionFloatConstant>(Distribution)->GetValue();
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////	UNiagaraEmitterConversionContext																		  /////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void UNiagaraEmitterConversionContext::AddScript(
	UNiagaraScriptConversionContext* ScriptConversionContext
	, EScriptExecutionCategory TargetScriptExecutionCategory
	, int32 TargetIndex)
{
	FName ExecutionCategoryName;
	FName ExecutionSubcategoryName;
	switch (TargetScriptExecutionCategory) {
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
		return;
	};

	const TSharedPtr<FNiagaraEmitterHandleViewModel>& TargetEmitterHandleViewModel = UFXConverterUtilitiesLibrary::GuidToNiagaraEmitterHandleViewModelMap.FindChecked(EmitterHandleViewModelGuid);

	TArray<UNiagaraStackItemGroup*> StackItemGroups;
	TargetEmitterHandleViewModel->GetEmitterStackViewModel()->GetRootEntry()->GetUnfilteredChildrenOfType<UNiagaraStackItemGroup>(StackItemGroups);
	UNiagaraStackItemGroup* const* StackItemGroup = StackItemGroups.FindByPredicate([ExecutionCategoryName, ExecutionSubcategoryName](const UNiagaraStackItemGroup* EmitterItemGroup) {
		return EmitterItemGroup->GetExecutionCategoryName() == ExecutionCategoryName && EmitterItemGroup->GetExecutionSubcategoryName() == ExecutionSubcategoryName; });

	if (StackItemGroup == nullptr)
	{
		return;
	}

	TArray<UNiagaraStackEntry*> TargetStackEntryArr;
	TargetStackEntryArr.Add(*StackItemGroup);

	UNiagaraClipboardContent* ClipboardContent = UNiagaraClipboardContent::Create();
	UNiagaraScript* NiagaraScript = ScriptConversionContext->GetScript();

	UNiagaraClipboardFunction* ClipboardFunction = UNiagaraClipboardFunction::CreateScriptFunction(ClipboardContent, "Function", NiagaraScript); //@todo(ng) proper name here
	ClipboardFunction->Inputs = ScriptConversionContext->GetClipboardFunctionInputs();
	ClipboardContent->Functions.Add(ClipboardFunction);

	FNiagaraEditorModule::Get().GetClipboard().SetClipboardContent(ClipboardContent);
	FText PasteWarning = FText();
	FNiagaraStackClipboardUtilities::PasteSelection(TargetStackEntryArr, PasteWarning);
	if (PasteWarning.IsEmpty() == false)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s"), *PasteWarning.ToString());
	}
}

void UNiagaraEmitterConversionContext::AddRenderer(UNiagaraRendererProperties* NewRendererProperties)
{
	const TSharedPtr<FNiagaraEmitterHandleViewModel>& TargetEmitterHandleViewModel = UFXConverterUtilitiesLibrary::GuidToNiagaraEmitterHandleViewModelMap.FindChecked(EmitterHandleViewModelGuid);

	TArray<UNiagaraStackItemGroup*> StackItemGroups;
	TargetEmitterHandleViewModel->GetEmitterStackViewModel()->GetRootEntry()->GetUnfilteredChildrenOfType<UNiagaraStackItemGroup>(StackItemGroups);
	UNiagaraStackItemGroup* const* StackItemGroup = StackItemGroups.FindByPredicate([](const UNiagaraStackItemGroup* EmitterItemGroup) {
		return EmitterItemGroup->GetExecutionCategoryName() == UNiagaraStackEntry::FExecutionCategoryNames::Render
			&& EmitterItemGroup->GetExecutionSubcategoryName() == UNiagaraStackEntry::FExecutionSubcategoryNames::Render; });

	if (StackItemGroup == nullptr)
	{
		return;
	}

	TArray<UNiagaraStackEntry*> TargetStackEntryArr;
	TargetStackEntryArr.Add(*StackItemGroup);

	UNiagaraClipboardContent* ClipboardContent = UNiagaraClipboardContent::Create();
	ClipboardContent->Renderers.Add(NewRendererProperties);
	FNiagaraEditorModule::Get().GetClipboard().SetClipboardContent(ClipboardContent);
	FText PasteWarning = FText();
	FNiagaraStackClipboardUtilities::PasteSelection(TargetStackEntryArr, PasteWarning);
	if (PasteWarning.IsEmpty() == false)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s"), *PasteWarning.ToString());
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////	UNiagaraScriptConversionContext																			  /////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void UNiagaraScriptConversionContext::Init(const FAssetData& InNiagaraScriptAssetData)
{
	Script = static_cast<UNiagaraScript*>(InNiagaraScriptAssetData.GetAsset());
}

bool UNiagaraScriptConversionContext::SetParameter(FString ParameterName, FNiagaraScriptContextInput ParameterInput)
{
	//@todo(ng) assert on ParameterInput.TypeDefinition
	ParameterInput.ClipboardFunctionInput->InputName = FName(*ParameterName);
	FunctionInputs.Add(ParameterInput.ClipboardFunctionInput);
	return true;
}
