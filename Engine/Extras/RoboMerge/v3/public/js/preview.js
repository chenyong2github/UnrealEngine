// Copyright Epic Games, Inc. All Rights Reserved.
"use strict"

function doit(cl, bot) {
	$.get(`/preview?cl=${cl}&bot=${bot}`)
	.then(data => {
		$('#graph').append(showFlowGraph(JSON.parse(data).allBranches, bot.toUpperCase()));
		$('#success-panel').show();
	})
	.catch(error => {

		const $errorPanel = $('#error-panel');
		$('pre', $errorPanel).text(error.responseText.replace(/\t/g, '    '));
		$errorPanel.show();
	});
}

window.doPreview = doit;
