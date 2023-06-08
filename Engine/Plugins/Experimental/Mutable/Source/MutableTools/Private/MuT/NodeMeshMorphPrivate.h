// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/NodeMeshMorph.h"
#include "MuT/NodeScalar.h"
#include "MuT/NodeMeshPrivate.h"
#include "MuT/AST.h"

namespace mu
{


	class NodeMeshMorph::Private : public NodeMesh::Private
	{
	public:

		MUTABLE_DEFINE_CONST_VISITABLE()

	public:

		static NODE_TYPE s_type;

		Ptr<NodeScalar> Factor;
		Ptr<NodeMesh> Base;
		Ptr<NodeMesh> Morph;
		
		bool bReshapeSkeleton = false;
		bool bReshapePhysicsVolumes = false;
		
		TArray<string> BonesToDeform;
		TArray<string> PhysicsToDeform;

        //!
		void Serialise( OutputArchive& arch ) const
		{
            uint32_t ver = 6;
			arch << ver;

			arch << Factor;
			arch << Base;
			arch << Morph;

			arch << bReshapeSkeleton;
			arch << bReshapePhysicsVolumes;
			arch << BonesToDeform;
			arch << PhysicsToDeform;
        }

		//!
		void Unserialise( InputArchive& arch )
		{
            uint32_t ver;
			arch >> ver;
			check(ver <= 6);

			arch >> Factor;
			arch >> Base;

			if (ver < 6)
			{
				TArray<Ptr<NodeMesh>> Morphs;
				arch >> Morphs;

				// The result will not be the same.
				if (!Morphs.IsEmpty())
				{
					Morph = Morphs.Last();
				}
			}
			else
			{
				arch >> Morph;
			}

			if (ver < 6)
			{
				bool bDeprecated;
				arch >> bDeprecated;
			}
	
			if (ver >= 2)
			{
				arch >> bReshapeSkeleton;
				arch >> bReshapePhysicsVolumes;
	
				// This repetition is needed, there was a bug where m_reshapePhysicsVolumes was serialized twice in ver 2.
				if (ver == 2) 
				{
					arch >> bReshapePhysicsVolumes;
				}
				arch >> BonesToDeform;			
			}
			else
			{
				bReshapeSkeleton = false;
				bReshapePhysicsVolumes = false;
				BonesToDeform.Empty();
			}

			if (ver == 3)
			{
				bool bDeformAllBones_DEPRECATED;
				arch >> bDeformAllBones_DEPRECATED;
			}

			if (ver >= 3 && ver < 5)
			{
				bool bDeformAllPhysics_DEPRECATED;
				arch >> bDeformAllPhysics_DEPRECATED;
			}

			if (ver >= 3)
			{
				arch >> PhysicsToDeform;
			}
			else
			{
				PhysicsToDeform.Empty();
			}

		}

		// NodeMesh::Private interface
        Ptr<NodeLayout> GetLayout( int index ) const override;

	};


}
