#include "IEyeTrackerModule.h"
#include "EyeTrackerTypes.h"
#include "IEyeTracker.h"
#include "Modules/ModuleManager.h"
#include "GameFramework/WorldSettings.h"
#include "Engine/World.h"
#include "WindowsMixedRealityInteropLoader.h"
#include "IXRTrackingSystem.h"

#if WITH_WINDOWS_MIXED_REALITY
	#include "MixedRealityInterop.h"
#endif

#if WITH_WINDOWS_MIXED_REALITY
static WindowsMixedReality::MixedRealityInterop* HMD = nullptr;

class FWindowsMixedRealityEyeTracker :
	public IEyeTracker
{
public:
	FWindowsMixedRealityEyeTracker()
	{
		if (!HMD)
		{
			HMD = WindowsMixedReality::LoadInteropLibrary();
			if (!HMD)
			{
				return;
			}
		}

		// If this was created, then we want to use it, so request user perms
#if PLATFORM_HOLOLENS
		// If remoting, delay requesting permissions until after the remoting session is created.

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
		if (!HMD || !HMD->GetEyeGaze(ray))
		{
			return false;
		}
		
		FTransform t2w = FTransform::Identity;
		if (GEngine != nullptr)
		{
			IXRTrackingSystem* TrackingSys = GEngine->XRSystem.Get();
			if (TrackingSys)
			{
				t2w = TrackingSys->GetTrackingToWorldTransform();
			}
		}
		
		OutGazeData.GazeDirection = t2w.TransformVector(FromMixedRealityVector(ray.direction));
		OutGazeData.GazeOrigin = t2w.TransformPosition(FromMixedRealityVector(ray.origin) * GetWorldToMetersScale());
		OutGazeData.ConfidenceValue = 1;
		
		return true;
	}
	virtual bool GetEyeTrackerStereoGazeData(FEyeTrackerStereoGazeData& OutGazeData) const override
	{
		return false;
	}
	virtual EEyeTrackerStatus GetEyeTrackerStatus() const override
	{
		if (!HMD || !HMD->SupportsEyeTracking() || !HMD->IsEyeTrackingAllowed())
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
