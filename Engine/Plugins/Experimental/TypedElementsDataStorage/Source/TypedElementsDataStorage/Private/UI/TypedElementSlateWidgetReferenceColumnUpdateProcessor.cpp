// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/TypedElementSlateWidgetReferenceColumnUpdateProcessor.h"

#include "Elements/Columns/TypedElementSlateWidgetColumns.h"
#include "Elements/Framework/TypedElementQueryBuilder.h"

void UTypedElementSlateWidgetReferenceColumnUpdateFactory::RegisterQueries(ITypedElementDataStorageInterface& DataStorage) const
{
	RegisterDeleteRowOnWidgetDeleteQuery(DataStorage);
	RegisterDeleteColumnOnWidgetDeleteQuery(DataStorage);
}

#pragma optimize("", off)
void UTypedElementSlateWidgetReferenceColumnUpdateFactory::RegisterDeleteRowOnWidgetDeleteQuery(ITypedElementDataStorageInterface& DataStorage) const
{
	using namespace TypedElementQueryBuilder;
	using DSI = ITypedElementDataStorageInterface;

	DataStorage.RegisterQuery(
		Select(
			TEXT("Delete row with deleted widget"),
			FProcessor(DSI::EQueryTickPhase::FrameEnd, DataStorage.GetQueryTickGroupName(DSI::EQueryTickGroups::PrepareSyncWidgets)).ForceToGameThread(true),
			[](DSI::IQueryContext& Context, TypedElementRowHandle Row, const FTypedElementSlateWidgetReferenceColumn& WidgetReference)
			{
				if (!WidgetReference.Widget.IsValid())
				{
					Context.RemoveRow(Row);
				}
			}
		)
		.Where()
			.All<FTypedElementSlateWidgetReferenceDeletesRowTag>()
		.Compile()
	);
}
#pragma optimize("", on)

void UTypedElementSlateWidgetReferenceColumnUpdateFactory::RegisterDeleteColumnOnWidgetDeleteQuery(ITypedElementDataStorageInterface& DataStorage) const
{
	using namespace TypedElementQueryBuilder;
	using DSI = ITypedElementDataStorageInterface;

	DataStorage.RegisterQuery(
		Select(
			TEXT("Delete widget column for deleted widget"),
			FProcessor(DSI::EQueryTickPhase::FrameEnd, DataStorage.GetQueryTickGroupName(DSI::EQueryTickGroups::PrepareSyncWidgets)).ForceToGameThread(true),
			[](DSI::IQueryContext& Context, TypedElementRowHandle Row, const FTypedElementSlateWidgetReferenceColumn& WidgetReference)
			{
				if (!WidgetReference.Widget.IsValid())
				{
					Context.RemoveColumns<FTypedElementSlateWidgetReferenceColumn>(Row);
				}
			}
		)
		.Where()
			.None<FTypedElementSlateWidgetReferenceDeletesRowTag>()
		.Compile()
	);
}
