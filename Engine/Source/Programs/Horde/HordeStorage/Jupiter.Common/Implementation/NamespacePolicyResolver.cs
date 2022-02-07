// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using EpicGames.Horde.Storage;
using Jupiter.Implementation;
using Microsoft.Extensions.Options;

namespace Jupiter.Common
{
    public interface INamespacePolicyResolver
    {
        public NamespaceSettings.PerNamespaceSettings GetPoliciesForNs(NamespaceId ns);

        static NamespaceId JupiterInternalNamespace
        {
            get { return new NamespaceId("jupiter-internal"); }
        }
    }
    public class NamespacePolicyResolver : INamespacePolicyResolver
    {
        private readonly IOptionsMonitor<NamespaceSettings> _namespaceSettings;

        public NamespacePolicyResolver(IOptionsMonitor<NamespaceSettings> namespaceSettings)
        {
            _namespaceSettings = namespaceSettings;
        }

        public NamespaceSettings.PerNamespaceSettings GetPoliciesForNs(NamespaceId ns)
        {
            if (ns == INamespacePolicyResolver.JupiterInternalNamespace)
            {
                return new NamespaceSettings.PerNamespaceSettings()
                {
                    // we grant the full storage access claim access to the internal namespace, this is typically set of admins and service accounts that can access everything
                    Claims = new string[] {"Storage=full" },
                    StoragePool = ""
                };
            }

            if (_namespaceSettings.CurrentValue.Policies.TryGetValue(ns.ToString(),
                    out NamespaceSettings.PerNamespaceSettings? settings))
            {
                return settings;
            }

            // attempt to find the default mapping
            if (_namespaceSettings.CurrentValue.Policies.TryGetValue("*",
                    out NamespaceSettings.PerNamespaceSettings? defaultSettings))
            {
                return defaultSettings;
            }

            throw new ArgumentException($"Unable to find a valid policy for namespace {ns}");
        }
    }
}
