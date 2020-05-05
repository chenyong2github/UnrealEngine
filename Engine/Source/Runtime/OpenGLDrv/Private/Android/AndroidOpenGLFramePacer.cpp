// Copyright Epic Games, Inc. All Rights Reserved.

#include "AndroidOpenGLFramePacer.h"

#if USE_ANDROID_OPENGL

/*******************************************************************
 * FAndroidOpenGLFramePacer implementation
 *******************************************************************/

#include "OpenGLDrvPrivate.h"
#include "Math/UnrealMathUtility.h"
#if USE_ANDROID_OPENGL_SWAPPY
#include "Android/AndroidJNI.h"
#include "Android/AndroidApplication.h"
#include "swappyGL.h"
#include "swappyGL_extra.h"
#include "swappy_common.h"
#endif

#include "AndroidEGL.h"
#include <EGL/egl.h>

void FAndroidOpenGLFramePacer::Init()
{
	bSwappyInit = false;
#if USE_ANDROID_OPENGL_SWAPPY
	if (FAndroidPlatformRHIFramePacer::CVarUseSwappyForFramePacing.GetValueOnAnyThread() == 1)
	{
		// initialize now if set on startup
		InitSwappy();
	}
	else
	{
		// initialize later if set by console
		FAndroidPlatformRHIFramePacer::CVarUseSwappyForFramePacing.AsVariable()->SetOnChangedCallback(FConsoleVariableDelegate::CreateLambda([this](IConsoleVariable* Variable)
		{
			if (Variable->GetInt() == 1)
			{
				InitSwappy();
			}
		}));
	}
#endif
}

#if USE_ANDROID_OPENGL_SWAPPY
void FAndroidOpenGLFramePacer::InitSwappy()
{
	if (!bSwappyInit)
	{
		// initialize Swappy
		JNIEnv* Env = FAndroidApplication::GetJavaEnv();
		if (ensure(Env))
		{
			FPlatformMisc::LowLevelOutputDebugStringf(TEXT("Init Swappy"));
			SwappyGL_init(Env, FJavaWrapper::GameActivityThis);
		}
		bSwappyInit = true;
	}
}
#endif

FAndroidOpenGLFramePacer::~FAndroidOpenGLFramePacer()
{
#if USE_ANDROID_OPENGL_SWAPPY
	FAndroidPlatformRHIFramePacer::CVarUseSwappyForFramePacing.AsVariable()->SetOnChangedCallback(FConsoleVariableDelegate());
	if (bSwappyInit)
	{
		FPlatformMisc::LowLevelOutputDebugStringf(TEXT("Shutdown Swappy"));
		SwappyGL_destroy();
		bSwappyInit = false;
	}
#endif
}

static bool GGetTimeStampsSucceededThisFrame = true;
static uint32 GGetTimeStampsRetryCount = 0;

static bool CanUseGetFrameTimestamps()
{
	return FAndroidPlatformRHIFramePacer::CVarUseGetFrameTimestamps.GetValueOnAnyThread()
		&& eglGetFrameTimestampsANDROID_p
		&& eglGetNextFrameIdANDROID_p
		&& eglPresentationTimeANDROID_p
		&& (GGetTimeStampsRetryCount < FAndroidPlatformRHIFramePacer::CVarTimeStampErrorRetryCount.GetValueOnAnyThread());
}
static bool CanUseGetFrameTimestampsForThisFrame()
{
	return CanUseGetFrameTimestamps() && GGetTimeStampsSucceededThisFrame;
}

bool ShouldUseGPUFencesToLimitLatency()
{
	if (CanUseGetFrameTimestampsForThisFrame())
	{
		return true; // this method requires a GPU fence to give steady results
	}
	return FAndroidPlatformRHIFramePacer::CVarDisableOpenGLGPUSync.GetValueOnAnyThread() == 0; // otherwise just based on the FAndroidPlatformRHIFramePacer::CVar; thought to be bad to use GPU fences on PowerVR
}

static uint32 NextFrameIDSlot = 0;
#define NUM_FRAMES_TO_MONITOR (4)
static EGLuint64KHR FrameIDs[NUM_FRAMES_TO_MONITOR] = { 0 };

static int32 RecordedFrameInterval[100];
static int32 NumRecordedFrameInterval = 0;

extern float AndroidThunkCpp_GetMetaDataFloat(const FString& Key);


bool FAndroidOpenGLFramePacer::SupportsFramePace(int32 QueryFramePace)
{
#if USE_ANDROID_OPENGL_SWAPPY
	if (FAndroidPlatformRHIFramePacer::CVarUseSwappyForFramePacing.GetValueOnAnyThread() == 1)
	{
		int NumRates = Swappy_getSupportedRefreshRates(nullptr, 0);
		TArray<uint64> RefreshRatesNs;
		RefreshRatesNs.AddZeroed(NumRates);
		Swappy_getSupportedRefreshRates((uint64_t*)RefreshRatesNs.GetData(), NumRates);
		TArray<int32> RefreshRates;
		RefreshRates.Empty(NumRates);
		FString DebugString = TEXT("Supported Refresh Rates:");
		for (uint64 RateNs : RefreshRatesNs)
		{
			if (RateNs > 0)
			{
				int32 RefreshRate = FMath::DivideAndRoundNearest(1000000000ull, RateNs);
				RefreshRates.Add(RefreshRate);
				DebugString += FString::Printf(TEXT(" %d (%ld ns)"), RefreshRate, RateNs);
			}
		}
		UE_LOG(LogRHI, Log, TEXT("%s"), *DebugString);

		for (uint64 Rate : RefreshRates)
		{
			if ((Rate % QueryFramePace) == 0)
			{
				UE_LOG(LogRHI, Log, TEXT("Using Refresh rate %d with sync interval %d"), Rate, Rate / QueryFramePace);
				return true;
			}
		}
	}
#endif
	return FGenericPlatformRHIFramePacer::SupportsFramePace(QueryFramePace);
}

bool FAndroidOpenGLFramePacer::SwapBuffers(bool bLockToVsync)
{
#if !UE_BUILD_SHIPPING
	if (FAndroidPlatformRHIFramePacer::CVarStallSwap.GetValueOnAnyThread() > 0.0f)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_Swap_Intentional_Stall);
		FPlatformProcess::Sleep(FAndroidPlatformRHIFramePacer::CVarStallSwap.GetValueOnRenderThread() / 1000.0f);
	}
#endif

	VERIFY_EGL_SCOPE();

	EGLDisplay eglDisplay = AndroidEGL::GetInstance()->GetDisplay();
	EGLSurface eglSurface = AndroidEGL::GetInstance()->GetSurface();
	int32 SyncInterval = FAndroidPlatformRHIFramePacer::GetLegacySyncInterval();

	bool bPrintMethod = false;

#if USE_ANDROID_OPENGL_SWAPPY
	if (FAndroidPlatformRHIFramePacer::CVarUseSwappyForFramePacing.GetValueOnRenderThread() != 0 && bSwappyInit)
	{
		int64 DesiredFrameNS = (1000000000L) / (int64)FAndroidPlatformRHIFramePacer::GetFramePace();
		SwappyGL_setSwapIntervalNS(DesiredFrameNS);
		SwappyGL_setAutoSwapInterval(false);
		SwappyGL_swap(eglDisplay, eglSurface);
	}
	else
#endif
	{
		if (DesiredSyncIntervalRelativeTo60Hz != SyncInterval)
		{
			GGetTimeStampsRetryCount = 0;

			bPrintMethod = true;
			DesiredSyncIntervalRelativeTo60Hz = SyncInterval;
			DriverRefreshRate = 60.0f;
			DriverRefreshNanos = 16666666;


			EGLnsecsANDROID EGL_COMPOSITE_DEADLINE_ANDROID_Value = -1;
			EGLnsecsANDROID EGL_COMPOSITE_INTERVAL_ANDROID_Value = -1;
			EGLnsecsANDROID EGL_COMPOSITE_TO_PRESENT_LATENCY_ANDROID_Value = -1;

			if (eglGetCompositorTimingANDROID_p)
			{
				{
					EGLint Item = EGL_COMPOSITE_DEADLINE_ANDROID;
					if (!eglGetCompositorTimingANDROID_p(eglDisplay, eglSurface, 1, &Item, &EGL_COMPOSITE_DEADLINE_ANDROID_Value))
					{
						EGL_COMPOSITE_DEADLINE_ANDROID_Value = -1;
					}
				}
				{
					EGLint Item = EGL_COMPOSITE_INTERVAL_ANDROID;
					if (!eglGetCompositorTimingANDROID_p(eglDisplay, eglSurface, 1, &Item, &EGL_COMPOSITE_INTERVAL_ANDROID_Value))
					{
						EGL_COMPOSITE_INTERVAL_ANDROID_Value = -1;
					}
				}
				{
					EGLint Item = EGL_COMPOSITE_TO_PRESENT_LATENCY_ANDROID;
					if (!eglGetCompositorTimingANDROID_p(eglDisplay, eglSurface, 1, &Item, &EGL_COMPOSITE_TO_PRESENT_LATENCY_ANDROID_Value))
					{
						EGL_COMPOSITE_TO_PRESENT_LATENCY_ANDROID_Value = -1;
					}
				}
				UE_LOG(LogRHI, Log, TEXT("AndroidEGL:SwapBuffers eglGetCompositorTimingANDROID EGL_COMPOSITE_DEADLINE_ANDROID=%lld, EGL_COMPOSITE_INTERVAL_ANDROID=%lld, EGL_COMPOSITE_TO_PRESENT_LATENCY_ANDROID=%lld"),
					EGL_COMPOSITE_DEADLINE_ANDROID_Value,
					EGL_COMPOSITE_INTERVAL_ANDROID_Value,
					EGL_COMPOSITE_TO_PRESENT_LATENCY_ANDROID_Value
				);
			}

			float RefreshRate = AndroidThunkCpp_GetMetaDataFloat(TEXT("ue4.display.getRefreshRate"));

			UE_LOG(LogRHI, Log, TEXT("JNI Display getRefreshRate=%f"),
				RefreshRate
			);

			if (EGL_COMPOSITE_INTERVAL_ANDROID_Value >= 4000000 && EGL_COMPOSITE_INTERVAL_ANDROID_Value <= 41666666)
			{
				DriverRefreshRate = float(1000000000.0 / double(EGL_COMPOSITE_INTERVAL_ANDROID_Value));
				DriverRefreshNanos = EGL_COMPOSITE_INTERVAL_ANDROID_Value;
			}
			else if (RefreshRate >= 24.0f && RefreshRate <= 250.0f)
			{
				DriverRefreshRate = RefreshRate;
				DriverRefreshNanos = int64(0.5 + 1000000000.0 / double(RefreshRate));
			}

			UE_LOG(LogRHI, Log, TEXT("Final display timing metrics: DriverRefreshRate=%7.4f  DriverRefreshNanos=%lld"),
				DriverRefreshRate,
				DriverRefreshNanos
			);

			// make sure requested interval is in supported range
			EGLint MinSwapInterval, MaxSwapInterval;
			AndroidEGL::GetInstance()->GetSwapIntervalRange(MinSwapInterval, MaxSwapInterval);

			int64 SyncIntervalNanos = (30 + 1000000000l * int64(SyncInterval)) / 60;

			int32 UnderDriverInterval = int32(SyncIntervalNanos / DriverRefreshNanos);
			int32 OverDriverInterval = UnderDriverInterval + 1;

			int64 UnderNanos = int64(UnderDriverInterval) * DriverRefreshNanos;
			int64 OverNanos = int64(OverDriverInterval) * DriverRefreshNanos;

			DesiredSyncIntervalRelativeToDevice = (FMath::Abs(SyncIntervalNanos - UnderNanos) < FMath::Abs(SyncIntervalNanos - OverNanos)) ?
				UnderDriverInterval : OverDriverInterval;

			int32 DesiredDriverSyncInterval = FMath::Clamp<int32>(DesiredSyncIntervalRelativeToDevice, MinSwapInterval, MaxSwapInterval);

			UE_LOG(LogRHI, Log, TEXT("AndroidEGL:SwapBuffers Min=%d, Max=%d, Request=%d, ClosestDriver=%d, SetDriver=%d"), MinSwapInterval, MaxSwapInterval, DesiredSyncIntervalRelativeTo60Hz, DesiredSyncIntervalRelativeToDevice, DesiredDriverSyncInterval);

			if (DesiredDriverSyncInterval != DriverSyncIntervalRelativeToDevice)
			{
				DriverSyncIntervalRelativeToDevice = DesiredDriverSyncInterval;
				UE_LOG(LogRHI, Log, TEXT("Called eglSwapInterval %d"), DesiredDriverSyncInterval);
				eglSwapInterval(eglDisplay, DriverSyncIntervalRelativeToDevice);
			}
		}

		if (DesiredSyncIntervalRelativeToDevice > DriverSyncIntervalRelativeToDevice)
		{
			{
				UE_CLOG(bPrintMethod, LogRHI, Display, TEXT("Using niave method for frame pacing (possible with timestamps method)"));
				if (LastTimeEmulatedSync > 0.0)
				{
					QUICK_SCOPE_CYCLE_COUNTER(STAT_StallForEmulatedSyncInterval);
					float MinTimeBetweenFrames = (float(DesiredSyncIntervalRelativeToDevice) / DriverRefreshRate);

					float ThisTime = FPlatformTime::Seconds() - LastTimeEmulatedSync;
					if (ThisTime > 0 && ThisTime < MinTimeBetweenFrames)
					{
						FPlatformProcess::Sleep(MinTimeBetweenFrames - ThisTime);
					}
				}
			}
		}
		if (CanUseGetFrameTimestamps())
		{
			UE_CLOG(bPrintMethod, LogRHI, Display, TEXT("Using eglGetFrameTimestampsANDROID method for frame pacing"));

			//static bool bPrintOnce = true;
			if (FrameIDs[(int32(NextFrameIDSlot) - 1) % NUM_FRAMES_TO_MONITOR])
				// not supported   && eglGetFrameTimestampsSupportedANDROID_p && eglGetFrameTimestampsSupportedANDROID_p(eglDisplay, eglSurface, EGL_FIRST_COMPOSITION_START_TIME_ANDROID))
			{
				//UE_CLOG(bPrintOnce, LogRHI, Log, TEXT("eglGetFrameTimestampsSupportedANDROID retured true for EGL_FIRST_COMPOSITION_START_TIME_ANDROID"));
				EGLint TimestampList = EGL_FIRST_COMPOSITION_START_TIME_ANDROID;
				//EGLint TimestampList = EGL_COMPOSITION_LATCH_TIME_ANDROID;
				//EGLint TimestampList = EGL_LAST_COMPOSITION_START_TIME_ANDROID;
				//EGLint TimestampList = EGL_DISPLAY_PRESENT_TIME_ANDROID;
				EGLnsecsANDROID Result = 0;
				int32 DeltaFrameIndex = 1;
				for (int32 Index = int32(NextFrameIDSlot) - 1; Index >= int32(NextFrameIDSlot) - NUM_FRAMES_TO_MONITOR && Index >= 0; Index--)
				{
					Result = 0;
					if (FrameIDs[Index % NUM_FRAMES_TO_MONITOR])
					{
						eglGetFrameTimestampsANDROID_p(eglDisplay, eglSurface, FrameIDs[Index % NUM_FRAMES_TO_MONITOR], 1, &TimestampList, &Result);
					}
					if (Result > 0)
					{
						break;
					}
					DeltaFrameIndex++;
				}

				GGetTimeStampsSucceededThisFrame = Result > 0;
				if (GGetTimeStampsSucceededThisFrame)
				{
					EGLnsecsANDROID FudgeFactor = 0; //  8333 * 1000;
					EGLnsecsANDROID DeltaNanos = EGLnsecsANDROID(DesiredSyncIntervalRelativeToDevice) * EGLnsecsANDROID(DeltaFrameIndex) * DriverRefreshNanos;
					EGLnsecsANDROID PresentationTime = Result + DeltaNanos + FudgeFactor;
					eglPresentationTimeANDROID_p(eglDisplay, eglSurface, PresentationTime);
					GGetTimeStampsRetryCount = 0;
				}
				else
				{
					GGetTimeStampsRetryCount++;
					if (GGetTimeStampsRetryCount == FAndroidPlatformRHIFramePacer::CVarTimeStampErrorRetryCount.GetValueOnAnyThread())
					{
						UE_LOG(LogRHI, Log, TEXT("eglGetFrameTimestampsANDROID_p failed for %d consecutive frames, reverting to naive frame pacer."), GGetTimeStampsRetryCount);
					}
				}
			}
			else
			{
				//UE_CLOG(bPrintOnce, LogRHI, Log, TEXT("eglGetFrameTimestampsSupportedANDROID doesn't exist or retured false for EGL_FIRST_COMPOSITION_START_TIME_ANDROID, discarding eglGetNextFrameIdANDROID_p and eglGetFrameTimestampsANDROID_p"));
			}
			//bPrintOnce = false;
		}

		LastTimeEmulatedSync = FPlatformTime::Seconds();

		{
			QUICK_SCOPE_CYCLE_COUNTER(STAT_eglSwapBuffers);

			FrameIDs[(NextFrameIDSlot) % NUM_FRAMES_TO_MONITOR] = 0;
			if (eglGetNextFrameIdANDROID_p && (CanUseGetFrameTimestamps() || FAndroidPlatformRHIFramePacer::CVarSpewGetFrameTimestamps.GetValueOnAnyThread()))
			{
				eglGetNextFrameIdANDROID_p(eglDisplay, eglSurface, &FrameIDs[(NextFrameIDSlot) % NUM_FRAMES_TO_MONITOR]);
			}
			NextFrameIDSlot++;

			if (eglSurface == NULL || !eglSwapBuffers(eglDisplay, eglSurface))
			{
				// shutdown if swapbuffering goes down
				if (SwapBufferFailureCount > 10)
				{
					//Process.killProcess(Process.myPid());		//@todo android
				}
				SwapBufferFailureCount++;

				// basic reporting
				if (eglSurface == NULL)
				{
					return false;
				}
				else
				{
					if (eglGetError() == EGL_CONTEXT_LOST)
					{
						//Logger.LogOut("swapBuffers: EGL11.EGL_CONTEXT_LOST err: " + eglGetError());					
						//Process.killProcess(Process.myPid());		//@todo android
					}
				}

				return false;
			}
		}

		if (DesiredSyncIntervalRelativeToDevice > 0 && eglGetFrameTimestampsANDROID_p && FAndroidPlatformRHIFramePacer::CVarSpewGetFrameTimestamps.GetValueOnAnyThread())
		{
			static EGLint TimestampList[9] =
			{
				EGL_REQUESTED_PRESENT_TIME_ANDROID,
				EGL_RENDERING_COMPLETE_TIME_ANDROID,
				EGL_COMPOSITION_LATCH_TIME_ANDROID,
				EGL_FIRST_COMPOSITION_START_TIME_ANDROID,
				EGL_LAST_COMPOSITION_START_TIME_ANDROID,
				EGL_FIRST_COMPOSITION_GPU_FINISHED_TIME_ANDROID,
				EGL_DISPLAY_PRESENT_TIME_ANDROID,
				EGL_DEQUEUE_READY_TIME_ANDROID,
				EGL_READS_DONE_TIME_ANDROID
			};

			static const TCHAR* TimestampStrings[9] =
			{
				TEXT("EGL_REQUESTED_PRESENT_TIME_ANDROID"),
				TEXT("EGL_RENDERING_COMPLETE_TIME_ANDROID"),
				TEXT("EGL_COMPOSITION_LATCH_TIME_ANDROID"),
				TEXT("EGL_FIRST_COMPOSITION_START_TIME_ANDROID"),
				TEXT("EGL_LAST_COMPOSITION_START_TIME_ANDROID"),
				TEXT("EGL_FIRST_COMPOSITION_GPU_FINISHED_TIME_ANDROID"),
				TEXT("EGL_DISPLAY_PRESENT_TIME_ANDROID"),
				TEXT("EGL_DEQUEUE_READY_TIME_ANDROID"),
				TEXT("EGL_READS_DONE_TIME_ANDROID")
			};


			EGLnsecsANDROID Results[NUM_FRAMES_TO_MONITOR][9] = { {0} };
			EGLnsecsANDROID FirstRealValue = 0;
			for (int32 Index = int32(NextFrameIDSlot) - NUM_FRAMES_TO_MONITOR; Index < int32(NextFrameIDSlot); Index++)
			{
				eglGetFrameTimestampsANDROID_p(eglDisplay, eglSurface, FrameIDs[Index % NUM_FRAMES_TO_MONITOR], 9, TimestampList, Results[Index % NUM_FRAMES_TO_MONITOR]);
				for (int32 IndexInner = 0; IndexInner < 9; IndexInner++)
				{
					if (!FirstRealValue || (Results[Index % NUM_FRAMES_TO_MONITOR][IndexInner] > 1 && Results[Index % NUM_FRAMES_TO_MONITOR][IndexInner] < FirstRealValue))
					{
						FirstRealValue = Results[Index % NUM_FRAMES_TO_MONITOR][IndexInner];
					}
				}
			}
			UE_CLOG(FAndroidPlatformRHIFramePacer::CVarSpewGetFrameTimestamps.GetValueOnAnyThread() > 1, LogRHI, Log, TEXT("************************************  frame %d   base time is %lld"), NextFrameIDSlot - 1, FirstRealValue);

			for (int32 Index = int32(NextFrameIDSlot) - NUM_FRAMES_TO_MONITOR; Index < int32(NextFrameIDSlot); Index++)
			{

				UE_CLOG(FAndroidPlatformRHIFramePacer::CVarSpewGetFrameTimestamps.GetValueOnAnyThread() > 1, LogRHI, Log, TEXT("eglGetFrameTimestampsANDROID_p  frame %d"), Index);
				for (int32 IndexInner = 0; IndexInner < 9; IndexInner++)
				{
					int32 MsVal = (Results[Index % NUM_FRAMES_TO_MONITOR][IndexInner] > 1) ? int32((Results[Index % NUM_FRAMES_TO_MONITOR][IndexInner] - FirstRealValue) / 1000000) : int32(Results[Index % NUM_FRAMES_TO_MONITOR][IndexInner]);

					UE_CLOG(FAndroidPlatformRHIFramePacer::CVarSpewGetFrameTimestamps.GetValueOnAnyThread() > 1, LogRHI, Log, TEXT("     %8d    %s"), MsVal, TimestampStrings[IndexInner]);
				}
			}

			int32 IndexLast = int32(NextFrameIDSlot) - NUM_FRAMES_TO_MONITOR;
			int32 IndexLastNext = IndexLast + 1;

			if (Results[IndexLast % NUM_FRAMES_TO_MONITOR][3] > 1 && Results[IndexLastNext % NUM_FRAMES_TO_MONITOR][3] > 1)
			{
				int32 MsVal = int32((Results[IndexLastNext % NUM_FRAMES_TO_MONITOR][3] - Results[IndexLast % NUM_FRAMES_TO_MONITOR][3]) / 1000000);

				RecordedFrameInterval[NumRecordedFrameInterval++] = MsVal;
				if (NumRecordedFrameInterval == 100)
				{
					FString All;
					int32 NumOnTarget = 0;
					int32 NumBelowTarget = 0;
					int32 NumAboveTarget = 0;
					for (int32 Index = 0; Index < 100; Index++)
					{
						if (Index)
						{
							All += TCHAR(' ');
						}
						All += FString::Printf(TEXT("%d"), RecordedFrameInterval[Index]);

						if (RecordedFrameInterval[Index] > DesiredSyncIntervalRelativeTo60Hz * 16 - 8 && RecordedFrameInterval[Index] < DesiredSyncIntervalRelativeTo60Hz * 16 + 8)
						{
							NumOnTarget++;
						}
						else if (RecordedFrameInterval[Index] < DesiredSyncIntervalRelativeTo60Hz * 16)
						{
							NumBelowTarget++;
						}
						else
						{
							NumAboveTarget++;
						}
					}
					UE_LOG(LogRHI, Log, TEXT("%3d fast  %3d ok  %3d slow   %s"), NumBelowTarget, NumOnTarget, NumAboveTarget, *All);
					NumRecordedFrameInterval = 0;
				}
			}
		}
	}

	return true;
}

#endif