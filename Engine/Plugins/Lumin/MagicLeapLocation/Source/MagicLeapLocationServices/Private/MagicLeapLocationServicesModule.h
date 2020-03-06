// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IMagicLeapLocationServicesModule.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMagicLeapLocationServices, Verbose, All);

class FMagicLeapLocationServicesModule : public IMagicLeapLocationServicesModule
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	class UMagicLeapLocationServicesImpl* ImplInstance;
};

inline FMagicLeapLocationServicesModule& GetMagicLeapLocationServicesModule()
{
	return FModuleManager::Get().GetModuleChecked<FMagicLeapLocationServicesModule>("MagicLeapLocationServices");
}
