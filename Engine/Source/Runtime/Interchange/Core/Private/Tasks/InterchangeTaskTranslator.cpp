// Copyright Epic Games, Inc. All Rights Reserved.
#include "InterchangeTaskTranslator.h"

#include "Async/TaskGraphInterfaces.h"
#include "CoreMinimal.h"
#include "InterchangeManager.h"
#include "InterchangeSourceData.h"
#include "InterchangeTranslatorBase.h"
#include "Stats/Stats.h"
#include "Templates/SharedPointer.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "Nodes/InterchangeBaseNodeContainer.h"

void Interchange::FTaskTranslator::DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	TSharedPtr<Interchange::FImportAsyncHelper, ESPMode::ThreadSafe> AsyncHelper = WeakAsyncHelper.Pin();
	check(AsyncHelper.IsValid());

	UInterchangeTranslatorBase* Translator = AsyncHelper->Translators.IsValidIndex(SourceIndex) ? AsyncHelper->Translators[SourceIndex] : nullptr;
	if (!Translator)
	{
		return;
	}
	const UInterchangeSourceData* SourceData = AsyncHelper->SourceDatas.IsValidIndex(SourceIndex) ? AsyncHelper->SourceDatas[SourceIndex] : nullptr;
	if (!SourceData)
	{
		return;
	}

	if (!AsyncHelper->BaseNodeContainers.IsValidIndex(SourceIndex) || !AsyncHelper->BaseNodeContainers[SourceIndex].IsValid())
	{
		return;
	}

	//Translate the source data
	UInterchangeBaseNodeContainer& BaseNodeContainer = *(AsyncHelper->BaseNodeContainers[SourceIndex].Get());
	Translator->Translate(SourceData, BaseNodeContainer);
}
