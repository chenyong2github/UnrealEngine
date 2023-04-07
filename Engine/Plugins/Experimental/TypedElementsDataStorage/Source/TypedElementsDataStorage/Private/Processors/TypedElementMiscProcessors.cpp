// Copyright Epic Games, Inc. All Rights Reserved.

#include "Processors/TypedElementMiscProcessors.h"

#include "Elements/Columns/TypedElementMiscColumns.h"
#include "Elements/Framework/TypedElementQueryBuilder.h"

void UTypedElementRemoveSyncToWorldTagFactory::RegisterQueries(ITypedElementDataStorageInterface& DataStorage) const
{
	using namespace TypedElementQueryBuilder;
	using DSI = ITypedElementDataStorageInterface;

	DataStorage.RegisterQuery(
		Select(
			TEXT("Remove 'sync to world' tag"),
			FPhaseAmble(FPhaseAmble::ELocation::Postamble, DSI::EQueryTickPhase::FrameEnd),
			[](DSI::IQueryContext& Context, const TypedElementRowHandle* Rows)
			{
				Context.RemoveColumns<FTypedElementSyncBackToWorldTag>(TConstArrayView<TypedElementRowHandle>(Rows, Context.GetRowCount()));
			}
		)
		.Where()
			.All<FTypedElementSyncBackToWorldTag>()
		.Compile()
	);
}
