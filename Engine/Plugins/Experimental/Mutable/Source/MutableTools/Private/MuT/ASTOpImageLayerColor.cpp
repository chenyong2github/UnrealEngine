// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuT/ASTOpImageLayerColor.h"

#include "MuT/ASTOpSwitch.h"
#include "Containers/Map.h"
#include "HAL/PlatformMath.h"
#include "MuR/MemoryPrivate.h"
#include "MuR/ModelPrivate.h"
#include "MuR/RefCounted.h"
#include "MuR/Types.h"
#include "MuT/StreamsPrivate.h"

#include <memory>
#include <utility>


namespace mu
{


	ASTOpImageLayerColor::ASTOpImageLayerColor()
		: base(this)
		, color(this)
		, mask(this)
	{
	}


	//-------------------------------------------------------------------------------------------------
	ASTOpImageLayerColor::~ASTOpImageLayerColor()
	{
		// Explicit call needed to avoid recursive destruction
		ASTOp::RemoveChildren();
	}


	//-------------------------------------------------------------------------------------------------
	bool ASTOpImageLayerColor::IsEqual(const ASTOp& InOtherUntyped) const
	{
		if (const ASTOpImageLayerColor* Other = dynamic_cast<const ASTOpImageLayerColor*>(&InOtherUntyped))
		{
			return base == Other->base &&
				color == Other->color &&
				mask == Other->mask &&
				blendType == Other->blendType &&
				blendTypeAlpha == Other->blendTypeAlpha;
		}
		return false;
	}


	//-------------------------------------------------------------------------------------------------
	uint64 ASTOpImageLayerColor::Hash() const
	{
		uint64 res = std::hash<OP_TYPE>()(GetOpType());
		hash_combine(res, base.child().get());
		hash_combine(res, color.child().get());
		hash_combine(res, mask.child().get());
		return res;
	}


	//-------------------------------------------------------------------------------------------------
	mu::Ptr<ASTOp> ASTOpImageLayerColor::Clone(MapChildFuncRef mapChild) const
	{
		Ptr<ASTOpImageLayerColor> n = new ASTOpImageLayerColor();
		n->base = mapChild(base.child());
		n->color = mapChild(color.child());
		n->mask = mapChild(mask.child());
		n->blendType = blendType;
		n->blendTypeAlpha = blendTypeAlpha;
		return n;
	}


	//-------------------------------------------------------------------------------------------------
	void ASTOpImageLayerColor::ForEachChild(const TFunctionRef<void(ASTChild&)> f)
	{
		f(base);
		f(color);
		f(mask);
	}


	//-------------------------------------------------------------------------------------------------
	void ASTOpImageLayerColor::Link(FProgram& program, const FLinkerOptions*)
	{
		// Already linked?
		if (!linkedAddress)
		{
			OP::ImageLayerColourArgs args;
			FMemory::Memzero(&args, sizeof(args));

			args.blendType = (uint8)blendType;
			args.blendTypeAlpha = (uint8)blendTypeAlpha;

			if (base) args.base = base->linkedAddress;
			if (color) args.colour = color->linkedAddress;
			if (mask) args.mask = mask->linkedAddress;

			linkedAddress = (OP::ADDRESS)program.m_opAddress.Num();
			program.m_opAddress.Add((uint32)program.m_byteCode.Num());
			AppendCode(program.m_byteCode, GetOpType());
			AppendCode(program.m_byteCode, args);
		}
	}


	//-------------------------------------------------------------------------------------------------
	FImageDesc ASTOpImageLayerColor::GetImageDesc(bool returnBestOption, FGetImageDescContext* context) const 
	{
		FImageDesc res;

		// Local context in case it is necessary
		FGetImageDescContext localContext;
		if (!context)
		{
			context = &localContext;
		}
		else
		{
			// Cached result?
			FImageDesc* PtrValue = context->m_results.Find(this);
			if (PtrValue)
			{
				return *PtrValue;
			}
		}

		// Actual work
		if (base)
		{
			res = base->GetImageDesc(returnBestOption, context);
		}


		// Cache the result
		if (context)
		{
			context->m_results.Add(this, res);
		}

		return res;
	}


	//-------------------------------------------------------------------------------------------------
	void ASTOpImageLayerColor::GetLayoutBlockSize(int* pBlockX, int* pBlockY)
	{
		if (base)
		{
			base->GetLayoutBlockSize(pBlockX, pBlockY);
		}
	}


	//-------------------------------------------------------------------------------------------------
	mu::Ptr<ImageSizeExpression> ASTOpImageLayerColor::GetImageSizeExpression() const
	{
		if (base)
		{
			return base->GetImageSizeExpression();
		}

		return nullptr;
	}


	//-------------------------------------------------------------------------------------------------
	Ptr<ASTOp> ASTOpImageLayerColor::OptimiseSink(const FModelOptimizationOptions& options, FOptimizeSinkContext& context) const
	{
		Ptr<ASTOp> at;

		// Layer effects may be worth sinking down switches and conditionals, to be able
		// to apply extra optimisations
		auto baseAt = base.child();
		auto maskAt = mask.child();

		// Promote conditions from the base
		OP_TYPE baseType = baseAt->GetOpType();
		switch (baseType)
		{
			// Seems to cause operation explosion in optimizer in bandit model.
			// moved to generic sink in the default.
//            case OP_TYPE::IM_CONDITIONAL:
//            {
//                m_modified = true;

//                OP op = program.m_code[baseAt];

//                OP aOp = program.m_code[at];
//                aOp.args.ImageLayerColour.base = program.m_code[baseAt].args.Conditional.yes;
//                op.args.Conditional.yes = program.AddOp( aOp );

//                OP bOp = program.m_code[at];
//                bOp.args.ImageLayerColour.base = program.m_code[baseAt].args.Conditional.no;
//                op.args.Conditional.no = program.AddOp( bOp );

//                at = program.AddOp( op );
//                break;
//            }

		case OP_TYPE::IM_SWITCH:
		{
			// Warning:
			// It seems to cause data explosion in optimizer in some models. Because
			// all switch branches become unique constants

			// See if the blended has an identical switch, to optimise it too
			const ASTOpSwitch* baseSwitch = dynamic_cast<const ASTOpSwitch*>(baseAt.get());

			// Mask not supported yet
			if (maskAt)
			{
				break;
			}

			// Move the layer operation down base paths
			Ptr<ASTOpSwitch> nop = mu::Clone<ASTOpSwitch>(baseSwitch);

			if (nop->def)
			{
				Ptr<ASTOpImageLayerColor> defOp = mu::Clone<ASTOpImageLayerColor>(this);
				defOp->base = nop->def.child();
				nop->def = defOp;
			}

			for (int32 v = 0; v < nop->cases.Num(); ++v)
			{
				if (nop->cases[v].branch)
				{
					Ptr<ASTOpImageLayerColor> bOp = mu::Clone<ASTOpImageLayerColor>(this);
					bOp->base = nop->cases[v].branch.child();
					nop->cases[v].branch = bOp;
				}
			}

			at = nop;
			break;
		}

		case OP_TYPE::IM_DISPLACE:
		{
			// Mask not supported yet. If there is a mask it wouldn't be correct to sink
			// unless the mask was a similar displace.
			if (maskAt)
			{
				break;
			}

			Ptr<ASTOpFixed> NewDisplace = mu::Clone<ASTOpFixed>(baseAt);

			Ptr<ASTOp> sourceOp = NewDisplace->children[NewDisplace->op.args.ImageDisplace.source].child();
			Ptr<ASTOpImageLayerColor> NewSource = mu::Clone<ASTOpImageLayerColor>(this);
			NewSource->base = sourceOp;
			NewDisplace->SetChild(NewDisplace->op.args.ImageDisplace.source, NewSource);

			at = NewDisplace;
			break;
		}

		case OP_TYPE::IM_RASTERMESH:
		{
			// Mask not supported yet. If there is a mask it wouldn't be correct to sink.				
			if (maskAt)
			{
				break;
			}

			Ptr<ASTOpFixed> NewRaster = mu::Clone<ASTOpFixed>(baseAt);

			Ptr<ASTOp> sourceOp = NewRaster->children[NewRaster->op.args.ImageRasterMesh.image].child();
			Ptr<ASTOpImageLayerColor> NewSource = mu::Clone<ASTOpImageLayerColor>(this);
			NewSource->base = sourceOp;
			NewRaster->SetChild(NewRaster->op.args.ImageRasterMesh.image, NewSource);

			at = NewRaster;
			break;
		}


		default:
			break;

		}

		return at;
	}

}
