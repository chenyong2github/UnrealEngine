// Copyright Epic Games, Inc. All Rights Reserved.


#include "CodeVisitor.h"

#include "MutableTrace.h"

namespace mu
{


	//-------------------------------------------------------------------------------------------------
	//-------------------------------------------------------------------------------------------------
	//-------------------------------------------------------------------------------------------------
	void SubtreeParametersVisitor::Run(OP::ADDRESS root, PROGRAM& program)
	{
		// Cached?
		auto it = m_resultCache.find(root);
		if (it != m_resultCache.end())
		{
			m_params = it->second;
			return;
		}

		// Not cached
		{
			MUTABLE_CPUPROFILER_SCOPE(SubtreeParametersVisitor);

			m_visited.resize(program.m_opAddress.size());
			if (program.m_opAddress.size())
			{
				FMemory::Memzero(&m_visited[0], m_visited.size());
			}

			m_currentParams.resize(program.m_parameters.size());
			if (m_currentParams.size())
			{
				FMemory::Memzero(&m_currentParams[0], sizeof(int) * m_currentParams.size());
			}

			m_pending.reserve(program.m_opAddress.size() / 4);
			m_pending.push_back(root);

			while (m_pending.size())
			{
				OP::ADDRESS at = m_pending.back();
				m_pending.pop_back();

				if (!m_visited[at])
				{
					m_visited[at] = true;

					switch (program.GetOpType(at))
					{
					case OP_TYPE::NU_PARAMETER:
					case OP_TYPE::SC_PARAMETER:
					case OP_TYPE::BO_PARAMETER:
					case OP_TYPE::CO_PARAMETER:
					case OP_TYPE::PR_PARAMETER:
					case OP_TYPE::IM_PARAMETER:
						m_currentParams[program.GetOpArgs<OP::ParameterArgs>(at).variable]++;
						break;

					default:
						break;
					}

					ForEachReference(program, at, [&](OP::ADDRESS ref)
						{
							if (ref)
							{
								m_pending.push_back(ref);
							}
						});
				}
			}


			// Build result
			m_params.clear();
			for (size_t i = 0; i < m_currentParams.size(); ++i)
			{
				if (m_currentParams[i])
				{
					m_params.push_back(int(i));
				}
			}

			m_resultCache[root] = m_params;
		}
	}

}