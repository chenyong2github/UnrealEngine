#include "CoreMinimal.h"
#include "Containers/UnrealString.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "ShaderCore.h"

class FPixelStreamingShadersModule : public IModuleInterface
{
public:
	virtual ~FPixelStreamingShadersModule() = default;

	virtual void StartupModule() override;
};

void FPixelStreamingShadersModule::StartupModule()
{
	FString ShaderDirectory = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("PixelStreaming"))->GetBaseDir(), TEXT("Shaders"));
	AddShaderSourceDirectoryMapping(TEXT("/Plugin/PixelStreaming"), ShaderDirectory);
}

IMPLEMENT_MODULE(FPixelStreamingShadersModule, PixelStreamingShaders)
