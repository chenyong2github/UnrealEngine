// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DataprepAssetInstance.h"

#include "DataPrepAsset.h"
#include "DataPrepContentConsumer.h"
#include "DataprepAssetProducers.h"
#include "DataprepCorePrivateUtils.h"
#include "Parameterization/DataprepParameterization.h"

// UDataprepAssetInstance =================================================================

void UDataprepAssetInstance::ExecuteRecipe(const TSharedPtr<FDataprepActionContext>& InActionsContext)
{
	check( Parent );

	// Doing the parameterizaton
	TMap<UObject*,UObject*> SourceToCopy;
	ActionsFromDataprepAsset = GetCopyOfActions( SourceToCopy );
	Parameterization->ApplyParameterization( SourceToCopy );

	ExecuteRecipe_Internal( InActionsContext, ActionsFromDataprepAsset );

	ActionsFromDataprepAsset.Empty();
}

UObject* UDataprepAssetInstance::GetParameterizationObject()
{
	return Parameterization->GetParameterizationInstance();
}

TArray<UDataprepActionAsset*> UDataprepAssetInstance::GetCopyOfActions(TMap<UObject*,UObject*>& OutOriginalToCopy) const
{
	check( Parent );
	return Parent->GetCopyOfActions( OutOriginalToCopy );
}

bool UDataprepAssetInstance::SetParent(UDataprepAssetInterface* InParent, bool bNotifyChanges )
{
	check(InParent);

	// Copy set of producers 
	if(Inputs != nullptr)
	{
		Inputs->GetOnChanged().RemoveAll( this );
		DataprepCorePrivateUtils::DeleteRegisteredAsset( Inputs );
	}
	Inputs = DuplicateObject<UDataprepAssetProducers>( InParent->GetProducers(), this );

	// Copy consumer 
	if(Output != nullptr)
	{
		Output->GetOnChanged().RemoveAll( this );
		DataprepCorePrivateUtils::DeleteRegisteredAsset( Output );
	}
	Output = DuplicateObject<UDataprepContentConsumer>( InParent->GetConsumer(), this );

	UDataprepAssetInterface* RealParent = InParent;

	// If InParent is an instance, get the up to the original parent
	if(UDataprepAssetInstance* InstanceOfInstance = Cast<UDataprepAssetInstance>(InParent))
	{
		// #ueent_todo: Copy values from InstanceOfInstance's parameterization to this

		do
		{
			RealParent = InstanceOfInstance->Parent;
			InstanceOfInstance = Cast<UDataprepAssetInstance>(InstanceOfInstance->Parent);
		}
		while(InstanceOfInstance != nullptr);

		check(RealParent);
	}

	Parent = RealParent;
	
	check ( Parent->GetClass() == UDataprepAsset::StaticClass() );
	Parameterization = NewObject<UDataprepParameterizationInstance>( this, NAME_None, RF_Public | RF_Transactional );
	Parameterization->SetParameterizationSource( *( static_cast<UDataprepAsset*>( Parent )->GetDataprepParameterization() ) );
	

	if(bNotifyChanges)
	{
		OnChanged.Broadcast( FDataprepAssetChangeType::ProducerModified );
		OnChanged.Broadcast( FDataprepAssetChangeType::ConsumerModified );
		OnParentChanged.Broadcast();
	}

	return true;
}
