// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UI/SRenderPagesPageViewerPreview.h"


namespace UE::RenderPages::Private
{
	/**
	 * A page viewer widget, allows the user to render a page in low-resolution and afterwards scrub through the outputted frames of it in the editor.
	 */
	class SRenderPagesPageViewerRendered : public SRenderPagesPageViewerPreview
	{
	protected:
		virtual bool IsPreviewWidget() const override { return false; }
	};
}
