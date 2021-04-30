// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleManager.h"

#include "Features/IModularFeatures.h"
#include "VirtualizationSourceControlUtilities.h"
#include "Virtualization/VirtualizedBulkData.h"

namespace UE
{
namespace Virtualization
{

class FVirtualizationModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		IModularFeatures::Get().RegisterModularFeature(FName("VirtualizationSourceControlUtilities"), &SourceControlutility);
	}

	virtual void ShutdownModule() override
	{
		IModularFeatures::Get().UnregisterModularFeature(FName("VirtualizationSourceControlUtilities"), &SourceControlutility);
	}

private:
	FVirtualizationSourceControlUtilities SourceControlutility;
};

} // namespace Virtualization
} // namespace UE

IMPLEMENT_MODULE(UE::Virtualization::FVirtualizationModule, Virtualization);
