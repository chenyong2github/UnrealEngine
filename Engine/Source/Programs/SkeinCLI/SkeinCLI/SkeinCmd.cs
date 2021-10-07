// Copyright Epic Games, Inc. All Rights Reserved.

using McMaster.Extensions.CommandLineUtils;
using System.Collections.Generic;
using System.Reflection;
using System.Threading.Tasks;

namespace SkeinCLI
{
    [Command(Name = "skein", UnrecognizedArgumentHandling = UnrecognizedArgumentHandling.Throw, OptionsComparison = System.StringComparison.InvariantCultureIgnoreCase)]
    [VersionOptionFromMember("--version", MemberName = nameof(GetVersion))]
    [Subcommand(
        typeof(AuthCmd),
        typeof(ProjectsCmd),
        typeof(AssetsCmd))]
    class SkeinCmd : SkeinCmdBase
    {
        protected override Task<int> OnExecute(CommandLineApplication app)
        {
            // this shows help even if the --help option isn't specified
            app.ShowHelp();
			return base.OnExecute(app);
        }

        private static string GetVersion()
            => typeof(SkeinCmd).Assembly.GetCustomAttribute<AssemblyInformationalVersionAttribute>().InformationalVersion;

        public override List<string> CreateArgs()
        {
            var args = new List<string>();
            return args;
        }
    }
}