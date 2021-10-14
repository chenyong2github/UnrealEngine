// Copyright Epic Games, Inc. All Rights Reserved.

#include "DirectLinkExtensionModule.h"

#include "DirectLinkExternalSource.h"
#include "DirectLinkManager.h"
#include "DirectLinkUriResolver.h"
#include "UI/SDirectLinkAvailableSource.h"

#include "ContentBrowserModule.h"
#include "Editor.h"
#include "ExternalSourceModule.h"
#include "Interfaces/IMainFrameModule.h"
#include "IUriManager.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "DirectLinkEditorModule"

namespace UE::DatasmithImporter
{
	class FDirectLinkExtensionModule : public IDirectLinkExtensionModule
	{
		const FName DirectLinkUriResolverName = TEXT("DirectLinkUriResolverName");

	public:
		virtual void StartupModule() override
		{
			DirectLinkManager = MakeUnique<FDirectLinkManager>();

			if (IUriManager* UriManager = IExternalSourceModule::Get().GetManager())
			{
				UriManager->RegisterResolver(DirectLinkUriResolverName, MakeShared<FDirectLinkUriResolver>());
			}
		}

		virtual void ShutdownModule() override
		{
			if (IUriManager* UriManager = IExternalSourceModule::Get().GetManager())
			{
				UriManager->UnregisterResolver(DirectLinkUriResolverName);
			}

			DirectLinkManager.Reset();
		}

		virtual IDirectLinkManager* GetManager() const override
		{
			return DirectLinkManager.Get();
		}

		virtual TSharedPtr<FDirectLinkExternalSource> DisplayDirectLinkSourcesDialog() override;

	private:
		TUniquePtr<FDirectLinkManager> DirectLinkManager;
	};

	TSharedPtr<FDirectLinkExternalSource> FDirectLinkExtensionModule::DisplayDirectLinkSourcesDialog()
	{
		TSharedPtr<SWindow> ParentWindow;

		if (FModuleManager::Get().IsModuleLoaded("MainFrame"))
		{
			IMainFrameModule& MainFrame = FModuleManager::LoadModuleChecked<IMainFrameModule>("MainFrame");
			ParentWindow = MainFrame.GetParentWindow();
		}

		TSharedRef<SWindow> Window = SNew(SWindow)
			.Title(LOCTEXT("DirectLinkEditorAvailableSourcesTitle", "DirectLink Available Sources"))
			.SizingRule(ESizingRule::Autosized);

		TSharedPtr<SDirectLinkAvailableSource> AvailableSourceWindow;
		Window->SetContent
		(
			SAssignNew(AvailableSourceWindow, SDirectLinkAvailableSource)
			.WidgetWindow(Window)
			.ProceedButtonLabel(FText(LOCTEXT("ImportSelectedLabel", "Import Selected")))
			.ProceedButtonTooltip(FText(LOCTEXT("ImportSelectedTooltip", "Import selected directlink source.")))
		);

		FSlateApplication::Get().AddModalWindow(Window, ParentWindow);

		return AvailableSourceWindow->GetShouldProceed() ? AvailableSourceWindow->GetSelectedSource() : nullptr;
	}
}

IMPLEMENT_MODULE(UE::DatasmithImporter::FDirectLinkExtensionModule, DirectLinkExtension);

#undef LOCTEXT_NAMESPACE
