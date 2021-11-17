// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.Linq;
using Microsoft.Extensions.Caching.Memory;

namespace Horde.Storage
{
    
    public class HordeStorageSettings
    {
        public enum RefDbImplementations
        {
            Mongo,
            Cosmos,
            DynamoDb

        }

        public enum TransactionLogWriterImplementations
        {
            Callisto,
            Memory
        }

        public enum ReplicationLogWriterImplementations
        {
            Memory,
            Scylla
        }

        public enum TreeStoreImplementations
        {
            Memory,
            DynamoDb
        }
        public enum TreeRootStoreImplementations
        {
            Memory,
            DynamoDb
        }

        public enum StorageBackendImplementations
        {
            S3,
            Azure,
            FileSystem,
            Memory
        }

        public enum ReferencesDbImplementations
        {
            Memory,
            Scylla
        }

        public enum ContentIdStoreImplementations
        {
            Memory,
            Scylla
        }

        public enum LeaderElectionImplementations
        {
            Static, 
            Kubernetes
        }
        
        public class ValidStorageBackend : ValidationAttribute
        {
            public override string FormatErrorMessage(string name)
            {
                return "Need to specify at least one storage backend. Valid ones are: " +
                       string.Join(", ", Enum.GetNames(typeof(StorageBackendImplementations)));
            }

            public override bool IsValid(object? value)
            {
                if (value == null) return true;
                return value is IEnumerable<string> backends && backends.All(x => Enum.TryParse(typeof(StorageBackendImplementations), x, true, out _));
            }
        }

        [ValidStorageBackend]
        public string[]? StorageImplementations { get; set; }

        [Required]
        public TransactionLogWriterImplementations TransactionLogWriterImplementation { get; set; } = TransactionLogWriterImplementations.Memory;

        [Required] public ReplicationLogWriterImplementations ReplicationLogWriterImplementation { get; set; } = ReplicationLogWriterImplementations.Memory;

        [Required] public RefDbImplementations RefDbImplementation { get; set; } = RefDbImplementations.Mongo;

        [Required]
        public TreeStoreImplementations TreeStoreImplementation { get; set; } = TreeStoreImplementations.Memory;

        [Required]
        public TreeRootStoreImplementations TreeRootStoreImplementation { get; set; } = TreeRootStoreImplementations.Memory;

        [Required]
        public ReferencesDbImplementations ReferencesDbImplementation { get; set; } = ReferencesDbImplementations.Memory;

        public LeaderElectionImplementations LeaderElectionImplementation { get; set; } = LeaderElectionImplementations.Static;
        public ContentIdStoreImplementations ContentIdStoreImplementation { get; set; } = ContentIdStoreImplementations.Memory;

        public int? MaxSingleBlobSize { get; set; } = null; // disable blob partitioning

        public int LastAccessRollupFrequencySeconds { get; set; } = 900; // 15 minutes

        public bool UseNewDDCEndpoints { get; set; } = false;
    }

    public class MongoSettings
    {
        [Required] public string ConnectionString { get; set; } = "";

        public bool RequireTls12 { get; set; } = true;
    }


    public class DynamoDbSettings
    {
        [Required] public string ConnectionString { get; set; } = "";

        public long ReadCapacityUnits { get; set; } = 100;
        public long WriteCapacityUnits { get; set; } = 20;

        public bool UseOndemandCapacityProvisioning { get; set; } = true;
        
        /// <summary>
        /// Endpoint name for DynamoDB Accelerator (DAX). Acts as a cache in front of DynamoDB to speed up requests
        /// Disabled when set to null.
        /// </summary>
        public string? DaxEndpoint { get; set; } = null;

        /// <summary>
        /// Enabling this will make Horde.Storage create missing tables on demand, works great for local tables but for global tables its easier to have terraform manage it
        /// </summary>
        public bool CreateTablesOnDemand { get; set; } = true;

        public static (string, int) ParseDaxEndpointAsHostPort(string endpoint)
        {
            if (!endpoint.Contains(":"))
            {
                return (endpoint, 8111);
            }

            string host = endpoint.Split(":")[0];
            int port = Convert.ToInt32(endpoint.Split(":")[1]);
            return (host, port);
        }
    }


    public class CosmosSettings
    {
        [Range(400, 10_000)] public int DefaultRU = 400;
    }

    public class CallistoTransactionLogSettings
    {
        [Required] public string ConnectionString { get; set; } = "";
    }

    public class MemoryCacheBlobSettings : MemoryCacheOptions
    {
        public bool Enabled { get; set; } = true;

        public bool EnableSlidingExpiry { get; set; } = true;
        public int SlidingExpirationMinutes { get; set; } = 60;
    }

    public class MemoryCacheTreeSettings : MemoryCacheOptions
    {
        public bool Enabled { get; set; } = true;
        public bool EnableSlidingExpiry { get; set; } = true;
        public int SlidingExpirationMinutes { get; set; } = 60;
    }

    public class MemoryCacheRefSettings : MemoryCacheOptions
    {
        public bool Enabled { get; set; } = true;
        public bool EnableSlidingExpiry { get; set; } = true;
        public int SlidingExpirationMinutes { get; set; } = 60;
    }


    public class AzureSettings
    {
        [Required] public string ConnectionString { get; set; } = "";
    }

    public class FilesystemSettings
    {
        [Required] public string RootDir { get; set; } = "";
        public ulong MaxSizeBytes { get; set; } = 500 * 1024 * 1024;
        public double TriggerThresholdPercentage { get; set; } = 0.95;
        public double TargetThresholdPercentage { get; set; } = 0.85;
    }

    public class S3Settings
    {
        [Required] public string ConnectionString  { get; set; } = "";

        [Required] public string BucketName { get; set; } = "";

        public bool ForceAWSPathStyle { get; set; }
        public bool CreateBucketIfMissing { get;set; } = true;

        // Options to disable setting of bucket access policies, useful for local testing as minio does not support them.
        public bool SetBucketPolicies { get; set; } = true;
    }

    public class GCSettings
    {
        public bool BlobCleanupServiceEnabled { get; set; } = true;
        
        public bool CleanOldRefRecords { get; set; } = false;
        public bool CleanOldBlobs { get; set; } = true;

        public TimeSpan LastAccessCutoff { get; set; } = TimeSpan.FromDays(14);

        public TimeSpan BlobCleanupPollFrequency { get; set; } = TimeSpan.FromMinutes(60);
        public TimeSpan RefCleanupPollFrequency { get; set; } = TimeSpan.FromMinutes(60);
    }
}
