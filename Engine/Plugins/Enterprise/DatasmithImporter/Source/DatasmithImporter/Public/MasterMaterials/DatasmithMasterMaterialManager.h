// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Containers/ContainersFwd.h"
#include "Templates/SharedPointer.h"
#include "Templates/UniquePtr.h"

class FDatasmithMasterMaterialSelector;

class DATASMITHIMPORTER_API FDatasmithMasterMaterialManager
{
public:
	static void Create();
	static void Destroy();
	static FDatasmithMasterMaterialManager& Get();

	FString GetHostFromString( const FString& HostString );

	void RegisterSelector( const TCHAR* Host, TSharedPtr< FDatasmithMasterMaterialSelector > Selector );
	void UnregisterSelector( const TCHAR* Host );

	const TSharedPtr< FDatasmithMasterMaterialSelector > GetSelector( const TCHAR* Host ) const;

private:
	static TUniquePtr< FDatasmithMasterMaterialManager > Instance;

	TMap< FString, TSharedPtr< FDatasmithMasterMaterialSelector > > Selectors;
};