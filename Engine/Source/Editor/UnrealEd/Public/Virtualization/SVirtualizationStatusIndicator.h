// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"
#include "Widgets/SWidget.h"

namespace UE::Virtualization
{

/** 
 * Returns the tool menu widget showing profiling data for content virtualization
 * in the editor.
 * 
 * @return	A valid widget pointer if content virtualization is enabled, and an 
 *			invalid pointer if it is disabled.
 */
UNREALED_API TSharedPtr<SWidget> GetVirtualizationStatusIndicator();

} // namespace UE::Virtualization
