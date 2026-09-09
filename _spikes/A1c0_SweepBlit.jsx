/*
	A1c0_SweepBlit.jsx (Onion Skin) - Phase 0, spike A1c, step 0.

	THE QUESTION:

	  saveBlittedImageToPng takes (boolean, File, integer, boolean) and is
	  undocumented. What are the four parameters, and which values are legal?

	Do not guess a signature - map it. ExtendScript throws on an out-of-domain
	argument, and the throws ARE the measurement: they draw the boundary of the
	integer enum. Every call that succeeds writes a PNG, and the DIMENSIONS and
	HASHES of those PNGs say what each parameter changed.

	Sweep: b1 in {false,true} x n in 0..7 x b2 in {false,true} = 32 calls.
	Files are named A1c0_<b1>_<n>_<b2>.png so the analyser can key on them.

	Deliberately NOT answering the pan question yet. That comes after the
	signature is known, because a pan test run through a misunderstood argument
	proves nothing.

	HOW TO RUN: once, with a comp open and the comp viewer focused. This writes
	up to 32 PNGs beside the script - it is a spike directory, that is fine.

	Reads and writes files. Changes nothing in AE.
*/

(function () {

	var here = new File($.fileName).parent;

	var L = [];
	function say(s) { L.push(String(s)); }

	say("A1c0 signature sweep - " + new Date().toString());
	say("AE version: " + app.version);

	var v = app.activeViewer;
	if (!v) { alert("No active viewer. Click the comp viewer and retry."); return; }

	var view = v.views[v.activeViewIndex];
	say("zoom = " + view.options.zoom);

	var c = app.project.activeItem;
	if (c && c instanceof CompItem) {
		say("comp = \"" + c.name + "\"  " + c.width + " x " + c.height +
		    "  par=" + c.pixelAspect +
		    "  res=" + c.resolutionFactor[0] + "," + c.resolutionFactor[1]);
		say("whole-comp*zoom would be " +
		    Math.round(c.width * view.options.zoom) + " x " +
		    Math.round(c.height * view.options.zoom));
	}

	//	First, confirm the arity claim itself rather than trusting the report.
	say("");
	say("reported arity check:");
	try {
		view.saveBlittedImageToPng();
		say("  0 args: NO THROW (unexpected)");
	} catch (e) {
		say("  0 args throws: " + e);
	}

	say("");
	say("sweep (b1, File, n, b2):");

	var bools = [false, true];
	var okCount = 0, errCount = 0;
	var seenErr = {};

	for (var i = 0; i < bools.length; i++) {
		for (var n = 0; n <= 7; n++) {
			for (var j = 0; j < bools.length; j++) {
				var b1 = bools[i], b2 = bools[j];
				var name = "A1c0_" + (b1 ? "T" : "F") + "_" + n + "_" + (b2 ? "T" : "F") + ".png";
				var f = new File(here.fsName + "/" + name);
				if (f.exists) { try { f.remove(); } catch (e0) {} }

				var tag = "  (" + (b1 ? "true " : "false") + ", f, " + n + ", " +
				          (b2 ? "true " : "false") + ") ";
				try {
					view.saveBlittedImageToPng(b1, f, n, b2);
					if (f.exists && f.length > 0) {
						say(tag + "OK   " + f.length + " bytes  -> " + name);
						okCount++;
					} else {
						say(tag + "returned but wrote NO FILE");
						errCount++;
					}
				} catch (e) {
					var msg = String(e);
					say(tag + "THROW " + msg);
					seenErr[msg] = (seenErr[msg] || 0) + 1;
					errCount++;
				}
			}
		}
	}

	say("");
	say("ok=" + okCount + "  failed=" + errCount);
	say("");
	say("distinct error messages:");
	var any = false;
	for (var k in seenErr) {
		if (seenErr.hasOwnProperty(k)) { say("  x" + seenErr[k] + "  " + k); any = true; }
	}
	if (!any) say("  none");

	say("");
	say("NEXT: run A1c0_analyse.py - it compares dimensions and hashes across");
	say("the surviving combinations to say what each parameter actually does.");

	var text = L.join("\n");

	var log = new File(here.fsName + "/A1c0_log.txt");
	log.encoding = "UTF-8";
	if (log.open("w")) { log.write(text); log.close(); }

	alert(text.length > 3200 ? text.substr(0, 3200) + "\n\n[...see A1c0_log.txt]" : text);

})();
