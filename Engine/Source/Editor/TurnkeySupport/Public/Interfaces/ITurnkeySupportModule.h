// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SlateFwd.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "Widgets/SWidget.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/MultiBox/MultiBoxExtender.h"


/**
 * Interface for turnkey support module
 */
class ITurnkeySupportModule
	: public IModuleInterface
{
public:

	/**
	 *
	 * @return	The newly-created menu widget
	 */
	virtual TSharedRef<SWidget> MakeTurnkeyMenu() const = 0;

public:

	/**
	 * Gets a reference to the search module instance.
	 *
	 * @todo gmp: better implementation using dependency injection.
	 * @return A reference to the MainFrame module.
	 */
	static ITurnkeySupportModule& Get( )
	{
		static const FName TurnkeySupportModuleName = "TurnkeySupport";
		return FModuleManager::LoadModuleChecked<ITurnkeySupportModule>(TurnkeySupportModuleName);
	}

public:

	/**
	 * Virtual destructor.
	 */
	virtual ~ITurnkeySupportModule( ) { }
};
