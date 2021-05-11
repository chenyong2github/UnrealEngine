import { BranchDefs } from './branchdefs';
import { BranchGraph } from './branchgraph';
import { ContextualLogger } from '../common/logger';
import { PerforceContext } from '../common/perforce';
import { Status } from './status';

const logger = new ContextualLogger('preview')
let p4: PerforceContext | null = null

// let branchSpecsRootPath = ''
export function init(_root: string) {
	// branchSpecsRootPath = root
}

export async function getPreview(cl: number, singleBot?: string) {
	// @todo print content from shelf
	if (!p4) {
		p4 = new PerforceContext(logger)
	}

	const status = new Status(new Date(), 'preview', logger)

	const bots: [string, string][] = []
	for (const entry of (await p4.describe(cl, undefined, true)).entries) {
		const match = entry.depotFile.match(/.*\/(.*)\.branchmap\.json$/)
		if (match) {
			bots.push([match[1], match[0]])
		}
	}

	const allStreamSpecs = await p4.streams()

	for (const [bot, path] of bots) {
		if (singleBot && bot.toLowerCase() !== singleBot.toLowerCase()) {
			continue
		}
		const fileText = await p4.print(`${path}@=${cl}`)

		const validationErrors: string[] = []
		const result = BranchDefs.parseAndValidate(validationErrors, fileText, allStreamSpecs)

		if (!result.branchGraphDef) {
			throw new Error(validationErrors.length === 0 ? 'Failed to parse' : validationErrors.join('\n'))
		}

		const graph = new BranchGraph(bot)
		graph.config = result.config
		graph._initFromBranchDefInternal(result.branchGraphDef)

		for (const branch of graph.branches) {
			status.addBranch(branch)
		}
	}

	return status
}
