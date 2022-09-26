// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MutableTools/Private/AST.h"


namespace mu
{

	class ASTOpImagePixelFormat : public ASTOp
	{
	public:

		ASTChild Source;
		EImageFormat Format = EImageFormat::IF_NONE;
		EImageFormat FormatIfAlpha = EImageFormat::IF_NONE;

	public:

		ASTOpImagePixelFormat();
		ASTOpImagePixelFormat(const ASTOpImagePixelFormat&) = delete;
		~ASTOpImagePixelFormat();

		OP_TYPE GetOpType() const override { return OP_TYPE::IM_PIXELFORMAT; }
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

