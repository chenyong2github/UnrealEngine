// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UNiagaraComponent;
class UNiagaraFlipbookSettings;
class UNiagaraSystem;
class UTextureRenderTarget2D;
class FCanvas;

struct FNiagaraFlipbookRenderer
{
public:
	enum class ERenderType
	{
		None,
		View,
		DataInterface,
		Particle
	};

public:
	FNiagaraFlipbookRenderer(UNiagaraComponent* PreviewComponent, float WorldTime);
	FNiagaraFlipbookRenderer(UNiagaraComponent* PreviewComponent, UNiagaraFlipbookSettings* FlipbookSettings, float WorldTime);

	bool IsValid() const;

	bool RenderView(UTextureRenderTarget2D* RenderTarget, int32 iOutputTextureIndex) const;
	bool RenderView(UTextureRenderTarget2D* RenderTarget, FCanvas* Canvas, int32 iOutputTextureIndex, FIntRect ViewRect) const;

	static ERenderType GetRenderType(FName SourceName, FName& OutName);

	static TArray<FName> GatherAllRenderOptions(UNiagaraSystem* NiagaraSystem);

private:
	UNiagaraComponent* PreviewComponent = nullptr;
	UNiagaraFlipbookSettings* FlipbookSettings = nullptr;
	float WorldTime = 0.0f;
};
