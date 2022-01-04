// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "NiagaraCommon.h"
#include "NiagaraShared.h"
#include "NiagaraDataInterface.h"
#include "NiagaraParameterStore.h"
#include "NiagaraDataInterfaceUObjectPropertyReader.generated.h"

USTRUCT()
struct FNiagaraUObjectPropertyReaderRemap
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Remap")
	FName GraphName = NAME_None;

	UPROPERTY(EditAnywhere, Category = "Remap")
	FName RemapName = NAME_None;

	bool operator==(const FNiagaraUObjectPropertyReaderRemap& Other) const
	{
		return GraphName == Other.GraphName && RemapName == Other.RemapName;
	}
};

/**
Data interface to read properties from UObjects.
Rather than having BP tick functions that push data into Niagara this data interface will instead pull them.
*/
UCLASS(EditInlineNew, Category = "DataInterface", meta=(DisplayName="Object Reader", Experimental))
class UNiagaraDataInterfaceUObjectPropertyReader : public UNiagaraDataInterface
{
	GENERATED_UCLASS_BODY()

public:
	DECLARE_NIAGARA_DI_PARAMETER();

	/** User parameter Object binding to read properties from. */
	UPROPERTY(EditAnywhere, Category = "UObjectPropertyReader")
	FNiagaraUserParameterBinding UObjectParameterBinding;

	UPROPERTY(EditAnywhere, Category = "UObjectPropertyReader")
	TArray<FNiagaraUObjectPropertyReaderRemap> PropertyRemap;

	/** Optional source actor to use, if the user parameter binding is valid this will be ignored. */
	UPROPERTY(EditAnywhere, Category = "UObjectPropertyReader")
	TLazyObjectPtr<AActor> SourceActor;

	/**
	When an actor is bound as the object we will also search for a component of this type to bind properties to.
	For example, setting this to a UPointLightComponent when binding properties we will first look at the actor
	then look for a component of UPointLightComponent and look at properties on that also.
	If no class is specified here we look at the RootComponent instead.
	*/
	UPROPERTY(EditAnywhere, Category = "UObjectPropertyReader")
	TObjectPtr<UClass> SourceActorComponentClass;

	//UNiagaraDataInterface Interface
	virtual void PostInitProperties() override;
	// UObject Interface End

	// UNiagaraDataInterface Interface Begin
	virtual bool CanExecuteOnTarget(ENiagaraSimTarget Target) const override { return true; }

	virtual bool Equals(const UNiagaraDataInterface* Other) const override;
	virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const override;

	virtual bool InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual void DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual int32 PerInstanceDataSize() const override;

	virtual void ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FNiagaraSystemInstanceID& InstanceID) override;

	virtual bool HasPreSimulateTick() const override { return true; }
	virtual bool PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds) override;

	virtual void GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions) override;
	virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction& OutFunc) override;
#if WITH_EDITORONLY_DATA
	//virtual bool AppendCompileHash(FNiagaraCompileHashVisitor* InVisitor) const override;
	virtual void GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) override;
	virtual bool GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL) override;
#endif
	//UNiagaraDataInterface Interface

	/** Remaps a property reader, where the  */
	UFUNCTION(BlueprintCallable, Category = Niagara)
	static void SetUObjectReaderPropertyRemap(UNiagaraComponent* NiagaraComponent, FName UserParameterName, FName GraphName, FName RemapName);

private:
	void VMGetComponentTransform(FVectorVMExternalFunctionContext& Context);

	FName GetRemappedPropertyName(FName InName) const
	{
		for ( const FNiagaraUObjectPropertyReaderRemap& RemapEntry : PropertyRemap )
		{
			if ( RemapEntry.GraphName == InName )
			{
				return RemapEntry.RemapName;
			}
		}
		return InName;
	};

	uint32 ChangeId = 0;
};
