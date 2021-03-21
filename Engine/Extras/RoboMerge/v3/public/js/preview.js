// Copyright Epic Games, Inc. All Rights Reserved.
"use strict"

function doit(query) {
	$.get(query)
	.then(data => {
		const botNames = new Set
		const allBranches = JSON.parse(data).allBranches
		for (const branch of allBranches) {
			botNames.add(branch.bot)
		}
		$('#graph').append(showFlowGraph(allBranches, []));
		$('.bots').html([...botNames].map(s => `<tt>${s.toLowerCase()}</tt>`).join(', '))
		$('#success-panel').show();
	})
	.catch(error => {

		const $errorPanel = $('#error-panel');
		$('pre', $errorPanel).text(error.responseText.replace(/\t/g, '    '));
		$errorPanel.show();
	});
}

window.doPreview = doit;
