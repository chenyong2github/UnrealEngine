// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using EpicGames.Horde.Storage;
using Horde.Storage.Implementation;
using Jupiter.Implementation;

namespace Horde.Storage
{
    // ReSharper disable once ClassNeverInstantiated.Global
    public class ReplicationSettings
    {
        /// <summary>
        /// Enable to start a replicating another Jupiter instance into this one
        /// </summary>
        public bool Enabled { get; set; } = false;

        /// <summary>
        /// Path to a directory were the local state can be kept
        /// </summary>
        [Required]
        public string StateRoot { get; set; } = "";

        /// <summary>
        /// Path to a directory where state used to be stored. State is migrated from old state root to the new during startup.
        /// </summary>
        public string OldStateRoot { get; set; } = "";

        /// <summary>
        /// The frequency at which to poll for new replication events
        /// </summary>
        [Required]
        [Range(15, int.MaxValue)]
        public int ReplicationPollFrequencySeconds { get; set; } = 15;
        
        [Required]
        // ReSharper disable once CollectionNeverUpdated.Global
        public List<ReplicatorSettings> Replicators { get; set; } = new List<ReplicatorSettings>();

        public string GetStateRoot()
        {
            return PathUtil.ResolvePath(StateRoot);
        }
    }

    public class ReplicatorSettings
    {
        /// <summary>
        /// The namespace which this replicator is replicating, will be used both on the source and destination side 
        /// </summary>
        [Required]
        public NamespaceId NamespaceToReplicate { get; set; }

        /// <summary>
        /// Name of this replicator instance, should be unique within the cluster
        /// </summary>
        [Required]
        [Key]
        public string ReplicatorName { get; set; } = "";

        /// <summary>
        /// The connection string to the remote jupiter instance which will be replicated
        /// </summary>
        [Required]
        [Url]
        public string ConnectionString { get; set; } = "";
        public ReplicatorVersion Version { get; set; } = ReplicatorVersion.V1;

        /// <summary>
        /// Max number of replications that can run in parallel, set to -1 to disable the limit and go as wide as possible
        /// </summary>
        public int MaxParallelReplications { get; set; } = 64;

        /// <summary>
        /// The replication will stop its run after this many events have been processed, yielding its time until the next time a replication is triggered.
        /// </summary>
        public int MaxReplicationsPerRun { get; set; } = 1000;

        /// <summary>
        /// Max number of offsets to skip when finding broken events in the transaction logs, higher value means we scan further increasing the likelihood of finding a valid entry, but also means risk that we skip records you would want.
        /// </summary>
        public int? MaxOffsetsAttempted { get; set; } = 100000;

        /// <summary>
        /// Do not read in old history using a snapshot, instead just start replicating the new incremental state. Means we will only have partial state but we will also get going quicker on relatively useful blobs.
        /// </summary>
        public bool SkipSnapshot { get; set; } = false;
    }

    public enum ReplicatorVersion
    {
        V1, 
        Refs
    }

    public class ServiceCredentialSettings
    {
        /// <summary>
        /// The client id to use for communication with other services
        /// </summary>
        public string OAuthClientId { get; set; } = "";

        /// <summary>
        /// The client secret to use for communication with other services
        /// </summary>
        public string OAuthClientSecret { get; set; } = "";

        /// <summary>
        /// The url to login for a token to use to connect to the other services. Set to empty to disable login, assuming a unprotected service.
        /// </summary>
        public string OAuthLoginUrl { get; set; } = "";

        /// <summary>
        /// The scope to request
        /// </summary>
        public string OAuthScope { get; set; } = "cache_access";
    }
}
