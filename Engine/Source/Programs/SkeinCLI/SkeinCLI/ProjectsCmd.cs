// Copyright Epic Games, Inc. All Rights Reserved.

using McMaster.Extensions.CommandLineUtils;
using Serilog;
using System;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.IO;
using System.Threading.Tasks;

namespace SkeinCLI
{

    [Command(Name = "projects", Description = "Executes project related commands")]
    [Subcommand(
        typeof(ProjectsListCmd),
        typeof(ProjectsSnapshotsCmd),
        typeof(ProjectsStatusCmd),
        typeof(ProjectsRevertCmd))]
    class ProjectsCmd : SkeinCmdBase
    {
        // This will automatically be set before OnExecute is invoked.
        private SkeinCmd Parent { get; set; }

        public override List<string> CreateArgs()
        {
            var args = Parent.CreateArgs();
            args.Add("projects");

            return args;
        }

        protected override Task<int> OnExecute(CommandLineApplication app)
        {
            //'skein projects' is an incomplete command, so we show help here even if --help is not specified
            app.ShowHelp();
            return base.OnExecute(app);
        }
    }

    #region projects list

    [Command(Name = "list", Description = "Get a list of projects")]
    class ProjectsListCmd : SkeinCmdBase
    {
        private ProjectsCmd Parent { get; set; }

        public override List<string> CreateArgs()
        {
            var args = Parent.CreateArgs();
            args.Add("list");

            return args;
        }

        protected override Task<int> OnExecute(CommandLineApplication app)
        {
			/*
			 * TODO: Remove this.
			 * This is a test implementation to verify the behaviour of the -j flag
			 */
			Random rnd = new Random();
			if (rnd.NextDouble() > .5)
			{
				List<string> projects = new List<string>() { "project1", "testProject", "this is a test project name" };
				Output("Retrieved projects", projects);
				return base.OnExecute(app);
			}
			else 
			{
				OutputError(403, "Cannot reach server");
				base.OnExecute(app);
				return Task.FromResult(403);
			}
        }
    }

    #endregion

    #region projects snapshots

    [Command(Name = "snapshots", Description = "Executes project snapshots related commands")]
    [Subcommand(
        typeof(ProjectsSnapshotsGetCmd),
        typeof(ProjectsSnapshotsCreateCmd))]
    class ProjectsSnapshotsCmd : SkeinCmdBase
    {
        private ProjectsCmd Parent { get; set; }

        public override List<string> CreateArgs()
        {
            var args = Parent.CreateArgs();
            args.Add("snapshots");

            return args;
        }

        protected override Task<int> OnExecute(CommandLineApplication app)
        {
            //'skein projects snapshots' is an incomplete command, so we show help here even if --help is not specified
            app.ShowHelp();
            return base.OnExecute(app);
        }
    }

    [Command(Name = "get", Description = "Download a project snapshot locally")]
    class ProjectsSnapshotsGetCmd : SkeinCmdBase
    {
        private ProjectsSnapshotsCmd Parent { get; set; }

        [Argument(0, Name = "project-id", Description = "uuid of the project to get")]
        [Required]
        public string ProjectId { get; set; }

        [Argument(1, Name = "path", Description = "Local path where to save the snapshot, defaults to the current path")]
        [DirectoryExists]
        public string Path { get; set; }

        [Option("-s|--snapshot", CommandOptionType.SingleValue, Description = "Version of the snapshot to download")]
        public string Snapshot { get; set; }

        public override List<string> CreateArgs()
        {
            var args = Parent.CreateArgs();
            args.Add("get");

            return args;
        }

        protected override Task<int> OnExecute(CommandLineApplication app)
        {
            if (string.IsNullOrEmpty(Path))
            {
                Path = Directory.GetCurrentDirectory();
            }
            Log.Logger.ForContext<ProjectsSnapshotsGetCmd>().Information($"Snapshot: {Snapshot}, ProjectId: {ProjectId}, Path: {Path}");
            Log.Logger.ForContext<ProjectsSnapshotsGetCmd>().Information("Download progress shown here [...]");
            return base.OnExecute(app);
        }
    }

    [Command(Name = "create", Description = "Create a snapshot of a project and upload assets")]
    class ProjectsSnapshotsCreateCmd : SkeinCmdBase
    {
        private ProjectsSnapshotsCmd Parent { get; set; }

        [Argument(0, Name = "path", Description = "Local path from where to create the snapshot, defaults to the current path")]
        [DirectoryExists]
        public string Path { get; set; }

        [Option("-m|--message", CommandOptionType.SingleValue, Description = "snapshot description")]
        [Required]
        public string Message { get; set; }

        public override List<string> CreateArgs()
        {
            var args = Parent.CreateArgs();
            args.Add("create");

            return args;
        }

        protected override Task<int> OnExecute(CommandLineApplication app)
        {
            if (string.IsNullOrEmpty(Path))
            {
                Path = Directory.GetCurrentDirectory();
            }
            Log.Logger.ForContext<ProjectsSnapshotsGetCmd>().Information($"Path: {Path}, Message: {Message}");
            Log.Logger.ForContext<ProjectsSnapshotsCreateCmd>().Information("Create operation result shown here [...]");
            return base.OnExecute(app);
        }
    }

    #endregion
    
    #region projects status

    [Command(Name = "status", Description = "List the all project assets and if they are modified locally")]
    class ProjectsStatusCmd : SkeinCmdBase
    {
        private ProjectsCmd Parent { get; set; }

        [Argument(0, Name = "path", Description = "Local path for which to request the status, defaults to the current path")]
        [DirectoryExists]
        public string Path { get; set; }

        public override List<string> CreateArgs()
        {
            var args = Parent.CreateArgs();
            args.Add("status");

            return args;
        }

        protected override Task<int> OnExecute(CommandLineApplication app)
        {
            if (string.IsNullOrEmpty(Path))
            {
                Path = Directory.GetCurrentDirectory();
            }
            Log.Logger.ForContext<ProjectsSnapshotsGetCmd>().Information($"Path: {Path}");
            Log.Logger.ForContext<ProjectsStatusCmd>().Information("Status described here [...]");
            return base.OnExecute(app);
        }
    }

    #endregion

    #region projects revert

    [Command(Name = "revert", Description = "Revert the project to the snapshot")]
    class ProjectsRevertCmd : SkeinCmdBase
    {
        private ProjectsCmd Parent { get; set; }

        [Argument(0, Name = "path", Description = "Local path for which to revert the project, defaults to the current path")]
        [DirectoryExists]
        public string Path { get; set; }

        public override List<string> CreateArgs()
        {
            var args = Parent.CreateArgs();
            args.Add("revert");

            return args;
        }

        protected override Task<int> OnExecute(CommandLineApplication app)
        {
            if (string.IsNullOrEmpty(Path))
            {
                Path = Directory.GetCurrentDirectory();
            }

            bool proceed = true;
            if (!Quiet)
            {
                proceed = Prompt.GetYesNo($"Are you sure you want to revert the project at {Path}?",
                    defaultAnswer: true,
                    promptColor: ConsoleColor.Black,
                    promptBgColor: ConsoleColor.White);
            }

            if (proceed)
            {
                Log.Logger.ForContext<ProjectsSnapshotsGetCmd>().Information($"Path: {Path}");
                Log.Logger.ForContext<ProjectsRevertCmd>().Information("Revert operation result here [...]");
            }
            else 
            {
                Log.Logger.ForContext<ProjectsRevertCmd>().Information("User cancelled operation");
            }
            return base.OnExecute(app);
        }
    }

    #endregion
}