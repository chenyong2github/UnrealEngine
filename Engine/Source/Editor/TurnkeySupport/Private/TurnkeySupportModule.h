// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWidget.h"
#include "Interfaces/ITurnkeySupportModule.h"


DECLARE_LOG_CATEGORY_EXTERN(LogTurnkeySupport, Log, All);

/**
 * Editor main frame module
 */
class FTurnkeySupportModule	: public ITurnkeySupportModule
{
public:

	/**
	 *
	 * @return	The newly-created menu widget
	 */
	virtual TSharedRef<SWidget> MakeTurnkeyMenu() const override;

public:

	// IModuleInterface interface

	virtual void StartupModule( ) override;
	virtual void ShutdownModule( ) override;

	virtual bool SupportsDynamicReloading( ) override
	{
		return true; // @todo: Eventually, this should probably not be allowed.
	}


private:

};
