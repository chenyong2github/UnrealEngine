// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

struct FNNIModelRaw;
namespace UE::NNECore { class FAttributeMap; }

namespace NNX
{
	
//TODO create a dedicated FOptimizerOptionsMap when function diverge with FAttributeMap
//example introduction of sparse tensor to FMLAttribute witch make no sense as an optimizer attribute
using FOptimizerOptionsMap = UE::NNECore::FAttributeMap;

/** Interface class for NNX model validator */
class IModelValidator
{
public:
	virtual ~IModelValidator() = default;
	virtual FString GetName() const = 0;
	virtual bool ValidateModel(const FNNIModelRaw& InputModel, const FOptimizerOptionsMap& Options) const = 0;
};

/** Interface class for NNX model optimizer pass */
class IModelOptimizerPass
{
public:
	virtual ~IModelOptimizerPass() = default;
	virtual FString GetName() const = 0;

	//Optimize the model in place, potentially changing the format
	virtual bool ApplyPass(FNNIModelRaw& Model, const FOptimizerOptionsMap& Options) const = 0;
};

/** Interface class for NNX model optimizer */
class IModelOptimizer
{
public:
	virtual ~IModelOptimizer() = default;
	virtual FString GetName() const = 0;

	//Allow to extend/customize an optimizer by adding passes. They should be executed in order.
	virtual void AddOptimizationPass(TSharedPtr<IModelOptimizerPass> ModelOptimizerPass) = 0;
	
	//Allow to extend/customize an optimizer all validators should be run between each pass.
	virtual void AddValidator(TSharedPtr<IModelValidator>) = 0;
	
	//Apply all passes and validators to the input model, produce an optimized model potentially in a different format
	virtual bool Optimize(const FNNIModelRaw& InputModel, FNNIModelRaw& OutModel, const FOptimizerOptionsMap& Options) = 0;
};

} // NNX
