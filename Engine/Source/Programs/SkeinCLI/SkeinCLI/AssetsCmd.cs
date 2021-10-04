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

    [Command(Name = "assets", Description = "Executes assets related commands")]
    [Subcommand(
        typeof(AssetsTrackCmd),
        typeof(AssetsUntrackCmd),
        typeof(AssetsGetCmd),
        typeof(AssetsRenameCmd),
        typeof(AssetsRevertCmd))]
    class AssetsCmd : SkeinCmdBase
    {
        // This will automatically be set before OnExecute is invoked.
        private SkeinCmd Parent { get; set; }

        public override List<string> CreateArgs()
        {
            var args = Parent.CreateArgs();
            args.Add("assets");

            return args;
        }

        protected override Task<int> OnExecute(CommandLineApplication app)
        {
            //'skein assets' is an incomplete command, so we show help here even if --help is not specified
            app.ShowHelp();
            return base.OnExecute(app);
        }
    }

    #region assets track

    [Command(Name = "track", Description = "Track an asset for the project")]
    class AssetsTrackCmd : SkeinCmdBase
    {
        private AssetsCmd Parent { get; set; }

        [Argument(0, Name = "filepath", Description = "File path for the asset to track")]
        [FileExists]
        [Required]
        public string FilePath { get; set; }

        public override List<string> CreateArgs()
        {
            var args = Parent.CreateArgs();
            args.Add("track");

            return args;
        }

        protected override Task<int> OnExecute(CommandLineApplication app)
        {
            Log.Logger.ForContext<ProjectsSnapshotsGetCmd>().Information($"File Path: {FilePath}");
            Log.Logger.ForContext<AssetsTrackCmd>().Information("Result of track command here [...]");
            return base.OnExecute(app);
        }
    }

    #endregion

    #region assets untrack

    [Command(Name = "untrack", Description = "Untrack an asset for the project")]
    class AssetsUntrackCmd : SkeinCmdBase
    {
        private AssetsCmd Parent { get; set; }

        [Argument(0, Name = "filepath", Description = "File path for the asset to untrack")]
        [FileExists]
        [Required]
        public string FilePath { get; set; }

        public override List<string> CreateArgs()
        {
            var args = Parent.CreateArgs();
            args.Add("untrack");

            return args;
        }

        protected override Task<int> OnExecute(CommandLineApplication app)
        {
            Log.Logger.ForContext<ProjectsSnapshotsGetCmd>().Information($"File Path: {FilePath}");
            Log.Logger.ForContext<AssetsUntrackCmd>().Information("Result of untrack command here [...]");
            return base.OnExecute(app);
        }
    }

    #endregion

    #region assets get

    [Command(Name = "get", Description = "Get an asset, update skein.yml")]
    class AssetsGetCmd : SkeinCmdBase
    {
        private AssetsCmd Parent { get; set; }

        [Argument(0, Name = "asset-id", Description = "uuid of the asset to get")]
        [Required]
        public string AssetId { get; set; }

        [Argument(1, Name = "path", Description = "Local path where to download the asset, defaults to the current path")]
        [DirectoryExists]
        public string Path { get; set; }

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
            Log.Logger.ForContext<ProjectsSnapshotsGetCmd>().Information($"AssetId: {AssetId}, Path: {Path}");
            Log.Logger.ForContext<AssetsGetCmd>().Information("Result of assets get described here [...]");
            return base.OnExecute(app);
        }
    }

    #endregion

    #region assets rename

    [Command(Name = "rename", Description = "Rename an asset and update skein.yml")]
    class AssetsRenameCmd : SkeinCmdBase
    {
        private AssetsCmd Parent { get; set; }

        [Argument(0, Name = "old-filepath", Description = "File path for the asset to rename")]
        [FileExists]
        [Required]
        public string OldFilePath { get; set; }

        [Argument(1, Name = "new-filepath", Description = "New path for the renamed asset")]
        [FileNotExists]
        [Required]
        public string NewFilePath { get; set; }

        public override List<string> CreateArgs()
        {
            var args = Parent.CreateArgs();
            args.Add("rename");

            return args;
        }

        protected override Task<int> OnExecute(CommandLineApplication app)
        {
            Log.Logger.ForContext<ProjectsSnapshotsGetCmd>().Information($"Old File Path: {OldFilePath}, New File Path: {NewFilePath}");
            Log.Logger.ForContext<AssetsRenameCmd>().Information("Result of assets rename described here [...]");
            return base.OnExecute(app);
        }
    }

    #endregion

    #region assets revert

    [Command(Name = "revert", Description = "Revert the project to the snapshot")]
    class AssetsRevertCmd : SkeinCmdBase
    {
        private AssetsCmd Parent { get; set; }

        [Argument(0, Name = "filepath", Description = "File path for the asset to revert")]
        [FileExists]
        [Required]
        public string FilePath { get; set; }

        public override List<string> CreateArgs()
        {
            var args = Parent.CreateArgs();
            args.Add("revert");

            return args;
        }

        protected override Task<int> OnExecute(CommandLineApplication app)
        {
            bool proceed = true;
            if (!Quiet)
            {
                proceed = Prompt.GetYesNo($"Are you sure you want to revert the asset at {FilePath}?",
                    defaultAnswer: true,
                    promptColor: ConsoleColor.Black,
                    promptBgColor: ConsoleColor.White);
            }

            if (proceed)
            {
                Log.Logger.ForContext<ProjectsSnapshotsGetCmd>().Information($"File Path: {FilePath}");
                Log.Logger.ForContext<AssetsRevertCmd>().Information("Assets Revert operation result here [...]");
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