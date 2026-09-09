/*
	A1c1_MakeCalib.jsx (Onion Skin) - Phase 0, spike A1c.

	Builds the calibration comp A1 is measured against, and reports the live
	viewer zoom for the same moment.

	WHY GENERATED, NOT HAND-BUILT. The whole measurement is "predicted screen
	position vs actual, within 1px". That is only as good as our certainty about
	where the markers ARE in comp space. Placing them by hand introduces exactly
	the error we are trying to measure.

	THE COMP. Size and pixel aspect are both asked for. Mid-grey ground - grey,
	not black, so that a failed screen capture (which comes back flat black)
	cannot be mistaken for a successful capture of the background.

	Five markers, each a distinct saturated colour so detection is a colour
	distance and never a shape guess. Four near the corners plus one at centre:
	the corners give scale and translation, and the centre is a REDUNDANT point
	the solve does not need - which makes it a free residual check. A solve that
	fits four points perfectly and misses the fifth is wrong in a way four
	points alone could never reveal.

	Marker centres are PROPORTIONAL to the comp size (the same proportions the
	original 1920x1080 layout used), so a smaller comp keeps the layout:
	    red     ( 8.33%, 14.81%)      green   (91.67%, 14.81%)
	    blue    ( 8.33%, 85.19%)      yellow  (91.67%, 85.19%)
	    magenta (50%,    50%)   <- the redundant one

	Writes A1c1_calib.json beside the script: comp size, marker table, and the
	viewer zoom at build time. The solver reads it, so the truth lives in one
	place and is never retyped.

	HOW TO RUN: File > Scripts > Run Script File. Then fit the comp in the
	viewer, and run os_A1c1.exe to capture.
*/

(function () {

	//	COMP SIZE IS A PARAMETER, and it has to be.
	//
	//	The markers span 760 comp-pixels vertically. At 100% zoom that needs a
	//	panel over 760 px tall; the first run's viewer was 449, so every 100% state
	//	failed with "missing corner markers" - seven captures that were IMPOSSIBLE
	//	as specified, not badly executed. Shrinking the comp for the high-zoom
	//	states keeps the markers on screen while still exercising the transform at
	//	scale 1.0, which is the thing being measured.
	//
	//	Rule of thumb: comp height <= panel height / zoom.
	var W = 1920, H = 1080;
	{
		var sz = prompt("Comp size?  WIDTHxHEIGHT" +
		                "   -   1920x1080 for the fit and 50% states," +
		                "   640x360 for the 100% states" +
		                "   (the markers must fit inside the panel)",
		                "1920x1080", "A1 calibration");
		if (sz === null) return;
		var mm = /^\s*(\d+)\s*[xX*]\s*(\d+)\s*$/.exec(sz);
		if (mm) { W = parseInt(mm[1], 10); H = parseInt(mm[2], 10); }
	}
	var S = Math.max(8, Math.round(W / 40));		// marker square side

	//	A1's 12 states include a NON-SQUARE pixel aspect comp, because a viewer
	//	transform that is right for square pixels can be wrong for anamorphic
	//	ones in a way nothing else in the matrix would reveal. Ask rather than
	//	hardcode, so that state is reachable without editing the script.
	//	1.0 = square; 2.0 = anamorphic; AE's D1/DV NTSC widescreen is 1.21.
	var PAR = 1.0;
	{
		var ans = prompt("Pixel aspect ratio for the calibration comp?\n" +
		                 "1.0 = square (the normal case)\n" +
		                 "2.0 = anamorphic (the non-square state A1 requires)",
		                 "1.0", "A1 calibration");
		if (ans === null) return;			// cancelled
		var p = parseFloat(ans);
		if (!isNaN(p) && p > 0.1 && p < 4) PAR = p;
	}

	//	Fractions of the comp, so the layout follows the size. Same proportions
	//	as the original 1920x1080 layout (160/1760 and 160/920).
	function MX(fx, fy) { return [Math.round(W * fx), Math.round(H * fy)]; }
	var L0 = MX(0.083333, 0.148148), R0 = MX(0.916667, 0.851852), C0 = MX(0.5, 0.5);
	var MARKERS = [
		{ name: "cal_red",     x: L0[0], y: L0[1], c: [1, 0, 0] },
		{ name: "cal_green",   x: R0[0], y: L0[1], c: [0, 1, 0] },
		{ name: "cal_blue",    x: L0[0], y: R0[1], c: [0, 0, 1] },
		{ name: "cal_yellow",  x: R0[0], y: R0[1], c: [1, 1, 0] },
		{ name: "cal_magenta", x: C0[0], y: C0[1], c: [1, 0, 1] }
	];

	app.beginUndoGroup("A1 calibration comp");

	//	Reuse the comp if it is already there, so repeated runs do not litter
	//	the project with near-identical comps.
	var comp = null;
	for (var i = 1; i <= app.project.numItems; i++) {
		var it = app.project.item(i);
		if (it instanceof CompItem && it.name === "A1_CALIB") { comp = it; break; }
	}
	if (comp) {
		while (comp.numLayers > 0) comp.layer(1).remove();
		comp.pixelAspect = PAR;
	} else {
		comp = app.project.items.addComp("A1_CALIB", W, H, PAR, 10, 24);
	}

	//	Ground first, so it ends up at the bottom of the stack.
	var bg = comp.layers.addSolid([0.25, 0.25, 0.25], "cal_bg", W, H, PAR);
	bg.locked = false;

	for (var m = MARKERS.length - 1; m >= 0; m--) {
		var d = MARKERS[m];
		var L = comp.layers.addSolid(d.c, d.name, S, S, PAR);
		L.property("Position").setValue([d.x, d.y]);
	}

	//	DESELECT EVERYTHING. addSolid leaves the last layer created selected, and
	//	AE draws a selection bounding box and handles OVER it in the viewer. In
	//	the first run that ate ~82% of the red marker (69 detected pixels where
	//	the other four found 361) and biased its centroid by ~1.1 px - which is
	//	most of A1's entire 1 px budget, sitting in a corner the fit depends on.
	//	The measurement cannot be allowed to depend on what happens to be
	//	selected.
	for (var s = 1; s <= comp.numLayers; s++) comp.layer(s).selected = false;

	comp.openInViewer();

	//	Same argument for the overlays AE draws on top of the image: rulers and
	//	guides put pixels over the markers. Turn off what scripting can reach.
	//	AFTER openInViewer, so these are the options of the view we will actually
	//	capture rather than whichever view happened to be active before.
	try {
		var vv = app.activeViewer;
		if (vv) {
			var vo = vv.views[vv.activeViewIndex].options;
			vo.guidesVisibility = false;
			vo.rulers = false;
		}
	} catch (e) {}

	app.endUndoGroup();

	/* ---------------------------------------------------------------- */

	var zoom = null, viewerType = null;
	try {
		var v = app.activeViewer;
		if (v) {
			viewerType = v.type;
			zoom = v.views[v.activeViewIndex].options.zoom;
		}
	} catch (e) {}

	var j = [];
	j.push('{');
	j.push('  "comp": "A1_CALIB",');
	j.push('  "width": ' + W + ',');
	j.push('  "height": ' + H + ',');
	j.push('  "pixelAspect": ' + PAR + ',');
	j.push('  "markerSize": ' + S + ',');
	j.push('  "viewerType": ' + (viewerType === null ? 'null' : viewerType) + ',');
	j.push('  "zoomAtBuild": ' + (zoom === null ? 'null' : zoom) + ',');
	j.push('  "markers": [');
	for (var k = 0; k < MARKERS.length; k++) {
		var d2 = MARKERS[k];
		j.push('    {"name": "' + d2.name + '", "x": ' + d2.x + ', "y": ' + d2.y +
		       ', "rgb": [' + d2.c[0] + ', ' + d2.c[1] + ', ' + d2.c[2] + ']}' +
		       (k < MARKERS.length - 1 ? ',' : ''));
	}
	j.push('  ]');
	j.push('}');
	var text = j.join("\n");

	var here = new File($.fileName).parent;
	var f = new File(here.fsName + "/A1c1_calib.json");
	f.encoding = "UTF-8";
	if (f.open("w")) { f.write(text); f.close(); }

	alert("A1_CALIB built and opened.\n\n" +
	      "viewer zoom now: " + zoom + "\n\n" +
	      "NEXT:\n" +
	      "  1. Leave the comp viewer showing A1_CALIB.\n" +
	      "  2. Run os_A1c1.exe from the spikes folder.\n" +
	      "  3. Hover the cursor over the viewer IMAGE AREA until it captures.\n\n" +
	      "Re-run THIS script after each zoom/pan change, so the recorded zoom\n" +
	      "always matches the capture it is compared against.");

})();
