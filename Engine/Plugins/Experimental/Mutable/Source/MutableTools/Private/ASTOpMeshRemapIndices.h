// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AST.h"


namespace mu
{


	//---------------------------------------------------------------------------------------------
	//! 
	//---------------------------------------------------------------------------------------------
	class ASTOpMeshRemapIndices : public ASTOp
	{
	public:

		//! Mesh that will have the vertex indices remapped.
		ASTChild source;

		//! Mesh used to obtain the final vertex indices.
		ASTChild reference;

	public:

		ASTOpMeshRemapIndices();
		ASTOpMeshRemapIndices(const ASTOpMeshRemapIndices&) = delete;
		~ASTOpMeshRemapIndices() override;

		// ASTOp interface
		OP_TYPE GetOpType() const override { return OP_TYPE::ME_REMAPINDICES; }
		uint64 Hash() const override;
		void ForEachChild(const std::function<void(ASTChild&)>&) override;
		bool IsEqual(const ASTOp& otherUntyped) const override;
		Ptr<ASTOp> Clone(MapChildFunc& mapChild) const override;
		void Link(PROGRAM& program, const FLinkerOptions*) override;
	};


}

