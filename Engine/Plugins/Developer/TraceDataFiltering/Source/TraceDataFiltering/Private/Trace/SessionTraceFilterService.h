// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ISessionTraceFilterService.h"
#include "BaseSessionFilterService.h"

namespace Trace
{
	class IAnalysisSession;
	typedef uint64 FSessionHandle;
}

/** Implementation of ISessionTraceFilterService specifically to use with Trace, using Trace::IChannelProvider to provide information about Channels available on the running application 
 and Trace::ISessionService to change the Channel state(s). */
class FSessionTraceFilterService : public FBaseSessionFilterService
{
public:
	FSessionTraceFilterService(Trace::FSessionHandle InHandle, TSharedPtr<const Trace::IAnalysisSession> InSession);
	virtual ~FSessionTraceFilterService() {}

	/** Begin FBaseSessionFilterService overrides */
	virtual void OnApplyChannelChanges() override;
	/** End OnApplyChannelChanges overrides */
};
