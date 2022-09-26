// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Operations.h"
#include "ModelPrivate.h"
#include "SystemPrivate.h"


#include <algorithm>


namespace mu
{

    //---------------------------------------------------------------------------------------------
    //! Decide what operations are an "add resource" since they are handled differently sometimes.
    //---------------------------------------------------------------------------------------------
    inline bool VisitorIsAddResource(const OP_TYPE& type)
    {
        return  type == OP_TYPE::IN_ADDIMAGE ||
                type == OP_TYPE::IN_ADDMESH;
    }



    //---------------------------------------------------------------------------------------------
    //! Code visitor that:
    //! - is top-down
    //! - cannot change the visited instructions.
    //! - will not visit twice the same instruction with the same state.
    //! - Its iterative
    //---------------------------------------------------------------------------------------------
    template<typename STATE=int>
    class UniqueConstCodeVisitorIterative : public Base
    {
    public:

        UniqueConstCodeVisitorIterative( bool skipResources=false )
        {
            // Default state
            m_states.push_back(STATE());
            m_currentState = 0;
            m_skipResources = skipResources;
        }

        //! Ensure virtual destruction
        virtual ~UniqueConstCodeVisitorIterative() {}

    protected:

        //!
        void SetDefaultState(const STATE& s)
        {
            m_states[0] = s;
        }

        //!
        const STATE& GetDefaultState() const
        {
            return m_states[0];
        }

        //! Use this from visit to access the state at the time of processing the current
        //! instruction.
        STATE GetCurrentState() const
        {
            return m_states[m_currentState];
        }

        //! For manual recursion that changes the state for a specific path.
        void RecurseWithState(OP::ADDRESS at, const STATE& newState)
        {
            auto it = std::find(m_states.begin(),m_states.end(),newState);
            if (it==m_states.end())
            {
                m_states.push_back(newState);
                it = m_states.end() - 1;
            }
            int stateIndex = (int)(it - m_states.begin());

            m_pending.push_back( PENDING(at,stateIndex) );
        }

        //! For manual recursion that doesn't change the state for a specific path.
        void RecurseWithCurrentState(OP::ADDRESS at)
        {
            m_pending.push_back( PENDING(at,m_currentState) );
        }

        //! Can be called from visit to set the state to visit all children ops
        void SetCurrentState(const STATE& newState)
        {
            auto it = std::find(m_states.begin(),m_states.end(),newState);
            if (it==m_states.end())
            {
                m_states.push_back(newState);
                it = m_states.end() - 1;
            }
            m_currentState = (int)(it - m_states.begin());
        }


        void Traverse( OP::ADDRESS root, PROGRAM& program, bool visitDecorators = true )
        {
            m_pending.reserve( program.m_opAddress.size() );

            // Visit the given root
            m_pending.push_back( PENDING(root,0) );
            Recurse( program );

            if (visitDecorators)
            {
                // Fix the code used in the parameter descriptions
                for ( std::size_t p=0; p<program.m_parameters.size(); ++p )
                {
                    for ( std::size_t d=0; d<program.m_parameters[p].m_descImages.size(); ++d )
                    {
                        m_pending.push_back(PENDING(program.m_parameters[p].m_descImages[d],0) );
                        Recurse( program );
                    }
                }
            }
        }

        void FullTraverse( PROGRAM& program, bool visitDecorators = true )
        {
            // Visit all the state roots
            for ( std::size_t p=0; p<program.m_states.size(); ++p )
            {
                m_pending.push_back(PENDING(program.m_states[p].m_root,0) );
                Recurse( program );
            }

            if (visitDecorators)
            {
                // Fix the code used in the parameter descriptions
                for ( std::size_t p=0; p<program.m_parameters.size(); ++p )
                {
                    for ( std::size_t d=0; d<program.m_parameters[p].m_descImages.size(); ++d )
                    {
                        m_pending.push_back(PENDING(program.m_parameters[p].m_descImages[d],0) );
                        Recurse( program );
                    }
                }
            }
        }


    private:

        //! Do the actual work by overriding this in the derived classes.
        //! Return true if the traverse has to continue with the children of "at"
        virtual bool Visit( OP::ADDRESS at, PROGRAM& program ) = 0;

        //! Operations to be processed
        struct PENDING
        {
            PENDING()
            {
                at = 0;
                stateIndex = 0;
            }
            PENDING(OP::ADDRESS _at, int _stateIndex)
            {
                at = _at;
                stateIndex = _stateIndex;
            }

            OP::ADDRESS at;
            int stateIndex;
        };
        vector<PENDING> m_pending;

        //! States found so far
        vector<STATE> m_states;

        //! Index of the current state, from the m_states vector.
        int m_currentState;

        //! If true, operations adding resources (meshes or images) will only
        //! have the base operation recursed, but not the resources.
        bool m_skipResources;

        //! Array of states visited for each operation.
        //! Empty vector means operation not visited at all.
        vector<vector<int>> m_visited;

        //! Process all the pending operations and visit all children if necessary
        void Recurse( PROGRAM& program )
        {
            m_visited = vector<vector<int>>(program.m_opAddress.size());

            while ( m_pending.size() )
            {
                OP::ADDRESS at = m_pending.back().at;
                m_currentState = m_pending.back().stateIndex;
                m_pending.pop_back();

                bool recurse = false;

                bool visitedInThisState = std::find(m_visited[at].begin(), m_visited[at].end(), m_currentState)
                    !=
                    m_visited[at].end();
                if (!visitedInThisState)
                {
                    m_visited[at].push_back(m_currentState);

                    // Visit may change current state
                    recurse = Visit( at, program );
                }

                if (recurse)
                {
                    ForEachReference( program, at, [&](OP::ADDRESS ref)
                    {
                        if (ref)
                        {
                            m_pending.push_back( PENDING(ref,m_currentState) );
                        }
                    });
                }
            }
        }

    };


    //---------------------------------------------------------------------------------------------
    //! Code visitor that:
    //! - is top-down
    //! - cannot change the instructions.
    //! - will repeat visits to the instructions that have multiple references.
    //! - Its iterative
    //---------------------------------------------------------------------------------------------
    template<class STATE=int>
    class RepeatConstCodeVisitorIterative : public Base
    {
    public:

        RepeatConstCodeVisitorIterative( bool skipResources=false )
        {
            // Default state
            m_states.push_back(STATE());
            m_currentState = 0;
            m_skipResources = skipResources;
        }

        //! Ensure virtual destruction
        virtual ~RepeatConstCodeVisitorIterative() {}

    protected:

        //!
        void SetDefaultState(const STATE& s)
        {
            m_states[0] = s;
        }

        //!
        const STATE& GetDefaultState() const
        {
            return m_states[0];
        }

        //! Use this from visit to access the state at the time of processing the current
        //! instruction.
        const STATE& GetCurrentState() const
        {
            return m_states[m_currentState];
        }

        //! For manual recursion that changes the state for a specific path.
        void RecurseWithState(OP::ADDRESS at, const STATE& newState)
        {
            auto it = std::find(m_states.begin(),m_states.end(),newState);
            if (it==m_states.end())
            {
                m_states.push_back(newState);
                it = m_states.end() - 1;
            }
            int stateIndex = (int)(it - m_states.begin());

            //check(at<1000000);
            m_pending.push_back( PENDING(at,stateIndex) );
        }

        //! For manual recursion that doesn't change the state for a specific path.
        void RecurseWithCurrentState(OP::ADDRESS at)
        {
            //check(at<1000000);
            m_pending.push_back( PENDING(at,m_currentState) );
        }

        //! Can be called from visit to set the state to visit all children ops
        void SetCurrentState(const STATE& newState)
        {
            auto it = std::find(m_states.begin(),m_states.end(),newState);
            if (it==m_states.end())
            {
                m_states.push_back(newState);
                it = m_states.end() - 1;
            }
            m_currentState = (int)(it - m_states.begin());
        }


        void Traverse( OP::ADDRESS root, PROGRAM& program, bool visitDecorators = true )
        {
            m_pending.reserve( program.m_opAddress.size() );

            // Visit the given root
            m_pending.push_back( PENDING(root,0) );
            Recurse( program );

            if (visitDecorators)
            {
                // Fix the code used in the parameter descriptions
                for ( std::size_t p=0; p<program.m_parameters.size(); ++p )
                {
                    for ( std::size_t d=0; d<program.m_parameters[p].m_descImages.size(); ++d )
                    {
                        m_pending.push_back(PENDING(program.m_parameters[p].m_descImages[d],0) );
                        Recurse( program );
                    }
                }
            }
        }

        void FullTraverse( PROGRAM& program, bool visitDecorators = true )
        {
            // Visit all the state roots
            for ( std::size_t p=0; p<program.m_states.size(); ++p )
            {
                m_pending.push_back(PENDING(program.m_states[p].m_root,0) );
                Recurse( program );
            }

            if (visitDecorators)
            {
                // Fix the code used in the parameter descriptions
                for ( std::size_t p=0; p<program.m_parameters.size(); ++p )
                {
                    for ( std::size_t d=0; d<program.m_parameters[p].m_descImages.size(); ++d )
                    {
                        m_pending.push_back(PENDING(program.m_parameters[p].m_descImages[d],0) );
                        Recurse( program );
                    }
                }
            }
        }


    private:

        //! Do the actual work by overriding this in the derived classes.
        //! Return true if the traverse has to continue with the children of "at"
        virtual bool Visit( OP::ADDRESS at, PROGRAM& program ) = 0;

        //! Operations to be processed
        struct PENDING
        {
            PENDING()
            {
                at = 0;
                stateIndex = 0;
            }
            PENDING(OP::ADDRESS _at, int _stateIndex)
            {
                at = _at;
                stateIndex = _stateIndex;
            }

            OP::ADDRESS at;
            int stateIndex;
        };
        vector<PENDING> m_pending;

        //! States found so far
        vector<STATE> m_states;

        //! Index of the current state, from the m_states vector.
        int m_currentState;

        //! If true, operations adding resources (meshes or images) will only
        //! have the base operation recursed, but not the resources.
        bool m_skipResources;


        //! Process all the pending operations and visit all children if necessary
        void Recurse( PROGRAM& program )
        {
            while ( m_pending.size() )
            {
                OP::ADDRESS at = m_pending.back().at;
                m_currentState = m_pending.back().stateIndex;
                m_pending.pop_back();

                bool recurse = false;

                // Visit may change current state
                recurse = Visit( at, program );

                if (recurse)
                {
                    if (m_skipResources && VisitorIsAddResource(program.GetOpType(at)))
                    {
                        auto args = program.GetOpArgs<OP::InstanceAddArgs>(at);

                        // Recurse only the base
                        OP::ADDRESS base = args.instance;
                        if (base)
                        {
							check(base<program.m_opAddress.size());
                            m_pending.push_back( PENDING(base,m_currentState) );
                        }
                    }
                    else
                    {
                        ForEachReference( program, at, [&](OP::ADDRESS ref)
                        {
                            if (ref)
                            {
								check(ref<program.m_opAddress.size());
                                m_pending.push_back( PENDING(ref,m_currentState) );
                            }
                        });
                    }
                }
            }
        }

    };


    //---------------------------------------------------------------------------------------------
    //! Code visitor template for visitors that:
    //! - only traverses the operations that are relevant for a given set of parameter values. It
    //! only considers the discrete parameters like integers and booleans. In the case of forks
    //! caused by continuous parameters like float weights for interpolation, all the branches are
    //! traversed.
    //! - cannot change the instructinos
    //---------------------------------------------------------------------------------------------
    struct COVERED_CODE_VISITOR_STATE
    {
        uint16_t m_underResourceCount = 0;

        bool operator==(const COVERED_CODE_VISITOR_STATE& o) const
        {
            return m_underResourceCount==o.m_underResourceCount;
        }
    };

    template<typename PARENT,typename STATE>
    class DiscreteCoveredCodeVisitorBase : public PARENT
    {
    public:

        DiscreteCoveredCodeVisitorBase
            (
                System::Private* pSystem,
                const ModelPtrConst& pModel,
                const ParametersPtrConst& pParams,
                unsigned lodMask,
                bool skipResources=false
            )
            : PARENT(skipResources)
        {
            m_pSystem = pSystem;
            m_pModel = pModel;
            m_pParams = pParams.get();
            m_lodMask = lodMask;

            // Visiting state
            PARENT::SetDefaultState( STATE() );
        }

        void Run( OP::ADDRESS at  )
        {
            PARENT::SetDefaultState( STATE() );

            PARENT::Traverse( at, m_pModel->GetPrivate()->m_program );
        }

    protected:

        virtual bool Visit( OP::ADDRESS at, PROGRAM& program )
        {
            bool recurse = true;

            OP_TYPE type = program.GetOpType(at);

            switch ( type )
            {
            case OP_TYPE::NU_CONDITIONAL:
            case OP_TYPE::SC_CONDITIONAL:
            case OP_TYPE::CO_CONDITIONAL:
            case OP_TYPE::IM_CONDITIONAL:
            case OP_TYPE::ME_CONDITIONAL:
            case OP_TYPE::LA_CONDITIONAL:
            case OP_TYPE::IN_CONDITIONAL:
            {
                auto args = program.GetOpArgs<OP::ConditionalArgs>(at);

                recurse = false;

                PARENT::RecurseWithCurrentState( args.condition );

                // If there is no expression, we'll assume true.
                bool value = true;

                if (args.condition)
                {
                    value = m_pSystem->BuildBool(m_pModel, m_pParams, args.condition);
                }

                if (value)
                {
                    PARENT::RecurseWithCurrentState( args.yes );
                }
                else
                {
                    PARENT::RecurseWithCurrentState( args.no );
                }
                break;
            }

            case OP_TYPE::NU_SWITCH:
            case OP_TYPE::SC_SWITCH:
            case OP_TYPE::CO_SWITCH:
            case OP_TYPE::IM_SWITCH:
            case OP_TYPE::ME_SWITCH:
            case OP_TYPE::LA_SWITCH:
            case OP_TYPE::IN_SWITCH:
            {
                recurse = false;

				const uint8_t* data = program.GetOpArgsPointer(at);
				
				OP::ADDRESS VarAddress;
				FMemory::Memcpy( &VarAddress, data, sizeof(OP::ADDRESS));
				data += sizeof(OP::ADDRESS);

                if (VarAddress)
                {
					OP::ADDRESS DefAddress;
					FMemory::Memcpy( &DefAddress, data, sizeof(OP::ADDRESS));
					data += sizeof(OP::ADDRESS);

					uint32_t CaseCount;
					FMemory::Memcpy( &CaseCount, data, sizeof(uint32_t));
					data += sizeof(uint32_t);

                    PARENT::RecurseWithCurrentState( VarAddress );

                    int var = m_pSystem->BuildInt( m_pModel, m_pParams, VarAddress );

					OP::ADDRESS valueAt = DefAddress;
					for (uint32_t C = 0; C < CaseCount; ++C)
					{
						int32_t Condition;
						FMemory::Memcpy( &Condition, data, sizeof(int32_t));		
						data += sizeof(int32_t);

						OP::ADDRESS At;
						FMemory::Memcpy( &At, data, sizeof(OP::ADDRESS));
						data += sizeof(OP::ADDRESS);

						if (At && var == (int)Condition)
						{
							valueAt = At;
							break;	
						}
					}

					PARENT::RecurseWithCurrentState( valueAt );
                }

                break;
            }


            case OP_TYPE::IN_ADDLOD:
            {
                auto args = program.GetOpArgs<OP::InstanceAddLODArgs>(at);

                recurse = false;

                STATE newState = PARENT::GetCurrentState();
                for (int t=0;t<MUTABLE_OP_MAX_ADD_COUNT;++t)
                {
                    OP::ADDRESS lodAt = args.lod[t];
                    if (lodAt)
                    {
                        bool selected = ( (1<<t) & m_lodMask ) != 0;
                        if ( selected )
                        {
                            PARENT::RecurseWithState( lodAt, newState );
                        }
                    }
                }
                break;
            }


            case OP_TYPE::IN_ADDMESH:
            {
                auto args = program.GetOpArgs<OP::InstanceAddArgs>(at);

                recurse = false;

                PARENT::RecurseWithCurrentState(args.instance);

                STATE newState = PARENT::GetCurrentState();
                newState.m_underResourceCount=1;

                OP::ADDRESS meshAt = args.value;
                if (meshAt)
                {
                    PARENT::RecurseWithState(meshAt, newState);
                }
                break;
            }


            case OP_TYPE::IN_ADDIMAGE:
            {
                auto args = program.GetOpArgs<OP::InstanceAddArgs>(at);

                recurse = false;

                PARENT::RecurseWithCurrentState(args.instance);

                STATE newState = PARENT::GetCurrentState();
                newState.m_underResourceCount=1;

                OP::ADDRESS imageAt = args.value;
                if (imageAt)
                {
                    PARENT::RecurseWithState(imageAt, newState);
                }
                break;
            }

            default:
                break;
            }

            return recurse;
        }


    protected:
        System::Private* m_pSystem = nullptr;
        ModelPtrConst m_pModel;
        const Parameters* m_pParams = nullptr;
        unsigned m_lodMask = 0;
    };


    //---------------------------------------------------------------------------------------------
    //! Code visitor that:
    //! - only traverses the operations that are relevant for a given set of parameter values. It
    //! only considers the discrete parameters like integers and booleans. In the case of forks
    //! caused by continuous parameters like float weights for interpolation, all the branches are
    //! traversed.
    //! - cannot change the instructions
    //! - will not repeat visits to instructions with the same state
    //! - the state has to be a compatible with COVERED_CODE_VISITOR_STATE
    //---------------------------------------------------------------------------------------------
    template<typename COVERED_STATE=COVERED_CODE_VISITOR_STATE>
    class UniqueDiscreteCoveredCodeVisitor :
            public DiscreteCoveredCodeVisitorBase
            <
            UniqueConstCodeVisitorIterative<COVERED_STATE>,
            COVERED_STATE
            >
    {
        using PARENT=DiscreteCoveredCodeVisitorBase<
        UniqueConstCodeVisitorIterative<COVERED_STATE>,
        COVERED_STATE
        >;
    public:

        UniqueDiscreteCoveredCodeVisitor
            (
                System::Private* pSystem,
                const ModelPtrConst& pModel,
                const ParametersPtrConst& pParams,
                unsigned lodMask
            )
            : PARENT( pSystem, pModel, pParams, lodMask )
        {
        }

    };



    //---------------------------------------------------------------------------------------------
    //! Calculate all the parameters found under a particular operation
    //! It has an internal cache, so don't reuse objects of this class if the program changes.
    //---------------------------------------------------------------------------------------------
    class MUTABLERUNTIME_API SubtreeParametersVisitor : public Base
    {
    public:

        void Run( OP::ADDRESS root, PROGRAM& program );

        //! After Run, list of relevant parameters.
        vector<int> m_params;

    private:

        vector<int> m_currentParams;
        vector<uint8_t> m_visited;
        vector<OP::ADDRESS> m_pending;

        // Result cache
        map< OP::ADDRESS, vector<int> > m_resultCache;
    };


}

