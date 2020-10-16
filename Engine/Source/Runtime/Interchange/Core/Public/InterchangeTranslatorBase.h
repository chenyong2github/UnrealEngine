// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "InterchangeSourceData.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "InterchangeTranslatorBase.generated.h"

UCLASS(BlueprintType, Blueprintable, Abstract)
class INTERCHANGECORE_API UInterchangeTranslatorBase : public UObject
{
	GENERATED_BODY()
public:

	/** return true if the translator can translate the specified file. */
	virtual bool CanImportSourceData(const UInterchangeSourceData* SourceData) const
	{
		return false;
	}

	/**
	 * Translate a source data into node(s) that are hold in the specified nodes container.
	 *
	 * @param SourceData - The source data containing the data to translate
	 * @param BaseNodeContainer - The nodes container where to put the translated source data result.
	 * @return true if the translator can translate the source data, false otherwise.
	 */
	virtual bool Translate(const UInterchangeSourceData* SourceData, UInterchangeBaseNodeContainer& BaseNodeContainer) const
	{
		return false;
	}

	/**
	 * Release source is called when we do not need anymore the translator source and also early in the cancel process.
	 * This is where out of process translator will send the stop command.
	 * A good example of why its useful to release the source is when the dispatcher delete the worker, the join on the
	 * thread will be very short and will not block the main thread. If the worker stop command was send before the completion task.
	 */
	virtual void ReleaseSource()
	{
		return;
	}

	/**
	 * This function is call when the import is done (FImportAsyncHelper::CleanUp) and we are cleaning the data.
	 * Use it to free resource that need to be release before the next garbage collector pass.
	 */
	virtual void ImportFinish()
	{
		return;
	}
};
