// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.
#pragma once

#include "MixedRealityInterop.h"
#include <winrt\Microsoft.Azure.SpatialAnchors.h>
#include "SpatialAnchorHelper.h"
#include <winrt\Windows.UI.Input.Spatial.h>

namespace WindowsMixedReality
{
	std::shared_ptr<WindowsMixedReality::SpatialAnchorHelper> GetSpatialAnchorHelper();
}
