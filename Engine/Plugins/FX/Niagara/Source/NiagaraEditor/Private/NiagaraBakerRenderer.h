// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UNiagaraComponent;
class UNiagaraBakerSettings;
class UNiagaraSystem;
class UTextureRenderTarget2D;
class FCanvas;

struct FNiagaraBakerRenderer
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
	FNiagaraBakerRenderer(UNiagaraComponent* PreviewComponent, float WorldTime);
	FNiagaraBakerRenderer(UNiagaraComponent* PreviewComponent, UNiagaraBakerSettings* BakerSettings, float WorldTime);

	bool IsValid() const;

	bool RenderView(UTextureRenderTarget2D* RenderTarget, int32 iOutputTextureIndex) const;
	bool RenderView(UTextureRenderTarget2D* RenderTarget, FCanvas* Canvas, int32 iOutputTextureIndex, FIntRect ViewRect) const;

	static ERenderType GetRenderType(FName SourceName, FName& OutName);

	static TArray<FName> GatherAllRenderOptions(UNiagaraSystem* NiagaraSystem);

private:
	UNiagaraComponent* PreviewComponent = nullptr;
	UNiagaraBakerSettings* BakerSettings = nullptr;
	float WorldTime = 0.0f;
};
