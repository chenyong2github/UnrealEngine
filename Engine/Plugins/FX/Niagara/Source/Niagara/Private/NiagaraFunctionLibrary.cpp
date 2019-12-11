// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NiagaraFunctionLibrary.h"
#include "EngineGlobals.h"
#include "GameFramework/WorldSettings.h"
#include "Engine/Engine.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "ContentStreaming.h"
#include "Internationalization/Internationalization.h"

#include "NiagaraWorldManager.h"
#include "NiagaraDataInterfaceStaticMesh.h"
#include "NiagaraStats.h"

#define LOCTEXT_NAMESPACE "NiagaraFunctionLibrary"

//DECLARE_CYCLE_STAT(TEXT("FastDot4"), STAT_NiagaraFastDot4, STATGROUP_Niagara);

#if WITH_EDITOR
int32 GForceNiagaraSpawnAttachedSolo = 0;
static FAutoConsoleVariableRef CVarForceNiagaraSpawnAttachedSolo(
	TEXT("fx.ForceNiagaraSpawnAttachedSolo"),
	GForceNiagaraSpawnAttachedSolo,
	TEXT("If > 0 Niagara systems which are spawned attached will be force to spawn in solo mode for debugging.\n"),
	ECVF_Default
);
#endif


int32 GAllowFastPathFunctionLibrary = 0;
static FAutoConsoleVariableRef CVarAllowFastPathFunctionLibrary(
	TEXT("fx.AllowFastPathFunctionLibrary"),
	GAllowFastPathFunctionLibrary,
	TEXT("If > 0 Allow the graph to insert custom fastpath operations into the graph.\n"),
	ECVF_Default
);

TArray<FNiagaraFunctionSignature> UNiagaraFunctionLibrary::VectorVMOps;
TArray<FString> UNiagaraFunctionLibrary::VectorVMOpsHLSL;

UNiagaraFunctionLibrary::UNiagaraFunctionLibrary(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{
}


UNiagaraComponent* CreateNiagaraSystem(UNiagaraSystem* SystemTemplate, UWorld* World, AActor* Actor, bool bAutoDestroy, ENCPoolMethod PoolingMethod)
{
	UNiagaraComponent* NiagaraComponent;
	if (PoolingMethod == ENCPoolMethod::None)
	{
		NiagaraComponent = NewObject<UNiagaraComponent>((Actor ? Actor : (UObject*)World));
		NiagaraComponent->SetAsset(SystemTemplate);
		NiagaraComponent->bAutoActivate = false;
	}
	else
	{
		UNiagaraComponentPool* ComponentPool = FNiagaraWorldManager::Get(World)->GetComponentPool();
		NiagaraComponent = ComponentPool->CreateWorldParticleSystem(SystemTemplate, World, PoolingMethod);
	}	

	NiagaraComponent->SetAutoDestroy(bAutoDestroy);
	NiagaraComponent->bAllowAnyoneToDestroyMe = true;
	return NiagaraComponent;
}


/**
* Spawns a Niagara System at the specified world location/rotation
* @return			The spawned UNiagaraComponent
*/
UNiagaraComponent* UNiagaraFunctionLibrary::SpawnSystemAtLocation(const UObject* WorldContextObject, UNiagaraSystem* SystemTemplate, FVector SpawnLocation, FRotator SpawnRotation, FVector Scale, bool bAutoDestroy, bool bAutoActivate, ENCPoolMethod PoolingMethod, bool bPreCullCheck)
{
	UNiagaraComponent* PSC = NULL;
	if (SystemTemplate)
	{
		UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
		if (World != nullptr)
		{
			bool bShouldCull = false;
			if (bPreCullCheck)
			{
				FNiagaraWorldManager* WorldManager = FNiagaraWorldManager::Get(World);
				bShouldCull = WorldManager->ShouldPreCull(SystemTemplate, SpawnLocation);
			}

			if (!bShouldCull)
			{
				PSC = CreateNiagaraSystem(SystemTemplate, World, World->GetWorldSettings(), bAutoDestroy, PoolingMethod);
#if WITH_EDITORONLY_DATA
				PSC->bWaitForCompilationOnActivate = true;
#endif
				PSC->RegisterComponentWithWorld(World);

				PSC->SetAbsolute(true, true, true);
				PSC->SetWorldLocationAndRotation(SpawnLocation, SpawnRotation);
				PSC->SetRelativeScale3D(Scale);
				if (bAutoActivate)
				{
					PSC->Activate(true);
				}
			}
		}
	}
	return PSC;
}





/**
* Spawns a Niagara System attached to a component
* @return			The spawned UNiagaraComponent
*/
UNiagaraComponent* UNiagaraFunctionLibrary::SpawnSystemAttached(UNiagaraSystem* SystemTemplate, USceneComponent* AttachToComponent, FName AttachPointName, FVector Location, FRotator Rotation, EAttachLocation::Type LocationType, bool bAutoDestroy, bool bAutoActivate, ENCPoolMethod PoolingMethod, bool bPreCullCheck)
{
	UNiagaraComponent* PSC = nullptr;
	if (SystemTemplate)
	{
		if (AttachToComponent == NULL)
		{
			UE_LOG(LogScript, Warning, TEXT("UNiagaraFunctionLibrary::SpawnSystemAttached: NULL AttachComponent specified!"));
		}
		else
		{
			bool bShouldCull = false;
			if (bPreCullCheck)
			{
				FNiagaraWorldManager* WorldManager = FNiagaraWorldManager::Get(AttachToComponent->GetWorld());
				//TODO: For now using the attach parent location and ignoring the emitters relative location which is clearly going to be a bit wrong in some cases.
				bShouldCull = WorldManager->ShouldPreCull(SystemTemplate, AttachToComponent->GetComponentLocation());
			}

			if (!bShouldCull)
			{
				PSC = CreateNiagaraSystem(SystemTemplate, AttachToComponent->GetWorld(), AttachToComponent->GetOwner(), bAutoDestroy, PoolingMethod);
#if WITH_EDITOR
				if (GForceNiagaraSpawnAttachedSolo > 0)
				{
					PSC->SetForceSolo(true);
				}
#endif
				PSC->RegisterComponentWithWorld(AttachToComponent->GetWorld());

				PSC->AttachToComponent(AttachToComponent, FAttachmentTransformRules::KeepRelativeTransform, AttachPointName);
				if (LocationType == EAttachLocation::KeepWorldPosition)
				{
					PSC->SetWorldLocationAndRotation(Location, Rotation);
				}
				else
				{
					PSC->SetRelativeLocationAndRotation(Location, Rotation);
				}
				PSC->SetRelativeScale3D(FVector(1.f));

				if (bAutoActivate)
				{
					PSC->Activate();
				}
			}
		}
	}
	return PSC;
}

/**
* Spawns a Niagara System attached to a component
* @return			The spawned UNiagaraComponent
*/

UNiagaraComponent* UNiagaraFunctionLibrary::SpawnSystemAttached(
	UNiagaraSystem* SystemTemplate,
	USceneComponent* AttachToComponent,
	FName AttachPointName,
	FVector Location,
	FRotator Rotation,
	FVector Scale,
	EAttachLocation::Type LocationType,
	bool bAutoDestroy,
	ENCPoolMethod PoolingMethod,
	bool bAutoActivate,
	bool bPreCullCheck
)
{
	UNiagaraComponent* PSC = nullptr;
	if (SystemTemplate)
	{
		if (!AttachToComponent)
		{
			UE_LOG(LogScript, Warning, TEXT("UGameplayStatics::SpawnNiagaraEmitterAttached: NULL AttachComponent specified!"));
		}
		else
		{
			UWorld* const World = AttachToComponent->GetWorld();
			if (World && !World->IsNetMode(NM_DedicatedServer))
			{
				bool bShouldCull = false;
				if (bPreCullCheck)
				{
					FNiagaraWorldManager* WorldManager = FNiagaraWorldManager::Get(World);
					//TODO: For now using the attach parent location and ignoring the emitters relative location which is clearly going to be a bit wrong in some cases.
					bShouldCull = WorldManager->ShouldPreCull(SystemTemplate, AttachToComponent->GetComponentLocation());
				}

				if (!bShouldCull)
				{
					PSC = CreateNiagaraSystem(SystemTemplate, World, AttachToComponent->GetOwner(), bAutoDestroy, PoolingMethod);
				if (PSC)
				{
#if WITH_EDITOR
					if (GForceNiagaraSpawnAttachedSolo > 0)
					{
						PSC->SetForceSolo(true);
					}
#endif
					PSC->SetupAttachment(AttachToComponent, AttachPointName);

					if (LocationType == EAttachLocation::KeepWorldPosition)
					{
						const FTransform ParentToWorld = AttachToComponent->GetSocketTransform(AttachPointName);
						const FTransform ComponentToWorld(Rotation, Location, Scale);
						const FTransform RelativeTM = ComponentToWorld.GetRelativeTransform(ParentToWorld);
						PSC->SetRelativeLocation_Direct(RelativeTM.GetLocation());
						PSC->SetRelativeRotation_Direct(RelativeTM.GetRotation().Rotator());
						PSC->SetRelativeScale3D_Direct(RelativeTM.GetScale3D());
					}
					else
					{
						PSC->SetRelativeLocation_Direct(Location);
						PSC->SetRelativeRotation_Direct(Rotation);

						if (LocationType == EAttachLocation::SnapToTarget)
						{
							// SnapToTarget indicates we "keep world scale", this indicates we we want the inverse of the parent-to-world scale 
							// to calculate world scale at Scale 1, and then apply the passed in Scale
							const FTransform ParentToWorld = AttachToComponent->GetSocketTransform(AttachPointName);
							PSC->SetRelativeScale3D_Direct(Scale * ParentToWorld.GetSafeScaleReciprocal(ParentToWorld.GetScale3D()));
						}
						else
						{
							PSC->SetRelativeScale3D_Direct(Scale);
						}
					}

					PSC->RegisterComponentWithWorld(World);
					if (bAutoActivate)
					{
						PSC->Activate(true);
					}

					// Notify the texture streamer so that PSC gets managed as a dynamic component.
					IStreamingManager::Get().NotifyPrimitiveUpdated(PSC);
				}
				}
			}
		}
	}
	return PSC;
}

/**
* Set a constant in an emitter of a Niagara System
void UNiagaraFunctionLibrary::SetUpdateScriptConstant(UNiagaraComponent* Component, FName EmitterName, FName ConstantName, FVector Value)
{
	TArray<TSharedPtr<FNiagaraEmitterInstance>> &Emitters = Component->GetSystemInstance()->GetEmitters();

	for (TSharedPtr<FNiagaraEmitterInstance> &Emitter : Emitters)
	{		
		if(UNiagaraEmitter* PinnedProps = Emitter->GetProperties().Get())
		{
			FName CurName = *PinnedProps->EmitterName;
			if (CurName == EmitterName)
			{
				Emitter->GetProperties()->UpdateScriptProps.ExternalConstants.SetOrAdd(FNiagaraTypeDefinition::GetVec4Def(), ConstantName, Value);
				break;
			}
		}
	}
}
*/

void UNiagaraFunctionLibrary::OverrideSystemUserVariableStaticMeshComponent(UNiagaraComponent* NiagaraSystem, const FString& OverrideName, UStaticMeshComponent* StaticMeshComponent)
{
	if (!NiagaraSystem)
	{
		UE_LOG(LogNiagara, Warning, TEXT("NiagaraSystem in \"Set Niagara Static Mesh Component\" is NULL, OverrideName \"%s\" and StaticMeshComponent \"%s\", skipping."), *OverrideName, StaticMeshComponent ? *StaticMeshComponent->GetName() : TEXT("NULL"));
		return;
	}

	if (!StaticMeshComponent)
	{
		UE_LOG(LogNiagara, Warning, TEXT("StaticMeshComponent in \"Set Niagara Static Mesh Component\" is NULL, OverrideName \"%s\" and NiagaraSystem \"%s\", skipping."), *OverrideName, *NiagaraSystem->GetOwner()->GetName());
		return;
	}

	const FNiagaraParameterStore& OverrideParameters = NiagaraSystem->GetOverrideParameters();

	FNiagaraVariable Variable(FNiagaraTypeDefinition(UNiagaraDataInterfaceStaticMesh::StaticClass()), *OverrideName);
	
	int32 Index = OverrideParameters.IndexOf(Variable);
	if (Index == INDEX_NONE)
	{
		UE_LOG(LogNiagara, Warning, TEXT("Could not find index of variable \"%s\" in the OverrideParameters map of NiagaraSystem \"%s\"."), *OverrideName, *NiagaraSystem->GetOwner()->GetName());
		return;
	}
	
	UNiagaraDataInterfaceStaticMesh* StaticMeshInterface = Cast<UNiagaraDataInterfaceStaticMesh>(OverrideParameters.GetDataInterface(Index));
	if (!StaticMeshInterface)
	{
		UE_LOG(LogNiagara, Warning, TEXT("Did not find a matching Static Mesh Data Interface variable named \"%s\" in the User variables of NiagaraSystem \"%s\" ."), *OverrideName, *NiagaraSystem->GetOwner()->GetName());
		return;
	}

	StaticMeshInterface->SetSourceComponentFromBlueprints(StaticMeshComponent);
}

void UNiagaraFunctionLibrary::OverrideSystemUserVariableStaticMesh(UNiagaraComponent* NiagaraSystem, const FString& OverrideName, UStaticMesh* StaticMesh)
{
	if (!NiagaraSystem)
	{
		UE_LOG(LogNiagara, Warning, TEXT("NiagaraSystem in \"Set Niagara Static Mesh Component\" is NULL, OverrideName \"%s\" and StaticMesh \"%s\", skipping."), *OverrideName, StaticMesh ? *StaticMesh->GetName() : TEXT("NULL"));
		return;
	}

	if (!StaticMesh)
	{
		UE_LOG(LogNiagara, Warning, TEXT("StaticMesh in \"Set Niagara Static Mesh Component\" is NULL, OverrideName \"%s\" and NiagaraSystem \"%s\", skipping."), *OverrideName, *NiagaraSystem->GetOwner()->GetName());
		return;
	}

	const FNiagaraParameterStore& OverrideParameters = NiagaraSystem->GetOverrideParameters();

	FNiagaraVariable Variable(FNiagaraTypeDefinition(UNiagaraDataInterfaceStaticMesh::StaticClass()), *OverrideName);

	int32 Index = OverrideParameters.IndexOf(Variable);
	if (Index == INDEX_NONE)
	{
		UE_LOG(LogNiagara, Warning, TEXT("Could not find index of variable \"%s\" in the OverrideParameters map of NiagaraSystem \"%s\"."), *OverrideName, *NiagaraSystem->GetOwner()->GetName());
		return;
	}

	UNiagaraDataInterfaceStaticMesh* StaticMeshInterface = Cast<UNiagaraDataInterfaceStaticMesh>(OverrideParameters.GetDataInterface(Index));
	if (!StaticMeshInterface)
	{
		UE_LOG(LogNiagara, Warning, TEXT("Did not find a matching Static Mesh Data Interface variable named \"%s\" in the User variables of NiagaraSystem \"%s\" ."), *OverrideName, *NiagaraSystem->GetOwner()->GetName());
		return;
	}

	StaticMeshInterface->SetDefaultMeshFromBlueprints(StaticMesh);
}


void UNiagaraFunctionLibrary::OverrideSystemUserVariableSkeletalMeshComponent(UNiagaraComponent* NiagaraSystem, const FString& OverrideName, USkeletalMeshComponent* SkeletalMeshComponent)
{
	if (!NiagaraSystem)
	{
		UE_LOG(LogNiagara, Warning, TEXT("NiagaraSystem in \"Set Niagara Skeletal Mesh Component\" is NULL, OverrideName \"%s\" and SkeletalMeshComponent \"%s\", skipping."), *OverrideName, SkeletalMeshComponent ? *SkeletalMeshComponent->GetName() : TEXT("NULL"));
		return;
	}

	if (!SkeletalMeshComponent)
	{
		UE_LOG(LogNiagara, Warning, TEXT("SkeletalMeshComponent in \"Set Niagara Skeletal Mesh Component\" is NULL, OverrideName \"%s\" and NiagaraSystem \"%s\", skipping."), *OverrideName, *NiagaraSystem->GetOwner()->GetName());
		return;
	}

	const FNiagaraParameterStore& OverrideParameters = NiagaraSystem->GetOverrideParameters();

	FNiagaraVariable Variable(FNiagaraTypeDefinition(UNiagaraDataInterfaceSkeletalMesh::StaticClass()), *OverrideName);
	
	int32 Index = OverrideParameters.IndexOf(Variable);
	if (Index == INDEX_NONE)
	{
		UE_LOG(LogNiagara, Warning, TEXT("Could not find index of variable \"%s\" in the OverrideParameters map of NiagaraSystem \"%s\"."), *OverrideName, *NiagaraSystem->GetOwner()->GetName());
		return;
	}
	
	UNiagaraDataInterfaceSkeletalMesh* SkeletalMeshInterface = Cast<UNiagaraDataInterfaceSkeletalMesh>(OverrideParameters.GetDataInterface(Index));
	if (!SkeletalMeshInterface)
	{
		UE_LOG(LogNiagara, Warning, TEXT("Did not find a matching Skeletal Mesh Data Interface variable named \"%s\" in the User variables of NiagaraSystem \"%s\" ."), *OverrideName, *NiagaraSystem->GetOwner()->GetName());
		return;
	}

	SkeletalMeshInterface->SetSourceComponentFromBlueprints(SkeletalMeshComponent);
}

UNiagaraParameterCollectionInstance* UNiagaraFunctionLibrary::GetNiagaraParameterCollection(UObject* WorldContextObject, UNiagaraParameterCollection* Collection)
{
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	if (World != nullptr)
	{
		return FNiagaraWorldManager::Get(World)->GetParameterCollection(Collection);
	}
	return nullptr;
}


const TArray<FNiagaraFunctionSignature>& UNiagaraFunctionLibrary::GetVectorVMFastPathOps()
{
	if (GAllowFastPathFunctionLibrary == 0)
	{
		static TArray<FNiagaraFunctionSignature> Empty;
		return Empty;
	}

	InitVectorVMFastPathOps();
	return VectorVMOps;
}

bool UNiagaraFunctionLibrary::DefineFunctionHLSL(const FNiagaraFunctionSignature& FunctionSignature, FString& HlslOutput)
{
	InitVectorVMFastPathOps();

	const int32 i = VectorVMOps.IndexOfByKey(FunctionSignature);
	if ( i == INDEX_NONE )
	{
		return false;
	}

	HlslOutput += VectorVMOpsHLSL[i];
	return true;
}

const FName FastPathLibraryName(TEXT("FastPathLibrary"));
const FName FastPathDot4Name(TEXT("FastPathDot4"));
const FName FastPathTransformPositionName(TEXT("FastPathTransformPosition"));
const FName FastMatrixToQuaternionName(TEXT("FastMatrixToQuaternion"));
const FName FastPathEmitterLifeCycleName(TEXT("FastPathEmitterLifeCycle"));
const FName FastPathSpawnRateName(TEXT("FastPathSpawnRate"));
const FName FastPathSpawnBurstInstantaneousName(TEXT("FastPathSpawnBurstInstantaneous"));
const FName FastPathSolveVelocitiesAndForces(TEXT("FastPathSolveVelocitiesAndForces"));

struct FVectorKernelFastDot4
{
	static FNiagaraFunctionSignature GetFunctionSignature()
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FastPathDot4Name;
		Sig.OwnerName = FastPathLibraryName;
		Sig.bMemberFunction = false;
		Sig.bRequiresContext = false;
		Sig.SetDescription(LOCTEXT("FastPathDot4Desc", "Fast path for Vector4 dot product."));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec4Def(), TEXT("A")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec4Def(), TEXT("B")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Value")));
		return Sig;
	}

	static FString GetFunctionHLSL()
	{
		return FString();
	}

	static void Exec(FVectorVMContext& Context)
	{
		//SCOPE_CYCLE_COUNTER(STAT_NiagaraFastDot4);

		VectorVM::FExternalFuncInputHandler<float>InVecA[4] =
		{
			VectorVM::FExternalFuncInputHandler<float>(Context), 
			VectorVM::FExternalFuncInputHandler<float>(Context), 
			VectorVM::FExternalFuncInputHandler<float>(Context),
			VectorVM::FExternalFuncInputHandler<float>(Context)
		};

		VectorVM::FExternalFuncInputHandler<float>InVecB[4] =
		{
			VectorVM::FExternalFuncInputHandler<float>(Context),
			VectorVM::FExternalFuncInputHandler<float>(Context),
			VectorVM::FExternalFuncInputHandler<float>(Context),
			VectorVM::FExternalFuncInputHandler<float>(Context)
		};


		VectorVM::FExternalFuncRegisterHandler<float> OutValue(Context);

		VectorRegister* RESTRICT AX = (VectorRegister *)InVecA[0].GetDest();
		VectorRegister* RESTRICT AY = (VectorRegister *)InVecA[1].GetDest();
		VectorRegister* RESTRICT AZ = (VectorRegister *)InVecA[2].GetDest();
		VectorRegister* RESTRICT AW = (VectorRegister *)InVecA[3].GetDest();

		VectorRegister* RESTRICT BX = (VectorRegister *)InVecB[0].GetDest();
		VectorRegister* RESTRICT BY = (VectorRegister *)InVecB[1].GetDest();
		VectorRegister* RESTRICT BZ = (VectorRegister *)InVecB[2].GetDest();
		VectorRegister* RESTRICT BW = (VectorRegister *)InVecB[3].GetDest();
		VectorRegister* RESTRICT Out = (VectorRegister *)OutValue.GetDest();

		const int32 Loops = Context.GetNumLoops<4>();
		for (int32 i = 0; i < Loops; ++i)
		{

			VectorRegister AVX0= VectorLoadAligned(&AX[i]);
			VectorRegister AVY0= VectorLoadAligned(&AY[i]);
			VectorRegister AVZ0= VectorLoadAligned(&AZ[i]);
			VectorRegister AVW0= VectorLoadAligned(&AW[i]);
			VectorRegister BVX0= VectorLoadAligned(&BX[i]);
			VectorRegister BVY0= VectorLoadAligned(&BY[i]);
			VectorRegister BVZ0= VectorLoadAligned(&BZ[i]);
			VectorRegister BVW0= VectorLoadAligned(&BW[i]);

			/*
				 R[19] = :mul(R[21], R[25]);
				 R[21] = :mad(R[20], R[24], R[19]);
				 R[19] = :mad(R[22], R[26], R[21]);
				 R[20] = :mad(R[23], R[27], R[19]);
			*/
			VectorRegister AMBX0	= VectorMultiply(AVX0, BVX0);
			VectorRegister AMBXY0= VectorMultiplyAdd(AVY0, BVY0, AMBX0);
			VectorRegister AMBXYZ0= VectorMultiplyAdd(AVZ0, BVZ0, AMBXY0);
			VectorRegister AMBXYZW0= VectorMultiplyAdd(AVW0, BVW0, AMBXYZ0);
			VectorStoreAligned(AMBXYZW0, &Out[i]) ;

			/*
			(float Sum = 0.0f;
			for (int32 VecIdx = 0; VecIdx < 4; VecIdx++)
			{
				Sum += InVecA[VecIdx].GetAndAdvance() * InVecB[VecIdx].GetAndAdvance();
			}

			*OutValue.GetDestAndAdvance() = Sum;
			*/
		}
	}
};

struct FVectorKernelFastTransformPosition
{
	static FNiagaraFunctionSignature GetFunctionSignature()
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FastPathTransformPositionName;
		Sig.OwnerName = FastPathLibraryName;
		Sig.bMemberFunction = false;
		Sig.bRequiresContext = false;
		Sig.SetDescription(LOCTEXT("FastPathTransformPositionDesc", "Fast path for Matrix4 transforming a Vector3 position"));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("Mat")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Position")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("PositionTransformed")));
		return Sig;
	}

	static FString GetFunctionHLSL()
	{
		return FString();
	}

	static void Exec(FVectorVMContext& Context)
	{
#if 0
		TArray<VectorVM::FExternalFuncInputHandler<float>, TInlineAllocator<16>> InMatrix;
		for (int i = 0; i < 16; i++)
		{
			InMatrix.Emplace(Context);
		}

		TArray<VectorVM::FExternalFuncInputHandler<float>, TInlineAllocator<16>> InVec;
		for (int i = 0; i < 3; i++)
		{
			InVec.Emplace(Context);
		}

		TArray<VectorVM::FExternalFuncInputHandler<float>, TInlineAllocator<16>> OutVec;
		for (int i = 0; i < 3; i++)
		{
			OutVec.Emplace(Context);
		}

		/*
	29	| R[19] = :mul(R[20], R[26]);
	30	| R[27] = :mul(R[21], R[26]);
	31	| R[28] = :mul(R[22], R[26]);
	32	| R[29] = :mul(R[23], R[26]);
	33	| R[26] = :mul(R[20], R[25]);
	34	| R[30] = :mul(R[21], R[25]);
	35	| R[31] = :mul(R[22], R[25]);
	36	| R[32] = :mul(R[23], R[25]);
	37	| R[25] = :mul(R[20], R[24]);
	38	| R[33] = :mul(R[21], R[24]);
	39	| R[34] = :mul(R[22], R[24]);
	40	| R[35] = :mul(R[23], R[24]);
	41	| R[24] = :add(R[26], R[25]);
	42	| R[25] = :add(R[30], R[33]);
	43	| R[26] = :add(R[31], R[34]);
	44	| R[30] = :add(R[32], R[35]);
	45	| R[31] = :add(R[19], R[24]);
	46	| R[19] = :add(R[27], R[25]);
	47	| R[24] = :add(R[28], R[26]);
	48	| R[25] = :add(R[29], R[30]);
	49	| R[26] = :add(R[20], R[31]);
	50	| R[20] = :add(R[21], R[19]);
	51	| R[19] = :add(R[22], R[24]);
		*/
		/*float* RESTRICT M00 = (float *)InMatrix[0].GetDest();
		float* RESTRICT M01 = (float *)InMatrix[1].GetDest();
		float* RESTRICT M02 = (float *)InMatrix[2].GetDest();
		float* RESTRICT M03 = (float *)InMatrix[3].GetDest();
		float* RESTRICT M10 = (float *)InMatrix[4].GetDest();
		float* RESTRICT M11 = (float *)InMatrix[5].GetDest();
		float* RESTRICT M12 = (float *)InMatrix[6].GetDest();
		float* RESTRICT M13 = (float *)InMatrix[7].GetDest(); 
		float* RESTRICT M20 = (float *)InMatrix[8].GetDest();
		float* RESTRICT M21 = (float *)InMatrix[9].GetDest();
		float* RESTRICT M22 = (float *)InMatrix[10].GetDest();
		float* RESTRICT M23 = (float *)InMatrix[11].GetDest();
		float* RESTRICT M30 = (float *)InMatrix[12].GetDest();
		float* RESTRICT M31 = (float *)InMatrix[13].GetDest();
		float* RESTRICT M32 = (float *)InMatrix[14].GetDest();
		float* RESTRICT M33 = (float *)InMatrix[15].GetDest();*/
		TArray<float* RESTRICT, TInlineAllocator<16>> InMatrixDest;
		for (int i = 0; i < 16; i++)
		{
			InMatrixDest.Emplace((float *)InMatrix[i].GetDest());
		}

		float* RESTRICT BX = (float *)InVec[0].GetDest();
		float* RESTRICT BY = (float *)InVec[1].GetDest();
		float* RESTRICT BZ = (float *)InVec[2].GetDest();
		
		float* RESTRICT OutValueX = (float *)OutVec[0].GetDest();
		float* RESTRICT OutValueY = (float *)OutVec[1].GetDest();
		float* RESTRICT OutValueZ = (float *)OutVec[2].GetDest();
		
		int32 LoopInstances = Align(Context.NumInstances, 1) /1;
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			/*FMatrix LocalMat;
			for (int32 Row = 0; Row < 4; Row++)
			{
				for (int32 Column = 0; Column < 4; Column++)
				{
					LocalMat.M[Row][Column] = *InMatrixDest[Row * 4 + Column]++;
				}
			}*/

			for (int32 Row = 0; Row < 4; Row++)
			{
				for (int32 Column = 0; Column < 4; Column++)
				{
					*InMatrixDest[Row * 4 + Column]++;
				}
			}
			*BX++; *BY++; *BZ++;

			//FVector LocalVec(*BX++, *BY++, *BZ++);

			//FVector LocalOutVec = LocalMat.TransformPosition(LocalVec);

			//*(OutValueX)++ = LocalOutVec.X;
			//*(OutValueY)++ = LocalOutVec.Y;
			//*(OutValueZ)++ = LocalOutVec.Z;
		}
#endif
	}
};

struct FVectorKernelFastMatrixToQuaternion
{
	static FNiagaraFunctionSignature GetFunctionSignature()
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FastMatrixToQuaternionName;
		Sig.OwnerName = FastPathLibraryName;
		Sig.bMemberFunction = false;
		Sig.bRequiresContext = false;
		Sig.SetDescription(LOCTEXT("FastMatrixToQuaternionDesc", "Fast path for Matrix4 to Quaternion"));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("Mat")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetQuatDef(), TEXT("Quat")));
		return Sig;
	}

	static FString GetFunctionHLSL()
	{
		static FString FunctionHLSL = TEXT(R"(
	void FastMatrixToQuaternion_FastPathLibrary(float4x4 Mat, out float4 Quat)
	{
		float tr = Mat[0][0] + Mat[1][1] + Mat[2][2];
		if (tr > 0.0f)
		{
			float InvS = rsqrt(tr + 1.f);
			float s = 0.5f * InvS;

			Quat.x = (Mat[2][1] - Mat[1][2]) * s;
			Quat.y = (Mat[0][2] - Mat[2][0]) * s;
			Quat.z = (Mat[1][0] - Mat[0][1]) * s;
			Quat.w = 0.5f * rcp(InvS);
		}
		else if ( (Mat[0][0] > Mat[1][1]) && (Mat[0][0] > Mat[2][2]) )
		{
			float s = Mat[0][0] - Mat[1][1] - Mat[2][2] + 1.0f;
			float InvS = rsqrt(s);
			s = 0.5f * InvS;

			Quat.x = 0.5f * rcp(InvS);
			Quat.y = (Mat[1][0] + Mat[0][1]) * s;
			Quat.z = (Mat[2][0] + Mat[0][2]) * s;
			Quat.w = (Mat[2][1] - Mat[1][2]) * s;
		}
		else if ( Mat[1][1] > Mat[2][2] )
		{
			float s = Mat[1][1] - Mat[2][2] - Mat[0][0] + 1.0f;
			float InvS = rsqrt(s);
			s = 0.5f * InvS;

			Quat.x = (Mat[0][1] + Mat[1][0]) * s;
			Quat.y = 0.5f * rcp(InvS);
			Quat.z = (Mat[2][1] + Mat[1][2]) * s;
			Quat.w = (Mat[0][2] - Mat[2][0]) * s;

		}
		else
		{
			float s = Mat[2][2] - Mat[0][0] - Mat[1][1] + 1.0f;
			float InvS = rsqrt(s);
			s = 0.5f * InvS;

			Quat.x = (Mat[0][2] + Mat[2][0]) * s;
			Quat.y = (Mat[1][2] + Mat[2][1]) * s;
			Quat.z = 0.5f * rcp(InvS);
			Quat.w = (Mat[1][0] - Mat[0][1]) * s;
		}
	}
)");

		return FunctionHLSL;
	}

	static void Exec(FVectorVMContext& Context)
	{
		TArray<VectorVM::FExternalFuncInputHandler<float>, TInlineAllocator<16>> InMatrix;
		for (int i=0; i < 16; i++)
		{
			InMatrix.Emplace(Context);
		}

		TArray<VectorVM::FExternalFuncRegisterHandler<float>, TInlineAllocator<4>> OutQuat;
		for (int i=0; i < 4; ++i)
		{
			OutQuat.Emplace(Context);
		}

		for ( int32 i=0; i < Context.GetNumInstances(); ++i )
		{
			FMatrix Mat;
			for ( int32 j=0; j < 16; ++j )
			{
				Mat.M[j & 3][j >> 2] = InMatrix[j].GetAndAdvance();
			}

			FQuat Quat(Mat);
			*OutQuat[0].GetDestAndAdvance() = Quat.X;
			*OutQuat[1].GetDestAndAdvance() = Quat.Y;
			*OutQuat[2].GetDestAndAdvance() = Quat.Z;
			*OutQuat[3].GetDestAndAdvance() = Quat.W;
		}
	}
};

struct FVectorKernel_EmitterLifeCycle
{
	static FNiagaraFunctionSignature GetFunctionSignature()
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FastPathEmitterLifeCycleName;
		Sig.OwnerName = FastPathLibraryName;
		Sig.bMemberFunction = false;
		Sig.bRequiresContext = false;
		Sig.SetDescription(LOCTEXT("FastPathEmitterLifeCycleDesc", "Fast path for life cycle"));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("EngineDeltaTime")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("EngineNumParticles")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetExecutionStateEnum(), TEXT("ScalabilityEmitterExecutionState")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetExecutionStateEnum(), TEXT("SystemExecutionState")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetExecutionStateSouceEnum(), TEXT("SystemExecutionStateSource")));

		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("ModuleNextLoopDuration")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("ModuleNextLoopDelay")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("ModuleDurationRecalcEachLoop")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("ModuleDelayFirstLoopOnly")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("ModuleMaxLoopCount")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("ModuleAutoComplete")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("ModuleCompleteOnInactive")));

		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetExecutionStateEnum(), TEXT("EmitterExecutionState")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetExecutionStateSouceEnum(), TEXT("EmitterExecutionStateSource")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("EmitterAge")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("EmitterLoopedAge")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("EmitterCurrentLoopDuration")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("EmitterCurrentLoopDelay")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("EmitterLoopCount")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("EmitterNormalizedLoopAge")));

		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetExecutionStateEnum(), TEXT("EmitterExecutionState")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetExecutionStateSouceEnum(), TEXT("EmitterExecutionStateSource")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("EmitterAge")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("EmitterLoopedAge")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("EmitterCurrentLoopDuration")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("EmitterCurrentLoopDelay")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("EmitterLoopCount")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("EmitterNormalizedLoopAge")));

		return Sig;
	}

	static FString GetFunctionHLSL()
	{
		return FString();
	}

	static void Exec(FVectorVMContext& Context)
	{
		VectorVM::FExternalFuncInputHandler<float> InEngineDeltaTime(Context);
		VectorVM::FExternalFuncInputHandler<int>   InEngineNumParticles(Context);
		VectorVM::FExternalFuncInputHandler<ENiagaraExecutionState> InScalabilityEmitterExecutionState(Context);
		VectorVM::FExternalFuncInputHandler<ENiagaraExecutionState> InSystemExecutionState(Context);
		VectorVM::FExternalFuncInputHandler<ENiagaraExecutionStateSource> InSystemExecutionStateSource(Context);

		VectorVM::FExternalFuncInputHandler<float> InModuleNextLoopDuration(Context);
		VectorVM::FExternalFuncInputHandler<float> InModuleNextLoopDelay(Context);
		VectorVM::FExternalFuncInputHandler<bool>  InModuleDurationRecalcEachLoop(Context);
		VectorVM::FExternalFuncInputHandler<bool>  InModuleDelayFirstLoopOnly(Context);
		VectorVM::FExternalFuncInputHandler<int>   InModuleMaxLoopCount(Context);
		VectorVM::FExternalFuncInputHandler<bool>  InModuleAutoComplete(Context);
		VectorVM::FExternalFuncInputHandler<bool>  InModuleCompleteOnInactive(Context);

		VectorVM::FExternalFuncInputHandler<ENiagaraExecutionState> InEmitterExecutionState(Context);
		VectorVM::FExternalFuncInputHandler<ENiagaraExecutionStateSource> InEmitterExecutionStateSource(Context);
		VectorVM::FExternalFuncInputHandler<float> InEmitterAge(Context);
		VectorVM::FExternalFuncInputHandler<float> InEmitterLoopedAge(Context);
		VectorVM::FExternalFuncInputHandler<float> InEmitterCurrentLoopDuration(Context);
		VectorVM::FExternalFuncInputHandler<float> InEmitterCurrentLoopDelay(Context);
		VectorVM::FExternalFuncInputHandler<int>   InEmitterLoopCount(Context);
		VectorVM::FExternalFuncInputHandler<float> InEmitterNormalizedLoopAge(Context);

		VectorVM::FExternalFuncRegisterHandler<ENiagaraExecutionState> OutEmitterExecutionState(Context);
		VectorVM::FExternalFuncRegisterHandler<ENiagaraExecutionStateSource> OutEmitterExecutionStateSource(Context);
		VectorVM::FExternalFuncRegisterHandler<float> OutEmitterAge(Context);
		VectorVM::FExternalFuncRegisterHandler<float> OutEmitterLoopedAge(Context);
		VectorVM::FExternalFuncRegisterHandler<float> OutEmitterCurrentLoopDuration(Context);
		VectorVM::FExternalFuncRegisterHandler<float> OutEmitterCurrentLoopDelay(Context);
		VectorVM::FExternalFuncRegisterHandler<int>   OutEmitterLoopCount(Context);
		VectorVM::FExternalFuncRegisterHandler<float> OutEmitterNormalizedLoopAge(Context);

		for (int i = 0; i < Context.GetNumInstances(); ++i)
		{
			const float EngineDeltaTime = InEngineDeltaTime.GetAndAdvance();
			const int EngineNumParticles = InEngineNumParticles.GetAndAdvance();

			const ENiagaraExecutionState ScalabilityEmitterExecutionState = InScalabilityEmitterExecutionState.GetAndAdvance();

			const ENiagaraExecutionState SystemExecutionState = InSystemExecutionState.GetAndAdvance();
			const ENiagaraExecutionStateSource SystemExecutionStateSource = InSystemExecutionStateSource.GetAndAdvance();

			const float ModuleNextLoopDuration = InModuleNextLoopDuration.GetAndAdvance();
			const float ModuleNextLoopDelay = InModuleNextLoopDelay.GetAndAdvance();
			const bool ModuleDurationRecalcEachLoop = InModuleDurationRecalcEachLoop.GetAndAdvance();
			const bool ModuleDelayFirstLoopOnly = InModuleDelayFirstLoopOnly.GetAndAdvance();
			const int ModuleMaxLoopCount = InModuleMaxLoopCount.GetAndAdvance();
			const bool ModuleAutoComplete = InModuleAutoComplete.GetAndAdvance();
			const bool ModuleCompleteOnInactive = InModuleCompleteOnInactive.GetAndAdvance();

			ENiagaraExecutionState EmitterExecutionState = InEmitterExecutionState.GetAndAdvance();
			ENiagaraExecutionStateSource EmitterExecutionStateSource = InEmitterExecutionStateSource.GetAndAdvance();
			float EmitterAge = InEmitterAge.GetAndAdvance();
			float EmitterLoopedAge = InEmitterLoopedAge.GetAndAdvance();
			float EmitterCurrentLoopDuration = InEmitterCurrentLoopDuration.GetAndAdvance();
			float EmitterCurrentLoopDelay = InEmitterCurrentLoopDelay.GetAndAdvance();
			int EmitterLoopCount = InEmitterLoopCount.GetAndAdvance();
			float EmitterNormalizedLoopAge = InEmitterNormalizedLoopAge.GetAndAdvance();

			// Skip disabled emitters
			if ( EmitterExecutionState != ENiagaraExecutionState::Disabled )
			{
				// Initialize parameters
				if (EmitterAge == 0.0f)
				{
					EmitterLoopedAge = -ModuleNextLoopDelay;
					EmitterCurrentLoopDuration = ModuleNextLoopDuration;
					EmitterCurrentLoopDelay = ModuleNextLoopDelay;
				}

				// Handle emitter looping
				EmitterAge += EngineDeltaTime;
				EmitterLoopedAge += EngineDeltaTime;
				const int32 LoopsPerformed = FMath::FloorToInt(EmitterLoopedAge / EmitterCurrentLoopDuration);
				if (LoopsPerformed > 0)
				{
					EmitterLoopedAge -= float(LoopsPerformed) * EmitterCurrentLoopDuration;
					EmitterLoopCount += LoopsPerformed;

					if (ModuleDurationRecalcEachLoop)
					{
						EmitterCurrentLoopDuration = ModuleNextLoopDuration;
					}
					if (ModuleDelayFirstLoopOnly)
					{
						EmitterCurrentLoopDelay = 0.0f;
					}
					EmitterNormalizedLoopAge = EmitterLoopedAge / EmitterCurrentLoopDuration;
				}

				// Set emitter state from scalability (if allowed)
				if ( EmitterExecutionStateSource <= ENiagaraExecutionStateSource::Scalability)
				{
					EmitterExecutionState = ScalabilityEmitterExecutionState;
					EmitterExecutionStateSource = ENiagaraExecutionStateSource::Scalability;
				}

				// Exceeded maximum loops?
				if (ModuleMaxLoopCount > 0 && EmitterLoopCount >= ModuleMaxLoopCount)
				{
					if (EmitterExecutionStateSource <= ENiagaraExecutionStateSource::Internal)
					{
						EmitterExecutionState = ENiagaraExecutionState::Inactive;
						EmitterExecutionStateSource = ENiagaraExecutionStateSource::Internal;
					}
				}

				// Are we complete?
				if (EmitterExecutionState != ENiagaraExecutionState::Active && (ModuleCompleteOnInactive || (EngineNumParticles == 0 && ModuleAutoComplete)))
				{
					if ( EmitterExecutionStateSource <= ENiagaraExecutionStateSource::InternalCompletion )
					{
						EmitterExecutionState = ENiagaraExecutionState::Complete;
						EmitterExecutionStateSource = ENiagaraExecutionStateSource::InternalCompletion;
					}
				}
			}

			// Set values
			*OutEmitterExecutionState.GetDestAndAdvance() = EmitterExecutionState;
			*OutEmitterExecutionStateSource.GetDestAndAdvance() = EmitterExecutionStateSource;
			*OutEmitterAge.GetDestAndAdvance() = EmitterAge;
			*OutEmitterLoopedAge.GetDestAndAdvance() = EmitterLoopedAge;
			*OutEmitterCurrentLoopDuration.GetDestAndAdvance() = EmitterCurrentLoopDuration;
			*OutEmitterCurrentLoopDelay.GetDestAndAdvance() = EmitterCurrentLoopDelay;
			*OutEmitterLoopCount.GetDestAndAdvance() = EmitterLoopCount;
			*OutEmitterNormalizedLoopAge.GetDestAndAdvance() = EmitterNormalizedLoopAge;
		}
	}
};

struct FVectorKernel_SpawnRate
{
	static FNiagaraFunctionSignature GetFunctionSignature()
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FastPathSpawnRateName;
		Sig.OwnerName = FastPathLibraryName;
		Sig.bMemberFunction = false;
		Sig.bRequiresContext = false;
		Sig.SetDescription(LOCTEXT("FastPathSpawnRateDesc", "Fast path for spawn rate"));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("EngineDeltaTime")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("ModuleSpawnRate")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("ScalabilityEmitterSpawnCountScale")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("EngineEmitterSpawnCountScale")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("EmitterModuleSpawnRemainder")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("EmitterLoopedAge")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("EmitterSpawnGroup")));

		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("SpawningCanEverSpawn")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("EmitterModuleSpawnRemainder")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("EmitterModuleSpawnInfoCount")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("EmitterModuleSpawnInfoInterpStartDt")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("EmitterModuleSpawnInfoIntervalDt")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("EmitterModuleSpawnInfoSpawnGroup")));

		return Sig;
	}

	static FString GetFunctionHLSL()
	{
		return FString();
	}

	static void Exec(FVectorVMContext& Context)
	{
		VectorVM::FExternalFuncInputHandler<float> InEngineDeltaTime(Context);
		VectorVM::FExternalFuncInputHandler<float> InModuleSpawnRate(Context);
		VectorVM::FExternalFuncInputHandler<float> InScalabilityEmitterSpawnCountScale(Context);
		VectorVM::FExternalFuncInputHandler<float> InEngineEmitterSpawnCountScale(Context);
		VectorVM::FExternalFuncInputHandler<float> InEmitterModuleSpawnRemainder(Context);
		VectorVM::FExternalFuncInputHandler<float> InEmitterLoopedAge(Context);
		VectorVM::FExternalFuncInputHandler<int32> InEmitterSpawnGroup(Context);

		VectorVM::FExternalFuncRegisterHandler<bool> OutSpawningCanEverSpawn(Context);
		VectorVM::FExternalFuncRegisterHandler<float> OutEmitterModuleSpawnRemainder(Context);
		VectorVM::FExternalFuncRegisterHandler<int32> OutEmitterModuleSpawnInfoCount(Context);
		VectorVM::FExternalFuncRegisterHandler<float> OutEmitterModuleSpawnInfoInterpStartDt(Context);
		VectorVM::FExternalFuncRegisterHandler<float> OutEmitterModuleSpawnInfoIntervalDt(Context);
		VectorVM::FExternalFuncRegisterHandler<int32> OutEmitterModuleSpawnInfoSpawnGroup(Context);

		for (int i = 0; i < Context.GetNumInstances(); ++i)
		{
			// Gather values
			float EngineDeltaTime = InEngineDeltaTime.GetAndAdvance();
			float ModuleSpawnRate = InModuleSpawnRate.GetAndAdvance();
			float ScalabilityEmitterSpawnCountScale = InScalabilityEmitterSpawnCountScale.GetAndAdvance();
			float EngineEmitterSpawnCountScale = InEngineEmitterSpawnCountScale.GetAndAdvance();
			float EmitterModuleSpawnRemainder = InEmitterModuleSpawnRemainder.GetAndAdvance();
			float EmitterLoopedAge = InEmitterLoopedAge.GetAndAdvance();
			int32 EmitterSpawnGroup = InEmitterSpawnGroup.GetAndAdvance();

			//
			float LocalModuleSpawnRate = ModuleSpawnRate * ScalabilityEmitterSpawnCountScale * EngineEmitterSpawnCountScale;
			float LocalModuleIntervalDT = 1.0f / LocalModuleSpawnRate;
			float LocalModuleInterpStartDT = LocalModuleIntervalDT * (1.0f - EmitterModuleSpawnRemainder);

			FNiagaraSpawnInfo SpawnInfo;
			if (EmitterLoopedAge > 0.0f)
			{
				float fSpawnCount = (LocalModuleSpawnRate * EngineDeltaTime) + EmitterModuleSpawnRemainder;
				int32 LocalModuleSpawnCount = FMath::FloorToInt(fSpawnCount);
				EmitterModuleSpawnRemainder = fSpawnCount - float(LocalModuleSpawnCount);

				SpawnInfo.Count = LocalModuleSpawnCount;
				SpawnInfo.InterpStartDt = LocalModuleInterpStartDT;
				SpawnInfo.IntervalDt = LocalModuleIntervalDT;
				SpawnInfo.SpawnGroup = EmitterSpawnGroup;
			}

			// Write values
			*OutEmitterModuleSpawnInfoCount.GetDestAndAdvance() = SpawnInfo.Count;
			*OutEmitterModuleSpawnInfoInterpStartDt.GetDestAndAdvance() = SpawnInfo.InterpStartDt;
			*OutEmitterModuleSpawnInfoIntervalDt.GetDestAndAdvance() = SpawnInfo.IntervalDt;
			*OutEmitterModuleSpawnInfoSpawnGroup.GetDestAndAdvance() = SpawnInfo.SpawnGroup;
			*OutSpawningCanEverSpawn.GetDestAndAdvance() = true;
			*OutEmitterModuleSpawnRemainder.GetDestAndAdvance() = EmitterModuleSpawnRemainder;
		}
	}
};

struct FVectorKernel_SpawnBurstInstantaneous
{
	static FNiagaraFunctionSignature GetFunctionSignature()
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FastPathSpawnBurstInstantaneousName;
		Sig.OwnerName = FastPathLibraryName;
		Sig.bMemberFunction = false;
		Sig.bRequiresContext = false;
		Sig.SetDescription(LOCTEXT("FastPathSpawnBurstInstantaneous", "Fast path for spawn burst instantaneous"));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("EngineDeltaTime")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("ScalabilityEmitterSpawnCountScale")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("EmitterLoopedAge")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("ModuleSpawnTime")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("ModuleSpawnCount")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("ModuleSpawnGroup")));

		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("SpawningCanEverSpawn")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("EmitterModuleSpawnInfoCount")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("EmitterModuleSpawnInfoInterpStartDt")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("EmitterModuleSpawnInfoIntervalDt")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("EmitterModuleSpawnInfoSpawnGroup")));

		return Sig;
	}

	static FString GetFunctionHLSL()
	{
		return FString();
	}

	static void Exec(FVectorVMContext& Context)
	{
		VectorVM::FExternalFuncInputHandler<float> InEngineDeltaTime(Context);
		VectorVM::FExternalFuncInputHandler<float> InScalabilityEmitterSpawnCountScale(Context);
		VectorVM::FExternalFuncInputHandler<float> InEmitterLoopedAge(Context);
		VectorVM::FExternalFuncInputHandler<float> InModuleSpawnTime(Context);
		VectorVM::FExternalFuncInputHandler<int32> InModuleSpawnCount(Context);
		VectorVM::FExternalFuncInputHandler<int32> InModuleSpawnGroup(Context);

		VectorVM::FExternalFuncRegisterHandler<bool> OutSpawningCanEverSpawn(Context);
		VectorVM::FExternalFuncRegisterHandler<int32> OutEmitterModuleSpawnInfoCount(Context);
		VectorVM::FExternalFuncRegisterHandler<float> OutEmitterModuleSpawnInfoInterpStartDt(Context);
		VectorVM::FExternalFuncRegisterHandler<float> OutEmitterModuleSpawnInfoIntervalDt(Context);
		VectorVM::FExternalFuncRegisterHandler<int32> OutEmitterModuleSpawnInfoSpawnGroup(Context);

		for (int i=0; i < Context.GetNumInstances(); ++i)
		{
			// Gather values
			float EngineDeltaTime = InEngineDeltaTime.GetAndAdvance();
			float ScalabilityEmitterSpawnCountScale = InScalabilityEmitterSpawnCountScale.GetAndAdvance();
			float EmitterLoopedAge = InEmitterLoopedAge.GetAndAdvance();
			float ModuleSpawnTime = InModuleSpawnTime.GetAndAdvance();
			int32 ModuleSpawnCount = InModuleSpawnCount.GetAndAdvance();
			int32 ModuleSpawnGroup = InModuleSpawnGroup.GetAndAdvance();

			const float PreviousTime = EmitterLoopedAge - EngineDeltaTime;

			*OutSpawningCanEverSpawn.GetDestAndAdvance() = EmitterLoopedAge <= ModuleSpawnTime;
			if ( (ModuleSpawnTime >= PreviousTime) && (ModuleSpawnTime < EmitterLoopedAge) )
			{
				*OutEmitterModuleSpawnInfoCount.GetDestAndAdvance() = ModuleSpawnCount;
				*OutEmitterModuleSpawnInfoInterpStartDt.GetDestAndAdvance() = ModuleSpawnTime - PreviousTime;
			}
			else
			{
				*OutEmitterModuleSpawnInfoCount.GetDestAndAdvance() = 0;
				*OutEmitterModuleSpawnInfoInterpStartDt.GetDestAndAdvance() = 0.0f;
			}
			*OutEmitterModuleSpawnInfoIntervalDt.GetDestAndAdvance() = 0.0f;
			*OutEmitterModuleSpawnInfoSpawnGroup.GetDestAndAdvance() = ModuleSpawnGroup;
		}
	}
};

struct FVectorKernel_SolveVelocitiesAndForces
{
	static FNiagaraFunctionSignature GetFunctionSignature()
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FastPathSolveVelocitiesAndForces;
		Sig.OwnerName = FastPathLibraryName;
		Sig.bMemberFunction = false;
		Sig.bRequiresContext = false;
		Sig.SetDescription(LOCTEXT("FastPathSolveVelocitiesAndForces", "Fast path for SolveVelocitiesAndForces"));

		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("EngineDeltaTime")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("PhysicsForce")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("PhysicsDrag")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("ParticlesMass")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("ParticlesPosition")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("ParticlesVelocity")));

		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("ParticlesPosition")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("ParticlesVelocity")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("ParticlesPreviousVelocity")));

		return Sig;
	}

	static FString GetFunctionHLSL()
	{
		return FString();
	}

	static void Exec(FVectorVMContext& Context)
	{
		VectorVM::FExternalFuncInputHandler<float> InEngineDeltaTime(Context);
		VectorVM::FExternalFuncInputHandler<float> InPhysicsForceX(Context);
		VectorVM::FExternalFuncInputHandler<float> InPhysicsForceY(Context);
		VectorVM::FExternalFuncInputHandler<float> InPhysicsForceZ(Context);
		VectorVM::FExternalFuncInputHandler<float> InPhysicsDrag(Context);
		VectorVM::FExternalFuncInputHandler<float> InParticlesMass(Context);
		VectorVM::FExternalFuncInputHandler<float> InParticlesPositionX(Context);
		VectorVM::FExternalFuncInputHandler<float> InParticlesPositionY(Context);
		VectorVM::FExternalFuncInputHandler<float> InParticlesPositionZ(Context);
		VectorVM::FExternalFuncInputHandler<float> InParticlesVelocityX(Context);
		VectorVM::FExternalFuncInputHandler<float> InParticlesVelocityY(Context);
		VectorVM::FExternalFuncInputHandler<float> InParticlesVelocityZ(Context);

		VectorVM::FExternalFuncRegisterHandler<float> OutParticlesPositionX(Context);
		VectorVM::FExternalFuncRegisterHandler<float> OutParticlesPositionY(Context);
		VectorVM::FExternalFuncRegisterHandler<float> OutParticlesPositionZ(Context);
		VectorVM::FExternalFuncRegisterHandler<float> OutParticlesVelocityX(Context);
		VectorVM::FExternalFuncRegisterHandler<float> OutParticlesVelocityY(Context);
		VectorVM::FExternalFuncRegisterHandler<float> OutParticlesVelocityZ(Context);
		VectorVM::FExternalFuncRegisterHandler<float> OutParticlesPreviousVelocityX(Context);
		VectorVM::FExternalFuncRegisterHandler<float> OutParticlesPreviousVelocityY(Context);
		VectorVM::FExternalFuncRegisterHandler<float> OutParticlesPreviousVelocityZ(Context);
#if 1
		const float EngineDeltaTime = InEngineDeltaTime.Get();
		const float MassMin = 0.0001f;

		for (int i = 0; i < Context.GetNumInstances(); ++i)
		{
			// Gather values
			FVector PhysicsForce(InPhysicsForceX.GetAndAdvance(), InPhysicsForceY.GetAndAdvance(), InPhysicsForceZ.GetAndAdvance());
			float PhysicsDrag = InPhysicsDrag.GetAndAdvance();

			float ParticleMass = InParticlesMass.GetAndAdvance();
			FVector ParticlePosition(InParticlesPositionX.GetAndAdvance(), InParticlesPositionY.GetAndAdvance(), InParticlesPositionZ.GetAndAdvance());
			FVector ParticleVelocity(InParticlesVelocityX.GetAndAdvance(), InParticlesVelocityY.GetAndAdvance(), InParticlesVelocityZ.GetAndAdvance());

			*OutParticlesPreviousVelocityX.GetDestAndAdvance() = ParticleVelocity.X;
			*OutParticlesPreviousVelocityY.GetDestAndAdvance() = ParticleVelocity.Y;
			*OutParticlesPreviousVelocityZ.GetDestAndAdvance() = ParticleVelocity.Z;

			// Apply velocity
			const float OOParticleMassDT = (1.0f / FMath::Max(ParticleMass, MassMin)) * EngineDeltaTime;
			ParticleVelocity += PhysicsForce * OOParticleMassDT;

			// Apply Drag
			float ClampedDrag = FMath::Clamp(PhysicsDrag * EngineDeltaTime, 0.0f, 1.0f);
			ParticleVelocity -= ParticleVelocity * ClampedDrag;

			// Velocity Clamp
			//-TODO: Not used

			// Limit Acceleration
			//-TODO: Not used

			// Apply velocity
			ParticlePosition += ParticleVelocity * EngineDeltaTime;

			// Write parameters
			*OutParticlesPositionX.GetDestAndAdvance() = ParticlePosition.X;
			*OutParticlesPositionY.GetDestAndAdvance() = ParticlePosition.Y;
			*OutParticlesPositionZ.GetDestAndAdvance() = ParticlePosition.Z;

			*OutParticlesVelocityX.GetDestAndAdvance() = ParticleVelocity.X;
			*OutParticlesVelocityY.GetDestAndAdvance() = ParticleVelocity.Y;
			*OutParticlesVelocityZ.GetDestAndAdvance() = ParticleVelocity.Z;
		}
#else
		const VectorRegister EngineDeltaTime = VectorSetFloat1(InEngineDeltaTime.Get());
		const VectorRegister MassMin = VectorSetFloat1(0.0001f);

		for (int i=0; i < Context.GetNumLoops<4>(); ++i)
		{
			// Gather values
			VectorRegister PhysicsForceX		= VectorLoad(InPhysicsForceX.GetDestAndAdvance());
			VectorRegister PhysicsForceY		= VectorLoad(InPhysicsForceY.GetDestAndAdvance());
			VectorRegister PhysicsForceZ		= VectorLoad(InPhysicsForceZ.GetDestAndAdvance());
			VectorRegister PhysicsDrag			= VectorLoad(InPhysicsDrag.GetDestAndAdvance());

			VectorRegister ParticlesMass		= VectorLoad(InParticlesMass.GetDestAndAdvance());
			VectorRegister ParticlesPositionX	= VectorLoad(InParticlesPositionX.GetDestAndAdvance());
			VectorRegister ParticlesPositionY	= VectorLoad(InParticlesPositionY.GetDestAndAdvance());
			VectorRegister ParticlesPositionZ	= VectorLoad(InParticlesPositionZ.GetDestAndAdvance());
			VectorRegister ParticlesVelocityX	= VectorLoad(InParticlesVelocityX.GetDestAndAdvance());
			VectorRegister ParticlesVelocityY	= VectorLoad(InParticlesVelocityY.GetDestAndAdvance());
			VectorRegister ParticlesVelocityZ	= VectorLoad(InParticlesVelocityZ.GetDestAndAdvance());

			VectorStore(ParticlesVelocityX, OutParticlesPreviousVelocityX.GetDestAndAdvance());
			VectorStore(ParticlesVelocityY, OutParticlesPreviousVelocityY.GetDestAndAdvance());
			VectorStore(ParticlesVelocityZ, OutParticlesPreviousVelocityZ.GetDestAndAdvance());

			// Apply velocity
			const VectorRegister OOParticleMassDT = VectorMultiply(VectorReciprocal(VectorMax(ParticlesMass, MassMin)), EngineDeltaTime);
			ParticlesVelocityX = VectorMultiplyAdd(PhysicsForceX, OOParticleMassDT, ParticlesVelocityX);
			ParticlesVelocityY = VectorMultiplyAdd(PhysicsForceY, OOParticleMassDT, ParticlesVelocityY);
			ParticlesVelocityZ = VectorMultiplyAdd(PhysicsForceZ, OOParticleMassDT, ParticlesVelocityZ);

			// Apply Drag
			VectorRegister ClampedDrag = VectorMultiply(PhysicsDrag, EngineDeltaTime);
			ClampedDrag = VectorMax(VectorMin(ClampedDrag, VectorOne()), VectorZero());
			ClampedDrag = VectorNegate(ClampedDrag);

			ParticlesVelocityX = VectorMultiplyAdd(ParticlesVelocityX, ClampedDrag, ParticlesVelocityX);
			ParticlesVelocityY = VectorMultiplyAdd(ParticlesVelocityY, ClampedDrag, ParticlesVelocityY);
			ParticlesVelocityZ = VectorMultiplyAdd(ParticlesVelocityZ, ClampedDrag, ParticlesVelocityZ);

			// Velocity Clamp
			//-TODO: Not used

			// Limit Acceleration
			//-TODO: Not used

			// Apply velocity
			ParticlesPositionX = VectorMultiplyAdd(ParticlesVelocityX, EngineDeltaTime, ParticlesPositionX);
			ParticlesPositionY = VectorMultiplyAdd(ParticlesVelocityY, EngineDeltaTime, ParticlesPositionY);
			ParticlesPositionZ = VectorMultiplyAdd(ParticlesVelocityZ, EngineDeltaTime, ParticlesPositionZ);

			// Write parameters
			VectorStore(ParticlesPositionX, OutParticlesPositionX.GetDestAndAdvance());
			VectorStore(ParticlesPositionY, OutParticlesPositionY.GetDestAndAdvance());
			VectorStore(ParticlesPositionZ, OutParticlesPositionZ.GetDestAndAdvance());

			VectorStore(ParticlesVelocityX, OutParticlesVelocityX.GetDestAndAdvance());
			VectorStore(ParticlesVelocityY, OutParticlesVelocityY.GetDestAndAdvance());
			VectorStore(ParticlesVelocityZ, OutParticlesVelocityZ.GetDestAndAdvance());
		}
#endif
	}

	template<bool bForceConstant, bool bDragConstant, bool bMassConstant>
	FORCEINLINE static void ExecOptimized(FVectorVMContext& Context)
	{
#if 0
		const VectorRegister MassMin = VectorSetFloat1(0.0001f);
		const VectorRegister EngineDeltaTime = VectorSetFloat1(VectorVM::FExternalFuncInputHandler<float>(Context).Get());
		VectorRegister PhysicsForceX = VectorLoadFloat1(VectorVM::FExternalFuncInputHandler<VectorRegister>(Context).GetDest());
		VectorRegister PhysicsForceY = VectorLoadFloat1(VectorVM::FExternalFuncInputHandler<VectorRegister>(Context).GetDest());
		VectorRegister PhysicsForceZ = VectorLoadFloat1(VectorVM::FExternalFuncInputHandler<VectorRegister>(Context).GetDest());
		VectorRegister PhysicsDrag = VectorLoadFloat1(VectorVM::FExternalFuncInputHandler<VectorRegister>(Context).GetDest());
		VectorRegister ParticlesMass = VectorLoadFloat1(VectorVM::FExternalFuncInputHandler<VectorRegister>(Context).GetDest());
		const VectorRegister* RESTRICT InParticlesPositionX = VectorVM::FExternalFuncInputHandler<VectorRegister>(Context).GetDest();
		const VectorRegister* RESTRICT InParticlesPositionY = VectorVM::FExternalFuncInputHandler<VectorRegister>(Context).GetDest();
		const VectorRegister* RESTRICT InParticlesPositionZ = VectorVM::FExternalFuncInputHandler<VectorRegister>(Context).GetDest();
		const VectorRegister* RESTRICT InParticlesVelocityX = VectorVM::FExternalFuncInputHandler<VectorRegister>(Context).GetDest();
		const VectorRegister* RESTRICT InParticlesVelocityY = VectorVM::FExternalFuncInputHandler<VectorRegister>(Context).GetDest();
		const VectorRegister* RESTRICT InParticlesVelocityZ = VectorVM::FExternalFuncInputHandler<VectorRegister>(Context).GetDest();

		VectorRegister* RESTRICT OutParticlesPositionX = VectorVM::FExternalFuncRegisterHandler<VectorRegister>(Context).GetDest();
		VectorRegister* RESTRICT OutParticlesPositionY = VectorVM::FExternalFuncRegisterHandler<VectorRegister>(Context).GetDest();
		VectorRegister* RESTRICT OutParticlesPositionZ = VectorVM::FExternalFuncRegisterHandler<VectorRegister>(Context).GetDest();
		VectorRegister* RESTRICT OutParticlesVelocityX = VectorVM::FExternalFuncRegisterHandler<VectorRegister>(Context).GetDest();
		VectorRegister* RESTRICT OutParticlesVelocityY = VectorVM::FExternalFuncRegisterHandler<VectorRegister>(Context).GetDest();
		VectorRegister* RESTRICT OutParticlesVelocityZ = VectorVM::FExternalFuncRegisterHandler<VectorRegister>(Context).GetDest();
		VectorRegister* RESTRICT OutParticlesPreviousVelocityX = VectorVM::FExternalFuncRegisterHandler<VectorRegister>(Context).GetDest();
		VectorRegister* RESTRICT OutParticlesPreviousVelocityY = VectorVM::FExternalFuncRegisterHandler<VectorRegister>(Context).GetDest();
		VectorRegister* RESTRICT OutParticlesPreviousVelocityZ = VectorVM::FExternalFuncRegisterHandler<VectorRegister>(Context).GetDest();

		const VectorRegister OOParticleMassDT = VectorMultiply(VectorReciprocal(VectorMax(ParticlesMass, MassMin)), EngineDeltaTime);

		VectorRegister ClampedDrag = VectorMultiply(PhysicsDrag, EngineDeltaTime);
		ClampedDrag = VectorMax(VectorMin(ClampedDrag, VectorOne()), VectorZero());
		ClampedDrag = VectorNegate(ClampedDrag);

		for (int i = 0; i < Context.GetNumLoops<4>(); ++i)
		{
			// Gather values
			VectorRegister ParticlesPositionX = VectorLoad(InParticlesPositionX + i);
			VectorRegister ParticlesPositionY = VectorLoad(InParticlesPositionY + i);
			VectorRegister ParticlesPositionZ = VectorLoad(InParticlesPositionZ + i);
			VectorRegister ParticlesVelocityX = VectorLoad(InParticlesVelocityX + i);
			VectorRegister ParticlesVelocityY = VectorLoad(InParticlesVelocityY + i);
			VectorRegister ParticlesVelocityZ = VectorLoad(InParticlesVelocityZ + i);

			VectorStore(ParticlesVelocityX, OutParticlesPreviousVelocityX + i);
			VectorStore(ParticlesVelocityY, OutParticlesPreviousVelocityY + i);
			VectorStore(ParticlesVelocityZ, OutParticlesPreviousVelocityZ + i);

			// Apply velocity
			ParticlesVelocityX = VectorMultiplyAdd(PhysicsForceX, OOParticleMassDT, ParticlesVelocityX);
			ParticlesVelocityY = VectorMultiplyAdd(PhysicsForceY, OOParticleMassDT, ParticlesVelocityY);
			ParticlesVelocityZ = VectorMultiplyAdd(PhysicsForceZ, OOParticleMassDT, ParticlesVelocityZ);

			// Apply Drag
			ParticlesVelocityX = VectorMultiplyAdd(ParticlesVelocityX, ClampedDrag, ParticlesVelocityX);
			ParticlesVelocityY = VectorMultiplyAdd(ParticlesVelocityY, ClampedDrag, ParticlesVelocityY);
			ParticlesVelocityZ = VectorMultiplyAdd(ParticlesVelocityZ, ClampedDrag, ParticlesVelocityZ);

			// Velocity Clamp
			//-TODO: Not used

			// Limit Acceleration
			//-TODO: Not used

			// Apply velocity
			ParticlesPositionX = VectorMultiplyAdd(ParticlesVelocityX, EngineDeltaTime, ParticlesPositionX);
			ParticlesPositionY = VectorMultiplyAdd(ParticlesVelocityY, EngineDeltaTime, ParticlesPositionY);
			ParticlesPositionZ = VectorMultiplyAdd(ParticlesVelocityZ, EngineDeltaTime, ParticlesPositionZ);

			// Write parameters
			VectorStore(ParticlesPositionX, OutParticlesPositionX + i);
			VectorStore(ParticlesPositionY, OutParticlesPositionY + i);
			VectorStore(ParticlesPositionZ, OutParticlesPositionZ + i);

			VectorStore(ParticlesVelocityX, OutParticlesVelocityX + i);
			VectorStore(ParticlesVelocityY, OutParticlesVelocityY + i);
			VectorStore(ParticlesVelocityZ, OutParticlesVelocityZ + i);
		}
#else
		VectorVM::FExternalFuncInputHandler<float> InEngineDeltaTime(Context);
		VectorVM::FExternalFuncInputHandler<VectorRegister> InPhysicsForceXHandler(Context);
		VectorVM::FExternalFuncInputHandler<VectorRegister> InPhysicsForceYHandler(Context);
		VectorVM::FExternalFuncInputHandler<VectorRegister> InPhysicsForceZHandler(Context);
		VectorVM::FExternalFuncInputHandler<VectorRegister> InPhysicsDragHandler(Context);
		VectorVM::FExternalFuncInputHandler<VectorRegister> InParticlesMassHandler(Context);
		VectorVM::FExternalFuncInputHandler<VectorRegister> InParticlesPositionXHandler(Context);
		VectorVM::FExternalFuncInputHandler<VectorRegister> InParticlesPositionYHandler(Context);
		VectorVM::FExternalFuncInputHandler<VectorRegister> InParticlesPositionZHandler(Context);
		VectorVM::FExternalFuncInputHandler<VectorRegister> InParticlesVelocityXHandler(Context);
		VectorVM::FExternalFuncInputHandler<VectorRegister> InParticlesVelocityYHandler(Context);
		VectorVM::FExternalFuncInputHandler<VectorRegister> InParticlesVelocityZHandler(Context);

		VectorVM::FExternalFuncRegisterHandler<VectorRegister> OutParticlesPositionXHandler(Context);
		VectorVM::FExternalFuncRegisterHandler<VectorRegister> OutParticlesPositionYHandler(Context);
		VectorVM::FExternalFuncRegisterHandler<VectorRegister> OutParticlesPositionZHandler(Context);
		VectorVM::FExternalFuncRegisterHandler<VectorRegister> OutParticlesVelocityXHandler(Context);
		VectorVM::FExternalFuncRegisterHandler<VectorRegister> OutParticlesVelocityYHandler(Context);
		VectorVM::FExternalFuncRegisterHandler<VectorRegister> OutParticlesVelocityZHandler(Context);
		VectorVM::FExternalFuncRegisterHandler<VectorRegister> OutParticlesPreviousVelocityXHandler(Context);
		VectorVM::FExternalFuncRegisterHandler<VectorRegister> OutParticlesPreviousVelocityYHandler(Context);
		VectorVM::FExternalFuncRegisterHandler<VectorRegister> OutParticlesPreviousVelocityZHandler(Context);

		const VectorRegister EngineDeltaTime = VectorSetFloat1(InEngineDeltaTime.Get());
		const VectorRegister* RESTRICT InPhysicsForceX = InPhysicsForceXHandler.GetDest();
		const VectorRegister* RESTRICT InPhysicsForceY = InPhysicsForceYHandler.GetDest();
		const VectorRegister* RESTRICT InPhysicsForceZ = InPhysicsForceZHandler.GetDest();
		const VectorRegister* RESTRICT InPhysicsDrag = InPhysicsDragHandler.GetDest();
		const VectorRegister* RESTRICT InParticlesMass = InParticlesMassHandler.GetDest();
		const VectorRegister* RESTRICT InParticlesPositionX = InParticlesPositionXHandler.GetDest();
		const VectorRegister* RESTRICT InParticlesPositionY = InParticlesPositionYHandler.GetDest();
		const VectorRegister* RESTRICT InParticlesPositionZ = InParticlesPositionZHandler.GetDest();
		const VectorRegister* RESTRICT InParticlesVelocityX = InParticlesVelocityXHandler.GetDest();
		const VectorRegister* RESTRICT InParticlesVelocityY = InParticlesVelocityYHandler.GetDest();
		const VectorRegister* RESTRICT InParticlesVelocityZ = InParticlesVelocityZHandler.GetDest();

		VectorRegister* RESTRICT OutParticlesPositionX = OutParticlesPositionXHandler.GetDest();
		VectorRegister* RESTRICT OutParticlesPositionY = OutParticlesPositionYHandler.GetDest();
		VectorRegister* RESTRICT OutParticlesPositionZ = OutParticlesPositionZHandler.GetDest();
		VectorRegister* RESTRICT OutParticlesVelocityX = OutParticlesVelocityXHandler.GetDest();
		VectorRegister* RESTRICT OutParticlesVelocityY = OutParticlesVelocityYHandler.GetDest();
		VectorRegister* RESTRICT OutParticlesVelocityZ = OutParticlesVelocityZHandler.GetDest();
		VectorRegister* RESTRICT OutParticlesPreviousVelocityX = OutParticlesPreviousVelocityXHandler.GetDest();
		VectorRegister* RESTRICT OutParticlesPreviousVelocityY = OutParticlesPreviousVelocityYHandler.GetDest();
		VectorRegister* RESTRICT OutParticlesPreviousVelocityZ = OutParticlesPreviousVelocityZHandler.GetDest();

		const VectorRegister MassMin = VectorSetFloat1(0.0001f);

		const VectorRegister ConstantPhysicsForceX = bForceConstant ? VectorLoadFloat1(InPhysicsForceX) : VectorZero();
		const VectorRegister ConstantPhysicsForceY = bForceConstant ? VectorLoadFloat1(InPhysicsForceY) : VectorZero();
		const VectorRegister ConstantPhysicsForceZ = bForceConstant ? VectorLoadFloat1(InPhysicsForceZ) : VectorZero();
		const VectorRegister ConstantPhysicsDrag = bDragConstant ? VectorLoadFloat1(InPhysicsDrag) : VectorZero();
		const VectorRegister ConstantParticlesMass = bMassConstant ? VectorLoadFloat1(InParticlesMass) : VectorZero();

		for (int i = 0; i < Context.GetNumLoops<4>(); ++i)
		{
			// Gather values
			VectorRegister PhysicsForceX = bForceConstant ? ConstantPhysicsForceX : VectorLoad(InPhysicsForceX + i);
			VectorRegister PhysicsForceY = bForceConstant ? ConstantPhysicsForceY : VectorLoad(InPhysicsForceY + i);
			VectorRegister PhysicsForceZ = bForceConstant ? ConstantPhysicsForceZ : VectorLoad(InPhysicsForceZ + i);
			VectorRegister PhysicsDrag = bDragConstant ? ConstantPhysicsDrag : VectorLoad(InPhysicsDrag + i);

			VectorRegister ParticlesMass = bMassConstant ? ConstantParticlesMass : VectorLoad(InParticlesMass + i);
			VectorRegister ParticlesPositionX = VectorLoad(InParticlesPositionX + i);
			VectorRegister ParticlesPositionY = VectorLoad(InParticlesPositionY + i);
			VectorRegister ParticlesPositionZ = VectorLoad(InParticlesPositionZ + i);
			VectorRegister ParticlesVelocityX = VectorLoad(InParticlesVelocityX + i);
			VectorRegister ParticlesVelocityY = VectorLoad(InParticlesVelocityY + i);
			VectorRegister ParticlesVelocityZ = VectorLoad(InParticlesVelocityZ + i);

			VectorStore(ParticlesVelocityX, OutParticlesPreviousVelocityX + i);
			VectorStore(ParticlesVelocityY, OutParticlesPreviousVelocityY + i);
			VectorStore(ParticlesVelocityZ, OutParticlesPreviousVelocityZ + i);

			// Apply velocity
			const VectorRegister OOParticleMassDT = VectorMultiply(VectorReciprocal(VectorMax(ParticlesMass, MassMin)), EngineDeltaTime);
			ParticlesVelocityX = VectorMultiplyAdd(PhysicsForceX, OOParticleMassDT, ParticlesVelocityX);
			ParticlesVelocityY = VectorMultiplyAdd(PhysicsForceY, OOParticleMassDT, ParticlesVelocityY);
			ParticlesVelocityZ = VectorMultiplyAdd(PhysicsForceZ, OOParticleMassDT, ParticlesVelocityZ);

			// Apply Drag
			VectorRegister ClampedDrag = VectorMultiply(PhysicsDrag, EngineDeltaTime);
			ClampedDrag = VectorMax(VectorMin(ClampedDrag, VectorOne()), VectorZero());
			ClampedDrag = VectorNegate(ClampedDrag);

			ParticlesVelocityX = VectorMultiplyAdd(ParticlesVelocityX, ClampedDrag, ParticlesVelocityX);
			ParticlesVelocityY = VectorMultiplyAdd(ParticlesVelocityY, ClampedDrag, ParticlesVelocityY);
			ParticlesVelocityZ = VectorMultiplyAdd(ParticlesVelocityZ, ClampedDrag, ParticlesVelocityZ);

			// Velocity Clamp
			//-TODO: Not used

			// Limit Acceleration
			//-TODO: Not used

			// Apply velocity
			ParticlesPositionX = VectorMultiplyAdd(ParticlesVelocityX, EngineDeltaTime, ParticlesPositionX);
			ParticlesPositionY = VectorMultiplyAdd(ParticlesVelocityY, EngineDeltaTime, ParticlesPositionY);
			ParticlesPositionZ = VectorMultiplyAdd(ParticlesVelocityZ, EngineDeltaTime, ParticlesPositionZ);

			// Write parameters
			VectorStore(ParticlesPositionX, OutParticlesPositionX + i);
			VectorStore(ParticlesPositionY, OutParticlesPositionY + i);
			VectorStore(ParticlesPositionZ, OutParticlesPositionZ + i);

			VectorStore(ParticlesVelocityX, OutParticlesVelocityX + i);
			VectorStore(ParticlesVelocityY, OutParticlesVelocityY + i);
			VectorStore(ParticlesVelocityZ, OutParticlesVelocityZ + i);
		}
#endif
	}
};

bool UNiagaraFunctionLibrary::GetVectorVMFastPathExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, FVMExternalFunction &OutFunc)
{
	if (BindingInfo.Name == FastPathDot4Name)
	{
		OutFunc = FVMExternalFunction::CreateStatic(FVectorKernelFastDot4::Exec);
		return true;
	}
	else if (BindingInfo.Name == FastPathTransformPositionName)
	{
		OutFunc = FVMExternalFunction::CreateStatic(FVectorKernelFastTransformPosition::Exec);
		return true;
	}
	else if (BindingInfo.Name == FastMatrixToQuaternionName)
	{
		OutFunc = FVMExternalFunction::CreateStatic(FVectorKernelFastMatrixToQuaternion::Exec);
		return true;
	}
	else if (BindingInfo.Name == FastPathEmitterLifeCycleName)
	{
		OutFunc = FVMExternalFunction::CreateStatic(FVectorKernel_EmitterLifeCycle::Exec);
		return true;
	}
	else if (BindingInfo.Name == FastPathSpawnRateName)
	{
		OutFunc = FVMExternalFunction::CreateStatic(FVectorKernel_SpawnRate::Exec);
		return true;
	}
	else if (BindingInfo.Name == FastPathSpawnBurstInstantaneousName)
	{
		OutFunc = FVMExternalFunction::CreateStatic(FVectorKernel_SpawnBurstInstantaneous::Exec);
		return true;
	}
	else if (BindingInfo.Name == FastPathSolveVelocitiesAndForces)
	{
#if 0
		OutFunc = FVMExternalFunction::CreateStatic(FVectorKernel_SolveVelocitiesAndForces::Exec);
#else
		const bool bForceConstant = BindingInfo.InputParamLocations[1] && BindingInfo.InputParamLocations[2] && BindingInfo.InputParamLocations[3];
		const bool bDragConstant = BindingInfo.InputParamLocations[4];
		const bool bMassConstant = BindingInfo.InputParamLocations[5];

		if ( bForceConstant )
		{
			if ( bDragConstant )
			{
				if ( bMassConstant )
					OutFunc = FVMExternalFunction::CreateStatic(FVectorKernel_SolveVelocitiesAndForces::ExecOptimized<true, true, true>);
				else
					OutFunc = FVMExternalFunction::CreateStatic(FVectorKernel_SolveVelocitiesAndForces::ExecOptimized<true, true, false>);
			}
			else
			{
				if (bMassConstant)
					OutFunc = FVMExternalFunction::CreateStatic(FVectorKernel_SolveVelocitiesAndForces::ExecOptimized<true, false, true>);
				else
					OutFunc = FVMExternalFunction::CreateStatic(FVectorKernel_SolveVelocitiesAndForces::ExecOptimized<true, false, false>);
			}
		}
		else
		{
			if (bDragConstant)
			{
				if (bMassConstant)
					OutFunc = FVMExternalFunction::CreateStatic(FVectorKernel_SolveVelocitiesAndForces::ExecOptimized<false, true, true>);
				else
					OutFunc = FVMExternalFunction::CreateStatic(FVectorKernel_SolveVelocitiesAndForces::ExecOptimized<false, true, false>);
			}
			else
			{
				if (bMassConstant)
					OutFunc = FVMExternalFunction::CreateStatic(FVectorKernel_SolveVelocitiesAndForces::ExecOptimized<false, false, true>);
				else
					OutFunc = FVMExternalFunction::CreateStatic(FVectorKernel_SolveVelocitiesAndForces::Exec);
					//OutFunc = FVMExternalFunction::CreateStatic(FVectorKernel_SolveVelocitiesAndForces::ExecOptimized<false, false, false>);
			}
		}
#endif
		return true;
	}
	return false;
}


void UNiagaraFunctionLibrary::InitVectorVMFastPathOps()
{
	if (VectorVMOps.Num() > 0)
		return;

	VectorVMOps.Emplace(FVectorKernelFastDot4::GetFunctionSignature());
	VectorVMOps.Emplace(FVectorKernelFastTransformPosition::GetFunctionSignature());
	VectorVMOps.Emplace(FVectorKernelFastMatrixToQuaternion::GetFunctionSignature());
	VectorVMOps.Emplace(FVectorKernel_EmitterLifeCycle::GetFunctionSignature());
	VectorVMOps.Emplace(FVectorKernel_SpawnRate::GetFunctionSignature());
	VectorVMOps.Emplace(FVectorKernel_SpawnBurstInstantaneous::GetFunctionSignature());
	VectorVMOps.Emplace(FVectorKernel_SolveVelocitiesAndForces::GetFunctionSignature());

	VectorVMOpsHLSL.Emplace(FVectorKernelFastDot4::GetFunctionHLSL());
	VectorVMOpsHLSL.Emplace(FVectorKernelFastTransformPosition::GetFunctionHLSL());
	VectorVMOpsHLSL.Emplace(FVectorKernelFastMatrixToQuaternion::GetFunctionHLSL());
	VectorVMOpsHLSL.Emplace(FVectorKernel_EmitterLifeCycle::GetFunctionHLSL());
	VectorVMOpsHLSL.Emplace(FVectorKernel_SpawnRate::GetFunctionHLSL());
	VectorVMOpsHLSL.Emplace(FVectorKernel_SpawnBurstInstantaneous::GetFunctionHLSL());
	VectorVMOpsHLSL.Emplace(FVectorKernel_SolveVelocitiesAndForces::GetFunctionHLSL());

	check(VectorVMOps.Num() == VectorVMOpsHLSL.Num());
}

#undef LOCTEXT_NAMESPACE