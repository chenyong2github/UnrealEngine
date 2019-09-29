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
UNiagaraComponent* UNiagaraFunctionLibrary::SpawnSystemAtLocation(const UObject* WorldContextObject, UNiagaraSystem* SystemTemplate, FVector SpawnLocation, FRotator SpawnRotation, FVector Scale, bool bAutoDestroy, bool bAutoActivate, ENCPoolMethod PoolingMethod)
{
	UNiagaraComponent* PSC = NULL;
	if (SystemTemplate)
	{
		UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
		if (World != nullptr)
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
	return PSC;
}





/**
* Spawns a Niagara System attached to a component
* @return			The spawned UNiagaraComponent
*/
UNiagaraComponent* UNiagaraFunctionLibrary::SpawnSystemAttached(UNiagaraSystem* SystemTemplate, USceneComponent* AttachToComponent, FName AttachPointName, FVector Location, FRotator Rotation, EAttachLocation::Type LocationType, bool bAutoDestroy, bool bAutoActivate, ENCPoolMethod PoolingMethod)
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
	bool bAutoActivate
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
	if (VectorVMOps.Num() == 0)
	{
		InitVectorVMFastPathOps();
	}
	return VectorVMOps;
}

struct FVectorKernelFastDot4
{
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

		int32 LoopInstances = Align(Context.NumInstances, 4) / 4;
		for (int32 i = 0; i < LoopInstances; ++i)
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

const FName FastPathLibraryName(TEXT("FastPathLibrary"));
const FName FastPathDot4Name(TEXT("FastPathDot4"));
const FName FastPathTransformPositionName(TEXT("FastPathTransformPosition"));

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
	return false;
}


void UNiagaraFunctionLibrary::InitVectorVMFastPathOps()
{
	if (GAllowFastPathFunctionLibrary == 0)
		return;

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
		VectorVMOps.Add(Sig);
	}
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
		VectorVMOps.Add(Sig);
	}
}

#undef LOCTEXT_NAMESPACE