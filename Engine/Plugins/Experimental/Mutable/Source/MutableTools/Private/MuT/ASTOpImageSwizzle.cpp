// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuT/ASTOpImageSwizzle.h"

#include "Containers/Map.h"
#include "HAL/UnrealMemory.h"
#include "Math/UnrealMathSSE.h"
#include "Misc/AssertionMacros.h"
#include "MuR/ImagePrivate.h"
#include "MuR/MemoryPrivate.h"
#include "MuR/ModelPrivate.h"
#include "MuR/MutableMath.h"
#include "MuR/RefCounted.h"
#include "MuR/Types.h"
#include "MuT/ASTOpConditional.h"
#include "MuT/ASTOpImageCompose.h"
#include "MuT/ASTOpImagePatch.h"
#include "MuT/ASTOpImagePixelFormat.h"
#include "MuT/ASTOpImageMultiLayer.h"
#include "MuT/ASTOpSwitch.h"
#include "MuT/StreamsPrivate.h"

#include <memory>
#include <utility>


namespace mu
{

	ASTOpImageSwizzle::ASTOpImageSwizzle()
		: Sources{ ASTChild(this),ASTChild(this),ASTChild(this),ASTChild(this) }
	{
	}


	ASTOpImageSwizzle::~ASTOpImageSwizzle()
	{
		// Explicit call needed to avoid recursive destruction
		ASTOp::RemoveChildren();
	}


	bool ASTOpImageSwizzle::IsEqual(const ASTOp& otherUntyped) const
	{
		if (const ASTOpImageSwizzle* Other = dynamic_cast<const ASTOpImageSwizzle*>(&otherUntyped))
		{
			for (int32 i = 0; i<MUTABLE_OP_MAX_SWIZZLE_CHANNELS; ++i)
			{
				if (!(Sources[i] == Other->Sources[i] && SourceChannels[i] == Other->SourceChannels[i]))
				{
					return false;
				}
			}
			return Format == Other->Format;
		}
		return false;
	}


	uint64 ASTOpImageSwizzle::Hash() const
	{
		uint64 res = std::hash<void*>()(Sources[0].child().get());
		hash_combine(res, std::hash<void*>()(Sources[1].child().get()));
		hash_combine(res, std::hash<void*>()(Sources[2].child().get()));
		hash_combine(res, std::hash<void*>()(Sources[3].child().get()));
		hash_combine(res, std::hash<uint8>()(SourceChannels[0]));
		hash_combine(res, std::hash<uint8>()(SourceChannels[1]));
		hash_combine(res, std::hash<uint8>()(SourceChannels[2]));
		hash_combine(res, std::hash<uint8>()(SourceChannels[3]));
		hash_combine(res, Format);
		return res;
	}


	mu::Ptr<ASTOp> ASTOpImageSwizzle::Clone(MapChildFuncRef mapChild) const
	{
		mu::Ptr<ASTOpImageSwizzle> n = new ASTOpImageSwizzle();
		for (int32 i = 0; i < MUTABLE_OP_MAX_SWIZZLE_CHANNELS; ++i)
		{
			n->Sources[i] = mapChild(Sources[i].child());
			n->SourceChannels[i] = SourceChannels[i];
		}
		n->Format = Format;
		return n;
	}


	void ASTOpImageSwizzle::ForEachChild(const TFunctionRef<void(ASTChild&)> f)
	{
		for (int32 i = 0; i < MUTABLE_OP_MAX_SWIZZLE_CHANNELS; ++i)
		{
			f(Sources[i]);
		}
	}


	void ASTOpImageSwizzle::Link(FProgram& program, const FLinkerOptions*)
	{
		// Already linked?
		if (!linkedAddress)
		{
			OP::ImageSwizzleArgs args;
			FMemory::Memzero(&args, sizeof(args));

			args.format = Format;
			for (int32 i = 0; i < MUTABLE_OP_MAX_SWIZZLE_CHANNELS; ++i)
			{
				if (Sources[i]) args.sources[i] = Sources[i]->linkedAddress;
				args.sourceChannels[i] = SourceChannels[i];
			}			

			linkedAddress = (OP::ADDRESS)program.m_opAddress.Num();
			program.m_opAddress.Add((uint32_t)program.m_byteCode.Num());
			AppendCode(program.m_byteCode, GetOpType());
			AppendCode(program.m_byteCode, args);
		}
	}


	mu::Ptr<ASTOp> ASTOpImageSwizzle::OptimiseSemantic(const FModelOptimizationOptions&) const
	{
		// If children channels are also swizzle ops, recurse them
		Ptr<ASTOpImageSwizzle> sat;

		for (int c = 0; c < MUTABLE_OP_MAX_SWIZZLE_CHANNELS; ++c)
		{
			Ptr<ASTOp> candidate = Sources[c].child();
			if (!candidate)
			{
				continue;
			}

			switch (candidate->GetOpType())
			{
			// Swizzle
			case OP_TYPE::IM_SWIZZLE:
			{
				if (!sat)
				{
					sat = mu::Clone<ASTOpImageSwizzle>(this);
				}
				const ASTOpImageSwizzle* typedCandidate = dynamic_cast<const ASTOpImageSwizzle*>(candidate.get());
				int candidateChannel = SourceChannels[c];

				sat->Sources[c] = typedCandidate->Sources[candidateChannel].child();
				sat->SourceChannels[c] = typedCandidate->SourceChannels[candidateChannel];

				break;
			}

			// Format
			case OP_TYPE::IM_PIXELFORMAT:
			{
				// We can remove the format if its source is already an uncompressed format
				ASTOpImagePixelFormat* typedCandidate = dynamic_cast<ASTOpImagePixelFormat*>(candidate.get());
				Ptr<ASTOp> formatSource = typedCandidate->Source.child();

				if (formatSource)
				{
					FImageDesc desc = formatSource->GetImageDesc();
					if (desc.m_format != EImageFormat::IF_NONE && !IsCompressedFormat(desc.m_format))
					{
						if (!sat)
						{
							sat = mu::Clone<ASTOpImageSwizzle>(this);
						}
						sat->Sources[c] = formatSource;
					}
				}

				break;
			}

			default:
				break;
			}

		}

		return sat;
	}

	namespace
	{
		//---------------------------------------------------------------------------------------------
		//! Set al the non-null sources of an image swizzle operation to the given value
		//---------------------------------------------------------------------------------------------
		void ReplaceAllSources(Ptr<ASTOpImageSwizzle>& op, Ptr<ASTOp>& value)
		{
			check(op->GetOpType() == OP_TYPE::IM_SWIZZLE);
			for (int c = 0; c < MUTABLE_OP_MAX_SWIZZLE_CHANNELS; ++c)
			{
				if (op->Sources[c])
				{
					op->Sources[c] = value;
				}
			}
		}
	}

	mu::Ptr<ASTOp> ASTOpImageSwizzle::OptimiseSink(const FModelOptimizationOptions& options, FOptimizeSinkContext& context) const
	{
		MUTABLE_CPUPROFILER_SCOPE(OptimiseSwizzleAST);

		//! Basic optimisation first
		Ptr<ASTOp> at = OptimiseSemantic(options);
		if (at)
		{
			return at;
		}

		// If all sources are the same, we can sink the instruction
		bool bAllChannelsAreTheSame = true;
		bool bAllChannelsAreTheSameType = true;
		Ptr<ASTOp> channelSourceAt;
		for (int c = 0; c < MUTABLE_OP_MAX_SWIZZLE_CHANNELS; ++c)
		{
			Ptr<ASTOp> candidate = Sources[c].child();
			if (candidate)
			{
				if (!channelSourceAt)
				{
					channelSourceAt = candidate;
				}
				else
				{
					bAllChannelsAreTheSame = bAllChannelsAreTheSame && (channelSourceAt == candidate);
					bAllChannelsAreTheSameType = bAllChannelsAreTheSameType && (channelSourceAt->GetOpType() == candidate->GetOpType());
				}
			}
		}

		OP_TYPE sourceType = channelSourceAt->GetOpType();

		if (bAllChannelsAreTheSame && channelSourceAt)
		{
			// The instruction can be sunk
			switch (sourceType)
			{

			case OP_TYPE::IM_PLAINCOLOUR:
			{
				auto NewPlain = mu::Clone<ASTOpFixed>(channelSourceAt);
				Ptr<ASTOpFixed> NewSwizzle = new ASTOpFixed;
				NewSwizzle->op.type = OP_TYPE::CO_SWIZZLE;
				for (int i = 0; i < MUTABLE_OP_MAX_SWIZZLE_CHANNELS; ++i)
				{
					NewSwizzle->SetChild(NewSwizzle->op.args.ColourSwizzle.sources[i], NewPlain->children[NewPlain->op.args.ImagePlainColour.colour]);
					NewSwizzle->op.args.ColourSwizzle.sourceChannels[i] = SourceChannels[i];
				}
				NewPlain->SetChild(NewPlain->op.args.ImagePlainColour.colour, NewSwizzle);
				NewPlain->op.args.ImagePlainColour.format = Format;
				at = NewPlain;
				break;
			}

			case OP_TYPE::IM_SWITCH:
			{
				// Move the swizzle down all the paths
				Ptr<ASTOpSwitch> nop = mu::Clone<ASTOpSwitch>(channelSourceAt);

				if (nop->def)
				{
					Ptr<ASTOpImageSwizzle> defOp = mu::Clone<ASTOpImageSwizzle>(this);
					ReplaceAllSources(defOp, nop->def.child());
					nop->def = defOp;
				}

				for (int32 v = 0; v < nop->cases.Num(); ++v)
				{
					if (nop->cases[v].branch)
					{
						Ptr<ASTOpImageSwizzle> bOp = mu::Clone<ASTOpImageSwizzle>(this);
						ReplaceAllSources(bOp, nop->cases[v].branch.child());
						nop->cases[v].branch = bOp;
					}
				}

				at = nop;
				break;
			}

			case OP_TYPE::IM_CONDITIONAL:
			{
				// We move the swizzle down the two paths
				Ptr<ASTOpConditional> nop = mu::Clone<ASTOpConditional>(channelSourceAt);

				Ptr<ASTOpImageSwizzle> aOp = mu::Clone<ASTOpImageSwizzle>(this);
				ReplaceAllSources(aOp, nop->yes.child());
				nop->yes = aOp;

				Ptr<ASTOpImageSwizzle> bOp = mu::Clone<ASTOpImageSwizzle>(this);
				ReplaceAllSources(bOp, nop->no.child());
				nop->no = bOp;

				at = nop;
				break;
			}

			case OP_TYPE::IM_LAYER:
			{
				// We move the swizzle down the two paths
				Ptr<ASTOpFixed> nop = mu::Clone<ASTOpFixed>(channelSourceAt);

				Ptr<ASTOpImageSwizzle> aOp = mu::Clone<ASTOpImageSwizzle>(this);
				ReplaceAllSources(aOp, nop->children[nop->op.args.ImageLayer.base].child());
				nop->SetChild(nop->op.args.ImageLayer.base, aOp);

				Ptr<ASTOpImageSwizzle> bOp = mu::Clone<ASTOpImageSwizzle>(this);
				ReplaceAllSources(bOp, nop->children[nop->op.args.ImageLayer.blended].child());
				nop->SetChild(nop->op.args.ImageLayer.blended, bOp);

				at = nop;
				break;
			}

			case OP_TYPE::IM_LAYERCOLOUR:
			{
				// We move the swizzle down the base path
				Ptr<ASTOpFixed> nop = mu::Clone<ASTOpFixed>(channelSourceAt);

				Ptr<ASTOpImageSwizzle> NewSwizzle = mu::Clone<ASTOpImageSwizzle>(this);
				ReplaceAllSources(NewSwizzle, nop->children[nop->op.args.ImageLayerColour.base].child());
				nop->SetChild(nop->op.args.ImageLayerColour.base, NewSwizzle);

				// We need to swizzle the colour too
				Ptr<ASTOpFixed> cOp = new ASTOpFixed;
				cOp->op.type = OP_TYPE::CO_SWIZZLE;
				for (int i = 0; i < MUTABLE_OP_MAX_SWIZZLE_CHANNELS; ++i)
				{
					cOp->SetChild(cOp->op.args.ColourSwizzle.sources[i], nop->children[nop->op.args.ImageLayerColour.colour]);
					cOp->op.args.ColourSwizzle.sourceChannels[i] = SourceChannels[i];
				}
				nop->SetChild(nop->op.args.ImageLayerColour.colour, cOp);

				at = nop;
				break;
			}

			case OP_TYPE::IM_DISPLACE:
			{
				Ptr<ASTOpFixed> NewDisplace = mu::Clone<ASTOpFixed>(channelSourceAt);
				Ptr<ASTOpImageSwizzle> NewSwizzle = mu::Clone<ASTOpImageSwizzle>(this);
				ReplaceAllSources(NewSwizzle, NewDisplace->children[NewDisplace->op.args.ImageDisplace.source].child());
				NewDisplace->SetChild(NewDisplace->op.args.ImageDisplace.source, NewSwizzle);
				at = NewDisplace;
				break;
			}

			case OP_TYPE::IM_RASTERMESH:
			{
				Ptr<ASTOpFixed> NewRaster = mu::Clone<ASTOpFixed>(channelSourceAt);
				Ptr<ASTOpImageSwizzle> NewSwizzle = mu::Clone<ASTOpImageSwizzle>(this);
				ReplaceAllSources(NewSwizzle, NewRaster->children[NewRaster->op.args.ImageRasterMesh.image].child());
				NewRaster->SetChild(NewRaster->op.args.ImageRasterMesh.image, NewSwizzle);
				at = NewRaster;
				break;
			}

			default:
				bAllChannelsAreTheSame = false;
				break;
			}
		}

		if (!bAllChannelsAreTheSame && bAllChannelsAreTheSameType)
		{
			// Maybe we can still sink the instruction in some cases

			// If we have RGB being the same IM_MULTILAYER, and alpha a compatible IM_MULTILAYER we can optimize with
			// a special multilayer blend mode. This happens often because of higher level group projector nodes.
			if (!at
				&&
				Sources[0] == Sources[1] && Sources[0] == Sources[2]
				&&
				Sources[0] && Sources[0]->GetOpType() == OP_TYPE::IM_MULTILAYER
				&&
				Sources[3] && Sources[3]->GetOpType() == OP_TYPE::IM_MULTILAYER
				&&
				SourceChannels[0] == 0 && SourceChannels[1] == 1 && SourceChannels[2] == 2 && SourceChannels[3] == 0
				)
			{
				const ASTOpImageMultiLayer* ColorMultiLayer = dynamic_cast<const ASTOpImageMultiLayer*>(Sources[0].child().get());
				check(ColorMultiLayer);
				const ASTOpImageMultiLayer* AlphaMultiLayer = dynamic_cast<const ASTOpImageMultiLayer*>(Sources[3].child().get());
				check(AlphaMultiLayer);

				bool bIsSpecialMultiLayer = !AlphaMultiLayer->mask
					&&
					ColorMultiLayer->range == AlphaMultiLayer->range;

				if (bIsSpecialMultiLayer)
				{
					// We can combine the 2 multilayers into the composite blend+lighten mode

					Ptr<ASTOpImageSwizzle> NewBase = mu::Clone<ASTOpImageSwizzle>(this);
					NewBase->Sources[0] = ColorMultiLayer->base.child();
					NewBase->Sources[1] = ColorMultiLayer->base.child();
					NewBase->Sources[2] = ColorMultiLayer->base.child();
					NewBase->Sources[3] = AlphaMultiLayer->base.child();

					Ptr<ASTOpImageSwizzle> NewBlended = mu::Clone<ASTOpImageSwizzle>(this);
					NewBlended->Sources[0] = ColorMultiLayer->blend.child();
					NewBlended->Sources[1] = ColorMultiLayer->blend.child();
					NewBlended->Sources[2] = ColorMultiLayer->blend.child();
					NewBlended->Sources[3] = AlphaMultiLayer->blend.child();

					Ptr<ASTOpImageMultiLayer> NewMultiLayer = mu::Clone<ASTOpImageMultiLayer>(ColorMultiLayer);
					NewMultiLayer->blendTypeAlpha = AlphaMultiLayer->blendType;
					NewMultiLayer->base = NewBase;
					NewMultiLayer->blend = NewBlended;

					if ( NewMultiLayer->mask.child() == AlphaMultiLayer->blend.child()
						&&
						NewBlended->Format==EImageFormat::IF_RGBA_UBYTE
						)
					{
						// Additional optimization is possible here.
						NewMultiLayer->bUseMaskFromBlended = true;
						NewMultiLayer->mask = nullptr;
					}

					at = NewMultiLayer;
				}
			}


			// If the channels are compatible switches, we can still sink the swizzle.
			if (!at && sourceType == OP_TYPE::IM_SWITCH)
			{
				const ASTOpSwitch* FirstSwitch = dynamic_cast<const ASTOpSwitch*>(Sources[0].child().get());
				check(FirstSwitch);

				bool bAreAllSwitchesCompatible = true;
				for (int32 c = 1; c < MUTABLE_OP_MAX_SWIZZLE_CHANNELS; ++c)
				{
					if (Sources[c])
					{
						const ASTOpSwitch* Typed = dynamic_cast<const ASTOpSwitch*>(Sources[c].child().get());
						check(Typed);
						if (!Typed->IsCompatibleWith(FirstSwitch))
						{
							bAreAllSwitchesCompatible = false;
							break;
						}
					}
				}

				if (bAreAllSwitchesCompatible)
				{
					// Move the swizzle down all the paths
					Ptr<ASTOpSwitch> nop = mu::Clone<ASTOpSwitch>(channelSourceAt);

					if (nop->def)
					{
						Ptr<ASTOpImageSwizzle> defOp = mu::Clone<ASTOpImageSwizzle>(this);
						for (int c = 0; c < MUTABLE_OP_MAX_SWIZZLE_CHANNELS; ++c)
						{
							const ASTOpSwitch* ChannelSwitch = dynamic_cast<const ASTOpSwitch*>(Sources[c].child().get());
							if (ChannelSwitch)
							{
								defOp->Sources[c] = ChannelSwitch->def.child();
							}
						}
						nop->def = defOp;
					}

					for (int32 v = 0; v < nop->cases.Num(); ++v)
					{
						if (nop->cases[v].branch)
						{
							Ptr<ASTOpImageSwizzle> branchOp = mu::Clone<ASTOpImageSwizzle>(this);
							for (int c = 0; c < MUTABLE_OP_MAX_SWIZZLE_CHANNELS; ++c)
							{
								const ASTOpSwitch* ChannelSwitch = dynamic_cast<const ASTOpSwitch*>(Sources[c].child().get());
								if (ChannelSwitch)
								{
									branchOp->Sources[c] = ChannelSwitch->cases[v].branch.child();
								}
							}
							nop->cases[v].branch = branchOp;
						}
					}

					at = nop;
				}
			}

			// Swizzle down a displace.
			if (!at && sourceType == OP_TYPE::IM_DISPLACE)
			{
				const ASTOpFixed* FirstDisplace = dynamic_cast<const ASTOpFixed*>(Sources[0].child().get());
				check(FirstDisplace);

				bool bAreAllDisplacesCompatible = true;
				for (int32 c = 1; c < MUTABLE_OP_MAX_SWIZZLE_CHANNELS; ++c)
				{
					if (Sources[c])
					{
						const ASTOpFixed* Typed = dynamic_cast<const ASTOpFixed*>(Sources[c].child().get());
						check(Typed);
						if (FirstDisplace->op.args.ImageDisplace.displacementMap != Typed->op.args.ImageDisplace.displacementMap)
						{
							bAreAllDisplacesCompatible = false;
							break;
						}
					}
				}

				if (bAreAllDisplacesCompatible)
				{
					// Move the swizzle down all the paths
					Ptr<ASTOpFixed> NewDisplace = mu::Clone<ASTOpFixed>(FirstDisplace);

					Ptr<ASTOpImageSwizzle> SourceOp = mu::Clone<ASTOpImageSwizzle>(this);
					for (int c = 0; c < MUTABLE_OP_MAX_SWIZZLE_CHANNELS; ++c)
					{
						const ASTOpFixed* ChannelDisplace = dynamic_cast<const ASTOpFixed*>(Sources[c].child().get());
						if (ChannelDisplace)
						{
							SourceOp->Sources[c] = ChannelDisplace->children[ChannelDisplace->op.args.ImageDisplace.source].child();
						}
					}

					NewDisplace->SetChild(NewDisplace->op.args.ImageDisplace.source, SourceOp);

					at = NewDisplace;
				}

			}

			// Swizzle down a raster mesh.
			if (!at && sourceType == OP_TYPE::IM_RASTERMESH)
			{
				const ASTOpFixed* FirstRasterMesh = dynamic_cast<const ASTOpFixed*>(Sources[0].child().get());
				check(FirstRasterMesh);

				bool bAreAllFirstRasterMeshesCompatible = true;
				for (int32 c = 1; c < MUTABLE_OP_MAX_SWIZZLE_CHANNELS; ++c)
				{
					if (Sources[c])
					{
						const ASTOpFixed* Typed = dynamic_cast<const ASTOpFixed*>(Sources[c].child().get());
						check(Typed);
						// Compare all args but the source image
						OP::ImageRasterMeshArgs ArgCopy = FirstRasterMesh->op.args.ImageRasterMesh;
						ArgCopy.image = Typed->op.args.ImageRasterMesh.image;

						if (FMemory::Memcmp(&ArgCopy, &Typed->op.args.ImageRasterMesh, sizeof(OP::ImageRasterMeshArgs)) != 0)
						{
							bAreAllFirstRasterMeshesCompatible = false;
							break;
						}
					}
				}

				if (bAreAllFirstRasterMeshesCompatible)
				{
					// Move the swizzle down all the paths
					Ptr<ASTOpFixed> NewRaster = mu::Clone<ASTOpFixed>(FirstRasterMesh);

					Ptr<ASTOpImageSwizzle> NewSwizzle = mu::Clone<ASTOpImageSwizzle>(this);
					for (int c = 0; c < MUTABLE_OP_MAX_SWIZZLE_CHANNELS; ++c)
					{
						const ASTOpFixed* ChannelRaster = dynamic_cast<const ASTOpFixed*>(Sources[c].child().get());
						if (ChannelRaster)
						{
							NewSwizzle->Sources[c] = ChannelRaster->children[ChannelRaster->op.args.ImageRasterMesh.image].child();
						}
					}

					NewRaster->SetChild(NewRaster->op.args.ImageRasterMesh.image, NewSwizzle);

					at = NewRaster;
				}

			}
		}

		// Swizzle of RGB from a source + A from a layer colour
		// This can be optimize to apply the layer colour on-base directly to the alpha channel to skip the swizzle
		if ( !at 
			&&
			Sources[0] && Sources[0]==Sources[1] && Sources[0]==Sources[2]
			&&
			Sources[3] && Sources[3]->GetOpType()==OP_TYPE::IM_LAYERCOLOUR )
		{
			// Move the swizzle down all the paths
			Ptr<ASTOpFixed> NewLayerColour = mu::Clone<ASTOpFixed>(Sources[3].child());

			Ptr<ASTOpImageSwizzle> NewSwizzle = mu::Clone<ASTOpImageSwizzle>(this);
			NewSwizzle->Sources[3] = NewLayerColour->children[NewLayerColour->op.args.ImageLayerColour.base].child();

			NewLayerColour->op.args.ImageLayerColour.blendTypeAlpha = NewLayerColour->op.args.ImageLayerColour.blendType;
			NewLayerColour->op.args.ImageLayerColour.blendType = uint8(EBlendType::BT_NONE);
			NewLayerColour->SetChild(NewLayerColour->op.args.ImageLayerColour.base, NewSwizzle);

			at = NewLayerColour;

		}


//            OP::ADDRESS sourceAt = program.m_code[at].args.ImageSwizzle.sources[0];
//            OP_TYPE sourceType = (OP_TYPE)program.m_code[sourceAt].type;
//            switch ( sourceType )
//            {
//            case OP_TYPE::IM_COMPOSE:
//            {
//                bool canSink = true;
//                OP::ADDRESS layout = program.m_code[sourceAt].args.ImageCompose.layout;
//                uint32_t block = program.m_code[sourceAt].args.ImageCompose.blockIndex;
//                for ( int c=1; c<MUTABLE_OP_MAX_SWIZZLE_CHANNELS; ++c )
//                {
//                    OP::ADDRESS candidate = program.m_code[at].args.ImageSwizzle.sources[c];
//                    if ( candidate )
//                    {
//                        if ( program.m_code[candidate].type==sourceType )
//                        {
//                            OP::ADDRESS clayout =
//                                    program.m_code[candidate].args.ImageCompose.layout;
//                            uint32_t cblock =
//                                    program.m_code[candidate].args.ImageCompose.blockIndex;
//                            canSink = canSink
//                                    && ( layout == clayout )
//                                    && ( block == cblock );
//                        }
//                        else
//                        {
//                            canSink = false;
//                        }
//                    }
//                }

//                if ( canSink )
//                {
//                    m_modified = true;

//                    OP op = program.m_code[sourceAt];

//                    OP swizzleBaseOp = program.m_code[at];
//                    OP swizzleBlockOp = program.m_code[at];

//                    for ( int c=0; c<MUTABLE_OP_MAX_SWIZZLE_CHANNELS; ++c )
//                    {
//                        OP::ADDRESS source = program.m_code[at].args.ImageSwizzle.sources[c];
//                        if ( source )
//                        {
//                            swizzleBaseOp.args.ImageSwizzle.sources[c] =
//                                    program.m_code[ source ].args.ImageCompose.base;

//                            swizzleBlockOp.args.ImageSwizzle.sources[c] =
//                                    program.m_code[ source ].args.ImageCompose.blockImage;
//                        }
//                    }
//                    op.args.ImageCompose.base = program.AddOp( swizzleBaseOp );
//                    op.args.ImageCompose.blockImage = program.AddOp( swizzleBlockOp );

//                    at = program.AddOp( op );
//                }
//                break;
//            }

//            case OP_TYPE::IM_CONDITIONAL:
//            {
//                // We can always sink into conditionals
//                m_modified = true;

//                OP op = program.m_code[sourceAt];

//                OP swizzleYesOp = program.m_code[at];
//                OP swizzleNoOp = program.m_code[at];
//                OP::ADDRESS source0 = 0;
//                for ( int c=0; c<MUTABLE_OP_MAX_SWIZZLE_CHANNELS; ++c )
//                {
//                    OP::ADDRESS source = program.m_code[at].args.ImageSwizzle.sources[c];
//                    if ( c==0 )
//                    {
//                        source0 = source;
//                    }

//                    if ( source
//                        &&
//                        ( (c==0) || ( program.m_code[source0].args.Conditional.condition == program.m_code[source].args.Conditional.condition ) )
//                        )
//                    {
//                        swizzleYesOp.args.ImageSwizzle.sources[c] =
//                                program.m_code[ source ].args.Conditional.yes;

//                        swizzleNoOp.args.ImageSwizzle.sources[c] =
//                                program.m_code[ source ].args.Conditional.no;
//                    }
//                }
//                op.args.Conditional.yes = program.AddOp( swizzleYesOp );
//                op.args.Conditional.no = program.AddOp( swizzleNoOp );

//                at = program.AddOp( op );
//                break;
//            }


//            case OP_TYPE::IM_SWITCH:
//            {
//                bool canSink = true;
//                OP::ADDRESS variable = program.m_code[sourceAt].args.Switch.variable;

//                // If at least 3 channels can be combined, do it. We may be duplicating some data
//                // But it greatly optimises speed in some cases like the constant alpha masks of
//                // the face colour interpolation.
//                // TODO: Make this dependent on compilation options.
//                //for ( int c=1; c<MUTABLE_OP_MAX_SWIZZLE_CHANNELS; ++c )
//                for ( int c=1; c<3; ++c )
//                {
//                    OP::ADDRESS candidate = program.m_code[at].args.ImageSwizzle.sources[c];
//                    if ( candidate )
//                    {
//                        if ( program.m_code[candidate].type==sourceType )
//                        {
//                            OP::ADDRESS cvariable =
//                                    program.m_code[candidate].args.Switch.variable;
//                            canSink = canSink && ( variable == cvariable );
//                        }
//                        else
//                        {
//                            canSink = false;
//                        }
//                    }
//                }

//                if ( canSink )
//                {
//                    // TODO since data-switch
////                    m_modified = true;

////                    OP op = program.m_code[sourceAt];

////                    if ( op.args.Switch.def )
////                    {
////                        OP swizzleDefOp = program.m_code[at];
////                        for ( int c=0; c<MUTABLE_OP_MAX_SWIZZLE_CHANNELS; ++c )
////                        {
////                            OP::ADDRESS source = program.m_code[at].args.ImageSwizzle.sources[c];
////                            if ( source )
////                            {
////                                if ( program.m_code[source].type==sourceType )
////                                {
////                                    swizzleDefOp.args.ImageSwizzle.sources[c] =
////                                            program.m_code[ source ].args.Switch.def;
////                                }
////                                else
////                                {
////                                    swizzleDefOp.args.ImageSwizzle.sources[c] = source;
////                                }
////                            }
////                        }
////                        op.args.Switch.def = program.AddOp( swizzleDefOp );
////                    }

////                    for ( int o=0; o<MUTABLE_OP_MAX_SWITCH_OPTIONS; ++o )
////                    {
////                        if ( op.args.Switch.values[o] )
////                        {
////                            OP swizzleOptOp = program.m_code[at];
////                            for ( int c=0; c<MUTABLE_OP_MAX_SWIZZLE_CHANNELS; ++c )
////                            {
////                                OP::ADDRESS source = program.m_code[at].args.ImageSwizzle.sources[c];
////                                if ( source )
////                                {
////                                    if ( program.m_code[source].type==sourceType )
////                                    {
////                                        swizzleOptOp.args.ImageSwizzle.sources[c] =
////                                                program.m_code[ source ].args.Switch.values[o];
////                                    }
////                                    else
////                                    {
////                                        swizzleOptOp.args.ImageSwizzle.sources[c] = source;
////                                    }
////                                }
////                            }
////                            op.args.Switch.values[o] = program.AddOp( swizzleOptOp );
////                        }
////                    }

////                    at = program.AddOp( op );
//                }
//                break;
//            }


//            case OP_TYPE::IM_INTERPOLATE:
//            {
//                bool canSink = true;
//                OP::ADDRESS factor = program.m_code[sourceAt].args.ImageInterpolate.factor;

//                // It is worth to sink 3 channels
//                // TODO: Make it depend on compilation parameters
//                //for ( int c=1; c<MUTABLE_OP_MAX_SWIZZLE_CHANNELS; ++c )
//                for ( int c=1; c<3; ++c )
//                {
//                    OP::ADDRESS candidate = program.m_code[at].args.ImageSwizzle.sources[c];
//                    if ( candidate )
//                    {
//                        if ( program.m_code[candidate].type==sourceType )
//                        {
//                            OP::ADDRESS cfactor =
//                                    program.m_code[candidate].args.ImageInterpolate.factor;
//                            canSink = canSink && ( factor == cfactor );
//                        }
//                        else
//                        {
//                            canSink = false;
//                        }
//                    }
//                }

//                if ( canSink )
//                {
//                    m_modified = true;

//                    OP op = program.m_code[sourceAt];

//                    for ( int o=0; o<MUTABLE_OP_MAX_INTERPOLATE_COUNT; ++o )
//                    {
//                        if ( op.args.ImageInterpolate.targets[o] )
//                        {
//                            OP swizzleOptOp = program.m_code[at];
//                            for ( int c=0; c<MUTABLE_OP_MAX_SWIZZLE_CHANNELS; ++c )
//                            {
//                                OP::ADDRESS source = program.m_code[at].args.ImageSwizzle.sources[c];

//                                if ( source
//                                     && program.m_code[source].type==OP_TYPE::IM_INTERPOLATE
//                                     && program.m_code[source].args.ImageInterpolate.factor==factor
//                                     )
//                                {
//                                    swizzleOptOp.args.ImageSwizzle.sources[c] =
//                                            program.m_code[ source ].args.ImageInterpolate.targets[o];
//                                }
//                            }
//                            op.args.ImageInterpolate.targets[o] = program.AddOp( swizzleOptOp );
//                        }
//                    }

//                    at = program.AddOp( op );
//                }
//                break;
//            }


//            case OP_TYPE::IM_INTERPOLATE3:
//            {
//                // We can sink if all channels are interpolates for the same factors
//                OP::ADDRESS sourceAt0 = program.m_code[at].args.ImageSwizzle.sources[0];
//                OP::ADDRESS factor10 = program.m_code[sourceAt0].args.ImageInterpolate3.factor1;
//                OP::ADDRESS factor20 = program.m_code[sourceAt0].args.ImageInterpolate3.factor2;

//                OP::ADDRESS sourceAt1 = program.m_code[at].args.ImageSwizzle.sources[1];
//                OP::ADDRESS factor11 = program.m_code[sourceAt1].args.ImageInterpolate3.factor1;
//                OP::ADDRESS factor21 = program.m_code[sourceAt1].args.ImageInterpolate3.factor2;

//                OP::ADDRESS sourceAt2 = program.m_code[at].args.ImageSwizzle.sources[2];
//                OP::ADDRESS factor12 = program.m_code[sourceAt2].args.ImageInterpolate3.factor1;
//                OP::ADDRESS factor22 = program.m_code[sourceAt2].args.ImageInterpolate3.factor2;

//                bool canSink = true;
//                canSink &= program.m_code[sourceAt0].type == program.m_code[sourceAt1].type;
//                canSink &= program.m_code[sourceAt0].type == program.m_code[sourceAt2].type;
//                canSink &= factor10 == factor11;
//                canSink &= factor10 == factor12;
//                canSink &= factor20 == factor21;
//                canSink &= factor20 == factor22;

//                if ( canSink )
//                {
//                    m_modified = true;

//                    OP op = program.m_code[sourceAt0];

//                    // Target 0
//                    {
//                        OP swizzleOptOp = program.m_code[at];
//                        for ( int c=0; c<MUTABLE_OP_MAX_SWIZZLE_CHANNELS; ++c )
//                        {
//                            OP::ADDRESS source = program.m_code[at].args.ImageSwizzle.sources[c];
//                            if ( source
//                                 && program.m_code[source].type==OP_TYPE::IM_INTERPOLATE3
//                                 && program.m_code[source].args.ImageInterpolate3.factor1==factor10
//                                 && program.m_code[source].args.ImageInterpolate3.factor2==factor20
//                                 )
//                            {
//                                swizzleOptOp.args.ImageSwizzle.sources[c] =
//                                        program.m_code[ source ].args.ImageInterpolate3.target0;
//                            }
//                        }
//                        op.args.ImageInterpolate3.target0 = program.AddOp( swizzleOptOp );
//                    }

//                    // Target 1
//                    {
//                        OP swizzleOptOp = program.m_code[at];
//                        for ( int c=0; c<MUTABLE_OP_MAX_SWIZZLE_CHANNELS; ++c )
//                        {
//                            OP::ADDRESS source = program.m_code[at].args.ImageSwizzle.sources[c];
//                            if ( source
//                                 && program.m_code[source].type==OP_TYPE::IM_INTERPOLATE3
//                                 && program.m_code[source].args.ImageInterpolate3.factor1==factor10
//                                 && program.m_code[source].args.ImageInterpolate3.factor2==factor20
//                                 )
//                            {
//                                swizzleOptOp.args.ImageSwizzle.sources[c] =
//                                        program.m_code[ source ].args.ImageInterpolate3.target1;
//                            }
//                        }
//                        op.args.ImageInterpolate3.target1 = program.AddOp( swizzleOptOp );
//                    }

//                    // Target 2
//                    {
//                        OP swizzleOptOp = program.m_code[at];
//                        for ( int c=0; c<MUTABLE_OP_MAX_SWIZZLE_CHANNELS; ++c )
//                        {
//                            OP::ADDRESS source = program.m_code[at].args.ImageSwizzle.sources[c];
//                            if ( source
//                                 && program.m_code[source].type==OP_TYPE::IM_INTERPOLATE3
//                                 && program.m_code[source].args.ImageInterpolate3.factor1==factor10
//                                 && program.m_code[source].args.ImageInterpolate3.factor2==factor20
//                                 )
//                            {
//                                swizzleOptOp.args.ImageSwizzle.sources[c] =
//                                        program.m_code[ source ].args.ImageInterpolate3.target2;
//                            }
//                        }
//                        op.args.ImageInterpolate3.target2 = program.AddOp( swizzleOptOp );
//                    }

//                    at = program.AddOp( op );
//                }
//                break;
//            }

//            default:
//                break;
//            }
//		}

		return at;
	}



	//!
	FImageDesc ASTOpImageSwizzle::GetImageDesc(bool returnBestOption, FGetImageDescContext* context) const
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

		if (Sources[0].child())
		{
			res = Sources[0]->GetImageDesc(returnBestOption, context);
			res.m_format = Format;
			check(res.m_format != EImageFormat::IF_NONE);
		}

		// Cache the result
		if (context)
		{
			context->m_results.Add(this, res);
		}

		return res;
	}


	void ASTOpImageSwizzle::GetLayoutBlockSize(int* pBlockX, int* pBlockY)
	{
		if (Sources[0].child())
		{
			// Assume the block size of the biggest mip
			Sources[0].child()->GetLayoutBlockSize(pBlockX, pBlockY);
		}
	}


	bool ASTOpImageSwizzle::IsImagePlainConstant(FVector4f& colour) const
	{
		// TODO: Maybe something could be done here.
		return false;
	}


	mu::Ptr<ImageSizeExpression> ASTOpImageSwizzle::GetImageSizeExpression() const
	{
		mu::Ptr<ImageSizeExpression> pRes;

		if (Sources[0].child())
		{
			pRes = Sources[0].child()->GetImageSizeExpression();
		}
		else
		{
			pRes = new ImageSizeExpression;
		}

		return pRes;
	}

}