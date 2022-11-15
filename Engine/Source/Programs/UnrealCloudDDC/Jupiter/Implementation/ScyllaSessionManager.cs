// Copyright Epic Games, Inc. All Rights Reserved.

using Cassandra;
using OpenTelemetry.Trace;

namespace Jupiter.Implementation
{
    public interface IScyllaSessionManager
    {
        ISession GetSessionForReplicatedKeyspace();
        ISession GetSessionForLocalKeyspace();

        /// <summary>
        /// True if scylla specific features can be used
        /// </summary>
        bool IsScylla { get; }

        /// <summary>
        /// True when using a base cassandra protocol
        /// </summary>
        bool IsCassandra { get; }
    }

    public class ScyllaSessionManager : IScyllaSessionManager
    {
        private readonly ISession _replicatedSession;
        private readonly ISession _localSession;

        public ScyllaSessionManager(ISession replicatedSession, ISession localSession, bool isScylla, bool isCassandra)
        {
            _replicatedSession = replicatedSession;
            _localSession = localSession;
            IsScylla = isScylla;
            IsCassandra = isCassandra;
        }

        public ISession GetSessionForReplicatedKeyspace()
        {
            return _replicatedSession;

        }

        public ISession GetSessionForLocalKeyspace()
        {
            return _localSession;
        }

        public bool IsScylla { get; }
        public bool IsCassandra { get; }
    }

    public static class ScyllaTraceExtensions
    {
        private static readonly Tracer ScyllaTracer = TracerProvider.Default.GetTracer(ScyllaServiceName);

        private const string ScyllaServiceName = "ScyllaDB";

        [System.Diagnostics.CodeAnalysis.SuppressMessage("Style", "IDE0060:Remove unused parameter", Justification = "Useful to create from the tracer in case we want to use that context")]
        public static TelemetrySpan BuildScyllaSpan(this Tracer tracer, string spanName)
        {
            return ScyllaTracer.StartActiveSpan(spanName, parentContext: Tracer.CurrentSpan.Context).SetAttribute("type", "db").SetAttribute("service.name", ScyllaServiceName).SetAttribute("operation.name", spanName);
        }
    }
}
