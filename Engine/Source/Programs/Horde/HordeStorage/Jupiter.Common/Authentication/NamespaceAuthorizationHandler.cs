// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.Threading.Tasks;
using Jupiter.Implementation;
using Microsoft.AspNetCore.Authorization;
using Microsoft.Extensions.Options;

namespace Jupiter
{
    // verifies that you have access to a namespace by checking if you have a corresponding claim to that namespace
    public class NamespaceAuthorizationHandler : AuthorizationHandler<NamespaceAccessRequirement, NamespaceId>
    {
        private readonly IOptionsMonitor<NamespaceSettings> _namespaceSettings;

        public NamespaceAuthorizationHandler(IOptionsMonitor<NamespaceSettings> namespaceSettings)
        {
            _namespaceSettings = namespaceSettings;
        }

        protected override Task HandleRequirementAsync(AuthorizationHandlerContext context, NamespaceAccessRequirement requirement,
            NamespaceId namespaceName)
        {
            if (context.User.HasClaim(claim => claim.Type == "AllNamespaces"))
            {
                context.Succeed(requirement);
                return Task.CompletedTask;
            }

            NamespaceSettings.PerNamespaceSettings settings = _namespaceSettings.CurrentValue.GetPoliciesForNs(namespaceName);
            // These are ANDed, e.g. all claims needs to be present
            foreach (string expectedClaim in settings.Claims)
            {
                // if expected claim is * then everyone is allowed to use the namespace
                if (expectedClaim == "*")
                {
                    context.Succeed(requirement);
                }

                if (context.User.HasClaim(claim => claim.Type == expectedClaim))
                {
                    context.Succeed(requirement);
                }
            }

            return Task.CompletedTask;
        }
    }

    public class NamespaceAccessRequirement : IAuthorizationRequirement
    {
        public const string Name = "NamespaceAccess";
    }
}
