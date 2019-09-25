var render = (function() {
	var GraphViz = __graphviz_asm_Module;

	var graphviz = null;
	var error;
	return function(src) {
		if (graphviz === null) {
			graphviz = GraphViz(function(err) {
				error += graphviz['Pointer_stringify'](err);
			});
		}

		var call = graphviz['ccall'];
		var toString = function(pointer) {
			var result = graphviz['Pointer_stringify'](pointer);
			graphviz['_free'](result);
			return result;
		}

		error = '';
		// do preparse? - check for balanced [] and <> (and {}?), need to ignore strings and do string escaping
		var resultPointer = call('vizRenderFromString', 'number', ['string', 'string', 'string'], [src, 'svg', 'dot']);
		return resultPointer === 0 ? ['error', error] : ['success', toString(resultPointer)];
	};
})();

/*
var Pointer_stringify = function(ptr, length) {
	if (length === 0 || !ptr)
		return "";
	var hasUtf = 0; var i = 0;
	while(1) {
		var t = HEAPU8[ptr+i>>0];
		hasUtf |= t;
		if (t==0 && !length)
			break;
		i++;
		if (length && i==length)
			break;
	}
	if (!length) length=i;
	var ret="";
	if (hasUtf<128) {
		var MAX_CHUNK=1024;
		var curr;
		while(length>0) {
			curr=String.fromCharCode.apply(String, HEAPU8.subarray(ptr, ptr + Math.min(length, MAX_CHUNK)));
			ret = ret ? ret+curr : curr;
			ptr += MAX_CHUNK;
			length -= MAX_CHUNK
		}
	}
	return ret;
}
*/
