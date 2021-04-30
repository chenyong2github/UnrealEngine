// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Features/IModularFeature.h"

class FPackagePath;

namespace UE
{
namespace Virtualization
{

class IVirtualizationSourceControlUtilities : public IModularFeature
{
public:
	/**
	 * Given a package path this method will attempt to sync th e.upayload file that is compatible with
	 * the .uasset file of the package.
	 * 
	 * We can make the following assumptions about the relationship between .uasset and .upayload files: 
	 * 1) The .uasset may be submitted to perforce without the .upayload (if the payload is unmodified)
	 * 2) If the payload is modified then the .uasset and .upayload file must be submitted at the same time.
	 * 3) The caller has already checked the existing .upayload file (if any) to see if it contains the payload
	 * that they are looking for.
	 * 
	 * If the above is true then we can sync the .upayload file to the same perforce changelist as the 
	 * * .uasset and be sure that we have the correct version.
	 * 
	 * Note that this has only been tested with perforce and so other source control solutions are currently
	 * unsupported.
	 */
	virtual bool SyncPayloadSidecarFile(const FPackagePath& PackagePath) = 0;
};

} // namespace Virtualization
} // namespace UE