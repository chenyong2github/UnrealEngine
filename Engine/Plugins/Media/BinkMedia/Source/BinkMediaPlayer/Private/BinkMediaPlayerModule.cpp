// Copyright Epic Games Tools LLC
//   Licenced under the Unreal Engine EULA 

#include "BinkMediaPlayerPCH.h"
#include "BinkFunctionLibrary.h"

DEFINE_LOG_CATEGORY(LogBink);

TSharedPtr<FBinkMovieStreamer, ESPMode::ThreadSafe> MovieStreamer;

#if BINKPLUGIN_UE4_EDITOR
class UFactory;
#include "ISettingsModule.h"
#include "PropertyEditorModule.h"
#include "PropertyCustomizationHelpers.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "IDetailPropertyRow.h"
#include "DesktopPlatformModule.h"
#include "EditorDirectories.h"
#include "SourceControlHelpers.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Input/SFilePathPicker.h"
#include "Editor/MainFrame/Public/Interfaces/IMainFrameModule.h"
#include "Editor/UnrealEd/Public/Editor.h"
#include "IDetailCustomization.h"
#include "IDetailChildrenBuilder.h"

#include "BinkMediaPlayer.h"

struct FBinkMoviePlayerSettingsDetails : public IDetailCustomization {
	static TSharedRef<IDetailCustomization> MakeInstance() { return MakeShareable(new FBinkMoviePlayerSettingsDetails); }
	virtual void CustomizeDetails( IDetailLayoutBuilder& DetailLayout ) override {
		IDetailCategoryBuilder& MoviesCategory = DetailLayout.EditCategory("Movies");

		StartupMoviesPropertyHandle = DetailLayout.GetProperty("StartupMovies");

		TSharedRef<FDetailArrayBuilder> StartupMoviesBuilder = MakeShareable(new FDetailArrayBuilder(StartupMoviesPropertyHandle.ToSharedRef()));
		StartupMoviesBuilder->OnGenerateArrayElementWidget(FOnGenerateArrayElementWidget::CreateSP(this, &FBinkMoviePlayerSettingsDetails::GenerateArrayElementWidget));

		MoviesCategory.AddProperty("bWaitForMoviesToComplete");
		MoviesCategory.AddProperty("bMoviesAreSkippable");

		const bool bForAdvanced = false;
		MoviesCategory.AddCustomBuilder(StartupMoviesBuilder, bForAdvanced);
	}

	void GenerateArrayElementWidget(TSharedRef<IPropertyHandle> PropertyHandle, int32 ArrayIndex, IDetailChildrenBuilder& ChildrenBuilder) {
		IDetailPropertyRow& FilePathRow = ChildrenBuilder.AddProperty(PropertyHandle);
		FilePathRow.CustomWidget(false)
			.NameContent()
			[
				PropertyHandle->CreatePropertyNameWidget()
			]
		.ValueContent()
			.MaxDesiredWidth(0.0f)
			.MinDesiredWidth(125.0f)
			[
				SNew(SFilePathPicker)
					.BrowseButtonImage(FEditorStyle::GetBrush("PropertyWindow.Button_Ellipsis"))
					.BrowseButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
					.BrowseButtonToolTip(LOCTEXT("FileButtonToolTipText", "Choose a file from this computer"))
					.BrowseDirectory(FEditorDirectories::Get().GetLastDirectory(ELastDirectory::GENERIC_OPEN))
					.BrowseTitle(LOCTEXT("PropertyEditorTitle", "File picker..."))
					.FilePath(this, &FBinkMoviePlayerSettingsDetails::HandleFilePathPickerFilePath, PropertyHandle)
					.FileTypeFilter(TEXT("Bink 2 Movie (*.bk2)|*.bk2"))
					.OnPathPicked(this, &FBinkMoviePlayerSettingsDetails::HandleFilePathPickerPathPicked, PropertyHandle)
			];
	}

	FString HandleFilePathPickerFilePath( TSharedRef<IPropertyHandle> Property ) const {
		FString FilePath;
		Property->GetValue(FilePath);
		return FilePath;
	}

	void HandleFilePathPickerPathPicked( const FString& PickedPath, TSharedRef<IPropertyHandle> Property ) {
		const FString MoviesBaseDir = FPaths::ConvertRelativePathToFull( BINKMOVIEPATH );
		const FString FullPath = FPaths::ConvertRelativePathToFull(PickedPath);

		if (FullPath.StartsWith(MoviesBaseDir)) {
			FText FailReason;
			if (SourceControlHelpers::CheckoutOrMarkForAdd(PickedPath, LOCTEXT("MovieFileDescription", "movie"), FOnPostCheckOut(), FailReason)) {
				Property->SetValue(FPaths::GetBaseFilename(FullPath.RightChop(MoviesBaseDir.Len())));
			} else {
				FNotificationInfo Info(FailReason);
				Info.ExpireDuration = 3.0f;
				FSlateNotificationManager::Get().AddNotification(Info);
			}
		} else if (!PickedPath.IsEmpty()) {
			const FString FileName = FPaths::GetCleanFilename(PickedPath);
			const FString DestPath = MoviesBaseDir / FileName;
			FText FailReason;
			if (SourceControlHelpers::CopyFileUnderSourceControl(DestPath, PickedPath, LOCTEXT("MovieFileDescription", "movie"), FailReason)) {
				// trim the path so we just have a partial path with no extension (the movie player expects this)
				Property->SetValue(FPaths::GetBaseFilename(DestPath.RightChop(MoviesBaseDir.Len())));
			} else {
				FNotificationInfo FailureInfo(FailReason);
				FailureInfo.ExpireDuration = 3.0f;
				FSlateNotificationManager::Get().AddNotification(FailureInfo);
			}
		} else {
			Property->SetValue(PickedPath);
		}
	}

	TSharedPtr<IPropertyHandle> StartupMoviesPropertyHandle;
};
#endif //BINKPLUGIN_UE4_EDITOR

unsigned bink_gpu_api;
unsigned bink_gpu_api_hdr;
EPixelFormat bink_force_pixel_format = PF_Unknown;

#ifdef BINK_NDA_GPU_ALLOC
extern void* BinkAllocGpu(UINTa Amt, U32 Align);
extern void BinkFreeGpu(void* ptr, UINTa Amt);
#else
#define BinkAllocGpu 0
#define BinkFreeGpu 0
#endif

#ifdef BINK_NDA_CPU_ALLOC
extern void* BinkAllocCpu(UINTa Amt, U32 Align);
extern void BinkFreeCpu(void* ptr);
#else
static void *BinkAllocCpu(UINTa Amt, U32 Align) { return FMemory::Malloc(Amt, Align); }
static void BinkFreeCpu(void * ptr) { FMemory::Free(ptr); }
#endif


FString BinkUE4CookOnTheFlyPath(FString path, const TCHAR *filename) 
{
	// If this isn't a shipping build, copy the file to our temp directory (so that cook-on-the-fly works)
	FString toPath = FPaths::ConvertRelativePathToFull(BINKTEMPPATH) + filename;
	FString fromPath = path + filename;
	toPath = toPath.Replace(TEXT("/./"), TEXT("/"));
	fromPath = fromPath.Replace(TEXT("/./"), TEXT("/"));
	FPlatformFileManager::Get().GetPlatformFile().CopyFile(*toPath, *fromPath);
	return fromPath;
}

struct FBinkMediaPlayerModule : IModuleInterface, FTickableGameObject
{
	virtual void StartupModule() override 
	{
		// TODO: make this an INI setting and/or configurable in Project Settings
		//BinkPluginTurnOnGPUAssist();

		BINKPLUGININITINFO InitInfo = { 0 };
		InitInfo.queue = GDynamicRHI->RHIGetNativeGraphicsQueue();
		InitInfo.physical_device = GDynamicRHI->RHIGetNativePhysicalDevice();
		InitInfo.alloc = BinkAllocCpu;
		InitInfo.free = BinkFreeCpu;
		InitInfo.gpu_alloc = BinkAllocGpu;
		InitInfo.gpu_free = BinkFreeGpu;

		if(IsVulkanPlatform(GMaxRHIShaderPlatform)) 
		{
			bink_gpu_api_hdr = IsHDREnabled();
			bink_gpu_api = BinkVulkan;
			InitInfo.sdr_and_hdr_render_target_formats[0] = 44; // VK_FORMAT_B8G8R8A8_UNORM;
			InitInfo.sdr_and_hdr_render_target_formats[1] = 64; // VK_FORMAT_A2B10G10R10_UNORM_PACK32;
		}
		else if (FModuleManager::Get().IsModuleLoaded(TEXT("D3D12RHI")) || FModuleManager::Get().IsModuleLoaded(TEXT("XboxOneD3D12RHI")))
		{
			bink_gpu_api_hdr = true;
			bink_gpu_api = BinkD3D12;
			// NOTE/WARNING: This format should match the backbuffer if you are using overlay rendering! Otherwise you will get D3D Errors!
			// Note: D3D12 Bink backend only supports a single format currently!
			InitInfo.sdr_and_hdr_render_target_formats[0] = 24;// DXGI_FORMAT_R10G10B10A2_UNORM;
			InitInfo.sdr_and_hdr_render_target_formats[1] = 24;// DXGI_FORMAT_R10G10B10A2_UNORM;
			bink_force_pixel_format = PF_A2B10G10R10;
		}
		else if(IsMetalPlatform(GMaxRHIShaderPlatform)) 
		{
			bink_gpu_api = BinkMetal;
            InitInfo.sdr_and_hdr_render_target_formats[0] = 80;// MTLPixelFormatBGRA8Unorm;
            InitInfo.sdr_and_hdr_render_target_formats[1] = 90;// MTLPixelFormatRGB10A2Unorm;
		} 
		else if (IsOpenGLPlatform(GMaxRHIShaderPlatform))
		{
			bink_gpu_api = BinkGL;
		}
		else if (IsD3DPlatform(GMaxRHIShaderPlatform))
		{
			bink_gpu_api = BinkD3D11;
		}
		else
		{
			bink_gpu_api = BinkNDA;
		}

		bPluginInitialized = (bool)BinkPluginInit(GDynamicRHI->RHIGetNativeDevice(), &InitInfo, bink_gpu_api);

		if (!bPluginInitialized) 
		{
			printf("Bink Error: %s\n", BinkPluginError());
		}

		MovieStreamer = MakeShareable(new FBinkMovieStreamer);

		GetMoviePlayer()->RegisterMovieStreamer(MovieStreamer);

#if BINKPLUGIN_UE4_EDITOR
		ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
		if (SettingsModule && !IsRunningGame()) 
		{
			SettingsModule->RegisterSettings("Project", "Project", "Bink Movies",
				LOCTEXT("MovieSettingsName", "Bink Movies"),
				LOCTEXT("MovieSettingsDescription", "Bink Movie player settings"),
				GetMutableDefault<UBinkMoviePlayerSettings>()
				);

			//SettingsModule->RegisterViewer("Project", *this);

			IMainFrameModule& MainFrameModule = FModuleManager::LoadModuleChecked<IMainFrameModule>("MainFrame");
			MainFrameModule.OnMainFrameCreationFinished().AddRaw(this, &FBinkMediaPlayerModule::Initialize);
		}
#endif
		GetMutableDefault<UBinkMoviePlayerSettings>()->LoadConfig();
	}

	virtual void ShutdownModule() override 
	{
		BinkPluginShutdown();
		if (overlayHook.IsValid() && GEngine && GEngine->GameViewport) 
		{
			GEngine->GameViewport->OnDrawn().Remove(overlayHook);
		}
	}

#if BINKPLUGIN_UE4_EDITOR
	void Initialize(TSharedPtr<SWindow> InRootWindow, bool bIsNewProjectWindow) {
		// This overrides the 
		FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.RegisterCustomClassLayout("MoviePlayerSettings", FOnGetDetailCustomizationInstance::CreateStatic(&FBinkMoviePlayerSettingsDetails::MakeInstance));
		PropertyModule.NotifyCustomizationModuleChanged();
	}
#endif

	virtual bool IsTickable() const override { return bPluginInitialized; }
	virtual bool IsTickableInEditor() const override { return true; }
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(FBinkMediaPlayerModule, STATGROUP_Tickables); }

	void DrawBinks() 
	{
		ENQUEUE_RENDER_COMMAND(BinkProcess)([](FRHICommandListImmediate& RHICmdList) 
		{ 
			RHICmdList.SubmitCommandsHint();
			RHICmdList.EnqueueLambda([](FRHICommandListImmediate& RHICmdList) {
				BINKPLUGINFRAMEINFO FrameInfo = {};
				FrameInfo.cmdBuf = RHICmdList.GetNativeCommandBuffer();
				BinkPluginSetPerFrameInfo(&FrameInfo);
				BinkPluginProcessBinks(0);
				BinkPluginAllScheduled();
				BinkPluginDraw(1, 0);
			});
			RHICmdList.PostExternalCommandsReset();
			RHICmdList.SubmitCommandsHint();
		});
		UBinkFunctionLibrary::Bink_DrawOverlays();
	}

	virtual void Tick(float DeltaTime) override 
	{
		if (GEngine && GEngine->GameViewport) 
		{
			if (overlayHook.IsValid()) 
			{
				GEngine->GameViewport->OnDrawn().Remove(overlayHook);
			}
			overlayHook = GEngine->GameViewport->OnDrawn().AddRaw(this, &FBinkMediaPlayerModule::DrawBinks);
		}
		else 
		{
			DrawBinks();
		}
	}

    FDelegateHandle overlayHook;

#if BINKPLUGIN_UE4_EDITOR
	FDelegateHandle pieBeginHook;
	FDelegateHandle pieEndHook;
#endif
	bool bPluginInitialized = false;
};

IMPLEMENT_MODULE( FBinkMediaPlayerModule, BinkMediaPlayer )

