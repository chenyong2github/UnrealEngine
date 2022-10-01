// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/AST.h"


namespace mu
{

	class ASTOpImageMipmap : public ASTOp
	{
	public:

		ASTChild Source;

		uint8_t Levels = 0;

		//! Number of mipmaps that can be generated for a single layout block.
		uint8_t BlockLevels = 0;

		//! This is true if this operation is supposed to build only the tail mipmaps.
		//! It is used during the code optimisation phase, and to validate the code.
		bool bOnlyTail = false;

		//! Mipmap generation settings. 
		float SharpenFactor = 0.0f;
		EAddressMode AddressMode = EAddressMode::AM_NONE;
		EMipmapFilterType FilterType = EMipmapFilterType::MFT_Unfiltered;
		bool DitherMipmapAlpha = false;

	public:

		ASTOpImageMipmap();
		ASTOpImageMipmap(const ASTOpImageMipmap&) = delete;
		~ASTOpImageMipmap();

		OP_TYPE GetOpType() const override { return OP_TYPE::IM_MIPMAP; }
		uint64 Hash() const override;
		bool IsEqual(const ASTOp& otherUntyped) const override;
		Ptr<ASTOp> Clone(MapChildFunc& mapChild) const override;
		void ForEachChild(const std::function<void(ASTChild&)>&) override;
		void Link(PROGRAM& program, const FLinkerOptions* Options) override;
		Ptr<ASTOp> OptimiseSink(const MODEL_OPTIMIZATION_OPTIONS& options, OPTIMIZE_SINK_CONTEXT& context) const override;
		FImageDesc GetImageDesc(bool returnBestOption, GetImageDescContext* context) override;
		void GetLayoutBlockSize(int* pBlockX, int* pBlockY) override;
		bool IsImagePlainConstant(vec4<float>& colour) const override;
		Ptr<ImageSizeExpression> GetImageSizeExpression() const override;

	};

}

