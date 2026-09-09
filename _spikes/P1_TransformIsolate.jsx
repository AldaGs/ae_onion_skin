/*
	P1_TransformIsolate.jsx - does a layer-applied effect see TRANSFORM animation?

	The question that decides Phase 2's architecture. AE applies layer transforms
	AFTER effects, so an effect on a layer should see an unchanging raster when
	only Position is animated - every ghost would then receive the current
	frame's transform and land exactly on top of the others, invisibly.

	Run 3's test layer animated BOTH its transform and its shape path, so the
	ghosts it produced prove nothing about which one was responsible. This comp
	separates them.

	FOUR LAYERS, one variable each:

	  A_TransformOnly   position keyframes, shape never changes
	  B_PathOnly        shape scales in place, position never changes
	  C_TransformOnly_Adj   same art as A, ghosted from an ADJUSTMENT layer
	  D_PathOnly_Adj        same art as B, ghosted from an ADJUSTMENT layer

	A and B carry the effect themselves. C and D are ghosted by an adjustment
	layer above them, which sees comp space AFTER transforms.

	THE BROKEN CONTROL is built in: B is the case that must work whatever the
	answer, because its art genuinely changes in layer space. If B shows no
	ghosts either, the test rig is wrong and A's result means nothing.

	WHAT EACH OUTCOME MEANS

	  A ghosts + B ghosts   -> transforms ARE visible to a layer effect. My claim
	                           is wrong, the simple placement works, and Phase 2
	                           needs no new model.
	  A blank  + B ghosts   -> transforms are invisible to a layer effect, as
	                           predicted. Character animation cannot be ghosted
	                           this way and the adjustment-layer route is the
	                           only one left.
	  A blank  + B blank    -> the rig is broken; ignore the run.

	  C and D then say whether the adjustment layer sees them, over an OPAQUE
	  background - which is the case that motivated everything after run 1.

	Run:  File > Scripts > Run Script File...  then look at frame 12.
*/

(function () {
	var FX_MATCH = "aldai OnionSkin";
	var W = 960, H = 540, DUR = 3, FPS = 24;

	app.beginUndoGroup("Onion Skin transform isolation");

	var comp = app.project.items.addComp("OS_TransformIsolate", W, H, 1, DUR, FPS);
	comp.openInViewer();

	//	Opaque background. Deliberate: it is half the problem, and a test comp
	//	without it would pass for the wrong reason.
	var bg = comp.layers.addSolid([0.10, 0.10, 0.12], "BG_Opaque", W, H, 1);
	bg.moveToEnd();

	function makeSquare(name, x, y, color) {
		var L = comp.layers.addShape();
		L.name = name;
		var grp = L.property("Contents").addProperty("ADBE Vector Group");
		var cont = grp.property("Contents");
		var rect = cont.addProperty("ADBE Vector Shape - Rect");
		rect.property("ADBE Vector Rect Size").setValue([90, 90]);
		var fill = cont.addProperty("ADBE Vector Graphic - Fill");
		fill.property("ADBE Vector Fill Color").setValue(color);
		L.property("Transform").property("Position").setValue([x, y]);
		return { layer: L, rect: rect };
	}

	//	--- A: position animates, shape never changes -----------------------
	var A = makeSquare("A_TransformOnly", 150, 150, [0.95, 0.95, 0.90, 1]);
	var pA = A.layer.property("Transform").property("Position");
	pA.setValueAtTime(0,    [150, 150]);
	pA.setValueAtTime(1.0,  [810, 150]);

	//	--- B: shape scales in place, position never changes ----------------
	var B = makeSquare("B_PathOnly", 150, 390, [0.95, 0.95, 0.90, 1]);
	var sB = B.rect.property("ADBE Vector Rect Size");
	sB.setValueAtTime(0,   [40, 40]);
	sB.setValueAtTime(1.0, [220, 220]);

	//	--- C and D: same art, ghosted from an adjustment layer -------------
	var C = makeSquare("C_TransformOnly_Adj", 480, 150, [0.55, 0.85, 1.0, 1]);
	var pC = C.layer.property("Transform").property("Position");
	pC.setValueAtTime(0,   [480, 260]);
	pC.setValueAtTime(1.0, [480,  60]);

	var D = makeSquare("D_PathOnly_Adj", 700, 390, [0.55, 0.85, 1.0, 1]);
	var sD = D.rect.property("ADBE Vector Rect Size");
	sD.setValueAtTime(0,   [40, 40]);
	sD.setValueAtTime(1.0, [220, 220]);

	function applyOnion(layer) {
		var fx;
		try {
			fx = layer.property("ADBE Effect Parade").addProperty(FX_MATCH);
		} catch (e) {
			alert("Could not apply '" + FX_MATCH + "'.\n" +
					"Is onionSkin.aex installed?\n\n" + e.toString());
			return null;
		}
		//	Named rather than indexed: disk IDs are append-only and an index
		//	would silently drift the day a param is added.
		try { fx.property("Previous Frames").setValue(4); } catch (e) {}
		try { fx.property("Next Frames").setValue(4); } catch (e) {}
		try { fx.property("Strength").setValue(70); } catch (e) {}
		try { fx.property("Falloff").setValue(75); } catch (e) {}
		return fx;
	}

	applyOnion(A.layer);
	applyOnion(B.layer);

	//	The adjustment layer sits above everything, so it sees comp space with
	//	all transforms already applied - and an opaque background with them.
	var adj = comp.layers.addSolid([1, 1, 1], "ONION_Adjustment", W, H, 1);
	adj.adjustmentLayer = true;
	adj.moveToBeginning();
	applyOnion(adj);

	comp.time = 12 / FPS;

	app.endUndoGroup();

	alert("OS_TransformIsolate built. Go to frame 12 and look.\n\n" +
			"A_TransformOnly  (white, top-left)   effect ON THE LAYER, position animates\n" +
			"B_PathOnly       (white, bottom-left) effect ON THE LAYER, shape animates\n" +
			"C / D            (blue)               ghosted by the adjustment layer\n\n" +
			"B is the control: if B shows no ghosts the rig is broken and A proves nothing.\n\n" +
			"Report: which of A, B, C, D show ghosts.");
})();
