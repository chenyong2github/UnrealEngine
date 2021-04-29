// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IdentityModel.Tokens.Jwt;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Security.Claims;
using System.Text.Encodings.Web;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Threading.Tasks;
using Serilog;
using HordeServer.Models;
using HordeServer.Services;
using Microsoft.AspNetCore.Authentication;
using Microsoft.AspNetCore.Authentication.Cookies;
using Microsoft.AspNetCore.Authentication.JwtBearer;
using Microsoft.AspNetCore.Authentication.OAuth.Claims;
using Microsoft.AspNetCore.Authentication.OpenIdConnect;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using Microsoft.IdentityModel.Protocols.OpenIdConnect;
using Microsoft.IdentityModel.Tokens;
using Microsoft.OpenApi.Models;
using MongoDB.Bson;
using MongoDB.Bson.Serialization;
using MongoDB.Bson.Serialization.Conventions;
using System.Net.Http;
using Microsoft.AspNetCore.HttpOverrides;
using HordeServer.Utilities;
using Grpc.Core.Interceptors;
using Grpc.Core;
using EpicGames.Core;
using Microsoft.Extensions.Diagnostics.HealthChecks;
using Microsoft.AspNetCore.Diagnostics.HealthChecks;
using HordeServer.Collections;
using Microsoft.AspNetCore.Http;
using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using System.Threading;
using HordeCommon;
using Microsoft.AspNetCore.DataProtection;
using StackExchange.Redis;
using HordeServer.Storage;
using HordeServer.Storage.Impl;
using HordeServer.Logs;
using HordeServer.Logs.Builder;
using HordeServer.Logs.Storage;
using HordeServer.Logs.Readers;
using HordeServer.Logs.Storage.Impl;
using HordeServer.Collections.Impl;
using HordeServer.Services.Impl;
using Microsoft.Extensions.DependencyInjection.Extensions;
using HordeServer.Authentication;
using HordeServer.Rpc;
using HordeServer.Storage.Services;
using HordeServer.Tasks.Impl;
using HordeServer.Tasks;
using StatsdClient;
using Status = Grpc.Core.Status;
using HordeServer.Notifications.Impl;
using HordeServer.Notifications;

namespace HordeServer
{
	class Startup
	{
		class GrpcExceptionInterceptor : Interceptor
		{
			ILogger<GrpcExceptionInterceptor> Logger;

			public GrpcExceptionInterceptor(ILogger<GrpcExceptionInterceptor> Logger)
			{
				this.Logger = Logger;
			}

			[SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "<Pending>")]
			IDisposable? RequestContext<TRequest>(TRequest Request)
			{
				try
				{
					return Logger.BeginScope("Request: {Request}", JsonSerializer.Serialize<TRequest>(Request));
				}
				catch
				{
					return null;
				}
			}

			[SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "<Pending>")]
			IDisposable? ConnectionContext(ConnectionInfo Connection)
			{
				try
				{
					return Logger.BeginScope("Client: {ClientIp}", Connection.RemoteIpAddress);
				}
				catch
				{
					return null;
				}
			}

			public override AsyncClientStreamingCall<TRequest, TResponse> AsyncClientStreamingCall<TRequest, TResponse>(ClientInterceptorContext<TRequest, TResponse> Context, AsyncClientStreamingCallContinuation<TRequest, TResponse> Continuation)
			{
				try
				{
					return base.AsyncClientStreamingCall(Context, Continuation);
				}
				catch (StructuredRpcException Ex)
				{
					Logger.LogError(Ex, Ex.Format, Ex.Args);
					throw;
				}
				catch (Exception Ex)
				{
					Logger.LogError(Ex, "Exception in call to {Method}", Context.Method);
					throw new RpcException(new Status(StatusCode.Internal, $"An exception was thrown on the server: {Ex}"));
				}
			}

			public override AsyncDuplexStreamingCall<TRequest, TResponse> AsyncDuplexStreamingCall<TRequest, TResponse>(ClientInterceptorContext<TRequest, TResponse> Context, AsyncDuplexStreamingCallContinuation<TRequest, TResponse> Continuation)
			{
				try
				{
					return base.AsyncDuplexStreamingCall(Context, Continuation);
				}
				catch (StructuredRpcException Ex)
				{
					Logger.LogError(Ex, Ex.Format, Ex.Args);
					throw;
				}
				catch (Exception Ex)
				{
					Logger.LogError(Ex, "Exception in call to {Method}", Context.Method);
					throw new RpcException(new Status(StatusCode.Internal, $"An exception was thrown on the server: {Ex}"));
				}
			}

			[SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "<Pending>")]
			public override AsyncServerStreamingCall<TResponse> AsyncServerStreamingCall<TRequest, TResponse>(TRequest Request, ClientInterceptorContext<TRequest, TResponse> Context, AsyncServerStreamingCallContinuation<TRequest, TResponse> Continuation)
			{
				try
				{
					return base.AsyncServerStreamingCall(Request, Context, Continuation);
				}
				catch (StructuredRpcException Ex)
				{
					using IDisposable? Scope = RequestContext(Request); 
					Logger.LogError(Ex, Ex.Format, Ex.Args);
					throw;
				}
				catch (Exception Ex)
				{
					using IDisposable? Scope = RequestContext(Request);
					Logger.LogError(Ex, "Exception in call to {Method}", Context.Method);
					throw new RpcException(new Status(StatusCode.Internal, $"An exception was thrown on the server: {Ex}"));
				}
			}

			[SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "<Pending>")]
			public override async Task<TResponse> UnaryServerHandler<TRequest, TResponse>(TRequest Request, ServerCallContext Context, UnaryServerMethod<TRequest, TResponse> Continuation)
			{
				try
				{
					return await base.UnaryServerHandler(Request, Context, Continuation);
				}
				catch (StructuredRpcException Ex)
				{
					using IDisposable? ConnectionScope = ConnectionContext(Context.GetHttpContext().Connection);
					using IDisposable? Scope = RequestContext(Request);
					Logger.LogError(Ex, Ex.Format, Ex.Args);
					throw;
				}
				catch (Exception Ex)
				{
					using IDisposable? ConnectionScope = ConnectionContext(Context.GetHttpContext().Connection);
					using IDisposable? Scope = RequestContext(Request);
					Logger.LogError(Ex, "Exception in call to {Method}", Context.Method);
					throw new RpcException(new Status(StatusCode.Internal, $"An exception was thrown on the server: {Ex}"));
				}
			}
		}

		class BsonSerializationProvider : IBsonSerializationProvider
		{
			public IBsonSerializer? GetSerializer(Type Type)
			{
				if (Type == typeof(ContentHash))
				{
					return new ContentHashSerializer();
				}
				if (Type == typeof(DateTimeOffset))
				{
					return new DateTimeOffsetStringSerializer();
				}
				return null;
			}
		}

		class JsonObjectIdConverter : JsonConverter<ObjectId>
		{
			public override ObjectId Read(ref Utf8JsonReader Reader, Type TypeToConvert, JsonSerializerOptions Options)
			{
				return Reader.GetString().ToObjectId();
			}

			public override void Write(Utf8JsonWriter Writer, ObjectId ObjectId, JsonSerializerOptions Options)
			{
				Writer.WriteStringValue(ObjectId.ToString());
			}
		}
		
		class JsonDateTimeConverter : JsonConverter<DateTime>
		{
			public override DateTime Read(ref Utf8JsonReader Reader, Type TypeToConvert, JsonSerializerOptions Options)
			{
				Debug.Assert(TypeToConvert == typeof(DateTime));
				return DateTime.Parse(Reader.GetString(), CultureInfo.CurrentCulture);
			}

			public override void Write(Utf8JsonWriter Writer, DateTime DateTime, JsonSerializerOptions Options)
			{
				Writer.WriteStringValue(DateTime.ToUniversalTime().ToString("yyyy'-'MM'-'dd'T'HH':'mm':'ssZ", CultureInfo.CurrentCulture));
			}
		}

		public Startup(IConfiguration Configuration)
		{
			this.Configuration = Configuration;
		}

		public IConfiguration Configuration { get; }

		ServerSettings Settings { get; set; } = new ServerSettings();

		private ConnectionMultiplexer? RedisConMux = null;

		// This method gets called by the runtime. Use this method to add services to the container.
		public void ConfigureServices(IServiceCollection Services)
		{
			IConfigurationSection ConfigSection = Configuration.GetSection("Horde");
			Services.AddOptions<ServerSettings>().Configure(Options => ConfigSection.Bind(Options)).ValidateDataAnnotations();

			ConfigSection.Bind(Settings);

			if (Settings.GlobalThreadPoolMinSize != null)
			{
				// Min thread pool size is set to combat timeouts seen with the Redis client.
				// See comments for <see cref="ServerSettings.GlobalThreadPoolMinSize" /> and
				// https://github.com/StackExchange/StackExchange.Redis/issues/1680
				int Min = Settings.GlobalThreadPoolMinSize.Value;
				ThreadPool.SetMinThreads(Min, Min);
			}

			if (String.IsNullOrEmpty(Settings.RedisConnectionConfig))
			{
				// FIXME: Debug why [Required] annotation is not working in ServerSettings.
				// This is just a crude fix to avoid a cryptic startup message.
				throw new Exception("Redis not configured in settings!");
			}
			
			// The Connect() is sync as AddSingleton() does not support async operations.
			// This *should* be fine as this is only established once during startup (reconnects happen async)
			RedisConMux = ConnectionMultiplexer.Connect(Settings.RedisConnectionConfig);
			Services.AddSingleton<IDatabase>(Sp => RedisConMux.GetDatabase());
			Services.AddSingleton(new RedisConnectionPool(20, Settings.RedisConnectionConfig));
			
			Services.AddDataProtection().PersistKeysToStackExchangeRedis(RedisConMux, "aspnet-data-protection");

			if (Settings.CorsEnabled)
			{
				Services.AddCors(Options =>
				{
					Options.AddPolicy("CorsPolicy",
						Builder => Builder.WithOrigins(Settings.CorsOrigin.Split(";"))
						.AllowAnyMethod()
						.AllowAnyHeader()
						.AllowCredentials());
				});
			}

			Services.AddGrpc(Options =>
			{
				Options.EnableDetailedErrors = true;
				Options.Interceptors.Add(typeof(LifetimeGrpcInterceptor));
				Options.Interceptors.Add(typeof(GrpcExceptionInterceptor));
			});

			Services.AddSingleton<IAgentCollection, AgentCollection>();
			Services.AddSingleton<IAgentSoftwareCollection, AgentSoftwareCollection>();
			Services.AddSingleton<ICounterCollection, CounterCollection>();
			Services.AddSingleton<IGraphCollection, GraphCollection>();
			Services.AddSingleton<IIssueCollection, IssueCollection>();
			Services.AddSingleton<IJobCollection, JobCollection>();
			Services.AddSingleton<IJobStepRefCollection, JobStepRefCollection>();
			Services.AddSingleton<IJobTimingCollection, JobTimingCollection>();
			Services.AddSingleton<ILeaseCollection, LeaseCollection>();
			Services.AddSingleton<ILogEventCollection, LogEventCollection>();
			Services.AddSingleton<ILogFileCollection, LogFileCollection>();
			Services.AddSingleton<INotificationTriggerCollection, NotificationTriggerCollection>();
			Services.AddSingleton<IPoolCollection, PoolCollection>();
			Services.AddSingleton<IProjectCollection, ProjectCollection>();
			Services.AddSingleton<ISessionCollection, SessionCollection>();
			Services.AddSingleton<IServiceAccountCollection, ServiceAccountCollection>();
			Services.AddSingleton<ISubscriptionCollection, SubscriptionCollection>();
			Services.AddSingleton<ISoftwareCollection, SoftwareCollection>();
			Services.AddSingleton<IStreamCollection, StreamCollection>();
			Services.AddSingleton<ITemplateCollection, TemplateCollection>();
			Services.AddSingleton<ITestDataCollection, TestDataCollection>();
			Services.AddSingleton<ITelemetryCollection, TelemetryCollection>();
			Services.AddSingleton<IUgsMetadataCollection, UgsMetadataCollection>();
			Services.AddSingleton<IUserCollection, UserCollectionV2>();

			Services.AddSingleton(typeof(ISingletonDocument<>), typeof(SingletonDocument<>));

			Services.AddSingleton<AutoscaleService>();
			switch (Settings.FleetManager)
			{
				case FleetManagerType.Aws:
					Services.AddSingleton<IFleetManager, AwsFleetManager>();
					break;
				default:
					Services.AddSingleton<IFleetManager, DefaultFleetManager>();
					break;
			}

			Services.AddSingleton<AclService>();
			Services.AddSingleton<AgentService>();
			Services.AddSingleton<AgentSoftwareService>();
			Services.AddSingleton<ArtifactService>();
			Services.AddSingleton<ConsistencyService>();
			Services.AddSingleton<RequestTrackerService>();
			Services.AddSingleton<CredentialService>();
			Services.AddSingleton<DatabaseService>();
			Services.AddSingleton<IDogStatsd>(ctx =>
			{
				string? DatadogAgentHost = Environment.GetEnvironmentVariable("DD_AGENT_HOST");
				if (DatadogAgentHost != null)
				{
					// Datadog agent is configured, enable DogStatsD for metric collection
					StatsdConfig Config = new StatsdConfig
					{
						StatsdServerName = DatadogAgentHost,
						StatsdPort = 8125,
					};
					
					DogStatsdService DogStatsdService = new DogStatsdService();
					DogStatsdService.Configure(Config);
					return DogStatsdService;
				}
				return new NoOpDogStatsd();
			});
			Services.AddSingleton<IClock, Clock>();
			Services.AddSingleton<IDowntimeService, DowntimeService>();
			Services.AddSingleton<IIssueService, IssueService>();
			Services.AddSingleton<JobService>();
			Services.AddSingleton<LifetimeService>();
			Services.AddSingleton<ILogFileService, LogFileService>();
			Services.AddSingleton<INotificationService, NotificationService>();			

			if (String.IsNullOrEmpty(Settings.PerforceBridge))
			{
				Services.AddSingleton<IPerforceService, LocalPerforceService>();
			}
			else
			{
				Services.AddSingleton<IPerforceService, BridgePerforceService>(SP => new BridgePerforceService(Settings.PerforceBridge!, SP.GetRequiredService<ILogger<BridgePerforceService>>()));
			}

			Services.AddSingleton<PerforceLoadBalancer>();
			Services.AddSingleton<PoolService>();
			Services.AddSingleton<ProjectService>();
			Services.AddSingleton<ScheduleService>();
			Services.AddSingleton<SlackNotificationSink>();
			Services.AddSingleton<INotificationSink, SlackNotificationSink>(SP => SP.GetRequiredService<SlackNotificationSink>());
			Services.AddSingleton<SoftwareService>();
			Services.AddSingleton<StreamService>();
			Services.AddSingleton<TemplateService>();
			Services.AddSingleton<UpgradeService>();

			Services.AddSingleton<ActionCacheService>();
			Services.AddSingleton<ByteStreamService>();
			Services.AddSingleton<CapabilitiesService>();
			Services.AddSingleton<ContentStorageService>();
			Services.AddSingleton<ExecutionService>();

			Services.AddSingleton<FileSystemStorageBackend>();
			Services.AddSingleton<IStorageService, SimpleStorageService>(SP => new SimpleStorageService(SP.GetRequiredService<FileSystemStorageBackend>()));

			ConfigureStorageProvider(Services, Settings);
			ConfigureLogStorage(Services);
			//			ConfigureLogFileWriteCache(Services, Settings);

			if (Settings.DisableAuth)
			{
				var Scheme = AnonymousAuthenticationHandler.AuthenticationScheme;
				Services.AddAuthentication(Scheme)
					.AddScheme<AnonymousAuthenticationOptions, AnonymousAuthenticationHandler>(Scheme, Options =>
					{
						Options.AdminClaimType = Settings.AdminClaimType;
						Options.AdminClaimValue = Settings.AdminClaimValue;
					});
			}
			else
			{
				AuthenticationBuilder AuthBuilder = Services.AddAuthentication(Options =>
					{
						// If an authentication cookie is present, use it to get authentication information
						Options.DefaultAuthenticateScheme = CookieAuthenticationDefaults.AuthenticationScheme;
						Options.DefaultSignInScheme = CookieAuthenticationDefaults.AuthenticationScheme;

						// If authentication is required, and no cookie is present, use OIDC to sign in
						Options.DefaultChallengeScheme = OktaDefaults.AuthenticationScheme;
					})
					.AddCookie(Options =>
					{
						Options.Events.OnValidatePrincipal = Context =>
						{
							if (Context.Principal.FindFirst(HordeClaimTypes.UserId) == null)
							{
								Context.RejectPrincipal();
							}
							return Task.CompletedTask;
						};

						Options.Events.OnRedirectToAccessDenied = Context => 
						{ 
							Context.Response.StatusCode = StatusCodes.Status403Forbidden; 
							return Context.Response.CompleteAsync(); 
						};
					});

				List<string> AuthSchemes = new List<string>();
				AuthSchemes.Add(CookieAuthenticationDefaults.AuthenticationScheme);

				AuthBuilder.AddScheme<ServiceAccountAuthOptions, ServiceAccountAuthHandler>(
					ServiceAccountAuthHandler.AuthenticationScheme, Options => { });
				AuthSchemes.Add(ServiceAccountAuthHandler.AuthenticationScheme);

				if (Settings.OidcClientId != null && Settings.OidcAuthority != null)
				{
					AuthBuilder.AddOkta(OktaDefaults.AuthenticationScheme, OpenIdConnectDefaults.DisplayName, 
						Options =>
						{
							Options.Authority = Settings.OidcAuthority;
							Options.ClientId = Settings.OidcClientId;

							if (!String.IsNullOrEmpty(Settings.OidcSigninRedirect))
							{
								Options.Events = new OpenIdConnectEvents
								{
									OnRedirectToIdentityProvider = async RedirectContext =>
									{
										RedirectContext.ProtocolMessage.RedirectUri = Settings.OidcSigninRedirect;
										await Task.CompletedTask;
									}
								};
							}
						});
					AuthSchemes.Add(OktaDefaults.AuthenticationScheme);
				}

				AuthBuilder.AddScheme<JwtBearerOptions, HordeJwtBearerHandler>(HordeJwtBearerHandler.AuthenticationScheme, Options => { });
				AuthSchemes.Add(HordeJwtBearerHandler.AuthenticationScheme);

				Services.AddAuthorization(Options =>
					{
						Options.DefaultPolicy = new AuthorizationPolicyBuilder(AuthSchemes.ToArray())
							.RequireAuthenticatedUser()							
							.Build();
					});
			}

			Services.AddHostedService(Provider => Provider.GetRequiredService<AgentService>());
			Services.AddHostedService(Provider => Provider.GetRequiredService<AutoscaleService>());
			Services.AddHostedService(Provider => Provider.GetRequiredService<ConsistencyService>());
			Services.AddHostedService(Provider => (DowntimeService)Provider.GetRequiredService<IDowntimeService>());
			Services.AddHostedService(Provider => Provider.GetRequiredService<JobTaskSource>());
			Services.AddHostedService(Provider => Provider.GetRequiredService<IIssueService>());
			Services.AddHostedService(Provider => (LogFileService)Provider.GetRequiredService<ILogFileService>());
			Services.AddHostedService(Provider => (NotificationService)Provider.GetRequiredService<INotificationService>());
			if (!Settings.DisableSchedules)
			{
				Services.AddHostedService(Provider => Provider.GetRequiredService<ScheduleService>());
			}

			Services.AddHostedService<MetricService>();
			Services.AddHostedService(Provider => Provider.GetRequiredService<PerforceLoadBalancer>());
			Services.AddHostedService<PoolUpdateService>();
			Services.AddHostedService(Provider => Provider.GetRequiredService<SlackNotificationSink>());
			Services.AddHostedService<StreamConfigService>();
			Services.AddHostedService<TelemetryService>();

			// Task sources. Order of registration is important here; it dictates the order in which sources are served.
			Services.AddSingleton<JobTaskSource>();
			Services.AddSingleton<ActionTaskSource>();
			Services.AddSingleton<ConformTaskSource>();

			Services.AddSingleton<ITaskSource, UpgradeTaskSource>();
			Services.AddSingleton<ITaskSource, ShutdownTaskSource>();
			Services.AddSingleton<ITaskSource, RestartTaskSource>();
			Services.AddSingleton<ITaskSource, ConformTaskSource>(Provider => Provider.GetRequiredService<ConformTaskSource>());
			Services.AddSingleton<ITaskSource, JobTaskSource>(Provider => Provider.GetRequiredService<JobTaskSource>());
			Services.AddSingleton<ITaskSource, ActionTaskSource>(Provider => Provider.GetRequiredService<ActionTaskSource>());

			// Allow longer to shutdown so we can debug missing cancellation tokens
			Services.Configure<HostOptions>(Options =>
			{
				Options.ShutdownTimeout = TimeSpan.FromSeconds(30.0);
			});

			// Allow forwarded headers
			Services.Configure<ForwardedHeadersOptions>(Options =>
			{
				Options.ForwardedHeaders = ForwardedHeaders.XForwardedFor | ForwardedHeaders.XForwardedProto;
				Options.KnownProxies.Clear();
				Options.KnownNetworks.Clear();
			});

			Services.AddMvc().AddJsonOptions(Options => ConfigureJsonSerializer(Options.JsonSerializerOptions));

			Services.AddControllers();

			Services.AddSwaggerGen(Config =>
			{
				Config.SwaggerDoc("v1", new OpenApiInfo { Title = "Horde Server API", Version = "v1" });
				Config.IncludeXmlComments(Path.Combine(AppContext.BaseDirectory, $"{Assembly.GetExecutingAssembly().GetName().Name}.xml"));
			});

			ConfigureMongoDbClient();

			OnAddHealthChecks(Services);
		}

		public static void ConfigureJsonSerializer(JsonSerializerOptions Options)
		{
			Options.IgnoreNullValues = true;
			Options.PropertyNamingPolicy = JsonNamingPolicy.CamelCase;
			Options.PropertyNameCaseInsensitive = true;
			Options.Converters.Add(new JsonObjectIdConverter());
			Options.Converters.Add(new JsonStringEnumConverter());
			Options.Converters.Add(new JsonKnownTypesConverterFactory());
			Options.Converters.Add(new JsonStringIdConverterFactory());
			Options.Converters.Add(new JsonDateTimeConverter());
		}

		private static void ConfigureStorageProvider(IServiceCollection Services, ServerSettings Settings)
		{
			switch (Settings.ExternalStorageProviderType)
			{
				case StorageProviderType.FileSystem:
					Services.AddSingleton<IStorageBackend, FileSystemStorageBackend>();
					break;
				case StorageProviderType.S3:
					Services.AddSingleton<IStorageBackend, AwsStorageBackend>();
					break;
				case StorageProviderType.Transient:
					Services.AddSingleton<IStorageBackend, TransientStorageBackend>();
					break;
				case StorageProviderType.Relay:
					Services.AddSingleton<IStorageBackend, RelayStorageBackend>();
					break;
				default:
					throw new NotImplementedException();
			}
		}

		private static void ConfigureLogStorage(IServiceCollection Services)
		{
			Services.AddSingleton<ILogBuilder>(Provider =>
			{
				RedisConnectionPool? RedisConnectionPool = Provider.GetService<RedisConnectionPool>();
				if(RedisConnectionPool == null)
				{
					return new LocalLogBuilder();
				}
				else
				{
					return new RedisLogBuilder(RedisConnectionPool, Provider.GetRequiredService<ILogger<RedisLogBuilder>>());
				}
			});
			Services.AddSingleton<ILogStorage>(Provider =>
			{
				ILogStorage Storage = new PersistentLogStorage(Provider.GetRequiredService<IStorageBackend>(), Provider.GetRequiredService<ILogger<PersistentLogStorage>>());

//				IDatabase? RedisDb = Provider.GetService<IDatabase>();
//				if (RedisDb != null)
//				{
//					Storage = new RedisLogStorage(RedisDb, Provider.GetService<ILogger<RedisLogStorage>>(), Storage);
//				}

				Storage = new SequencedLogStorage(Storage);
				Storage = new LocalLogStorage(50, Storage);
				return Storage;
			});
		}
/*
		private static void ConfigureLogFileWriteCache(IServiceCollection Services, ServerSettings Settings)
		{
			bool RedisConfigured = !String.IsNullOrEmpty(Settings.RedisConnectionConfig);
			string CacheType = Settings.LogServiceWriteCacheType.ToLower(CultureInfo.CurrentCulture);

			if (CacheType == "inmemory")
			{
				Services.AddSingleton<ILogFileWriteCache2>(Sp => new InMemoryLogFileWriteCache2());
			}
			else if (CacheType == "redis" && RedisConfigured)
			{
				Services.AddSingleton<ILogFileWriteCache2>(Sp => new RedisLogFileWriteCache2(Sp.GetService<ILogger<RedisLogFileWriteCache>>(), Sp.GetService<IDatabase>()));
			}
			else if (CacheType == "redis" && !RedisConfigured)
			{
				throw new Exception("Redis must be configured to use the Redis-backed log write cache");
			}
			else
			{
				throw new Exception("Unknown value set for LogServiceWriteCacheType in config: " + Settings.LogServiceWriteCacheType);
			}
		}
*/
		public static void ConfigureMongoDbClient()
		{
			// Ignore extra elements on deserialized documents
			ConventionPack ConventionPack = new ConventionPack();
			ConventionPack.Add(new IgnoreExtraElementsConvention(true));
			ConventionPack.Add(new EnumRepresentationConvention(BsonType.String));
			ConventionRegistry.Register("Horde", ConventionPack, Type => true);

			// Register the custom serializers
			BsonSerializer.RegisterSerializationProvider(new BsonSerializationProvider());
			BsonSerializer.RegisterSerializationProvider(new StringIdSerializationProvider());
		}

		private static void OnAddHealthChecks(IServiceCollection Services)
		{
			IHealthChecksBuilder HealthChecks = Services.AddHealthChecks().AddCheck("self", () => HealthCheckResult.Healthy(), tags: new[] { "self" });
		}

		// This method gets called by the runtime. Use this method to configure the HTTP request pipeline.
		public void Configure(IApplicationBuilder App, IWebHostEnvironment Env)
		{
			App.UseForwardedHeaders();

			if (Env.IsDevelopment())
			{
				App.UseDeveloperExceptionPage();
			}

			if (Settings.CorsEnabled)
			{
				App.UseCors("CorsPolicy");
			}

			// Enable middleware to serve generated Swagger as a JSON endpoint.
			App.UseSwagger();

			// Enable serilog request logging
			App.UseSerilogRequestLogging();

			// Include the source IP address with requests
			App.Use(async (Context, Next) => {
				using (Serilog.Context.LogContext.PushProperty("RemoteIP", Context.Connection.RemoteIpAddress))
				{
					await Next();
				}
			});

			// Enable middleware to serve swagger-ui (HTML, JS, CSS, etc.),
			// specifying the Swagger JSON endpoint.
			App.UseSwaggerUI(c =>
			{
				c.SwaggerEndpoint("/swagger/v1/swagger.json", "Horde Server API");
				c.RoutePrefix = "swagger";
			});

			if (!Env.IsDevelopment())
			{
				App.UseMiddleware<RequestTrackerMiddleware>();	
			}
			
			App.UseRouting();

			App.UseAuthentication();
			App.UseAuthorization();

			App.UseEndpoints(Endpoints =>
			{
				Endpoints.MapGrpcService<RpcService>();
				Endpoints.MapGrpcService<ActionRpcService>();
				Endpoints.MapGrpcService<ActionCacheService>();
				Endpoints.MapGrpcService<ByteStreamService>();
				Endpoints.MapGrpcService<CapabilitiesService>();
				Endpoints.MapGrpcService<ContentStorageService>();
				Endpoints.MapGrpcService<ExecutionService>();

				Endpoints.MapControllers();
			});
		}
	}
}
