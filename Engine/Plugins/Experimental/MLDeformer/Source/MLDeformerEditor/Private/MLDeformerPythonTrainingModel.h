// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MLDeformer/Public/MLDeformerAsset.h"
#include "MLDeformerPythonTrainingModel.generated.h"

class UMLPytorchDataSetInterface;
class FMLDeformerEditorData;
class UMLDeformerAsset;
class FMLDeformerFrameCache;

/** Training process return codes. */
UENUM()
enum class ETrainingResult : uint8
{
	Success = 0,	/** The training successfully finished. */
	Aborted,		/** The user has aborted the training process. */
	AbortedCantUse,	/** The user has aborted the training process and we can't use the resulting network. */
	FailOnData,		/** The input or output data to the network has issues, which means we cannot train. */
	FailUnknown		/** There is an unknown error (see output log). */
};

/**
 * 
 */
UCLASS(Blueprintable)
class MLDEFORMEREDITOR_API UMLDeformerPythonTrainingModel : public UObject
{
	GENERATED_BODY()
public:
	UMLDeformerPythonTrainingModel();
	~UMLDeformerPythonTrainingModel();

	void Clear();

	UFUNCTION(BlueprintCallable, Category = Python)
	static UMLDeformerPythonTrainingModel* Get();

	/** 
	 * Train model using training settings and metadata from the interface.
	 * This function is implemented in the python scripts.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = Python)
	int32 Train() const;

	/** 
	 * Create the dataset interface from the editor data.
	 * Such an interface provides the samples for training the model.
	 */
	UFUNCTION(BlueprintCallable, Category = DataSet)
	void CreateDataSetInterface();

	/** Get the current ML deformer asset. **/
	UFUNCTION(BlueprintCallable, Category = Python)
	UMLDeformerAsset* GetDeformerAsset();

	UPROPERTY(BlueprintReadOnly, Category = Python)
	UMLPytorchDataSetInterface* DataSetInterface = nullptr;

	void SetEditorData(TSharedPtr<FMLDeformerEditorData> InEditorData);
	void SetFrameCache(TSharedPtr<FMLDeformerFrameCache> InFrameCache);

private:
	TSharedPtr<FMLDeformerEditorData> EditorData = nullptr;
	TSharedPtr<FMLDeformerFrameCache> FrameCache = nullptr;
};
