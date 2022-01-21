// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleManager.h"

#include "Features/IModularFeatures.h"
#include "ISourceControlModule.h"
#include "MessageLogModule.h"
#include "Misc/DelayedAutoRegister.h"
#include "PackageSubmissionChecks.h"
#include "Serialization/EditorBulkData.h"
#include "VirtualizationSourceControlUtilities.h"

#define LOCTEXT_NAMESPACE "Virtualization"

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

		FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog");
		MessageLogModule.RegisterLogListing("LogVirtualization", LOCTEXT("AssetVirtualizationLogLabel", "Asset Virtualization"));
	}

	virtual void ShutdownModule() override
	{
		if (FModuleManager::Get().IsModuleLoaded("MessageLog"))
		{
			FMessageLogModule& MessageLogModule = FModuleManager::GetModuleChecked<FMessageLogModule>("MessageLog");
			MessageLogModule.UnregisterLogListing("LogVirtualization&");
		}
	
		// The SourceControl module might be destroyed before this one, depending on shutdown order so we need to check if 
		// it is loaded before unregistering.
		if (ISourceControlModule* SourceControlModule = FModuleManager::GetModulePtr<ISourceControlModule>(FName("SourceControl")))
		{
			SourceControlModule->UnregisterPreSubmitFinalize(PackageSubmissionHandle);
		}

		PackageSubmissionHandle.Reset();
		
		IModularFeatures::Get().UnregisterModularFeature(FName("VirtualizationSourceControlUtilities"), &SourceControlutility);
	}

private:
	Experimental::FVirtualizationSourceControlUtilities SourceControlutility;

	FDelegateHandle PackageSubmissionHandle;
};

} // namespace UE::Virtualization

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(UE::Virtualization::FVirtualizationModule, Virtualization);
