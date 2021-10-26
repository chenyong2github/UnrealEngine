// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleManager.h"

#include "Features/IModularFeatures.h"
#include "ISourceControlModule.h"
#include "Misc/DelayedAutoRegister.h"
#include "PackageSubmissionChecks.h"
#include "Serialization/VirtualizedBulkData.h"
#include "VirtualizationSourceControlUtilities.h"

namespace UE::Virtualization
{

class FVirtualizationModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		IModularFeatures::Get().RegisterModularFeature(FName("VirtualizationSourceControlUtilities"), &SourceControlutility);

		// Delay this until after the source control module has loaded
		FDelayedAutoRegisterHelper(EDelayedRegisterRunPhase::EarliestPossiblePluginsLoaded, [this]() 
			{
				PackageSubmissionHandle = ISourceControlModule::Get().RegisterPreSubmitFinalize(
					FSourceControlPreSubmitFinalizeDelegate::FDelegate::CreateStatic(&OnPrePackageSubmission));
			});
	}

	virtual void ShutdownModule() override
	{
		IModularFeatures::Get().UnregisterModularFeature(FName("VirtualizationSourceControlUtilities"), &SourceControlutility);
		
		// The SourceControl module might be destroyed before this one, depending on shutdown order so we need to check if 
		// it is loaded before unregistering.
		if (ISourceControlModule* SourceControlModule = FModuleManager::GetModulePtr<ISourceControlModule>(FName("SourceControl")))
		{
			SourceControlModule->UnregisterPreSubmitFinalize(PackageSubmissionHandle);
		}

		PackageSubmissionHandle.Reset();
		
	}

private:
	Experimental::FVirtualizationSourceControlUtilities SourceControlutility;

	FDelegateHandle PackageSubmissionHandle;
};

} // namespace UE::Virtualization

IMPLEMENT_MODULE(UE::Virtualization::FVirtualizationModule, Virtualization);
