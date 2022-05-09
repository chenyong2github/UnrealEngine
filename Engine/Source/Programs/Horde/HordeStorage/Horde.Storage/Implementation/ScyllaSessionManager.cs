// Copyright Epic Games, Inc. All Rights Reserved.

using Cassandra;

namespace Horde.Storage.Implementation
{
    public interface IScyllaSessionManager
    {
        ISession GetSessionForReplicatedKeyspace();
        ISession GetSessionForLocalKeyspace();
    }

    public class ScyllaSessionManager : IScyllaSessionManager
    {
        private readonly ISession _replicatedSession;
        private readonly ISession _localSession;

        public ScyllaSessionManager(ISession replicatedSession, ISession localSession)
        {
            _replicatedSession = replicatedSession;
            _localSession = localSession;
        }

        public ISession GetSessionForReplicatedKeyspace()
        {
            return _replicatedSession;

        }

        public ISession GetSessionForLocalKeyspace()
        {
            return _localSession;
        }
    }
}
