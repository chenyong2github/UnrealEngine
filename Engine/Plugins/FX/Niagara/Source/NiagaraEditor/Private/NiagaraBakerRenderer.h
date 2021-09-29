// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/GCObject.h"

class UNiagaraComponent;
class UNiagaraBakerSettings;
class UNiagaraSystem;
class UTextureRenderTarget2D;
class FCanvas;

struct FNiagaraBakerRenderer : FGCObject
{
public:
	enum class ERenderType
	{
		None,
		SceneCapture,
		BufferVisualization,
		DataInterface,
		Particle
	};

public:
	FNiagaraBakerRenderer();
	virtual ~FNiagaraBakerRenderer();
	bool RenderView(UNiagaraComponent* PreviewComponent, const UNiagaraBakerSettings* BakerSettings, float WorldTime, UTextureRenderTarget2D* RenderTarget, int32 iOutputTextureIndex) const;
	bool RenderView(UNiagaraComponent* PreviewComponent, const UNiagaraBakerSettings* BakerSettings, float WorldTime, UTextureRenderTarget2D* RenderTarget, FCanvas* Canvas, int32 iOutputTextureIndex, FIntRect ViewRect) const;

	// FGCObject Impl
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override
	{
		return TEXT("FNiagaraBakerRenderer");
	}
	// FGCObject Impl

	static ERenderType GetRenderType(FName SourceName, FName& OutName);

	static TArray<FName> GatherAllRenderOptions(UNiagaraSystem* NiagaraSystem);

private:
	class USceneCaptureComponent2D* SceneCaptureComponent = nullptr;
};
