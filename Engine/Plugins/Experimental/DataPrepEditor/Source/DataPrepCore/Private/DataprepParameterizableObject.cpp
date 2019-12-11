// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DataprepParameterizableObject.h"

#include "DataPrepAsset.h"
#include "DataprepCoreUtils.h"
#include "Parameterization/DataprepParameterization.h"
#include "Parameterization/DataprepParameterizationUtils.h"

#include "UObject/UnrealType.h"

void UDataprepParameterizableObject::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty( PropertyChangedEvent );

	if ( !bool( PropertyChangedEvent.ChangeType & ( EPropertyChangeType::Interactive | EPropertyChangeType::Redirected ) ) )
	{
		UDataprepAsset* DataprepAsset = FDataprepCoreUtils::GetDataprepAssetOfObject( this );
		UDataprepParameterization * Parameterization = nullptr;
		if ( DataprepAsset )
		{
			Parameterization = DataprepAsset->GetDataprepParameterization();
		}

		if ( !Parameterization )
		{
			UClass* Class = GetClass();
			FString PathToDataprepParametrization = Class->GetMetaData( UDataprepParameterization::MetadataClassGeneratorName );
			Parameterization = FindObject<UDataprepParameterization>( nullptr, *PathToDataprepParametrization );
			
		}

		if ( Parameterization )
		{
			TArray<FDataprepPropertyLink> PropertyChain = FDataprepParameterizationUtils::MakePropertyChain( PropertyChangedEvent );
			Parameterization->OnObjectPostEdit( this, PropertyChain, PropertyChangedEvent.ChangeType );
		}
	}
}
