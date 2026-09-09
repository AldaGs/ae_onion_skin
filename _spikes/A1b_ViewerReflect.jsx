/*
	A1b_ViewerReflect.jsx (Onion Skin) - throwaway probe for Phase 0, spike A1.

	THE QUESTION:

	  Does AE's SCRIPTING object model expose comp viewer zoom or scroll offset,
	  when neither AEGP nor the OS window tree does?

	Where we are. AEGP_ItemViewSuite1 has exactly one call and it returns
	playback time. A1a walked AE's whole window tree (149 windows) and found no
	OS-readable control anywhere in AE's native UI - every non-Drover window
	belongs to a CEP extension, an expression editor, or a stray Edit box. So
	AE draws its own magnification popup, and there is nothing to read.

	ExtendScript is the last analytic hope, and it is worth one script because
	AEGP can call it (AEGP_ExecuteScript) - so anything findable here is
	reachable from the plug-in.

	This does not guess property names. It REFLECTS, so the answer is what the
	object model actually has, not what it is remembered to have.

	How to run:
	    AE > File > Scripts > Run Script File... > pick this file
	  (needs Preferences > Scripting & Expressions > Allow Scripts to Write
	   Files and Access Network, only for the log file; the alert works anyway)

	Run it with a COMP OPEN and the comp viewer FOCUSED, at a magnification
	that is NOT 100% - so that a property holding the zoom is holding a
	distinctive value we can recognise on sight.

	It reads. It changes nothing.
*/

(function () {

	var out = [];
	function say(s) { out.push(String(s)); }

	function reflectOn(label, obj) {
		say("");
		say("=== " + label + " ===");
		if (obj === null || obj === undefined) {
			say("  <null or undefined>");
			return;
		}
		say("  typeof: " + typeof obj);
		try { say("  toString: " + obj.toString()); } catch (e) { say("  toString threw: " + e); }

		var props;
		try {
			props = obj.reflect.properties;
		} catch (e) {
			say("  reflect threw: " + e);
			return;
		}

		say("  " + props.length + " properties:");
		for (var i = 0; i < props.length; i++) {
			var name = props[i].name;
			if (name === "__proto__" || name === "reflect") continue;

			var val;
			try {
				val = obj[name];
				if (val === null)            val = "<null>";
				else if (val === undefined)  val = "<undefined>";
				else if (typeof val === "object") {
					//	Show the type, and the value too if it is short - a zoom
					//	is likely a plain number, but say so either way.
					val = "[object] " + val.toString();
				}
			} catch (e) {
				val = "<threw: " + e + ">";
			}
			say("    " + name + " = " + val);
		}

		var meths;
		try {
			meths = obj.reflect.methods;
			var mn = [];
			for (var j = 0; j < meths.length; j++) mn.push(meths[j].name);
			say("  methods: " + mn.join(", "));
		} catch (e) { /* some objects have none */ }
	}

	say("A1b viewer reflection - " + new Date().toString());
	say("AE version: " + app.version);

	/* ---------------------------------------------------------------- */

	var v = null;
	try { v = app.activeViewer; } catch (e) { say("app.activeViewer threw: " + e); }

	if (!v) {
		say("");
		say("NO ACTIVE VIEWER. Click the comp viewer and run again.");
	} else {
		reflectOn("app.activeViewer", v);

		//	activeViewer.type tells us whether this is even the comp viewer.
		try { say("\nviewer type: " + v.type); } catch (e) {}

		var views = null;
		try { views = v.views; } catch (e) { say("v.views threw: " + e); }

		if (views) {
			say("\nviews.length = " + views.length);
			for (var i = 0; i < views.length; i++) {
				reflectOn("views[" + i + "]", views[i]);
				try {
					reflectOn("views[" + i + "].options", views[i].options);
				} catch (e) {
					say("views[" + i + "].options threw: " + e);
				}
			}
		}
		try { reflectOn("activeViewer.activeViewIndex holder", v.views[v.activeViewIndex]); } catch (e) {}
	}

	/* ---------------------------------------------------------------- */
	/*  What we are hunting for, stated so the reader can scan for it    */
	/* ---------------------------------------------------------------- */
	say("");
	say("================ HOW TO READ THIS ================");
	say("Scan the property dumps for anything holding:");
	say("  - the current magnification (e.g. 0.5, 50, '50%')");
	say("  - a scroll / pan offset (a pair of numbers, comp pixels or screen)");
	say("  - a rect or size for the drawn image inside the panel");
	say("");
	say("FOUND -> the analytic route for A1 is ALIVE: AEGP can reach it via");
	say("         AEGP_ExecuteScript, and A1 becomes cheap.");
	say("NOT FOUND -> all three analytic sources are now exhausted (AEGP,");
	say("         the OS window tree, ExtendScript). A1 falls to the");
	say("         EMPIRICAL route, or Option A fails its gate.");
	say("");
	say("Absence here is only as good as the state AE was in: this must be");
	say("run with a comp open, the comp viewer focused, at a magnification");
	say("other than 100%.");

	/* ---------------------------------------------------------------- */

	var text = out.join("\n");

	//	Write next to this script, so the result survives the alert.
	try {
		var here = new File($.fileName).parent;
		var f = new File(here.fsName + "/A1b_result.txt");
		f.encoding = "UTF-8";
		if (f.open("w")) { f.write(text); f.close(); }
	} catch (e) { /* alert still carries it */ }

	alert(text.length > 3500 ? text.substr(0, 3500) + "\n\n[...truncated - see A1b_result.txt]" : text);

})();
