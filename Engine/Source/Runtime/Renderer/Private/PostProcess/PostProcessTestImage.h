// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ScreenPass.h"
#include "PostProcess/RenderingCompositionGraph.h"

void AddTestImagePass(FRDGBuilder& GraphBuilder, const FScreenPassViewInfo& ScreenPassView, FRDGTextureRef OutputTexture, FIntRect OutputViewRect);

FRenderingCompositeOutputRef AddTestImagePass(FRenderingCompositionGraph& Graph, FRenderingCompositeOutputRef Input);