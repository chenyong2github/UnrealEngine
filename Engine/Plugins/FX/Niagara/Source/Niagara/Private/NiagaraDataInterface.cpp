// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterface.h"
#include "Curves/CurveVector.h"
#include "Curves/CurveLinearColor.h"
#include "Curves/CurveFloat.h"
#include "NiagaraTypes.h"
#include "ShaderParameterUtils.h"
#include "NiagaraShader.h"
#include "NiagaraComponent.h"

#define LOCTEXT_NAMESPACE "NiagaraDataInterface"

UNiagaraDataInterface::UNiagaraDataInterface(FObjectInitializer const& ObjectInitializer)
{
	bRenderDataDirty = false;
	bUsedByGPUEmitter = false;
}

UNiagaraDataInterface::~UNiagaraDataInterface()
{
	// @todo-threadsafety Can there be a UNiagaraDataInterface class itself created? Perhaps by the system?
	if ( Proxy.IsValid() )
	{
		ENQUEUE_RENDER_COMMAND(FDeleteProxyRT) (
			[RT_Proxy=MoveTemp(Proxy)](FRHICommandListImmediate& CmdList)
			{
				// This will release RT_Proxy on the RT
			}
		);
		check(Proxy.IsValid() == false);
	}
}

#if WITH_EDITORONLY_DATA
bool UNiagaraDataInterface::AppendCompileHash(FNiagaraCompileHashVisitor* InVisitor) const
{
	return true;
}
#endif

void UNiagaraDataInterface::GetAssetTagsForContext(const UObject* InAsset, const TArray<const UNiagaraDataInterface*>& InProperties, TMap<FName, uint32>& NumericKeys, TMap<FName, FString>& StringKeys) const
{
	UClass* Class = GetClass();

	// Default count up how many instances there are of this class and report to content browser
	if (Class)
	{
		uint32 NumInstances = 0;
		for (const UNiagaraDataInterface* Prop : InProperties)
		{
			if (Prop && Prop->IsA(Class))
			{
				NumInstances++;
			}
		}
		
		// Note that in order for these tags to be registered, we always have to put them in place for the CDO of the object, but 
		// for readability's sake, we leave them out of non-CDO assets.
		if (NumInstances > 0 || (InAsset && InAsset->HasAnyFlags(EObjectFlags::RF_ClassDefaultObject)))
		{
			FString Key = Class->GetName();
			Key.ReplaceInline(TEXT("NiagaraDataInterface"), TEXT(""));
			NumericKeys.Add(*Key) = NumInstances;
		}
	}
}

void UNiagaraDataInterface::PostLoad()
{
	Super::PostLoad();
	SetFlags(RF_Public);
}

#if WITH_EDITOR
void UNiagaraDataInterface::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	RefreshErrors();
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

bool UNiagaraDataInterface::CopyTo(UNiagaraDataInterface* Destination) const 
{
	bool result = CopyToInternal(Destination);
#if WITH_EDITOR
	Destination->OnChanged().Broadcast();
#endif
	return result;
}

bool UNiagaraDataInterface::Equals(const UNiagaraDataInterface* Other) const
{
	if (Other == nullptr || Other->GetClass() != GetClass())
	{
		return false;
	}
	return true;
}

bool UNiagaraDataInterface::IsUsedWithGPUEmitter(FNiagaraSystemInstance* SystemInstance) const
{
	return bUsedByGPUEmitter;
}

bool UNiagaraDataInterface::IsDataInterfaceType(const FNiagaraTypeDefinition& TypeDef)
{
	const UClass* Class = TypeDef.GetClass();
	if (Class && Class->IsChildOf(UNiagaraDataInterface::StaticClass()))
	{
		return true;
	}
	return false;
}

bool UNiagaraDataInterface::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (Destination == nullptr || Destination->GetClass() != GetClass())
	{
		return false;
	}
	return true;
}

#if WITH_EDITOR
void UNiagaraDataInterface::GetFeedback(UNiagaraSystem* InAsset, UNiagaraComponent* InComponent, TArray<FNiagaraDataInterfaceError>& OutErrors, TArray<FNiagaraDataInterfaceFeedback>& OutWarnings, TArray<FNiagaraDataInterfaceFeedback>& OutInfo)
{
	OutErrors = GetErrors();
	OutWarnings.Empty();
	OutInfo.Empty();
}

void UNiagaraDataInterface::GetFeedback(UNiagaraDataInterface* DataInterface, TArray<FNiagaraDataInterfaceError>& Errors, TArray<FNiagaraDataInterfaceFeedback>& Warnings,
	TArray<FNiagaraDataInterfaceFeedback>& Info)
{
	if (!DataInterface)
		return;

	UNiagaraSystem* Asset = nullptr;
	UNiagaraComponent* Component = nullptr;

	// Walk the hierarchy to attempt to get the system and/or component
	UObject* Curr = DataInterface->GetOuter();
	while (Curr)
	{
		Asset = Cast<UNiagaraSystem>(Curr);
		if (Asset)
		{
			break;
		}

		Component = Cast<UNiagaraComponent>(Curr);
		if (Component)
		{
			Asset = Component->GetAsset();
			break;
		}

		Curr = Curr->GetOuter();
	}

	DataInterface->GetFeedback(Asset, Component, Errors, Warnings, Info);
}

void UNiagaraDataInterface::ValidateFunction(const FNiagaraFunctionSignature& Function, TArray<FText>& OutValidationErrors)
{
	TArray<FNiagaraFunctionSignature> DIFuncs;
	GetFunctions(DIFuncs);

	if (!DIFuncs.ContainsByPredicate([&](const FNiagaraFunctionSignature& Sig) { return Sig.EqualsIgnoringSpecifiers(Function); }))
	{
		//We couldn't find this signature in the list of available functions.
		//Lets try to find one with the same name whose parameters may have changed.
		int32 ExistingSigIdx = DIFuncs.IndexOfByPredicate([&](const FNiagaraFunctionSignature& Sig) { return Sig.GetName() == Function.GetName(); });;
		if (ExistingSigIdx != INDEX_NONE)
		{
			OutValidationErrors.Add(FText::Format(LOCTEXT("DI Function Parameter Mismatch!", "Data Interface function called but it's parameters do not match any available function!\nThe API for this data interface function has likely changed and you need to update your graphs.\nInterface: {0}\nFunction: {1}\n"), FText::FromString(GetClass()->GetName()), FText::FromString(Function.GetName())));
		}
		else
		{
			OutValidationErrors.Add(FText::Format(LOCTEXT("Unknown DI Function", "Unknown Data Interface function called!\nThe API for this data interface has likely changed and you need to update your graphs.\nInterface: {0}\nFunction: {1}\n"), FText::FromString(GetClass()->GetName()), FText::FromString(Function.GetName())));
		}
	}
}

void UNiagaraDataInterface::RefreshErrors()
{
	OnErrorsRefreshedDelegate.Broadcast();
}

FSimpleMulticastDelegate& UNiagaraDataInterface::OnErrorsRefreshed()
{
	return OnErrorsRefreshedDelegate;
}

#endif

#undef LOCTEXT_NAMESPACE

