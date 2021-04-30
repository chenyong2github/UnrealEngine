// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Virtualization/IVirtualizationSourceControlUtilities.h"

namespace UE
{
namespace Virtualization
{

class FVirtualizationSourceControlUtilities : public IVirtualizationSourceControlUtilities
{
public:
	FVirtualizationSourceControlUtilities() = default;
	~FVirtualizationSourceControlUtilities() = default;

private:
	/** 
	 * Wrapper call around SyncPayloadSidecarFileInternal which will either call it directly
	 * or attempt to marshal it to the main thread if needed 
	 */
	virtual bool SyncPayloadSidecarFile(const FPackagePath& PackagePath) override;

	/** Effectively the override of IVirtualizationSourceControlUtilities::SyncPayloadSidecarFile */
	bool SyncPayloadSidecarFileInternal(const FPackagePath& PackagePath);
};

} // namespace Virtualization
} // namespace UE
