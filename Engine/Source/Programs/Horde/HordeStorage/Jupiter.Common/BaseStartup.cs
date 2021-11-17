// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.IO;
using System.Linq;
using System.Net.Mime;
using System.Reflection;
using System.Text;
using System.Threading.Tasks;
using Amazon;
using Datadog.Trace;
using Microsoft.AspNetCore.Authentication;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Diagnostics.HealthChecks;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.HttpOverrides;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Routing;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Diagnostics.HealthChecks;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Options;
using Microsoft.Extensions.Primitives;
using Microsoft.OpenApi.Models;
using Newtonsoft.Json;
using Newtonsoft.Json.Serialization;
using Okta.AspNet.Abstractions;
using Okta.AspNetCore;
using Serilog;
using ILogger = Serilog.ILogger;
using OktaWebApiOptions = Okta.AspNetCore.OktaWebApiOptions;

namespace Jupiter
{
    public abstract class BaseStartup
    {
        protected readonly ILogger _logger = Log.ForContext<BaseStartup>();

        public BaseStartup(IConfiguration configuration, IWebHostEnvironment environment)
        {
            Configuration = configuration;
            Environment = environment;
            Auth = new AuthSettings();
        }

        public IConfiguration Configuration { get; }
        public IWebHostEnvironment Environment { get; }

        protected AuthSettings Auth { get; }

        // This method gets called by the runtime. Use this method to add services to the container.
        public void ConfigureServices(IServiceCollection services)
        {
            // aws specific settings
            services.AddOptions<AWSCredentialsSettings>().Bind(Configuration.GetSection("AWSCredentials")).ValidateDataAnnotations();
            services.AddDefaultAWSOptions(Configuration.GetAWSOptions());
            // send log4net logs to serilog and configure aws to log to log4net (they lack a serilog implementation)
            AWSConfigs.LoggingConfig.LogTo = LoggingOptions.Log4Net;

            services.AddOptions<AuthSettings>().Bind(Configuration.GetSection("Auth")).ValidateDataAnnotations();
            Configuration.GetSection("Auth").Bind(Auth);

            services.AddOptions<ServiceAccountAuthOptions>().Bind(Configuration.GetSection("ServiceAccounts")).ValidateDataAnnotations();

            services.AddOptions<AuthorizationSettings>().Bind(Configuration.GetSection("Authorization")).ValidateDataAnnotations();
            services.AddOptions<JupiterSettings>().Bind(Configuration.GetSection("Jupiter")).ValidateDataAnnotations();

            // this is the same as invoke MvcBuilder.AddJsonOptions but with a service provider passed so we can use DI in the options creation
            // see https://stackoverflow.com/questions/53288633/net-core-api-custom-json-resolver-based-on-request-values
            // inject our custom json options and then pass a DI context to customize the serialization
            services.Configure<MvcNewtonsoftJsonOptions>(OnJsonOptions);
            services.AddTransient<IConfigureOptions<MvcNewtonsoftJsonOptions>, MvcJsonOptionsWrapper>();

            services.AddControllers()
                .AddNewtonsoftJson()
                .AddMvcOptions(options =>
                {
                    options.InputFormatters.Add(new CompactBinaryInputFormatter());
                    options.OutputFormatters.Add(new CompactBinaryOutputFormatter());
                    options.OutputFormatters.Add(new RawOutputFormatter());

                    options.FormatterMappings.SetMediaTypeMappingForFormat("raw", MediaTypeNames.Application.Octet);
                    options.FormatterMappings.SetMediaTypeMappingForFormat("uecb", CustomMediaTypeNames.UnrealCompactBinary);

                    OnAddControllers(options);
                }).ConfigureApiBehaviorOptions(options =>
                {
                    options.InvalidModelStateResponseFactory = context =>
                    {
                        var result = new BadRequestObjectResult(context.ModelState);
                        // always return errors as json objects
                        // we could allow more types here, but we do not want raw for instance
                        result.ContentTypes.Add(MediaTypeNames.Application.Json);

                        return result;
                    };
                });

            services.AddHttpContextAccessor();

            // we change the name of the jwt scheme as okta uses the same name and does not allow us to reconfigure it.
            const string JwtScheme = "JWTBearer";
            List<string> defaultSchemes = new List<string>();

            AuthenticationBuilder authenticationBuilder = services.AddAuthentication(options =>
                {
                    switch (Auth.Method)
                    {
                        case AuthMethod.JWTBearer:
                            options.DefaultAuthenticateScheme = JwtScheme;
                            options.DefaultChallengeScheme = JwtScheme;
                            break;
                        case AuthMethod.Okta:
                            options.DefaultAuthenticateScheme = OktaDefaults.ApiAuthenticationScheme;
                            options.DefaultChallengeScheme = OktaDefaults.ApiAuthenticationScheme;
                            break;
                        case AuthMethod.Disabled:
                            options.DefaultAuthenticateScheme = DisabledAuthenticationHandler.AuthenticateScheme;
                            options.DefaultChallengeScheme = DisabledAuthenticationHandler.AuthenticateScheme;
                            break;
                        default:
                            throw new ArgumentOutOfRangeException();
                    }
                });
            if (Auth.Method == AuthMethod.JWTBearer)
            {
                defaultSchemes.Add(JwtScheme);
                authenticationBuilder.AddJwtBearer(JwtScheme, options =>
                {
                    options.Authority = Auth.JwtAuthority;
                    options.Audience = Auth.JwtAudience;
                });
            }

            if (Auth.Method == AuthMethod.Okta)
            {
                defaultSchemes.Add(OktaDefaults.ApiAuthenticationScheme);
                authenticationBuilder.AddOktaWebApi(new OktaWebApiOptions
                {
                    OktaDomain = Auth.OktaDomain,
                    AuthorizationServerId = Auth.AuthorizationServerId,
                    Audience = Auth.JwtAudience,
                });
            }

            if (Auth.Method == AuthMethod.Disabled)
            {
                defaultSchemes.Add(DisabledAuthenticationHandler.AuthenticateScheme);
                authenticationBuilder.AddTestAuth(options => { });
            }

            defaultSchemes.Add(ServiceAccountAuthHandler.AuthenticationScheme);
            authenticationBuilder.AddScheme<ServiceAccountAuthOptions, ServiceAccountAuthHandler>(ServiceAccountAuthHandler.AuthenticationScheme, options => { });

            services.AddAuthorization(options =>
            {
                options.AddPolicy(NamespaceAccessRequirement.Name, policy =>
                {
                    policy.AuthenticationSchemes = defaultSchemes;
                    policy.Requirements.Add(new NamespaceAccessRequirement());
                });

                OnAddAuthorization(options, defaultSchemes);
            });
            services.AddSingleton<IAuthorizationHandler, NamespaceAuthorizationHandler>();

            services.Configure<ForwardedHeadersOptions>(options =>
            {
                options.ForwardedHeaders = ForwardedHeaders.XForwardedFor | ForwardedHeaders.XForwardedProto;
            });


            services.AddSwaggerGen(settings =>
            {
                string? assemblyName = Assembly.GetEntryAssembly()?.GetName().Name;
                settings.SwaggerDoc("v1", info: new OpenApiInfo
                {
                    Title = $"{assemblyName} API",
                    Contact = new OpenApiContact
                    {
                        Name = "Joakim Lindqvist",
                        Email = "joakim.lindqvist@epicgames.com",
                    }
                });

                // Set the comments path for the Swagger JSON and UI.
                string xmlFile = $"{assemblyName}.xml";
                string xmlPath = Path.Combine(AppContext.BaseDirectory, xmlFile);
                if (File.Exists(xmlPath))
                {
                    settings.IncludeXmlComments(xmlPath);
                }
            });

            OnAddService(services);

            OnAddHealthChecks(services);
        }

        private void OnAddHealthChecks(IServiceCollection services)
        {
            IHealthChecksBuilder healthChecks = services.AddHealthChecks()
                .AddCheck("self", () => HealthCheckResult.Healthy(), tags: new[] {"self"});
            OnAddHealthChecks(services, healthChecks);

            healthChecks.AddDatadogPublisher("jupiter.healthchecks");
        }

        /// <summary>
        /// Register health checks for individual services
        /// </summary>
        /// <remarks>Use the self tag for checks if the service is running while the services tag can be used for any dependencies which needs to work</remarks>
        /// <param name="services">DI service injector</param>
        /// <param name="healthChecks">A already configured builder that you can add more checks to</param>
        protected abstract void OnAddHealthChecks(IServiceCollection services, IHealthChecksBuilder healthChecks);

        protected abstract void OnAddAuthorization(AuthorizationOptions authorizationOptions, List<string> defaultSchemes);

        protected virtual void OnJsonOptions(MvcNewtonsoftJsonOptions options)
        {
        }

        protected virtual void OnAddControllers(MvcOptions options)
        {
        }

        protected abstract void OnAddService(IServiceCollection services);

        // This method gets called by the runtime. Use this method to configure the HTTP request pipeline.
        public void Configure(IApplicationBuilder app, IWebHostEnvironment env)
        {
            JupiterSettings jupiterSettings = app.ApplicationServices.GetService<IOptionsMonitor<JupiterSettings>>()!.CurrentValue;
            
            bool IsHttpPort(HttpContext httpContext) => !IsHighPerfHttpPort(httpContext);
            bool IsHighPerfHttpPort(HttpContext httpContext) => jupiterSettings.DisableAuthOnPorts.Any(port => port == httpContext.Connection.LocalPort);
            
            if (jupiterSettings.ShowPII)
            {
                _logger.Error("Personally Identifiable information being shown. This should not be generally enabled in prod.");

                // do not hide personal information during development
                Microsoft.IdentityModel.Logging.IdentityModelEventSource.ShowPII = true;
            }

            if (jupiterSettings.DisableAuthOnPorts.Any())
            {
                _logger.Information("Auth is disabled on port(s) {Ports}", jupiterSettings.DisableAuthOnPorts);
            }

            app.MapWhen(IsHttpPort, httpApp =>
            {
                ConfigureMiddlewares(false, jupiterSettings, httpApp, env);
            });
            
            app.MapWhen(IsHighPerfHttpPort, highPerfApp =>
            {
                ConfigureMiddlewares(true, jupiterSettings, highPerfApp, env);
            });
        }

        private void ConfigureMiddlewares(bool isHighPerf, JupiterSettings jupiterSettings, IApplicationBuilder app, IWebHostEnvironment env)
        {
            // enable use of forwarding headers as we expect a reverse proxy to be running in front of us
            app.UseMiddleware<DatadogTraceMiddleware>("ForwardedHeaders");
            app.UseForwardedHeaders();

            if (!isHighPerf && jupiterSettings.UseRequestLogging)
            {
                app.UseMiddleware<DatadogTraceMiddleware>("RequestLogging");
                app.UseSerilogRequestLogging();
            }

            OnConfigureAppEarly(app, env);
                
            if (!isHighPerf && env.IsDevelopment() && UseDeveloperExceptionPage)
            {
                app.UseDeveloperExceptionPage();
            }
            else
            { 
                app.UseExceptionHandler("/error");
            }
                
            app.UseMiddleware<DatadogTraceMiddleware>("Routing");
            app.UseRouting();

            if (!isHighPerf)
            {
                app.UseMiddleware<DatadogTraceMiddleware>("Authentication");
                app.UseAuthentication();
                app.UseMiddleware<DatadogTraceMiddleware>("Authorization");
                app.UseAuthorization();
            }
            
            app.UseMiddleware<DatadogTraceMiddleware>("Endpoints");
            app.UseEndpoints(endpoints =>
            {
                bool PassAllChecks(HealthCheckRegistration check) => true;

                // Ready checks in Kubernetes is to verify that the service is working, if this returns false the app will not get any traffic (load balancer ignores it)
                endpoints.MapHealthChecks("/health/ready", options: new HealthCheckOptions()
                {
                    Predicate = jupiterSettings.DisableHealthChecks ? PassAllChecks : (check) => check.Tags.Contains("self"),
                });

                // Live checks in Kubernetes to see if the pod is working as it should, if this returns false the entire pod is killed
                endpoints.MapHealthChecks("/health/live", options: new HealthCheckOptions()
                {
                    Predicate = jupiterSettings.DisableHealthChecks ? PassAllChecks : (check) => check.Tags.Contains("services"),
                });

                OnUseEndpoints(env, endpoints);
                
                endpoints.MapControllers();
            });

            if (!isHighPerf && jupiterSettings.HostSwaggerDocumentation)
            {
                app.UseMiddleware<DatadogTraceMiddleware>("Swagger");
                app.UseSwagger();
                app.UseReDoc(options => { options.SpecUrl = "/swagger/v1/swagger.json"; });
            }

            OnConfigureApp(app, env);
        }

        protected virtual void OnUseEndpoints(IWebHostEnvironment env, IEndpointRouteBuilder endpoints)
        {
            
        }

        public virtual bool UseDeveloperExceptionPage { get; } = true;

        protected virtual void OnConfigureAppEarly(IApplicationBuilder app, IWebHostEnvironment env)
        {
        }

        protected virtual void OnConfigureApp(IApplicationBuilder app, IWebHostEnvironment env)
        {
            
        }
    }

    public class MvcJsonOptionsWrapper : IConfigureOptions<MvcNewtonsoftJsonOptions>
    {
        IServiceProvider ServiceProvider;

        public MvcJsonOptionsWrapper(IServiceProvider serviceProvider)
        {
            ServiceProvider = serviceProvider;
        }
        public void Configure(MvcNewtonsoftJsonOptions options)
        {
            options.SerializerSettings.ContractResolver = new FieldFilteringResolver(ServiceProvider);
        }
    }

    public class FieldFilteringResolver : DefaultContractResolver
    {
        private IHttpContextAccessor _httpContextAccessor;

        public FieldFilteringResolver(IServiceProvider sp)
        {
            _httpContextAccessor = sp.GetRequiredService<IHttpContextAccessor>();

            NamingStrategy = new CamelCaseNamingStrategy(false, true);
        }

        protected override JsonProperty CreateProperty(MemberInfo member, MemberSerialization memberSerialization)
        {

            JsonProperty property = base.CreateProperty(member, memberSerialization);

            property.ShouldSerialize = o =>
            {
                HttpContext? httpContext = _httpContextAccessor.HttpContext;

                if (httpContext == null)
                    return true;

                // if no fields are being filtered we should serialize the property
                if (!httpContext.Request.Query.ContainsKey("fields"))
                    return true;

                StringValues fields = httpContext.Request.Query["fields"];
                bool ignore = true;
                foreach (string field in fields)
                {
                    // a empty field to filter for is considered a match for everything as fields= should be the same as just omitting fields
                    if (string.IsNullOrEmpty(field))
                    {
                        return true;
                    }
                    if (string.Equals(field, property.PropertyName, StringComparison.InvariantCultureIgnoreCase))
                    {
                        ignore = false;
                    }
                }

                return !ignore;
            };

            return property;
        }
    }


    public enum AuthMethod
    {
        JWTBearer,
        Okta,
        Disabled
    };

    public class AuthSettings
    {
        [Required] 
        public AuthMethod Method { get; set; } = AuthMethod.JWTBearer;


        [Required]
        public string OktaDomain { get; set; } = "";
        public string AuthorizationServerId { get; set; } = OktaWebOptions.DefaultAuthorizationServerId;

        [Required]
        public string JwtAuthority { get; set; } = "";

        [Required]
        public string JwtAudience { get; set; } = "";

    }

    public class JupiterSettings
    {
        // enable to unhide potentially personal information, see https://aka.ms/IdentityModel/PII
        public bool ShowPII = false;
        public bool DisableHealthChecks { get; set; } = false;
        public bool HostSwaggerDocumentation { get; set; } = true;

        public List<int> DisableAuthOnPorts { get; set; } = new();

        // Enable to echo every request to the log file, usually this is more efficiently done on the load balancer
        public bool UseRequestLogging { get; set; } = false;

        //public Dictionary<string, int> DisableAuthOnPorts { get; set; } = new ();
    }

    public class DatadogTraceMiddleware
    {
        private readonly RequestDelegate _next;
        private readonly string _scopeName;

        public DatadogTraceMiddleware(RequestDelegate next, string scopeName)
        {
            _next = next;
            _scopeName = scopeName;
        }

        public async Task InvokeAsync(HttpContext httpContext)
        {
            using Scope _ = Tracer.Instance.StartActive(_scopeName);

            //Move to next delegate/middleware in the pipleline
            await _next.Invoke(httpContext);
        }
    }
}
