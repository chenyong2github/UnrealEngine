// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_SLATE_WIDGET_TRACKING

#include "Widgets/Accessibility/SlateWidgetTracker.h"
#include "Types/ISlateMetaData.h"
#include "Widgets/SWidget.h"

FSlateWidgetTracker::FSlateWidgetTracker() {}
FSlateWidgetTracker::~FSlateWidgetTracker() {}

FSlateWidgetTracker& FSlateWidgetTracker::Get()
{
	static FSlateWidgetTracker Singleton;
	return Singleton;
}

void FSlateWidgetTracker::AddLooseWidget(const SWidget* LooseWidget)
{
	const TArray<TSharedRef<ISlateMetaData>>& MetaDataInterfaces = LooseWidget->GetAllMetaDataInterfaces();
	for (const TSharedRef<ISlateMetaData>& MetaDataInterface : MetaDataInterfaces)
	{
		TArray<FName> MetaDataTypeIds;
		MetaDataInterface->GetMetaDataTypeIds(MetaDataTypeIds);
		for (FName& MetaDataTypeId : MetaDataTypeIds)
		{
			if (TrackedWidgets.Contains(MetaDataTypeId))
			{
				TrackedWidgets[MetaDataTypeId].Add(LooseWidget);
				NotifyTrackedWidgetsChange(LooseWidget, MetaDataTypeId, ETrackedSlateWidgetOperations::AddedTrackedWidget);
			}
		}
	}
	LooseWidgets.Add(LooseWidget);
}

void FSlateWidgetTracker::RemoveLooseWidget(const SWidget* LooseWidget)
{
	const TArray<TSharedRef<ISlateMetaData>>& MetaDataInterfaces = LooseWidget->GetAllMetaDataInterfaces();
	for (const TSharedRef<ISlateMetaData>& MetaDataInterface : MetaDataInterfaces)
	{
		TArray<FName> MetaDataTypeIds;
		MetaDataInterface->GetMetaDataTypeIds(MetaDataTypeIds);
		for (FName& MetaDataTypeId : MetaDataTypeIds)
		{
			if (TrackedWidgets.Contains(MetaDataTypeId))
			{
				TrackedWidgets[MetaDataTypeId].Remove(LooseWidget);
				NotifyTrackedWidgetsChange(LooseWidget, MetaDataTypeId, ETrackedSlateWidgetOperations::RemovedTrackedWidget);
			}
		}
	}
	LooseWidgets.Remove(LooseWidget);
}

void FSlateWidgetTracker::MetaDataAddedToWidget(const SWidget* Widget, const TSharedRef<ISlateMetaData>& AddedMetaData)
{
	TArray<FName> MetaDataTypeIds;
	AddedMetaData->GetMetaDataTypeIds(MetaDataTypeIds);
	for (FName& MetaDataTypeId : MetaDataTypeIds)
	{
		if (TrackedWidgets.Contains(MetaDataTypeId))
		{
			TrackedWidgets[MetaDataTypeId].Add(Widget);
			NotifyTrackedWidgetsChange(Widget, MetaDataTypeId, ETrackedSlateWidgetOperations::AddedTrackedWidget);
		}
	}
}

void FSlateWidgetTracker::MetaDataRemovedFromWidget(const SWidget* Widget, const TSharedRef<ISlateMetaData>& RemovedMetaData)
{
	TArray<FName> MetaDataTypeIds;
	RemovedMetaData->GetMetaDataTypeIds(MetaDataTypeIds);
	for (FName& MetaDataTypeId : MetaDataTypeIds)
	{
		if (TrackedWidgets.Contains(MetaDataTypeId))
		{
			TrackedWidgets[MetaDataTypeId].Remove(Widget);
			NotifyTrackedWidgetsChange(Widget, MetaDataTypeId, ETrackedSlateWidgetOperations::RemovedTrackedWidget);
		}
	}
}

const TArray<const SWidget*>* FSlateWidgetTracker::GetTrackedWidgetsWithMetaData_Internal(const FName& MetaDataTypeId)
{
	return TrackedWidgets.Find(MetaDataTypeId);
}

void FSlateWidgetTracker::NotifyTrackedWidgetsChange(const SWidget* TrackedWidget, const FName& MetaDataTypeId, ETrackedSlateWidgetOperations Operation)
{
	if (FTrackedWidgetListener* TrackedWidgetListener = TrackedWidgetListeners.Find(MetaDataTypeId))
	{
		TrackedWidgetListener->Broadcast(TrackedWidget, MetaDataTypeId, Operation);
	}
}

void FSlateWidgetTracker::RegisterTrackedMetaData(const FName& MetaDataTypeId)
{
	if (!TrackedWidgets.Contains(MetaDataTypeId))
	{
		TrackedWidgets.Add(MetaDataTypeId, {});
		for (const SWidget* Widget : LooseWidgets)
		{
			if (ensure(Widget))
			{
				if (Widget->GetAllMetaDataInterfaces().ContainsByPredicate([&MetaDataTypeId](const TSharedRef<ISlateMetaData>& MetaData) { return MetaData->IsOfTypeName(MetaDataTypeId); }))
				{
					TrackedWidgets[MetaDataTypeId].Add(Widget);
				}
			}
		}
	}
}

void FSlateWidgetTracker::UnregisterTrackedMetaData(const FName& MetaDataTypeId)
{
	TrackedWidgets.Remove(MetaDataTypeId);
}

#endif //WITH_SLATE_WIDGET_TRACKING
