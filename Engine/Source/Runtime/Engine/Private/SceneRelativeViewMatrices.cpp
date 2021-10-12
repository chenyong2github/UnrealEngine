// Copyright Epic Games, Inc. All Rights Reserved.

#include "SceneRelativeViewMatrices.h"
#include "Misc/LargeWorldRenderPosition.h"
#include "SceneView.h"

FRelativeViewMatrices FRelativeViewMatrices::Create(const FViewMatrices& Matrices, const FViewMatrices& PrevMatrices)
{
	FInitializer Initializer;
	Initializer.ViewToWorld = Matrices.GetInvViewMatrix();
	Initializer.WorldToView = Matrices.GetViewMatrix();
	Initializer.ViewToClip = Matrices.GetProjectionMatrix();
	Initializer.ClipToView = Matrices.GetInvProjectionMatrix();
	Initializer.PrevViewToWorld = PrevMatrices.GetInvViewMatrix();
	Initializer.PrevClipToView = PrevMatrices.GetInvProjectionMatrix();
	return Create(Initializer);
}

FRelativeViewMatrices FRelativeViewMatrices::Create(const FInitializer& Initializer)
{
	const FLargeWorldRenderPosition AbsoluteOrigin(Initializer.ViewToWorld.GetOrigin());

	FRelativeViewMatrices Result;
	Result.TilePosition = AbsoluteOrigin.GetTile();
	Result.RelativeWorldToView = AbsoluteOrigin.MakeFromRelativeWorldMatrix(Initializer.WorldToView);
	Result.ViewToRelativeWorld = AbsoluteOrigin.MakeToRelativeWorldMatrix(Initializer.ViewToWorld);
	Result.ViewToClip = FMatrix44f(Initializer.ViewToClip);
	Result.ClipToView = FMatrix44f(Initializer.ClipToView);
	Result.RelativeWorldToClip = Result.RelativeWorldToView * Result.ViewToClip;
	Result.ClipToRelativeWorld = Result.ClipToView * Result.ViewToRelativeWorld;

	Result.PrevViewToRelativeWorld = AbsoluteOrigin.MakeClampedToRelativeWorldMatrix(Initializer.PrevViewToWorld);
	Result.PrevClipToView = FMatrix44f(Initializer.PrevClipToView);
	Result.PrevClipToRelativeWorld = Result.PrevClipToView * Result.PrevViewToRelativeWorld;
	return Result;
}
