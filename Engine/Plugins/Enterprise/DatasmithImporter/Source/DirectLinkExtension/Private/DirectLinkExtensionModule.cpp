// Copyright Epic Games, Inc. All Rights Reserved.

#include "DirectLinkExtensionModule.h"

#include "DirectLinkExternalSource.h"
#include "DirectLinkManager.h"
#include "DirectLinkUriResolver.h"
#include "UI/DirectLinkExtensionUI.h"
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
			// This will instantiate the DirectLinkManager.
			FDirectLinkManager::GetInstance();
			DirectLinkExtensionUI = MakeUnique<FDirectLinkExtensionUI>();

			IUriManager& UriManager = IExternalSourceModule::Get().GetManager();
			UriManager.RegisterResolver(DirectLinkUriResolverName, MakeShared<FDirectLinkUriResolver>());
		}

		virtual void ShutdownModule() override
		{
			if (IExternalSourceModule::IsAvailable())
			{
				IUriManager& UriManager = IExternalSourceModule::Get().GetManager();
				UriManager.UnregisterResolver(DirectLinkUriResolverName);
			}

			DirectLinkExtensionUI.Reset();
			FDirectLinkManager::ResetInstance();
		}

		virtual IDirectLinkManager& GetManager() const override
		{
			return FDirectLinkManager::GetInstance();
		}

		virtual TSharedPtr<FDirectLinkExternalSource> DisplayDirectLinkSourcesDialog() override;

	private:
		TUniquePtr<FDirectLinkExtensionUI> DirectLinkExtensionUI;
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
			.SizingRule(ESizingRule::UserSized)
			.AutoCenter(EAutoCenter::PreferredWorkArea)
			.ClientSize(FVector2D(600, 200))
			.SupportsMinimize(false);

		TSharedPtr<SDirectLinkAvailableSource> AvailableSourceWindow;
		Window->SetContent
		(
			SAssignNew(AvailableSourceWindow, SDirectLinkAvailableSource)
			.WidgetWindow(Window)
			.ProceedButtonLabel(FText(LOCTEXT("SelectLabel", "Select")))
			.ProceedButtonTooltip(FText::GetEmpty())
		);

		FSlateApplication::Get().AddModalWindow(Window, ParentWindow);

		return AvailableSourceWindow->GetShouldProceed() ? AvailableSourceWindow->GetSelectedSource() : nullptr;
	}
}

IMPLEMENT_MODULE(UE::DatasmithImporter::FDirectLinkExtensionModule, DirectLinkExtension);

#undef LOCTEXT_NAMESPACE
