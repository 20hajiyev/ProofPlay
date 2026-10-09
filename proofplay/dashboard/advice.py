"""Turns a ProofPlay report into the player's list of main mistakes, each explained in plain
English: what happened, the evidence, why it costs time, and what to do. Deterministic templates
over measured numbers - nothing here is guessed, so nothing can be hallucinated.

Each item: {id, title, impact_s (None = not timed), source: proof | loss | consistency | stats,
            evidence: [str], explanation: str, action: str, map: {type, index} | None,
            where: {x, z} | None}
"""


def kmh(ms):
    return round(ms * 3.6)


def ordinal(n):
    return f"{n}{'th' if 10 <= n % 100 <= 20 else {1: 'st', 2: 'nd', 3: 'rd'}.get(n % 10, 'th')}"


ACTION = {  # (short instruction, the change in a sentence, what went wrong)
    "brake_later": ("Brake {m} m later", "braking {m} m later", "you braked too early"),
    "brake_earlier": ("Brake {m} m earlier", "braking {m} m earlier", "you braked too late"),
    "lift_later": ("Stay on the gas {m} m longer", "lifting off {m} m later", "you lifted off the gas too early"),
    "lift_earlier": ("Lift off {m} m earlier", "lifting off {m} m earlier", "you lifted off too late"),
    "add_brake": ("Tap the brake {m} m before the corner", "a short brake {m} m before the corner", "you went in flat out and too fast"),
}


def corner_of(report, corner_id):
    return next((c for c in report.get("corners", []) if c["id"] == corner_id), None)


def pass_of(corner, lap):
    if not corner:
        return None
    return next((p for p in corner["passes"] if p["lap"] == lap), None)


def proof_items(report):
    items = []
    for i, p in enumerate(report.get("proofs", [])):
        c = corner_of(report, p["corner"])
        ps = pass_of(c, p["lap"])
        short, cond, wrong = ACTION.get(p["kind"], (p["kind"], p["kind"], "the timing was off"))
        m = int(p["amount_m"])
        short, cond = short.format(m=m), cond.format(m=m)
        evidence = [f"{p['sims']} test runs", f"+{p['gain_s']:.2f} s"]
        text = f"On lap {p['lap']}, {wrong} for corner {p['corner']}. "
        if ps and all(k in ps for k in ("entry_speed", "min_speed", "exit_speed")):  # older reports lack these
            text += (f"You went in at {kmh(ps['entry_speed'])} km/h, slowed to {kmh(ps['min_speed'])} km/h "
                     f"and came out at {kmh(ps['exit_speed'])} km/h. ")
            evidence.append(f"{kmh(ps['entry_speed'])} → {kmh(ps['min_speed'])} → {kmh(ps['exit_speed'])} km/h")
        text += (f"We re-ran your exact race in the game with one change, {cond}. "
                 f"You came out {abs(p['exit_speed_gain'] * 3.6):.0f} km/h {'faster' if p['exit_speed_gain'] >= 0 else 'slower'} and were {p['gain_s']:.2f} s ahead by the next straight. ")
        lo, hi = p.get("window_lo_m"), p.get("window_hi_m")
        if p.get("robust") and lo is not None and hi and hi > lo:
            text += f"You do not have to be exact: anything from {int(lo)} to {int(hi)} m also saved time in our tests."
            evidence.append(f"works {int(lo)}-{int(hi)} m")
        elif "robust" in p and not p["robust"]:
            text += "Careful: this only paid off at this exact distance. A few metres either way did not help, so treat it as a hint."
            evidence.append("exact point only")
        items.append({
            "id": f"proof{i}", "source": "proof", "impact_s": p["gain_s"], "robust": p.get("robust"),
            "title": f"Corner {p['corner']}: {short.lower()}",
            "evidence": evidence, "explanation": text.strip(),
            "action": f"Next race: {short.lower()} into corner {p['corner']}. The map shows your line and the faster one.",
            "map": {"type": "proof", "index": i},
            "where": {"x": c["x"], "z": c["z"]} if c else None,
        })
    return items


CAUSE = {
    "stopped": ("Your car almost stopped here (down to {v:.0f} km/h). That usually means you hit something, "
                "got stuck on a wall, or spun.",
                "Leave space when cars bunch up before a corner. If you get stuck, reverse out right away.",
                "stopped"),
    "off_road": ("You went {m:.1f} m off the track. The car grips less off the road, so it slows down.",
                 "Go into the corner a little slower and press the gas gently on the way out.",
                 "ran wide"),
    "wreck": ("Your car was wrecked here, and waiting to respawn cost time.",
              "Use Mend when your health is low, and use Ward when an attack is coming.",
              "wrecked"),
    "start": ("This part includes the start and the crowded first corners.",
              "At the start, keep your own line to the first corner instead of fighting the cars next to you.",
              "start"),
}


def loss_items(report):
    items = []
    for i, g in enumerate(report.get("losses", [])):
        texts, actions = [], []
        evidence = [f"−{g['loss_s']:.1f} s vs your best lap", f"{int(g['from_m'])}-{int(g['to_m'])} m"]
        for c in g["causes"]:
            if c["kind"] == "hit":
                times = "once" if c["count"] == 1 else f"{c['count']} times"
                texts.append(f"You were hit by {c['ability']} {times}. Each hit slows you down and pushes you off your line.")
                actions.append("When the attack warning shows, use Ward or move to another lane.")
                evidence.append(f"{c['count']}× {c['ability']}")
            elif c["kind"] in CAUSE:
                what, todo, tag = CAUSE[c["kind"]]
                texts.append(what.format(v=c.get("min_speed_kmh", 0), m=c.get("metres", 0)))
                actions.append(todo)
                evidence.append(tag)
        if not texts:
            texts.append("Nothing went wrong here, no hit, no stop, no going off. You were just slower than on your best lap, "
                         "most likely because you braked earlier or pressed the gas later.")
            actions.append("Drive this part like on your best lap: brake at the same spot and press the gas earlier on the way out.")
        items.append({
            "id": f"loss{i}", "source": "loss", "impact_s": g["loss_s"],
            "title": f"Lap {g['lap']}, {int(g['from_m'])}‑{int(g['to_m'])} m: {g['loss_s']:.1f} s lost",  # non-breaking hyphen
            "evidence": evidence,
            "explanation": " ".join(texts) + f" Compared with your own best lap, this part cost you {g['loss_s']:.1f} s.",
            "action": " ".join(dict.fromkeys(actions)),
            "map": {"type": "loss", "index": i},
            "where": {"x": g["x"], "z": g["z"]},
        })
    return items


def consistency_items(report):
    items = []
    for c in report.get("corners", []):
        if len(c["passes"]) < 2:
            continue
        if any("min_speed" not in p for p in c["passes"]):
            continue  # report from before per-pass speeds were recorded
        losses = [p["loss_s"] for p in c["passes"]]
        spread = max(losses) - min(losses)
        if spread < 0.6:
            continue
        worst = max(c["passes"], key=lambda p: p["loss_s"])
        best = min(c["passes"], key=lambda p: p["loss_s"])
        items.append({
            "id": f"cons{c['id']}", "source": "consistency", "impact_s": spread,
            "title": f"Corner {c['id']}: different every lap",
            "evidence": [f"best on lap {best['lap']}", f"worst on lap {worst['lap']}", f"{spread:.1f} s apart"],
            "explanation": (f"You took corner {c['id']} at {kmh(best['min_speed'])} km/h on lap {best['lap']} but only "
                            f"{kmh(worst['min_speed'])} km/h on lap {worst['lap']}. Same corner, {spread:.1f} s apart. "
                            "This usually means you braked at a different spot or took a different line each time."),
            "action": f"Copy your lap {best['lap']}: brake at the same spot and take the same line every lap.",
            "map": None,
            "where": {"x": c["x"], "z": c["z"]},
        })
    return items


def stats_items(quick):
    if not quick:
        return []
    items = []
    uses, hits = quick.get("uses", 0), quick.get("hits", 0)
    if uses >= 3 and hits / uses < 0.3:
        items.append({
            "id": "stat_aim", "source": "stats", "impact_s": None,
            "title": f"Weapons missing: {hits} of {uses} hit",
            "evidence": [f"{uses} used", f"{hits} hits", f"{hits / uses:.0%}"],
            "explanation": (f"You fired {uses} times and hit {hits}. Every miss is a weapon you could have used "
                            "to slow a rival or to protect yourself."),
            "action": "Fire Lance only when a rival is right ahead of you on a straight. Otherwise keep it for defence.",
            "map": None, "where": None,
        })
    if quick.get("damage_taken", 0) >= 100:
        items.append({
            "id": "stat_dmg", "source": "stats", "impact_s": None,
            "title": f"Heavy damage taken: {quick['damage_taken']:.0f}",
            "evidence": [f"{quick['damage_taken']:.0f} damage", f"{quick.get('wrecks', 0)} wrecks"],
            "explanation": "Damage makes the car slower, and a wreck costs seconds while you wait to respawn.",
            "action": "Keep Ward for attacks from behind and use Mend before your health runs out.",
            "map": None, "where": None,
        })
    if quick.get("resets", 0):
        items.append({
            "id": "stat_reset", "source": "stats", "impact_s": None,
            "title": f"{quick['resets']} manual reset{'s' if quick['resets'] > 1 else ''}",
            "evidence": [f"{quick['resets']} resets"],
            "explanation": "A reset puts you back on the track but costs a few seconds and usually a place.",
            "action": "When stuck, try reversing out first. Use the reset only as a last resort.",
            "map": None, "where": None,
        })
    return items


def quick_hints(quick):
    """Stage 1 notes straight from the results screen, before any replay analysis."""
    if not quick:
        return []
    hints = []
    pos, n = quick.get("position", 0), quick.get("participants", 0)
    if pos == 1:
        hints.append(("good", f"Won the race, {pos} of {n}."))
    elif pos and n and pos == n:
        hints.append(("bad", f"Finished last, {pos} of {n}."))
    uses, hits = quick.get("uses", 0), quick.get("hits", 0)
    if uses and hits / uses < 0.3:
        hints.append(("bad", f"Most shots missed: {hits} of {uses} hit ({hits / uses:.0%})."))
    elif uses and hits / uses >= 0.5:
        hints.append(("good", f"Accurate shooting: {hits} of {uses} hit ({hits / uses:.0%})."))
    if quick.get("damage_taken", 0) >= 100:
        hints.append(("bad", f"Took heavy damage ({quick['damage_taken']:.0f})."))
    spread = quick.get("lap_spread_s", 0)
    if spread >= 3.0:
        hints.append(("bad", f"Lap times vary by {spread:.1f} s."))
    elif len(quick.get("lap_times", [])) > 2:
        hints.append(("good", f"Consistent laps, within {spread:.1f} s."))
    if quick.get("wrecks", 0):
        hints.append(("bad", f"Wrecked {quick['wrecks']} time{'s' if quick['wrecks'] > 1 else ''}."))
    if quick.get("wrecks_caused", 0):
        hints.append(("good", f"Wrecked {quick['wrecks_caused']} rival{'s' if quick['wrecks_caused'] > 1 else ''}."))
    return [{"tone": t, "text": x} for t, x in hints]


def build_advice(report):
    timed = proof_items(report) + loss_items(report) + consistency_items(report)
    # Proven fixes always stay (the plan may combine a small one); measured losses need 0.05 s.
    timed = [a for a in timed if a["source"] == "proof" or (a["impact_s"] or 0) >= 0.05]
    timed.sort(key=lambda a: -(a["impact_s"] or 0))
    return timed[:12] + stats_items(report.get("quick"))


def consistency_check(quick, report):
    """The game's own results-screen numbers against what the replay measured."""
    if not quick:
        return None
    res, cb = report["result"], report["combat"]
    pairs = [
        ("position", quick["position"], res["position"]),
        ("time", round(quick["time_s"], 3), round(res["time_s"], 3)),
        ("lap times", [round(x, 2) for x in quick["lap_times"]], [round(x, 2) for x in res.get("lap_times", [])]),
        ("weapon uses", quick["uses"], cb["uses"]),
        ("hits", quick["hits"], cb["hits"]),
        ("damage", round(quick["damage_taken"], 1), round(cb["damage_taken"], 1)),
    ]
    return {"checked": len(pairs), "matched": sum(a == b for _, a, b in pairs),
            "rows": [{"name": n, "game": a, "replay": b, "ok": a == b} for n, a, b in pairs]}
