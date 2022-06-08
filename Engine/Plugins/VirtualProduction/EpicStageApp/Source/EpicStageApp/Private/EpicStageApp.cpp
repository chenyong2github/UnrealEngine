#include "EpicStageApp.h"
#include "Modules/ModuleManager.h"
#include "IWebRemoteControlModule.h"

void FEpicStageAppModule::StartupModule()
{
	IWebRemoteControlModule& WebRemoteControl = FModuleManager::LoadModuleChecked<IWebRemoteControlModule>("WebRemoteControl");
	RouteHandler.RegisterRoutes(WebRemoteControl);

	StageAppBeaconReceiver.Startup();
}

void FEpicStageAppModule::ShutdownModule()
{
	if (IWebRemoteControlModule* WebRemoteControl = FModuleManager::GetModulePtr<IWebRemoteControlModule>("WebRemoteControl"))
	{
		RouteHandler.UnregisterRoutes(*WebRemoteControl);
	}

	StageAppBeaconReceiver.Shutdown();
}
