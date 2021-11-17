// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace Horde.Storage.Implementation
{
    public interface ILeaderElection
    {
        public class OnLeaderChangedEventArgs
        {
            public bool IsLeader { get; }
            public string LeaderName { get; }

            public OnLeaderChangedEventArgs(bool isLeader, string leaderName)
            {
                IsLeader = isLeader;
                LeaderName = leaderName;
            }
        }

        bool IsThisInstanceLeader();

        event EventHandler<OnLeaderChangedEventArgs>? OnLeaderChanged;
    }
    
    public class LeaderElectionStub : ILeaderElection
    {
        private readonly bool _isLeader;

        public LeaderElectionStub(bool isLeader)
        {
            _isLeader = isLeader;

            OnLeaderChanged?.Invoke(this, new ILeaderElection.OnLeaderChangedEventArgs(isLeader, ""));
        }

        public bool IsThisInstanceLeader()
        {
            return _isLeader;
        }

        public event EventHandler<ILeaderElection.OnLeaderChangedEventArgs>? OnLeaderChanged;
    }
}
