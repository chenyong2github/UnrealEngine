// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_SLATE_WIDGET_TRACKING

#include "CoreMinimal.h"

class SWidget;
class ISlateMetaData;

enum class ETrackedSlateWidgetOperations : uint8
{
	AddedTrackedWidget,
	RemovedTrackedWidget
};

DECLARE_MULTICAST_DELEGATE_ThreeParams(FTrackedWidgetListener, const SWidget*, const FName&, ETrackedSlateWidgetOperations)

class SLATECORE_API FSlateWidgetTracker
{

public:

	virtual ~FSlateWidgetTracker();
	static FSlateWidgetTracker& Get();

	void AddLooseWidget(const SWidget* LooseWidget);
	void RemoveLooseWidget(const SWidget* LooseWidget);

	void MetaDataAddedToWidget(const SWidget* Widget, const TSharedRef<ISlateMetaData>& AddedMetaData);
	void MetaDataRemovedFromWidget(const SWidget* Widget, const TSharedRef<ISlateMetaData>& RemovedMetaData);

private:

	/** Singleton access only. */
	FSlateWidgetTracker();

	const TArray<const SWidget*>* GetTrackedWidgetsWithMetaData_Internal(const FName& MetaDataTypeId);

	void NotifyTrackedWidgetsChange(const SWidget* TrackedWidget, const FName& MetaDataTypeId, ETrackedSlateWidgetOperations Operation);

	void RegisterTrackedMetaData(const FName& MetaDataTypeId);
	void UnregisterTrackedMetaData(const FName& MetaDataTypeId);

	TMap<FName, TArray<const SWidget*>> TrackedWidgets;
	TArray<const SWidget*> LooseWidgets;

	TMap<FName, FTrackedWidgetListener> TrackedWidgetListeners;

public:

	template <typename TMetaDataType>
	const TArray<const SWidget*>* GetTrackedWidgetsWithMetaData()
	{
		return GetTrackedWidgetsWithMetaData_Internal(TMetaDataType::GetTypeId());
	}

	template <typename TMetaData>
	FTrackedWidgetListener& AddTrackedWidgetListener()
	{
		RegisterTrackedMetaData(TMetaData::GetTypeId());
		return TrackedWidgetListeners.FindOrAdd(TMetaData::GetTypeId());
	}

	template <typename TMetaData>
	void RemoveTrackedWidgetListener(FDelegateHandle Handle)
	{
		if (Handle.IsValid())
		{
			if (FTrackedWidgetListener* TrackedWidgetListener = TrackedWidgetListeners.Find(TMetaData::GetTypeId()))
			{
				TrackedWidgetListener->Remove(Handle);
				if (!TrackedWidgetListener->IsBound())
				{
					UnregisterTrackedMetaData(TMetaData::GetTypeId());
					TrackedWidgetListeners.Remove(TMetaData::GetTypeId());
				}
			}
		}
	}

	template <typename TMetaData, typename TListenerOwner>
	void RemoveAllTrackedWidgetListenersForObject(TListenerOwner* OwnerObject)
	{
		if (OwnerObject != nullptr)
		{
			if (FTrackedWidgetListener* TrackedWidgetListener = TrackedWidgetListeners.Find(TMetaData::GetTypeId()))
			{
				TrackedWidgetListener->RemoveAll(OwnerObject);
				if (!TrackedWidgetListener->IsBound())
				{
					UnregisterTrackedMetaData(TMetaData::GetTypeId());
					TrackedWidgetListeners.Remove(TMetaData::GetTypeId());
				}
			}
		}
	}
};

#endif //WITH_SLATE_WIDGET_TRACKING
