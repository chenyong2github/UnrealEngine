import { BranchDefs } from './branchdefs';
import { BranchGraph } from './branchgraph';
import { ContextualLogger } from '../common/logger';
import { PerforceContext } from '../common/perforce';
import { Status } from './status';

const logger = new ContextualLogger('preview')
let p4: PerforceContext | null = null

let branchSpecsRootPath = ''
export function init(root: string) {
	branchSpecsRootPath = root
}

export async function getPreview(cl: number, bot: string) {
	// @todo print content from shelf
	if (!p4) {
		p4 = new PerforceContext(logger)
	}

	const path = `${branchSpecsRootPath}/${bot}.branchmap.json` 

	const fileText = await p4.print(`${path}@=${cl}`)

	const validationErrors: string[] = []
	const result = BranchDefs.parseAndValidate(validationErrors, fileText)

	if (!result.branchGraphDef) {
		throw new Error(validationErrors.length === 0 ? 'Failed to parse' : validationErrors.join('\n'))
	}

	const graph = new BranchGraph(bot)
	graph.config = result.config
	graph._initFromBranchDefInternal(result.branchGraphDef)

	const status = new Status(new Date(), 'preview', logger)
	for (const branch of graph.branches) {
		status.addBranch(branch)
	}

	return status
}
