// Copyright Epic Games, Inc. All Rights Reserved.

// Use ShaderPrint to debug print from any shader type.
// Call BeginView() at the start of a view to init the debug output buffer.
// Call DrawView() to composite the debug data to the final render target.
// Call EndView() at the end of the view to tidy up.

// The shader HLSL code should include the ShaderPrintCommon.ush and use the ShaderPrint*() functions.
// The shader C++ code needs to bind the buffers required to capture the debug print. See ShaderPrintParameters.h for this. 

#pragma once

#include "RenderGraph.h"
#include "Templates/RefCounting.h"
#include "ScreenPass.h"

class FViewInfo;

namespace ShaderPrint
{
	// Does the platform support the ShaderPrint system?
	bool IsSupported(const EShaderPlatform Platform);
	// Have we enabled the ShaderPrint system?
	bool IsEnabled();
	// Call this to know if a view can render this debug information
	bool IsEnabled(const FViewInfo& View);

	// Accessors for controlling from code: enabling/disabling, font size, and max drawn element
	void SetEnabled(bool bInEnabled);
	void SetFontSize(int32 InFontSize);
	void SetMaxValueCount(int32 InMaxCount);

	// Allocate the debug print buffer associated with the view
	void BeginView(FRDGBuilder& GraphBuilder, FViewInfo& View);
	// Draw info from the debug print buffer to the given output target
	void DrawView(FRDGBuilder& GraphBuilder, const FViewInfo& View, FScreenPassTexture OutputTexture);
	// Release the debug print buffer associated with the view
	void EndView(FViewInfo& View);
}
