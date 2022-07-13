// Copyright Epic Games, Inc. All Rights Reserved.

#include "LandscapeTextureHeightPatch.h"

#include "Engine/TextureRenderTarget2D.h"
#include "Landscape.h"
#include "LandscapeDataAccess.h"
#include "LandscapePatchLogging.h"
#include "LandscapePatchManager.h"
#include "RenderGraph.h" // RDG_EVENT_NAME
#include "RHIStaticStates.h"
#include "TextureCompiler.h"
#include "UObject/ObjectSaveContext.h"

#define LOCTEXT_NAMESPACE "LandscapeTextureHeightPatch"

#if WITH_EDITOR
namespace LandscapeTextureHeightPatchLocals
{
	static const FLinearColor ClearColor = LandscapeDataAccess::PackHeight(LandscapeDataAccess::MidValue).ReinterpretAsLinear();

	void CopyTextureOnRenderThread(FRHICommandListImmediate& RHICmdList, const FTextureResource& Source, FTextureResource& Destination)
	{
		using namespace UE::Landscape;

		FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("LandscapeTextureHeightPatchCopyTexture"));

		FRDGTextureRef SourceTexture = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(Source.GetTexture2DRHI(), TEXT("CopySource")));
		FRDGTextureRef DestinationTexture = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(Destination.GetTexture2DRHI(), TEXT("CopyDestination")));

		// All my efforts of getting CopyToResolveTarget to work without complaints have failed, so we just use our own copy shader.
		FSimpleTextureCopyPS::AddToRenderGraph(GraphBuilder, SourceTexture, DestinationTexture);

		GraphBuilder.Execute();
	}
}

void ULandscapeTextureHeightPatch::ConvertInternalRenderTargetBackFromNativeTexture(bool bLoading)
{
	using namespace LandscapeTextureHeightPatchLocals;
	using namespace UE::Landscape;

	if (IsValid(InternalTexture))
	{
		InternalTexture->UpdateResource();
	}

	// TODO: should the GetResource be an ensure?
	if (!IsValid(InternalTexture) || !InternalTexture->GetResource())
	{
		if (InternalRenderTarget)
		{
			Modify();
			InternalRenderTarget = nullptr;
		}
		return;
	}

	if (IsValid(InternalRenderTarget))
	{
		InternalRenderTarget->Modify();
	}

	FTextureCompilingManager::Get().FinishCompilation({ InternalTexture });

	ResizeRenderTargetIfNeeded(InternalTexture->GetResource()->GetSizeX(), 
		InternalTexture->GetResource()->GetSizeY());

	FTextureResource* Source = InternalTexture->GetResource();
	FTextureResource* Destination = InternalRenderTarget->GetResource();

	// If we're in a different format, we need to "un-bake" the height from the texture.
	if (InternalRenderTarget->RenderTargetFormat != ETextureRenderTargetFormat::RTF_RGBA8)
	{
		FLandscapeHeightPatchConvertToNativeParams ConversionParams = GetConversionParams();
		if (bLoading)
		{
			ConversionParams.HeightScale = SavedConversionHeightScale;
		}

		ENQUEUE_RENDER_COMMAND(LandscapeTextureHeightPatchRTToTexture)(
			[ConversionParams, Source, Destination](FRHICommandListImmediate& RHICmdList)
		{
			using namespace UE::Landscape;

			FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("LandscapeTextureHeightPatchConvertFromNative"));

			FRDGTextureRef SourceTexture = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(Source->GetTexture2DRHI(), TEXT("ConversionSource")));
			FRDGTextureRef DestinationTexture = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(Destination->GetTexture2DRHI(), TEXT("ConversionDestination")));

			FConvertBackFromNativeLandscapePatchPS::AddToRenderGraph(GraphBuilder, SourceTexture, DestinationTexture, ConversionParams);

			GraphBuilder.Execute();
		});
	}
	else
	{
		// When formats match, we can just copy back and forth.
		ENQUEUE_RENDER_COMMAND(LandscapeTextureHeightPatchRTToTexture)(
			[Source, Destination](FRHICommandListImmediate& RHICmdList)
		{
			CopyTextureOnRenderThread(RHICmdList, *Source, *Destination);
		});
	}
}

void ULandscapeTextureHeightPatch::ConvertInternalRenderTargetToNativeTexture(bool bBlock)
{
	using namespace LandscapeTextureHeightPatchLocals;
	using namespace UE::Landscape;

	if (!IsValid(InternalRenderTarget))
	{
		if (InternalTexture)
		{
			Modify();
			InternalTexture = nullptr;
		}
		return;
	}

	if (IsValid(InternalTexture))
	{
		InternalTexture->Modify();
	}
	ResizeTextureIfNeeded(InternalRenderTarget->SizeX, InternalRenderTarget->SizeY, /*bClear*/ false, /*bUpdateResource*/ false);

	UTextureRenderTarget2D* NativeEncodingRenderTarget = InternalRenderTarget;

	// If the format doesn't match the format that we use generally for our internal texture, save the patch in our native
	// height format, applying whatever scale/offset is relevant. The stored texture thus ends up being the native equivalent
	// (with scale 1 and offset 0). This is easier than trying to support various kinds of RT-to-texture conversions.
	if (NativeEncodingRenderTarget->RenderTargetFormat != ETextureRenderTargetFormat::RTF_RGBA8)
	{
		// We need a temporary render target to write the converted result, then we'll copy that to the texture.
		NativeEncodingRenderTarget = NewObject<UTextureRenderTarget2D>(this);
		NativeEncodingRenderTarget->ClearColor = ClearColor;
		NativeEncodingRenderTarget->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
		NativeEncodingRenderTarget->InitAutoFormat(InternalRenderTarget->SizeX, InternalRenderTarget->SizeY);
		NativeEncodingRenderTarget->UpdateResourceImmediate(false);

		FTextureResource* Source = InternalRenderTarget->GetResource();
		FTextureResource* Destination = NativeEncodingRenderTarget->GetResource();

		FLandscapeHeightPatchConvertToNativeParams ConversionParams = GetConversionParams();
		SavedConversionHeightScale = ConversionParams.HeightScale;

		ENQUEUE_RENDER_COMMAND(LandscapeTextureHeightPatchRTToTexture)(
			[ConversionParams, Source, Destination](FRHICommandListImmediate& RHICmdList)
		{
			using namespace UE::Landscape;

			FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("LandscapeTextureHeightPatchConvertToNative"));

			FRDGTextureRef SourceTexture = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(Source->GetTexture2DRHI(), TEXT("ConversionSource")));
			FRDGTextureRef DestinationTexture = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(Destination->GetTexture2DRHI(), TEXT("ConversionDestination")));

			FConvertToNativeLandscapePatchPS::AddToRenderGraph(GraphBuilder, SourceTexture, DestinationTexture, ConversionParams);

			GraphBuilder.Execute();
		});
	}

	// Write the render target to the texture.
	// TODO: The header for this requires the texture to be square power of 2, but it actually doesn't seem to
	// be an enforced requirement. If that changes, we'll need our own ReadPixels followed by locking a mip
	// and writing to it.
	// This call does a flush for us, so the render target should be updated.
	NativeEncodingRenderTarget->UpdateTexture2D(InternalTexture, ETextureSourceFormat::TSF_BGRA8);

	InternalTexture->UpdateResource();
	
	if (bBlock)
	{
		FTextureCompilingManager::Get().FinishCompilation({ InternalTexture });
	}
}

FLandscapeHeightPatchConvertToNativeParams ULandscapeTextureHeightPatch::GetConversionParams()
{
	// When doing conversions, we bake into a height in the same way that we do when applying the patch.

	FLandscapeHeightPatchConvertToNativeParams ConversionParams;
	ConversionParams.ZeroInEncoding = EncodingSettings.ZeroInEncoding;

	double LandscapeHeightScale = Landscape.IsValid() ? Landscape->GetTransform().GetScale3D().Z : 1;
	LandscapeHeightScale = LandscapeHeightScale == 0 ? 1 : LandscapeHeightScale;
	ConversionParams.HeightScale = EncodingSettings.WorldSpaceEncodingScale * LANDSCAPE_INV_ZSCALE / LandscapeHeightScale;

	// TODO: We can choose whether we want to bake in the height offset if it exists. Doing so will handle
	// some edge cases where the value stored in the patch is outside the range storeable in the native format
	// normally, but within the range of the landscape due to the patch being far above/below the landscape to
	// compensate. However, while this is good for conversions for the purposes of serialization, it's not good
	// for conversions for the purposes of source mode change, so we would need to do things slightly differently
	// in the two cases. For now, we'll just not bother with that (unlikely?) edge case.
	ConversionParams.HeightOffset = 0;

	return ConversionParams;
}

// Render targets don't get serialized, so whenever we need to save, copy, etc, we convert
// to a UTexture2D, and then we convert back when needed.
void ULandscapeTextureHeightPatch::PostLoad()
{
	Super::PostLoad();

	if (SourceMode == ELandscapeTexturePatchSourceMode::TextureBackedRenderTarget && InternalTexture)
	{
		InternalTexture->ConditionalPostLoad();
		ConvertInternalRenderTargetBackFromNativeTexture(/* bLoading =*/ true);
	}
}

void ULandscapeTextureHeightPatch::PreSave(FObjectPreSaveContext SaveContext)
{
	Super::PreSave(SaveContext);

	if (SourceMode == ELandscapeTexturePatchSourceMode::TextureBackedRenderTarget)
	{
		ConvertInternalRenderTargetToNativeTexture(true);
	}
}

void ULandscapeTextureHeightPatch::PreDuplicate(FObjectDuplicationParameters& DupParams)
{
	Super::PreDuplicate(DupParams);

	if (SourceMode == ELandscapeTexturePatchSourceMode::TextureBackedRenderTarget)
	{
		ConvertInternalRenderTargetToNativeTexture(true);
	}
}

// Called when serializing to text for copy/paste
void ULandscapeTextureHeightPatch::ExportCustomProperties(FOutputDevice& Out, uint32 Indent)
{
	Super::ExportCustomProperties(Out, Indent);

	if (SourceMode == ELandscapeTexturePatchSourceMode::TextureBackedRenderTarget)
	{
		ConvertInternalRenderTargetToNativeTexture(true);
	}
}

void ULandscapeTextureHeightPatch::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.Property)
	{
		if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(ULandscapeTextureHeightPatch, SourceMode))
		{
			SetSourceMode(SourceMode);
			if (SourceMode == ELandscapeTexturePatchSourceMode::InternalTexture)
			{
				FTextureCompilingManager::Get().FinishCompilation({ InternalTexture });
			}
		}
		else if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(ULandscapeTextureHeightPatch, InitializationMode))
		{
			bShowTextureAssetProperty = InitializationMode == ELandscapeTextureHeightPatchInitMode::TextureAsset;
		}
		else if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(ULandscapeTextureHeightPatch, SourceEncoding))
		{
			ResetSourceEncodingMode(SourceEncoding);
		}
	}
		
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void ULandscapeTextureHeightPatch::PostInitProperties()
{
	bShowTextureAssetProperty = InitializationMode == ELandscapeTextureHeightPatchInitMode::TextureAsset;

	Super::PostInitProperties();
}

void ULandscapeTextureHeightPatch::OnComponentCreated()
{
	using namespace LandscapeTextureHeightPatchLocals;

	Super::OnComponentCreated();

	if (bWasCopy)
	{
		if (SourceMode == ELandscapeTexturePatchSourceMode::TextureBackedRenderTarget
			&& IsValid(InternalTexture))
		{
			ConvertInternalRenderTargetBackFromNativeTexture();
		}
	}
	else // if not copy, ie adding a totally new component
	{
		// Set component location to be the bottom of the parent actor bounding box.
		AActor* ParentActor = GetAttachParentActor();
		if (ParentActor)
		{
			FVector Origin, BoxExtent;
			GetAttachParentActor()->GetActorBounds(false, Origin, BoxExtent);

			Origin.Z -= BoxExtent.Z;
			SetWorldLocation(Origin);
		}
	}

	PreviousSourceMode = SourceMode;
}
#endif // WITH_EDITOR

void ULandscapeTextureHeightPatch::ResetSourceEncodingMode(ELandscapeTextureHeightPatchEncoding EncodingMode)
{
	Modify();
	SourceEncoding = EncodingMode;
	if (EncodingMode == ELandscapeTextureHeightPatchEncoding::ZeroToOne)
	{
		EncodingSettings.ZeroInEncoding = 0.5;
		EncodingSettings.WorldSpaceEncodingScale = 400;
	}
	else if (EncodingMode == ELandscapeTextureHeightPatchEncoding::WorldUnits)
	{
		EncodingSettings.ZeroInEncoding = 0;
		EncodingSettings.WorldSpaceEncodingScale = 1;
	}
}

void ULandscapeTextureHeightPatch::SetInternalRenderTargetFormat(ETextureRenderTargetFormat Format)
{
#if WITH_EDITOR
	Modify();
	InternalRenderTargetFormat = Format;
	if (InternalRenderTarget)
	{
		ResizeRenderTargetIfNeeded(InternalRenderTarget->SizeX, InternalRenderTarget->SizeY);
	}
#endif
}

#if WITH_EDITOR
void ULandscapeTextureHeightPatch::UpdateShaderParams(
	UE::Landscape::FApplyLandscapeTextureHeightPatchPS::FParameters& Params, 
	const FIntPoint& DestinationResolution, FIntRect& DestinationBoundsOut) const
{
	using namespace UE::Geometry;
	using namespace UE::Landscape;

	// We want our patch to be oriented with its Z axis to be along the Z axis of the landscape. The way we do this here
	// is by just changing the rotation component of the patch transform to be the rotation of the landscape, except for
	// yaw (rotation around Z axis) that we copy from the patch transform.
	// Obviously this is not perfect, but it's not clear whether there's a clean way to deal with differing transforms for
	// landscape vs patch.
	// TODO: Make it so that patches do not inherit parent non-yaw components of rotation?

	FTransform PatchToWorld = GetPatchToWorldTransform(); 
	
	FVector2D FullPatchDimensions = GetFullUnscaledWorldSize();
	Params.InPatchWorldDimensions = FVector2f(FullPatchDimensions);

	FTransform FromPatchUVToPatch(FQuat4d::Identity, FVector3d(-FullPatchDimensions.X / 2, -FullPatchDimensions.Y / 2, 0), 
		FVector3d(FullPatchDimensions.X, FullPatchDimensions.Y, 1));
	FMatrix44d PatchLocalToUVs = FromPatchUVToPatch.ToInverseMatrixWithScale();

	FTransform LandscapeHeightmapToWorld = PatchManager->GetHeightmapCoordsToWorld();
	FMatrix44d LandscapeToWorld = LandscapeHeightmapToWorld.ToMatrixWithScale();

	FMatrix44d WorldToPatch = PatchToWorld.ToInverseMatrixWithScale();

	// In unreal, matrix composition is done by multiplying the subsequent ones on the right, and the result
	// is transpose of what our shader will expect (because unreal right multiplies vectors by matrices).
	FMatrix44d LandscapeToPatchUVTransposed = LandscapeToWorld * WorldToPatch * PatchLocalToUVs;
	Params.InHeightmapToPatch = (FMatrix44f)LandscapeToPatchUVTransposed.GetTransposed();

	FVector3d ComponentScale = PatchToWorld.GetScale3D();
	double LandscapeHeightScale = Landscape.IsValid() ? Landscape->GetTransform().GetScale3D().Z : 1;
	LandscapeHeightScale = LandscapeHeightScale == 0 ? 1 : LandscapeHeightScale;

	bool bNativeEncoding = SourceMode == ELandscapeTexturePatchSourceMode::InternalTexture 
		|| SourceEncoding == ELandscapeTextureHeightPatchEncoding::NativePackedHeight;

	// To get height scale in heightmap coordinates, we have to undo the scaling that happens to map the 16bit int to [-256, 256), and undo
	// the landscape actor scale.
	Params.InHeightScale = bNativeEncoding ? 1 
		: LANDSCAPE_INV_ZSCALE * EncodingSettings.WorldSpaceEncodingScale / LandscapeHeightScale;
	if (bApplyComponentZScale)
	{
		Params.InHeightScale *= ComponentScale.Z;
	}

	Params.InZeroInEncoding = bNativeEncoding ? LandscapeDataAccess::MidValue : EncodingSettings.ZeroInEncoding;

	Params.InHeightOffset = 0;
	switch (ZeroHeightMeaning)
	{
	case ELandscapeTextureHeightPatchZeroHeightMeaning::LandscapeZ:
		break; // no offset necessary
	case ELandscapeTextureHeightPatchZeroHeightMeaning::PatchZ:
	{
		FVector3d PatchOriginInHeightmapCoords = LandscapeHeightmapToWorld.InverseTransformPosition(PatchToWorld.GetTranslation());
		Params.InHeightOffset = PatchOriginInHeightmapCoords.Z - LandscapeDataAccess::MidValue;
		break;
	}
	case ELandscapeTextureHeightPatchZeroHeightMeaning::WorldZero:
	{
		FVector3d WorldOriginInHeightmapCoords = LandscapeHeightmapToWorld.InverseTransformPosition(FVector::ZeroVector);
		Params.InHeightOffset = WorldOriginInHeightmapCoords.Z - LandscapeDataAccess::MidValue;
		break;
	}
	default:
		ensure(false);
	}

	// The outer half-pixel shouldn't affect the landscape because it is not part of our official coverage area.
	Params.InEdgeUVDeadBorder = FVector2f::Zero();
	FVector2D TextureResolution;
	if (GetTextureResolution(TextureResolution))
	{
		Params.InEdgeUVDeadBorder = FVector2f(0.5 / TextureResolution.X, 0.5 / TextureResolution.Y);
	}

	Params.InFalloffWorldMargin = Falloff / FMath::Min(ComponentScale.X, ComponentScale.Y);

	Params.InBlendMode = static_cast<uint32>(BlendMode);

	// Pack our booleans into a bitfield
	using EShaderFlags = FApplyLandscapeTextureHeightPatchPS::EFlags;
	EShaderFlags Flags = EShaderFlags::None;

	Flags |= (FalloffMode == ELandscapeTextureHeightPatchFalloffMode::RoundedRectangle) ?
		EShaderFlags::RectangularFalloff : EShaderFlags::None;

	Flags |= bUseTextureAlphaChannel ?
		EShaderFlags::ApplyPatchAlpha : EShaderFlags::None;

	Flags |= bNativeEncoding ?
		EShaderFlags::InputIsPackedHeight : EShaderFlags::None;

	Params.InFlags = static_cast<uint8>(Flags);

	// Get the output bounds, which are used to limit the amount of landscape pixels we have to process. 
	// To get them, convert all of the corners into heightmap 2d coordinates and get the bounding box.
	auto PatchUVToHeightmap2DCoordinates = [&PatchToWorld, &FromPatchUVToPatch, &LandscapeHeightmapToWorld](const FVector2f& UV)
	{
		FVector WorldPosition = PatchToWorld.TransformPosition(
			FromPatchUVToPatch.TransformPosition(FVector(UV.X, UV.Y, 0)));
		FVector HeightmapCoordinates = LandscapeHeightmapToWorld.InverseTransformPosition(WorldPosition);
		return FVector2d(HeightmapCoordinates.X, HeightmapCoordinates.Y);
	};
	FBox2D FloatBounds(ForceInit);
	FloatBounds += PatchUVToHeightmap2DCoordinates(FVector2f(0, 0));
	FloatBounds += PatchUVToHeightmap2DCoordinates(FVector2f(0,1));
	FloatBounds += PatchUVToHeightmap2DCoordinates(FVector2f(1, 0));
	FloatBounds += PatchUVToHeightmap2DCoordinates(FVector2f(1,1));

	DestinationBoundsOut = FIntRect(
		FMath::Clamp(FMath::Floor(FloatBounds.Min.X), 0, DestinationResolution.X - 1),
		FMath::Clamp(FMath::Floor(FloatBounds.Min.Y), 0, DestinationResolution.Y - 1),
		FMath::Clamp(FMath::CeilToInt(FloatBounds.Max.X) + 1, 0, DestinationResolution.X),
		FMath::Clamp(FMath::CeilToInt(FloatBounds.Max.Y) + 1, 0, DestinationResolution.Y));
}

UTextureRenderTarget2D* ULandscapeTextureHeightPatch::Render_Native(bool bIsHeightmap,
	UTextureRenderTarget2D* InCombinedResult,
	const FName& InWeightmapLayerName)
{
	using namespace UE::Landscape;

	if (!ensure(PatchManager.IsValid()) || SourceMode == ELandscapeTexturePatchSourceMode::None
		|| (SourceMode == ELandscapeTexturePatchSourceMode::TextureBackedRenderTarget && (!IsValid(InternalRenderTarget)))
		|| (SourceMode == ELandscapeTexturePatchSourceMode::TextureAsset && (!IsValid(TextureAsset) 
			|| !ensureMsgf(TextureAsset->VirtualTextureStreaming == 0, TEXT("ULandscapeTextureHeightPatch: Virtual textures are not supported"))))
		|| (SourceMode == ELandscapeTexturePatchSourceMode::InternalTexture && (!IsValid(InternalTexture))))
	{
		return InCombinedResult;
	}

	// Texture height patch doesn't affect weight maps
	if (!bIsHeightmap)
	{
		return InCombinedResult;
	}

	// Go ahead and pack everything into a copy of the param struct so we don't have to capture everything
	// individually in the lambda below.
	FApplyLandscapeTextureHeightPatchPS::FParameters ShaderParamsToCopy;
	FIntRect DestinationBounds;
	UpdateShaderParams(ShaderParamsToCopy, FIntPoint(InCombinedResult->SizeX, InCombinedResult->SizeY), DestinationBounds);

	if (DestinationBounds.IsEmpty())
	{
		// Patch must be outside the landscape.
		return InCombinedResult;
	}

	UTexture* PatchUObject = InternalTexture;
	if (SourceMode == ELandscapeTexturePatchSourceMode::TextureBackedRenderTarget)
	{
		PatchUObject = InternalRenderTarget;
	}
	else if (SourceMode == ELandscapeTexturePatchSourceMode::TextureAsset)
	{
		PatchUObject = TextureAsset;
	} 
	
	if (!PatchUObject)
	{
		return InCombinedResult;
	}
	FTextureResource* Patch = PatchUObject->GetResource();

	ENQUEUE_RENDER_COMMAND(LandscapeTextureHeightPatch)([InCombinedResult, ShaderParamsToCopy, Patch, DestinationBounds](FRHICommandListImmediate& RHICmdList)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeTextureHeightPatch_Render);

		FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("ApplyLandmassTextureHeightPatch"));
		
		TRefCountPtr<IPooledRenderTarget> DestinationRenderTarget = CreateRenderTarget(InCombinedResult->GetResource()->GetTexture2DRHI(), TEXT("LandscapeTextureHeightPatchOutput"));
		FRDGTextureRef DestinationTexture = GraphBuilder.RegisterExternalTexture(DestinationRenderTarget);

		// Make a copy of our heightmap input so we can read and write at the same time (needed for blending)
		FRDGTextureRef InputCopy = GraphBuilder.CreateTexture(DestinationTexture->Desc, TEXT("LandscapeTextureHeightPatchInputCopy"));

		FRHICopyTextureInfo CopyTextureInfo;
		CopyTextureInfo.NumMips = 1;
		CopyTextureInfo.Size = FIntVector(DestinationTexture->Desc.GetSize().X, DestinationTexture->Desc.GetSize().Y, 0);
		AddCopyTexturePass(GraphBuilder, DestinationTexture, InputCopy, CopyTextureInfo);

		FApplyLandscapeTextureHeightPatchPS::FParameters* ShaderParams =
			GraphBuilder.AllocParameters<FApplyLandscapeTextureHeightPatchPS::FParameters>();
		*ShaderParams = ShaderParamsToCopy;

		TRefCountPtr<IPooledRenderTarget> PatchRenderTarget = CreateRenderTarget(Patch->GetTexture2DRHI(), TEXT("LandscapeTextureHeightPatch"));
		FRDGTextureRef PatchTexture = GraphBuilder.RegisterExternalTexture(PatchRenderTarget);
		FRDGTextureSRVRef PatchSRV = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateForMipLevel(PatchTexture, 0));
		ShaderParams->InHeightPatch = PatchSRV;
		ShaderParams->InHeightPatchSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp>::GetRHI();

		FRDGTextureSRVRef InputCopySRV = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateForMipLevel(InputCopy, 0));
		ShaderParams->InSourceHeightmap = InputCopySRV;

		ShaderParams->RenderTargets[0] = FRenderTargetBinding(DestinationTexture, ERenderTargetLoadAction::ENoAction, /*InMipIndex = */0);

		FApplyLandscapeTextureHeightPatchPS::AddToRenderGraph(GraphBuilder, ShaderParams, DestinationBounds);

		GraphBuilder.Execute();
	});

	return InCombinedResult;
}
#endif // WITH_EDITOR

void ULandscapeTextureHeightPatch::DeleteInternalTextures()
{
	if (InternalRenderTarget || InternalTexture)
	{
		Modify();
	}

	InternalRenderTarget = nullptr;
	InternalTexture = nullptr;

	if (SourceMode != ELandscapeTexturePatchSourceMode::TextureAsset && PatchManager.IsValid())
	{
		PatchManager->RequestLandscapeUpdate();
	}
}

void ULandscapeTextureHeightPatch::Reinitialize()
{
#if WITH_EDITOR
	using namespace UE::Landscape;
	using namespace LandscapeTextureHeightPatchLocals;

	if (SourceMode == ELandscapeTexturePatchSourceMode::TextureAsset)
	{
		// Nothing to reinitialize...
		UE_LOG(LogLandscapePatch, Warning, TEXT("ULandscapeTextureHeightPatch::Reinitialize: Unable to reinitialize when source mode is set"
			"to be a texture asset."));
		return;
	}
	if ((InitializationMode == ELandscapeTextureHeightPatchInitMode::TextureAsset 
			&& (!IsValid(TextureAsset) 
				|| !ensureMsgf(TextureAsset->VirtualTextureStreaming == 0, TEXT("ULandscapeTextureHeightPatch: Virtual textures are not supported"))))
		|| (InitializationMode == ELandscapeTextureHeightPatchInitMode::FromLandscape && !Landscape.IsValid()))
	{
		// Don't have what we need for initialization
		return;
	}

	// Figure out what needs to be modified for undo/redo...
	if (SourceMode == ELandscapeTexturePatchSourceMode::TextureBackedRenderTarget)
	{
		if (!IsValid(InternalRenderTarget))
		{
			// A new render target is going to be created
			Modify();
		}
		else
		{
			InternalRenderTarget->Modify();
		}
	}
	else if (SourceMode == ELandscapeTexturePatchSourceMode::InternalTexture)
	{
		if (!IsValid(InternalTexture))
		{
			// A new texture is going to be created
			Modify();
		}
		else
		{
			InternalTexture->Modify();
		}
	}


	switch (InitializationMode)
	{
	case ELandscapeTextureHeightPatchInitMode::Blank:
		SourceEncoding = ELandscapeTextureHeightPatchEncoding::NativePackedHeight;
		if (SourceMode == ELandscapeTexturePatchSourceMode::TextureBackedRenderTarget)
		{
			ResizeRenderTargetIfNeeded(InitTextureSizeX, InitTextureSizeY);
		}
		else if (SourceMode == ELandscapeTexturePatchSourceMode::InternalTexture)
		{
			ResizeTextureIfNeeded(InitTextureSizeX, InitTextureSizeY, /*bClear*/ true, /*bUpdateResource*/ true);
		}
		break;
	case ELandscapeTextureHeightPatchInitMode::FromLandscape:
	{
		if (bBaseResolutionOffLandscape)
		{
			FVector2D DesiredResolution;
			GetInitResolutionFromLandscape(ResolutionMultiplier, DesiredResolution);
			InitTextureSizeX = DesiredResolution.X;
			InitTextureSizeY = DesiredResolution.Y;
		}

		// We're going to need our render target regardless of source mode because we'll write to it 
		// before copying to texture if needed.
		SourceEncoding = ELandscapeTextureHeightPatchEncoding::NativePackedHeight;
		ResizeRenderTargetIfNeeded(InitTextureSizeX, InitTextureSizeY);
			
		// If ZeroHeightMeaning is not landscape Z, then we're going to be applying an offset to our data when
		// applying it to landscape, which means we'll need to apply the inverse offset when initializing here
		// so that we get the same landscape back. In that case, we'll render the landscape to an intermediate
		// target first, then do the copy to the internal target while adding in the offset. Otherwise, we can
		// render directly to internal render target.
		double OffsetToApply = 0;
		if (ZeroHeightMeaning != ELandscapeTextureHeightPatchZeroHeightMeaning::LandscapeZ)
		{
			FTransform LandscapeHeightmapToWorld = PatchManager->GetHeightmapCoordsToWorld();
			double ZeroHeight = 0;
			if (ZeroHeightMeaning == ELandscapeTextureHeightPatchZeroHeightMeaning::PatchZ)
			{
				ZeroHeight = LandscapeHeightmapToWorld.InverseTransformPosition(GetComponentTransform().GetTranslation()).Z;
			}
			else if (ZeroHeightMeaning == ELandscapeTextureHeightPatchZeroHeightMeaning::WorldZero)
			{
				ZeroHeight = LandscapeHeightmapToWorld.InverseTransformPosition(FVector::ZeroVector).Z;
			}
			OffsetToApply = LandscapeDataAccess::MidValue - ZeroHeight;
		}

		UTextureRenderTarget2D* RenderedHeightmapSection = InternalRenderTarget;
		if (OffsetToApply != 0)
		{
			RenderedHeightmapSection = NewObject<UTextureRenderTarget2D>(this);
			RenderedHeightmapSection->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RG8;
			RenderedHeightmapSection->InitAutoFormat(InitTextureSizeX, InitTextureSizeY);
			RenderedHeightmapSection->UpdateResourceImmediate(true);
		}

		// Note that we need the "for resolution" overload here since our current texture may not yet have the correct resolution
		FVector2d FullPatchDimensions(GetFullUnscaledWorldSizeForResolution(FVector2D(InitTextureSizeX, InitTextureSizeY)));

		Landscape->RenderHeightmap(GetPatchToWorldTransform(), FBox2D(-FullPatchDimensions / 2, FullPatchDimensions / 2), RenderedHeightmapSection);

		bApplyComponentZScale = false;

		// Apply the inverse offset we determined earlier.
		if (OffsetToApply != 0)
		{
			ENQUEUE_RENDER_COMMAND(RenderHeightmap)([Input = RenderedHeightmapSection->GetResource(), Patch = InternalRenderTarget->GetResource(), OffsetToApply](FRHICommandListImmediate& RHICmdList)
			{
				FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("OffsetTextureHeightPatchInitialization"));

				TRefCountPtr<IPooledRenderTarget> InputRenderTarget = CreateRenderTarget(Input->GetTexture2DRHI(), TEXT("LandscapeTextureHeightPatchOffsetOutput"));
				FRDGTextureRef InputTexture = GraphBuilder.RegisterExternalTexture(InputRenderTarget);

				TRefCountPtr<IPooledRenderTarget> DestinationRenderTarget = CreateRenderTarget(Patch->GetTexture2DRHI(), TEXT("LandscapeTextureHeightPatchOffsetOutput"));
				FRDGTextureRef DestinationTexture = GraphBuilder.RegisterExternalTexture(DestinationRenderTarget);

				FOffsetHeightmapPS::FParameters* ShaderParams =
					GraphBuilder.AllocParameters<FOffsetHeightmapPS::FParameters>();

				FRDGTextureSRVRef InputSRV = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateForMipLevel(InputTexture, 0));
				ShaderParams->InHeightmap = InputSRV;
				ShaderParams->InHeightOffset = OffsetToApply;
				ShaderParams->RenderTargets[0] = FRenderTargetBinding(DestinationTexture, ERenderTargetLoadAction::ENoAction, /*InMipIndex = */0);

				FOffsetHeightmapPS::AddToRenderGraph(GraphBuilder, ShaderParams);

				GraphBuilder.Execute();
			});
		}

		if (SourceMode == ELandscapeTexturePatchSourceMode::InternalTexture)
		{
			ConvertInternalRenderTargetToNativeTexture(true);
			InternalRenderTarget = nullptr;
		}

		break;
	}
	case ELandscapeTextureHeightPatchInitMode::TextureAsset:
	{
		if (!IsValid(TextureAsset))
		{
			break;
		}

		if (!TextureAsset->GetResource())
		{
			TextureAsset->UpdateResource();
			FTextureCompilingManager::Get().FinishCompilation({ TextureAsset });
		}

		if (bBaseResolutionOffLandscape)
		{
			InitTextureSizeX = TextureAsset->GetResource()->GetSizeX();
			InitTextureSizeY = TextureAsset->GetResource()->GetSizeY();
		}

		// We're going to need our render target regardless of source mode because we'll write to it 
		// before copying to texture if needed.
		ResizeRenderTargetIfNeeded(InitTextureSizeX, InitTextureSizeY);

		ENQUEUE_RENDER_COMMAND(LandscapeTextureHeightPatchCopyInternalRT)(
			[Source = TextureAsset->GetResource(), Destination = InternalRenderTarget->GetResource()](FRHICommandListImmediate& RHICmdList)
		{
			CopyTextureOnRenderThread(RHICmdList, *Source, *Destination);
		});

		if (SourceMode == ELandscapeTexturePatchSourceMode::InternalTexture)
		{
			ConvertInternalRenderTargetToNativeTexture(true);
			InternalRenderTarget = nullptr;
		}
		break;
	}
	}//end switch on InitializationMode

	if (PatchManager.IsValid())
	{
		PatchManager->RequestLandscapeUpdate();
	}
#endif // WITH_EDITOR
}

#if WITH_EDITOR
bool ULandscapeTextureHeightPatch::ResizeRenderTargetIfNeeded(int32 SizeX, int32 SizeY)
{
	using namespace LandscapeTextureHeightPatchLocals;

	bool bChanged = false;

	ETextureRenderTargetFormat FormatToUse = SourceEncoding == ELandscapeTextureHeightPatchEncoding::NativePackedHeight ?
		ETextureRenderTargetFormat::RTF_RGBA8 : InternalRenderTargetFormat.GetValue();

	if (!IsValid(InternalRenderTarget))
	{
		Modify();

		InternalRenderTarget = NewObject<UTextureRenderTarget2D>(this);
		InternalRenderTarget->ClearColor = ClearColor;

		InternalRenderTarget->RenderTargetFormat = FormatToUse;
		InternalRenderTarget->InitAutoFormat(SizeX, SizeY);

		bChanged = true;
	}
	else if (InternalRenderTarget->SizeX != SizeX
		|| InternalRenderTarget->SizeY != SizeY
		|| InternalRenderTarget->RenderTargetFormat != FormatToUse)
	{
		InternalRenderTarget->Modify();
		InternalRenderTarget->RenderTargetFormat = FormatToUse;
		InternalRenderTarget->InitAutoFormat(SizeX, SizeY);

		bChanged = true;
	}

	InternalRenderTarget->UpdateResourceImmediate(true);

	return bChanged;
}

bool ULandscapeTextureHeightPatch::ResizeTextureIfNeeded(int32 SizeX, int32 SizeY, bool bClear, bool bUpdateResource)
{
	using namespace LandscapeTextureHeightPatchLocals;

	bool bChanged = false;

	if (!IsValid(InternalTexture) || !InternalTexture->GetResource())
	{
		Modify();
		bChanged = true;

		InternalTexture = NewObject<UTexture2D>(this);
		InternalTexture->SRGB = false;
		InternalTexture->MipGenSettings = TMGS_NoMipmaps;
		InternalTexture->AddressX = TA_Clamp;
		InternalTexture->AddressY = TA_Clamp;
		InternalTexture->LODGroup = TEXTUREGROUP_Terrain_Heightmap;
		// TODO: How do we allow lossless compression, instead of disallowing compression entirely? Just setting 
		// LossyCompressionAmount to ETextureLossyCompressionAmount::TLCA_None is not sufficient.
		InternalTexture->CompressionNone = true;
	}
	else if (bClear
		|| InternalTexture->GetResource()->GetSizeX() != SizeX
		|| InternalTexture->GetResource()->GetSizeY() != SizeY)
	{
		bChanged = true;
		InternalTexture->Modify();
	}

	if (!InternalTexture->GetResource()
		|| InternalTexture->GetResource()->GetSizeX() != SizeX
		|| InternalTexture->GetResource()->GetSizeY() != SizeY)
	{
		InternalTexture->Source.Init(SizeX, SizeY, 1, 1, ETextureSourceFormat::TSF_BGRA8);
	}
	if (bClear)
	{
		TArray<FColor> SourceColors;
		SourceColors.Init(LandscapeDataAccess::PackHeight(LandscapeDataAccess::MidValue), SizeX * SizeY);
		uint8* SourceData = InternalTexture->Source.LockMip(0);
		FMemory::Memcpy(SourceData, SourceColors.GetData(), sizeof(FColor) * SourceColors.Num());
		InternalTexture->Source.UnlockMip(0);
	}

	if (bChanged)
	{
		if (bUpdateResource)
		{
			InternalTexture->UpdateResource();
		}

		// TODO: Do we need these?
		InternalTexture->PostEditChange();
		InternalTexture->MarkPackageDirty();
	}

	return bChanged;
}

bool ULandscapeTextureHeightPatch::SetSourceMode(ELandscapeTexturePatchSourceMode NewMode, bool bInitializeIfRenderTarget)
{
	SourceMode = NewMode;

	if (PreviousSourceMode == NewMode)
	{
		return true;
	}

	if (PreviousSourceMode == ELandscapeTexturePatchSourceMode::TextureBackedRenderTarget
		&& NewMode == ELandscapeTexturePatchSourceMode::InternalTexture)
	{
		ConvertInternalRenderTargetToNativeTexture(false);
	}
	else if (PreviousSourceMode == ELandscapeTexturePatchSourceMode::InternalTexture
		&& NewMode == ELandscapeTexturePatchSourceMode::TextureBackedRenderTarget)
	{
		ConvertInternalRenderTargetBackFromNativeTexture();
	}

	if (NewMode == ELandscapeTexturePatchSourceMode::TextureBackedRenderTarget 
		&& !InternalRenderTarget && bInitializeIfRenderTarget)
	{
		FVector2D DesiredResolution;
		if (GetInitResolutionFromLandscape(ResolutionMultiplier, DesiredResolution))
		{
			ResizeRenderTargetIfNeeded(DesiredResolution.X, DesiredResolution.Y);
		}
		else
		{
			ResizeRenderTargetIfNeeded(InitTextureSizeX, InitTextureSizeY);
		}
		
	}

	// Discard any unused internal textures
	if (NewMode != ELandscapeTexturePatchSourceMode::TextureBackedRenderTarget)
	{
		InternalRenderTarget = nullptr;
	}
	if (NewMode == ELandscapeTexturePatchSourceMode::TextureAsset)
	{
		InternalTexture = nullptr;
	}

	PreviousSourceMode = NewMode;

	return true;
}
#endif // WITH_EDITOR

void ULandscapeTextureHeightPatch::SnapToLandscape()
{
	Modify();

	if (!Landscape.IsValid())
	{
		return;
	}

	FTransform LandscapeTransform = Landscape->GetTransform();
	FTransform PatchTransform = GetComponentTransform();

	FQuat LandscapeRotation = LandscapeTransform.GetRotation();
	FQuat PatchRotation = PatchTransform.GetRotation();

	// Get rotation of patch relative to landscape
	FQuat PatchRotationRelativeLandscape = LandscapeRotation.Inverse()* PatchRotation;

	// Get component of that relative rotation that is around the landscape Z axis.
	double RadiansAroundZ = PatchRotationRelativeLandscape.GetTwistAngle(FVector::ZAxisVector);

	// Round that rotation to nearest 90 degree increment
	int32 Num90DegreeRotations = FMath::RoundToDouble(RadiansAroundZ / FMathd::HalfPi);
	double NewRadiansAroundZ = Num90DegreeRotations * FMathd::HalfPi;

	// Now adjust the patch transform.
	FQuat NewPatchRotation = FQuat(FVector::ZAxisVector, NewRadiansAroundZ) * LandscapeRotation;
	SetWorldRotation(NewPatchRotation);

	// Once we have the rotation adjusted, we need to adjust the patch size and positioning.
	// However don't bother if either the patch or landscape scale is 0. We might still be able
	// to align in one of the axes in such a case, but it is not worth the code complexity for
	// a broken use case.
	FVector LandscapeScale = Landscape->GetTransform().GetScale3D();
	FVector PatchScale = GetComponentTransform().GetScale3D();
	if (LandscapeScale.X == 0 || LandscapeScale.Y == 0)
	{
		UE_LOG(LogLandscapePatch, Warning, TEXT("ULandscapeTextureHeightPatch::SnapToLandscape: Landscape target "
			"for height patch had a zero scale in one of the dimensions. Skipping aligning position."));
		return;
	}
	if (PatchScale.X == 0 || PatchScale.Y == 0)
	{
		UE_LOG(LogLandscapePatch, Warning, TEXT("ULandscapeTextureHeightPatch::SnapToLandscape: Height patch "
			"had a zero scale in one of the dimensions. Skipping aligning position."));
		return;
	}

	// Start by adjusting size to be a multiple of landscape quad size.
	double PatchExtentX = PatchScale.X * UnscaledPatchCoverage.X;
	double PatchExtentY = PatchScale.Y * UnscaledPatchCoverage.Y;
	if (Num90DegreeRotations % 2)
	{
		// Relative to the landscape, our lenght and width are backwards...
		Swap(PatchExtentX, PatchExtentY);
	}

	int32 LandscapeQuadsX = FMath::RoundToInt(PatchExtentX / LandscapeScale.X);
	int32 LandscapeQuadsY = FMath::RoundToInt(PatchExtentY / LandscapeScale.Y);

	double NewPatchExtentX = LandscapeQuadsX * LandscapeScale.X;
	double NewPatchExtentY = LandscapeQuadsY * LandscapeScale.Y;
	if (Num90DegreeRotations % 2)
	{
		Swap(NewPatchExtentX, NewPatchExtentY);
	}
	UnscaledPatchCoverage = FVector2D(NewPatchExtentX / PatchScale.X, NewPatchExtentY / PatchScale.Y);

	// Now adjust the center of the patch. This gets snapped to either integer or integer + 0.5 increments
	// in landscape coordinates depending on whether patch length/width is odd or even in landscape coordinates.

	FVector PatchCenterInLandscapeCoordinates = LandscapeTransform.InverseTransformPosition(GetComponentLocation());
	double NewPatchCenterX, NewPatchCenterY;
	if (LandscapeQuadsX % 2)
	{
		NewPatchCenterX = FMath::RoundToDouble(PatchCenterInLandscapeCoordinates.X + 0.5) - 0.5;
	}
	else
	{
		NewPatchCenterX = FMath::RoundToDouble(PatchCenterInLandscapeCoordinates.X);
	}
	if (LandscapeQuadsY % 2)
	{
		NewPatchCenterY = FMath::RoundToDouble(PatchCenterInLandscapeCoordinates.Y + 0.5) - 0.5;
	}
	else
	{
		NewPatchCenterY = FMath::RoundToDouble(PatchCenterInLandscapeCoordinates.Y);
	}

	FVector NewCenterInLandscape(NewPatchCenterX, NewPatchCenterY, PatchCenterInLandscapeCoordinates.Z);
	SetWorldLocation(LandscapeTransform.TransformPosition(NewCenterInLandscape));
}

#if WITH_EDITOR
bool ULandscapeTextureHeightPatch::SetTextureResolution(FVector2D ResolutionIn)
{
	if (SourceMode == ELandscapeTexturePatchSourceMode::TextureAsset)
	{
		UE_LOG(LogLandscapePatch, Warning, TEXT("ULandscapeTextureHeightPatch::ResizeIfNeeded: Cannot resize when source mode is set to texture asset."));
		return false;
	}

	if (ResolutionIn.X <= 0 || ResolutionIn.Y <= 0)
	{
		UE_LOG(LogLandscapePatch, Warning, TEXT("ULandscapeTextureHeightPatch::ResizeIfNeeded: Inputs must be positive."));
		return false;
	}

	if (SourceMode == ELandscapeTexturePatchSourceMode::TextureBackedRenderTarget)
	{
		return ResizeRenderTargetIfNeeded(ResolutionIn.X, ResolutionIn.Y);
	}
	else //if (SourceMode == ELandscapeTexturePatchSourceMode::InternalTexture)
	{
		return ResizeTextureIfNeeded(ResolutionIn.X, ResolutionIn.Y, false, true);
	}
	return false;
}
#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE