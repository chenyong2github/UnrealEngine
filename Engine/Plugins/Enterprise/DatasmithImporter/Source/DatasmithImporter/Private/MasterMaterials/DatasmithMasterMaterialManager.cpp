// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MasterMaterials/DatasmithMasterMaterialManager.h"

#include "MasterMaterials/DatasmithMasterMaterialSelector.h"

TUniquePtr< FDatasmithMasterMaterialManager > FDatasmithMasterMaterialManager::Instance;

void FDatasmithMasterMaterialManager::Create()
{
	Instance = MakeUnique< FDatasmithMasterMaterialManager >();
}

void FDatasmithMasterMaterialManager::Destroy()
{
	Instance.Reset();
}

FDatasmithMasterMaterialManager& FDatasmithMasterMaterialManager::Get()
{
	check( Instance.IsValid() );
	return *Instance.Get();
}

FString FDatasmithMasterMaterialManager::GetHostFromString( const FString& HostString )
{
	if ( HostString.Contains( TEXT("CityEngine") ) )
	{
		return TEXT("CityEngine");
	}
	else if ( HostString.Contains( TEXT("Deltagen") ) )
	{
		return TEXT("Deltagen");
	}
	else if ( HostString.Contains(TEXT("VRED") ) )
	{
		return TEXT("VRED");
	}
	else
	{
		return HostString;
	}
}

void FDatasmithMasterMaterialManager::RegisterSelector( const TCHAR* Host, TSharedPtr< FDatasmithMasterMaterialSelector > Selector )
{
	Selectors.FindOrAdd( Host ) = Selector;
}

void FDatasmithMasterMaterialManager::UnregisterSelector( const TCHAR* Host )
{
	Selectors.Remove( Host );
}

const TSharedPtr< FDatasmithMasterMaterialSelector > FDatasmithMasterMaterialManager::GetSelector( const TCHAR* Host ) const
{
	if ( Selectors.Contains( Host ) )
	{
		return Selectors[ Host ];
	}
	else
	{
		return MakeShared< FDatasmithMasterMaterialSelector >();
	}
}
