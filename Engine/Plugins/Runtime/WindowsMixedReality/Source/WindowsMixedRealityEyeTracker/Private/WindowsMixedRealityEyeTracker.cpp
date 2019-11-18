#include "IEyeTrackerModule.h"
#include "EyeTrackerTypes.h"
#include "IEyeTracker.h"
#include "Modules/ModuleManager.h"
#include "GameFramework/WorldSettings.h"
#include "Engine/World.h"

#if WITH_WINDOWS_MIXED_REALITY
	#include "MixedRealityInterop.h"
#endif
#include "IWindowsMixedRealityHMDPlugin.h"

#if WITH_WINDOWS_MIXED_REALITY

class FWindowsMixedRealityEyeTracker :
	public IEyeTracker
{
public:
	FWindowsMixedRealityEyeTracker()
	{
		// If this was created, then we want to use it, so request user perms
#if PLATFORM_HOLOLENS
		// If remoting, delay requesting permissions until after the remoting session is created.
		WindowsMixedReality::MixedRealityInterop* HMD = IWindowsMixedRealityHMDPlugin::Get().GetMixedRealityInterop();
		if (HMD != nullptr)
		{
			HMD->RequestUserPermissionForEyeTracking();
		}
#endif
	}
	
	virtual ~FWindowsMixedRealityEyeTracker()
	{
	}
	
private:
	// IEyeTracker
	virtual void SetEyeTrackedPlayer(APlayerController*) override { }
	virtual bool GetEyeTrackerGazeData(FEyeTrackerGazeData& OutGazeData) const override
	{
		WindowsMixedReality::EyeGazeRay ray;
		WindowsMixedReality::MixedRealityInterop* HMD = IWindowsMixedRealityHMDPlugin::Get().GetMixedRealityInterop();
		if (HMD == nullptr || !HMD->GetEyeGaze(ray))
		{
			return false;
		}
		
		OutGazeData.GazeDirection = FromMixedRealityVector(ray.direction);
		OutGazeData.GazeOrigin = FromMixedRealityVector(ray.origin) * GetWorldToMetersScale();
		OutGazeData.ConfidenceValue = 1;
		return true;
	}
	virtual bool GetEyeTrackerStereoGazeData(FEyeTrackerStereoGazeData& OutGazeData) const override
	{
		return false;
	}
	virtual EEyeTrackerStatus GetEyeTrackerStatus() const override
	{
		WindowsMixedReality::MixedRealityInterop* HMD = IWindowsMixedRealityHMDPlugin::Get().GetMixedRealityInterop();
		if (HMD == nullptr || !HMD->SupportsEyeTracking() || !HMD->IsEyeTrackingAllowed())
		{
			return EEyeTrackerStatus::NotConnected;
		}
		return EEyeTrackerStatus::Tracking;
	}
	virtual bool IsStereoGazeDataAvailable() const override
	{
		return false;
	}
	//~ IEyeTracker

	static FORCEINLINE FVector FromMixedRealityVector(DirectX::XMFLOAT3 pos)
	{
		return FVector(-pos.z, pos.x, pos.y);
	}

	static FORCEINLINE float GetWorldToMetersScale()
	{
		return GWorld ? GWorld->GetWorldSettings()->WorldToMeters : 100.f;
	}
};
#endif

class FWindowsMixedRealityEyeTrackerModule :
	public IEyeTrackerModule
{
public:
	static inline FWindowsMixedRealityEyeTrackerModule& Get()
	{
		return FModuleManager::LoadModuleChecked<FWindowsMixedRealityEyeTrackerModule>("WindowsMixedRealityEyeTracker");
	}
	
	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded("WindowsMixedRealityEyeTracker");
	}
	
	virtual FString GetModuleKeyName() const override
	{
		return TEXT("WindowsMixedRealityEyeTracker");
	}

	virtual bool IsEyeTrackerConnected() const override
	{
#if WITH_WINDOWS_MIXED_REALITY
		WindowsMixedReality::MixedRealityInterop* HMD = IWindowsMixedRealityHMDPlugin::Get().GetMixedRealityInterop();
		if (HMD != nullptr)
		{
			return HMD->SupportsEyeTracking();
		}
#endif
		return false;
	}
	
	virtual TSharedPtr< class IEyeTracker, ESPMode::ThreadSafe > CreateEyeTracker() override
	{
#if WITH_WINDOWS_MIXED_REALITY
		return TSharedPtr< class IEyeTracker, ESPMode::ThreadSafe >(new FWindowsMixedRealityEyeTracker);
#else
		return TSharedPtr< class IEyeTracker, ESPMode::ThreadSafe >();
#endif
	}
};

IMPLEMENT_MODULE(FWindowsMixedRealityEyeTrackerModule, WindowsMixedRealityEyeTracker)
