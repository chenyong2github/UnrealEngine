// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "LensModel.generated.h"


class ULensDistortionModelHandlerBase;


/**
 * Abstract base class for lens models
 */
UCLASS(Abstract)
class CAMERACALIBRATION_API ULensModel : public UObject
{
	GENERATED_BODY()

public:
	/** Get the lens model name */
	virtual FName GetModelName() const PURE_VIRTUAL(ULensModel::GetModelName, return FName(""););

	/** Get the struct of distortion parameters supported by this model */
	virtual UScriptStruct* GetParameterStruct() const PURE_VIRTUAL(ULensModel::GetParameterStruct, return nullptr;);

	/** Get the number of float fields in the parameter struct supported by this model */
	virtual uint32 GetNumParameters() const;

	/** 
	 * Fill the destination array of floats with the values of the fields in the source struct 
	 * Note: the template type must be a UStruct
	 */
	template<typename StructType>
	void ToArray(StructType& SrcData, TArray<float>& DstArray) const
	{
		ToArray_Internal(StructType::StaticStruct(), &SrcData, DstArray);
	}

	/**
	 * Populate the float fields in the destination struct with the values in the source array
	 * Note: the template type must be a UStruct
	 */
	template<typename StructType>
	void FromArray(const TArray<float>& SrcArray, StructType& DstData)
	{
		FromArray_Internal(StructType::StaticStruct(), SrcArray, &DstData);
	}

	/** Returns the first handler that supports the given LensModel */
	static TSubclassOf<ULensDistortionModelHandlerBase> GetHandlerClass(TSubclassOf<ULensModel> LensModel);

protected:
	/** Internal implementation of ToArray. See declaration of public template method. */
	virtual void ToArray_Internal(UScriptStruct* TypeStruct, void* SrcData, TArray<float>& DstArray) const;

	/** Internal implementation of FromArray. See declaration of public template method. */
	virtual void FromArray_Internal(UScriptStruct* TypeStruct, const TArray<float>& SrcArray, void* DstData);
};
