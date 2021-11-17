// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using Amazon.DAX;
using Amazon.DynamoDBv2;
using Amazon.Extensions.NETCore.Setup;
using Amazon.Runtime;
using Amazon.S3;
using Amazon.SecretsManager;
using Cassandra;
using Horde.Storage.Controllers;
using Horde.Storage.Implementation;
using Horde.Storage.Implementation.LeaderElection;
using Jupiter;
using Jupiter.Common.Implementation;
using Jupiter.Utils;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Routing;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Options;
using StatsdClient;

namespace Horde.Storage
{
    // ReSharper disable once ClassNeverInstantiated.Global
    public class HordeStorageStartup : BaseStartup
    {
        public HordeStorageStartup(IConfiguration configuration, IWebHostEnvironment environment) : base(configuration,
            environment)
        {
            string? ddAgentHost = System.Environment.GetEnvironmentVariable("DD_AGENT_HOST");
            if (!string.IsNullOrEmpty(ddAgentHost))
            {
                _logger.Information("Initializing Dogstatsd to connect to: {DatadogAgentHost}", ddAgentHost);
                StatsdConfig dogstatsdConfig = new StatsdConfig
                {
                    StatsdServerName = ddAgentHost,
                    StatsdPort = 8125,
                };

                DogStatsd.Configure(dogstatsdConfig);
            }
        }

        protected override void OnAddAuthorization(AuthorizationOptions authorizationOptions, List<string> defaultSchemes)
        {
            authorizationOptions.AddPolicy("Cache.read", policy =>
            {
                policy.AuthenticationSchemes = defaultSchemes;
                policy.RequireClaim("Cache", "read", "readwrite", "full");
            });

            authorizationOptions.AddPolicy("Cache.write", policy =>
            {
                policy.AuthenticationSchemes = defaultSchemes;
                policy.RequireClaim("Cache", "write", "readwrite", "full");
            });

            authorizationOptions.AddPolicy("Cache.delete", policy =>
            {
                policy.AuthenticationSchemes = defaultSchemes;
                policy.RequireClaim("Cache", "delete", "full");
            });

            authorizationOptions.AddPolicy("Object.read", policy =>
            {
                policy.AuthenticationSchemes = defaultSchemes;
                policy.RequireClaim("Cache", "read", "readwrite", "full");
            });

            authorizationOptions.AddPolicy("Object.write", policy =>
            {
                policy.AuthenticationSchemes = defaultSchemes;
                policy.RequireClaim("Cache", "write", "readwrite", "full");
            });

            authorizationOptions.AddPolicy("Object.delete", policy =>
            {
                policy.AuthenticationSchemes = defaultSchemes;
                policy.RequireClaim("Cache", "delete", "full");
            });

            authorizationOptions.AddPolicy("Storage.read", policy =>
            {
                policy.AuthenticationSchemes = defaultSchemes;
                policy.RequireClaim("Storage", "read", "readwrite", "full");
            });

            authorizationOptions.AddPolicy("Storage.write", policy =>
            {
                policy.AuthenticationSchemes = defaultSchemes;
                policy.RequireClaim("Storage", "write", "readwrite", "full");
            });

            authorizationOptions.AddPolicy("Storage.delete", policy =>
            {
                policy.AuthenticationSchemes = defaultSchemes;
                policy.RequireClaim("Storage", "delete", "full");
            });

            authorizationOptions.AddPolicy("replication-log.read", policy =>
            {
                policy.AuthenticationSchemes = defaultSchemes;
                policy.RequireClaim("transactionLog", "read", "readwrite", "full");
            });

            authorizationOptions.AddPolicy("replication-log.write", policy =>
            {
                policy.AuthenticationSchemes = defaultSchemes;
                policy.RequireClaim("transactionLog", "write", "readwrite", "full");
            });


            authorizationOptions.AddPolicy("Admin", policy =>
            {
                policy.AuthenticationSchemes = defaultSchemes;
                policy.RequireClaim("Admin");
            });

            // A policy that grants any authenticated user access
            authorizationOptions.AddPolicy("Any", policy =>
            {
                policy.RequireAuthenticatedUser();
            });
        }

        protected override void OnUseEndpoints(IWebHostEnvironment env, IEndpointRouteBuilder endpoints)
        {
            HordeStorageSettings settings = endpoints.ServiceProvider.GetService<IOptionsMonitor<HordeStorageSettings>>()!.CurrentValue!;

            if (settings.UseNewDDCEndpoints)
            {
                DDCEndpoints ddcEndpoints = endpoints.ServiceProvider.GetService<DDCEndpoints>()!;

                endpoints.Map(ddcEndpoints.GetRawRoute, ddcEndpoints.GetRaw).RequireAuthorization("Cache.read");
            }
        }

        protected override void OnAddService(IServiceCollection services)
        {
            // For requests served on the high-perf HTTP port there is not authn/authz.
            // Suppressing the check below removes the warning for that case.
            services.Configure<RouteOptions>(options =>
            {
                options.SuppressCheckForUnhandledSecurityMetadata = true;
            });

            services.AddHttpClient();

            services.AddOptions<HordeStorageSettings>().Bind(Configuration.GetSection("Horde.Storage")).ValidateDataAnnotations();
            services.AddOptions<MongoSettings>().Bind(Configuration.GetSection("Mongo")).ValidateDataAnnotations();
            services.AddOptions<CosmosSettings>().Bind(Configuration.GetSection("Cosmos")).ValidateDataAnnotations();
            services.AddOptions<DynamoDbSettings>().Bind(Configuration.GetSection("DynamoDb")).ValidateDataAnnotations();

            services.AddOptions<CallistoTransactionLogSettings>().Bind(Configuration.GetSection("Callisto")).ValidateDataAnnotations();

            services.AddOptions<MemoryCacheBlobSettings>().Bind(Configuration.GetSection("Cache.Blob")).ValidateDataAnnotations();
            services.AddOptions<MemoryCacheRefSettings>().Bind(Configuration.GetSection("Cache.Db")).ValidateDataAnnotations();

            services.AddOptions<S3Settings>().Bind(Configuration.GetSection("S3")).ValidateDataAnnotations();
            services.AddOptions<AzureSettings>().Bind(Configuration.GetSection("Azure")).ValidateDataAnnotations();
            services.AddOptions<FilesystemSettings>().Bind(Configuration.GetSection("Filesystem")).ValidateDataAnnotations();

            services.AddOptions<ConsistencyCheckSettings>().Bind(Configuration.GetSection("ConsistencyCheck")).ValidateDataAnnotations();

            services.AddOptions<GCSettings>().Configure(o => Configuration.GetSection("GC").Bind(o)).ValidateDataAnnotations();
            services.AddOptions<ReplicationSettings>().Configure(o => Configuration.GetSection("Replication").Bind(o)).ValidateDataAnnotations();
            services.AddOptions<ServiceCredentialSettings>().Configure(o => Configuration.GetSection("ServiceCredentials").Bind(o)).ValidateDataAnnotations();
            services.AddOptions<SnapshotSettings>().Configure(o => Configuration.GetSection("Snapshot").Bind(o)).ValidateDataAnnotations();

            services.AddOptions<ScyllaSettings>().Configure(o => Configuration.GetSection("Scylla").Bind(o)).ValidateDataAnnotations();

            services.AddOptions<KubernetesLeaderElectionSettings>().Configure(o => Configuration.GetSection("Kubernetes").Bind(o)).ValidateDataAnnotations();


            services.AddSingleton(typeof(OodleCompressor), CreateOodleCompressor);
            services.AddSingleton(typeof(CompressedBufferUtils), CreateCompressedBufferUtils);

            services.AddSingleton<AWSCredentials>(provider =>
            {
                AWSCredentialsSettings awsSettings = provider.GetService<IOptionsMonitor<AWSCredentialsSettings>>()!.CurrentValue;

                return AWSCredentialsHelper.GetCredentials(awsSettings, "Horde.Storage");
            });
            services.AddSingleton(typeof(IAmazonDynamoDB), AddDynamo);
            
            services.AddSingleton<BlobCleanupService>();
            services.AddHostedService<BlobCleanupService>(p => p.GetService<BlobCleanupService>()!);

            services.AddSingleton(serviceType: typeof(IDDCRefService), typeof(DDCRefService));
            services.AddSingleton(serviceType: typeof(DDCEndpoints));

            services.AddSingleton(serviceType: typeof(IObjectService), typeof(ObjectService));
            services.AddSingleton(serviceType: typeof(IReferencesStore), ObjectStoreFactory);
            services.AddSingleton(serviceType: typeof(IReferenceResolver), typeof(ReferenceResolver));
            services.AddSingleton(serviceType: typeof(IContentIdStore), ContentIdStoreFactory);

            services.AddSingleton(serviceType: typeof(ITransactionLogWriter), TransactionLogWriterFactory);
            services.AddSingleton(serviceType: typeof(IAmazonS3), CreateS3);
            services.AddSingleton(serviceType: typeof(IBlobStore), StorageBackendFactory);

            services.AddSingleton<AmazonS3Store>();
            services.AddSingleton<FileSystemStore>();
            services.AddSingleton<AzureBlobStore>();
            services.AddSingleton<MemoryCacheBlobStore>();

            services.AddSingleton(serviceType: typeof(IScyllaSessionManager), ScyllaFactory);

            services.AddSingleton(serviceType: typeof(IReplicationLog), ReplicationLogWriterFactory);
            
            LastAccessTrackerRefRecord lastAccessTracker = new LastAccessTrackerRefRecord();
            services.AddSingleton(serviceType: typeof(ILastAccessCache<RefRecord>), lastAccessTracker);
            services.AddSingleton(serviceType: typeof(ILastAccessTracker<RefRecord>), lastAccessTracker);

            services.AddSingleton(serviceType: typeof(ILeaderElection), CreateLeaderElection);

            services.AddSingleton(Configuration);
            services.AddSingleton<DynamoNamespaceStore>();
            services.AddSingleton(RefStoreFactory);

            services.AddSingleton<FormatResolver>();

            services.AddSingleton(serviceType: typeof(ISecretResolver), typeof(SecretResolver));
            services.AddSingleton(typeof(IAmazonSecretsManager), CreateAWSSecretsManager);

            services.AddSingleton<LastAccessService>();
            services.AddHostedService<LastAccessService>(p => p.GetService<LastAccessService>()!);

            services.AddSingleton<IServiceCredentials, ServiceCredentials>(p => ActivatorUtilities.CreateInstance<ServiceCredentials>(p));
            
            services.AddSingleton<ReplicationService>();
            services.AddHostedService<ReplicationService>(p => p.GetService<ReplicationService>()!);

            services.AddSingleton<ReplicationSnapshotService>();
            services.AddHostedService<ReplicationSnapshotService>(p => p.GetService<ReplicationSnapshotService>()!);

            services.AddSingleton<ConsistencyCheckService>();
            services.AddHostedService<ConsistencyCheckService>(p => p.GetService<ConsistencyCheckService>()!);
        
            services.AddTransient(typeof(IRefCleanup), typeof(RefCleanup));

            services.AddTransient(typeof(VersionFile), typeof(VersionFile));

            services.AddSingleton<RefCleanupService>();
            services.AddHostedService<RefCleanupService>(p => p.GetService<RefCleanupService>()!);

            // This will technically create a new settings object, but as we do not dynamically update it the settings will reflect what we actually want
            HordeStorageSettings settings = services.BuildServiceProvider().GetService<IOptionsMonitor<HordeStorageSettings>>()!.CurrentValue!;

            if (settings.LeaderElectionImplementation == HordeStorageSettings.LeaderElectionImplementations.Kubernetes)
            {
                // add the kubernetes leader instance under its actual type as well to make it easier to find
                services.AddSingleton<KubernetesLeaderElection>(p => (KubernetesLeaderElection)p.GetService<ILeaderElection>()!);
                services.AddHostedService<KubernetesLeaderElection>(p => p.GetService<KubernetesLeaderElection>()!);
            }
        }

        private IAmazonSecretsManager CreateAWSSecretsManager(IServiceProvider provider)
        {
            AWSCredentials awsCredentials = provider.GetService<AWSCredentials>()!;
            AWSOptions awsOptions = Configuration.GetAWSOptions();
            awsOptions.Credentials = awsCredentials;

            IAmazonSecretsManager serviceClient = awsOptions.CreateServiceClient<IAmazonSecretsManager>();
            return serviceClient;
        }

        private CompressedBufferUtils CreateCompressedBufferUtils(IServiceProvider provider)
        {
            return ActivatorUtilities.CreateInstance<CompressedBufferUtils>(provider);
        }

        private object CreateOodleCompressor(IServiceProvider provider)
        {
            OodleCompressor compressor = new OodleCompressor();
            compressor.InitializeOodle();
            return compressor;
        }

        private object ContentIdStoreFactory(IServiceProvider provider)
        {
            HordeStorageSettings settings = provider.GetService<IOptionsMonitor<HordeStorageSettings>>()!.CurrentValue!;
            switch (settings.ContentIdStoreImplementation)
            {
                case HordeStorageSettings.ContentIdStoreImplementations.Memory:
                    return ActivatorUtilities.CreateInstance<MemoryContentIdStore>(provider);
                case HordeStorageSettings.ContentIdStoreImplementations.Scylla:
                    return ActivatorUtilities.CreateInstance<ScyllaContentIdStore>(provider);
                default:
                    throw new ArgumentOutOfRangeException();
            }
        }

        private IScyllaSessionManager ScyllaFactory(IServiceProvider provider)
        {
            ScyllaSettings settings = provider.GetService<IOptionsMonitor<ScyllaSettings>>()!.CurrentValue!;

            Serilog.Extensions.Logging.SerilogLoggerProvider serilogLoggerProvider = new();
            Diagnostics.AddLoggerProvider(serilogLoggerProvider);

            const string DefaultKeyspaceName = "jupiter";
            // Configure the builder with your cluster's contact points
            var cluster = Cluster.Builder()
                .AddContactPoints(settings.ContactPoints)
                .WithLoadBalancingPolicy(new DefaultLoadBalancingPolicy(settings.LocalDatacenterName))
                .WithPoolingOptions(PoolingOptions.Create().SetMaxConnectionsPerHost(HostDistance.Local, settings.MaxConnectionForLocalHost))
                .WithDefaultKeyspace(DefaultKeyspaceName)
                .WithExecutionProfiles(options => 
                    options.WithProfile("default", builder => builder.WithConsistencyLevel(ConsistencyLevel.LocalOne)))
                .Build();

            Dictionary<string, string> replicationStrategy = ReplicationStrategies.CreateSimpleStrategyReplicationProperty(2);
            if (settings.KeyspaceReplicationStrategy != null)
            {
                replicationStrategy = settings.KeyspaceReplicationStrategy;
                _logger.Information("Scylla Replication strategy for replicated keyspace is set to {@ReplicationStrategy}", replicationStrategy);
            }
            ISession replicatedSession = cluster.ConnectAndCreateDefaultKeyspaceIfNotExists(replicationStrategy);

            replicatedSession.Execute(new SimpleStatement("CREATE TYPE IF NOT EXISTS blob_identifier (hash blob)"));
            replicatedSession.UserDefinedTypes.Define(UdtMap.For<ScyllaBlobIdentifier>("blob_identifier", DefaultKeyspaceName));

            string localKeyspaceName = $"jupiter_local_{settings.LocalKeyspaceSuffix}";

            Dictionary<string, string> replicationStrategyLocal = ReplicationStrategies.CreateSimpleStrategyReplicationProperty(2);
            if (settings.LocalKeyspaceReplicationStrategy != null)
            {
                replicationStrategyLocal = settings.LocalKeyspaceReplicationStrategy;
                _logger.Information("Scylla Replication strategy for local keyspace is set to {@ReplicationStrategy}", replicationStrategyLocal);
            }

            // the local keyspace is never replicated so we do not support controlling how the replication strategy is setup
            replicatedSession.CreateKeyspaceIfNotExists(localKeyspaceName, replicationStrategyLocal);
            ISession localSession = cluster.Connect(localKeyspaceName);

            localSession.Execute(new SimpleStatement("CREATE TYPE IF NOT EXISTS blob_identifier (hash blob)"));
            localSession.UserDefinedTypes.Define(UdtMap.For<ScyllaBlobIdentifier>("blob_identifier", localKeyspaceName));

            return new ScyllaSessionManager(replicatedSession, localSession);
        }

        private IReferencesStore ObjectStoreFactory(IServiceProvider provider)
        {
            HordeStorageSettings settings = provider.GetService<IOptionsMonitor<HordeStorageSettings>>()!.CurrentValue!;
            switch (settings.ReferencesDbImplementation)
            {
                case HordeStorageSettings.ReferencesDbImplementations.Memory:
                    return ActivatorUtilities.CreateInstance<MemoryReferencesStore>(provider);
                case HordeStorageSettings.ReferencesDbImplementations.Scylla:
                    return ActivatorUtilities.CreateInstance<ScyllaReferencesStore>(provider);
                default:
                    throw new ArgumentOutOfRangeException();
            }
        }

        protected override void OnConfigureAppEarly(IApplicationBuilder app, IWebHostEnvironment env)
        {
            HordeStorageSettings settings = app.ApplicationServices.GetService<IOptionsMonitor<HordeStorageSettings>>()!.CurrentValue!;
            // Register two methods as middlewares for benchmarking purposes
            // By circumventing the full ASP.NET stack we can measure the performance difference between these calls.
            if (!settings.UseNewDDCEndpoints)
            {
                DDCRefController ddcRefController = ActivatorUtilities.CreateInstance<DDCRefController>(app.ApplicationServices);
                DebugController debugController = ActivatorUtilities.CreateInstance<DebugController>(app.ApplicationServices);
                app.Use(ddcRefController.FastGet);
                app.Use(debugController.FastGetBytes);
            }
        }

        private ILeaderElection CreateLeaderElection(IServiceProvider provider)
        {
            HordeStorageSettings settings = provider.GetService<IOptionsMonitor<HordeStorageSettings>>()!.CurrentValue!;
            if (settings.LeaderElectionImplementation == HordeStorageSettings.LeaderElectionImplementations.Kubernetes)
            {
                return ActivatorUtilities.CreateInstance<KubernetesLeaderElection>(provider);
            }
            else
            {
                // hard coded leader election that assumes it is always the leader
                return ActivatorUtilities.CreateInstance<LeaderElectionStub>(provider, true);
            }
        }

        private IAmazonS3 CreateS3(IServiceProvider provider)
        {
            HordeStorageSettings settings = provider.GetService<IOptionsMonitor<HordeStorageSettings>>()!.CurrentValue!;

            if (settings.StorageImplementations == null)
            {
                throw new ArgumentException("No storage implementation set");
            }
            
            bool isS3InUse = settings.StorageImplementations.Any(x =>
                String.Equals(x, HordeStorageSettings.StorageBackendImplementations.S3.ToString(), StringComparison.CurrentCultureIgnoreCase));

            if (isS3InUse)
            {
                S3Settings s3Settings = provider.GetService<IOptionsMonitor<S3Settings>>()!.CurrentValue!;
                AWSCredentials awsCredentials = provider.GetService<AWSCredentials>()!;
                AWSOptions awsOptions = Configuration.GetAWSOptions();
                if (s3Settings.ConnectionString.ToUpper() != "AWS")
                {
                    awsOptions.DefaultClientConfig.ServiceURL = s3Settings.ConnectionString;
                }

                awsOptions.Credentials = awsCredentials;

                IAmazonS3 serviceClient = awsOptions.CreateServiceClient<IAmazonS3>();

                if (serviceClient.Config is AmazonS3Config c)
                {
                    if (s3Settings.ForceAWSPathStyle)
                    {
                        c.ForcePathStyle = true;
                    }

                }
                else
                {
                    throw new NotImplementedException();
                }

                return serviceClient;
            }

            return null!;
        }

        private IAmazonDynamoDB AddDynamo(IServiceProvider provider)
        {
            IOptionsMonitor<HordeStorageSettings> settings = provider.GetService<IOptionsMonitor<HordeStorageSettings>>()!;

            bool hasDynamo = settings.CurrentValue.RefDbImplementation == HordeStorageSettings.RefDbImplementations.DynamoDb ||
                             settings.CurrentValue.TreeRootStoreImplementation == HordeStorageSettings.TreeRootStoreImplementations.DynamoDb ||
                             settings.CurrentValue.TreeStoreImplementation == HordeStorageSettings.TreeStoreImplementations.DynamoDb;
            if (!hasDynamo)
            {
                return null!;
            }

            AWSCredentials awsCredentials = provider.GetService<AWSCredentials>()!;

            DynamoDbSettings dynamoDbSettings = provider.GetService<IOptionsMonitor<DynamoDbSettings>>()!.CurrentValue;

            AWSOptions awsOptions = Configuration.GetAWSOptions();
            if (dynamoDbSettings.ConnectionString.ToUpper() != "AWS")
                awsOptions.DefaultClientConfig.ServiceURL = dynamoDbSettings.ConnectionString;

            awsOptions.Credentials = awsCredentials;

            IAmazonDynamoDB serviceClient = awsOptions.CreateServiceClient<IAmazonDynamoDB>();

            if (!string.IsNullOrEmpty(dynamoDbSettings.DaxEndpoint))
            {
                (string daxHost, int daxPort) = DynamoDbSettings.ParseDaxEndpointAsHostPort(dynamoDbSettings.DaxEndpoint);
                DaxClientConfig daxConfig = new (daxHost, daxPort)
                {
                    AwsCredentials = awsOptions.Credentials
                };
                
                serviceClient = new ClusterDaxClient(daxConfig);
            }

            return serviceClient;
        }

        private ITransactionLogWriter TransactionLogWriterFactory(IServiceProvider provider)
        {
            HordeStorageSettings settings = provider.GetService<IOptionsMonitor<HordeStorageSettings>>()!.CurrentValue;
            switch (settings.TransactionLogWriterImplementation)
            {
                case HordeStorageSettings.TransactionLogWriterImplementations.Callisto:
                    return ActivatorUtilities.CreateInstance<CallistoTransactionLogWriter>(provider);
                case HordeStorageSettings.TransactionLogWriterImplementations.Memory:
                    return ActivatorUtilities.CreateInstance<MemoryTransactionLogWriter>(provider);
                default:
                    throw new NotImplementedException();
            }
        }

        private IReplicationLog ReplicationLogWriterFactory(IServiceProvider provider)
        {
            HordeStorageSettings settings = provider.GetService<IOptionsMonitor<HordeStorageSettings>>()!.CurrentValue;
            switch (settings.ReplicationLogWriterImplementation)
            {
                case HordeStorageSettings.ReplicationLogWriterImplementations.Scylla:
                    return ActivatorUtilities.CreateInstance<ScyllaReplicationLog>(provider);
                case HordeStorageSettings.ReplicationLogWriterImplementations.Memory:
                    return ActivatorUtilities.CreateInstance<MemoryReplicationLog>(provider);
                default:
                    throw new NotImplementedException();
            }
        }

        private HordeStorageSettings.StorageBackendImplementations[] GetStorageImplementationEnums(string[]? storageImpls)
        {
            storageImpls ??= new[] {HordeStorageSettings.StorageBackendImplementations.Memory.ToString()};
            return storageImpls.Select(x =>
            {
                if (Enum.TryParse(typeof(HordeStorageSettings.StorageBackendImplementations), x, true, out var backendType) && backendType is HordeStorageSettings.StorageBackendImplementations type)
                {
                    return type;
                }

                throw new ArgumentException($"Unable to find storage implementation for specified value '{x}'");
            }).ToArray();
        }
        
        private IBlobStore StorageBackendFactory(IServiceProvider provider)
        {
            IOptionsMonitor<HordeStorageSettings> settings = provider.GetService<IOptionsMonitor<HordeStorageSettings>>()!;
            IOptionsMonitor<GCSettings> gcSettings = provider.GetService<IOptionsMonitor<GCSettings>>()!;
            BlobCleanupService blobCleanupService = provider.GetService<BlobCleanupService>()!;

            List<IBlobStore> storageBackends = GetStorageImplementationEnums(settings.CurrentValue.StorageImplementations)
                    .Select<HordeStorageSettings.StorageBackendImplementations, IBlobStore?>(impl =>
                    {
                        switch (impl)
                        {
                            case HordeStorageSettings.StorageBackendImplementations.S3: return provider.GetService<AmazonS3Store>();
                            case HordeStorageSettings.StorageBackendImplementations.Azure: return provider.GetService<AzureBlobStore>();
                            case HordeStorageSettings.StorageBackendImplementations.Memory: return provider.GetService<MemoryCacheBlobStore>();
                            case HordeStorageSettings.StorageBackendImplementations.FileSystem:
                                FileSystemStore store = ActivatorUtilities.CreateInstance<FileSystemStore>(provider)!;
                                blobCleanupService.RegisterCleanup(store);
                                return store;
                            default: throw new NotImplementedException();
                        }
                    })
                    .Select(x => x ?? throw new ArgumentException("Unable to resolve all stores!"))
                    .ToList();

            // Only use the hierarchical backend if more than one backend is specified 
            IBlobStore blobStore = storageBackends.Count == 1 ? storageBackends[0] : new HierarchicalBlobStore(storageBackends);
            
            if (gcSettings.CurrentValue.CleanOldBlobs)
            {
                OrphanBlobCleanup orphanBlobCleanup = ActivatorUtilities.CreateInstance<OrphanBlobCleanup>(provider, blobStore);
                blobCleanupService.RegisterCleanup(orphanBlobCleanup);    
            }

            return blobStore;
        }

        private static IRefsStore RefStoreFactory(IServiceProvider provider)
        {
            HordeStorageSettings settings = provider.GetService<IOptionsMonitor<HordeStorageSettings>>()!.CurrentValue;

            IRefsStore refsStore = settings.RefDbImplementation switch
            {
                HordeStorageSettings.RefDbImplementations.DynamoDb => ActivatorUtilities.CreateInstance<DynamoDbRefsStore>(provider),
                HordeStorageSettings.RefDbImplementations.Mongo => ActivatorUtilities.CreateInstance<MongoRefsStore>(provider),
                HordeStorageSettings.RefDbImplementations.Cosmos => ActivatorUtilities.CreateInstance<CosmosRefsStore>(provider),
                _ => throw new NotImplementedException()
            };

            MemoryCacheRefSettings memoryCacheSettings = provider.GetService<IOptionsMonitor<MemoryCacheRefSettings>>()!.CurrentValue;

            if (memoryCacheSettings.Enabled)
            {
                refsStore = ActivatorUtilities.CreateInstance<CachedRefStore>(provider, refsStore);
            }

            return refsStore;
        }

        protected override void OnAddHealthChecks(IServiceCollection services, IHealthChecksBuilder healthChecks)
        {
            ServiceProvider provider = services.BuildServiceProvider();
            HordeStorageSettings settings = provider.GetService<IOptionsMonitor<HordeStorageSettings>>()!.CurrentValue;

            switch (settings.RefDbImplementation)
            {
                case HordeStorageSettings.RefDbImplementations.Mongo:
                case HordeStorageSettings.RefDbImplementations.Cosmos:
                    MongoSettings mongoSettings = provider.GetService<IOptionsMonitor<MongoSettings>>()!.CurrentValue;
                    healthChecks.AddMongoDb(mongoSettings.ConnectionString, tags: new[] {"services"});
                    break;
                case HordeStorageSettings.RefDbImplementations.DynamoDb:
                    // dynamo health tests disabled for now as they do not support specifying credentials that are automatically renewed
                    break;
                    /*
                    AWSCredentials awsCredentials = provider.GetService<AWSCredentials>()!;
                    IAmazonDynamoDB amazonDynamoDb = provider.GetService<IAmazonDynamoDB>()!;

                    // if the region endpoint is null, e.g. we are using a local dynamo then we do not create health checks as they do not support this
                    if (amazonDynamoDb.Config.RegionEndpoint != null)
                    {
                        healthChecks.AddDynamoDb(options =>
                        {
                            ImmutableCredentials credentials = awsCredentials.GetCredentials();
                            options.RegionEndpoint = amazonDynamoDb.Config.RegionEndpoint;
                            options.AccessKey = credentials.AccessKey;
                            options.SecretKey = credentials.SecretKey;
                        }, tags: new[] {"services"});
                    }
                    break;*/
                default:
                    throw new ArgumentOutOfRangeException();
            }

            foreach (HordeStorageSettings.StorageBackendImplementations impl in GetStorageImplementationEnums(settings.StorageImplementations))
            {
                switch (impl)
                {
                    case HordeStorageSettings.StorageBackendImplementations.S3:

                        // The S3 health checks are disabled as they do not support dynamic credentials similar to the issue with dynamo
                        /*AWSCredentials awsCredentials = provider.GetService<AWSCredentials>();
                        S3Settings s3Settings = provider.GetService<IOptionsMonitor<S3Settings>>()!.CurrentValue;
                        healthChecks.AddS3(options =>
                        {
                            options.BucketName = s3Settings.BucketName;
                            options.S3Config = provider.GetService<IAmazonS3>().Config as AmazonS3Config;
                            options.Credentials = awsCredentials;
                        }, tags: new[] {"services"});*/
                        break;
                    case HordeStorageSettings.StorageBackendImplementations.Azure:
                        AzureSettings azureSettings = provider.GetService<IOptionsMonitor<AzureSettings>>()!.CurrentValue;
                        healthChecks.AddAzureBlobStorage(azureSettings.ConnectionString, tags: new[] {"services"});
                        break;

                    case HordeStorageSettings.StorageBackendImplementations.FileSystem:
                        FilesystemSettings filesystemSettings = provider.GetService<IOptionsMonitor<FilesystemSettings>>()!.CurrentValue;
                        healthChecks.AddDiskStorageHealthCheck(options =>
                        {
                            string? driveRoot = Path.GetPathRoot(filesystemSettings.RootDir);
                            options.AddDrive(driveRoot);
                        });
                        break;
                    case HordeStorageSettings.StorageBackendImplementations.Memory:
                        break;
                    default:
                        throw new ArgumentOutOfRangeException("Unhandled storage impl " + impl);
                }
            }

            healthChecks.AddCheck<LastAccessServiceCheck>("LastAccessServiceCheck", tags: new[] {"services"});

            healthChecks.AddCheck<ReplicatorServiceCheck>("ReplicatorServiceCheck", tags: new[] { "services" });
            healthChecks.AddCheck<ReplicationSnapshotServiceCheck>("ReplicationSnapshotServiceCheck", tags: new[] { "services" });

            GCSettings gcSettings = provider.GetService<IOptionsMonitor<GCSettings>>()!.CurrentValue;

            if (gcSettings.CleanOldRefRecords)
            {
                healthChecks.AddCheck<RefCleanupServiceCheck>("RefCleanupCheck", tags: new[] {"services"});
            }

            if (gcSettings.CleanOldBlobs)
            {
                healthChecks.AddCheck<BlobCleanupServiceCheck>("BlobStoreCheck", tags: new[] {"services"});
            }

            if (settings.LeaderElectionImplementation == HordeStorageSettings.LeaderElectionImplementations.Kubernetes)
            {
                healthChecks.AddCheck<KubernetesLeaderServiceCheck>("KubernetesLeaderService", tags: new[] {"services"});
            }
        }
    }
}
