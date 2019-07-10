#include "IEyeTrackerModule.h"
#include "EyeTrackerTypes.h"
#include "IEyeTracker.h"
#include "Modules/ModuleManager.h"
#include "GameFramework/WorldSettings.h"
#include "Engine/World.h"

#if WITH_WINDOWS_MIXED_REALITY && PLATFORM_HOLOLENS
	#include "MixedRealityInterop.h"
#endif

#if WITH_WINDOWS_MIXED_REALITY && PLATFORM_HOLOLENS
static WindowsMixedReality::MixedRealityInterop hmd;

class FWindowsMixedRealityEyeTracker :
	public IEyeTracker
{
public:
	FWindowsMixedRealityEyeTracker()
	{
		// If this was created, then we want to use it, so request user perms
		hmd.RequestUserPermissionForEyeTracking();
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
		if (!hmd.GetEyeGaze(ray))
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
		if (!hmd.SupportsEyeTracking() || !hmd.IsEyeTrackingAllowed())
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
#if WITH_WINDOWS_MIXED_REALITY && PLATFORM_HOLOLENS
		return hmd.SupportsEyeTracking();
#else
		return false;
#endif
	}
	
	virtual TSharedPtr< class IEyeTracker, ESPMode::ThreadSafe > CreateEyeTracker() override
	{
#if WITH_WINDOWS_MIXED_REALITY && PLATFORM_HOLOLENS
		return TSharedPtr< class IEyeTracker, ESPMode::ThreadSafe >(new FWindowsMixedRealityEyeTracker);
#else
		return TSharedPtr< class IEyeTracker, ESPMode::ThreadSafe >();
#endif
	}
};

IMPLEMENT_MODULE(FWindowsMixedRealityEyeTrackerModule, WindowsMixedRealityEyeTracker)
