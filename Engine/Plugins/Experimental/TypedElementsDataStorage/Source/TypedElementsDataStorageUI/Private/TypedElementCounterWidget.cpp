// Copyright Epic Games, Inc. All Rights Reserved.

#include "TypedElementCounterWidget.h"

#include "Elements/Columns/TypedElementSlateWidgetColumns.h"
#include "Elements/Columns/TypedElementValueCacheColumns.h"
#include "Elements/Framework/TypedElementQueryBuilder.h"
#include "Framework/Text/TextLayout.h"
#include "Layout/Margin.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Text/STextBlock.h"
#include "TypedElementSubsystems.h"

#define LOCTEXT_NAMESPACE "TypedElementUI_CounterWidget"


//
// UTypedElementCounterWidgetFactory
//

void UTypedElementCounterWidgetFactory::RegisterQueries(ITypedElementDataStorageInterface& DataStorage) const
{
	using namespace TypedElementQueryBuilder;
	using DSI = ITypedElementDataStorageInterface;

	DataStorage.RegisterQuery(Select(TEXT("Sync Counter Widgets"), 
		FProcessor(DSI::EQueryTickPhase::FrameEnd, DataStorage.GetQueryTickGroupName(DSI::EQueryTickGroups::SyncWidgets)).ForceToGameThread(true),
		[](
			FCachedQueryContext<UTypedElementDataStorageSubsystem>& Context, 
			FTypedElementSlateWidgetReferenceColumn& Widget,
			FTypedElementU32IntValueCacheColumn& Comparison, 
			const FTypedElementCounterWidgetColumn& Counter
		)
		{
			UTypedElementDataStorageSubsystem& Subsystem = Context.GetCachedMutableDependency<UTypedElementDataStorageSubsystem>();
			DSI* DataInterface = Subsystem.Get();
			checkf(DataInterface, TEXT("FTypedElementsDataStorageUiModule tried to process widgets before the "
				"Typed Elements Data Storage interface is available."));

			DSI::FQueryResult Result = DataInterface->RunQuery(Counter.Query);
			if (Result.Completed == DSI::FQueryResult::ECompletion::Fully && Result.Count != Comparison.Value)
			{
				TSharedPtr<SWidget> WidgetPointer = Widget.Widget.Pin();
				checkf(WidgetPointer, TEXT("Referenced widget is not valid. A constructed widget may not have been cleaned up. This can "
					"also happen if this processor is running in the same phase as the processors responsible for cleaning up old "
					"references."));
				checkf(WidgetPointer->GetType() == STextBlock::StaticWidgetClass().GetWidgetType(),
					TEXT("Stored widget with FTypedElementCounterWidgetFragment doesn't match type %s, but was a %s."),
					*(STextBlock::StaticWidgetClass().GetWidgetType().ToString()),
					*(WidgetPointer->GetTypeAsString()));

				STextBlock* WidgetInstance = static_cast<STextBlock*>(WidgetPointer.Get());
				WidgetInstance->SetText(FText::Format(Counter.LabelTextFormatter, Result.Count));
				Comparison.Value = Result.Count;
			}
		}
	).Compile());
}

void UTypedElementCounterWidgetFactory::RegisterWidgetConstructor(ITypedElementDataStorageInterface& DataStorage,
	ITypedElementDataStorageUiInterface& DataStorageUi) const
{
	using namespace TypedElementQueryBuilder;

	FName Purpose("LevelEditor.StatusBar.ToolBar");

	TUniquePtr<FTypedElementCounterWidgetConstructor> ActorCounter = MakeUnique<FTypedElementCounterWidgetConstructor>();
	ActorCounter->LabelText = LOCTEXT("ActorCounterStatusBarLabel", "{0} {0}|plural(one=Actor, other=Actors)");
	ActorCounter->ToolTipText = LOCTEXT(
		"ActorCounterStatusBarToolTip",
		"The total number of actors currently in the editor, excluding PIE/SIE and previews.");
	ActorCounter->Query = DataStorage.RegisterQuery(
		Count().
		Where().
		All("/Script/MassActors.MassActorFragment"_Type).
		Compile());
	DataStorageUi.RegisterWidgetFactory(Purpose, MoveTemp(ActorCounter));

	TUniquePtr<FTypedElementCounterWidgetConstructor> WidgetCounter = MakeUnique<FTypedElementCounterWidgetConstructor>();
	WidgetCounter->LabelText = LOCTEXT("WidgetCounterStatusBarLabel", "{0} {0}|plural(one=Widget, other=Widgets)");
	WidgetCounter->ToolTipText = LOCTEXT(
		"WidgetCounterStatusBarToolTip",
		"The total number of widgets in the editor hosted through the Typed Element's Data Storage.");
	WidgetCounter->Query = DataStorage.RegisterQuery(
		Count().
		Where().
		All<FTypedElementSlateWidgetReferenceColumn>().
		Compile());
	DataStorageUi.RegisterWidgetFactory(Purpose, MoveTemp(WidgetCounter));
}


//
// FTypedElementCounterWidgetConstructor
//

TSharedPtr<SWidget> FTypedElementCounterWidgetConstructor::CreateWidget()
{
	return SNew(STextBlock)
		.Text(FText::Format(LabelText, 0))
		.Margin(FMargin(4.0f, 0.0f))
		.Justification(ETextJustify::Center);
}

void FTypedElementCounterWidgetConstructor::AddColumns(
	ITypedElementDataStorageInterface* DataStorage, TypedElementRowHandle Row, const TSharedPtr<SWidget>& Widget)
{
	Super::AddColumns(DataStorage, Row, Widget);

	FTypedElementCounterWidgetColumn* CounterColumn = DataStorage->AddOrGetColumn<FTypedElementCounterWidgetColumn>(Row);
	checkf(CounterColumn, TEXT("Added a new FTypedElementCounterWidgetColumn to the Typed Elements Data Storage, but didn't get a valid pointer back."));
	CounterColumn->LabelTextFormatter = LabelText;
	CounterColumn->Query = Query;

	FTypedElementU32IntValueCacheColumn* CacheColumn = DataStorage->AddOrGetColumn<FTypedElementU32IntValueCacheColumn>(Row);
	checkf(CacheColumn, TEXT("Added a new FTypedElementUnsigned32BitIntValueCache to the Typed Elements Data Storage, but didn't get a valid pointer back."));
	CacheColumn->Value = 0;
}

#undef LOCTEXT_NAMESPACE