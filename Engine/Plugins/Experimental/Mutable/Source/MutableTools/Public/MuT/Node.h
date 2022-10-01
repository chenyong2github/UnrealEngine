// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/RefCounted.h"
#include "MuR/Ptr.h"
#include "MuR/Types.h"
#include "MuR/Serialisation.h"


//! This tag is used to identify files containing serialised Node hierarchies. The tag is not added
//! or checked by the Node serialisation methods, but the involved tools should take care of it.
#define MUTABLE_SOURCE_MODEL_FILETAG	"amsm"


namespace mu
{
	//! \defgroup tools Tools library classes
	//! Tools library classes.

	//! \defgroup model Model nodes
	//! \ingroup tools
	//! This group contains the nodes that can be used to compose models.

	//! \defgroup transform Transform nodes
	//! \ingroup tools
	//! This group contains the nodes that can be used to compose model transformations.


	// Forward declarations
	class Node;
	typedef Ptr<Node> NodePtr;
	typedef Ptr<const Node> NodePtrConst;

	class NodeMap;
	typedef Ptr<NodeMap> NodeMapPtr;
	typedef Ptr<const NodeMap> NodeMapPtrConst;


	//! Information about the type of a node, to provide some means to the tools to deal generically
	//! with nodes.
	//! \ingroup tools
	struct NODE_TYPE
	{
		NODE_TYPE();
		NODE_TYPE( const char* strName, const NODE_TYPE* pParent );

		const char* m_strName;
		const NODE_TYPE* m_pParent;
	};



    //! %Base class for all graphs used in the source data to define models and transforms.
	class MUTABLETOOLS_API Node : public RefCounted
	{
	public:

		// Possible subclasses
		enum class EType : uint8
		{
			Colour = 0,
			Component = 1,
			Image = 2,
			Layout = 3,
			LOD = 4,
			Mesh = 5,
			Object = 6,
			PatchImage = 8,
			Scalar = 9,
			PatchMesh = 13,
			Volume_Deprecated = 14,
			Projector = 15,
			Surface = 16,
			Modifier = 18,
			Range = 19,
			String = 20,
			Bool = 21,

			None
		};

		//-----------------------------------------------------------------------------------------
		// Life cycle
		//-----------------------------------------------------------------------------------------

		static void Serialise( const Node* pNode, OutputArchive& arch );
		virtual void SerialiseWrapper(OutputArchive& arch) const = 0;
		static NodePtr StaticUnserialise( InputArchive& arch );


		//-----------------------------------------------------------------------------------------
		// Own Interface
		//-----------------------------------------------------------------------------------------

		//! Clone the node tree.
		//! \param pMap is an optional parameter that will return a map from the source tree nodes
		//! to the cloned tree nodes.
        virtual NodePtr Clone( NodeMapPtr pMap = nullptr ) const = 0;

		//! Node type hierarchy data.
        virtual const NODE_TYPE* GetType() const;
		static const NODE_TYPE* GetStaticType();

		//! Generic access to graph connections
        virtual int GetInputCount() const = 0;
		virtual Node* GetInputNode( int i ) const = 0;
        virtual void SetInputNode( int i, NodePtr pNode ) = 0;

		//! Set the opaque context returned in messages in the compiler log.
		void SetMessageContext( const void* context );

		//-----------------------------------------------------------------------------------------
        // Interface pattern
		//-----------------------------------------------------------------------------------------
		class Private;
		virtual Private* GetBasePrivate() const = 0;

	protected:

		inline ~Node() {}

		//!
		EType Type = EType::None;

	};


	//! Container mapping nodes to nodes.
	class MUTABLETOOLS_API NodeMap : public RefCounted
	{
	public:

		NodeMap();

		//-----------------------------------------------------------------------------------------
		// Own Interface
		//-----------------------------------------------------------------------------------------

		//! Return the number of elements in the map
		int GetSize() const;

		//! Insert a new key-value pair or overwrite the previews value with that key
		void Add( const void* key, NodePtr value );

		//! Get the value for a particular key. Returns 0 if the key is not in the map.
		NodePtr Get( const void* key ) const;

		//-----------------------------------------------------------------------------------------
		// Interface pattern
		//-----------------------------------------------------------------------------------------
		class Private;
		virtual Private* GetPrivate() const;

	protected:

		//! Forbidden. Manage with the Ptr<> template.
		~NodeMap();

	private:

		Private* m_pD;

	};


}
