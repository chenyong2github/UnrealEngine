#include "UI/QMSUIManager.h"
#include "UI/MSStyle.h"

#include "AssetData.h"
#include "ContentBrowserModule.h"
#include "LevelEditor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

#include "IPythonScriptPlugin.h"

#include "MSPythonBridge.h"
#include "TCPServerImp.h"
#include "UI/SMSWindow.h"


#define LOCTEXT_NAMESPACE "QMSLiveLink"
#define LEVELEDITOR_MODULE_NAME TEXT("LevelEditor")
#define CONTENTBROWSER_MODULE_NAME TEXT("ContentBrowser")

TUniquePtr<FQMSUIManagerImpl> FQMSUIManager::Instance;

class FQMSUIManagerImpl
{
public:
	void Initialize();
	void Shutdown();
	void JsonReceived(FString message);

private:
	void SetupMenuItem();
	void CreateWIndow();
	FTCPServer *tcpServer = NULL;	
	void FillToolbar(FToolBarBuilder& ToolbarBuilder);
};

void FQMSUIManager::Initialize()
{
	if (!Instance.IsValid())
	{
		Instance = MakeUnique<FQMSUIManagerImpl>();
		Instance->Initialize();
	}	
}

void FQMSUIManagerImpl::Initialize()
{
	FMSStyle::Initialize();

	SetupMenuItem();
	if (tcpServer == NULL)
	{
		tcpServer = new FTCPServer();
	}

	
}

void FQMSUIManagerImpl::SetupMenuItem() 
{
	FMSStyle::SetIcon("Logo", "Logo80x80");
	FMSStyle::SetIcon("ContextLogo", "Logo32x32");

	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>(LEVELEDITOR_MODULE_NAME);
	TSharedPtr<FExtender> ToolbarExtender = MakeShareable(new FExtender);
	ToolbarExtender->AddToolBarExtension("Settings", EExtensionHook::After, nullptr, FToolBarExtensionDelegate::CreateRaw(this, &FQMSUIManagerImpl::FillToolbar));
	LevelEditorModule.GetToolBarExtensibilityManager()->AddExtender(ToolbarExtender);
}

void FQMSUIManagerImpl::FillToolbar(FToolBarBuilder& ToolbarBuilder)
{
	ToolbarBuilder.BeginSection(TEXT("QuixelMSLiveLink"));
	{
			ToolbarBuilder.AddToolBarButton(
				FUIAction(FExecuteAction::CreateRaw( this, &FQMSUIManagerImpl::CreateWIndow)),
				FName(TEXT("Quixel MS LiveLink")),
				LOCTEXT("QMSLiveLink_label", "Megascans"),
				LOCTEXT("WorldProperties_ToolTipOverride", "Megascans Link with Bridge"),
				FSlateIcon(FMSStyle::GetStyleSetName(), "Megascans.Logo"),
				EUserInterfaceActionType::Button,
				FName(TEXT("QuixelMSLiveLink"))
				);
	}
	ToolbarBuilder.EndSection();
}

void FQMSUIManagerImpl::CreateWIndow() 
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
	MegascansSettingsWindow::OpenSettingsWindow(LevelEditorModule.GetLevelEditorTabManager().ToSharedRef());
	/*
	UMSPythonBridge* bridge = UMSPythonBridge::Get();
	bridge->InitializePythonWindow();
	bridge->TestFbxImport();
	*/
}

void FQMSUIManagerImpl::JsonReceived(FString message) {
	UMSPythonBridge* bridge = UMSPythonBridge::Get();
	bridge->GetUeData(message);	
}

void FQMSUIManager::Shutdown()
{
	// Do other destruction stuff
	FMSStyle::Shutdown();
}

#undef LOCTEXT_NAMESPACE

