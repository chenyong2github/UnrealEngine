// Copyright Epic Games, Inc. All Rights Reserved.
const EDGE_STYLES = {
    roboshelf: [['color', 'purple'], ['arrowhead', 'diamond']],
    forced: [],
    defaultFlow: [['color', 'blue']],
    onRequest: [['color', 'darkgray'], ['style', 'dashed']],
    blockAssets: [['color', 'darkgray'], ['style', 'dashed'], ['arrowhead', 'odiamond']]
};
function setDefault(map, key, def) {
    const val = map.get(key);
    if (val) {
        return val;
    }
    map.set(key, def);
    return def;
}
function renderGraph(src) {
    const hpccWasm = window["@hpcc-js/wasm"];
    return hpccWasm.graphviz.layout(src, "svg", "dot");
}
function decoratedAlias(bot, alias) {
    return bot + ':' + alias;
}
function showFlowGraph(data, botNameParam) {
    let whitelist = null;
    let botName;
    if (Array.isArray(botNameParam)) {
        botName = null;
        if (botNameParam.length > 0) {
            whitelist = botNameParam.map(s => s.toUpperCase());
        }
    }
    else {
        botName = botNameParam;
    }
    const addLinesForBranchGraph = (outLines, bot, branchList) => {
        const aliases = new Map();
        const nodeLabels = new Map();
        function getIdForBot(bot) {
            return (alias) => {
                const info = aliases.get(decoratedAlias(bot, alias));
                return info ? info.id : alias;
            };
        }
        // if showing all bots, build up map from stream to nodes
        if (!botName) {
            const streamMap = new Map();
            for (let branchStatus of branchList) {
                const [color, streams] = setDefault(streamMap, branchStatus.def.rootPath, [null, []]);
                streams.push(branchStatus);
                const nodeColor = branchStatus.def.config.graphNodeColor;
                if (!color && nodeColor) {
                    streamMap.set(branchStatus.def.rootPath, [nodeColor, streams]);
                }
            }
            // create a shared info for each set of nodes monitoring the same stream
            for (const [k, v] of streamMap) {
                const [color, streams] = v;
                if (streams.length > 1) {
                    const info = {
                        id: k.replace(/[^\w]+/g, '_'), tooltip: 'tooltip todo', name: k
                    };
                    if (color) {
                        info.graphNodeColor = color;
                    }
                    for (const branchStatus of streams) {
                        for (const alias of branchStatus.def.aliases) {
                            aliases.set(decoratedAlias(branchStatus.bot, alias), info);
                        }
                    }
                    setDefault(nodeLabels, 'nogroup', []).push(info);
                }
            }
        }
        // build map of all aliases and list of node information
        for (let branchStatus of branchList) {
            const branch = branchStatus.def;
            if (aliases.has(decoratedAlias(branchStatus.bot, branch.upperName)))
                continue;
            let tooltip = branch.rootPath;
            if (branch.aliases && branch.aliases.length !== 0) {
                tooltip += ` (${branch.aliases.join(', ')})`;
            }
            const info = {
                id: `_${branchStatus.bot}_${branch.upperName.replace(/[^\w]+/g, '_')}`,
                tooltip, name: branch.name
            };
            if (branch.config.graphNodeColor) {
                info.graphNodeColor = branch.config.graphNodeColor;
            }
            // note: branch.upperName is always in branch.aliases
            for (const alias of branch.aliases) {
                aliases.set(decoratedAlias(branchStatus.bot, alias), info);
            }
            setDefault(nodeLabels, botName ? 'nogroup' : branchStatus.bot, []).push(info);
        }
        const branchesById = new Map();
        for (const branchStatus of branchList) {
            const branch = branchStatus.def;
            const getId = getIdForBot(branchStatus.bot);
            const branchId = getId(branch.upperName);
            branchesById.set(branchId, {
                id: branchId,
                forcedDests: new Set(branch.forceFlowTo.map(getId)),
                defaultDests: new Set(branch.defaultFlow.map(getId)),
                blockAssetDests: new Set(branch.blockAssetTargets ? branch.blockAssetTargets.map(getId) : [])
            });
        }
        const linkOutward = new Set(), linkInward = new Set();
        const nodeImportance = new Map();
        // special case branches named Main
        nodeImportance.set(getIdForBot(bot)('MAIN'), 10);
        const links = [];
        for (const branchStatus of branchList) {
            const getId = getIdForBot(branchStatus.bot);
            const src = getId(branchStatus.def.upperName);
            const branch = branchesById.get(src);
            for (let flow of branchStatus.def.flowsTo) {
                const dst = getId(flow);
                nodeImportance.set(src, (nodeImportance.get(src) || 0) + 1);
                nodeImportance.set(dst, (nodeImportance.get(dst) || 0) + 1);
                const isForced = branch.forcedDests.has(dst);
                const edgeStyle = branchStatus.def.convertIntegratesToEdits ? 'roboshelf' :
                    isForced ? 'forced' :
                        branch.defaultDests.has(dst) ? 'defaultFlow' :
                            branch.blockAssetDests.has(dst) ? 'blockAssets' : 'onRequest';
                const styles = [...EDGE_STYLES[edgeStyle]];
                const link = { src, dst, styles };
                links.push(link);
                if (isForced) {
                    linkOutward.add(src + dst);
                }
                else if (edgeStyle === 'blockAssets' || edgeStyle === 'onRequest') {
                    linkInward.add(dst + src);
                }
            }
        }
        const nodeGroups = [];
        for (const [groupName, v] of nodeLabels) {
            const nodeInfo = [];
            nodeGroups.push([groupName, nodeInfo]);
            for (const info of v) {
                const { id, tooltip, name } = info;
                let factor = (Math.min(nodeImportance.get(id) || 0, 10) - 1) / 9;
                nodeInfo.push({
                    importance: factor,
                    marginX: (.2 * (1 - factor) + .4 * factor).toPrecision(1),
                    marginY: (.1 * (1 - factor) + .25 * factor).toPrecision(1),
                    fontSize: (14 * (1 - factor) + 20 * factor).toPrecision(2),
                    info: info
                });
            }
        }
        for (let [groupName, nodeInfo] of nodeGroups) {
            if (groupName !== 'nogroup') {
                outLines.push(`subgraph cluster_${groupName} {
	label="${groupName}";
`);
            }
            for (const info of nodeInfo) {
                const attrs = [
                    ['label', `"${info.info.name}"`],
                    ['tooltip', `"${info.info.tooltip}"`],
                    ['margin', `"${info.marginX},${info.marginY}"`],
                    ['fontsize', info.fontSize],
                ];
                if (info.importance > .5) {
                    attrs.push(['style', '"filled,bold"']);
                }
                if (info.info.graphNodeColor) {
                    attrs.push(['fillcolor', `"${info.info.graphNodeColor}"`]);
                }
                const attrStrs = attrs.map(([key, value]) => `${key}=${value}`);
                outLines.push(`${info.info.id} [${attrStrs.join(', ')}];`);
            }
            if (groupName !== 'nogroup') {
                outLines.push('}');
            }
        }
        for (const link of links) {
            // when there's forced/unforced pair, only the former is a constrait
            // (this makes flow )
            const combo = link.dst + link.src;
            if (combo && linkOutward.has(combo) && linkInward.has(combo)) {
                link.styles.push(['constraint', 'false']);
            }
            const styleStrs = link.styles.map(([key, value]) => `${key}=${value}`);
            const suffix = styleStrs.length === 0 ? '' : ` [${styleStrs.join(', ')}]`;
            outLines.push(`${link.src} -> ${link.dst}${suffix};`);
        }
    };
    let bots = new Map();
    for (let branch of data) {
        let bot;
        if (botName) {
            if (branch.bot !== botName)
                continue;
            bot = branch.bot;
        }
        else {
            if (whitelist && whitelist.indexOf(branch.bot) < 0)
                continue;
            bot = 'all';
        }
        let branchList = bots.get(bot);
        if (!branchList) {
            branchList = [];
            bots.set(bot, branchList);
        }
        branchList.push(branch);
    }
    let graphSrcIter = (function* () {
        let preface = [
            'digraph robomerge {',
            'fontname="sans-serif"; labelloc=top; fontsize=16;',
            'edge [penwidth=2]; nodesep=.7; ranksep=1.2;',
        ];
        for (let [bot, branchList] of bots) {
            let lines = preface.slice();
            lines.push(`label = "${bot}";`);
            lines.push(`tooltip="${bot} integration paths";`);
            lines.push(`node [shape=box, style=filled, fontname="sans-serif", fillcolor=moccasin];`);
            addLinesForBranchGraph(lines, bot, branchList);
            lines.push('}');
            yield [bot, lines.join('\n')];
        }
    })();
    let graphContainer = $('<div class="clearfix">');
    let flowGraph = $('<div class="flow-graph" style="display:inline-block;">').appendTo(graphContainer);
    let first = true;
    flowGraph.append($('<div>').css('text-align', 'center').text("Building graph..."));
    let present = (bot, graphSvg) => {
        if (graphSvg) {
            $('#graph-key-template')
                .clone()
                .removeAttr('id')
                .css('display', 'inline-block')
                .appendTo(graphContainer);
            $('#graph-loading-text').hide();
            let span = $('<span>').css('margin', 'auto').html(graphSvg);
            let svgEl = $('svg', span).addClass('branch-graph').removeAttr('width');
            // scale graph to 70% of default size
            let height = Math.round(parseInt(svgEl.attr('height')) * .7);
            svgEl.attr('height', height + 'px').css('vertical-align', 'top');
            let inserted = false;
            for (let existing of $('svg', flowGraph)) {
                let el = $(existing);
                if (parseInt(el.attr('height')) < height) {
                    span.insertBefore(el.parent());
                    inserted = true;
                    break;
                }
            }
            if (first) {
                first = false;
                flowGraph.empty();
            }
            if (!inserted)
                flowGraph.append(span);
        }
        const { value, done } = graphSrcIter.next();
        if (!done) {
            const [bot, src] = value;
            console.log(src);
            renderGraph(src).then(graphSvg => present(bot, graphSvg));
        }
    };
    present();
    return graphContainer;
}
