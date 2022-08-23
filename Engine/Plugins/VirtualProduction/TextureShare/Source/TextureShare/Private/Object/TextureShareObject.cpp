// Copyright Epic Games, Inc. All Rights Reserved.

#include "Object/TextureShareObject.h"
#include "Object/TextureShareObjectProxy.h"
#include "Module/TextureShareLog.h"
#include "Misc/TextureShareStrings.h"

#include "ITextureShareCoreObject.h"

//////////////////////////////////////////////////////////////////////////////////////////////
// FTextureShareObject
//////////////////////////////////////////////////////////////////////////////////////////////
FTextureShareObject::FTextureShareObject(const TSharedRef<ITextureShareCoreObject, ESPMode::ThreadSafe>& InCoreObject)
	: CoreObject(InCoreObject)
	, ObjectProxy(new FTextureShareObjectProxy(CoreObject))
	, TextureShareData(MakeShared<FTextureShareData, ESPMode::ThreadSafe>())
{ }

FTextureShareObject::~FTextureShareObject()
{
	EndSession();
}

//////////////////////////////////////////////////////////////////////////////////////////////
const FString& FTextureShareObject::GetName() const
{
	return CoreObject->GetObjectDesc().ShareName;
}

const FTextureShareCoreObjectDesc& FTextureShareObject::GetObjectDesc() const
{
	return CoreObject->GetObjectDesc();
}

bool FTextureShareObject::IsActive() const
{
	return CoreObject->IsActive();
}

bool FTextureShareObject::IsFrameSyncActive() const
{
	return bFrameSyncActive && CoreObject->IsFrameSyncActive();
}

bool FTextureShareObject::SetProcessId(const FString& InProcessId)
{
	return CoreObject->SetProcessId(InProcessId);
}

bool FTextureShareObject::SetSyncSetting(const FTextureShareCoreSyncSettings& InSyncSetting)
{
	return CoreObject->SetSyncSetting(InSyncSetting);
}

const FTextureShareCoreSyncSettings& FTextureShareObject::GetSyncSetting() const
{
	return CoreObject->GetSyncSetting();
}

FTextureShareCoreFrameSyncSettings FTextureShareObject::GetFrameSyncSettings(const ETextureShareFrameSyncTemplate InType) const
{
	return CoreObject->GetFrameSyncSettings(InType);
}

//////////////////////////////////////////////////////////////////////////////////////////////
bool FTextureShareObject::BeginSession()
{
	if (!bSessionActive && CoreObject->BeginSession())
	{
		bSessionActive = true;

		FTextureShareObjectProxy::BeginSession_GameThread(*this);

		return true;
	}

	return false;
}

bool FTextureShareObject::EndSession()
{
	if (bSessionActive)
	{
		bSessionActive = false;

		FTextureShareObjectProxy::EndSession_GameThread(*this);

		return true;
	}

	return false;
}

bool FTextureShareObject::IsSessionActive() const
{
	return CoreObject->IsSessionActive();
}

//////////////////////////////////////////////////////////////////////////////////////////////
bool FTextureShareObject::BeginFrameSync()
{
	if (CoreObject->IsBeginFrameSyncActive() && CoreObject->LockThreadMutex(ETextureShareThreadMutex::GameThread))
	{
		if (CoreObject->BeginFrameSync())
		{
			bFrameSyncActive = true;

			return true;
		}
	}

	return false;
}

bool FTextureShareObject::EndFrameSync(FViewport* InViewport)
{
	if (InViewport)
	{
		UpdateViewExtension(InViewport);
	}

	bFrameSyncActive = false;

	// Store gamethread data for proxy
	TextureShareData->ObjectData = CoreObject->GetData();
	TextureShareData->ReceivedObjectsData.Append(CoreObject->GetReceivedData());

	FTextureShareObjectProxy::UpdateProxy_GameThread(*this);

	// Reset ptr to data, now this data used in proxy
	TextureShareData = MakeShared<FTextureShareData, ESPMode::ThreadSafe>();

	// Game thread data now sent to the proxy.Game-thread data can now be cleared
	const bool bResult = CoreObject->EndFrameSync();

	CoreObject->UnlockThreadMutex(ETextureShareThreadMutex::RenderingThread);

	return bResult;
}

bool FTextureShareObject::FrameSync(const ETextureShareSyncStep InSyncStep)
{
	if (IsFrameSyncActive() && CoreObject->FrameSync(InSyncStep))
	{
		return true;
	}

	return false;
}

//////////////////////////////////////////////////////////////////////////////////////////////
const TArray<FTextureShareCoreObjectDesc>& FTextureShareObject::GetConnectedInterprocessObjects() const
{
	return CoreObject->GetConnectedInterprocessObjects();
}

FTextureShareCoreData& FTextureShareObject::GetCoreData()
{
	return CoreObject->GetData();
}

const FTextureShareCoreData& FTextureShareObject::GetCoreData() const
{
	return CoreObject->GetData();
}

const TArray<FTextureShareCoreObjectData>& FTextureShareObject::GetReceivedCoreObjectData() const
{
	return CoreObject->GetReceivedData();
}

//////////////////////////////////////////////////////////////////////////////////////////////
FTextureShareData& FTextureShareObject::GetData()
{
	return *TextureShareData;
}

TSharedPtr<FTextureShareSceneViewExtension, ESPMode::ThreadSafe> FTextureShareObject::GetViewExtension() const
{
	return ViewExtension;
}

TSharedPtr<ITextureShareObjectProxy, ESPMode::ThreadSafe> FTextureShareObject::GetProxy() const
{
	return ObjectProxy;
}

void FTextureShareObject::UpdateViewExtension(FViewport* InViewport)
{
	check(InViewport);

	if (!ViewExtension.IsValid())
	{
		// create new one
		ViewExtension = FSceneViewExtensions::NewExtension<FTextureShareSceneViewExtension>(ObjectProxy, InViewport);
	}
	else
	{
		// Update viewport at runtime
		ViewExtension->SetLinkedViewport(InViewport);
	}
}
