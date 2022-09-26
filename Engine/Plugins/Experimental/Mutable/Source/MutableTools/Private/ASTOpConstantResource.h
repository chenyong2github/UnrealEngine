// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AST.h"


namespace mu
{


	//---------------------------------------------------------------------------------------------
	//! A constant mesh, image, volume or layout
	//---------------------------------------------------------------------------------------------
	class ASTOpConstantResource : public ASTOp
	{
	private:

		//!
		//std::FILE* resourceFile = nullptr;
		//size_t fileSize = 0;

		//!
		Ptr<const RefCounted> loadedValue;
		Ptr<RefCounted> proxy;

		//! Value hash
		uint64 hash;

		//! We tried to link already but the result is a null op.
		bool bLinkedAndNull = false;

	public:

		//! Type of switch
		OP_TYPE type;

	public:

		~ASTOpConstantResource() override;

		// Own interface

		//! Get a hash of the stored value.
		uint64 GetValueHash() const;

		//! Get a copy of the stored value
		Ptr<const RefCounted> GetValue() const;

		//! Set the value to store in this op
		void SetValue(const Ptr<const RefCounted>& v,
			bool useDiskCache);


		// ASTOp interface
		OP_TYPE GetOpType() const override { return type; }
		void ForEachChild(const std::function<void(ASTChild&)>&) override;
		bool IsEqual(const ASTOp& otherUntyped) const override;
		Ptr<ASTOp> Clone(MapChildFunc& mapChild) const override;
		uint64 Hash() const override;
		void Link(PROGRAM& program, const FLinkerOptions*) override;
		FImageDesc GetImageDesc(bool, class GetImageDescContext*) override;
		void GetBlockLayoutSize(int blockIndex, int* pBlockX, int* pBlockY,
			BLOCK_LAYOUT_SIZE_CACHE* cache) override;
		void GetLayoutBlockSize(int* pBlockX, int* pBlockY) override;
		bool GetNonBlackRect(FImageRect& maskUsage) const override;
		bool IsImagePlainConstant(vec4<float>& colour) const override;
		Ptr<ImageSizeExpression> GetImageSizeExpression() const override;
	};


}

