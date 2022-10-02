// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "MuR/MemoryPrivate.h"
#include "MuR/Operations.h"
#include "MuR/Ptr.h"
#include "MuT/AST.h"

#include <functional>


namespace mu
{
struct PROGRAM;


	//---------------------------------------------------------------------------------------------
	//! Operations to add elements to a LOD
	//---------------------------------------------------------------------------------------------
	class ASTOpAddLOD : public ASTOp
	{
	public:

		vector< ASTChild > lods;

	public:

		ASTOpAddLOD();
		ASTOpAddLOD(const ASTOpAddLOD&) = delete;
		~ASTOpAddLOD();

		OP_TYPE GetOpType() const override { return OP_TYPE::IN_ADDLOD; }
		uint64 Hash() const override;
		void ForEachChild(const std::function<void(ASTChild&)>&) override;
		bool IsEqual(const ASTOp& otherUntyped) const override;
		Ptr<ASTOp> Clone(MapChildFunc& mapChild) const override;
		void Link(PROGRAM& program, const FLinkerOptions* Options) override;
	};



}

