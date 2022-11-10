// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ControlRigDefines.h"
#include "RigVMCore/RigVMDispatchFactory.h"
#include "RigUnitContext.h"
#include "ControlRig.h"
#include "RigDispatchFactory.generated.h"

/** Base class for all rig dispatch factories */
USTRUCT(BlueprintType, meta=(Abstract, BlueprintInternalUseOnlyHierarchical, ExecuteContextType=FControlRigExecuteContext))
struct CONTROLRIG_API FRigDispatchFactory : public FRigVMDispatchFactory
{
	GENERATED_BODY()

	FORCEINLINE virtual UScriptStruct* GetExecuteContextStruct() const override
	{
		return FControlRigExecuteContext::StaticStruct();
	}

	FORCEINLINE virtual void RegisterDependencyTypes() const override
	{
		FRigVMRegistry::Get().FindOrAddType(FControlRigExecuteContext::StaticStruct());
		FRigVMRegistry::Get().FindOrAddType(FRigElementKey::StaticStruct());
    	FRigVMRegistry::Get().FindOrAddType(FCachedRigElement::StaticStruct());
	}

	FORCEINLINE virtual TArray<TPair<FName,FString>> GetOpaqueArguments() const override
	{
		static const TArray<TPair<FName,FString>> OpaqueArguments = {
			TPair<FName,FString>(TEXT("Context"), TEXT("const FRigUnitContext&"))
		};
		return OpaqueArguments;
	}

#if WITH_EDITOR

	virtual FString GetArgumentDefaultValue(const FName& InArgumentName, TRigVMTypeIndex InTypeIndex) const override;

#endif

	FORCEINLINE static const FRigUnitContext& GetRigUnitContext(const FRigVMExtendedExecuteContext& InContext)
	{
		return *(const FRigUnitContext*)InContext.OpaqueArguments[0];
	}
};

