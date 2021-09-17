// Copyright Epic Games, Inc. All Rights Reserved.
#include "TechSoftInterface.h"

#include "TechSoftInterfaceImpl.h"


namespace CADLibrary
{
	TSharedPtr<ITechSoftInterface> TechSoftInterface;

	TSharedPtr<ITechSoftInterface>& GetTechSoftInterface()
	{
		return TechSoftInterface;
	}

	void InitializeTechSoftInterface()
	{
#ifdef USE_TECHSOFT_SDK
		TechSoftInterface = MakeShared<CADLibrary::FTechSoftInterfaceImpl>();
#endif
	}

	bool TECHSOFT_InitializeKernel(const TCHAR* InEnginePluginsPath)
	{
		if (!TechSoftInterface.IsValid())
		{
			return false;
		}

		return TechSoftInterface->InitializeKernel(InEnginePluginsPath);
	}
}
