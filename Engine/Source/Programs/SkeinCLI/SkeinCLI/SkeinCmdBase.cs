// Copyright Epic Games, Inc. All Rights Reserved.

using McMaster.Extensions.CommandLineUtils;
using Serilog;
using System;
using System.Collections.Generic;
using System.Threading.Tasks;

namespace SkeinCLI
{
    [HelpOption("--help")]
    abstract class SkeinCmdBase
    {
        public abstract List<string> CreateArgs();
        
        [Option("-q|--quiet", CommandOptionType.NoValue, Description = "Execute in quiet mode")]
        public bool Quiet { get; set; }

        protected virtual Task<int> OnExecute(CommandLineApplication app)
        {
            var args = CreateArgs();
            if (Quiet)
            {
                args.Add("-q");
            }

            Log.Logger.ForContext<SkeinCmdBase>().Debug("Executing: skein " + ArgumentEscaper.EscapeAndConcatenate(args));
            return Task.FromResult(0);
        }

        protected void OnException(Exception ex)
        {
            Log.Logger.ForContext<SkeinCmdBase>().Error(ex, "Exception");
        }
    }
}