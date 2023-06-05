// Copyright Epic Games, Inc. All Rights Reserved.

#include "VerseWorldPartition.h"
#include "Engine/World.h"
#include "WorldPartition/DataLayer/DataLayerAsset.h"
#include "WorldPartition/DataLayer/DataLayerManager.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"
#include "Logging/LogMacros.h"

DEFINE_LOG_CATEGORY_STATIC(LogVerseWorldPartition, Log, All);

UVerseDataLayerManagerBase::UVerseDataLayerManagerBase(class FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UVerseDataLayerManagerBase::Initialize()
{
	UDataLayerManager* DataLayerManager = GetOuterUDataLayerManager();
	if (ensure(DataLayerManager))
	{
		DataLayerManager->AddReferencedObject(this);
	}
}

UVerseDataLayerManagerBase* UVerseDataLayerManagerBase::GetVerseDataLayerManager(UDataLayerManager* InDataLayerManager)
{
	if (InDataLayerManager)
	{
		for (UObject* ReferencedObject : InDataLayerManager->GetReferencedObjects())
		{
			if (UVerseDataLayerManagerBase* ExistingVerseDataLayerManager = Cast<UVerseDataLayerManagerBase>(ReferencedObject))
			{
				return ExistingVerseDataLayerManager;
			}
		}
	}
	return nullptr;
}

#if WITH_VERSE

#include "WorldPartition.VerseWorldPartition.gen.h"
#include "WorldPartition.VerseWorldPartition.data_layer_runtime_state.gen.h"

namespace verse
{

void data_layer_asset::PostInitProperties()
{
	Super::PostInitProperties();

	AssetClass = UDataLayerAsset::StaticClass();
}

bool data_layer_manager::SetDataLayerRuntimeState(TNonNullPtr<data_layer_asset> DataLayerAsset, Edata_layer_runtime_state RuntimeState, bool IsRecursive)
{
	if (UDataLayerManager* DataLayerManager = GetOuterUDataLayerManager())
	{
		if (const UDataLayerInstance* DataLayerInstance = DataLayerManager->GetDataLayerInstanceFromAsset(Cast<UDataLayerAsset>(DataLayerAsset->Get())))
		{
			EDataLayerRuntimeState NewRuntimeState = (RuntimeState == Edata_layer_runtime_state::Unloaded) ? EDataLayerRuntimeState::Unloaded : (RuntimeState == Edata_layer_runtime_state::Loaded) ? EDataLayerRuntimeState::Loaded : EDataLayerRuntimeState::Activated;
			return DataLayerManager->SetDataLayerInstanceRuntimeState(DataLayerInstance, NewRuntimeState, IsRecursive);
		}
	}
	return false;
}

Edata_layer_runtime_state data_layer_manager::GetDataLayerEffectiveRuntimeState(TNonNullPtr<data_layer_asset> DataLayerAsset)
{
	if (UDataLayerManager* DataLayerManager = GetOuterUDataLayerManager())
	{
		if (const UDataLayerInstance* DataLayerInstance = DataLayerManager->GetDataLayerInstanceFromAsset(Cast<UDataLayerAsset>(DataLayerAsset->Get())))
		{
			EDataLayerRuntimeState RuntimeState = DataLayerManager->GetDataLayerInstanceEffectiveRuntimeState(DataLayerInstance);
			Edata_layer_runtime_state Result = (RuntimeState == EDataLayerRuntimeState::Unloaded) ? Edata_layer_runtime_state::Unloaded : (RuntimeState == EDataLayerRuntimeState::Loaded) ? Edata_layer_runtime_state::Loaded : Edata_layer_runtime_state::Activated;
			return Result;
		}
	}
	return Edata_layer_runtime_state::Unloaded;
}

TOptional<TNonNullPtr<verse::data_layer_manager>> VerseWorldPartition::GetDataLayerManager()
{
	TOptional<TNonNullPtr<verse::data_layer_manager>> VerseDataLayerManager;

	UWorld* World = ExecutionContext::GetActiveContext().GetScope()->GetWorldContext()->GetWorld();
	if (UDataLayerManager* DataLayerManager = World ? World->GetDataLayerManager() : nullptr)
	{
		if (verse::data_layer_manager* ExistingManager = Cast<verse::data_layer_manager>(UVerseDataLayerManagerBase::GetVerseDataLayerManager(DataLayerManager)))
		{
			VerseDataLayerManager = ExistingManager;
		}
		else
		{
			verse::data_layer_manager* NewManager = NewObject<data_layer_manager>(DataLayerManager);
			NewManager->Initialize();
			VerseDataLayerManager = NewManager;
		}
	}
	
	return VerseDataLayerManager;
}

}

#endif // WITH_VERSE