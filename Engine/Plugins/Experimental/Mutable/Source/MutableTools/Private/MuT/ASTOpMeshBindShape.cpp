// Copyright Epic Games, Inc. All Rights Reserved.

#include "ASTOpMeshBindShape.h"
#include "ASTOpSwitch.h"
#include "MuT/ErrorLogPrivate.h"
#include "MuT/CodeOptimiser.h"
#include "MuT/StreamsPrivate.h"

using namespace mu;


ASTOpMeshBindShape::ASTOpMeshBindShape()
    : Mesh(this)
    , Shape(this)
{
}


ASTOpMeshBindShape::~ASTOpMeshBindShape()
{
    // Explicit call needed to avoid recursive destruction
    ASTOp::RemoveChildren();
}


bool ASTOpMeshBindShape::IsEqual(const ASTOp& otherUntyped) const
{
	if ( auto other = dynamic_cast<const ASTOpMeshBindShape*>(&otherUntyped) )
    {
		const bool bSameFlags =
			m_reshapeSkeleton == other->m_reshapeSkeleton &&
			m_discardInvalidBindings == other->m_discardInvalidBindings &&
			m_enableRigidParts == other->m_enableRigidParts &&
			m_deformAllBones == other->m_deformAllBones &&
			m_deformAllPhysics == other->m_deformAllPhysics &&
    		m_reshapePhysicsVolumes == other->m_reshapePhysicsVolumes &&
			m_reshapeVertices == other->m_reshapeVertices;
    	
		return bSameFlags && 
				Mesh == other->Mesh && 
				Shape == other->Shape && 
				m_bonesToDeform == other->m_bonesToDeform &&
				m_physicsToDeform == other->m_physicsToDeform && 
				m_bindingMethod == other->m_bindingMethod;
	}
	
    return false;
}


uint64 ASTOpMeshBindShape::Hash() const
{
	uint64 res = std::hash<void*>()(Mesh.child().get() );
	hash_combine(res, Shape.child().get());
	hash_combine(res, m_reshapeSkeleton);
	hash_combine(res, m_discardInvalidBindings);
	hash_combine(res, m_enableRigidParts);
	hash_combine(res, m_deformAllBones);
	hash_combine(res, m_deformAllPhysics);
	hash_combine(res, m_reshapePhysicsVolumes);
	hash_combine(res, m_reshapeVertices);
	hash_combine(res, m_bindingMethod);

	for ( const string& S : m_bonesToDeform)
	{
		hash_combine(res, S);
	}

	for ( const string& S : m_physicsToDeform)
	{
		hash_combine(res, S);
	}

	return res;
}


mu::Ptr<ASTOp> ASTOpMeshBindShape::Clone(MapChildFunc& mapChild) const
{
    Ptr<ASTOpMeshBindShape> n = new ASTOpMeshBindShape();
	n->Mesh = mapChild(Mesh.child());
	n->Shape = mapChild(Shape.child());
	n->m_reshapeSkeleton = m_reshapeSkeleton;
	n->m_discardInvalidBindings = m_discardInvalidBindings;
	n->m_enableRigidParts = m_enableRigidParts;
	n->m_deformAllBones = m_deformAllBones;
	n->m_deformAllPhysics = m_deformAllPhysics;
	n->m_reshapePhysicsVolumes = m_reshapePhysicsVolumes;
	n->m_reshapeVertices = m_reshapeVertices;
	n->m_bonesToDeform = m_bonesToDeform;
	n->m_physicsToDeform = m_physicsToDeform;	
	n->m_bindingMethod = m_bindingMethod;
	return n;
}


void ASTOpMeshBindShape::ForEachChild(const std::function<void(ASTChild&)>& f )
{
	f(Mesh);
	f(Shape);
}


void ASTOpMeshBindShape::Link( PROGRAM& program, const FLinkerOptions*)
{
    // Already linked?
    if (!linkedAddress)
    {
        OP::MeshBindShapeArgs args;
        FMemory::Memzero( &args, sizeof(args) );

		if (m_reshapeSkeleton)
		{
			args.flags |= uint32(OP::EMeshBindShapeFlags::ReshapeSkeleton);
		}

    	if (m_discardInvalidBindings)
    	{
    		args.flags |= uint32(OP::EMeshBindShapeFlags::DiscardInvalidBindings);
    	}
    
    	if (m_enableRigidParts)
    	{
    		args.flags |= uint32(OP::EMeshBindShapeFlags::EnableRigidParts);
    	}
		
		if (m_deformAllBones)
    	{
    		args.flags |= uint32(OP::EMeshBindShapeFlags::DeformAllBones);
    	}

		if (m_deformAllPhysics)
    	{
    		args.flags |= uint32(OP::EMeshBindShapeFlags::DeformAllPhysics);
    	}

    	if (m_reshapePhysicsVolumes)
    	{
    		args.flags |= uint32(OP::EMeshBindShapeFlags::ReshapePhysicsVolumes);
    	}
  	
		if (m_reshapeVertices)
    	{
    		args.flags |= uint32(OP::EMeshBindShapeFlags::ReshapeVertices);
    	}  
 	
		args.bindingMethod = m_bindingMethod;    
		
		if (Mesh) 
		{
			args.mesh = Mesh->linkedAddress;
		}

		if (Shape)
		{
			args.shape = Shape->linkedAddress;
		}
		
		linkedAddress = (OP::ADDRESS)program.m_opAddress.size();
		program.m_opAddress.push_back((uint32_t)program.m_byteCode.size());
		AppendCode(program.m_byteCode, OP_TYPE::ME_BINDSHAPE);
		AppendCode(program.m_byteCode, args);

		AppendCode(program.m_byteCode, (int32)m_bonesToDeform.Num());
		for (const string& S : m_bonesToDeform)
		{
			const uint32 index = program.AddConstant(S);
			AppendCode(program.m_byteCode, index);
		}

		AppendCode(program.m_byteCode, (int32)m_physicsToDeform.Num());
		for (const string& S : m_physicsToDeform)
		{
			const uint32 index = program.AddConstant(S);
			AppendCode(program.m_byteCode, index);
		}
    }
}


//-------------------------------------------------------------------------------------------------
mu::Ptr<ASTOp> ASTOpMeshBindShape::OptimiseSink(const MODEL_OPTIMIZATION_OPTIONS&, OPTIMIZE_SINK_CONTEXT&) const
{
	Ptr<ASTOp> NewOp;

	Ptr<ASTOp> MeshAt = Mesh.child();
	if (!MeshAt)
	{ 
		return nullptr;
	}

	Ptr<ASTOp> ShapeAt = Shape.child();
	if (!ShapeAt)
	{
		return nullptr;
	}

	OP_TYPE MeshType = MeshAt->GetOpType();
	OP_TYPE ShapeType = ShapeAt->GetOpType();

	// See if both mesh and shape have an operation that can be optimized in a combined way
	if (MeshType==ShapeType)
	{
		switch (MeshType)
		{

		case OP_TYPE::ME_SWITCH:
		{
			// Id the switch variable and structure is the same
			const ASTOpSwitch* MeshSwitch = reinterpret_cast<const ASTOpSwitch*>(MeshAt.get());
			const ASTOpSwitch* ShapeSwitch = reinterpret_cast<const ASTOpSwitch*>(ShapeAt.get());
			bool bIsSimilarSwitch = MeshSwitch->IsCompatibleWith(ShapeSwitch);
			if (!bIsSimilarSwitch)
			{
				break;
			}

			// Move the operation down all the paths
			Ptr<ASTOpSwitch> NewSwitch = mu::Clone<ASTOpSwitch>(MeshAt);

			if (NewSwitch->def)
			{
				Ptr<ASTOpMeshBindShape> NewBind = mu::Clone<ASTOpMeshBindShape>(this);
				NewBind->Mesh = MeshSwitch->def.child();
				NewBind->Shape = ShapeSwitch->def.child();
				NewSwitch->def = NewBind;
			}

			// We need to copy the options because we change them
			for (size_t v = 0; v < NewSwitch->cases.size(); ++v)
			{
				if (NewSwitch->cases[v].branch)
				{
					Ptr<ASTOpMeshBindShape> NewBind = mu::Clone<ASTOpMeshBindShape>(this);
					NewBind->Mesh = MeshSwitch->cases[v].branch.child();
					NewBind->Shape = ShapeSwitch->FindBranch(MeshSwitch->cases[v].condition);
					NewSwitch->cases[v].branch = NewBind;
				}
			}

			NewOp = NewSwitch;
			break;
		}

		default:
			break;

		}
	}


	// If not already optimized
	if (!NewOp)
	{
		// Optimize only the mesh parameter
		switch (MeshType)
		{

		case OP_TYPE::ME_SWITCH:
		{
			// Move the operation down all the paths
			Ptr<ASTOpSwitch> NewSwitch = mu::Clone<ASTOpSwitch>(MeshAt);

			if (NewSwitch->def)
			{
				Ptr<ASTOpMeshBindShape> NewBind = mu::Clone<ASTOpMeshBindShape>(this);
				NewBind->Mesh = NewSwitch->def.child();
				NewSwitch->def = NewBind;
			}

			// We need to copy the options because we change them
			for (size_t v = 0; v < NewSwitch->cases.size(); ++v)
			{
				if (NewSwitch->cases[v].branch)
				{
					Ptr<ASTOpMeshBindShape> NewBind = mu::Clone<ASTOpMeshBindShape>(this);
					NewBind->Mesh = NewSwitch->cases[v].branch.child();
					NewSwitch->cases[v].branch = NewBind;
				}
			}

			NewOp = NewSwitch;
			break;
		}

		default:
			break;

		}
	}

	// If not already optimized
	if (!NewOp)
	{
		// Optimize only the shape parameter
		switch (ShapeType)
		{

		case OP_TYPE::ME_SWITCH:
		{
			// Move the operation down all the paths
			Ptr<ASTOpSwitch> NewSwitch = mu::Clone<ASTOpSwitch>(ShapeAt);

			if (NewSwitch->def)
			{
				Ptr<ASTOpMeshBindShape> NewBind = mu::Clone<ASTOpMeshBindShape>(this);
				NewBind->Shape = NewSwitch->def.child();
				NewSwitch->def = NewBind;
			}

			// We need to copy the options because we change them
			for (size_t v = 0; v < NewSwitch->cases.size(); ++v)
			{
				if (NewSwitch->cases[v].branch)
				{
					Ptr<ASTOpMeshBindShape> NewBind = mu::Clone<ASTOpMeshBindShape>(this);
					NewBind->Shape = NewSwitch->cases[v].branch.child();
					NewSwitch->cases[v].branch = NewBind;
				}
			}

			NewOp = NewSwitch;
			break;
		}

		default:
			break;

		}
	}


	return NewOp;
}
