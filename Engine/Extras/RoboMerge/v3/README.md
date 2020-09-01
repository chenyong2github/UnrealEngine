## Robomerge Documentation
<!-- The 'toc' comment is required by the marked-toc plugin and for roboserver.ts to insert a table of contents. -->
<!-- toc -->

### Quick Reference
#### Changelist Description Flags
**The following flags can be added to RoboMerge commands inside your Perforce commit description:**

| Symbol  | Name | Kind   | Example                   | Description                                                                                                                                                          |
|---------|------|--------|---------------------------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| -       | skip | branch | `#robomerge -7.40`        | Do not merge to the specified branch (used for branches that are merged to automatically)                                                                            |
| ignore  |      | global | `#robomerge ignore`       | Completely ignore this changelist - no automatic merges happen. Note that a target branch of 'ignore' is interpreted as an ignore flag (can be used without a #).    |
| deadend |      | global | `#robomerge deadend`      | Completely ignore this changelist - no automatic merges happen. Note that a target branch of 'deadend' is interpreted as a deadend flag (can be used without a #).   |
| #review |      | global | `#robomerge #review 7.40` | Add #codereview for owner for each commit in merge chain                                                                                                             |
| #manual |      | global | `#robomerge #manual`      | Do not commit merge - add #codereview and shelve for owner                                                                                                           |
| !       | null | branch | `#robomerge !7.40`        | Perform a null merge to the specified branch, i.e. convince Perforce that a merge has happened, but don't actually make any changes to the content of the stream.    |
| null    |      | global | `#robomerge null`         | Make every automatic merge of this commit a null merge (this is the old behavior of the deadend tag). Like ignore and deadend, can be used without a #.              |

Note that `#robomerge none` is now an error, due to confusion surrounding its usage.


### What is Robomerge?

#### Usage
RoboMerge monitors commits to Perforce streams. It reads commit messages to find commands telling it to merge between branches, and some branches are set up to be automatically merged. For example:

![Example Robomerge Graph](/img/RM-Example-Graph.png)

A common pattern is to configure RoboMerge to merge commits to Main into release and feature branches. In the above example, commits to **Release-5.0** are _automatically_ merged up to **Release-6.0**, and then up to **Main**. The merge paths for each of our development streams can be seen on the [RoboMerge admin page](/).

To request a changelist committed to Main to be merged down to **Release-6.0** and **Release-5.0**, the changelist description would include the line:

`#robomerge Release-6.0, Release-5.0`

Multiple branches can be separated by commas and/or spaces. Most branches are given short aliases, which can also be found on the admin page. This command could be simply written:

`#robomerge 5.0`

using the 5.0 alias and allowing RoboMerge to calculate the route from Main to 5.0. On the admin page, hover over the box for a stream to see what aliases have been set up.

#### Conflicts
If Perforce encounters a conflict when RoboMerge is merging a changelist:
- An email is sent to the author, warning of the conflict. Robomerge will also reach out via Slack with some common options to address the conflict
- RoboMerge will perform no further merges on the stream until the author unshelves the changes (or uses the Create a Shelf feature), resolves all conflicts and commits the generated changelist.
The admin page shows which streams RoboMerge is currently blocked on, including the author responsible for resolving the conflicts.

#### Example cases
Most of the changes are fine but one file should be deadended:

Two possibilities:

- Accept target on that file
- Exclude the file from the shelf, check in, and hit ignore on that one file (this may cause a problem for subsequent check ins that modify this file)

#### Bot config

Unless noted otherwise, edge properties can be specied on source nodes and will
apply to all outgoing edges (not allowed in default node properties). No check
mark in edge columns means the setting can currently *only* be specified on
source nodes. 

No default means null/empty string/empty list.

Question remains about whether list properties should be cumulative if specified
for both node and edge.


|name                      |used by|default|edge?|description                           |notes|
|--------------------------|:-----:|-------|-----|--------------------------------------|-----|
|`checkIntervalSecs`        |b |`30`        | |All edges are round-robinned within this time, unless delayed by integrations| |
|`noStreamAliases`          |b |`false`     | |Stream names not available for commands, e.g. if there are duplicate stream names in bot | |
|`reportToBuildHealth`      |b |`false`     | |UGS integration                          | |
|`slackChannel`             |b |            | |Channel to receive blockages             |Not practical to make this per edge|
|`visibility`               |bn|`['fte']`   | |Permissions to access bot/node on admin site| |
|`aliases`                  |n |            | |Alternative names for use in commands    | |
|`badgeProject`             |n |            | |UGS integration                          | |
|`depot`                    |n |**required**| |Depot, e.g. UE4                          |Flag currently called `defaultStreamDepot`|
|`rootPath`                 |n |from stream | |P4 depot full path                       | |
|`streamSubpath`            |n |`/...`      | |P4 depot sub-path                        | |
|`workspaceNameOverride`    |n |            | |Used specified workspace name for commits| |
|`enabled`                  |ne|`true`      | |If false, pretends node/edge doesn't exist| |
|`forcePause`               |ne|            | |If flag set, pause - applies each restart| |
|`additionalSlackChannelForBlockages` |e |  | |Single extra Slack channel               | |
|`blockAssetTargets`        |e |`false`     | |Reject integrations containing assets    | |
|`defaultIntegrationMethod` |e |normal      | |For edigrate                             | |
|`disallowSkip`             |e |            | |Remove skip option from UI               | |
|`emailOnBlockage`          |e |`true`      | |Email owners of conflicts?               | |
|`excludeAuthors`           |e |            | |Skip changes by these authors, e.g. skip `buildmachine` commits| |
|`ignoreBranchspecs`        |e |            | |Where branchspecs specified, ignore for this edge| |
|`incognitoMode`            |e |`false`     | |Terse description in committed changelists| |
|`initialCL`                |e |            | |First run only: which CL start _after_   | |
|`isDefaultBot`             |e |`false`     | |Run plain #robomerge commands? Should be `false` for streams monitored by multiple bots| |
|`lastGoodCLPath`           |e |            | |'Gate' file to read to find CIS-approved CL| |
|`maxFilesPerIntegration`   |e |`-1`        | |Reject integrations with more files than this| |
|`notify`                   |e |            | |Additional people to email on blockages  |Also `globalNotify`|
|`resolver`                 |e |            | |Single designated resolver               |Currently applies to both source and target nodes|
|`whitelist`                |e |            | |Only specified users can integrate using edge| |
