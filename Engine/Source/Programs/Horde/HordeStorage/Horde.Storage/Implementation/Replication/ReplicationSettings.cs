// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
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
        ///  Name of the current site, has to be globally unique across all deployments, used to avoid replication loops
        /// </summary>
        [Required]
        [Key]
        public string CurrentSite { get; set; } = "";

        /// <summary>
        /// The frequency at which to poll for new replication events
        /// </summary>
        [Required]
        [Range(15, int.MaxValue)]
        public int ReplicationPollFrequencySeconds { get; set; } = 15;
        
        [Required]
        // ReSharper disable once CollectionNeverUpdated.Global
        public List<ReplicatorSettings> Replicators { get; set; } = new List<ReplicatorSettings>();
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
