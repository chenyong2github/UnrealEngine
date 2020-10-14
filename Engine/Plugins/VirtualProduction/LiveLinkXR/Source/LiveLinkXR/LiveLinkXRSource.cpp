// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveLinkXRSource.h"
#include "LiveLinkXR.h"
#include "ILiveLinkClient.h"
#include "Engine/Engine.h"
#include "Async/Async.h"
#include "LiveLinkXRSourceSettings.h"
#include "Misc/CoreDelegates.h"
#include "Roles/LiveLinkTransformRole.h"
#include "Roles/LiveLinkTransformTypes.h"

#define LOCTEXT_NAMESPACE "LiveLinkXRSourceFactory"

FLiveLinkXRSource::FLiveLinkXRSource()
	: FLiveLinkXRSource(GetDefault<ULiveLinkXRSettingsObject>()->Settings)
{
}

FLiveLinkXRSource::FLiveLinkXRSource(const FLiveLinkXRSettings& Settings)
: Client(nullptr)
, Stopping(false)
, Thread(nullptr)
, FrameCounter(0)
{
	SourceStatus = LOCTEXT("SourceStatus_NoData", "No data");
	SourceType = LOCTEXT("SourceType_XR", "XR");
	SourceMachineName = LOCTEXT("XRSourceMachineName", "Local XR");

	if (!GEngine->XRSystem.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("LiveLinkXRSource: Couldn't find a valid XR System!"));
		return;
	}

	if (GEngine->XRSystem->GetSystemName() != FName(TEXT("SteamVR")))
	{
		UE_LOG(LogTemp, Error, TEXT("LiveLinkXRSource: Couldn't find a compatible XR System - currently, only SteamVR is supported!"));
		return;
	}

	bTrackTrackers = Settings.bTrackTrackers;
	bTrackControllers = Settings.bTrackControllers;
	bTrackHMDs = Settings.bTrackHMDs;
	LocalUpdateRateInHz = Settings.LocalUpdateRateInHz;

	DeferredStartDelegateHandle = FCoreDelegates::OnEndFrame.AddRaw(this, &FLiveLinkXRSource::Start);
}

FLiveLinkXRSource::~FLiveLinkXRSource()
{
	// This could happen if the object is destroyed before FCoreDelegates::OnEndFrame calls FLiveLinkXRSource::Start
	if (DeferredStartDelegateHandle.IsValid())
	{
		FCoreDelegates::OnEndFrame.Remove(DeferredStartDelegateHandle);
	}

	Stop();

	if (Thread != nullptr)
	{
		Thread->WaitForCompletion();
		delete Thread;
		Thread = nullptr;
	}
}

void FLiveLinkXRSource::ReceiveClient(ILiveLinkClient* InClient, FGuid InSourceGuid)
{
	Client = InClient;
	SourceGuid = InSourceGuid;
}


bool FLiveLinkXRSource::IsSourceStillValid() const
{
	// Source is valid if we have a valid thread
	bool bIsSourceValid = !Stopping && (Thread != nullptr);
	return bIsSourceValid;
}


bool FLiveLinkXRSource::RequestSourceShutdown()
{
	Stop();

	return true;
}

void FLiveLinkXRSource::EnumerateTrackedDevices()
{
	TrackedDevices.Empty();
	TrackedDeviceTypes.Empty();
	TrackedSubjectNames.Empty();

	if (!GEngine->XRSystem.IsValid())
	{
		return;
	}

	if (GEngine->XRSystem->GetSystemName() != FName(TEXT("SteamVR")))
	{
		return;
	}

	// Create subject names for all requested tracked devices

	TArray<int32> AllTrackedDevices;

	if (!GEngine->XRSystem->EnumerateTrackedDevices(AllTrackedDevices, EXRTrackedDeviceType::Any))
	{
		return;
	}

	for (int32 Tracker = 0; Tracker < AllTrackedDevices.Num(); Tracker++)
	{
		FString SubjectName = GEngine->XRSystem->GetSystemName().ToString();
		bool bValidDevice = false;
		switch ((int32)GEngine->XRSystem->GetTrackedDeviceType(AllTrackedDevices[Tracker]))
		{
		case (int32)EXRTrackedDeviceType::Other:
			if (bTrackTrackers)
			{
				SubjectName += TEXT("Tracker_");
				bValidDevice = true;
			}
			break;

		case (int32)EXRTrackedDeviceType::HeadMountedDisplay:
			if (bTrackHMDs)
			{
				SubjectName += TEXT("HMD_");
				bValidDevice = true;
			}
			break;

		case (int32)EXRTrackedDeviceType::Controller:
			if (bTrackControllers)
			{
				SubjectName += TEXT("Controller_");
				bValidDevice = true;
			}
			break;
		}

		if (bValidDevice)
		{
			SubjectName += GEngine->XRSystem->GetTrackedDevicePropertySerialNumber(AllTrackedDevices[Tracker]);
			TrackedDevices.Add(AllTrackedDevices[Tracker]);
			TrackedDeviceTypes.Add(GEngine->XRSystem->GetTrackedDeviceType(AllTrackedDevices[Tracker]));
			TrackedSubjectNames.Add(SubjectName);

			UE_LOG(LogTemp, Log, TEXT("LiveLinkXRSource: Found a tracked device with DeviceId %d and named it %s"), AllTrackedDevices[Tracker], *SubjectName);
		}
	}
}

// FRunnable interface
void FLiveLinkXRSource::Start()
{
	check(DeferredStartDelegateHandle.IsValid());

	FCoreDelegates::OnEndFrame.Remove(DeferredStartDelegateHandle);
	DeferredStartDelegateHandle.Reset();

	EnumerateTrackedDevices();
	
	SourceStatus = LOCTEXT("SourceStatus_Receiving", "Receiving");

	ThreadName = "LiveLinkXR Receiver ";
	ThreadName.AppendInt(FAsyncThreadIndex::GetNext());

	Thread = FRunnableThread::Create(this, *ThreadName, 128 * 1024, TPri_AboveNormal, FPlatformAffinity::GetPoolThreadMask());
}

void FLiveLinkXRSource::Stop()
{
	Stopping = true;
}

uint32 FLiveLinkXRSource::Run()
{
	const double CheckDeltaTime = 1.0f / (double)LocalUpdateRateInHz;
	double StartTime = FApp::GetCurrentTime();
	double EndTime = StartTime + CheckDeltaTime;

	while (!Stopping)
	{
		// Send new poses at the user specified update rate
		if (EndTime - StartTime >= CheckDeltaTime)
		{
			StartTime = FApp::GetCurrentTime();

			// Send new poses at the user specified update rate
			for (int32 Tracker = 0; Tracker < TrackedDevices.Num(); Tracker++)
			{
				FQuat Orientation;
				FVector Position;

				if (GEngine->XRSystem->IsTracking(TrackedDevices[Tracker]) && GEngine->XRSystem->GetCurrentPose(TrackedDevices[Tracker], Orientation, Position))
				{
					TSharedRef<FLiveLinkTransformFrameData> TransformFrameData = MakeShared<FLiveLinkTransformFrameData>();
					TransformFrameData->Transform = FTransform(Orientation, Position);
					TransformFrameData->WorldTime = FLiveLinkWorldTime(FDateTime::UtcNow().GetTicks() / (double)ETimespan::TicksPerSecond);
					TransformFrameData->MetaData.StringMetaData.Add(FName(TEXT("DeviceId")), FString::Printf(TEXT("%d"), TrackedDevices[Tracker]));
					TransformFrameData->MetaData.StringMetaData.Add(FName(TEXT("DeviceType")), GetDeviceTypeName(TrackedDeviceTypes[Tracker]));
					TransformFrameData->MetaData.StringMetaData.Add(FName(TEXT("DeviceControlId")), TrackedSubjectNames[Tracker]);

					Send(TransformFrameData, FName(TrackedSubjectNames[Tracker]));
				}
			}

			FrameCounter++;
		}

		FPlatformProcess::Sleep(0.001f);
		EndTime = FApp::GetCurrentTime();
	}
	
	return 0;
}

void FLiveLinkXRSource::Send(TSharedRef<FLiveLinkTransformFrameData> FrameDataToSend, FName SubjectName)
{
	if (Stopping || (Client == nullptr))
	{
		return;
	}

	if (!EncounteredSubjects.Contains(SubjectName))
	{
		FLiveLinkStaticDataStruct StaticData(FLiveLinkTransformStaticData::StaticStruct());
		Client->PushSubjectStaticData_AnyThread({SourceGuid, SubjectName}, ULiveLinkTransformRole::StaticClass(), MoveTemp(StaticData));
		EncounteredSubjects.Add(SubjectName);
	}

	FLiveLinkFrameDataStruct FrameData(FLiveLinkTransformFrameData::StaticStruct());
	*FrameData.Cast<FLiveLinkTransformFrameData>() = *FrameDataToSend;
	Client->PushSubjectFrameData_AnyThread({ SourceGuid, SubjectName }, MoveTemp(FrameData));
}

const FString FLiveLinkXRSource::GetDeviceTypeName(EXRTrackedDeviceType DeviceType)
{
	switch ((int32)DeviceType)
	{
		case (int32)EXRTrackedDeviceType::Invalid:				return TEXT("Invalid");
		case (int32)EXRTrackedDeviceType::HeadMountedDisplay:	return TEXT("HMD");
		case (int32)EXRTrackedDeviceType::Controller:			return TEXT("Controller");
		case (int32)EXRTrackedDeviceType::Other:				return TEXT("Tracker");
	}

	return FString::Printf(TEXT("Unknown (%i)"), (int32)DeviceType);
}

#undef LOCTEXT_NAMESPACE
