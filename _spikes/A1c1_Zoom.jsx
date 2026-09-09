/*
	A1c1_Zoom.jsx (Onion Skin) - Phase 0, spike A1c.

	Records the viewer zoom at this instant, appending one line to
	A1c1_zoom_log.txt.

	Run this IMMEDIATELY BEFORE each os_A1c1.exe capture. The solver pairs the
	nth zoom line with the nth capture, so the two must be made in lockstep -
	one zoom reading, one capture, then change the view and repeat.

	Separate from A1c1_MakeCalib.jsx on purpose: rebuilding the comp just to
	read a number would churn the project, and a rebuild between captures is
	exactly the kind of uncontrolled change that makes a measurement series
	untrustworthy.
*/

(function () {

	var v = app.activeViewer;
	if (!v) { alert("No active viewer. Click the comp viewer and retry."); return; }

	var view = v.views[v.activeViewIndex];
	var zoom = view.options.zoom;

	var comp = app.project.activeItem;
	var cname = (comp && comp instanceof CompItem) ? comp.name : "<not a comp>";

	var here = new File($.fileName).parent;
	var f = new File(here.fsName + "/A1c1_zoom_log.txt");
	f.encoding = "UTF-8";

	//	Count existing entries so the line numbers itself - the solver pairs on
	//	this index, and a mislabelled pair is a silently wrong result.
	var n = 1;
	if (f.exists && f.open("r")) {
		var body = f.read(); f.close();
		var lines = body.split("\n");
		for (var i = 0; i < lines.length; i++) if (/^\d+\t/.test(lines[i])) n++;
	}

	//	Log the comp GEOMETRY too, not just the name.
	//
	//	A1c1_calib.json holds only whichever comp MakeCalib built last, and a run
	//	that uses more than one comp size overwrites it. In run 2 that made the
	//	solver judge the four 640x360 captures against 1920x1080 marker
	//	coordinates - a factor of exactly 3 - and report a 66% zoom disagreement
	//	for what was purely bookkeeping. Geometry belongs with each reading,
	//	because that is the only place it is unambiguous.
	var cw = (comp && comp instanceof CompItem) ? comp.width : 0;
	var ch = (comp && comp instanceof CompItem) ? comp.height : 0;
	var cp = (comp && comp instanceof CompItem) ? comp.pixelAspect : 0;

	if (f.open("a")) {
		f.write(n + "\t" + zoom + "\t" + v.type + "\t" + cname + "\t" +
		        cw + "\t" + ch + "\t" + cp + "\t" +
		        new Date().toString() + "\n");
		f.close();
	}

	alert("zoom reading " + n + "\n\n" +
	      "zoom = " + zoom + "\ncomp = " + cname + "\n\n" +
	      "NOW run os_A1c1.exe and hover over the viewer image area.\n" +
	      "This must become capture " + n + " - keep them in lockstep.");

})();
