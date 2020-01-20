// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ISessionTraceFilterService.h"
#include "Misc/DateTime.h"

namespace Trace
{
	class IAnalysisSession;
	typedef uint64 FSessionHandle;
}

class IEventInfoProvider;

/** Implementation of ISessionTraceFilterService specifically to use with Trace, using Trace::IChannelProvider to provide information about Channels available on the running application 
 and Trace::ISessionService to change the Channel state(s). */
class FSessionTraceFilterService : public ISessionTraceFilterService
{
public:
	FSessionTraceFilterService(Trace::FSessionHandle InHandle, TSharedPtr<const Trace::IAnalysisSession> InSession);
	virtual ~FSessionTraceFilterService() {}

	/** Begin ISessionTraceFilterService overrides */
	virtual void GetRootObjects(TArray<FTraceObjectInfo>& OutObjects) const override;
	virtual void GetChildObjects(uint32 InObjectHash, TArray<FTraceObjectInfo>& OutChildObjects) const override;
	virtual const FDateTime& GetTimestamp() override;
	virtual void SetObjectFilterState(const FString& InObjectName, const bool bFilterState) override;
	virtual void UpdateFilterPresets(const TArray<TSharedPtr<IFilterPreset>>& InPresets) override;
	/** End ISessionTraceFilterService overrides */

protected:
	/** Callback at end of engine frame, used to dispatch all enabled/disabled channels */
	void OnEndFrame();

	/** Retrieves channels names from provider and marks them all as disabled */
	void DisableAllChannels();
protected:
	/** Session this instance represents the filtering service for */
	TSharedPtr<const Trace::IAnalysisSession> Session;
	/** Session handle for AnalyisSession*/
	Trace::FSessionHandle Handle;

	/** Names of channels that were either enabled or disabled during the duration of this frame */
	TSet<FString> FrameEnabledChannels;
	TSet<FString> FrameDisabledChannels;
	
	/** Timestamp at which contained data (including provider) was last updated */
	FDateTime TimeStamp;	
};
