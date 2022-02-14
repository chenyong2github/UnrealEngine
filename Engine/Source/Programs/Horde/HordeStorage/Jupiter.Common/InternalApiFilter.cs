// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using Jupiter;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.Filters;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Options;

public class InternalApiFilter : Attribute, IResourceFilter
{
    public void OnResourceExecuting(ResourceExecutingContext context)
    {
        IOptionsMonitor<JupiterSettings>? settings = context.HttpContext.RequestServices.GetService<IOptionsMonitor<JupiterSettings>>();

        bool isInternalPort = context.HttpContext.Connection.LocalPort == settings!.CurrentValue.InternalApiPort || 
            /* unit tests do not run on ports, we consider them always on the internal port */ (context.HttpContext.Connection.LocalPort == 0 && context.HttpContext.Connection.LocalIpAddress == null);
        if (!isInternalPort)
        {
            // this endpoint should only be exposed on the internal port, so we return a 404 as this is not on the internal port
            context.Result = new NotFoundResult();
        }
    }

    public void OnResourceExecuted(ResourceExecutedContext context)
    {
    }
}
