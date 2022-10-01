// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/NodeImage.h"


namespace mu
{

	// Forward definitions
	class NodeImageTable;
	typedef Ptr<NodeImageTable> NodeImageTablePtr;
	typedef Ptr<const NodeImageTable> NodeImageTablePtrConst;

	class Table;
	typedef Ptr<Table> TablePtr;
	typedef Ptr<const Table> TablePtrConst;


	//! This node provides the meshes stored in the column of a table.
	//! \ingroup transform
	class MUTABLETOOLS_API NodeImageTable : public NodeImage
	{
	public:

		//-----------------------------------------------------------------------------------------
		// Life cycle
		//-----------------------------------------------------------------------------------------

		NodeImageTable();

		void SerialiseWrapper(OutputArchive& arch) const override;
		static void Serialise( const NodeImageTable* pNode, OutputArchive& arch );
		static NodeImageTablePtr StaticUnserialise( InputArchive& arch );


		//-----------------------------------------------------------------------------------------
		// Node Interface
		//-----------------------------------------------------------------------------------------

        NodePtr Clone( NodeMapPtr pMap = 0 ) const override;

        const NODE_TYPE* GetType() const override;
		static const NODE_TYPE* GetStaticType();

        virtual int GetInputCount() const override;
        virtual Node* GetInputNode( int i ) const override;
        void SetInputNode( int i, NodePtr pNode ) override;

		//-----------------------------------------------------------------------------------------
		// Own Interface
		//-----------------------------------------------------------------------------------------

		//! Set the name of the implicit table parameter.
		void SetParameterName( const char* strName );

		//!
		TablePtr GetTable() const;
		void SetTable( TablePtr );

		//!
		const char* GetColumn() const;
		void SetColumn( const char* strName );

		//-----------------------------------------------------------------------------------------
		// Interface pattern
		//-----------------------------------------------------------------------------------------
		class Private;
		Private* GetPrivate() const;
        Node::Private* GetBasePrivate() const override;


	protected:

		//! Forbidden. Manage with the Ptr<> template.
		~NodeImageTable();

	private:

		Private* m_pD;

	};


}
